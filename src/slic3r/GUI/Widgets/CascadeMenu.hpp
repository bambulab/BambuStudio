#pragma once

#include <wx/bitmap.h>
#include <wx/colour.h>
#include <wx/panel.h>
#include <wx/popupwin.h>
#include <wx/string.h>
#include <wx/weakref.h>

#include "slic3r/GUI/Widgets/RoundedPopupWindow.hpp"

class wxBoxSizer;
class wxPaintEvent;

namespace Slic3r::GUI {

class CascadeMenuItem;
class CascadeMenuPopup;

/**
 * \brief Geometry and colours of the two-level nozzle menu, from the design.
 *
 * A 4px inset inside the 0.5px stroke, rows 24 tall with 4 between them. A first-level row is
 * [4 | tick 14 | 4 | label | 4 | chevron 16 | 7]; a second-level row drops the chevron and keeps
 * the label left-aligned in the same tick column.
 */
namespace CascadeMenu {

constexpr int Pad      = 4;
constexpr int Gap      = 4;
constexpr int ItemH    = 24;
constexpr int ItemPadR = 7;
constexpr int TickD    = 14;
constexpr int ChevronD = 16;

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

    wxString m_text;
    wxString m_tip;
    bool     m_enabled{true};
    bool     m_checked{false};
    bool     m_has_submenu{false};
    bool     m_hover{false};
    bool     m_dimmed{false};
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

    void Dismiss() override;
    void OnDismiss() override;

private:
    CascadeMenuPopup        *m_sub{nullptr};  // set on the first-level menu only
    CascadeMenuPopup        *m_main{nullptr}; // set on the second-level menu only
    wxWeakRef<CascadeMenuTip> m_tip;
};

} // namespace Slic3r::GUI
