#pragma once

#include <functional>
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

/**
 * \brief External-AMS attach row for one extruder: the live preview strip, the add / remove
 *        popups, and the per-extruder AMS-count edits behind them.
 *
 * Embed one per extruder. Hides itself when the selected printer has no AMS attach point.
 * Count edits take effect on the live global AMS-count registry immediately; call restore()
 * to revert them (e.g. on Cancel). Preview content reflects the currently selected machine.
 */
class NozzleAmsRow : public wxPanel
{
public:
    /** \brief How the row is embedded, which governs the label and band chrome. */
    enum class Placement
    {
        Inline,        ///< sits inside a nozzle row; the label hides when no AMS is attached
        StandaloneRow, ///< its own banded row; the label stays even when no AMS is attached
    };

    /**
     * \param parent          window the row is embedded in
     * \param extruder_index  logical extruder whose AMS units this row edits
     * \param label_text      leading label ("AMS" / "External AMS")
     * \param placement       embedding style; governs label-when-empty and band padding
     * \param on_changed      invoked after a user count edit, for the owner to relayout
     */
    NozzleAmsRow(wxWindow *parent, int extruder_index, const wxString &label_text, Placement placement, std::function<void()> on_changed);

    /** \brief Revert this extruder's AMS count to its value when the row was constructed. */
    void restore();

    void Rescale();

private:
    void refresh_previews();
    void update_sync_badge();
    void show_add_popup();
    void show_delete_popup(AMSPreview *preview);

    int                   m_extruder_index{0};
    Placement             m_placement{Placement::Inline};
    std::function<void()> m_on_changed;
    bool                  m_has_ams{false};
    std::pair<int, int>   m_snapshot{0, 0}; // {n4, n1} at construction, restored on Cancel

    Label                    *m_label{nullptr};
    wxPanel                  *m_previews_container{nullptr};
    wxBoxSizer               *m_previews_sizer{nullptr};
    std::vector<AMSPreview *> m_previews; // 4 AMS-type + 8 N3S-type, reused
    ScalableButton           *m_add_btn{nullptr};
    wxStaticBitmap           *m_sync_badge{nullptr}; // top-right corner check when counts match the printer
    PopupWindow              *m_popup{nullptr}; // tracks the live add/delete popup
};

} // namespace Slic3r::GUI
