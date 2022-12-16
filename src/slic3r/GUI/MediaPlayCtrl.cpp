#include "MediaPlayCtrl.h"
#include "Widgets/Button.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/Label.hpp"
#include "GUI_App.hpp"
#include "libslic3r/AppConfig.hpp"
#include "I18N.hpp"
#include "MsgDialog.hpp"
#include "DownloadProgressDialog.hpp"

#include <boost/filesystem/string_file.hpp>
#undef pid_t
#include <boost/process.hpp>
#ifdef __WIN32__
#include <boost/process/windows.hpp>
#elif __APPLE__
#include <sys/ipc.h>
#include <sys/shm.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>        /* For mode constants */
#include <fcntl.h>           /* For O_* constants */
#endif

namespace Slic3r {
namespace GUI {

MediaPlayCtrl::MediaPlayCtrl(wxWindow *parent, wxMediaCtrl2 *media_ctrl, const wxPoint &pos, const wxSize &size)
    : wxPanel(parent, wxID_ANY, pos, size)
    , m_media_ctrl(media_ctrl)
{
    SetBackgroundColour(*wxWHITE);
    m_media_ctrl->Bind(wxEVT_MEDIA_STATECHANGED, &MediaPlayCtrl::onStateChanged, this);

    m_button_play = new Button(this, "", "media_play", wxBORDER_NONE);
    m_button_play->SetCanFocus(false);

    m_label_status = new Label(this, "", LB_HYPERLINK);
    m_label_status->SetForegroundColour(wxColour("#2C2C2E"));

    m_button_play->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [this](auto &e) { TogglePlay(); });
    m_button_play->Bind(wxEVT_RIGHT_UP, [this](auto & e) { m_media_ctrl->Play(); });
    m_label_status->Bind(wxEVT_LEFT_UP, [this](auto &e) {
        auto url = wxString::Format(L"https://wiki.bambulab.com/%s/software/bambu-studio/faq/live-view", L"en");
        wxLaunchDefaultBrowser(url);
    });

    Bind(wxEVT_RIGHT_UP, [this](auto & e) { wxClipboard & c = *wxTheClipboard; if (c.Open()) { c.SetData(new wxTextDataObject(m_url)); c.Close(); } });

    wxBoxSizer * sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(m_button_play, 0, wxEXPAND | wxALL, 0);
    sizer->AddStretchSpacer(1);
    sizer->Add(m_label_status, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(25));
    SetSizer(sizer);

    m_thread = boost::thread([this] {
        media_proc();
    });

//#if BBL_RELEASE_TO_PUBLIC
//    m_next_retry = wxDateTime::Now();
//#endif

    parent->Bind(wxEVT_SHOW, &MediaPlayCtrl::on_show_hide, this);
    parent->GetParent()->GetParent()->Bind(wxEVT_SHOW, &MediaPlayCtrl::on_show_hide, this);

