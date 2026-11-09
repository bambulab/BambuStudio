#pragma once

#include <utility>
#include <vector>
#include <wx/string.h>

#include "slic3r/GUI/GUI_Utils.hpp"
#include "slic3r/GUI/ExtruderPanel.hpp"
#include "slic3r/GUI/ExtruderHelpers.hpp"

class wxBoxSizer;

namespace Slic3r::GUI {

class NozzleColumn;

// Editable nozzle configuration dialog for dual-extruder printers with a nozzle rack.
//
// Seeded from the caller's maintained in-memory configs (NOT re-collected from a
// MachineObject): the machine may be absent/disconnected, and the user may have
// already edited the config, so the passed-in vectors are the source of truth for what
// is *displayed*. Whether that is *synced* is a separate question, answered at open by
// collect_machine_nozzle_baseline() (ExtruderHelpers), which asks the printer.
class NozzleSettingsListDialog : public DPIDialog
{
public:
    NozzleSettingsListDialog(wxWindow                        *parent,
                           const std::vector<NozzleConfig> &left_configs,
                           const std::vector<NozzleConfig> &right_configs);

    std::vector<NozzleConfig> GetExtruderConfigs(int index) const; // 0 = left, 1 = right

    void on_dpi_changed(const wxRect &suggested_rect) override;

private:
    void create_layout(const std::vector<NozzleConfig> &left_seeds, const std::vector<NozzleConfig> &right_seeds);
    void build_columns(wxBoxSizer *main_sizer, const std::vector<NozzleConfig> &left_seeds, const std::vector<NozzleConfig> &right_seeds);
    void build_printer_status(wxBoxSizer *main_sizer);
    void build_button_bar(wxBoxSizer *main_sizer);
    void update_dialog_state();
    void align_separator_icon();
    void on_ams_count_changed(); // relayout/grow when a column's AMS preview count changes
    void show_synced(bool show); // synced badge, shown when all extruders are synced
    void layout_badge();

    // What the printer reported when this dialog opened. The synced badge shows only when the
    // printer is synced AND the edited configs still match it, slot-for-slot.
    MachineNozzleBaseline m_machine_baseline;

    wxPanel             *m_columns_container{nullptr}; // holds the columns; badge anchors here
    NozzleColumn        *m_left_column{nullptr};
    NozzleColumn        *m_right_column{nullptr};
    CustomSeparator     *m_separator{nullptr};
    wxStaticBitmap      *m_badge{nullptr}; // machine-wide synced badge (top-right of container)

    Button *m_confirm_btn{nullptr};
    Button *m_cancel_btn{nullptr};
};

} // namespace Slic3r::GUI