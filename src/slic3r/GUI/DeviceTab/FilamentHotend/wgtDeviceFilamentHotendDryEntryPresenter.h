#pragma once

#include <wx/string.h>

class wxWindow;

namespace Slic3r::GUI
{

class wgtDeviceFilamentHotendDryEntryPresenter
{
public:
    wxString GetCapabilityBoundaryMessage() const;
    void ShowCapabilityBoundaryTip(wxWindow* parent) const;
};

} // namespace Slic3r::GUI
