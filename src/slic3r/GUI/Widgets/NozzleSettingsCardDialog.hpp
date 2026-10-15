#pragma once

#include <utility>
#include <vector>
#include <wx/string.h>

#include "slic3r/GUI/GUI_Utils.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/ExtruderPanel.hpp"
#include "slic3r/GUI/NozzleSetupTelemetry.hpp"

class wxBoxSizer;
class PopupWindow;

namespace Slic3r::GUI {

class NozzleCard;
class NozzleAmsRow;

/**
 * \brief Card-grid nozzle settings dialog for singel extruder multi nozzle printers
 *
 * Layout is a two-row grid by nozzle number: even numbers on top, odd on the bottom. The dialog
 * records which nozzle is installed in which slot; picking the diameter the plate slices at stays
 * with the sidebar selector.
 */
class NozzleSettingsCardDialog : public DPIDialog
{
public:
    NozzleSettingsCardDialog(wxWindow *parent, const wxString &selected_diameter, const std::vector<NozzleConfig> &configs);

    wxString                  GetSelectedDiameter() const;
    std::vector<NozzleConfig> GetExtruderConfigs(int index) const; ///< configs for index 0; empty otherwise

    void on_dpi_changed(const wxRect &suggested_rect) override;

private:
    void create_layout(const std::vector<NozzleConfig> &seeds);
    void build_card_grid(wxBoxSizer *main_sizer, const std::vector<NozzleConfig> &seeds);
    void build_ams_row(wxBoxSizer *main_sizer);
    void build_button_bar(wxBoxSizer *main_sizer);
    void update_state();

    // Asked before a card takes a new diameter/flow. Returns false to leave that card untouched.
    // Only speaks up for the case the user cannot see coming: the nozzle being edited is the last
    // one of its kind on the rack and project filaments are pointing at it, so confirming here will
    // move them elsewhere. Everything else passes through silently.
    bool confirm_nozzle_change(const NozzleConfig &from);

    wxString m_selected_diameter; ///< the diameter the plate slices at, as the caller passed it in

    std::vector<NozzleCard *> m_cards;

    NozzleAmsRow *m_ams_row{nullptr};

    NozzleSetupEditTally m_edit_tally; // edits of this session; reported only when confirmed

    Button *m_confirm_btn{nullptr};
    Button *m_cancel_btn{nullptr};
};

} // namespace Slic3r::GUI
