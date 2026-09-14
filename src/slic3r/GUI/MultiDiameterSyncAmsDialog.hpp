#pragma once

#include <vector>
#include "SyncAmsInfoDialog.hpp"

class ComboBox;

namespace Slic3r::GUI {

/**
 * \brief Filament→AMS auto-map strategy chosen by the "Filament matching" selector.
 *
 * ColorFirst matches by filament type and colour, ignoring nozzle diameter. PrecisionFirst also
 * enforces the nozzle diameter (a filament only auto-maps to a slot of its own diameter). Either way
 * the mapping popup still lets the user override to any slot.
 */
enum class FilamentMatchScheme { ColorFirst, PrecisionFirst };

/**
 * \brief Sync-AMS dialog for single-extruder multi-nozzle (multi-diameter) machines.
 *
 * Forks SyncAmsInfoDialog: reuses the base preview / plate / mode / advanced-settings machinery and
 * the shared MaterialSyncItem + AmsMapingPopup mapping flow, changing only the mapping row layout to
 * a horizontal [source → dest ▾] and letting every AMS slot be picked in the popup. A "Filament
 * matching" selector chooses whether the initial auto-map enforces nozzle diameter; it is shown on
 * both cells either way, and the destination diameter is highlighted when it differs from the source.
 */
class MultiDiameterSyncAmsDialog : public SyncAmsInfoDialog
{
public:
    MultiDiameterSyncAmsDialog(wxWindow* parent, SyncInfo& info);
    ~MultiDiameterSyncAmsDialog() override = default;

protected:
    /// Build the horizontal mapping rows (Match mode) in place of the base chip grid.
    void reset_and_sync_ams_list() override;
    /// Build the same horizontal rows for Override mode (display-only, no click-to-remap).
    void generate_override_fix_ams_list() override;
    /// Auto-map via the base, then stamp each row's dest nozzle diameter / volume type.
    bool do_ams_mapping(MachineObject* obj_) override;
    /// Precision-first restricts the auto-map to slots whose nozzle diameter and flow type match the
    /// source filament (checked before colour); Color-first ignores the nozzle.
    bool use_nozzle_type_match() const override { return m_scheme == FilamentMatchScheme::PrecisionFirst; }
    /// Build the sync result from this dialog's mapping (records each slot's own nozzle diameter).
    void deal_ok() override;
    const SyncResult& get_result() override { return m_result; }
    /// Re-apply the equal-size previews and layout after a DPI change.
    void on_dpi_changed(const wxRect& suggested_rect) override;

    /// This layout stacks two previews above the mapping rows, so it needs more height than the base.
    int dialog_height_min()    const override { return FromDIP(680); }
    int dialog_height_middle() const override { return FromDIP(690); }
    int dialog_height_max()    const override { return FromDIP(720); }

    /// Every AMS slot is pickable in this dialog (no filament-type restriction).
    bool sync_popup_match_all() const override { return true; }
    /// After the base updates the mapping, reflect the chosen slot's nozzle diameter onto the row.
    void on_set_finish_mapping(wxCommandEvent& evt) override;

private:
    /// Rework the base's twin-preview panel: drop the swipe buttons and plate row, park the plate
    /// ComboBox beside the "Original" title, make both previews equal-size and collapsed by default.
    /// Idempotent (re-runs on DPI change).
    void apply_multi_diameter_layout();
    void collect_project_filaments(std::vector<std::string>& materials,
                                   std::vector<std::string>& display_materials,
                                   std::vector<std::string>& display_names,
                                   std::vector<std::string>& brands,
                                   std::vector<std::string>& filament_ids) const;
    /// Build the "Filament matching" title + Color-first/Precision-first selector into m_sizer_ams_mapping.
    void build_priority_toggle();
    /// Build one horizontal MaterialSyncItemH row per project filament into the given panel/sizer.
    /// Interactive rows are wired to open the AMS mapping popup.
    void build_sync_rows(wxWindow* panel, wxFlexGridSizer* sizer, MaterialHash& material_list,
                         std::vector<FilamentInfo>& filaments, bool interactive);
    /// Reflect a mapping result onto a row list: dest colour / name / nozzle diameter (or "Unmapped").
    void paint_items(MachineObject* obj, MaterialHash& list, const std::vector<FilamentInfo>& result);
    /// Which AMS / external sources the matcher may use for the current machine.
    std::vector<bool> default_map_opt(MachineObject* obj) const;
    /// Move a mapping grid into a vertical-only scroll window centred in its panel (>5 rows scroll).
    void wrap_mapping_scroll(wxWindow* owner, wxBoxSizer* owner_sizer, wxFlexGridSizer* grid,
                             wxScrolledWindow*& out_scroll);
    /// Fit a mapping scroll window: cap the height so more than 5 filament rows scroll; centre rows.
    void refresh_mapping_scroll(wxScrolledWindow* scroll, wxFlexGridSizer* grid, size_t filament_rows);

    bool                m_layout_applied{false};       ///< Guards the one-time preview-panel surgery.
    wxScrolledWindow*   m_mapping_scroll{nullptr};     ///< Scrollable wrapper of the match-mode rows.
    wxScrolledWindow*   m_fix_mapping_scroll{nullptr}; ///< Scrollable wrapper of the override-mode rows.
    ::ComboBox*         m_priority_combo{nullptr};     ///< "Filament matching" Color-first/Precision-first selector.
    FilamentMatchScheme m_scheme{FilamentMatchScheme::ColorFirst}; ///< Current auto-map strategy.

    // SyncAmsInfoDialog keeps m_result private, so the fork owns its own (get_result/deal_ok use this).
    SyncResult        m_result;
};

} // namespace Slic3r::GUI
