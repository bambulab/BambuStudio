#ifndef slic3r_AMSMaterialsSetting_hpp_
#define slic3r_AMSMaterialsSetting_hpp_

#include "libslic3r/Preset.hpp"
#include "wxExtensions.hpp"
#include "GUI_Utils.hpp"
#include "DeviceManager.hpp"
#include "wx/clrpicker.h"
#include "wx/colourdata.h"
#include "Widgets/RadioBox.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/RoundedRectangle.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/TextInput.hpp"
#include "../slic3r/Utils/CalibUtils.hpp"
#include "DeviceCore/DevNozzleRack.h"
#include "fila_manager/wgtFilaManagerStore.h"

#define AMS_MATERIALS_SETTING_DEF_COLOUR wxColour(255, 255, 255)
#define AMS_MATERIALS_SETTING_GREY900 wxColour(38, 46, 48)
#define AMS_MATERIALS_SETTING_GREY800 wxColour(50, 58, 61)
#define AMS_MATERIALS_SETTING_GREY700 wxColour(107, 107, 107)
#define AMS_MATERIALS_SETTING_GREY300 wxColour(174,174,174)
#define AMS_MATERIALS_SETTING_GREY200 wxColour(248, 248, 248)
#define AMS_MATERIALS_SETTING_BODY_WIDTH FromDIP(380)
#define AMS_MATERIALS_SETTING_LABEL_WIDTH FromDIP(80)
#define AMS_MATERIALS_SETTING_CALI_COL_INDENT FromDIP(150)
#define AMS_MATERIALS_SETTING_COMBOX_WIDTH wxSize(FromDIP(250), FromDIP(30))
#define AMS_MATERIALS_SETTING_BUTTON_SIZE wxSize(FromDIP(90), FromDIP(24))
#define AMS_MATERIALS_SETTING_INPUT_SIZE wxSize(FromDIP(90), FromDIP(24))
#define AMS_MATERIALS_SETTING_DIALOG_SIZE wxSize(FromDIP(526), FromDIP(503))
// Interior width available for the read-only tip: dialog width minus the 20px left + 20px right margins.
#define AMS_MATERIALS_SETTING_TIP_WIDTH (AMS_MATERIALS_SETTING_DIALOG_SIZE.GetWidth() - FromDIP(40))

namespace Slic3r { namespace GUI {

class ColorPicker : public wxWindow
{
public:
    wxBitmap        m_bitmap_border;
    wxBitmap        m_bitmap_border_dark;
    wxBitmap        m_bitmap_transparent;
    ScalableBitmap  m_bitmap_transparent_def; //default transparent material

    wxColour        m_colour;
    std::vector<wxColour>        m_cols;
    wxString        m_label;
    bool            m_selected{false};
    bool            m_show_full{false};
    bool            m_is_empty{false};
    int             ctype = 0;

    bool            transparent_changed{false};

    ColorPicker(wxWindow* parent, wxWindowID id, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize);
    ~ColorPicker();

    void msw_rescale();
    void set_color(wxColour col);
    void set_colors(std::vector<wxColour>  cols);
    void set_selected(bool sel) {m_selected = sel;Refresh();};
    void set_show_full(bool full) {m_show_full = full;Refresh();};
    void set_label(const wxString& label) {m_label = label;Refresh();};
    void is_empty(bool empty) {m_is_empty = empty;};

    void paintEvent(wxPaintEvent& evt);
    void render(wxDC& dc);
    void doRender(wxDC& dc);
};

class ColorPickerPopup : public wxPanel
{
public:
    struct ColorItem
    {
        std::vector<wxColour> colors;
        int                   ctype = 0;
        wxString              name;
    };

    wxWindow* m_evt_target{nullptr};
    wxStaticBitmap* m_custom_plus;
    StaticBox* m_custom_cp;
    wxColourData* m_clrData;
    StaticBox* m_def_color_box;
    wxFlexGridSizer* m_color_fg_sizer;
    wxColour m_def_col;
    std::vector<wxColour> m_def_cols;
    int m_def_ctype = 0;
    std::vector<wxColour> m_def_colors;
    std::vector<ColorItem> m_ams_color_items;
    std::vector<ColorPicker*> m_color_pickers;
    std::vector<ColorPicker*> m_default_color_pickers;
    std::vector<ColorPicker*> m_ams_color_pickers;
    std::vector<ColorPicker*> m_preset_color_pickers;

public:
    ColorPickerPopup(wxWindow* parent, wxWindow* evt_target);
    ~ColorPickerPopup() {};
    void on_custom_clr_picker(wxMouseEvent& event);
    void relayout_colours();
    void set_ams_colours(const std::vector<ColorItem>& ams);
    void set_preset_colours(const std::vector<ColorItem>& preset_colors);
    void set_def_colour(wxColour col, std::vector<wxColour> cols = {}, int ctype = 0);
    const std::vector<wxColour>& get_selected_colours() const { return m_def_cols; }
    int get_selected_ctype() const { return m_def_ctype; }

public:
};


class AMSPaEditBase : public DPIDialog
{
public:
    AMSPaEditBase(wxWindow* parent, wxWindowID id, const wxString& title,
                    const wxPoint& pos, const wxSize& size, long style)
        : DPIDialog(parent, id, title, pos, size, style) {}

