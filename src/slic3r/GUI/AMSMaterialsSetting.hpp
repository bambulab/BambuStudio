#ifndef slic3r_AMSMaterialsSetting_hpp_
#define slic3r_AMSMaterialsSetting_hpp_

#include "libslic3r/Preset.hpp"
#include "wxExtensions.hpp"
#include "GUI_Utils.hpp"
#include "wx/clrpicker.h"
#include "Widgets/RadioBox.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/RoundedRectangle.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/TextInput.hpp"

#define AMS_MATERIALS_SETTING_DEF_COLOUR wxColour(255, 255, 255)
#define AMS_MATERIALS_SETTING_GREY800 wxColour(50, 58, 61)
#define AMS_MATERIALS_SETTING_GREY700 wxColour(107, 107, 107)
#define AMS_MATERIALS_SETTING_GREY300 wxColour(174,174,174)
#define AMS_MATERIALS_SETTING_GREY200 wxColour(248, 248, 248)
#define AMS_MATERIALS_SETTING_BODY_WIDTH FromDIP(340)
#define AMS_MATERIALS_SETTING_LABEL_WIDTH FromDIP(100)
#define AMS_MATERIALS_SETTING_COMBOX_WIDTH wxSize(FromDIP(240), FromDIP(30))
#define AMS_MATERIALS_SETTING_BUTTON_SIZE wxSize(FromDIP(90), FromDIP(30))
#define AMS_MATERIALS_SETTING_INPUT_SIZE wxSize(FromDIP(90), FromDIP(30))

namespace Slic3r { namespace GUI {

class AMSMaterialsSetting : public DPIDialog
{
public:
    AMSMaterialsSetting(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size, long style = 0 );
    ~AMSMaterialsSetting();
    void create();


	void on_select_ok(wxMouseEvent &event);

protected:
    void on_dpi_changed(const wxRect &suggested_rect) override;

protected:
    wxPanel *           m_panel_body;
    wxStaticText *      m_title_filament;
    ComboBox *          m_comboBox_filament;
    wxStaticText *      m_title_colour;
    wxColourPickerCtrl *m_colourPicker1;
    wxStaticText *      m_title_temperature;
    wxStaticText *      m_label_firstlayer;
    wxStaticText *      m_label_other;
    TextInput *         m_input_firstlayer;
    TextInput *         m_input_other;
    Button *            m_button_confirm;

};

}} // namespace Slic3r::GUI

#endif