    m_lan_user = "bblp";
    m_lan_passwd = "bblp";
}

MediaPlayCtrl::~MediaPlayCtrl()
{
    {
        boost::unique_lock lock(m_mutex);
        m_tasks.push_back("<exit>");
        m_cond.notify_all();
    }
    m_thread.join();
}

void MediaPlayCtrl::SetMachineObject(MachineObject* obj)
{
    std::string machine = obj ? obj->dev_id : "";
    if (obj && obj->is_function_supported(PrinterFunction::FUNC_CAMERA_VIDEO)) {
        m_camera_exists = obj->has_ipcam;
        m_lan_mode      = obj->is_lan_mode_printer();
        m_lan_ip       = obj->is_function_supported(PrinterFunction::FUNC_LOCAL_TUNNEL) ? obj->dev_ip : "";
        m_lan_passwd    = obj->is_function_supported(PrinterFunction::FUNC_LOCAL_TUNNEL) ? obj->access_code : "";
        m_tutk_support = obj->is_function_supported(PrinterFunction::FUNC_REMOTE_TUNNEL);
    } else {
        m_camera_exists = false;
        m_lan_mode = false;
        m_lan_ip.clear();
        m_lan_passwd.clear();
        m_tutk_support = true;
    }
    if (machine == m_machine) {
        if (m_last_state == MEDIASTATE_IDLE && m_next_retry.IsValid() && wxDateTime::Now() >= m_next_retry)
            Play();
        return;
    }
    m_machine = machine;
    BOOST_LOG_TRIVIAL(info) << "MediaPlayCtrl switch machine: " << m_machine;
    m_failed_retry = 0;
    std::string stream_url;
    if (get_stream_url(&stream_url)) {
        m_streaming = boost::algorithm::contains(stream_url, "device=" + m_machine);
    } else {
        m_streaming = false;
    }
    if (m_last_state != MEDIASTATE_IDLE)
        Stop();
    if (m_next_retry.IsValid())
        Play();
    else
        SetStatus("", false);
}

void MediaPlayCtrl::Play()
{
    if (!m_next_retry.IsValid())
        return;
    if (!IsShownOnScreen())
        return;
    if (m_last_state != MEDIASTATE_IDLE) {
        return;
    }
    if (m_machine.empty()) {
        Stop();
        SetStatus(_L("Initialize failed (No Device)!"));
        return;
    }
    if (!m_camera_exists) {
        Stop();
        SetStatus(_L("Initialize failed (No Camera Device)!"));
        return;
    }

    m_last_state = MEDIASTATE_INITIALIZING;
    m_button_play->SetIcon("media_stop");
    SetStatus(_L("Initializing..."));

    if (!m_lan_ip.empty() && (!m_lan_mode || !m_lan_passwd.empty())) {
        m_url        = "bambu:///local/" + m_lan_ip + ".?port=6000&user=" + m_lan_user + "&passwd=" + m_lan_passwd;
        m_last_state = MEDIASTATE_LOADING;
        SetStatus(_L("Loading..."));
        if (wxGetApp().app_config->get("dump_video") == "true") {
            std::string file_h264 = data_dir() + "/video.h264";
            std::string file_info = data_dir() + "/video.info";
            BOOST_LOG_TRIVIAL(info) << "MediaPlayCtrl dump video to " << file_h264;
            // closed by BambuSource
            FILE *dump_h264_file = boost::nowide::fopen(file_h264.c_str(), "wb");
            FILE *dump_info_file = boost::nowide::fopen(file_info.c_str(), "wb");
            m_url                = m_url + "&dump_h264=" + boost::lexical_cast<std::string>(dump_h264_file);
            m_url                = m_url + "&dump_info=" + boost::lexical_cast<std::string>(dump_info_file);
        }
        boost::unique_lock lock(m_mutex);
        m_tasks.push_back(m_url);
        m_cond.notify_all();
        return;
    }

    if (m_lan_mode) {
        Stop();
        SetStatus(m_lan_passwd.empty() 
            ? _L("Initialize failed (Not supported with LAN-only mode)!") 
            : _L("Initialize failed (Not accessible in LAN-only mode)!"));
        return;
    }
    
    if (!m_tutk_support) { // not support tutk
        Stop();
        SetStatus(m_lan_ip.empty() 
            ? _L("Initialize failed (Missing LAN ip of printer)!") 
            : _L("Initialize failed (Not supported by printer)!"));
        return;
    }

    NetworkAgent* agent = wxGetApp().getAgent();
    if (agent) {
        agent->get_camera_url(m_machine, [this, m = m_machine](std::string url) {
            BOOST_LOG_TRIVIAL(info) << "MediaPlayCtrl camera_url: " << url << ", machine: " << m_machine;
            CallAfter([this, m, url] {
                if (m != m_machine) return;
                m_url = url;
                if (m_last_state == MEDIASTATE_INITIALIZING) {
                    if (url.empty() || !boost::algorithm::starts_with(url, "bambu:///")) {
                        Stop();
                        SetStatus(wxString::Format(_L("Initialize failed (%s)!"), url.empty() ? _L("Network unreachable") : from_u8(url)));
                    } else {
                        m_last_state = MEDIASTATE_LOADING;
                        SetStatus(_L("Loading..."));
                        if (wxGetApp().app_config->get("dump_video") == "true") {
                            BOOST_LOG_TRIVIAL(info) << "MediaPlayCtrl dump video to " << boost::filesystem::current_path();
                            m_url = m_url + "&dump=video.h264";
                        }
                        boost::unique_lock lock(m_mutex);
                        m_tasks.push_back(m_url);
                        m_cond.notify_all();
                    }
                } else {
                    BOOST_LOG_TRIVIAL(info) << "MediaPlayCtrl drop late ttcode for state: " << m_last_state;
                }
            });
        });
    }
}

void MediaPlayCtrl::Stop()
{
    if (m_last_state != MEDIASTATE_IDLE) {
        m_media_ctrl->InvalidateBestSize();
        m_button_play->SetIcon("media_play");
        boost::unique_lock lock(m_mutex);
        m_tasks.push_back("<stop>");
        m_cond.notify_all();
        m_last_state = MEDIASTATE_IDLE;
        if (m_failed_code)
            SetStatus(_L("Stopped [%d]!"), true);
        else
            SetStatus(_L("Stopped."), false);
        if (m_failed_code >= 100) // not keep retry on local error
            m_next_retry = wxDateTime();
    }
    ++m_failed_retry;
    if (m_next_retry.IsValid())
        m_next_retry = wxDateTime::Now() + wxTimeSpan::Seconds(5 * m_failed_retry);
}

void MediaPlayCtrl::TogglePlay()
{
    if (m_last_state != MEDIASTATE_IDLE) {
        m_next_retry = wxDateTime();
        Stop();
    } else {
        m_failed_retry = 0;
        m_next_retry   = wxDateTime::Now();
        Play();
    }
}

void MediaPlayCtrl::ToggleStream()
{
    std::string file_url = data_dir() + "/cameratools/url.txt";
    if (m_streaming) {
        boost::nowide::ofstream file(file_url);
        file.close();
        m_streaming = false;
        return;
    } else if (!boost::filesystem::exists(file_url)) {
        boost::nowide::ofstream file(file_url);
        file.close();
    }
    std::string url;
    if (!get_stream_url(&url)) {
        // create stream pipeline
        bool need_install = false;
        if (!start_stream_service(&need_install)) {
            if (!need_install) return;
            auto res = MessageDialog(this->GetParent(), _L("Virtual Camera Tools is required for this task!\nDo you want to install them?"), _L("Info"),
                                    wxOK | wxCANCEL).ShowModal();
            if (res == wxID_OK) {
                // download tools
                struct DownloadProgressDialog2 : DownloadProgressDialog
                {
                    MediaPlayCtrl *ctrl;
                    DownloadProgressDialog2(MediaPlayCtrl *ctrl) : DownloadProgressDialog(_L("Downloading Virtual Camera Tools")), ctrl(ctrl) {}
                    struct UpgradeNetworkJob2 : UpgradeNetworkJob
                    {
                        UpgradeNetworkJob2(std::shared_ptr<ProgressIndicator> pri) : UpgradeNetworkJob(pri) {
                            name         = "cameratools";
                            package_name = "camera_tools.zip";
                        }
                    };
                    std::shared_ptr<UpgradeNetworkJob> make_job(std::shared_ptr<ProgressIndicator> pri) override
                    { return std::make_shared<UpgradeNetworkJob2>(pri); }
                    void                               on_finish() override
                    {
                        wxGetApp().CallAfter([ctrl = this->ctrl] { ctrl->ToggleStream(); });
                        EndModal(wxID_CLOSE);
                    }
                };
                DownloadProgressDialog2 dlg(this);
                dlg.ShowModal();
            }
            return;
        }
    }
    if (!url.empty() && wxGetApp().app_config->get("not_show_vcamera_stop_prev") != "1") {
        MessageDialog dlg(this->GetParent(), _L("Another virtual camera is running.\nBambu Studio supports only a single virtual camera.\nDo you want to stop this virtual camera?"), _L("Warning"),
                                 wxYES | wxCANCEL | wxICON_INFORMATION);
        dlg.show_dsa_button();
        auto          res = dlg.ShowModal();
        if (dlg.get_checkbox_state())
            wxGetApp().app_config->set("not_show_vcamera_stop_prev", "1");
        if (res == wxID_CANCEL) return;
    }
    NetworkAgent *agent = wxGetApp().getAgent();
    if (!agent) return;
    agent->get_camera_url(m_machine, [this, m = m_machine](std::string url) {
            BOOST_LOG_TRIVIAL(info) << "camera_url: " << url;
        CallAfter([this, m, url] {
            if (m != m_machine) return;
            if (url.empty() || !boost::algorithm::starts_with(url, "bambu:///")) {
                MessageDialog(this->GetParent(), wxString::Format(_L("Virtual camera initialize failed (%s)!"), url.empty() ? _L("Network unreachable") : from_u8(url)), _L("Information"),
                              wxICON_INFORMATION)
                    .ShowModal();
                return;
            }
            std::string             file_url = data_dir() + "/cameratools/url.txt";
            boost::nowide::ofstream file(file_url);
            auto                    url2 = encode_path((url + "&device=" + m).c_str());
            file.write(url2.c_str(), url2.size());
            file.close();
            m_streaming = true;
        });
    });
}

void MediaPlayCtrl::SetStatus(wxString const &msg2, bool hyperlink)
{
    auto msg = wxString::Format(msg2, m_failed_code);
    BOOST_LOG_TRIVIAL(info) << "MediaPlayCtrl::SetStatus: " << msg.ToUTF8().data();
#ifdef __WXMSW__
    OutputDebugStringA("MediaPlayCtrl::SetStatus: ");
    OutputDebugStringA(msg.ToUTF8().data());
    OutputDebugStringA("\n");
#endif // __WXMSW__
    m_label_status->SetLabel(msg);
    long style = m_label_status->GetWindowStyle() & ~LB_HYPERLINK;
    if (hyperlink) {
        style |= LB_HYPERLINK;
    }
    m_label_status->SetWindowStyle(style);
    m_label_status->InvalidateBestSize();
    Layout();
}

bool MediaPlayCtrl::IsStreaming() const { return m_streaming; }

void MediaPlayCtrl::on_show_hide(wxShowEvent &evt)
{
    evt.Skip();
    if (m_isBeingDeleted) return;
    IsShownOnScreen() ? Play() : Stop();
}

void MediaPlayCtrl::media_proc()
{
    boost::unique_lock lock(m_mutex);
    while (true) {
        while (m_tasks.empty()) {
            m_cond.wait(lock);
        }
        wxString url = m_tasks.front();
        lock.unlock();
        if (url.IsEmpty()) {
            break;
        }
        else if (url == "<stop>") {
            m_media_ctrl->Stop();
        }
        else if (url == "<exit>") {
            break;
        }
        else if (url == "<play>") {
            m_media_ctrl->Play();
        }
        else {
            BOOST_LOG_TRIVIAL(info) <<  "MediaPlayCtrl: start load";
            m_media_ctrl->Load(wxURI(url));
            BOOST_LOG_TRIVIAL(info) << "MediaPlayCtrl: end load";
        }
        lock.lock();
        m_tasks.pop_front();
        wxMediaEvent theEvent(wxEVT_MEDIA_STATECHANGED, m_media_ctrl->GetId());
        theEvent.SetId(0);
        m_media_ctrl->GetEventHandler()->AddPendingEvent(theEvent);
    }
}

#ifdef __WIN32__
struct detach_process
    : public ::boost::process::detail::windows::handler_base_ext
{
    template<class Executor> void on_setup(Executor &exec) const { exec.creation_flags |= ::boost::winapi::CREATE_NO_WINDOW_; }
};
#endif

bool MediaPlayCtrl::start_stream_service(bool *need_install)
{
#ifdef __WIN32__
    std::string file_source = data_dir() + "\\cameratools\\bambu_source.exe";
    std::string file_ffmpeg = data_dir() + "\\cameratools\\ffmpeg.exe";
    std::string file_ff_cfg = data_dir() + "\\cameratools\\ffmpeg.cfg";
#else
    std::string file_source = data_dir() + "/cameratools/bambu_source";
    std::string file_ffmpeg = data_dir() + "/cameratools/ffmpeg";
    std::string file_ff_cfg = data_dir() + "/cameratools/ffmpeg.cfg";
#endif
    if (!boost::filesystem::exists(file_source) || !boost::filesystem::exists(file_ffmpeg) || !boost::filesystem::exists(file_ff_cfg)) {
        if (need_install) *need_install = true;
        return false;
    }
    std::string file_url  = data_dir() + "/cameratools/url.txt";
    if (!boost::filesystem::exists(file_url)) {
        boost::nowide::ofstream file(file_url);
        file.close();
    }
    wxString file_url2 = L"bambu:///camera/" + from_u8(file_url);
    file_url2.Replace("\\", "/");
    file_url2 = wxURI(file_url2).BuildURI();
    try {
        std::string configs;
        boost::filesystem::load_string_file(file_ff_cfg, configs);
        std::vector<std::string> configss;
        boost::algorithm::split(configss, configs, boost::algorithm::is_any_of("\r\n"));
        configss.erase(std::remove(configss.begin(), configss.end(), std::string()), configss.end());
        boost::process::pipe intermediate;
        boost::filesystem::path start_dir(boost::filesystem::path(data_dir()) / "plugins");
#ifdef __WXMSW__
        start_dir = boost::filesystem::path(data_dir()) / "cameratools";
        std::string file_dll = data_dir() + "/cameratools/BambuSource.dll";
        std::string file_dll2 = data_dir() + "/plugins/BambuSource.dll";
        if (!boost::filesystem::exists(file_dll) || boost::filesystem::last_write_time(file_dll) != boost::filesystem::last_write_time(file_dll2))
            boost::filesystem::copy_file(file_dll2, file_dll, boost::filesystem::copy_option::overwrite_if_exists);
        boost::process::child process_source(file_source, file_url2.data().AsInternal(), boost::process::std_out > intermediate, detach_process(),
                                             boost::process::start_dir(start_dir), boost::process::limit_handles);
        boost::process::child process_ffmpeg(file_ffmpeg, configss, boost::process::std_in < intermediate, detach_process(), boost::process::limit_handles);
#else
        boost::filesystem::permissions(file_source, boost::filesystem::owner_exe | boost::filesystem::add_perms);
        boost::filesystem::permissions(file_ffmpeg, boost::filesystem::owner_exe | boost::filesystem::add_perms);
        // TODO: limit_handles has bugs on posix
        boost::process::child process_source(file_source, file_url2.data().AsInternal(), boost::process::std_out > intermediate, 
                                             boost::process::start_dir(start_dir));
        boost::process::child process_ffmpeg(file_ffmpeg, configss, boost::process::std_in < intermediate);
#endif
        process_source.detach();
        process_ffmpeg.detach();
    } catch (std::exception &e) {
        BOOST_LOG_TRIVIAL(info) << "MediaPlayCtrl failed to start camera stream: " << e.what();
        return false;
    }
    return true;
}

bool MediaPlayCtrl::get_stream_url(std::string *url)
{
#ifdef __WIN32__
    HANDLE shm = ::OpenFileMapping(FILE_MAP_READ, FALSE, L"bambu_stream_url");
    if (shm == NULL) return false;
    if (url) {
        char *addr = (char *) MapViewOfFile(shm, FILE_MAP_READ, 0, 0, 0);
        if (addr) {
            *url = addr;
            UnmapViewOfFile(addr);
            url = nullptr;
        }
    }
    CloseHandle(shm);
#elif __APPLE__
    std::string file_url = data_dir() + "/cameratools/url.txt";
    key_t key = ::ftok(file_url.c_str(), 1000);
    int shm = ::shmget(key, 1024, 0);
    if (shm == -1) return false;
    struct shmid_ds ds;
    ::shmctl(shm, IPC_STAT, &ds);
    if (ds.shm_nattch == 0) {
        return false;
    }
    if (url) {
        char *addr = (char *) ::shmat(shm, nullptr, 0);
        if (addr != (void*) -1) {
            *url = addr;
            ::shmdt(addr);
            url = nullptr;
        }
    }
#else
    int shm = ::shm_open("bambu_stream_url", O_RDONLY, 0);
    if (shm == -1) return false;
    if (url) {
        char *addr = (char *) ::mmap(nullptr, 1024, PROT_READ, MAP_SHARED, shm, 0);
        if (addr != MAP_FAILED) {
            *url = addr;
            ::munmap(addr, 1024);
            url = nullptr;
        }
    }
    ::close(shm);
#endif
    return url == nullptr;
}

void MediaPlayCtrl::onStateChanged(wxMediaEvent& event)
{
    auto last_state = m_last_state;
    auto state = m_media_ctrl->GetState();
    BOOST_LOG_TRIVIAL(info) << "MediaPlayCtrl::onStateChanged: " << state << ", last_state: " << last_state;
    if ((int) state < 0)
        return;
    {
        boost::unique_lock lock(m_mutex);
        if (!m_tasks.empty()) {
            BOOST_LOG_TRIVIAL(info) << "MediaPlayCtrl::onStateChanged: skip when task not finished";
            return;
        }
    }
    if ((last_state == MEDIASTATE_IDLE || last_state == MEDIASTATE_INITIALIZING) && state == wxMEDIASTATE_STOPPED) {
        return;
    }
    if ((last_state == wxMEDIASTATE_PAUSED || last_state == wxMEDIASTATE_PLAYING)  &&
        state == wxMEDIASTATE_STOPPED) {
        m_failed_code = m_media_ctrl->GetLastError();
        Stop();
        return;
    }
    if (last_state == MEDIASTATE_LOADING && state == wxMEDIASTATE_STOPPED) {
        wxSize size = m_media_ctrl->GetVideoSize();
        BOOST_LOG_TRIVIAL(info) << "MediaPlayCtrl::onStateChanged: size: " << size.x << "x" << size.y;
        m_failed_code = m_media_ctrl->GetLastError();
        if (size.GetWidth() > 1000) {
            m_last_state = state;
            SetStatus(_L("Playing..."), false);
            m_failed_retry = 0;
            boost::unique_lock lock(m_mutex);
            m_tasks.push_back("<play>");
            m_cond.notify_all();
        }
        else if (event.GetId()) {
            Stop();
            if (m_failed_code == 0)
                m_failed_code = 2;
            SetStatus(_L("Load failed [%d]!"));
        }
    } else {
        m_last_state = state;
    }
}

}}

void wxMediaCtrl2::DoSetSize(int x, int y, int width, int height, int sizeFlags)
{
    wxWindow::DoSetSize(x, y, width, height, sizeFlags);
    if (sizeFlags & wxSIZE_USE_EXISTING) return;
    wxSize size = GetVideoSize();
    if (size.GetWidth() <= 0)
        size = wxSize{16, 9};
    int maxHeight = (width * size.GetHeight() + size.GetHeight() - 1) / size.GetWidth();
    if (maxHeight != GetMaxHeight()) {
        // BOOST_LOG_TRIVIAL(info) << "wxMediaCtrl2::DoSetSize: width: " << width << ", height: " << height << ", maxHeight: " << maxHeight;
        SetMaxSize({-1, maxHeight});
        Slic3r::GUI::wxGetApp().CallAfter([this] {
            if (auto p = GetParent()) {
                p->Layout();
                p->Refresh();
            }
        });
    }
}

