#include "PrinterFileSystem.h"
#include "libslic3r/Utils.hpp"

#include <boost/algorithm/hex.hpp>
#include <boost/uuid/detail/md5.hpp>

#include "nlohmann/json.hpp"

#include <cstring>

wxDEFINE_EVENT(EVT_READY, wxCommandEvent);
wxDEFINE_EVENT(EVT_MODE_CHANGED, wxCommandEvent);
wxDEFINE_EVENT(EVT_FILE_CHANGED, wxCommandEvent);
wxDEFINE_EVENT(EVT_THUMBNAIL, wxCommandEvent);
wxDEFINE_EVENT(EVT_DOWNLOAD, wxCommandEvent);

wxDEFINE_EVENT(EVT_FILE_CALLBACK, wxCommandEvent);

static wxBitmap default_thumbnail;

PrinterFileSystem::PrinterFileSystem(std::string const &url)
    : url(url)
    , recv_thread(&PrinterFileSystem::RecvMessageThread, this)
{
    if (!default_thumbnail.IsOk())
        default_thumbnail= wxImage(Slic3r::encode_path(Slic3r::var("live_stream_default.png").c_str()));
    session.owner = this;
    session.logger = &PrinterFileSystem::DumpLog;
    //auto time = wxDateTime::Now();
    //for (int i = 0; i < 240; ++i) {
    //    files.push_back({"", time.GetTicks(), 0, default_thumbnail, false, i - 130});
    //    time.Add(wxDateSpan::Days(-1));
    //}
    //BuildGroups();
}

PrinterFileSystem::~PrinterFileSystem()
{
    stop = true;
    recv_thread.join();
}

void PrinterFileSystem::SetGroupMode(GroupMode mode)
{
    this->mode = mode;
    SendChangedEvent(EVT_MODE_CHANGED);
}

size_t PrinterFileSystem::EnterSubGroup(size_t index)
{
    if (mode == G_NONE)
        return index;
    index = mode == G_YEAR ? group_year[index] : group_month[index];
    mode = (GroupMode)(mode - 1);
    SendChangedEvent(EVT_MODE_CHANGED);
    return index;
}

void PrinterFileSystem::ListAllFiles()
{
    json req;
    req["notify"] = "DETAIL";
    SendRequest<FileList>(LIST_INFO, req, [this](json const& resp, FileList & list, auto) {
        json files = resp["file_lists"];
        for (auto& f : files) {
            std::string name = f["name"];
            boost::uint32_t time = 0; // f["time"];
            boost::uint64_t size = f["size"];
            File ff = {name, time, size, default_thumbnail};
            list.push_back(ff);
        }
        return 0;
    }, [this](int result, FileList const & list) {
        files = std::move(list);
        BuildGroups();
        SendChangedEvent(EVT_FILE_CHANGED);
        SetFocusRange(0, files.size());
        return 0;
    });
}

void PrinterFileSystem::DeleteFiles(size_t index)
{
    if (index == size_t(-1)) {
        size_t n = 0;
        for (size_t i = 0; i < files.size(); ++i) {
            auto &file = files[i];
            if ((file.flags & FF_SELECT) != 0 && (file.flags & FF_DELETED) == 0) {
                file.flags |= FF_DELETED;
                ++n;
            }
            if (n == 0) return;
        }
    } else {
        if (index >= files.size())
            return;
        auto &file = files[index];
        if ((file.flags & FF_DELETED) != 0)
            return;
        file.flags |= FF_DELETED;
    }
    if ((task_flags & FF_DELETED) == 0)
        DeleteFilesContinue();
}

