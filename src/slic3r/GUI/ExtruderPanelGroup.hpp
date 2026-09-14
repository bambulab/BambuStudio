#pragma once

#include <functional>
#include <vector>

#include <wx/panel.h>
#include <wx/timer.h>

#include "ExtruderPanel.hpp" // ExtruderPanel, CustomSeparator, and the NozzleConfig forward decl

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
    void Rescale();

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

    /// \brief true for dual-extruder cards; forwarded to the click callback as its \c is_dual argument.
    virtual bool IsDual() const { return false; }

    std::vector<ExtruderPanel *> m_panels;

private:
    // Re-arm click / hover on the whole card after a panel rebuilt its summary tiles
    void BindClickEvents();
    void BindHoverEvents();

    void OnPanelClick(wxMouseEvent &evt);
    void OnHover(wxMouseEvent &evt); // one handler bound to both enter and leave
    void SetHoverShown(bool shown);  // drives m_on_hover; a show is dropped while disabled/inactive
    void OnPaint(wxPaintEvent &evt);

    /**
     * \brief Re-fit the card to the height its panels now need.
     *
     * Call after a panel changed its height, so the card and the sidebar following it grow to hold
     * a tile row that wrapped onto extra lines instead of clipping it.
     */
    void FitToPanels();

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
    wxBoxSizer               *m_main_sizer{nullptr};
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
    explicit SingleExtruderPanelGroup(wxWindow *parent) : ExtruderPanelGroup(parent) { Build(); }

protected:
    void CreateExtruders(wxBoxSizer *content) override;
};

} // namespace Slic3r::GUI
