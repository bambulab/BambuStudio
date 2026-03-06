#pragma once

#include <wx/panel.h>

class Label;

namespace Slic3r {
class MachineObject;
}

namespace Slic3r::GUI
{

class wgtDeviceHotentPanel : public wxPanel
{
public:
    explicit wgtDeviceHotentPanel(wxWindow* parent);
    ~wgtDeviceHotentPanel() override = default;
    void UpdateByObj(Slic3r::MachineObject* obj_);

private:
    void CreateGui();

private:
    Label* m_label_title{ nullptr };
};

} // namespace Slic3r::GUI
