#pragma once

#include <wx/panel.h>

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
    void SetControlEnabled(bool enabled);
    void Rescale();

private:
    enum class RackMoveDir { Lift, Center, Lower };

    void create_ui();
    void update_state();
    void dispatch_rack_move(RackMoveDir dir);

private:
    Slic3r::MachineObject* m_obj{ nullptr };
    bool                   m_control_enabled{ true };

    wxStaticText* m_label{ nullptr };
    Button*       m_btn_lift{ nullptr };
    Button*       m_btn_center{ nullptr };
    Button*       m_btn_lower{ nullptr };
};

} // namespace Slic3r::GUI
