#include "Tab.hpp"
#include "Project.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"

#include <wx/app.h>
#include <wx/button.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>

#include <wx/bmpcbox.h>
#include <wx/bmpbuttn.h>
#include <wx/treectrl.h>
#include <wx/imaglist.h>
#include <wx/settings.h>
#include <wx/filedlg.h>
#include <wx/wupdlock.h>
#include <wx/dataview.h>
#include <wx/tokenzr.h>
#include <wx/arrstr.h>
#include <wx/tglbtn.h>

#include <boost/log/trivial.hpp>
#include <boost/system/error_code.hpp>

#include <wx/base64.h>
#include <wx/msgdlg.h>

#include "wxExtensions.hpp"
#include "GUI_App.hpp"
#include "GUI_ObjectList.hpp"
#include "MainFrame.hpp"
#include "GUI_Utils.hpp"
#include <slic3r/GUI/Widgets/WebView.hpp>

#include <boost/algorithm/string/predicate.hpp>
#include <regex>
#include <fstream>
#include <stdexcept>
#include <set>
#include <cwctype>

namespace Slic3r { namespace GUI {

wxDEFINE_EVENT(EVT_PROJECT_RELOAD, wxCommandEvent);

const std::vector<std::string> license_list = {
    "BSD License",
    "Apache License",
    "GPL License",
    "LGPL License",
    "MIT License",
    "CC License"
};

ProjectPanel::ProjectPanel(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size, long style) : wxPanel(parent, id, pos, size, style)
{
    m_project_home_url = wxString::Format("file://%s/web/model_new/index.html", from_u8(resources_dir()));
    wxString strlang = wxGetApp().current_language_code_safe();
    if (strlang != "")
        m_project_home_url = wxString::Format("file://%s/web/model_new/index.html?lang=%s", from_u8(resources_dir()), strlang);

    wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);

    m_browser = WebView::CreateWebView(this, m_project_home_url);
    if (m_browser == nullptr) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format("load web view of project page failed");
        return;
    }
    //m_browser->Hide();
    main_sizer->Add(m_browser, wxSizerFlags().Expand().Proportion(1));
    m_browser->Bind(wxEVT_WEBVIEW_NAVIGATED, &ProjectPanel::on_navigated, this);
    m_browser->Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &ProjectPanel::OnScriptMessage, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_NAVIGATING, &ProjectPanel::onWebNavigating, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_NEWWINDOW, &ProjectPanel::OnNewWindow, this);
    Bind(EVT_PROJECT_RELOAD, &ProjectPanel::on_reload, this);

    SetSizer(main_sizer);
    Layout();
    Fit();
}

ProjectPanel::~ProjectPanel() {}

std::string trim(const std::string &str, const std::string &charsToTrim)
{
    std::regex pattern("^[" + charsToTrim + "]+|[" + charsToTrim + "]+$");
    return std::regex_replace(str, pattern, "");
}

/**
 * On new window, we veto to stop extra windows appearing
 */
void ProjectPanel::OnNewWindow(wxWebViewEvent &evt)
{
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << ": " << evt.GetURL().ToUTF8().data();
    wxString flag = " (other)";

    if (evt.GetNavigationAction() == wxWEBVIEW_NAV_ACTION_USER) { flag = " (user)"; }

    if (wxGetApp().get_mode() == comDevelop)
        wxLogMessage("%s", "New window; url='" + evt.GetURL() + "'" + flag);

    // If we handle new window events then just load them in local browser
    wxString        tmpUrl = evt.GetURL();

    if (tmpUrl.StartsWith("File://") || tmpUrl.StartsWith("file://"))
    {
        std::regex  pattern("%22http.+%22");
        std::smatch matches;
        std::string UrlTmp = tmpUrl.ToStdString();
        if (std::regex_search(UrlTmp, matches, pattern)) { tmpUrl = trim(matches[0].str(), "%22"); }
    }

    if (boost::starts_with(tmpUrl, "http://") || boost::starts_with(tmpUrl, "https://")) {
        m_browser->Stop();
        evt.Veto();
        wxLaunchDefaultApplication(tmpUrl);
    }
}

