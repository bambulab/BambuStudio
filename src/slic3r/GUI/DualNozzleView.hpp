#pragma once
/**
 * \file DualNozzleView.hpp
 * \brief The sidebar nozzle content of a printer with two extruders and one nozzle each: a picker for
 *        the side that prints when the nozzles differ, then one card per nozzle with its diameter, type
 *        and AMS units, and the filament switcher icon between the cards.
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

class IconView;
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
     * \brief Redraw the AMS units on both cards from the AMS selection and the selected machine.
     *
     * Cheap when nothing changed, so call it on every machine update.
     *
     * \return true if a card changed, so the owner must fit the view again
     */
    bool refresh_ams();

    /// \brief Called after the user adds or removes an AMS unit on a card.
    void set_on_ams_changed(std::function<void()> on_changed) { m_on_ams_changed = std::move(on_changed); }

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
     * \brief Called when the user picks the side that prints in the single-diameter mode.
     *
     * The picker shows only while the two nozzles have different diameters. The argument is the
     * 0-based extruder whose nozzle prints.
     */
    void set_on_pick(std::function<void(int)> on_pick) { m_on_pick = std::move(on_pick); }

    /**
     * \brief Called when the user picks a nozzle for one extruder in the edit menu.
     *
     * The arguments are the extruder, the diameter such as "0.4", and the volume type.
     */
    void set_on_edit(std::function<void(int, const std::string &, NozzleVolumeType)> on_edit) { m_on_edit = std::move(on_edit); }

    /// \brief The filament switcher of the connected machine, as the icon between the cards shows it.
    enum class Switcher {
        None,     ///< no icon: no switcher, or the connected machine is not the selected printer
        Ready,
        NotReady, ///< installed, but it cannot switch filaments now
    };

    /**
     * \brief Show the filament switcher icon between the two nozzle cards; a click on it shows the
     *        switcher tip.
     *
     * Cheap when the state did not change, so call it on every machine update.
     */
    void set_switcher(Switcher switcher);

    void Rescale();

    // The switcher icon overlaps both cards, which a sizer cannot express, so it is placed after each layout.
    bool Layout() override;

private:
    void create_picker(wxBoxSizer *outer);
    void update_picker(bool shown);
    void open_edit_menu(int extruder, wxWindow *anchor);

    wxBoxSizer                                   *m_picker{nullptr};
    IconView                                     *m_switcher_icon{nullptr};
    Switcher                                      m_switcher{Switcher::None};
    std::array<Button *, EXTRUDERS>               m_picks{};        // one per extruder
    std::array<NozzleInfoCardView *, EXTRUDERS>   m_cards{};
    NozzleEditMenu                               *m_edit_menu{nullptr}; // created on first use, kept for reuse
    std::function<void(int)>                      m_on_pick;
    std::function<void()>                         m_on_ams_changed;
    std::function<void(int, const std::string &, NozzleVolumeType)> m_on_edit;
};

} // namespace Slic3r::GUI
