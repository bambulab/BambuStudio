#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <wx/panel.h>
#include <wx/string.h>

class wxBoxSizer;
class wxStaticBitmap;
class Label;
class ScalableButton;
class PopupWindow;

namespace Slic3r::GUI {

class AMSPreview; // defined in Widgets/AMSItem.hpp, in this namespace
struct AMSinfo;   // defined in Widgets/AMSItem.hpp, in this namespace

/**
 * \brief External-AMS attach row for one extruder: the live preview strip, the add / remove
 *        popups, and the per-extruder AMS-count edits behind them.
 *
 * Embed one per extruder. Hides itself when the selected printer has no AMS attach point.
 * Count edits take effect on the live global AMS-count registry immediately; call restore()
 * to revert them (e.g. on Cancel). Preview content reflects the currently selected machine,
 * and only while that machine's nozzles of this extruder are synced.
 */
class NozzleAmsRow : public wxPanel
{
public:
    /** \brief The row's own chrome. */
    enum class Style
    {
        Band,   ///< padded band with a corner check when the counts match the printer
        Plain,  ///< flush content, no corner check; lines up with the owner's other rows
        Inline, ///< on the owner's surface with a small label; for a card that lives across printer switches
    };

    /**
     * \param parent          window the row is embedded in
     * \param extruder_index  logical extruder whose AMS units this row edits
     * \param label_text      leading label ("AMS" / "External AMS"); empty shows none
     * \param style           the row's chrome
     * \param on_changed      invoked after a user count edit, for the owner to relayout
     */
    NozzleAmsRow(wxWindow *parent, int extruder_index, const wxString &label_text, Style style, std::function<void()> on_changed);
    ~NozzleAmsRow() override;

    /**
     * \brief Let the user add and remove units: the "+" and the delete popup on hover. On by default.
     *
     * \return true if the row changed, so the owner must lay it out again
     */
    bool set_editable(bool editable);

    /**
     * \brief Set if the extruder can feed from an AMS now. A row that cannot shows its units greyed,
     *        without trays. On by default.
     *
     * \return true if the row changed, so the owner must lay it out again
     */
    bool set_feedable(bool feedable);

    /**
     * \brief Re-read the counts, the selection and the machine, then redraw if anything changed.
     *
     * Cheap when nothing changed, so it is safe on every machine update. The row hides while the
     * selected printer has no such extruder or no AMS attach point. An Inline row also hides while it
     * has no unit and is not editable, and shows its label only on a printer with two extruders.
     *
     * \return true if the row changed, so the owner must lay it out again
     */
    bool refresh();

    /** \brief Revert this extruder's AMS count to its value when the row was constructed. */
    void restore();

    void Rescale();

private:
    bool refresh_previews(); // true if the previews changed
    void update_sync_badge();
    void show_add_popup();
    void show_delete_popup(AMSPreview *preview);

    int                   m_extruder_index{0};
    Style                 m_style{Style::Band};
    std::function<void()> m_on_changed;
    bool                  m_has_ams{false};
    bool                  m_editable{true};
    bool                  m_feedable{true};
    bool                  m_drawn{false};     // m_shown holds what the previews show
    std::vector<AMSinfo>  m_shown;            // the content of each open preview: the 4-slot units, then the 1-slot ones
    size_t                m_shown_4{0};       // how many of m_shown are 4-slot units
    std::pair<int, int>   m_snapshot{0, 0}; // {n4, n1} at construction, restored on Cancel
    bool                  m_selection_snapshot_initialized{false};
    std::string           m_selection_snapshot_device_id;
    std::vector<std::string> m_ams4_snapshot;
    std::vector<std::string> m_ams1_snapshot;

    Label                    *m_label{nullptr};
    wxPanel                  *m_previews_container{nullptr};
    wxBoxSizer               *m_previews_sizer{nullptr};
    std::vector<AMSPreview *> m_previews; // 4 AMS-type + 8 N3S-type, reused
    wxWindow                 *m_add_btn{nullptr};
    wxStaticBitmap           *m_sync_badge{nullptr}; // top-right corner check when counts match the printer; Band only
    PopupWindow              *m_popup{nullptr}; // tracks the live add/delete popup
};

} // namespace Slic3r::GUI
