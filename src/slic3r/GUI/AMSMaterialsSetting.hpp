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
#define AMS_MATERIALS_SETTING_COMBOX_WIDTH wxSize(FromDIP(250), FromDIP(30))
#define AMS_MATERIALS_SETTING_BUTTON_SIZE wxSize(FromDIP(90), FromDIP(24))
#define AMS_MATERIALS_SETTING_INPUT_SIZE wxSize(FromDIP(90), FromDIP(24))

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
    void is_empty(bool empty) {m_is_empty = empty;};

    void paintEvent(wxPaintEvent& evt);
    void render(wxDC& dc);
    void doRender(wxDC& dc);
};

class ColorPickerPopup : public PopupWindow
{
public:
    struct ColorItem
    {
        std::vector<wxColour> colors;
        int                   ctype = 0;
        wxString              name;
    };

    ScalableBitmap m_ts_bitmap_custom;
    wxStaticBitmap* m_ts_stbitmap_custom;
    StaticBox* m_custom_cp;
    wxColourData* m_clrData;
    StaticBox* m_def_color_box;
    wxFlexGridSizer* m_ams_fg_sizer;
    wxFlexGridSizer* m_other_fg_sizer;
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
    ColorPickerPopup(wxWindow* parent);
    ~ColorPickerPopup() {};
    void on_custom_clr_picker(wxMouseEvent& event);
    void set_ams_colours(const std::vector<ColorItem>& ams);
    void set_preset_colours(const std::vector<ColorItem>& preset_colors);
    void set_def_colour(wxColour col, std::vector<wxColour> cols = {}, int ctype = 0);
    const std::vector<wxColour>& get_selected_colours() const { return m_def_cols; }
    int get_selected_ctype() const { return m_def_ctype; }
    void paintEvent(wxPaintEvent& evt);
    void Popup();
    virtual void OnDismiss() wxOVERRIDE;
    virtual bool ProcessLeftDown(wxMouseEvent& event) wxOVERRIDE;

public:
};


class AMSMaterialsSetting : public DPIDialog
{
public:
    AMSMaterialsSetting(wxWindow *parent, wxWindowID id);
    ~AMSMaterialsSetting();
    void create();

	void paintEvent(wxPaintEvent &evt);
    void input_min_finish();
    void input_max_finish();
    void update();
    bool Show(bool show) override;
    void Popup(wxString filament = wxEmptyString, wxString sn = wxEmptyString,
               wxString temp_min = wxEmptyString, wxString temp_max = wxEmptyString,
               wxString k = wxEmptyString, wxString n = wxEmptyString);

    void post_select_event(int index);
    void TryRefreshPAProfiles();
    void set_color(wxColour color);
    void set_empty_color(wxColour color);
    void set_colors(std::vector<wxColour> colors);
    void set_ctype(int ctype);

    void on_picker_color(wxCommandEvent& color);
    MachineObject* obj{ nullptr };
    int            ams_id { 0 };        /* 0 ~ 3 */
    int            slot_id { 0 };        /* 0 ~ 3 */

    std::string    ams_filament_id;
    std::string    ams_setting_id;

    bool           m_is_third;
    // View-only mode: when set, the dialog can be opened to inspect filament
    // info but every editing control is disabled and no command is sent.
    // Used for 2D mode (laser/cut), mirroring the official-spool read-only flow.
    bool           m_view_only = false;
    wxString       m_brand_filament;
    wxString       m_brand_sn;
    wxString       m_brand_tmp;
    wxColour       m_brand_colour;
    std::string    m_filament_type;
    ColorPickerPopup m_color_picker_popup;
    ColorPicker *       m_clr_picker;
    Label*                 m_clr_name;
    std::vector<PACalibResult>  m_pa_profile_items;

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
    void on_select_filament(wxCommandEvent& evt);
    void update_filament_compatibility_hint();
    void on_select_cali_result(wxCommandEvent &evt);
    void on_select_nozzle_pos_id(wxCommandEvent &evt);
    void on_select_ok(wxCommandEvent &event);
    void on_select_reset(wxCommandEvent &event);
    void on_select_close(wxCommandEvent &event);
    void on_clr_picker(wxMouseEvent &event);
    bool is_virtual_tray();
    void update_widgets();