void PrinterFileSystem::DownloadFiles(size_t index, std::string const &path)
{
    if (index == (size_t) -1) {
        size_t n = 0;
        for (size_t i = 0; i < files.size(); ++i) {
            auto &file = files[i];
            if ((file.flags & FF_SELECT) == 0) continue;
            if ((file.flags & FF_DOWNLOAD) != 0 && file.progress >= 0) continue;
            file.flags |= FF_DOWNLOAD;
            file.progress = -1;
            ++n;
        }
        if (n == 0) return;
    } else {
        if (index >= files.size())
            return;
        auto &file = files[index];
        if ((file.flags & FF_DOWNLOAD) != 0 && file.progress >= 0)
            return;
        file.flags |= FF_DOWNLOAD;
        file.progress = -1;
    }
    if ((task_flags & FF_DOWNLOAD) == 0)
        DownloadNextFile(path);
}

size_t PrinterFileSystem::GetCount() const
{
    if (mode == G_NONE)
        return files.size();
    return mode == G_YEAR ? group_year.size() : group_month.size();
}

size_t PrinterFileSystem::GetIndexAtTime(boost::uint32_t time)
{
    auto iter = std::upper_bound(files.begin(), files.end(), File{"", time});
    size_t n = std::distance(files.begin(), iter) - 1;
    if (mode == G_NONE) {
        return n;
    }
    auto & group = mode == G_YEAR ? group_year : group_month;
    auto iter2 = std::upper_bound(group.begin(), group.end(), n);
    return std::distance(group.begin(), iter2) - 1;
}

void PrinterFileSystem::ToggleSelect(size_t index)
{
    if (index < files.size()) files[index].flags ^= FF_SELECT;
}

void PrinterFileSystem::SelectAll(bool select)
{
    if (select)
        for (auto &f : files) f.flags |= FF_SELECT;
    else
        for (auto &f : files) f.flags &= ~FF_SELECT;
}

void PrinterFileSystem::SetFocusRange(size_t start, size_t count)
{
    if (lock_start == start && lock_end == start + count)
        return;
    lock_start = start;
    lock_end = start + count;
    UpdateFocusThumbnail();
}

PrinterFileSystem::File const &PrinterFileSystem::GetFile(size_t index)
{
    if (mode == G_NONE)
        return files[index];
    if (mode == G_YEAR)
        index = group_year[index];
    return files[group_month[index]];
}

int PrinterFileSystem::RecvData(std::function<int(Bambu_Sample& sample)> const & callback)
{
    int result = 0;
    while (true) {
        Bambu_Sample sample;
        result = Bambu_ReadSample(&session, &sample);
        if (result == Bambu_success) {
            result = callback(sample);
            if (result == 1)
                continue;
        } else if (result == Bambu_would_block) {
            boost::this_thread::sleep(boost::posix_time::seconds(1));
            continue;
        } else if (result == Bambu_stream_end) {
            result = 0;
        } else {
            result = ERROR_PIPE;
        }
        break;
    }
    return result;
}

void PrinterFileSystem::BuildGroups()
{
    if (files.empty())
        return;
    wxDateTime t = wxDateTime((time_t) files.front().time);
    group_year.push_back(0);
    group_month.push_back(0);
    for (size_t i = 0; i < files.size(); ++i) {
        wxDateTime s = wxDateTime((time_t) files[i].time);
        if (s.GetYear() != t.GetYear()) {
            group_year.push_back(group_month.size());
            group_month.push_back(i);
        } else if (s.GetMonth() != t.GetMonth()) {
            group_month.push_back(i);
        }
        t = s;
    }
}

void PrinterFileSystem::DeleteFilesContinue()
{
    std::vector<size_t> indexes;
    std::vector<std::string> names;
    for (size_t i = 0; i < files.size(); ++i)
        if ((files[i].flags & FF_SELECT) && !files[i].name.empty()) {
            indexes.push_back(i);
            names.push_back(files[i].name);
        }
    task_flags &= ~FF_DELETED;
    if (names.empty())
        return;
    json req;
    json arr;
    for (auto &name : names) arr.push_back(name);
    req["delete"] = arr;
    task_flags |= FF_DELETED;
    SendRequest<Void>(
        FILE_DEL, req, nullptr, 
        [indexes, names, this](int, Void const &) {
            // TODO:
            for (size_t i = indexes.size() - 1; i >= 0; --i)
                FileRemoved(indexes[i], names[i]);
            SendChangedEvent(EVT_FILE_CHANGED);
            DeleteFilesContinue();
        });
}

