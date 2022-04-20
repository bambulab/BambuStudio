#ifndef slic3r_UpgradePanel_hpp_
#define slic3r_UpgradePanel_hpp_

#include <wx/panel.h>
#include <slic3r/GUI/Widgets/Button.hpp>
#include <slic3r/GUI/DeviceManager.hpp>
#include <slic3r/GUI/Widgets/ScrolledWindow.hpp>

namespace Slic3r {
namespace GUI {

class MachineInfoPanel : public wxPanel
{
protected:
    wxPanel* m_panel_caption;
    wxStaticText* m_staticText_machine_name;
    wxPanel* m_panel_content;
    wxStaticBitmap* m_bitmap_machine;
    wxStaticText* m_staticText_model_title;
    wxStaticText* m_staticText_model;
    wxStaticText* m_staticText_serial_number_title;
    wxStaticText* m_staticText_serial_number;
    wxStaticText* m_staticText_software_version_title;
    wxStaticText* m_staticText_software_version;
    Button* m_button_upgrade_firmware;
    void upgrade_firmware(wxCommandEvent& event);

public:
    MachineInfoPanel(wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = wxTAB_TRAVERSAL, const wxString& name = wxEmptyString);
    ~MachineInfoPanel();
    void update(MachineObject *obj, FirmwareInfo info);
    void msw_rescale() {}

    MachineObject *m_obj{nullptr};
    FirmwareInfo  m_info;
};


class UpgradePanel : public wxPanel
{
protected:
    wxScrolledWindow* m_scrolledWindow;
    wxBoxSizer* m_machine_list_sizer;
    std::vector<MachineInfoPanel*> m_machine_info_panels;
    std::vector<FirmwareInfo>      m_firmware_info;
    

public:
    UpgradePanel(wxWindow *parent, wxWindowID id = wxID_ANY, const wxPoint &pos = wxDefaultPosition, const wxSize &size = wxDefaultSize, long style = wxTAB_TRAVERSAL);
    ~UpgradePanel();
    void clean_machine_info_list();
    //void show_machine_info_list(bool show = true);
    void update_machine_info_list(std::vector<MachineObject*>& obj_list);
    void updata_machine_firmware_info(MachineObject *obj);
    void msw_rescale() {}
    bool Show(bool show = true) override;

    void update(MachineObject *obj_);

    MachineObject *obj { nullptr };
};

}
}

#endif
