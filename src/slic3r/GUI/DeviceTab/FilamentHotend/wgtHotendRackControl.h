#pragma once

#include <wx/panel.h>

#include <string>

class Button;

namespace Slic3r {
class MachineObject;
} // namespace Slic3r

namespace Slic3r::GUI {

class wgtHotendRackControl : public wxPanel
{
public:
    explicit wgtHotendRackControl(wxWindow* parent);
    ~wgtHotendRackControl() override = default;

    void UpdateByMachine(Slic3r::MachineObject* obj);
    void SetControlEnabled(Slic3r::MachineObject* obj, bool enabled);
    void Rescale();

private:
    enum class RackMoveDir { Lift, Center, Lower };

    void create_ui();
    void update_state();
    void dispatch_rack_move(RackMoveDir dir);

    Slic3r::MachineObject* get_current_dev() const;

private:
    std::string m_devid;
    bool        m_control_enabled{ true };

    wxStaticText* m_label{ nullptr };
    Button*       m_btn_lift{ nullptr };
    Button*       m_btn_center{ nullptr };
    Button*       m_btn_lower{ nullptr };
};

} // namespace Slic3r::GUI
