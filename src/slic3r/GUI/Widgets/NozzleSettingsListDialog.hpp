#pragma once

#include <utility>
#include <vector>
#include <wx/string.h>

#include "slic3r/GUI/GUI_Utils.hpp"
#include "slic3r/GUI/ExtruderPanel.hpp"
#include "slic3r/GUI/ExtruderHelpers.hpp"

class wxBoxSizer;

namespace Slic3r::GUI {

class DiameterButtonPanel;
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
                           const wxString                  &selected_diameter,
                           const std::vector<NozzleConfig> &left_configs,
                           const std::vector<NozzleConfig> &right_configs,
                           CustomSeparator::State           separator_state = CustomSeparator::State::Hidden);

    wxString                  GetSelectedDiameter() const;
    std::vector<NozzleConfig> GetExtruderConfigs(int index) const; // 0 = left, 1 = right

    void on_dpi_changed(const wxRect &suggested_rect) override;

private:
    void create_layout(const wxString &selected_diameter, const std::vector<NozzleConfig> &left_seeds, const std::vector<NozzleConfig> &right_seeds);
    void build_diameter_selector(wxBoxSizer *main_sizer, const wxString &selected_diameter);
    void build_columns(wxBoxSizer *main_sizer, const std::vector<NozzleConfig> &left_seeds, const std::vector<NozzleConfig> &right_seeds);
    void build_warning_panels(wxBoxSizer *main_sizer);
    void build_button_bar(wxBoxSizer *main_sizer);
    void setup_callbacks();
    void update_dialog_state();
    void align_separator_icon();
    void on_ams_count_changed(); // relayout/grow when a column's AMS preview count changes
    void show_badge(bool show);  // machine-wide synced badge (all extruders synced)
    void layout_badge();

    // What the printer reported when this dialog opened. The synced badge shows only when the
    // printer is synced AND the edited configs still match it, slot-for-slot.
    MachineNozzleBaseline m_machine_baseline;

    DiameterButtonPanel *m_diameter_panel{nullptr};
    wxPanel             *m_columns_container{nullptr}; // holds the columns; badge anchors here
    NozzleColumn        *m_left_column{nullptr};
    NozzleColumn        *m_right_column{nullptr};
    CustomSeparator     *m_separator{nullptr};
    wxStaticBitmap      *m_badge{nullptr}; // machine-wide synced badge (top-right of container)

    wxPanel *m_warning_panel{nullptr};         // mixed-diameter note
    wxPanel *m_warning_serious_panel{nullptr}; // empty-nozzle note

    Button *m_confirm_btn{nullptr};
    Button *m_cancel_btn{nullptr};
};

} // namespace Slic3r::GUI