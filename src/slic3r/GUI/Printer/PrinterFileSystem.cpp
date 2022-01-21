#include "PrinterFileSystem.h"
#include "libslic3r/Utils.hpp"

#include <boost/algorithm/hex.hpp>
#include <boost/uuid/detail/md5.hpp>
#include <cstring>

wxDEFINE_EVENT(EVT_FILE_CALLBACK, wxCommandEvent);
wxDEFINE_EVENT(EVT_FILE_CHANGED, wxCommandEvent);

static wxBitmap default_thumbnail;

PrinterFileSystem::PrinterFileSystem(std::string const &url, void *logger)
    : url(url), recv_thread(&PrinterFileSystem::RecvMessageThread, this)
{
    if (!default_thumbnail.IsOk())
        default_thumbnail= wxImage(Slic3r::var("live_stream_default.png"));
    session.logger = logger;
    auto time = wxDateTime::Now();
    for (int i = 0; i < 240; ++i) {
        files.push_back({"", time.GetTicks(), 0, default_thumbnail});
        time.Add(wxDateSpan::Days(-1));
    }
    BuildGroups();
}

PrinterFileSystem::~PrinterFileSystem()
{
    stop = true;
    recv_thread.join();
}

void PrinterFileSystem::SetGroupMode(GroupMode mode)
{
    this->mode = mode;
    SendChangedEvent();
}

int PrinterFileSystem::EnterSubGroup(int index)
{
    if (mode == G_NONE)
        return index;
    index = mode == G_YEAR ? group_year[index] : group_month[index];
    mode = (GroupMode)(mode - 1);
    SendChangedEvent();
    return index;
}

void PrinterFileSystem::ListAllFiles(PrinterFileSystem::Callback<Void> const &callback)
{
    pt::ptree req;
    SendRequest<Void, FileList>(FILE_LIST_ALL, req, [this](pt::ptree const& resp, FileList & list) {
        pt::ptree files = resp.get_child("files");
        for (auto& pair : files) {
            auto & file = pair.second;
            File f = {file.get_optional<std::string>("name").get(), file.get_optional<wxInt64>("time").get(), file.get_optional<wxInt64>("size").get()};
            list.push_back(f);
        }
        return 0;
    }, [this](FileList && list, Void &) {
        files = std::move(list);
        BuildGroups();
        SendChangedEvent();
        return 0;
    }, callback);
}

void PrinterFileSystem::DeleteFile(int index, Callback<PrinterFileSystem::Void> const &callback)
{
    if (index < 0 || index >= files.size() || files[index].name.empty()) {
        PostCallback(callback, ERROR_ITEM, Void());
        return;
    }
    auto name = files[index].name;
    SendRequest<Void, Void>(FILE_DELETE, pt::ptree(name), nullptr, [index, name, this](Void &&, Void &) {
        // TODO: 
        FileRemoved(index, name);
        SendChangedEvent();
        return 0;
    }, callback);
}

void PrinterFileSystem::DownloadFile(int index, std::string const &path, Callback<Progress> const &callback)
{
    if (index < 0 || index >= files.size() || files[index].name.empty()) {
        PostCallback(callback, ERROR_ITEM, Progress());
        return;
    }
    boost::shared_ptr<PrinterFileSystem> fs(new PrinterFileSystem(this));
    fs->SendRequest<Progress, Progress>(FILE_DOWNLOAD, pt::ptree(files[index].name), [fs, path, callback](pt::ptree const &resp, Progress & prog) {
        // in work thread, continue recv
        prog.size = 0;
        prog.total = resp.get_optional<int>("total").get();
        std::string md5 = resp.get_optional<std::string>("md5").get();
        std::ofstream ofs(path);
        boost::uuids::detail::md5 boost_md5;
        // receive data
        int result = fs->RecvData([&ofs, &prog, &boost_md5, callback](Tutk_Sample& sample) {
            ofs.write((char const *) sample.buffer, sample.size);
            boost_md5.process_bytes(sample.buffer, sample.size);
            prog.size += sample.size;
            callback(ERROR_CONT, prog);
            return ERROR_CONT;
        });
        // check size and md5
        if (prog.size == prog.total) {
            boost::uuids::detail::md5::digest_type digest;
            boost_md5.get_digest(digest);
            std::string str_md5;
            const auto char_digest = reinterpret_cast<const char*>(&digest);
            boost::algorithm::hex(char_digest,char_digest+sizeof(boost::uuids::detail::md5::digest_type), std::back_inserter(str_md5));
            if (!boost::iequals(str_md5, md5))
                result = ERROR_MD5;
        } else {
            result = ERROR_SIZE;
        }
        if (result != 0) {
            ofs.close();
            boost::system::error_code ec;
            boost::filesystem::remove(path, ec);
        }
        return result;
    }, nullptr, callback);
}