void ProjectPanel::onWebNavigating(wxWebViewEvent& evt)
{
    wxString tmpUrl = evt.GetURL();
    //wxString NowUrl = m_browser->GetCurrentURL();

    if (boost::starts_with(tmpUrl, "http://") || boost::starts_with(tmpUrl, "https://")) {
        m_browser->Stop();
        evt.Veto();
        wxLaunchDefaultApplication(tmpUrl);
    }
}

void ProjectPanel::on_reload(wxCommandEvent& evt)
{
    boost::thread reload = boost::thread([this] {
        std::string update_type;
        std::string license;
        std::string model_name;
        std::string model_author;
        std::string cover_file;
        std::string description;
        std::map<std::string, std::vector<json>> files;

        std::string p_name;
        std::string p_author;
        std::string p_description;
        std::string p_cover_file;
        std::string model_id;

        Model model = wxGetApp().plater()->model();

        license = model.model_info->license;
        model_name = model.model_info->model_name;
        cover_file = model.model_info->cover_file;
        description = model.model_info->description;
        update_type = model.model_info->origin;

        if (model.design_info && !model.design_info->DesignerId.empty()) {

            if (m_model_id_map.count(model.design_info->DesignerId) > 0) {
                model_id = m_model_id_map[model.design_info->DesignerId];
            }
            else {
                model_id = get_model_id(model.design_info->DesignerId);
                m_model_id_map[model.design_info->DesignerId] = model_id;
            }
        }

        try {
            if (!model.model_info->copyright.empty()) {
                json copy_right = json::parse(model.model_info->copyright);

                if (copy_right.is_array()) {
                    for (auto it = copy_right.begin(); it != copy_right.end(); it++) {
                        if ((*it).contains("author")) {
                            model_author = (*it)["author"].get<std::string>();
                        }
                    }
                }
            }
        }
        catch (...) {
            ;
        }

        if (model_author.empty() && model.design_info != nullptr)
            model_author = model.design_info->Designer;

        if (model.profile_info != nullptr) {
            p_name = model.profile_info->ProfileTile;
            p_description = model.profile_info->ProfileDescription;
            p_cover_file = model.profile_info->ProfileCover;
            p_author = model.profile_info->ProfileUserName;
        }

        //file info
        std::string file_path = encode_path(wxGetApp().plater()->model().get_auxiliary_file_temp_path().c_str());
        if (!file_path.empty()) {
            files = Reload(file_path);
        }
        else {
            clear_model_info();
            return;
        }

        json j;
        j["model"]["license"] = license;
        j["model"]["name"] = wxGetApp().url_encode(model_name);
        j["model"]["author"] = wxGetApp().url_encode(model_author);;
        j["model"]["cover_img"] = wxGetApp().url_encode(cover_file);
        j["model"]["description"] = wxGetApp().url_encode(description);
        j["model"]["preview_img"] = files["Model Pictures"];
        j["model"]["upload_type"] = update_type;
        j["model"]["model_id"] = model_id;

        j["file"]["BOM"] = files["Bill of Materials"];
        j["file"]["Assembly"] = files["Assembly Guide"];
        j["file"]["Other"] = files["Others"];

        j["profile"]["name"] = wxGetApp().url_encode(p_name);
        j["profile"]["author"] = wxGetApp().url_encode(p_author);
        j["profile"]["description"] = wxGetApp().url_encode(p_description);
        j["profile"]["cover_img"] = wxGetApp().url_encode(p_cover_file);
        j["profile"]["preview_img"] = files["Profile Pictures"];

        json payload = json::object();
        payload["command"] = "show_3mf_info";
        payload["sequence_id"] = std::to_string(ProjectPanel::m_sequence_id++);
        payload["model"] = j;

        m_last_payload = payload;

        auto dispatch_payload = [this](const json& data) {
            wxString strJS = wxString::Format("HandleStudio(%s)", data.dump(-1, ' ', false, json::error_handler_t::ignore));
#ifdef __APPLE__
            wxGetApp().CallAfter([this, strJS] { RunScript(strJS.ToStdString()); });
#else
            if (m_web_init_completed) {
                wxGetApp().CallAfter([this, strJS] { RunScript(strJS.ToStdString()); });
            }
#endif
        };

        dispatch_payload(m_last_payload);

    });
}

