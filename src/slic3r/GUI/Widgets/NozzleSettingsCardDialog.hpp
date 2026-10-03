#pragma once

#include <utility>
#include <vector>
#include <wx/string.h>

#include "slic3r/GUI/GUI_Utils.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/ExtruderPanel.hpp"

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
 * with the sidebar selector. In single-diameter mode (multi_nozzle_diameter_mode off) cards whose
 * diameter differs from that one are flagged; in multi-diameter mode every configured diameter is
 * valid and nothing is flagged.
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
    void build_warning_panels(wxBoxSizer *main_sizer);
    void build_button_bar(wxBoxSizer *main_sizer);
    void update_state();

    bool     m_multi_diameter_mode{false};
    wxString m_selected_diameter; ///< the diameter the plate slices at, as the caller passed it in

    std::vector<NozzleCard *> m_cards;

    NozzleAmsRow *m_ams_row{nullptr};

    wxPanel *m_warning_panel{nullptr};         // mixed-diameter note
    wxPanel *m_warning_serious_panel{nullptr}; // empty-nozzle note
    Label   *m_warning_note_label{nullptr};    // "mixed diameters need multi-diameter mode" note, above the legend
    Label   *m_warning_label{nullptr};         // wrapped after Fit so a long message doesn't widen the dialog
    Label   *m_warning_serious_label{nullptr};

    Button *m_confirm_btn{nullptr};
    Button *m_cancel_btn{nullptr};
};

} // namespace Slic3r::GUI
