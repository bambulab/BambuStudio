#include "wgtDeviceFilamentHotendDryEntryPresenter.h"

#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MsgDialog.hpp"

namespace Slic3r::GUI
{

wxString wgtDeviceFilamentHotendDryEntryPresenter::GetCapabilityBoundaryMessage() const
{
    return _L("Drying is not available in this phase. This entry is for capability preview only.");
}

void wgtDeviceFilamentHotendDryEntryPresenter::ShowCapabilityBoundaryTip(wxWindow* parent) const
{
    MessageDialog msg_dlg(parent, GetCapabilityBoundaryMessage(), _L("Dry"), wxICON_INFORMATION | wxOK);
    msg_dlg.ShowModal();
}

} // namespace Slic3r::GUI