int PrinterFileSystem::GetCount() const
{
    if (mode == G_NONE)
        return files.size();
    return mode == G_YEAR ? group_year.size() : group_month.size();
}

int PrinterFileSystem::GetIndexAtTime(wxInt64 time)
{
    auto iter = std::upper_bound(files.begin(), files.end(), File{"", time});
    int n = std::distance(files.begin(), iter) - 1;
    if (mode == G_NONE) {
        return n;
    }
    auto & group = mode == G_YEAR ? group_year : group_month;
    auto iter2 = std::upper_bound(group.begin(), group.end(), n);
    return std::distance(group.begin(), iter2) - 1;
}

void PrinterFileSystem::LockFiles(int start, int count, Callback<Thumbnail> const& callback)
{
    //boost::shared_ptr<PrinterFileSystem> file(new PrinterFileSystem(this));
    boost::shared_ptr<PrinterFileSystem> fs(shared_from_this());
    std::vector<std::string> names;
    pt::ptree req;
    for (auto & name : names)
        req.put("", name);
    int end = start + count;
    fs->SendRequest<Thumbnail, Thumbnail>(FILE_THUMBNAIL, req, [this, fs, names, start, end, callback](pt::ptree const &resp, Thumbnail & list) {
        // in work thread, continue recv
        // receive data
        int n = 0;
        wxString mimetype = resp.get_optional<std::string>("mimetype").get_value_or("image/jpeg");
        int result = fs->RecvData([names, &n, &list, start, end, this, mimetype, callback](Tutk_Sample& sample) {
            wxMemoryInputStream mis(sample.buffer, sample.size);
            wxImage image(mis, mimetype);
            PostCallback<Thumbnail>([name = names[n], callback] (int result, Thumbnail list) {
                //auto iter = std::find_if(files.begin(), files.end(), [name = names[n]] (File & f) { return f.name == name; });
                //if (iter != files.end()) {
                //    iter->thumbnail = image;
                //    Thumbnail list;
                //    list.start = std::distance(files.begin(), iter);
                //    list.thumbnails.push_back(*iter);
                //}
                //int start2 = start;
                //int end2 = end;
                //if (start2 >= files.size()) {
                //    end2 -= start - files.size();
                //    start2 = files.size();
                //}
                //if (end2 >= files.size())
                //    end2 = files.size();
                //list.start = start2;
                //std::copy(files.begin() + start2, files.begin() + end2, std::back_inserter(list.thumbnails));
                callback(result, list);
            }, ERROR_CONT, list);
            return ++n == names.size() ? 0 : ERROR_CONT;
        });
        return result;
    }, nullptr, callback);
}

PrinterFileSystem::File const & PrinterFileSystem::GetFile(int index)
{
    if (mode == G_NONE)
        return files[index];
    if (mode == G_YEAR)
        index = group_year[index];
    return files[group_month[index]];
}

void PrinterFileSystem::UnlockFiles(int start, int count)
{
}

