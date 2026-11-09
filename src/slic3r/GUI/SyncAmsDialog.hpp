#pragma once

#include <map>
#include <memory>
#include <utility>
#include <vector>

#include <wx/defs.h> // wxID_CANCEL

#include "GUI_Utils.hpp"              // DPIDialog
#include "libslic3r/PresetBundle.hpp" // AMSMapInfo
#include "libslic3r/ProjectTask.hpp"  // FilamentInfo

class Label;
class ComboBox;
class ScalableButton;
class wxStaticBitmap;

namespace Slic3r::GUI {

class CapsuleButton;
class ModelPreview;
class MappingList;
class MappingRow;
class AmsSlotPopup;

/**
 * \brief The outcome the Plater applies after the Sync-AMS dialog closes.
 *
 * \ref dlg_res == \c wxID_CANCEL means the user cancelled and nothing should be applied.
 */
struct SyncResult
{
    int                       dlg_res         = wxID_CANCEL; ///< ShowModal outcome; wxID_CANCEL == user cancelled
    bool                      direct_sync     = true;        ///< overwrite directly, no per-slot mapping
    bool                      is_same_printer = true;
    std::map<int, AMSMapInfo> sync_maps;                     ///< project filament id -> chosen slot
};

/**
 * \brief The new Sync-AMS dialog with 3D preview and new UI
 */
class SyncAmsDialog : public DPIDialog
{
public:
    /**
     * \param parent      the owning window
     * \param deferrable  true when the app suggested the sync rather than the user asking for it; cancel then reads "Later"
     */
    explicit SyncAmsDialog(wxWindow *parent, bool deferrable = false);
    const SyncResult &get_result() const { return m_result; }

protected:
    void on_dpi_changed(const wxRect &suggested_rect) override;

private:
    wxWindow *build_header();
    wxSizer  *build_preview();
    wxWindow *build_preview_card(const wxString &title, bool with_plate_combo, ::Label **out_title, ModelPreview **out_preview);

    /**
     * \brief Build the mapping section (rows, hint, advanced options) as one panel, so overwrite mode can hide it as a whole.
     */
    wxWindow *build_mapping_section();

    /**
     * \brief Build the advanced options inside the mapping panel; they only affect a mapping sync.
     */
    wxSizer  *build_advanced();
    wxSizer  *build_footer(bool deferrable);

    /**
     * \brief Switch the dialog's mode: capsule selection, description, and mapping section visibility.
     *
     * \param overwrite  true for overwrite mode, false for mapping mode
     */
    void set_mode(bool overwrite);

    /**
     * \brief Show or hide the advanced-settings body.
     */
    void toggle_advanced();

    /**
     * \brief Fit the dialog height to its content and keep the user's width.
     *
     * A content change grows or shrinks the dialog by its own height, so the other sections keep their size.
     * A height the user dragged in or out is kept on top of the content height.
     */
    void relayout();

    /**
     * \brief Feed both previews the scene for the plate combo's selected plate.
     */
    void on_plate_changed();

    /**
     * \brief The plate combo's selected plate index, or -1 for all plates.
     */
    int current_plate();

    /**
     * \brief Repaint the right preview card to match the current mode.
     *
     * Mapping mode recolours each mapped source to its destination slot's colour; overwrite mode recolours
     * project filament i to the i-th synced tray's colour, as the overwrite replaces the list in tray order.
     *
     * \param reframe  true only when the plate geometry changed; false for a colour-only refresh (a pick,
     *                 policy change or mode switch) so the viewer keeps its orientation
     */
    void update_mapped_preview(bool reframe);

    /**
     * \brief Show the rows for every project filament. The plate combo only scopes the previews.
     */
    void rebuild_mapping();

    /**
     * \brief Match every project filament with the current strategy and refill the rows.
     *
     * Discards manual picks.
     */
    void remap_all();

    /**
     * \brief Match filaments against the selected machine and store the results in \ref m_mappings.
     *
     * Multi-nozzle printers rank slots by the strategy combo; every other printer uses the classic ranking.
     *
     * \param source  the filaments to (re)match; existing entries for them are replaced
     */
    void auto_map(const std::vector<FilamentInfo> &source);

    /**
     * \brief Refill the rows from \ref m_mappings, one per source, and refresh the list height and preview.
     *
     * \param source  the filaments to show; ones without a mapping are skipped
     */
    void fill_rows(const std::vector<FilamentInfo> &source);

    /**
     * \brief Open the AMS slot picker anchored under a row's destination cell.
     *
     * \param row      the row whose destination cell anchors the picker
     * \param source   the source filament; currently every slot is offered, regardless of type
     * \param current  the shown selection; receives the pick, so a reopen re-selects it
     */
    void open_slot_picker(MappingRow *row, const FilamentInfo &source, std::shared_ptr<FilamentInfo> current);

    /**
     * \brief Show the "reset all" button exactly while some mapping differs from its auto-map result.
     */
    void update_reset_all();

    /**
     * \brief Show the coarser-nozzle hint exactly while some mapping moves a filament off a 0.2 nozzle or onto a 0.8 one.
     *
     * \return whether the hint's visibility changed, i.e. the dialog needs a relayout
     */
    bool update_detail_loss_hint();

    /**
     * \brief Restore every mapping to its auto-map result, discarding manual picks and resets.
     */
    void reset_all();

    /**
     * \brief Fold the current rows into \ref m_result. Call on an OK close.
     *
     * Overwrite mode syncs directly; mapping mode records each mapped slot keyed by source filament id.
     */
    void deal_ok();

    AmsSlotPopup   *m_slot_popup{nullptr}; ///< created on first use, reused across rows
    CapsuleButton  *m_mode_map{nullptr};
    CapsuleButton  *m_mode_overwrite{nullptr};
    ::Label        *m_desc{nullptr};
    ::Label        *m_after_title{nullptr};
    ::ComboBox     *m_plate_combo{nullptr};
    ::ComboBox     *m_policy{nullptr}; ///< matching strategy; only multi-nozzle printers have one
    ModelPreview   *m_preview_orig{nullptr};
    ModelPreview   *m_preview_mapped{nullptr};
    wxWindow       *m_mapping_panel{nullptr};
    MappingList    *m_mapping_list{nullptr};
    ScalableButton *m_reset_all{nullptr};
    wxWindow       *m_detail_loss_hint{nullptr};
    wxWindow       *m_advanced_body{nullptr};
    wxStaticBitmap *m_advanced_icon{nullptr};
    bool            m_overwrite{false};
    bool            m_advanced_open{false};

    int m_fitted_h{0};      ///< the client height relayout() last set; 0 until the first fit
    int m_height_offset{0}; ///< the height the user dragged in or out, kept on top of the fitted content height

    SyncResult m_result; ///< produced by deal_ok(), read by the Plater after an OK close

    /// Source filament id -> its destination, for every project filament. A row's picker writes through
    /// the same shared_ptr, so this always reflects the latest pick.
    std::map<int, std::shared_ptr<FilamentInfo>> m_mappings;
    std::map<int, FilamentInfo>                  m_auto_mappings; ///< the auto-map result per source, restored by reset_all()
};

} // namespace Slic3r::GUI
