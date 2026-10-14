#pragma once

#include <functional>
#include <vector>

#include <wx/panel.h>
#include <wx/timer.h>

#include "ExtruderPanel.hpp" // NozzleRackPanel, DiameterButtonPanel, HorizontalScrollView

/**
 * \file ExtruderPanelGroup.hpp
 * \brief Sidebar nozzle card of every printer with a nozzle rack: a "Nozzle ··· Settings" header,
 *        then the rack's slot tiles and AMS units, all in one rounded card.
 *
 * The settings entry in the header opens the nozzle-settings dialog. On single-extruder printers the
 * card also hosts the single-/multi-diameter mode pill on its top border and the diameter buttons.
 */

namespace Slic3r::GUI {

/**
 * \brief The sidebar nozzle card for single- and dual-extruder rack printers.
 *
 * Everything that differs between the two, such as slot labels, the hotend icon and the AMS split,
 * follows from the extruder count passed to \ref UpdateNozzles.
 */
class NozzleRackPanelGroup : public wxPanel
{
public:
    explicit NozzleRackPanelGroup(wxWindow *parent);

    /// \brief The tiles and AMS units inside the card.
    NozzleRackPanel *GetPanel() const { return m_panel; }

    void ShowBadge(bool show);

    /// \brief Fired when the header's settings entry is clicked.
    void SetOnClick(std::function<void()> callback) { m_on_click = std::move(callback); }

    /// \brief Fired with true when the pointer enters the card (any descendant), false when it leaves.
    void SetOnHover(std::function<void(bool)> callback) { m_on_hover = std::move(callback); }

    /**
     * \brief Rebuild the slot tiles, then refit the card to them.
     *
     * The tiles are fresh windows on every call, so the card re-arms its hover tracking here; callers
     * never do it by hand.
     *
     * \param extruders         nozzle configs per extruder, left first; a no-op when every list is empty
     * \param selected_diameter the currently selected printing diameter
     */
    void UpdateNozzles(const std::vector<std::vector<NozzleConfig>> &extruders, const wxString &selected_diameter);

    /**
     * \brief The single-/multi-diameter mode pill drawn across the card's top border.
     *
     * The card only owns the widget so it can be painted over its own border; the mode itself stays
     * with DiameterButtonPanel, which adopts the pill through \c AttachModePill and hides it on
     * printers without the mode.
     */
    ComboBox *GetModePill() const { return m_mode_pill; }

    /**
     * \brief The narrowest card that still shows the mode pill's label in full, in either mode.
     */
    int MinWidth() const;

    void AttachDiameterPanel(DiameterButtonPanel *panel);
    void DetachDiameterPanel(wxWindow *new_parent);
    void ShowDiameterPanel(bool show);
    void Rescale();

    // Every change to the body's content ends in a card layout, so the body is re-measured here.
    bool Layout() override;

private:
    void      CreateHeader(wxBoxSizer *outer);
    wxWindow *CreateSettingsEntry();
    void      CreateBody(wxBoxSizer *outer);

    // StaticBox keeps its colours rather than re-reading them at paint time, so the pill's theming
    // has to be re-applied whenever the card is rescaled or the system colours change.
    void StyleModePill();

    // The card's top border runs through the middle of the mode pill while it is shown.
    int BorderTop() const;

    // Click goes to the settings entry only; hover covers the whole card and is re-armed per rebuild.
    void BindClickEvents();
    void BindHoverEvents();

    // Re-fit the card to the height its content now needs.
    void FitToPanels();
    void RequestFit(); // FitToPanels once the current event is done; repeated requests merge

    void OnPanelClick(wxMouseEvent &evt);
    void OnHover(wxMouseEvent &evt); // one handler bound to both enter and leave
    void SetHoverShown(bool shown);  // drives m_on_hover; a show is dropped while disabled/inactive
    void OnPaint(wxPaintEvent &evt);
    void OnSize(wxSizeEvent &evt);   // a different width may change the content height

#ifdef __WXMSW__
    ScalableBitmap m_badge;
#endif
#ifdef __WXOSX__
    ScalableButton *m_badge{nullptr};
    void            LayoutBadge();
    void            OnSizeBadge(wxSizeEvent &evt); // keeps the badge pinned while the card resizes
#endif

    wxBoxSizer           *m_main_sizer{nullptr};
    ComboBox             *m_mode_pill{nullptr};
    wxSizerItem          *m_header_gap{nullptr};      // between the top border and the header row
    wxPanel              *m_settings_entry{nullptr};  // the header's settings entry: gear, label and chevron
    HorizontalScrollView *m_body{nullptr};            // diameter buttons + rack panel, scrolled together
    NozzleRackPanel      *m_panel{nullptr};
    DiameterButtonPanel  *m_diameter_panel{nullptr};
    wxBoxSizer           *m_diameter_sizer{nullptr};

    bool                      m_fit_pending{false}; // a re-fit is already queued
    std::function<void()>     m_on_click{};
    std::function<void(bool)> m_on_hover{};
    bool                      m_hover_shown{false};
    wxTimer                   m_hover_timer{this}; // one-shot grace before hiding on leave
};

} // namespace Slic3r::GUI
