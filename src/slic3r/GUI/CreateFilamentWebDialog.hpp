#pragma once

#include <string>
#include <wx/wx.h>
#include "GUI_Utils.hpp"
#include <slic3r/GUI/Widgets/WebView.hpp>
#include <nlohmann/json.hpp>

namespace Slic3r { namespace GUI {

class CreateFilamentWebDialog : public DPIDialog
{
public:
    explicit CreateFilamentWebDialog(wxWindow *parent,
                                     const std::string &vendor = {},
                                     const std::string &type = {},
                                     const std::string &serial = {});
    ~CreateFilamentWebDialog();

protected:
    void on_dpi_changed(const wxRect &suggested_rect) override;

private:
    wxWebView *m_browser { nullptr };
    std::string m_prefill_vendor;
    std::string m_prefill_type;
    std::string m_prefill_serial;

    void OnScriptMessage(wxWebViewEvent &evt);
    void OnDocumentLoaded(wxWebViewEvent &evt);
    void OnError(wxWebViewEvent &evt);

    // Inject initial data (vendor list, type list, printer list, filament presets)
    // into the page after it has loaded.
    void send_init_data(const std::string &filament_type = "");
    void send_compatible_printers(const std::string &preset_name);
    void send_presets_by_machine(const std::vector<std::string> &printer_names, const std::string &filament_type);
    void send_all_printers(const std::string &filament_type);
    void send_device_info(const std::string &filament_type);
    void send_supported_types();
    void send_filament_params(const std::string &preset_name, const std::string &printer_preset);

    // Handle confirmed creation from Web
    void handle_create_filament(const nlohmann::json &j);
    void handle_create_filament_based_on_type(const nlohmann::json &j);
    void handle_create_filament_copy_presets(const nlohmann::json &j);

    void run_script(const wxString &js);
};

}} // namespace Slic3r::GUI