std::string ProjectPanel::get_model_id(std::string desgin_id)
{
    std::string model_id;
    auto host = wxGetApp().get_http_url(wxGetApp().app_config->get_country_code(), "v1/design-service/model/" + desgin_id);
    Http http = Http::get(host);
    http.header("accept", "application/json")
        //.header("Authorization")
        .on_complete([this, &model_id](std::string body, unsigned status) {
        try {
            json j = json::parse(body);
            if (j.contains("id")) {
                int mid = j["id"].get<int>();
                if (mid > 0) {
                    model_id = std::to_string(mid);
                }
            }
        }
        catch (...) {
            ;
        }
            })
        .on_error([this](std::string body, std::string error, unsigned status) {
            })
        .perform_sync();

    return model_id;
}

void ProjectPanel::msw_rescale()
{
}

void ProjectPanel::on_size(wxSizeEvent &event)
{
    event.Skip();
}

void ProjectPanel::on_navigated(wxWebViewEvent& event)
{
    event.Skip();
}

void ProjectPanel::OnScriptMessage(wxWebViewEvent& evt)
{
    try {
        wxString strInput = evt.GetString();
        json     j = json::parse(strInput);

        wxString strCmd = j["command"];

        if (strCmd == "open_3mf_accessory") {
            wxString accessory_path =  j["accessory_path"];

            if (!accessory_path.empty()) {
                std::string decode_path = wxGetApp().url_decode(accessory_path.ToStdString());
                fs::path path(decode_path);

                if (fs::exists(path)) {
                    wxLaunchDefaultApplication(path.wstring(), 0);
                }
            }
        }
        else if (strCmd == "request_3mf_info") {
            m_web_init_completed = true;
            if (!m_last_payload.empty()) {
                auto payload_copy = m_last_payload;
                payload_copy["sequence_id"] = std::to_string(ProjectPanel::m_sequence_id++);
                wxString strJS = wxString::Format("HandleStudio(%s)", payload_copy.dump(-1, ' ', false, json::error_handler_t::ignore));
                wxGetApp().CallAfter([this, strJS] {
                    RunScript(strJS.ToStdString());
                });
            }
        }
        else if (strCmd == "request_confirm_save_project") {
            std::string sequence_id;
            if (j.contains("sequence_id")) {
                const auto& seq = j["sequence_id"];
                if (seq.is_string())
                    sequence_id = seq.get<std::string>();
                else if (seq.is_number_integer())
                    sequence_id = std::to_string(seq.get<long long>());
            }

            MessageDialog dlg(this,
                              _L("Do you want to save the changes ?"),
                              _L("Save"),
                              wxYES_NO | wxCANCEL | wxYES_DEFAULT | wxCENTRE);
            int res = dlg.ShowModal();
            if (res == wxID_YES) {
                save_project();
            }else if (res == wxID_NO ) {
                json resp = json::object();
                resp["command"] = "discard_project";
                if (!sequence_id.empty())
                    resp["sequence_id"] = sequence_id;

                wxString strJS = wxString::Format("window.HandleEditor && window.HandleEditor(%s);",
                                                  resp.dump(-1, ' ', false, json::error_handler_t::ignore));
                wxGetApp().CallAfter([this, strJS] {
                    RunScript(strJS.ToStdString());
                });
            } else {}
        }
        else if (strCmd == "modelmall_model_open") {
            if (j.contains("data")) {
                json data = j["data"];

                if (data.contains("id")) {
                    wxString model_id =  j["data"]["id"];

                    if (!model_id.empty()) {
                        std::string h = wxGetApp().get_model_http_url(wxGetApp().app_config->get_country_code());
                        auto l = wxGetApp().current_language_code_safe();
                        if (auto n = l.find('_'); n != std::string::npos)
                            l = l.substr(0, n);
                        auto url = (boost::format("%1%%2%/models/%3%") % h % l % model_id).str();
                        wxLaunchDefaultBrowser(url);
                    }
                }
            }
        }
        else if (strCmd == "editor_upload_file") {
            json response = json::object();
            std::string sequence_id;
            if (j.contains("sequence_id"))
                sequence_id = j["sequence_id"].get<std::string>();

            response["command"]     = "editor_upload_file_result";
            response["sequence_id"] = sequence_id;

            try {
                if (!j.contains("data") || !j["data"].is_object())
                    throw std::runtime_error("missing payload");

                const json& payload = j["data"];
                std::string base64  = payload.value("base64", std::string());
                if (base64.empty())
                    throw std::runtime_error("empty file content");

                std::string original_name = payload.value("filename", std::string());
                std::string extension;
                if (!original_name.empty()) {
                    fs::path original_path(original_name);
                    if (original_path.has_extension())
                        extension = original_path.extension().string();
                }

                size_t decoded_capacity = wxBase64DecodedSize(base64.length());
                if (decoded_capacity == 0)
                    throw std::runtime_error("invalid base64 length");

                std::string decoded;
                decoded.resize(decoded_capacity);
                size_t decoded_size = wxBase64Decode(decoded.data(), decoded.size(), base64.c_str(), base64.length());
                if (decoded_size == wxInvalidSize || decoded_size == 0)
                    throw std::runtime_error("base64 decode failed");
                decoded.resize(decoded_size);

                fs::path temp_dir;
                try {
                    temp_dir = fs::temp_directory_path();
                } catch (const std::exception& e) {
                    throw std::runtime_error(std::string("failed to locate temp directory: ") + e.what());
                }
                temp_dir /= "bambu_editor";

                boost::system::error_code ec;
                fs::create_directories(temp_dir, ec);
                if (ec) {
                    throw std::runtime_error("failed to create temp directory: " + ec.message());
                }

                fs::path temp_file = temp_dir / fs::unique_path("editor-%%%%%%%%-%%%%%%%%");
                if (!extension.empty()) {
                    if (extension.front() != '.')
                        extension.insert(extension.begin(), '.');
                    temp_file.replace_extension(extension);
                }

                std::ofstream output(temp_file.string(), std::ios::binary | std::ios::trunc);
                if (!output.is_open())
                    throw std::runtime_error("failed to open temp file for writing");
                output.write(decoded.data(), static_cast<std::streamsize>(decoded.size()));
                if (!output.good())
                    throw std::runtime_error("failed to write temp file");
                output.close();

                wxString stored_path_wx(temp_file.wstring());
                std::string stored_path(stored_path_wx.utf8_str());

                json data = json::object();
                data["path"]         = stored_path;
                data["encoded_path"] = wxGetApp().url_encode(stored_path);
                data["filename"]     = original_name;
                if (payload.contains("size"))
                    data["size"] = payload["size"];
                if (payload.contains("type"))
                    data["type"] = payload["type"];

                response["data"] = data;
            } catch (const std::exception& e) {
                response["error"] = e.what();
            }

            wxString payload = wxString::FromUTF8(response.dump(-1, ' ', false, json::error_handler_t::ignore));
            wxString script  = wxString::Format("window.HandleEditor && window.HandleEditor(%s);", payload);
            wxGetApp().CallAfter([this, script] {
                RunScript(script.ToStdString());
            });
        }
        else if (strCmd == "update_3mf_info") {
            json response = json::object();

            response["command"]     = "update_3mf_info_result";
            response["sequence_id"] = j["sequence_id"];

            try {
                if (!j.contains("model") || !j["model"].is_object())
                    throw std::runtime_error("invalid payload");

                const json& payload = j["model"];

                Model& model_ref = wxGetApp().plater()->model();
                if (model_ref.model_info == nullptr)
                    model_ref.model_info = std::make_shared<ModelInfo>();
                if (model_ref.profile_info == nullptr)
                    model_ref.profile_info = std::make_shared<ModelProfileInfo>();

                auto decode_string = [](const json& node, const char* key) -> std::string {
                    if (!node.contains(key))
                        return std::string();
                    const auto& val = node.at(key);
                    if (!val.is_string())
                        return std::string();
                    std::string out = val.get<std::string>();
                    if (out.empty())
                        return out;
                    try {
                        return wxGetApp().url_decode(out);
                    } catch (...) {
                        return out;
                    }
                };

                auto decode_required = [&](const json& node, const char* key) -> std::string {
                    std::string value = decode_string(node, key);
                    if (value.empty())
                        throw std::runtime_error(std::string("missing ") + key);
                    return value;
                };

                const json model_section   = payload.value("model", json::object());
                const json file_section    = payload.value("file", json::object());
                const json profile_section = payload.value("profile", json::object());

                model_ref.model_info->model_name  = decode_required(model_section, "name");
                model_ref.model_info->description = decode_required(model_section, "description");


                model_ref.profile_info->ProfileTile        = decode_string(profile_section, "name");
                model_ref.profile_info->ProfileDescription = decode_string(profile_section, "description");

                std::string auxiliary_root = encode_path(model_ref.get_auxiliary_file_temp_path().c_str());
                if (auxiliary_root.empty())
                    throw std::runtime_error("failed to locate auxiliary directory");

                wxString aux_root_wx(auxiliary_root.c_str());

                auto sanitize_filename = [](std::string name) -> std::string {
                    if (name.empty())
                        return name;
                    for (char& ch : name) {
                        if (ch == '\\' || ch == '/' || ch == ':' || ch == '*' ||
                            ch == '?'  || ch == '"' || ch == '<' || ch == '>' || ch == '|')
                            ch = '_';
                    }
                    return name;
                };

                auto json_array_or_empty = [](const json& parent, const char* key) -> json {
                    if (parent.contains(key) && parent.at(key).is_array())
                        return parent.at(key);
                    return json::array();
                };

                auto ensure_directory = [&](const wxString& folder_name) -> fs::path {
                    wxString full_path = aux_root_wx + "/" + folder_name;
                    fs::path dir_path(full_path.ToStdWstring());
                    boost::system::error_code ec;
                    if (!fs::exists(dir_path, ec)) {
                        fs::create_directories(dir_path, ec);
                        if (ec)
                            throw std::runtime_error("failed to create auxiliary directory: " + ec.message());
                    }
                    return dir_path;
                };

                auto detect_extension_from_data_uri = [](const std::string& data_uri) -> std::string {
                    auto slash = data_uri.find('/');
                    auto semicolon = data_uri.find(';');
                    if (slash != std::string::npos && semicolon != std::string::npos && semicolon > slash) {
                        std::string ext = data_uri.substr(slash + 1, semicolon - slash - 1);
                        if (!ext.empty())
                            return "." + ext;
                    }
                    return std::string();
                };

                auto normalize_path = [](const fs::path& path) {
                    std::wstring w = path.lexically_normal().wstring();
                    std::transform(w.begin(), w.end(), w.begin(), towlower);
                    return w;
                };

                auto copy_entries = [&](const json& list, const wxString& folder_name, bool is_image, std::vector<fs::path>* stored_paths) {
                    fs::path dest_dir = ensure_directory(folder_name);
                    if (stored_paths)
                        stored_paths->clear();

                    if (!list.is_array())
                        return;

                    size_t index = 0;
                    std::set<std::wstring> desired_paths;
                    for (const auto& entry : list) {
                        if (!entry.is_object())
                            continue;

                        std::string raw_filepath = entry.value("filepath", std::string());
                        std::string raw_filename = entry.value("filename", std::string());

                        bool is_data_uri = boost::algorithm::starts_with(raw_filepath, "data:");
                        std::string decoded_filepath = raw_filepath;
                        if (!is_data_uri)
                            decoded_filepath = wxGetApp().url_decode(raw_filepath);

                        fs::path dest_file;
                        bool reused_existing = false;

                        if (!is_data_uri && !decoded_filepath.empty()) {
                            fs::path src_path(decoded_filepath);
                            boost::system::error_code ec;
                            if (fs::exists(src_path, ec) && fs::equivalent(src_path.parent_path(), dest_dir, ec)) {
                                dest_file = src_path;
                                reused_existing = true;
                            }
                        }

                        std::string filename = sanitize_filename(wxGetApp().url_decode(raw_filename));
                        if (!reused_existing) {
                            std::string extension;
                            if (!filename.empty()) {
                                auto pos = filename.find_last_of('.');
                                if (pos != std::string::npos)
                                    extension = filename.substr(pos);
                            }

                            if (filename.empty()) {
                                extension = !extension.empty() ? extension : (is_data_uri ? detect_extension_from_data_uri(raw_filepath) : fs::path(decoded_filepath).extension().string());
                                if (!extension.empty() && extension.front() != '.')
                                    extension.insert(extension.begin(), '.');
                                filename = "file_" + std::to_string(index++) + extension;
                            }

                            dest_file = dest_dir / filename;
                            while (desired_paths.count(dest_file.lexically_normal().wstring()) != 0)
                                dest_file = dest_dir / (dest_file.stem().string() + "_" + std::to_string(index++) + dest_file.extension().string());

                            if (is_data_uri) {
                                auto comma = raw_filepath.find(',');
                                if (comma == std::string::npos)
                                    throw std::runtime_error("invalid data url");
                                std::string base64 = raw_filepath.substr(comma + 1);
                                std::string buffer;
                                buffer.resize(wxBase64DecodedSize(base64.length()));
                                size_t decoded_size = wxBase64Decode(buffer.data(), buffer.size(), base64.c_str(), base64.length());
                                if (decoded_size == wxInvalidSize || decoded_size == 0)
                                    throw std::runtime_error("failed to decode image data");
                                buffer.resize(decoded_size);
                                std::ofstream out(dest_file.string(), std::ios::binary | std::ios::trunc);
                                if (!out.is_open())
                                    throw std::runtime_error("failed to write file");
                                out.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                                out.close();
                            } else {
                                if (decoded_filepath.empty())
                                    throw std::runtime_error("missing attachment path");
                                fs::path src_path(decoded_filepath);
                                if (!fs::exists(src_path))
                                    throw std::runtime_error("attachment not found: " + decoded_filepath);
                                boost::system::error_code ec;
                                fs::copy_file(src_path, dest_file, fs::copy_option::overwrite_if_exists, ec);
                                if (ec)
                                    throw std::runtime_error("copy attachment failed: " + ec.message());
                            }
                        }

                        desired_paths.insert(normalize_path(dest_file));
                        if (stored_paths)
                            stored_paths->push_back(dest_file);
                    }

                    boost::system::error_code ec;
                    fs::directory_iterator end_it;
                    for (fs::directory_iterator it(dest_dir, ec); !ec && it != end_it; ++it) {
                        if (fs::is_directory(it->path(), ec))
                            continue;
                        if (desired_paths.count(normalize_path(it->path())) == 0)
                            fs::remove(it->path(), ec);
                    }
                };

                std::vector<fs::path> model_picture_paths;
                copy_entries(json_array_or_empty(model_section, "preview_img"), "Model Pictures", true, &model_picture_paths);
                copy_entries(json_array_or_empty(file_section, "BOM"), "Bill of Materials", false, nullptr);
                copy_entries(json_array_or_empty(file_section, "Assembly"), "Assembly Guide", false, nullptr);
                copy_entries(json_array_or_empty(file_section, "Other"), "Others", false, nullptr);
                std::vector<fs::path> profile_picture_paths;
                copy_entries(json_array_or_empty(profile_section, "preview_img"), "Profile Pictures", true, &profile_picture_paths);

                if (!model_picture_paths.empty()) {
                    const fs::path &cover_src = model_picture_paths.front();
                    model_ref.model_info->cover_file = cover_src.filename().string();

                    fs::path full_path      = cover_src.parent_path();
                    fs::path full_root_path = full_path.parent_path();
                    wxString full_root_path_str = encode_path(full_root_path.string().c_str());
                    wxString thumbnails_dir     = wxString::Format("%s/.thumbnails", full_root_path_str);
                    fs::path dir_path(thumbnails_dir.ToStdWstring());

                    boost::system::error_code ec;
                    if (!fs::exists(dir_path, ec))
                        fs::create_directories(dir_path, ec);

                    wxImage thumbnail_img;
                    if (generate_image(cover_src.string(), thumbnail_img, _3MF_COVER_SIZE)) {
                        auto cover_img_path = dir_path / "thumbnail_3mf.png";
                        thumbnail_img.SaveFile(encode_path(cover_img_path.string().c_str()));
                    }
                    if (generate_image(cover_src.string(), thumbnail_img, PRINTER_THUMBNAIL_SMALL_SIZE)) {
                        auto small_img_path = dir_path / "thumbnail_small.png";
                        thumbnail_img.SaveFile(encode_path(small_img_path.string().c_str()));
                    }
                    if (generate_image(cover_src.string(), thumbnail_img, PRINTER_THUMBNAIL_MIDDLE_SIZE)) {
                        auto middle_img_path = dir_path / "thumbnail_middle.png";
                        thumbnail_img.SaveFile(encode_path(middle_img_path.string().c_str()));
                    }
                } else {
                    model_ref.model_info->cover_file.clear();
                }

                if (!profile_picture_paths.empty())
                    model_ref.profile_info->ProfileCover = profile_picture_paths.front().filename().string();
                else
                    model_ref.profile_info->ProfileCover.clear();


                wxGetApp().plater()->set_plater_dirty(true);
                Slic3r::put_other_changes();
                update_model_data();

                response["status"]  = "success";
                response["message"] = "Project information saved";
            } catch (const std::exception& e) {
                response["error"] = e.what();
            }

            wxString payload = wxString::FromUTF8(response.dump(-1, ' ', false, json::error_handler_t::ignore));
            wxString script  = wxString::Format("window.HandleEditor && window.HandleEditor(%s);", payload);
            wxGetApp().CallAfter([this, script] {
                RunScript(script.ToStdString());
            });
        }
        else if (strCmd == "debug_info") {
            //wxString msg =  j["msg"];
            //OutputDebugString(wxString::Format("Model_Web: msg = %s \r\n", msg));
            //BOOST_LOG_TRIVIAL(info) << wxString::Format("Model_Web: msg = %s", msg);
        }

    }
    catch (std::exception& /*e*/) {
        // wxMessageBox(e.what(), "json Exception", MB_OK);
    }
}

