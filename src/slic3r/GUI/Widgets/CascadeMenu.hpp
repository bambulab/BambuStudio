#pragma once

#include <functional>

#include <wx/bitmap.h>
#include <wx/colour.h>
#include <wx/panel.h>
#include <wx/popupwin.h>
#include <wx/string.h>
#include <wx/weakref.h>

#include "slic3r/GUI/Widgets/RoundedPopupWindow.hpp"

class wxBoxSizer;
class wxMouseEvent;
class wxPaintEvent;

namespace Slic3r::GUI {

class CascadeMenuItem;
class CascadeMenuPopup;

/**
 * \brief Geometry and colours of the two-level nozzle menu, from the design.
 *
 * A 4px inset inside the 0.5px stroke, rows 24 tall with 4 between them. A first-level row is
 * [4 | tick 14 | 4 | label | 24 | 4 | chevron 16 | 7]; a second-level row drops the chevron and
 * keeps the label left-aligned in the same tick column. The 24 after the label is a minimum: a row
 * stretched by a wider sibling grows there, since the label is left-aligned.
 */
namespace CascadeMenu {

constexpr int Pad       = 4;
constexpr int Gap       = 4;
constexpr int ItemH     = 24;
constexpr int ItemPadR  = 7;
constexpr int LabelPadR = 24; // free space after the label, so a short label does not make a cramped panel
constexpr int TickD     = 14;
constexpr int ChevronD  = 16;

// The design's selected-row fill. Close to ThemeColor::LightGreen but a step more saturated, and the
// two are used side by side on this screen, so it does not fold into that token.
inline const wxColour SelectedBg{"#DBFDE7"};

// Design grey-500. The card stroke is this colour at one device pixel (the spec's 0.5px), and a
// second-level label that cannot be picked uses the same colour.
inline const wxColour Hairline{"#ACACAC"};

/**
 * \brief Append \p item to \p box, inset by the panel's own padding.
 *
 * Rows are separated by that same amount, so a row's fill never touches the border. \p first adds
 * the inset above the top row, which has no separator before it.
 */
void AddItem(wxBoxSizer *box, CascadeMenuItem *item, bool first);

/** \brief Close the panel: trailing inset, then size it to its content. */
void Finish(CascadeMenuPopup *popup, wxBoxSizer *box);

/**
 * \brief Place \p sub beside \p main, with its first row level with \p anchor.
 *
 * Backs off by the panel's own inset rather than starting at the anchor row's top, so the two
 * levels read as one surface.
 */
void PositionSubMenu(CascadeMenuPopup *sub, const CascadeMenuPopup *main, const wxWindow *anchor);

} // namespace CascadeMenu

/**
 * \brief The hover tip a faded row shows: a small black chip saying how to make the row selectable.
 *
 * A hover card, NOT a menu, so it must stay on wxPopupWindow and be put up with Show() rather than
 * Popup(): wxPopupTransientWindow::Show() grabs the pointer on GTK without arming the
 * click-outside dismiss path, which locks the whole UI (bambulab/BambuStudio#12233, #12253 --
 * ParamTooltip carries the same warning). A grab would also pull the pointer out from under the
 * very row that asked for the tip.
 */
class CascadeMenuTip : public wxPopupWindow
{
public:
    explicit CascadeMenuTip(wxWindow *parent);

    /** \brief Size to \p text and place the chip just under \p anchor, then show it. */
    void ShowBelow(const wxWindow *anchor, const wxString &text);

private:
    void OnPaint(wxPaintEvent &evt);
    void ApplyShape();

    wxString m_text;
};

/**
 * \brief One row of a nozzle menu: a tick for the current value, the label, and for a first-level
 *        row a trailing chevron that says it opens a submenu.
 *
 * Sends wxEVT_BUTTON when the user clicks an enabled row: press and release both on it. The
 * release, not the press, picks: a menu closed on the press hands the release to whatever window
 * lies under it. The event stops at the row, so an unhandled pick never reaches the menu's owner.
 */
class CascadeMenuItem : public wxPanel
{
public:
    CascadeMenuItem(wxWindow *parent, const wxString &text, bool enabled, bool checked, bool has_submenu);

    /**
     * \brief Fade the whole row rather than just greying its label.
     *
     * The two are the same "not selectable" state wearing the design's two renderings of it, chosen
     * by what the row is made of: a first-level row carries a tick column and a chevron, so fading
     * the lot keeps it legible, while a second-level row is only text and greying that is enough.
     */
    void SetDimmed(bool dimmed);

    /** \brief Message shown on hover, saying why the row cannot be picked and what would fix it. */
    void SetTip(const wxString &tip);

private:
    void OnPaint(wxPaintEvent &evt);
    void OnLeftUp(wxMouseEvent &evt);

