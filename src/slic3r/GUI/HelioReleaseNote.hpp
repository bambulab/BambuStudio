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
#include "libslic3r/ExtrusionEntity.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/LinkLabel.hpp"
#include "Widgets/ScrolledWindow.hpp"
#include <wx/hashmap.h>
#include <wx/webview.h>
#include <wx/html/htmlwin.h>


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

    int current_page{ 0 }; // 0 = legal terms page, 1 = PAT page
    std::shared_ptr<int> shared_ptr{nullptr};

    wxPanel* page_legal_panel{ nullptr };
    wxPanel* page_pat_panel{ nullptr };

    // Accordion sections
    wxPanel* terms_section_panel{ nullptr };
    wxPanel* terms_content_panel{ nullptr };
    wxPanel* privacy_section_panel{ nullptr };
    wxPanel* privacy_content_panel{ nullptr };
    wxScrolledWindow* m_scroll_panel{ nullptr };
    bool terms_expanded{ true };
    bool privacy_expanded{ true };

    // Checkbox for agreement
    ::CheckBox* m_agree_checkbox{ nullptr };

    Label* pat_err_label{ nullptr };
    TextInput* helio_input_pat{ nullptr };
    wxStaticBitmap* helio_pat_refresh{ nullptr };
    wxStaticBitmap* helio_pat_eview{ nullptr };
    wxStaticBitmap* helio_pat_dview{ nullptr };
    wxStaticBitmap* helio_pat_copy{ nullptr };
    Button* copy_pat_button{ nullptr };
    
    int m_original_tooltip_delay{500};

public:
    HelioStatementDialog(wxWindow *parent = nullptr);
    ~HelioStatementDialog();

    void on_dpi_changed(const wxRect &suggested_rect) override;
    void show_err_info(std::string type);
    void show_pat_option(std::string opt);
    void show_legal_page();
    void show_pat_page();
    void request_pat();
    void on_confirm(wxMouseEvent& e);
    void report_consent_install();
    void open_url(std::string type);
    void create_legal_page();
    void create_pat_page();
    void toggle_terms_section();
    void toggle_privacy_section();
    void update_confirm_button_state();
    void refresh_checkbox_visual();

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

// Theme colors for HelioInputDialog
struct HelioInputDialogTheme {
    wxColour bg;           // Main background
    wxColour card;         // Card background
    wxColour card2;        // Slightly darker card (for inputs)
    wxColour border;       // Card border
    wxColour text;         // Primary text
    wxColour muted;        // Secondary/muted text
    wxColour purple;       // Purple accent (simulation)
    wxColour blue;         // Blue accent (optimization)
};

class HelioCheckBadgePanel;

class HelioInputDialog : public DPIDialog
{
private:
    bool use_advanced_settings{false};
    bool only_advanced_settings{false};
    bool is_no_chamber{false};

    // Mode card panels (replacing toggle buttons)
    wxPanel* simulation_card_panel{nullptr};
    wxPanel* optimization_card_panel{nullptr};
    Label* simulation_card_title{nullptr};
    Label* simulation_card_subtitle{nullptr};
    Label* optimization_card_title{nullptr};
    Label* optimization_card_subtitle{nullptr};
    wxStaticBitmap* simulation_mode_icon{nullptr};
    wxStaticBitmap* optimization_mode_icon{nullptr};
    wxBitmap simulation_icon_color;
    wxBitmap simulation_icon_gray;
    wxBitmap optimization_icon_color;
    wxBitmap optimization_icon_gray;

    // Keep toggle button pointers for compatibility
    CustomToggleButton* togglebutton_simulate{nullptr};
    CustomToggleButton* togglebutton_optimize{nullptr};

    wxPanel* last_tid_panel{nullptr};
    Label*   last_tid_label{nullptr};

    std::map<std::string, TextInput*> m_input_items;
    std::map<std::string, ComboBox*> m_combo_items;
    Button* m_button_confirm{nullptr};
    wxString m_lastValidValue = wxEmptyString;

    std::string m_material_id;
    std::vector<HelioQuery::PrintPriorityOption> m_print_priority_options;
    std::vector<HelioQuery::PrintPriorityOption> m_available_print_priority_options;
    bool m_print_priority_loading{false};
    bool m_using_fallback_print_priority{false};  // Track if using hard-coded fallback options (old method)

    wxPanel* panel_simulation{nullptr};
    wxPanel* panel_pay_optimization{nullptr};
    wxPanel* panel_optimization{nullptr};
    wxPanel* panel_velocity_volumetric{nullptr};

    // Card wrapper panels
    wxPanel* card_simulation{nullptr};
    wxPanel* card_account_status{nullptr};
    wxPanel* card_environment{nullptr};
    wxPanel* card_optimization_settings{nullptr};

    wxPanel* advanced_settings_link{nullptr};
    LinkLabel* buy_now_link{nullptr};
    Button* buy_now_button{nullptr};
    LinkLabel* helio_wiki_link{nullptr};

    int current_action{-1}; //0-simulation 1-optimization
    int support_optimization{0}; //-1-no 0-yes
    int remaining_optimization_times{0};

    wxStaticBitmap* advanced_options_icon{nullptr};
    wxPanel* panel_advanced_option{nullptr};

    std::shared_ptr<int> shared_ptr{nullptr};

