#pragma once

#include <functional>
#include <vector>

#include <wx/panel.h>
#include <wx/timer.h>

#include "ExtruderPanel.hpp" // ExtruderPanel, CustomSeparator, and the NozzleConfig forward decl
#include "Widgets/StateColor.hpp" // ThemeColor, for the card background the constructor sets

/**
 * \file ExtruderPanelGroup.hpp
 * \brief Sidebar composite that frames one (single) or two (dual) ExtruderPanel(s) plus their
 *        switch separator into a single rounded card, and turns the whole surface into one
 *        click / hover target for the nozzle-settings dialog and the read-only details tip.
 *
 * The card comes in one variant per printer family. \ref ExtruderPanelGroup is the abstract base
 * that owns everything the variants share (border paint, badge, trailing arrow, click/hover
 * routing); each subclass supplies only the extruder panels it frames:
 *   - \ref DualExtruderPanelGroup  — left + switch separator + right (dual heads)
 *   - \ref SingleExtruderPanelGroup — one panel, no separator (single heads(extruder) with multiple nozzles)
 */

namespace Slic3r::GUI {

/**
 * \brief Abstract sidebar extruder card: shared chrome + behavior for every printer family.
 *
 * Not instantiable on its own — a concrete subclass decides which ExtruderPanel(s) to build via
 * \ref CreateExtruders. Layout is deferred to the non-virtual \ref Build, which each subclass constructor calls
 * as its final statement.
 */
class ExtruderPanelGroup : public wxPanel
{
public:
    ExtruderPanel *GetPanel(int index) const;

    void ShowBadge(bool show);

    void SetOnClick(std::function<void(bool)> callback) { m_on_click = std::move(callback); }

    // Fired with true when the pointer enters the panel (any descendant), false when it leaves.
    void SetOnHover(std::function<void(bool)> callback) { m_on_hover = std::move(callback); }

    /**
     * \brief update the data, refresh UI and do all the needed house cleaning
     *
     * The panel rebuilds its summary tiles as fresh windows on every call, so the group re-binds
     * click and hover across the whole surface here — callers never do it by hand.
     *
     * \param index             which framed panel (0 = single / left, 1 = right)
     * \param configs           the extruder's nozzle configs; a no-op when empty
     * \param selected_diameter the currently selected printing diameter
     */
    void UpdateNozzles(int index, const std::vector<NozzleConfig> &configs, const wxString &selected_diameter);

    // Layout
    virtual void Rescale();

    // Separator state is only meaningful for the dual printers
    virtual void                   SetSeparatorState(CustomSeparator::State state) {}
    virtual CustomSeparator::State GetSeparatorState() const { return CustomSeparator::State::Hidden; }

protected:
    explicit ExtruderPanelGroup(wxWindow *parent);

    /**
     * \brief Run the child-dependent construction sequence. Must be called by each subclass ctor.
     *
     * Invokes the virtual \ref CreateExtruders, appends the shared divider + arrow, sizes the panel,
     * then binds the click/hover surfaces and applies dark-mode theming.
     */
    void Build();

    /**
     * \brief Build the variant's ExtruderPanel(s) into \p content and register them in \ref m_panels.
     * \param content the horizontal sizer at the head of the card, ahead of the shared divider/arrow
     */
    virtual void CreateExtruders(wxBoxSizer *content) = 0;

    /**
     * \brief Build the variant's header row above the panels, if it has one.
     *
     * A variant with a header owns the card's settings entry, so \ref Build leaves out the shared
     * divider and trailing arrow when this returns true.
     *
     * \param outer the card's vertical sizer, still empty at this point
     * \return true when a header was added
     */
    virtual bool CreateHeader(wxBoxSizer *outer) { return false; }

    /// \brief true for dual-extruder cards; forwarded to the click callback as its \c is_dual argument.
    virtual bool IsDual() const { return false; }