    wxString m_text;
    wxString m_tip;
    bool     m_enabled{true};
    bool     m_checked{false};
    bool     m_has_submenu{false};
    bool     m_hover{false};
    bool     m_dimmed{false};
    bool     m_pressed{false}; // a press started on this row and has not left it yet
    wxBitmap m_tick;
    wxBitmap m_chevron;
};

/**
 * \brief One level of the cascade, as a popup that survives the next level opening over it.
 *
 * Showing a second popup hands native activation to it, and wx answers the kill-focus that lands on
 * the first one by dismissing it -- which is why a plain pair of popups collapses to the submenu
 * alone. DropDown runs into the same thing for its own two-level menu and solves it by refusing to
 * dismiss while its submenu is up, then handing activation back once the submenu closes; this is
 * that mechanism, lifted out of DropDown because that one couples it to its own item model.
 */
class CascadeMenuPopup : public RoundedPopupWindow
{
public:
    explicit CascadeMenuPopup(wxWindow *parent);

    // Set on the first level; the second level points back with SetMainMenu. Clear the first before
    // tearing a submenu down, or the guard below reads a window that is already on its way out.
    void SetSubMenu(CascadeMenuPopup *sub) { m_sub = sub; }
    void SetMainMenu(CascadeMenuPopup *main) { m_main = main; }

    // One tip chip per level, created on first use and owned by the menu. Keep it as a weak
    // reference because rebuilding a submenu calls DestroyChildren(), which destroys the chip too;
    // the next disabled row must create a new one rather than dereference the old window.
    void ShowTip(const wxWindow *anchor, const wxString &text);
    void HideTip();

    void Popup(wxWindow *focus = nullptr) override;
    void Dismiss() override;
    void OnDismiss() override;

    /**
     * \brief Hand a click on the other level of the cascade to that level.
     *
     * wx calls this before its own click-outside dismissal, which is the only place the two can
     * still be told apart: a click on the submenu is outside the first-level window and would
     * otherwise be read as the user clicking away from the menu.
     */
    bool ProcessLeftDown(wxMouseEvent &evt) override;

    /**
     * \brief Close the whole cascade: both levels down, wx's mouse capture handed back, and the
     *        pair destroyed when the owner asked for that.
     *
     * The single exit every close goes through, because the order is load-bearing and not obvious.
     * The capture has to be drained while both windows are still alive: wx releases a capture by
     * handing it to the next window on its stack, nothing in the destructor releases one (after a
     * Dismiss() the handler is unlinked, so ~wxPopupTransientWindow skips PopHandlers()), and a
     * stale entry is read on the next mouse event -- cocoa's WX_filterSendEvent forwards every
     * mouse event in the application to wxWindow::GetCapture() without checking that it is still
     * shown, or alive.
     */
    void CloseCascade();

    /**
     * \brief Destroy both levels when the cascade closes instead of keeping them for reuse.
     *
     * Off by default: a menu whose owner builds a fresh pair per gesture wants the old one gone,
     * one that keeps a pair for the life of its owner does not.
     */
    void SetDestroyOnClose(bool on) { m_destroy_on_close = on; }

    /** \brief Run after CloseCascade(), so the owner can drop its own pointers to the pair. */
    void SetOnClosed(std::function<void()> on_closed) { m_on_closed = std::move(on_closed); }

private:
    /** \brief Pop every mouse capture this cascade still holds off wx's capture stack. */
    void ReleaseCascadeCapture(wxWindow *sub);

    /** \brief The level this one is paired with: the submenu on a first level, the menu on a second. */
    CascadeMenuPopup *OtherLevel() const;

    /**
     * \brief Re-dispatch \p evt to the other level when the pointer is over it, hover included.
     *
     * Returns whether it did. Only one level holds the mouse capture at a time, and on macOS that
     * means the other one receives nothing at all from the system -- see SetHovered(). The levels
     * are separate windows, so PopupWindow's own hit test cannot reach across; this does.
     */
    bool RouteToOtherLevel(wxMouseEvent &evt);

#ifdef __WXOSX__
    void OnCascadeMouse(wxMouseEvent &evt);
    void OnIdleNoop(wxIdleEvent &evt);
#endif

    // Weak on both sides. The two levels are children of the same window and are destroyed in
    // creation order, so the first level goes while the second still points back at it.
    wxWeakRef<CascadeMenuPopup> m_sub;  // set on the first-level menu only
    wxWeakRef<CascadeMenuPopup> m_main; // set on the second-level menu only
    wxWeakRef<CascadeMenuTip>   m_tip;
    bool                        m_destroy_on_close{false}; // see SetDestroyOnClose()
    bool                        m_routed_in{false};        // handling an event the other level passed over
    std::function<void()>       m_on_closed;
};

} // namespace Slic3r::GUI
