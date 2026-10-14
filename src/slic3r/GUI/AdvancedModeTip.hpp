#pragma once

#include "Widgets/RoundedPopupWindow.hpp"

class wxStaticText;
class wxBoxSizer;

namespace Slic3r { namespace GUI {

// Hover tip shown over the Process panel's "Advance" mode switch: explains what
// advanced mode unlocks and links out to the Bambu Studio Academy course.
class AdvancedModeTip : public RoundedPopupWindow
{
public:
    explicit AdvancedModeTip(wxWindow *parent);

    // Positions and pops the card up. Built on wxPopupTransientWindow::Popup(), so
    // it stays open (and its link stays clickable, via wxPU_CONTAINS_CONTROLS) until
    // the user clicks anywhere outside it — no hover-driven auto-hide.
    void ShowTip(const wxPoint &pos);

private:
    wxBoxSizer   *m_sizer{nullptr};
    wxStaticText *m_body{nullptr};
    wxStaticText *m_link{nullptr};
};

}} // namespace Slic3r::GUI