void PrinterFileSystem::DownloadNextFile(std::string const &path)
{
    size_t index = size_t(-1);
    for (size_t i = 0; i < files.size(); ++i) {
        if ((files[i].flags & FF_DOWNLOAD) != 0 && files[i].progress == -1) {
            index = i;
            break;
        }
    }
    task_flags &= ~FF_DOWNLOAD;
    if (index >= files.size())
        return;
    json req;
    req["file"] = files[index].name;
    files[index].progress = 0;
    SendChangedEvent(EVT_DOWNLOAD, index, files[index].name);
    struct Download
    {
        int                       index;
        std::string               name;
        std::string               path;
        std::ofstream             ofs;
        boost::uuids::detail::md5 boost_md5;
    };
    std::shared_ptr<Download> download(new Download);
    download->index = index;
    download->name  = files[index].name;
    download->path  = path;
    task_flags |= FF_DOWNLOAD;
    SendRequest<Progress>(
        FILE_DOWNLOAD, req,
        [this, download](json const &resp, Progress &prog, unsigned char const *data) -> int {
            // in work thread, continue recv
            size_t size = resp["size"];
            prog.size   = resp["offset"];
            prog.total  = resp["total"];
            if (prog.size == 0) { download->ofs.open(download->path + "/" + download->name, std::ios::binary); }
            // receive data
            download->ofs.write((char const *) data, size);
            download->boost_md5.process_bytes(data, size);
            prog.size += size;
            if (prog.size < prog.total) { return CONTINUE; }
            download->ofs.close();
            int         result = 0;
            std::string md5    = resp["file_md5"];
            // check size and md5
            if (prog.size == prog.total) {
                boost::uuids::detail::md5::digest_type digest;
                download->boost_md5.get_digest(digest);
                for (int i = 0; i < 4; ++i) digest[i] = boost::endian::endian_reverse(digest[i]);
                std::string str_md5;
                const auto  char_digest = reinterpret_cast<const char *>(&digest[0]);
                boost::algorithm::hex(char_digest, char_digest + sizeof(digest), std::back_inserter(str_md5));
                if (!boost::iequals(str_md5, md5)) result = FILE_CHECK_ERR;
            } else {
                result = FILE_SIZE_ERR;
            }
            if (result != 0) {
                boost::system::error_code ec;
                boost::filesystem::remove(download->path, ec);
            }
            return result;
        },
        [this, download](int result, Progress const &data) {
            if (download->index >= 0)
                download->index = FindFile(download->index, download->name);
            if (download->index >= 0) {
                int progress = data.size * 100 / data.total;
                if (result > CONTINUE)
                    progress = -2;
                if (files[download->index].progress != progress) {
                    files[download->index].progress = progress;
                    SendChangedEvent(EVT_DOWNLOAD, download->index, files[download->index].name, data.size);
                }
            }
            if (result != CONTINUE) DownloadNextFile(download->path);
        });
}

