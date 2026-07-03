#include "SystemShare.hpp"

class wxWindow;

namespace Slic3r { namespace GUI { namespace SystemShare {

// Used on Linux (no native share UI) and on Windows until the WinRT
// DataTransferManager implementation lands. ShareUrl in SystemShare.cpp
// detects HasNativePicker() == false and omits the "Share via System..."
// menu item, so the user sees only Open in browser + Copy link.

bool HasNativePicker() { return false; }

void OpenNativePicker(wxWindow * /*anchor*/, const wxString & /*url*/, const wxString & /*title*/)
{
    // No-op. Cannot be reached unless HasNativePicker() returns true.
}

}}} // namespace Slic3r::GUI::SystemShare
