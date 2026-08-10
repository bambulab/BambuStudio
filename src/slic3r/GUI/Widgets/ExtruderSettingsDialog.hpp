#pragma once

#include <utility>
#include <vector>
#include <wx/string.h>

#include "slic3r/GUI/GUI_Utils.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/ExtruderPanel.hpp"

class wxBoxSizer;
class CheckBox;

namespace Slic3r::GUI {

class DiameterButtonPanel;
class NozzleColumn;

// Editable per-extruder nozzle configuration dialog.
//
// Seeded from the caller's maintained in-memory configs (NOT re-collected from a
// MachineObject): the machine may be absent/disconnected, and the user may have
// already edited the config, so the passed-in vectors are the source of truth.
class ExtruderSettingsDialog : public DPIDialog
{
public:
    ExtruderSettingsDialog(wxWindow                        *parent,
                           bool                             is_dual,
                           const wxString                  &selected_diameter,
                           const std::vector<NozzleConfig> &left_configs,
                           const std::vector<NozzleConfig> &right_configs   = {},
                           CustomSeparator::State           separator_state = CustomSeparator::State::Hidden);

    wxString                  GetSelectedDiameter() const;
    std::vector<NozzleConfig> GetExtruderConfigs(int index) const; // 0 = left/single, 1 = right
    // Project-level multi_nozzle_diameter_mode as left by the user. Off keeps the single-diameter flow.
    bool                      GetMultiDiameterMode() const;

    void on_dpi_changed(const wxRect &suggested_rect) override;

private:
    void create_layout(const wxString &selected_diameter, const std::vector<NozzleConfig> &left_seeds, const std::vector<NozzleConfig> &right_seeds);
    void build_diameter_selector(wxBoxSizer *main_sizer, const wxString &selected_diameter);
    void build_columns(wxBoxSizer *main_sizer, const std::vector<NozzleConfig> &left_seeds, const std::vector<NozzleConfig> &right_seeds);
    void build_multi_diameter_option(wxBoxSizer *main_sizer);
    void build_warning_panels(wxBoxSizer *main_sizer);
    void build_button_bar(wxBoxSizer *main_sizer);
    void setup_callbacks();
    void update_dialog_state();
    void align_separator_icon();
    void on_ams_count_changed(); // relayout/grow when a column's AMS preview count changes
    void restore_ams_counts();   // revert the global AMS-count registry to the on-open snapshot
    void show_badge(bool show);  // machine-wide synced badge (all extruders synced)
    void layout_badge();

    bool m_is_dual{false};

    // Snapshot of the per-extruder AMS counts at open, restored on Cancel (the count
    // registry is a live global side-effect of the add/remove popups). Pairs are {n4, n1}.
    std::pair<int, int> m_ams_snapshot[2]{{0, 0}, {0, 0}};

    // The per-extruder configs the dialog opened with (from the machine at open). The
    // synced badge shows when the edited configs still match these, slot-for-slot.
    std::vector<NozzleConfig> m_seed_configs[2];

    DiameterButtonPanel *m_diameter_panel{nullptr};
    wxPanel             *m_columns_container{nullptr}; // holds the columns; badge anchors here
    NozzleColumn        *m_left_column{nullptr};
    NozzleColumn        *m_right_column{nullptr};
    CustomSeparator     *m_separator{nullptr};
    wxStaticBitmap      *m_badge{nullptr}; // machine-wide synced badge (top-right of container)

    CheckBox *m_multi_diameter_check{nullptr}; // project-level multi_nozzle_diameter_mode

    wxPanel *m_warning_panel{nullptr};         // mixed-diameter note
    wxPanel *m_warning_serious_panel{nullptr}; // empty-nozzle note

    Button *m_confirm_btn{nullptr};
    Button *m_cancel_btn{nullptr};
};

} // namespace Slic3r::GUI