void ProjectPanel::update_model_data()
{
    Model model = wxGetApp().plater()->model();
    clear_model_info();

    //basics info
    //Note: Under the master branch, model_info will never return nullptr, but under the GitHub branch, it might. The reason is unclear.
    if (model.model_info == nullptr) {
        json payload = json::object();
        payload["command"] = "show_3mf_info";
        payload["sequence_id"] = std::to_string(ProjectPanel::m_sequence_id++);
        payload["model"] = json::object();

        wxString js = wxString::Format("HandleStudio(%s)",
                                    payload.dump(-1, ' ', false, json::error_handler_t::ignore));
        wxGetApp().CallAfter([this, js]{ RunScript(js.ToStdString()); });
        return;
    }

    auto event = wxCommandEvent(EVT_PROJECT_RELOAD);
    event.SetEventObject(this);
    wxPostEvent(this, event);
}

void ProjectPanel::clear_model_info()
{
    m_last_payload = json::object();

    json m_Res = json::object();
    m_Res["command"] = "clear_3mf_info";
    m_Res["sequence_id"] = std::to_string(ProjectPanel::m_sequence_id++);

    wxString strJS = wxString::Format("HandleStudio(%s)", m_Res.dump(-1, ' ', false, json::error_handler_t::ignore));

    wxGetApp().CallAfter([this, strJS] {
        RunScript(strJS.ToStdString());
    });
}

