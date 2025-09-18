#ifndef slic3r_BambuConnect_hpp_
#define slic3r_BambuConnect_hpp_

#include "PrintHost.hpp"

namespace Slic3r {

class BambuConnect : public PrintHost
{
public:
    explicit BambuConnect(DynamicPrintConfig *config);

    const char* get_name() const override;

    bool test(wxString &msg) const override;
    wxString get_test_ok_msg() const override;
    wxString get_test_failed_msg(wxString &msg) const override;
    bool upload(PrintHostUpload upload_data, ProgressFn prorgess_fn, ErrorFn error_fn) const override;
    bool has_auto_discovery() const override;
    bool can_test() const override;
    PrintHostPostUploadActions get_post_upload_actions() const override;
    bool retain_source_file() const override;
    std::string get_host() const override;

private:
    static std::string encode_uri_component(const std::string &value);
};

} // namespace Slic3r

#endif
