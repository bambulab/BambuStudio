#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "GUI_Utils.hpp"    // DPIDialog
#include "ISyncAmsDialog.hpp" // GUI::SyncResult

class Label;
class ComboBox;
class wxStaticBitmap;

namespace Slic3r {
struct FilamentInfo;
namespace GUI {

class CapsuleButton;
class ModelPreview;
class MappingList;
class MappingRow;
class AmsSlotPopup;

/**
 * \brief The new Sync-AMS dialog with 3D preview and new UI
 */
class SyncAmsDialog : public DPIDialog
{
public:
    explicit SyncAmsDialog(wxWindow *parent);
    const SyncResult &get_result() const { return m_result; }

protected:
    void on_dpi_changed(const wxRect &suggested_rect) override;

private:
    wxWindow *build_header();
    wxSizer  *build_preview();
    wxWindow *build_preview_card(const wxString &title, const wxString &tag, bool with_plate_combo, ::Label **out_title,
                                 ModelPreview **out_preview);

    /**
     * \brief Build the mapping section as one panel, so overwrite mode can hide it as a whole.
     */
    wxWindow *build_mapping_section();
    wxSizer  *build_advanced();
    wxSizer  *build_footer();

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
     * \brief Re-fit the dialog to its content.
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
     * Mapping mode recolours each mapped source to its destination slot's colour; overwrite mode shows the
     * plate in its own colours, since an overwrite replaces the whole list rather than remapping per source.
     *
     * \param reframe  true only when the plate geometry changed; false for a colour-only refresh (a pick,
     *                 policy change or mode switch) so the viewer keeps its orientation
     */
    void update_mapped_preview(bool reframe);

    /**
     * \brief Show the rows for the plate combo's scope, matching only filaments that have no mapping yet.
     *
     * Earlier results, including manual picks, survive a plate switch.
     */
    void rebuild_mapping();

    /**
     * \brief Re-match every filament mapped so far with the current strategy and refill the rows.
     *
     * Discards manual picks.
     */
    void remap_all();

    /**
     * \brief Match filaments against the selected machine and store the results in \ref m_mappings.
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
    ::ComboBox     *m_policy{nullptr};
    ModelPreview   *m_preview_orig{nullptr};
    ModelPreview   *m_preview_mapped{nullptr};
    wxWindow       *m_mapping_panel{nullptr};
    MappingList    *m_mapping_list{nullptr};
    wxWindow       *m_advanced_body{nullptr};
    wxStaticBitmap *m_advanced_icon{nullptr};
    bool            m_overwrite{false};
    bool            m_advanced_open{false};

    SyncResult m_result; ///< produced by deal_ok(), read by the Plater after an OK close

    /// Source filament id -> its destination, for every filament mapped in any plate scope so far. A row's
    /// picker writes through the same shared_ptr, so this always reflects the latest pick.
    std::map<int, std::shared_ptr<FilamentInfo>> m_mappings;
};

} // namespace GUI
} // namespace Slic3r