std::map<std::string, std::vector<json>> ProjectPanel::Reload(wxString aux_path)
{
    std::vector<fs::path>                           dir_cache;
    fs::directory_iterator                          iter_end;
    wxString                                        m_root_dir;
    std::map<std::string, std::vector<json>> m_paths_list;

    const static std::array<wxString, 5> s_default_folders = {
        ("Model Pictures"),
        ("Bill of Materials"),
        ("Assembly Guide"),
        ("Others"),
        //(".thumbnails"),
        ("Profile Pictures"),
    };

    for (auto folder : s_default_folders)
        m_paths_list[folder.ToStdString()] = std::vector<json>{};


    fs::path new_aux_path(aux_path.ToStdWstring());

    try {
        fs::remove_all(fs::path(m_root_dir.ToStdWstring()));
    }
    catch (...) {
        BOOST_LOG_TRIVIAL(error) << "Failed  removing the auxiliary directory" << m_root_dir.c_str();
    }

    m_root_dir = aux_path;
    // Check new path. If not exist, create a new one.
    if (!fs::exists(new_aux_path)) {
        fs::create_directory(new_aux_path);
        // Create default folders if they are not loaded
        for (auto folder : s_default_folders) {
            wxString folder_path = aux_path + "/" + folder;
            if (fs::exists(folder_path.ToStdWstring())) continue;
            fs::create_directory(folder_path.ToStdWstring());
        }
        return m_paths_list;
    }

    // Load from new path
    for (fs::directory_iterator iter(new_aux_path); iter != iter_end; iter++) {
        wxString path = iter->path().generic_wstring();
        dir_cache.push_back(iter->path());
    }


    for (auto dir : dir_cache) {
        for (fs::directory_iterator iter(dir); iter != iter_end; iter++) {
            if (fs::is_directory(iter->path())) continue;

            json pfile_obj;

            std::string file_path = iter->path().string();
            fs::path file_path_obj = fs::path(iter->path().string());

            for (auto folder : s_default_folders) {
                auto idx = file_path.find(folder.ToStdString());
                if (idx != std::string::npos) {

                    wxStructStat strucStat;
                    wxString file_name = encode_path(file_path.c_str());
                    wxStat(file_name, &strucStat);
                    wxFileOffset filelen = strucStat.st_size;

                    pfile_obj["filename"] = wxGetApp().url_encode(file_path_obj.filename().string().c_str());
                    pfile_obj["size"] = formatBytes((unsigned long)filelen);

                    std::string file_extension = file_path_obj.extension().string();
                    boost::algorithm::to_lower(file_extension);

                    //image
                    if (file_extension == ".jpg"    ||
                        file_extension == ".jpeg"   ||
                        file_extension == ".pjpeg"  ||
                        file_extension == ".png"    ||
                        file_extension == ".jfif"   ||
                        file_extension == ".pjp"    ||
                        file_extension == ".webp"   ||
                        file_extension == ".bmp")
                    {

                        wxString base64_str = to_base64(file_path);
                        pfile_obj["filepath"] = base64_str.ToStdString();
                        m_paths_list[folder.ToStdString()].push_back(pfile_obj);
                        break;
                    }
                    else {
                        pfile_obj["filepath"] = wxGetApp().url_encode(file_path);
                        m_paths_list[folder.ToStdString()].push_back(pfile_obj);
                        break;
                    }
                }
            }
        }
    }

    return m_paths_list;
}