void PrinterFileSystem::SendRequest(int type, pt::ptree const &req, callback_t const & callback)
{
    boost::unique_lock l(mutex);
    pt::ptree root;
    root.put("type", type);
    root.put("req", (sequence + callbacks.size()));
    root.put_child("data", req);
    std::ostringstream oss;
    pt::write_json(oss, root);
    auto msg = oss.str();
    int  result = Tutk_SendMessage(&session, PRINTER_REQ, msg.c_str(), msg.length());
    if (result != 0) {
        callback(ERROR_PIPE, pt::ptree());
        return;
    }
    callbacks.push_back(callback);
}

void PrinterFileSystem::RecvMessageThread()
{
    std::string url = this->url; // copy
    if (url.empty())
        return;
    Tutk_Open(&session, url.c_str()); // sync
    int ctrl;
    char buf[4096];
    int len = sizeof(buf);
    while (!stop) {
        int n = Tutk_RecvMessage(&session, &ctrl, buf, &len);
        if (n >= 0) {
            pt::ptree root;
            std::istringstream iss(std::string(buf, n));
            int seq = -1;
            pt::ptree resp;
            try {
                pt::read_json(iss, root);
                n = root.get_optional<int>("result").get();
                seq = root.get_optional<int>("seq").get();
                resp = root.get_child("data");
            }
            catch (...) {
                n = ERROR_JSON;
            }
            seq -= sequence;
            if (seq < 0 || seq >= callbacks.size())
                continue;
            auto c = callbacks[seq];
            if (c == nullptr)
                continue;
            if (n != ERROR_CONT) {
                boost::unique_lock l(mutex);
                if (seq == 0) {
                    callbacks.pop_front();
                    ++sequence;
                } else {
                    callbacks[seq] = callback_t();
                }
            }
            c(n, resp);
        } else if (n == Tutk_would_block) {
            boost::this_thread::sleep(boost::posix_time::seconds(1));
            continue;
        }
    }
}

int PrinterFileSystem::RecvData(std::function<int(Tutk_Sample& sample)> const & callback)
{
    int result = 0;
    while (true) {
        Tutk_Sample sample;
        result = Tutk_ReadSample(&session, &sample);
        if (result == Tutk_success) {
            result = callback(sample);
            if (result == 1)
                continue;
        } else if (result == Tutk_would_block) {
            boost::this_thread::sleep(boost::posix_time::seconds(1));
            continue;
        } else if (result == Tutk_stream_end) {
            result = 0;
        } else {
            result = ERROR_PIPE;
        }
        break;
    }
    return result;
}

void PrinterFileSystem::PostCallback(std::function<void(void)> const& callback)
{
    wxCommandEvent *e = new wxCommandEvent(EVT_FILE_CALLBACK);
    wxQueueEvent(event_handler, e);
}

PrinterFileSystem::PrinterFileSystem(PrinterFileSystem *parent)
    : PrinterFileSystem(parent->url, parent->session.logger)
{
}

void PrinterFileSystem::BuildGroups()
{
    if (files.empty())
        return;
    wxDateTime t = wxDateTime((time_t) files.front().time);
    group_year.push_back(0);
    group_month.push_back(0);
    for (int i = 0; i < files.size(); ++i) {
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

void PrinterFileSystem::SendChangedEvent()
{
    wxCommandEvent event(EVT_FILE_CHANGED);
    event.SetEventObject(this);
    ProcessEvent(event);
}

void PrinterFileSystem::FileRemoved(int index, std::string const & name)
{
    if (index >= files.size() || files[index].name != name) {
        auto iter = std::find_if(files.begin(), files.end(), [name] (File & f) { return f.name == name; });
        if (iter == files.end())
            return;
        index = std::distance(files.begin(), iter);
    }
    auto removeFromGroup = [](std::vector<int> & group, int index, int total) {
        for (auto iter = group.begin(); iter != group.end(); ++iter) {
            int index2 = -1;
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
        return -1;
    };
    int index2 = removeFromGroup(group_month, index, files.size());
    if (index2 >= 0) {
        int index3 = removeFromGroup(group_year, index, group_month.size());
        if (index3 >= 0)
            group_year.erase(group_year.begin() + index3);
        group_month.erase(group_month.begin() + index2);
    }
    files.erase(files.begin() + index);
}
