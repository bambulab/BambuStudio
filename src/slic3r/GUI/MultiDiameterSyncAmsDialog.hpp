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
    /// Re-fit the adaptive height once the base has built all content for a fresh show.
    bool Show(bool show) override;

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

    /// Fit the scroll area to the visible content (clamped to the screen) instead of a fixed tier,
    /// so the dialog height tracks its content and the user-added resize border stays in range.
    void apply_scroll_height(int height) override;

    /// Every AMS slot is pickable in this dialog (no filament-type restriction).
    bool sync_popup_match_all() const override { return true; }
    /// This dialog drops the "Are you sure to synchronize the filaments?" confirm line.
    bool needs_are_you_sure_title() const override { return false; }
    /// Overwrite replaces the project filaments directly and the twin preview already shows the
    /// before/after, so this dialog hides the redundant project-to-AMS list in Overwrite mode.
    bool override_shows_mapping_list() const override { return false; }
    /// Mapping here is driven by auto-map (Color-first / Precision-first), so there is nothing to
    /// manually reset; the reset-all button is never shown.
    bool shows_reset_all_btn() const override { return false; }
    /// After the base updates the mapping, reflect the chosen slot's nozzle diameter onto the row.
    void on_set_finish_mapping(wxCommandEvent& evt) override;
    /// Refresh the header's mode-specific description line (Match vs. Overwrite) on a mode switch.
    void on_mode_changed() override;

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
    /// Largest the scroll area may grow: ~90% of the current display's work area minus the dialog chrome.
    int max_scroll_height() const;
    /// Bound the width and height for resizing, tracking the current display/DPI.
    void apply_resizable_bounds();
    /// Let the content book stretch to the dialog width instead of clamping at the base width. Must be
    /// re-applied after any base mapping refresh, which re-pins the book to the base width.
    void relax_content_width();
    /// Size both plate previews to the current dialog width. Returns true when the size changed (so the
    /// caller can rescale the renders); a no-op when the width is unchanged.
    bool apply_preview_size();
    /// Size the header description and warning lines to the current dialog width so they re-wrap with it
    /// instead of at a fixed column. A no-op before the header is laid out.
    void apply_header_text_width();
    /// Width available to the content between the side margins, measured from the dialog so a narrow
    /// drag can shrink content back. Falls back to the design width before the first layout.
    int  usable_content_width() const;
    /// Re-tint the plain-wx surfaces StaticBox can't auto-map (mapping scroll backgrounds and the
    /// reparented preview titles) for the active theme. Runs on every show: the dialog is cached and
    /// can outlive a light/dark switch.
    void refresh_dynamic_colors();

    bool                m_layout_applied{false};       ///< Guards the one-time preview-panel surgery.
    wxScrolledWindow*   m_mapping_scroll{nullptr};     ///< Scrollable wrapper of the match-mode rows.
    wxScrolledWindow*   m_fix_mapping_scroll{nullptr}; ///< Scrollable wrapper of the override-mode rows.
    ::ComboBox*         m_priority_combo{nullptr};     ///< "Filament matching" Color-first/Precision-first selector.
    Label*              m_header_desc{nullptr};        ///< Header description line; text tracks the active map mode.
    Label*              m_header_warn{nullptr};        ///< Header warning line; re-wraps with the dialog width.
    FilamentMatchScheme m_scheme{FilamentMatchScheme::ColorFirst}; ///< Current auto-map strategy.
    wxWindow*           m_preview_card_orig{nullptr};   ///< "Original" preview card; re-centred as the width changes.
    wxWindow*           m_preview_card_mapped{nullptr}; ///< "Mapped" preview card; re-centred as the width changes.
    int                 m_preview_px{0};                ///< Last applied preview edge length (guards the render rescale).

    // SyncAmsInfoDialog keeps m_result private, so the fork owns its own (get_result/deal_ok use this).
    SyncResult        m_result;
};

} // namespace Slic3r::GUI
