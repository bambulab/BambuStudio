#pragma once

#include <wx/wx.h>
#include "GUI_Utils.hpp"
#include <slic3r/GUI/Widgets/WebView.hpp>
#include <nlohmann/json.hpp>
#include <string>

namespace Slic3r { namespace GUI {

class EditFilamentWebDialog : public DPIDialog
{
public:
    explicit EditFilamentWebDialog(wxWindow *parent, const std::string &filament_id);
    ~EditFilamentWebDialog();

    // Returns wxID_EDIT if user clicked "编辑预设", wxID_OK otherwise
    std::string get_edit_preset_name() const { return m_edit_preset_name; }

protected:
    void on_dpi_changed(const wxRect &suggested_rect) override;

private:
    wxWebView  *m_browser         { nullptr };
    std::string m_filament_id;
    std::string m_edit_preset_name;
    // cached for add-preset dialog
    std::string m_filament_name;
    std::string m_filament_type;
    std::string m_filament_vendor;

    void OnScriptMessage(wxWebViewEvent &evt);
    void OnDocumentLoaded(wxWebViewEvent &evt);
    void OnError(wxWebViewEvent &evt);

    void send_filament_edit_data();
    void run_script(const wxString &js);
};

}} // namespace Slic3r::GUI
