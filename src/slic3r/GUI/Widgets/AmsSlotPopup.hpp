#pragma once

#include <functional>
#include <string>
#include <vector>

#include <wx/event.h>
#include <wx/window.h>

#include "libslic3r/ProjectTask.hpp" // FilamentInfo

namespace Slic3r::GUI {

class AmsMapingPopup;

/**
 * \brief Which of the printer's slots take part in a sync.
 *
 * Auto-map and the slot picker must read the same value, so a slot is either offered by both or by neither.
 */
struct SyncSlots
{
    bool dual_nozzle{false};         ///< the printer profile has two nozzles, each fed by its own slots
    bool selector_feeds_both{false}; ///< a ready filament selector feeds both nozzles from one list; external spools stay out
};

/**
 * \brief Picking context the popup needs: which source filament is being mapped, whether the
 *        filament-type restriction is lifted, which slots are offered, and where every source currently maps to.
 */
struct SlotSelectionState
{
    int                       source_id{-1};    ///< project filament id being mapped (scopes the popup's selection)
    std::string               source_type;      ///< filament type of the source (the type-match gate)
    bool                      match_all{false}; ///< when set, any slot may be picked regardless of type
    SyncSlots                 slots;            ///< the slots on offer
    std::vector<FilamentInfo> mapping;          ///< every source's destination, FilamentInfo::id = source id
};

/**
 * \brief AMS slot picker for the sync dialog.
 *
 * A thin adapter over the app-wide \ref AmsMapingPopup: it drives that popup from the sync dialog's
 * per-row seam and translates the chosen slot back into a \ref FilamentInfo delivered through
 * \ref on_pick. The heavy popup is created on first use and reused across rows. Selectability follows
 * the legacy popup's own type/external-spool rules; which slots are offered follows \ref SyncSlots.
 */
class AmsSlotPopup : public wxWindow
{
public:
    explicit AmsSlotPopup(wxWindow *parent);

    /**
     * \brief Set the picking context for the current row.
     *
     * \param state  the source being mapped, its type gate and the current mapping
     */
    void set_state(const SlotSelectionState &state);

    /**
     * \brief Show the popup under a cell. Call after \ref set_state.
     *
     * Does nothing when no machine is selected.
     *
     * \param anchor  the cell to show under
     */
    void popup_at(wxWindow *anchor);

    std::function<void(const FilamentInfo &chosen)> on_pick;  ///< the user picked a slot
    std::function<void()>                           on_reset; ///< the user cleared the row's mapping

private:
    /**
     * \brief Build the wrapped slot picker on first use.
     */
    void ensure_impl();

    /**
     * \brief Forward the wrapped picker's pick event to \ref on_pick.
     *
     * \param evt  the pick event
     */
    void on_finish_mapping(wxCommandEvent &evt);

    AmsMapingPopup    *m_impl{nullptr};
    SlotSelectionState m_state;
};

} // namespace Slic3r::GUI
