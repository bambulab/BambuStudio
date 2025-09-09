#ifndef slic3r_GUI_HelioReleaseNote_hpp_
#define slic3r_GUI_HelioReleaseNote_hpp_

#include <limits>
#include <wx/wx.h>
#include <wx/intl.h>
#include <wx/collpane.h>
#include <wx/dataview.h>
#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/dataview.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/string.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/hyperlink.h>
#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/popupwin.h>
#include <wx/spinctrl.h>
#include <wx/artprov.h>
#include <wx/wrapsizer.h>
#include <wx/event.h>
#include <wx/hyperlink.h>
#include <wx/richtext/richtextctrl.h>

#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"
#include "HelioDragon.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/LinkLabel.hpp"
#include "Widgets/ScrolledWindow.hpp"
#include <wx/hashmap.h>
#include <wx/webview.h>


namespace Slic3r { namespace GUI {

wxDECLARE_EVENT(EVT_SECONDARY_CHECK_CONFIRM, wxCommandEvent);
wxDECLARE_EVENT(EVT_SECONDARY_CHECK_CANCEL, wxCommandEvent);
wxDECLARE_EVENT(EVT_SECONDARY_CHECK_RETRY, wxCommandEvent);
wxDECLARE_EVENT(EVT_SECONDARY_CHECK_DONE, wxCommandEvent);
wxDECLARE_EVENT(EVT_SECONDARY_CHECK_RESUME, wxCommandEvent);
wxDECLARE_EVENT(EVT_UPDATE_NOZZLE, wxCommandEvent);
wxDECLARE_EVENT(EVT_UPDATE_TEXT_MSG, wxCommandEvent);
wxDECLARE_EVENT(EVT_ERROR_DIALOG_BTN_CLICKED, wxCommandEvent);


class HelioStatementDialog : public DPIDialog
{
private:
    Label *m_title{nullptr};
    Button *m_button_confirm{nullptr};
    Button *m_button_cancel{nullptr};

    int current_page{ 0 };
    std::shared_ptr<int> shared_ptr{nullptr};

    wxPanel* page1_panel{ nullptr };
    wxPanel* page2_panel{ nullptr };
    wxPanel* page3_panel{ nullptr };

    bool page1_agree{ false };
    bool page2_agree{ false };

    Label* pat_err_label{ nullptr };
    TextInput* helio_input_pat{ nullptr };
    wxStaticBitmap* helio_pat_refresh{ nullptr };
    wxStaticBitmap* helio_pat_eview{ nullptr };
    wxStaticBitmap* helio_pat_dview{ nullptr };
    wxStaticBitmap* helio_pat_copy{ nullptr };

public:
    HelioStatementDialog(wxWindow *parent = nullptr);
    ~HelioStatementDialog() {};

    // void on_ok(wxMouseEvent &evt);
    void on_dpi_changed(const wxRect &suggested_rect) override;
    void show_err_info(std::string type);
    void show_pat_option(std::string opt);
    void show_agreement_page1();
    void show_agreement_page2();
    void show_pat_page();
    void request_pat();
    void on_confirm(wxMouseEvent& e);
    void report_consent_install();
    void open_url(std::string type);

    void OnLoaded(wxWebViewEvent& event);
    void OnTitleChanged(wxWebViewEvent& event);
    void OnError(wxWebViewEvent& event);
};

class HelioRemainUsageTime : public wxPanel
{
public:
    Label* label_click_to_use{ nullptr };
    Label* label_click_to_buy{ nullptr };
    HelioRemainUsageTime(wxWindow *parent = nullptr, wxString label = wxEmptyString);

public:
    void UpdateRemainTime(int remain_time);
    void UpdateHelpTips(int type);

private:
    void Create(wxString label);
   
private:
    int    m_remain_usage_time = 0;
    Label* m_label_remain_usage_time;
};

class HelioInputDialog : public DPIDialog
{
private:
    bool use_advanced_settings{false};
    bool only_advanced_settings{false};

    CustomToggleButton* togglebutton_simulate{nullptr};
    CustomToggleButton* togglebutton_optimize{nullptr};

    wxPanel* last_tid_panel{nullptr};
    Label*   last_tid_label{nullptr};

    std::map<std::string, TextInput*> m_input_items;
    std::map<std::string, ComboBox*> m_combo_items;
    Button* m_button_confirm{nullptr};
    wxString m_lastValidValue = wxEmptyString;

    wxPanel* panel_simulation{nullptr};
    wxPanel* panel_pay_optimization{nullptr};
    wxPanel* panel_optimization{nullptr};

    wxPanel* advanced_settings_link{nullptr};
    LinkLabel* buy_now_link{nullptr};
    LinkLabel* helio_wiki_link{nullptr};

    int current_action{-1}; //0-simulation 1-optimization
    int support_optimization{0}; //-1-no 0-yes
    int remaining_optimization_times{0};

    wxStaticBitmap* advanced_options_icon{nullptr};
    wxPanel* panel_advanced_option{nullptr};

    std::shared_ptr<int> shared_ptr{nullptr};

    HelioRemainUsageTime* m_remain_usage_time{nullptr};
    HelioRemainUsageTime* m_remain_purchased_time{nullptr};
public:
    HelioInputDialog(wxWindow *parent = nullptr);
    ~HelioInputDialog() {};

public:
    int get_action() const { return current_action; }

    HelioQuery::SimulationInput get_simulation_input(bool& ok);
    HelioQuery::OptimizationInput get_optimization_input(bool& ok);

private:
    wxBoxSizer* create_input_item(wxWindow* parent, std::string key, wxString name, wxString unit,
                                  const std::vector<std::shared_ptr<TextInputValChecker>>& checkers);
    wxBoxSizer* create_combo_item(wxWindow* parent, std::string key,  wxString name, std::map<int, wxString> combolist, int def);
    wxBoxSizer* create_input_optimize_layers(wxWindow* parent, int layer_count);

    void on_selected_simulation(wxMouseEvent& e) { update_action(0); }
    void on_selected_optimaztion(wxMouseEvent& e){ update_action(1); }
    void on_confirm(wxMouseEvent& e);
    void on_dpi_changed(const wxRect& suggested_rect) override;
    void update_action(int action);
    void show_advanced_mode();
};

class HelioPatNotEnoughDialog : public DPIDialog
{
public:
    HelioPatNotEnoughDialog(wxWindow* parent = nullptr);
    ~HelioPatNotEnoughDialog();
    void on_dpi_changed(const wxRect& suggested_rect) override;
};

}} // namespace Slic3r::GUI

#endif
