#include "wgtDeviceHotentPanel.h"

#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"
#include "slic3r/GUI/wxExtensions.hpp"

#include <wx/sizer.h>

namespace Slic3r::GUI
{

wgtDeviceHotentPanel::wgtDeviceHotentPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
{
    CreateGui();
}

void wgtDeviceHotentPanel::UpdateByObj(Slic3r::MachineObject* obj_)
{
    (void)obj_;
}

void wgtDeviceHotentPanel::CreateGui()
{
    SetBackgroundColour(*wxWHITE);

    wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);
    m_label_title = new Label(this, _L("wgtDeviceHotentPanel"));
    m_label_title->SetFont(Label::Body_14);
    root->Add(m_label_title, 0, wxALL, FromDIP(8));

    SetSizer(root);
    Layout();
}

} // namespace Slic3r::GUI
