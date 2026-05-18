#pragma once

#include <wx/panel.h>
#include <wx/simplebook.h>
#include <map>

#include "slic3r/GUI/Widgets/AnimaController.hpp"
#include "wgtDeviceHotendItem.h"

class Label;

namespace Slic3r {
class MachineObject;
}

namespace Slic3r::GUI {

class wgtDeviceHotentPanel : public wxPanel
{
public:
    explicit wgtDeviceHotentPanel(wxWindow *parent);
    ~wgtDeviceHotentPanel() override = default;
    void UpdateByMachine(Slic3r::MachineObject *machine_obj);
    void ReadAll();

private:
    void CreateGui();
    void CreateRefreshPage(wxPanel *parent);

private:
    // normal content page / reading page
    wxSimplebook *m_simple_book{nullptr};
    wxPanel      *m_panel_content{nullptr};
    wxPanel      *m_panel_refresh{nullptr};

    // reading page controls
    AnimaIcon *m_refresh_icon{nullptr};
    Label     *m_progress_refresh{nullptr};

    // key: rack nozzle id 0~8, value: hotend item pointer
    std::map<int, wgtDeviceHotendItem *> m_hotend_items;

    MachineObject *m_obj{nullptr};
};

} // namespace Slic3r::GUI