    Label* m_label_subscription{nullptr};
    Label* m_label_monthly_quota{nullptr};
    Label* m_label_addons{nullptr};
    bool m_free_trial_eligible{false};
    bool m_is_free_trial_active{false};
    bool m_is_free_trial_claimed{false};
    
    // Theme helper
    HelioInputDialogTheme get_theme() const;
    
    int m_original_tooltip_delay{500};
public:
    HelioInputDialog(wxWindow *parent = nullptr, const std::string& material_id = "");
    ~HelioInputDialog();

public:
    int get_action() const { return current_action; }

    HelioQuery::SimulationInput get_simulation_input(bool& ok);
    HelioQuery::OptimizationInput get_optimization_input(bool& ok);
    
    // Force "Slicer default" limits mode (disables "Helio default" option)
    void set_force_slicer_default(bool force);

private:
    wxBoxSizer* create_input_item(wxWindow* parent, std::string key, wxString name, wxString unit,
                                  const std::vector<std::shared_ptr<TextInputValChecker>>& checkers);
    wxBoxSizer* create_combo_item(wxWindow* parent, std::string key,  wxString name, std::map<int, wxString> combolist, int def, int width = 120);
    wxBoxSizer* create_input_optimize_layers(wxWindow* parent, int layer_count);

    // Card creation helper
    wxPanel* create_card_panel(wxWindow* parent, const wxString& title = wxEmptyString);
    void update_mode_card_styling(int selected_action);

    // Print priority helper methods
    wxBoxSizer* create_print_priority_combo(wxWindow* parent);
    void populate_print_priority_dropdown(ComboBox* combobox);
    void fetch_print_priority_options();
    void update_print_priority_dropdown();

    void on_selected_simulation(wxMouseEvent& e) { update_action(0); }
    void on_selected_optimaztion(wxMouseEvent& e){ update_action(1); }
    void on_confirm(wxMouseEvent& e);
    void on_dpi_changed(const wxRect& suggested_rect) override;
    void update_action(int action);
    void show_advanced_mode();
    void on_unlimited_click(wxMouseEvent& e);
    
public:
    void set_initial_action(int action) { update_action(action); }
};

class HelioPatNotEnoughDialog : public DPIDialog
{
public:
    HelioPatNotEnoughDialog(wxWindow* parent = nullptr);
    ~HelioPatNotEnoughDialog();
    void on_dpi_changed(const wxRect& suggested_rect) override;
};


class HelioRatingDialog : public DPIDialog
{
public:
    HelioRatingDialog(wxWindow *parent = nullptr, int original = 0, int optimized = 0, std::string mean_impro = "", std::string std_impro = "");
    ~HelioRatingDialog() {};

    wxString format_improvement(wxString imp);
    void on_dpi_changed(const wxRect &suggested_rect) override;
    std::shared_ptr<int> shared_ptr{nullptr};
    void show_rating(std::vector<wxStaticBitmap *> stars, int rating);
    int original_time;
    int optimized_time;
    std::string optimized_id;
    bool finish_rating = false;
    wxString quality_mean_improvement;
    wxString quality_std_improvement;   
};

class HelioSimulationResultsDialog : public DPIDialog
{
public:
    HelioSimulationResultsDialog(wxWindow *parent = nullptr, 
                                  HelioQuery::SimulationResult simulation = HelioQuery::SimulationResult(),
                                  int original_print_time_seconds = 0,
                                  const std::vector<std::pair<ExtrusionRole, float>>& roles_times = {});
    ~HelioSimulationResultsDialog() {};

    void on_dpi_changed(const wxRect &suggested_rect) override;
    void on_enhance_speed_quality(wxMouseEvent& event);
    void on_view_details(wxMouseEvent& event);

private:
    HelioQuery::SimulationResult m_simulation;
    int m_original_print_time_seconds;
    std::vector<std::pair<ExtrusionRole, float>> m_roles_times;
    Button* m_button_enhance{nullptr};
    Button* m_button_view_details{nullptr};
    Button* m_button_close{nullptr};
    
    // Fix suggestions expandable section
    wxPanel* m_fix_suggestions_content{nullptr};
    Label* m_fix_suggestions_arrow{nullptr};
    Label* m_fix_suggestions_preview{nullptr};
    bool m_fix_suggestions_expanded{false};
    
    // Nested expanders within fix suggestions
    wxPanel* m_advanced_content{nullptr};
    Label* m_advanced_arrow{nullptr};
    bool m_advanced_expanded{false};
    
    wxPanel* m_expert_content{nullptr};
    Label* m_expert_arrow{nullptr};
    bool m_expert_expanded{false};
    
    wxPanel* m_learn_more_content{nullptr};
    Label* m_learn_more_arrow{nullptr};
    bool m_learn_more_expanded{false};
    
    wxScrolledWindow* m_fix_suggestions_scroll{nullptr};
    
    HelioInputDialogTheme get_theme() const;
    wxString get_outcome_text(const HelioQuery::PrintInfo& print_info);
    wxString get_analysis_text(const HelioQuery::PrintInfo& print_info);
    wxString get_fix_suggestions_preview(const HelioQuery::PrintInfo& print_info);
    wxString format_time_improvement(int original_seconds, double speed_factor);
    void toggle_fix_suggestions();
    void toggle_advanced();
    void toggle_expert();
    void toggle_learn_more();
    void create_fix_suggestions_section(wxBoxSizer* parent_sizer, const HelioInputDialogTheme& theme);
};

}} // namespace Slic3r::GUI

#endif
