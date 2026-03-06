#pragma once

#include <wx/panel.h>

namespace Slic3r {
class MachineObject;
}

namespace Slic3r::GUI {

class wgtDeviceAmsPreview;
class wgtDeviceAmsFilaments;

class wgtDeviceFilamentPanel : public wxPanel
{
public:
    explicit wgtDeviceFilamentPanel(wxWindow *parent);
    ~wgtDeviceFilamentPanel() override = default;

public:
    void UpdateByObj(Slic3r::MachineObject *obj_);
    wgtDeviceAmsFilaments* GetAmsFilamentsWidget() const { return m_ams_filaments; }

private:
    void CreateGui();

private:
    wgtDeviceAmsPreview   *m_ams_preview{nullptr};
    wgtDeviceAmsFilaments *m_ams_filaments{nullptr};
};

} // namespace Slic3r::GUI