    void TryRefreshPAProfiles();

    MachineObject* obj{nullptr};
    int            ams_id{0};
    int            slot_id{0};
    std::string    ams_filament_id;

protected:
    void update_pa_profile_items();

    void on_select_cali_result(wxCommandEvent& evt);

    bool save_pa_profile_selection();

    bool is_virtual_tray();

    void update_kval_editability();

    virtual bool get_nozzle_type_override(int /*extruder_id*/,
                                          float& /*nozzle_diameter*/,
                                          NozzleFlowType& /*nozzle_flow_type*/) { return false; }

    virtual void on_pa_history_ready() = 0;

    std::vector<PACalibResult> m_pa_profile_items;
    int  m_pa_cali_select_id{0};
    bool m_pa_data_pending{false};

    ComboBox*  m_comboBox_cali_result{nullptr};
    TextInput* m_input_k_val{nullptr};
    TextInput* m_input_n_val{nullptr};

    void reset_calibration(const std::string& selected_filament_id);
};

class AMSMaterialsSetting : public AMSPaEditBase
{
public:
    AMSMaterialsSetting(wxWindow *parent, wxWindowID id);
    ~AMSMaterialsSetting();
    void create();

    void update();
    void update_nozzle_temp_display();
    bool Show(bool show) override;
    void Popup(wxString filament = wxEmptyString, wxString sn = wxEmptyString,
               wxString temp_min = wxEmptyString, wxString temp_max = wxEmptyString,
               wxString k = wxEmptyString, wxString n = wxEmptyString);

    void trigger_select_filament(const std::string& spool_id, bool from_printer, const wxString& display_text);
    void set_color(wxColour color);
    void set_empty_color(wxColour color);
    void set_colors(std::vector<wxColour> colors);
    void set_ctype(int ctype);

    void on_picker_color(wxCommandEvent& color);
    std::string    ams_setting_id;

    bool           m_is_third;
    bool           m_confirmed = false;
    bool           m_view_only = false;
    wxString       m_brand_filament;
    wxString       m_brand_sn;
    wxString       m_brand_tmp;
    wxColour       m_brand_colour;
    std::string    m_filament_type;
    ColorPickerPopup*   m_color_picker_popup{nullptr};
    ColorPicker *       m_clr_picker;
    Label*                 m_clr_name;

    struct FilamentInfos {
        std::string filament_id;
        std::string setting_id;
        std::string spool_id;   // non-empty only when entry comes from Filament Manager
    };

protected:
    void create_panel_normal(wxWindow* parent);
    void create_panel_kn(wxWindow* parent);
    void on_dpi_changed(const wxRect &suggested_rect) override;
    void on_select_nozzle_id(wxCommandEvent &evt);
    void apply_filament_selection();
    void on_select_nozzle_pos_id(wxCommandEvent &evt);
    void on_select_ok(wxCommandEvent &event);
    void on_select_reset(wxCommandEvent &event);
    void on_select_close(wxCommandEvent &event);
    void on_open_filament_select_dialog();
    void set_filament_box_text(const wxString& text);   // set the filament name shown in the jump box
    void enable_filament_box(bool enable);              // enable/disable the jump box + grey the text
    bool colour_editable() const;
    bool colour_palette_visible() const;
    void apply_dialog_size();
    void update_widgets();

    void update_filament_editing(bool is_printing);
    void update_nozzle_combo(MachineObject* obj);
    int  get_nozzle_combo_id_code() const;
    int  get_nozzle_sel_by_sn(MachineObject* obj, const std::string& sn);
    int  get_cali_index_by_ams_slot(MachineObject* obj, int ams_id, int slot_id);
    std::vector<ColorPickerPopup::ColorItem> get_preset_color_items(const std::string& filament_id) const;

    void get_filaments_info(const MachineObject*                     obj,
                            const std::string&                       nozzle_diameter_str,
                            wxArrayString&                           filament_items,
                            std::map<std::string, FilamentInfos>&    map_filament_items,
                            std::unordered_map<wxString, wxString>&  query_filament_vendors,
                            std::unordered_map<wxString, wxString>&  query_filament_types);

    Preset* get_filament_by_id(const std::string& filament_id, bool is_system);

    // AMSPaEditBase overrides
    bool get_nozzle_type_override(int extruder_id,
                                  float& nozzle_diameter,
                                  NozzleFlowType& nozzle_flow_type) override;
    void on_pa_history_ready() override;

protected:
    StateColor          m_btn_bg_green;
    StateColor          m_btn_bg_gray;
    wxPanel *           m_panel_normal;
    wxPanel *           m_panel_SN;
    wxStaticText *      m_sn_number;
    //wxPanel *           m_panel_body;
    wxStaticText *      m_title_filament;
    wxStaticText *      m_title_nozzle_type;
    wxSizerItem *       m_nozzle_type_spacer_item { nullptr };
    wxStaticText *      m_title_pa_profile;
    wxStaticText *      m_title_colour;
    wxString            m_nozzle_temp_min_str;
    wxString            m_nozzle_temp_max_str;
    StaticBox*          m_panel_temperature;
    Label*              m_nozzle_temp_label;
    TextInput*          m_input_total_weight;
    TextInput*          m_input_remain_weight;
    Button *            m_button_reset;
    Button *            m_button_confirm;
    Label*              m_tip_readonly;
    Button *            m_button_close;
    wxColourData *      m_clrData;

