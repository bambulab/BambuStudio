#ifndef slic3r_AMSMaterialsSetting_hpp_
#define slic3r_AMSMaterialsSetting_hpp_

#include "libslic3r/Preset.hpp"
#include "wxExtensions.hpp"
#include "GUI_Utils.hpp"
#include "DeviceManager.hpp"
#include "wx/clrpicker.h"
#include "Widgets/RadioBox.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/RoundedRectangle.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/TextInput.hpp"

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
    wxColour        m_colour;
    std::vector<wxColour>        m_cols;
    bool            m_selected{false};
    bool            m_show_full{false};
    
    ColorPicker(wxWindow* parent, wxWindowID id, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize);
    ~ColorPicker();

    void msw_rescale();
    void set_color(wxColour col);
    void set_colors(std::vector<wxColour>  cols);
    void set_selected(bool sel) {m_selected = sel;Refresh();};
    void set_show_full(bool full) {m_show_full = full;Refresh();};

    void paintEvent(wxPaintEvent& evt);
    void render(wxDC& dc);
    void doRender(wxDC& dc);
};

class ColorPickerPopup : public PopupWindow
{
public:
    StaticBox* m_def_color_box;
    wxFlexGridSizer* m_ams_fg_sizer;
    wxColour m_def_col;
    std::vector<wxColour> m_def_colors;
    std::vector<wxColour> m_ams_colors;
    std::vector<ColorPicker*> m_color_pickers;
    std::vector<ColorPicker*> m_ams_color_pickers;

public:
    ColorPickerPopup(wxWindow* parent);
    ~ColorPickerPopup() {};
    void set_ams_colours(std::vector<wxColour> ams);
    void set_def_colour(wxColour col);
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
    void enable_confirm_button(bool en);
    bool Show(bool show) override;
    void Popup(wxString filament = wxEmptyString, wxString sn = wxEmptyString,
               wxString temp_min = wxEmptyString, wxString temp_max = wxEmptyString,
               wxString k = wxEmptyString, wxString n = wxEmptyString);

    void post_select_event();
    void msw_rescale();
    void set_color(wxColour color);
    void set_colors(std::vector<wxColour> colors);

    void on_picker_color(wxCommandEvent& color);
    MachineObject* obj{ nullptr };
    int            ams_id { 0 };        /* 0 ~ 3 */
    int            tray_id { 0 };       /* 0 ~ 3 */

    std::string    ams_filament_id;
    std::string    ams_setting_id;

    bool           m_is_third;
    wxString       m_brand_filament;
    wxString       m_brand_sn;
    wxString       m_brand_tmp;
    wxColour       m_brand_colour;
    std::string    m_filament_type;
    ColorPickerPopup m_color_picker_popup;

protected:
    void create_panel_normal(wxWindow* parent);
    void create_panel_kn(wxWindow* parent);
    void on_dpi_changed(const wxRect &suggested_rect) override;
    void on_select_filament(wxCommandEvent& evt);
    void on_select_ok(wxCommandEvent &event);
    void on_select_reset(wxCommandEvent &event);
    void on_select_close(wxCommandEvent &event);
    void on_clr_picker(wxMouseEvent &event);
    bool is_virtual_tray();
    void update_widgets();

protected:
    StateColor          m_btn_bg_green;
    StateColor          m_btn_bg_gray;
    wxPanel *           m_panel_normal;
    wxPanel *           m_panel_SN;
    wxStaticText *      m_sn_number;
    wxStaticText *      warning_text;
    //wxPanel *           m_panel_body;
    wxStaticText *      m_title_filament;
    wxStaticText *      m_title_colour;
    wxStaticText *      m_title_temperature;
    TextInput *         m_input_nozzle_min;
    TextInput*          m_input_nozzle_max;
    Button *            m_button_reset;
    Button *            m_button_confirm;
    wxStaticText*       m_tip_readonly;
    Button *            m_button_close;
    ColorPicker *       m_clr_picker;
    wxColourData *      m_clrData;

    wxPanel *           m_panel_kn;
    wxStaticText*       m_ratio_text;
    wxStaticText*       m_k_param;
    TextInput*          m_input_k_val;
    wxStaticText*       m_n_param;
    TextInput*          m_input_n_val;
    int                 m_filament_selection;

#ifdef __APPLE__
    wxComboBox *m_comboBox_filament;
#else
    ComboBox *m_comboBox_filament;
#endif
    TextInput*       m_readonly_filament;
};

wxDECLARE_EVENT(EVT_SELECTED_COLOR, wxCommandEvent);

}} // namespace Slic3r::GUI

#endif