void PrinterFileSystem::UpdateFocusThumbnail()
{
    task_flags &= ~FF_THUMNAIL;
    if (lock_start >= files.size() || lock_start >= lock_end)
        return;
    size_t start = lock_start;
    size_t end   = std::min(lock_end, files.size());
    std::vector<std::string> names;
    for (; start < end; ++start) {
        if ((files[start].flags & FF_THUMNAIL) == 0) {
            names.push_back(files[start].name);
            if (names.size() >= 5)
                break;
        }
    }
    if (names.empty())
        return;
    json req;
    json arr;
    for (auto &name : names) arr.push_back(name);
    req["files"] = arr;
    task_flags |= FF_THUMNAIL;
    SendRequest<Thumbnail>(
        THUMBNAIL, req,
        [this](json const &resp, Thumbnail &thumb, unsigned char const *data) -> int {
            // in work thread, continue recv
            // receive data
            wxString            mimetype  = resp["mimetype"];
            auto                thumbnail = resp["thumbnail"];
            auto                size      = resp["size"];
            wxMemoryInputStream mis(data, size);
            wxImage             image(mis, mimetype);
            thumb.name      = thumbnail;
            thumb.thumbnail = image;
            return 0;
        },
        [this](int result, Thumbnail const &thumb) {
            auto n = thumb.name.find_last_of('.');
            auto name = n == std::string::npos ? thumb.name : thumb.name.substr(0, n);
            auto iter = std::find_if(files.begin(), files.end(), [&name](auto &f) { return boost::algorithm::starts_with(f.name, name); });
            if (iter != files.end()) {
                iter->thumbnail = thumb.thumbnail;
                iter->flags |= FF_THUMNAIL;
                int index = iter - files.begin();
                SendChangedEvent(EVT_THUMBNAIL, index, thumb.name);
            }
            if (result != CONTINUE)
                UpdateFocusThumbnail();
        });
}

size_t PrinterFileSystem::FindFile(size_t index, std::string const &name)
{
    if (index >= files.size() || files[index].name != name) {
        auto iter = std::find_if(files.begin(), files.end(), [name](File &f) { return f.name == name; });
        if (iter == files.end()) return -1;
        index = std::distance(files.begin(), iter);
    }
    return index;
}

void PrinterFileSystem::FileRemoved(size_t index, std::string const &name)
{
    index = FindFile(index, name);
    auto removeFromGroup = [](std::vector<size_t> &group, size_t index, int total) {
        for (auto iter = group.begin(); iter != group.end(); ++iter) {
            size_t index2 = -1;
            if (*iter < index) continue;
            if (*iter == index) {
                auto iter2 = iter + 1;
                if (iter2 == group.end() ? index == total - 1 : *iter2 == index + 1) {
                    index2 = std::distance(group.begin(), iter);
                }
                ++iter;
            }
            for (; iter != group.end(); ++iter) {
                --*iter;
            }
            return index2;
        }
        return size_t(-1);
    };
    size_t index2 = removeFromGroup(group_month, index, files.size());
    if (index2 < group_month.size()) {
        int index3 = removeFromGroup(group_year, index, group_month.size());
        if (index3 < group_year.size())
            group_year.erase(group_year.begin() + index3);
        group_month.erase(group_month.begin() + index2);
    }
    files.erase(files.begin() + index);
}

struct CallbackEvent : wxCommandEvent
{
    CallbackEvent(std::function<void(void)> const &callback) : wxCommandEvent(EVT_FILE_CALLBACK), callback(callback) {}
    ~CallbackEvent(){ callback(); }
    std::function<void(void)> const callback;
};

void PrinterFileSystem::PostCallback(std::function<void(void)> const& callback)
{
    wxCommandEvent *e = new CallbackEvent(callback);
    wxQueueEvent(this, e);
}

void PrinterFileSystem::SendChangedEvent(wxEventType type, size_t index, std::string const &str, long extra)
{
    wxCommandEvent event(type);
    event.SetEventObject(this);
    event.SetInt(index);
    if (!str.empty())
        event.SetString(wxString::FromUTF8(str.c_str()));
    event.SetExtraLong(extra);
    ProcessEvent(event);
}

void PrinterFileSystem::DumpLog(Bambu_Session *session, int level, Bambu_Message const *msg)
{
    if (level == 1) {
        wxString msg2(msg);
        if (msg2.EndsWith("]")) {
            auto n = msg2.find_last_of('[');
            if (n != wxString::npos) {
                long val   = 0;
                PrinterFileSystem *thiz = ((Session *) session)->owner;
                if (msg2.SubString(n + 1, msg2.Length() - 2).ToLong(&val))
                    thiz->last_error = val;
            }
        }
    }
    BOOST_LOG_TRIVIAL(info) << "PrinterFileSystem: " << msg;
    Bambu_FreeLogMsg(msg);
}

