#pragma once
/**
 * \file DualNozzleView.hpp
 * \brief The sidebar nozzle content of a printer with two extruders and one nozzle each: a picker for
 *        the extruders that print, then one card per nozzle with its diameter and type.
 */

#include <array>
#include <functional>
#include <string>
#include <vector>

#include <wx/panel.h>

#include "libslic3r/PrintConfig.hpp" // NozzleVolumeType

class Button;
class wxBoxSizer;

namespace Slic3r::GUI {

class NozzleInfoCardView;
class NozzleEditMenu;

/**
 * \brief Picker and nozzle cards of a printer with two single-nozzle extruders.
 *
 * Draws itself from the nozzle inventory and the project config on each \ref refresh, so it holds no
 * copy of the nozzles. A user action goes to the owner through a callback; the owner changes the
 * nozzles or the printer variant, and then calls \ref refresh again.
 *
 * Extruder 0 is the left nozzle, extruder 1 the right one.
 */
class DualNozzleView : public wxPanel
{
public:
    static constexpr int EXTRUDERS = 2;

    explicit DualNozzleView(wxWindow *parent);

    /**
     * \brief Redraw the picker and the cards from the nozzle inventory and the multi-diameter mode.
     */
    void refresh();

    /**
     * \brief Name the two nozzles on their cards and on the picker.
     *
     * \param left   name of extruder 0
     * \param right  name of extruder 1
     */
    void set_titles(const wxString &left, const wxString &right);

    /**
     * \brief Show or hide the sync check on the card of one extruder.
     *
     * \param extruder  0-based extruder
     * \param synced    the card matches the connected machine
     */
    void set_synced(int extruder, bool synced);

    /**
     * \brief Called when the user picks the extruders that print in the single-diameter mode.
     *
     * The argument holds one enabled flag per extruder, left first.
     */
    void set_on_pick(std::function<void(std::vector<bool>)> on_pick) { m_on_pick = std::move(on_pick); }

    /**
     * \brief Called when the user picks a nozzle for one extruder in the edit menu.
     *
     * The arguments are the extruder, the diameter such as "0.4", and the volume type.
     */
    void set_on_edit(std::function<void(int, const std::string &, NozzleVolumeType)> on_edit) { m_on_edit = std::move(on_edit); }

    void Rescale();

private:
    void create_picker(wxBoxSizer *outer);
    void update_picker(bool multi_diameter, bool same_diameter);
    void open_edit_menu(int extruder, wxWindow *anchor);

    wxBoxSizer                                   *m_picker{nullptr};
    std::array<Button *, EXTRUDERS + 1>           m_picks{};        // All, then one per extruder
    std::array<bool, EXTRUDERS + 1>               m_pick_grayed{};  // the pick cannot be clicked
    std::array<NozzleInfoCardView *, EXTRUDERS>   m_cards{};
    NozzleEditMenu                               *m_edit_menu{nullptr}; // created on first use, kept for reuse
    std::function<void(std::vector<bool>)>        m_on_pick;
    std::function<void(int, const std::string &, NozzleVolumeType)> m_on_edit;
};

} // namespace Slic3r::GUI