    void update_pa_profile_items();
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

protected:
    StateColor          m_btn_bg_green;
    StateColor          m_btn_bg_gray;
    wxPanel *           m_panel_normal;
    wxPanel *           m_panel_SN;
    wxStaticText *      m_sn_number;
    wxStaticText *      warning_text;
    wxStaticText *      m_filament_compatibility_hint { nullptr };
    //wxPanel *           m_panel_body;
    wxStaticText *      m_title_filament;
    wxStaticText *      m_title_nozzle_type;
    wxStaticText *      m_title_pa_profile;
    wxStaticText *      m_title_colour;
    wxStaticText *      m_title_temperature;
    TextInput *         m_input_nozzle_min;
    TextInput*          m_input_nozzle_max;
    ScalableBitmap *    degree;
    wxStaticBitmap *    bitmap_max_degree;
    wxStaticBitmap *    bitmap_min_degree;
    Button *            m_button_reset;
    Button *            m_button_confirm;
    Label*              m_tip_readonly;
    Button *            m_button_close;
    wxColourData *      m_clrData;

    wxPanel *           m_panel_kn;
    wxStaticText*       m_ratio_text;
    wxHyperlinkCtrl *   m_wiki_ctrl;
    wxStaticText*       m_k_param;
    TextInput*          m_input_k_val;
    wxStaticText*       m_n_param;
    TextInput*          m_input_n_val;
    int                 m_filament_selection { -1 };

    int m_pa_cali_select_id = 0;
    bool m_pa_data_pending{false};

    ComboBox *m_comboBox_filament;
    ComboBox * m_comboBox_nozzle_type;
    ComboBox * m_comboBox_cali_result;
    TextInput*       m_readonly_filament;

    std::map<std::string, FilamentInfos> map_filament_items;
    // Maps combobox item index → spool_id for Filament Manager entries.
    // Set by update_widgets(); empty for System Preset selections.
    std::map<int, std::string>           m_combo_idx_to_spool_id;
    // spool_id selected by the user in on_select_filament(); empty if a System
    // Preset was selected instead of a Filament Manager spool.
    std::string                          m_selected_spool_id;
};

wxDECLARE_EVENT(EVT_SELECTED_COLOR, wxCommandEvent);

// ---- AMSNewOfficialFilamentDlg ------------------------------------------
// Shown when an AMS slot detects a new official filament that is not yet in
// the Filament Manager. The user can choose how to handle it.
class AMSNewOfficialFilamentDlg : public DPIDialog
{
public:
    enum class Choice { RecordNew, LinkExisting, Skip };

    AMSNewOfficialFilamentDlg(wxWindow* parent);
    Choice GetChoice() const { return m_choice; }

    // Set the AMS tray context before each ShowModal() call so the
    // "Link to existing filament" dropdown can be populated with candidates.
    void SetTrayContext(MachineObject* obj,
                        const std::string& ams_id,
                        const std::string& slot_id);
    std::string GetSelectedLinkSpoolId() const { return m_selected_link_spool_id; }

    void on_dpi_changed(const wxRect&) override {
        m_btn_confirm->SetMinSize(AMS_MATERIALS_SETTING_BUTTON_SIZE);
        m_btn_confirm->SetCornerRadius(FromDIP(12));
        m_btn_cancel->SetMinSize(AMS_MATERIALS_SETTING_BUTTON_SIZE);
        m_btn_cancel->SetCornerRadius(FromDIP(12));
        Fit();
    }

private:
    void create();
    void populate_link_combo();
    void on_combo_selected(wxCommandEvent&);
    void on_confirm(wxCommandEvent&);
    void on_cancel(wxCommandEvent&);

    Choice    m_choice{ Choice::RecordNew };
    RadioBox* m_radio_record_new{ nullptr };
    RadioBox* m_radio_link_existing{ nullptr };
    RadioBox* m_radio_skip{ nullptr };
    Button*   m_btn_confirm{ nullptr };
    Button*   m_btn_cancel{ nullptr };

    ::ComboBox*                m_combo_link{ nullptr };
    wxSizer*                   m_combo_row{ nullptr };
    std::map<int, std::string> m_combo_idx_to_spool_id;
    std::map<int, wxString>    m_combo_header_texts;
    std::string                m_selected_link_spool_id;

    MachineObject* m_obj    { nullptr };
    std::string    m_ams_id;
    std::string    m_slot_id;
};

}} // namespace Slic3r::GUI

#endif
