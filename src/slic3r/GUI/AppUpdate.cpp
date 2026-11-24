#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "ReleaseNote.hpp"
#include "I18N.hpp"
#include "slic3r/Utils/ReleaseUpdater.hpp"
#include "slic3r/Utils/DesktopUpdater.hpp"
#ifdef __linux__
#include "slic3r/Utils/AppImageUpdater.hpp"
#endif
#include "slic3r/Utils/Http.hpp"

#include <atomic>
#include <algorithm>
#include <exception>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <wx/stdpaths.h>
#include <wx/filefn.h>
#include <thread>
#include <wx/progdlg.h>

namespace Slic3r::GUI {
namespace {
struct CheckGuard {
    std::atomic<bool> &flag;
    ~CheckGuard() { flag = false; }
};
void prompt_update_restart(GUI_App &app, const std::string &image)
{
    const int choice = wxMessageBox(
#ifdef __linux__
        _L("The update is installed. Restart Bambu Studio now? You will be asked to save any unsaved changes.") +
        "\n\n" + _L("The previous version is saved beside the AppImage with a .previous suffix."),
#else
        _L("The update is ready. Close Bambu Studio and install it now? You will be asked to save any unsaved changes."),
#endif
        _L("Update ready"), wxYES_NO | wxICON_INFORMATION, app.mainframe);
    if (choice == wxYES) {
        app.m_update_restart_path = image;
        // A deferred save retains the request; an explicit veto clears it.
        app.mainframe->Close();
    }
}

bool prepare_update(GUI_App &app, const std::string &image, const ReleaseUpdater::Release &release)
{
    wxWindow *parent = app.mainframe;
    wxProgressDialog progress(_L("Update Bambu Studio"), _L("Downloading and verifying the update…"),
                              100, parent, wxPD_APP_MODAL | wxPD_CAN_ABORT | wxPD_AUTO_HIDE | wxPD_ELAPSED_TIME);
    std::atomic<bool> cancel{false}, done{false};
    std::atomic<int> percent{0};
    std::exception_ptr error;
#ifndef __linux__
    std::string package_path;
#endif
    std::thread worker([&] {
        try {
#ifdef __linux__
            AppImageUpdater::download_and_install(image, release, cancel, percent);
#else
            namespace fs = std::filesystem;
#ifdef __APPLE__
            const auto parent_directory = fs::u8path(image).parent_path().u8string();
            const std::string filename = "update.dmg";
#else
            const auto parent_directory = fs::temp_directory_path().u8string();
            const std::string filename = "update.exe";
#endif
            // This string is consumed only after joining the worker.
            package_path = DesktopUpdater::download_package(parent_directory, release, filename, cancel, percent);
#ifdef __APPLE__
            DesktopUpdater::prepare_mac_bundle(package_path, image);
#endif
#endif
        }
        catch (...) { error = std::current_exception(); }
        done = true;
    });
    // The worker never accesses wx objects. Update pumps the modal event loop;
    // join before any captured state or the progress window can be destroyed.
    while (!done.load()) {
        if (!progress.Update(std::min(99, percent.load()))) cancel = true;
        wxMilliSleep(50);
    }
    worker.join();
    progress.Update(100);
#ifndef __linux__
    if (!error && cancel.load()) error = std::make_exception_ptr(std::runtime_error("Update cancelled"));
#endif
    if (error) {
#ifndef __linux__
        if (!package_path.empty()) {
            std::error_code ignored;
            std::filesystem::remove_all(std::filesystem::u8path(package_path).parent_path(), ignored);
        }
#endif
        if (!cancel.load()) {
            try { std::rethrow_exception(error); }
            catch (const std::exception &e) {
                wxMessageBox(_L("The update could not be installed. Your current version is still available.") +
                             "\n\n" + wxString::FromUTF8(e.what()), _L("Update failed"), wxOK | wxICON_ERROR, parent);
            }
        }
        return false;
    }
#ifndef __linux__
    app.m_update_package_path = std::move(package_path);
#endif
    return true;
}
} // namespace

bool GUI_App::check_application_update(bool show_tips)
{
    const std::string repository = BAMBUSTUDIO_UPDATE_REPOSITORY;
    const std::string asset = BAMBUSTUDIO_UPDATE_ASSET;
#ifdef __linux__
    const std::string image = AppImageUpdater::current_appimage();
#else
    namespace fs = std::filesystem;
    const auto executable = fs::u8path(wxStandardPaths::Get().GetExecutablePath().ToUTF8().data());
#ifdef __APPLE__
    const auto bundle = executable.parent_path().parent_path().parent_path();
    const std::string image = bundle.extension() == ".app" ? bundle.u8string() : "";
#else
    const std::string image = executable.u8string();
#endif
#endif
    if (repository.empty() || image.empty()) return false;
    if (!m_update_installed_path.empty()) {
        if (show_tips) prompt_update_restart(*this, m_update_installed_path);
        return true;
    }
    if (m_update_check_in_progress.exchange(true)) return true;
    const bool include_prereleases = app_config->get("enable_beta_version_update") == "true";
    // No app/cloud authorization headers are needed for public release metadata.
    auto http = Http::get("https://api.github.com/repos/" + repository + "/releases?per_page=100", false);
#ifdef __APPLE__
    const char *ca_file = std::getenv("SSL_CERT_FILE");
    http.ca_file(ca_file ? ca_file : "/etc/ssl/cert.pem");
#endif
    http.https_only().header("Accept", "application/vnd.github+json")
        .header("User-Agent", "BambuStudio-Updater")
        .timeout_connect(TIMEOUT_CONNECT).timeout_max(TIMEOUT_RESPONSE)
        .on_complete([this, repository, asset, image, include_prereleases, show_tips](std::string body, unsigned) {
            try {
                auto release = ReleaseUpdater::select_release(body, repository, asset, SLIC3R_VERSION, include_prereleases);
                CallAfter([this, release, image, repository, show_tips] {
                    CheckGuard guard{m_update_check_in_progress};
                    if (!release) {
                        if (show_tips) no_new_version();
                        return;
                    }
                    if (!show_tips && app_config->get("app", "skip_version") == release->version) return;
                    version_info.version_str = release->version;
                    UpdateVersionDialog dialog(mainframe);
                    dialog.update_version_info(wxString::FromUTF8(release->notes), wxString::FromUTF8(release->version), true);
                    dialog.m_link_open_in_browser->SetURL("https://github.com/" + repository + "/releases");
                    if (show_tips) dialog.m_button_skip_version->Hide();
                    if (dialog.ShowModal() != wxID_YES) return;
                    if (!prepare_update(*this, image, *release)) return;
                    m_update_installed_path = image;
                    prompt_update_restart(*this, image);
                });
            } catch (const std::exception &e) {
                const std::string message = e.what();
                CallAfter([this, show_tips, message] {
                    m_update_check_in_progress = false;
                    if (show_tips) wxMessageBox(wxString::FromUTF8(message), _L("Update check failed"), wxOK | wxICON_ERROR, mainframe);
                });
            }
        })
        .on_error([this, show_tips](std::string, std::string, unsigned) {
            CallAfter([this, show_tips] {
                m_update_check_in_progress = false;
                if (show_tips) wxMessageBox(_L("Could not check for updates. Please check your connection and try again."),
                                           _L("Update check failed"), wxOK | wxICON_ERROR, mainframe);
            });
        }).perform();
    return true;
}

void GUI_App::finish_application_update()
{
#ifdef __linux__
    if (m_update_restart_path.empty()) return;
    const wxString pid = wxString::Format("%ld", wxGetProcessId());
    const wxString script = wxString::FromUTF8(AppImageUpdater::restart_script());
    const wxString image = wxString::FromUTF8(m_update_restart_path);
    const wxString profile = wxString::FromUTF8(Slic3r::data_dir());
    const wxChar *arguments[] = {wxT("/bin/sh"), wxT("-c"), script.c_str(), wxT("bambustudio-restart"), pid.c_str(), image.c_str(), profile.c_str(), nullptr};
    if (wxExecute(arguments, wxEXEC_ASYNC | wxEXEC_MAKE_GROUP_LEADER) == 0)
        wxLogError("Could not schedule Bambu Studio restart; launch the updated AppImage manually.");
#else
    namespace fs = std::filesystem;
    if (m_update_package_path.empty()) return;
    const auto directory = fs::u8path(m_update_package_path).parent_path();
    if (m_update_restart_path.empty()) {
        std::error_code ignored;
        fs::remove_all(directory, ignored);
        return;
    }
    const wxString pid = wxString::Format("%ld", wxGetProcessId());
    const wxString application = wxString::FromUTF8(m_update_restart_path);
    const wxString package = wxString::FromUTF8(m_update_package_path);
    const wxString profile = wxString::FromUTF8(Slic3r::data_dir());
#ifdef __APPLE__
    const wxString stage = wxString::FromUTF8(directory.u8string());
    const wxString script = wxString::FromUTF8(DesktopUpdater::mac_install_script());
    const wxChar *arguments[] = {wxT("/bin/sh"), wxT("-c"), script.c_str(), wxT("bambustudio-update"), pid.c_str(), application.c_str(), stage.c_str(), profile.c_str(), nullptr};
#else
    const auto script_path = directory / "install.ps1";
    std::ofstream script_file(script_path, std::ios::binary);
    // Windows PowerShell 5.1 requires a BOM for UTF-8 script source.
    script_file << "\xEF\xBB\xBF" << DesktopUpdater::windows_install_script();
    script_file.close();
    if (!script_file) { wxLogError("Could not save the update helper."); return; }
    const wxString script = wxString::FromUTF8(script_path.u8string());
    const wxString powershell = wxGetOSDirectory() + wxT("\\System32\\WindowsPowerShell\\v1.0\\powershell.exe");
    const wxChar *arguments[] = {powershell.c_str(), wxT("-NoProfile"), wxT("-NonInteractive"), wxT("-ExecutionPolicy"), wxT("Bypass"), wxT("-File"), script.c_str(), wxT("-ParentId"), pid.c_str(), wxT("-Package"), package.c_str(), wxT("-Application"), application.c_str(), wxT("-DataDirectory"), profile.c_str(), nullptr};
#endif
    if (wxExecute(arguments, wxEXEC_ASYNC | wxEXEC_MAKE_GROUP_LEADER) == 0)
        wxLogError("Could not start the update helper. The current app is unchanged.");
#endif
}

} // namespace Slic3r::GUI
