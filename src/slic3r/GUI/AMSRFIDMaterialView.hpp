#ifndef slic3r_AMSRFIDMaterialView_hpp_
#define slic3r_AMSRFIDMaterialView_hpp_

#include "GUI_Utils.hpp"
#include "DeviceManager.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/Label.hpp"
#include "AMSMaterialsSetting.hpp"

namespace Slic3r { namespace GUI {

class AMSRFIDMaterialView : public AMSPaEditBase
{
public:
    AMSRFIDMaterialView(wxWindow* parent, wxWindowID id);
    ~AMSRFIDMaterialView() {}

    void Popup(MachineObject* obj, int ams_id, int slot_id,
               const wxString& brand_name, const wxString& color_name,
               const wxColour& color, const std::vector<wxColour>& cols, int ctype,
               const wxString& temp_min, const wxString& temp_max,
               const wxString& sn, const wxString& k_val);

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;
    void on_pa_history_ready() override;

private:
    void create();
    void apply_fixed_size();
    bool should_show_kn_section() const;
    void on_confirm();
    void on_reset();

    // top card
    ColorPicker*    m_clr_picker     {nullptr};
    wxStaticText*   m_lbl_brand      {nullptr};
    wxStaticText*   m_lbl_color_name {nullptr};

    // middle info panel
    StaticBox*      m_panel_info     {nullptr};
    wxStaticText*   m_lbl_temp       {nullptr};
    wxPanel*        m_panel_sn       {nullptr};
    wxStaticText*   m_lbl_sn         {nullptr};

    // flow dynamics section
    wxPanel*        m_panel_kn       {nullptr};

    // buttons
    Button*         m_button_confirm {nullptr};
    Button*         m_button_reset   {nullptr};
    StateColor      m_btn_bg_green;
    StateColor      m_btn_bg_gray;
};

}} // namespace Slic3r::GUI

#endif // slic3r_AMSRFIDMaterialView_hpp_