std::string ProjectPanel::formatBytes(unsigned long bytes)
{
    double dValidData = round(double(bytes) / (1024 * 1024) * 1000) / 1000;
    return wxString::Format("%.2fMB", dValidData).ToStdString();
}

wxString ProjectPanel::to_base64(std::string file_path)
{

    std::ifstream imageFile(encode_path(file_path.c_str()), std::ios::binary);
    if (!imageFile) {
        return wxEmptyString;
    }

    std::ostringstream imageStream;
    imageStream << imageFile.rdbuf();

    std::string binaryImageData = imageStream.str();

    std::string extension;
    size_t last_dot = file_path.find_last_of(".");

    if (last_dot != std::string::npos) {
        extension = file_path.substr(last_dot + 1);
    }

    wxString bease64_head = wxString::Format("data:image/%s;base64,", extension);


    std::wstringstream wss;
    wss << bease64_head;
    wss << wxBase64Encode(binaryImageData.data(), binaryImageData.size());

    wxString base64_str = wss.str();
    return  base64_str;
}

void ProjectPanel::RunScript(std::string content)
{
    WebView::RunScript(m_browser, content);
}

bool ProjectPanel::is_editing_page() const
{
    if (m_browser == nullptr)
        return false;
    wxString current_url = m_browser->GetCurrentURL();
    if (current_url.IsEmpty())
        return false;
    return current_url.Lower().Contains("editor.html");
}

bool ProjectPanel::Show(bool show)
{
    if (show) update_model_data();
    return wxPanel::Show(show);
}

void ProjectPanel::save_project()
{
    json resp = json::object();
    resp["command"] = "save_project";
    resp["sequence_id"] = std::to_string(ProjectPanel::m_sequence_id++);

    wxString strJS = wxString::Format("window.HandleEditor && window.HandleEditor(%s);",
                                      resp.dump(-1, ' ', false, json::error_handler_t::ignore));
    wxGetApp().CallAfter([this, strJS] {
        RunScript(strJS.ToStdString());
    });
}

}} // namespace Slic3r::GUI
