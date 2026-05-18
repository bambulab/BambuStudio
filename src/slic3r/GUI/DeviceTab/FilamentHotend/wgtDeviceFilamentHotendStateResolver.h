#pragma once

#include <cstddef>
#include <wx/string.h>

namespace Slic3r::GUI
{

struct wgtDeviceFilamentHotendLoadTipsModel
{
    bool select_all_checked{ false };
    bool select_all_enabled{ false };
    wxString summary_text;
};

class wgtDeviceFilamentHotendStateResolver
{
public:
    wgtDeviceFilamentHotendLoadTipsModel ResolveLoadTipsModel(size_t selectable_count, size_t selected_count) const;
};

} // namespace Slic3r::GUI
