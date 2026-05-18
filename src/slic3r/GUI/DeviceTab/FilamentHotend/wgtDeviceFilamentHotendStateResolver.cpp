#include "wgtDeviceFilamentHotendStateResolver.h"

#include "slic3r/GUI/I18N.hpp"

namespace Slic3r::GUI
{

wgtDeviceFilamentHotendLoadTipsModel wgtDeviceFilamentHotendStateResolver::ResolveLoadTipsModel(size_t selectable_count, size_t selected_count) const
{
    wgtDeviceFilamentHotendLoadTipsModel model{};
    model.select_all_checked = selectable_count > 0 && selectable_count == selected_count;
    model.select_all_enabled = selectable_count > 0;
    if (selected_count > 0) {
        model.summary_text = wxString::Format(_L("Selected %d slots"), static_cast<int>(selected_count));
    }
    return model;
}

} // namespace Slic3r::GUI