void PrinterFileSystem::SendRequest(int type, json const &req, callback_t2 const & callback)
{
    boost::unique_lock l(mutex);
    json root;
    root["cmdtype"] = type;
    root["sequence"] = sequence + callbacks.size();
    root["req"] = req;
    std::ostringstream oss;
    oss << root;
    auto msg = oss.str();
    //OutputDebugStringA(msg.c_str());
    //OutputDebugStringA("\n");
    BOOST_LOG_TRIVIAL(info) << "PrinterFileSystem::SendRequest: " << type << " msg: " << msg;
    int result = Bambu_SendMessage(&session, CTRL_TYPE, msg.c_str(), msg.length());
    if (result != 0) {
        callback(ERROR_PIPE, json(), nullptr);
        return;
    }
    callbacks.push_back(callback);
}

void PrinterFileSystem::InstallNotify(int type, callback_t2 const &callback)
{
    type -= NOTIFY_FIRST;
    if (notifies.size() <= size_t(type)) notifies.resize(type + 1);
    notifies[type] = callback;
}

void PrinterFileSystem::RecvMessageThread()
{
    std::string url = this->url; // copy
    int ret = Bambu_Open(&session, url.c_str() + 9); // skip bambu:/// sync
    if (ret == 0) ret = Bambu_StartStream(&session);
    wxCommandEvent event(EVT_READY);
    event.SetInt(ret);
    event.SetEventObject(this);
    wxPostEvent(this, event);
    if (ret != 0)
        return;
    Bambu_Sample sample;
    while (!stop) {
        int n = Bambu_ReadSample(&session, &sample);
        if (n != 0) {
            if (n == Bambu_would_block) {
                boost::this_thread::sleep(boost::posix_time::milliseconds(callbacks.empty() ? 1000 : 20));
                continue;
            }
            json r;
            while (!callbacks.empty()) {
                auto c = callbacks.front();
                callbacks.pop_front();
                ++sequence;
                if (c) c(n, r, nullptr);
            }
            // TODO: reopen
            return;
        }
        unsigned char const *end = sample.buffer + sample.size;
        unsigned char const *json_end = (unsigned char const *) memchr(sample.buffer, '\n', sample.size);
        while (json_end && json_end + 3 < end && json_end[1] != '\n')
            json_end = (unsigned char const *) memchr(json_end + 2, '\n', end - json_end - 2);
        if (json_end) json_end += 2;
        else json_end = end;
        std::string msg((char const *) sample.buffer, json_end - sample.buffer);
        json root;
        //OutputDebugStringA(msg.c_str());
        //OutputDebugStringA("\n");
        std::istringstream iss(msg);
        int cmd = 0;
        int seq = -1;
        int result = 0;
        json resp;
        try {
            iss >> root;
            if (!root["result"].is_null()) {
                result = root["result"];
                seq = root["sequence"];
                resp = root["reply"];
            } else {
                // maybe notify
                cmd  = root["cmdtype"];
                seq = root["sequence"];
                resp = root["notify"];
            }
        }
        catch (...) {
            result = ERROR_JSON;
            continue;
        }
        if (cmd > 0) {
            if (cmd < NOTIFY_FIRST) continue;
            cmd -= NOTIFY_FIRST;
            if (size_t(cmd) >= notifies.size()) continue;
            auto n = notifies[cmd];
            n(result, resp, json_end);
        } else {
            seq -= sequence;
            if (size_t(seq) >= callbacks.size())
                continue;
            auto c = callbacks[seq];
            if (c == nullptr)
                continue;
            if (result != CONTINUE) {
                boost::unique_lock l(mutex);
                callbacks[seq] = callback_t2();
                if (seq == 0) {
                    while (!callbacks.empty() && callbacks.front() == nullptr) {
                        callbacks.pop_front();
                        ++sequence;
                    }
                }
            }
            c(result, resp, json_end);
        }
    } // while
    Bambu_Close(&session);
}

