#ifndef slic3r_wgtFilaManagerPanel_h_
#define slic3r_wgtFilaManagerPanel_h_

#include <wx/panel.h>
#include <wx/webview.h>
#include "nlohmann/json.hpp"
#include <chrono>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Slic3r { namespace GUI {

#define FM_PROTOCOL_VERSION "1.0"

class wgtFilaManagerPanel : public wxPanel
{
public:
    wgtFilaManagerPanel(wxWindow* parent,
                        wxWindowID id   = wxID_ANY,
                        const wxPoint& pos  = wxDefaultPosition,
                        const wxSize& size  = wxDefaultSize,
                        long style = wxTAB_TRAVERSAL);
    ~wgtFilaManagerPanel();

    void msw_rescale();
    void on_sys_color_changed();
    bool Show(bool show) override;

private:
    /* ===== Bridge lifecycle ===== */
    void InitBridge();
    void OnWebMsg(wxWebViewEvent& evt);

    /* ===== C++ → JS channel ===== */
    void SendMsg(nlohmann::json msg);
    void send_response(int seq, int code, const nlohmann::json& data);
    void push_to_web(const std::string& command, const nlohmann::json& data);
    void flush_msg_queue();

    /* ===== Command registry ===== */
    using CmdHandler = std::function<void(int seq, const nlohmann::json& data)>;
    void register_handlers();
    std::unordered_map<std::string, CmdHandler> m_handlers;

    /* ===== Data builders ===== */
    nlohmann::json build_spool_list();
    nlohmann::json build_preset_options();
    nlohmann::json build_machine_list();
    nlohmann::json build_ams_data();

    static int64_t now_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    /* ===== State ===== */
    wxWebView*               m_browser       { nullptr };
    wxString                 m_home_url;
    bool                     m_bridge_ready  { false };
    std::vector<std::string> m_msg_queue;
};

}} // namespace Slic3r::GUI

#endif // slic3r_wgtFilaManagerPanel_h_
