#pragma once

#include <functional>
#include <string>

#include <wx/event.h>
#include <wx/window.h>

namespace Slic3r {
struct FilamentInfo;
} // namespace Slic3r

namespace Slic3r::GUI {

class AmsMapingPopup;

/**
 * \brief Picking context the popup needs: which source filament is being mapped and whether the
 *        filament-type restriction is lifted.
 */
struct SlotSelectionState
{
    int         source_id{-1};    ///< project filament id being mapped (scopes the popup's selection)
    std::string source_type;      ///< filament type of the source (the type-match gate)
    bool        match_all{false}; ///< when set, any slot may be picked regardless of type
};

/**
 * \brief AMS slot picker for the sync dialog.
 *
 * A thin adapter over the app-wide \ref AmsMapingPopup: it drives that popup from the sync dialog's
 * per-row seam and translates the chosen slot back into a \ref FilamentInfo delivered through
 * \ref on_pick. The heavy popup is created on first use and reused across rows. Selectability follows
 * the legacy popup's own type/external-spool rules.
 */
class AmsSlotPopup : public wxWindow
{
public:
    explicit AmsSlotPopup(wxWindow *parent);

    /**
     * \brief Set the picking context for the current row.
     *
     * \param state  the source being mapped and its type gate
     */
    void set_state(const SlotSelectionState &state);

    /**
     * \brief Set the row's current destination slot.
     *
     * \param current  the row's current destination
     */
    void set_current_slot(const FilamentInfo &current);

    /**
     * \brief Show the popup under a cell. Call after \ref set_state and \ref set_current_slot.
     *
     * Does nothing when no machine is selected.
     *
     * \param anchor  the cell to show under
     */
    void popup_at(wxWindow *anchor);

    std::function<void(const FilamentInfo &chosen)> on_pick;

private:
    /**
     * \brief Build the wrapped slot picker on first use.
     */
    void ensure_impl();

    /**
     * \brief Position the popup under a cell. The popup must already be sized.
     *
     * \param anchor  the cell to position under
     */
    void place_under(wxWindow *anchor);

    /**
     * \brief Forward the wrapped picker's pick event to \ref on_pick.
     *
     * \param evt  the pick event
     */
    void on_finish_mapping(wxCommandEvent &evt);

    AmsMapingPopup    *m_impl{nullptr};
    SlotSelectionState m_state;
    std::string        m_current_ams;
    std::string        m_current_slot;
};

} // namespace Slic3r::GUI
