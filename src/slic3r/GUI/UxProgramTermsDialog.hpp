#pragma once

#include "slic3r/GUI/GUI_Utils.hpp"

#include <string>
#include <wx/string.h>

class wxWebView;

namespace Slic3r { namespace GUI {

class UxProgramTermsDialog : public DPIDialog
{
public:
    explicit UxProgramTermsDialog(wxWindow* parent);

private:
    wxWebView* m_webview{nullptr};
    std::string m_host_url;

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;
};

}} // namespace Slic3r::GUI
