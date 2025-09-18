#include "BambuConnect.hpp"

#include <slic3r/GUI/GUI.hpp>
#include <slic3r/GUI/I18N.hpp>

#include <wx/filename.h>
#include <wx/utils.h>

#include <cctype>
#include <iomanip>
#include <sstream>

namespace Slic3r {

BambuConnect::BambuConnect(DynamicPrintConfig *) {}

const char* BambuConnect::get_name() const
{
    return "Bambu Connect";
}

bool BambuConnect::test(wxString &msg) const
{
    msg = _L("Connection test is not available for Bambu Connect.");
    return false;
}

wxString BambuConnect::get_test_ok_msg() const
{
    return {};
}

wxString BambuConnect::get_test_failed_msg(wxString &msg) const
{
    return msg;
}

bool BambuConnect::upload(PrintHostUpload upload_data, ProgressFn, ErrorFn error_fn) const
{
    wxString source_path = GUI::from_path(upload_data.source_path);
    if (source_path.empty() || !wxFileName::FileExists(source_path)) {
        if (error_fn) {
            error_fn(_L("The generated file is no longer available for Bambu Connect import."));
        }
        return false;
    }

    wxFileName file(source_path);
    if (!file.IsAbsolute())
        file.MakeAbsolute();

    const std::string encoded_path = encode_uri_component(GUI::into_u8(file.GetFullPath()));
    const std::string encoded_name = encode_uri_component(GUI::into_u8(file.GetName()));

    std::string url = "bambu-connect://import-file?path=";
    url += encoded_path;
    url += "&name=";
    url += encoded_name;
    url += "&version=1.0.0";

    if (!wxLaunchDefaultBrowser(url)) {
        if (error_fn) {
            error_fn(_L("Failed to invoke Bambu Connect. Please verify it is installed and registered as a protocol handler."));
        }
        return false;
    }

    return true;
}

bool BambuConnect::has_auto_discovery() const
{
    return false;
}

bool BambuConnect::can_test() const
{
    return false;
}

PrintHostPostUploadActions BambuConnect::get_post_upload_actions() const
{
    return {};
}

bool BambuConnect::retain_source_file() const
{
    return true;
}

std::string BambuConnect::get_host() const
{
    return "Bambu Connect";
}

std::string BambuConnect::encode_uri_component(const std::string &value)
{
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex << std::uppercase;

    for (unsigned char c : value) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << static_cast<char>(c);
        } else {
            escaped << '%' << std::setw(2) << static_cast<int>(c);
        }
    }

    return escaped.str();
}

} // namespace Slic3r