    /**
     * \brief The widget the card's top border has to run through instead of over, if any.
     *
     * A variant whose header hangs a control across the top edge returns it here: \ref OnPaint then
     * starts the border half that control's height down, so the line crosses its vertical centre and
     * the control's own opaque fill hides the crossing. Returns nullptr while it is hidden, so the
     * border snaps back to the top edge.
     */
    virtual wxWindow *BorderStraddlingWidget() const { return nullptr; }

    std::vector<ExtruderPanel *> m_panels;
    wxBoxSizer                  *m_main_sizer{nullptr};

    /**
     * \brief Re-fit the card to the height its panels and optional controls now need.
     */
    void FitToPanels();

private:
    // Re-arm click / hover on the whole card after a panel rebuilt its summary tiles
    void BindClickEvents();
    void BindHoverEvents();

    void OnPanelClick(wxMouseEvent &evt);
    void OnHover(wxMouseEvent &evt); // one handler bound to both enter and leave
    void SetHoverShown(bool shown);  // drives m_on_hover; a show is dropped while disabled/inactive
    void OnPaint(wxPaintEvent &evt);

    // Re-fit after a resize, since a different width fits a different number of tiles per line.
    void OnSize(wxSizeEvent &evt);

#ifdef __WXMSW__
    ScalableBitmap m_badge;
#endif
#ifdef __WXOSX__
    ScalableButton *m_badge{nullptr};
    void            LayoutBadge();
    void            OnSizeBadge(wxSizeEvent &evt); // keeps the badge pinned while the card resizes
#endif

    // Layout components
    bool                      m_fit_pending{false}; // a re-fit is already queued for this resize
    std::function<void(bool)> m_on_click{};
    std::function<void(bool)> m_on_hover{};
    bool                      m_hover_shown{false};
    wxTimer                   m_hover_timer{this}; // one-shot grace before hiding on leave
};

/**
 * \brief Extruder card for dual-head printers left panel, switch separator, right panel.
 */
class DualExtruderPanelGroup : public ExtruderPanelGroup
{
public:
    explicit DualExtruderPanelGroup(wxWindow *parent) : ExtruderPanelGroup(parent) { Build(); }

    void                   SetSeparatorState(CustomSeparator::State state) override;
    CustomSeparator::State GetSeparatorState() const override;

protected:
    void CreateExtruders(wxBoxSizer *content) override;
    bool IsDual() const override { return true; }

private:
    CustomSeparator *m_separator{nullptr};
};

/**
 * \brief Extruder card for single-head multiple nozzles printers one panel, no separator.
 */
class SingleExtruderPanelGroup : public ExtruderPanelGroup
{
public:
    explicit SingleExtruderPanelGroup(wxWindow *parent) : ExtruderPanelGroup(parent)
    {
        // The design draws this card on white rather than the shared grey, which is what gives its
        // nozzle tiles their contrast. Set before Build(), which reads it back for the mode pill.
        SetBackgroundColour(ThemeColor::White);
        Build();
    }

    /**
     * \brief The single-/multi-diameter mode pill drawn across the card's top border.
     *
     * The card only owns the widget so it can be painted over its own border; the mode itself stays
     * with DiameterButtonPanel, which adopts the pill through \c AttachModePill.
     */
    ComboBox *GetModePill() const { return m_mode_pill; }

    void AttachDiameterPanel(DiameterButtonPanel *panel);
    void DetachDiameterPanel(wxWindow *new_parent);
    void ShowDiameterPanel(bool show);
    void Rescale() override;

protected:
    void      CreateExtruders(wxBoxSizer *content) override;
    bool      CreateHeader(wxBoxSizer *outer) override;
    wxWindow *BorderStraddlingWidget() const override;

private:
    // StaticBox keeps its colours rather than re-reading them at paint time, so the pill's theming
    // has to be re-applied whenever the card is rescaled or the system colours change.
    void StyleModePill();

    ComboBox            *m_mode_pill{nullptr};
    DiameterButtonPanel *m_diameter_panel{nullptr};
    wxBoxSizer          *m_diameter_sizer{nullptr};
};

} // namespace Slic3r::GUI