    wxPanel *           m_panel_kn;
    wxStaticText*       m_ratio_text;
    Label*              m_wiki_ctrl;
    wxStaticText*       m_k_param;
    wxStaticText*       m_n_param;
    bool m_has_initial_filament_weight{ false };
    int  m_initial_total_weight{ 0 };
    int  m_initial_remain_weight{ 0 };

    StaticBox*      m_filament_box{nullptr};    // clickable jump box (was m_comboBox_filament)
    Label*          m_filament_text{nullptr};   // filament name shown left-aligned in the box
    wxStaticBitmap* m_filament_arrow{nullptr};  // right-side jump arrow
    bool            m_filament_box_editable{true};  // gate clicks without disabling native controls (keeps font consistent)
    wxString   m_current_filament_alias;
    ComboBox * m_comboBox_nozzle_type;

    std::map<std::string, FilamentInfos> map_filament_items;
    std::string                          m_selected_spool_id;
    std::string                          m_pending_spool_id;
    bool                                 m_pending_from_printer{false};

    std::string  m_open_spool_id;
    wxColour     m_open_colour;
    std::string  m_open_filament_id;
    bool         m_snap_taken{false};
};

wxDECLARE_EVENT(EVT_SELECTED_COLOR, wxCommandEvent);

// AMSNewOfficialFilamentDlg
class AMSNewOfficialFilamentDlg : public DPIDialog
{
public:
    enum class Choice { RecordNew, LinkExisting, Skip };

    AMSNewOfficialFilamentDlg(wxWindow* parent);
    Choice GetChoice() const { return m_choice; }

    void SetTrayContext(MachineObject* obj,
                        const std::string& ams_id,
                        const std::string& slot_id);
    void SetSoftMatchData(const SoftMatchPendingResponse& data);
    std::string GetSelectedLinkSpoolId() const { return m_selected_link_spool_id; }
    int GetHitSpoolId() const          { return m_hit_spool_id; }
    int GetSelectedCandidateId() const { return m_selected_candidate_id; }

    void on_dpi_changed(const wxRect&) override {
        m_btn_confirm->SetMinSize(AMS_MATERIALS_SETTING_BUTTON_SIZE);
        m_btn_confirm->SetCornerRadius(FromDIP(12));
        if (m_btn_record_new) {
            m_btn_record_new->SetMinSize(AMS_MATERIALS_SETTING_BUTTON_SIZE);
            m_btn_record_new->SetCornerRadius(FromDIP(12));
        }
        if (m_combo_link)
            m_combo_link->SetMinSize(wxSize(FromDIP(360), FromDIP(56)));
        // Raw-bitmap cards don't auto-rescale; re-render them at the new DPI.
        if (m_obj) populate_link_combo();
        Fit();
    }

private:
    void create();
    void populate_link_combo();
    void on_combo_selected(wxCommandEvent&);
    void on_confirm(wxCommandEvent&);
    void on_record_new(wxCommandEvent&);

    Choice    m_choice{ Choice::RecordNew };
    wxStaticText*   m_title{ nullptr };
    wxStaticBitmap* m_hit_card{ nullptr };
    wxStaticText*   m_match_label{ nullptr };
    Button*   m_btn_record_new{ nullptr };
    Button*   m_btn_confirm{ nullptr };

    ::ComboBox*                m_combo_link{ nullptr };
    wxSizer*                   m_combo_row{ nullptr };
    std::map<int, std::string>   m_combo_idx_to_spool_id;
    std::map<int, FilamentSpool> m_combo_idx_to_spool;
    std::string                m_selected_link_spool_id;

    MachineObject* m_obj    { nullptr };
    std::string    m_ams_id;
    std::string    m_slot_id;

    SoftMatchPendingResponse m_soft_match_data;
    FilamentSpool m_hit_spool;              // hit shown in the collapsed box by default
    int  m_prev_combo_sel{ -1 };            // last picked candidate index (-1 = none); for re-click toggle
    int  m_hit_spool_id{ 0 };
    int  m_selected_candidate_id{ 0 };
    bool m_only_hit{ false };
};

// AMSNewFilamentRecordedDlg
class AMSNewFilamentRecordedDlg : public DPIDialog
{
public:
    AMSNewFilamentRecordedDlg(wxWindow* parent, const FilamentSpool& sp);

    void on_dpi_changed(const wxRect&) override { Fit(); }

private:
    void create(const FilamentSpool& sp);
};

wxString spool_display_name(const FilamentSpool& sp);

}} // namespace Slic3r::GUI

#endif
