#pragma once

#include <wx/popupwin.h>
#include <wx/timer.h>
#include <wx/bitmap.h>
#include <wx/image.h>

#include <string>
#include <vector>

#include "WindowShadow.hpp"

class Label;
class wxStaticText;
class wxStaticBitmap;
class wxSizer;

namespace Slic3r::GUI {

// Native rich tooltip card for slicing parameters
class ParamTooltip : public wxPopupTransientWindow
{
public:
    // tip_pos is the screen anchor the card is placed beside (the row's right-center).
    // wiki_path is the option's wiki slug (og_line.label_path) — the same one the
    // clickable label uses; it drives the "View Wiki" link, falling back to the store.
    static bool ShowFor(const std::string &opt_key, const std::string &wiki_path, const wxPoint &tip_pos);
    static void Hide();
    // Destroy the singleton (its popup + shadow) and drop the localized store, so the next hover
    // rebuilds against the current MainFrame in the current language. Must run while the MainFrame
    // is torn down (app close, and the language-switch GUI rebuild): the popup is a child of that
    // frame, so once the frame is destroyed s_self dangles and the next instance() is a
    // use-after-free. Wired from MainFrame::shutdown().
    static void Shutdown();

    // Per-combobox-value tip for one enum value (e.g. opt_key "seam_position", value_key
    // "aligned"), or empty when there is no curated tip. Choice::BUILD feeds this into
    // ComboBox::SetItemTooltip so hovering a dropdown item shows that value's description.
    static wxString ItemTooltip(const std::string &opt_key, const std::string &value_key);

private:
    ParamTooltip();
    ~ParamTooltip();

    static ParamTooltip &instance(); // the singleton; created on first use
    static ParamTooltip *s_self;     // null until instance() first runs

    int content_width() const; // text/image column width, derived from CARD_WIDTH

    void     build_layout();
    /**
     * \brief Lay the details text out as a bullet list, one row per newline-separated point.
     *
     * Wrapped lines are indented to the text column so each point reads like a rendered
     * markdown bullet. The whole block collapses when there is no details text.
     *
     * \param s    Details text, one point per line.
     * \param fg   Text color for the current theme.
     * \param bg   Background color for the current theme.
     * \param wrap Width available to the block, marker included.
     */
    void     set_details(const wxString &s, const wxColour &fg, const wxColour &bg, int wrap);
    wxWindow *build_optkey_row();
    void     Rebuild(const std::string &opt_key, const std::string &wiki_path, bool dark);
    void      update_optkey_row(const std::string &opt_key, bool dark); // dev-mode-only opt_key pill
    void     ApplyShape();
    void     place_card(const wxPoint &tip_pos);
    void      update_shadow(bool show); // sync the soft-shadow layered window behind the card (Win32)
    wxBitmap LoadImage(const std::string &image_id, bool dark);

    bool DoShowFor(const std::string &opt_key, const std::string &wiki_path, const wxPoint &tip_pos);
    void DoHide(bool now);

    /**
     * \brief Play the "copied!" confirmation on the copy icon: crossfade to a check mark, hold, fade back.
     */
    void start_copy_feedback();

    void OnPaint(wxPaintEvent &evt);
    void OnTimer(wxTimerEvent &evt);
    /**
     * \brief Drive one frame of the copy-icon confirmation, then re-arm or finish the animation.
     *
     * \param evt The copy-animation timer event (unused).
     */
    void OnCopyAnim(wxTimerEvent &evt);

private:
    Label          *m_title   = nullptr;
    wxWindow       *m_divider = nullptr;
    Label          *m_desc    = nullptr;
    wxStaticBitmap *m_image   = nullptr;
    wxWindow       *m_details = nullptr; // container for the bulleted detail rows
    Label          *m_note    = nullptr;
    Label          *m_wiki    = nullptr;

    /** \brief One bullet of the details block: the marker and the wrapped text beside it. */
    struct DetailRow
    {
        wxSizer *sizer  = nullptr;
        Label   *marker = nullptr;
        Label   *text   = nullptr;
    };
    std::vector<DetailRow> m_detail_rows; // pooled across rebuilds; unused rows are hidden, not destroyed

    wxWindow       *m_optkey_pill = nullptr;
    wxStaticText   *m_optkey      = nullptr;
    wxStaticBitmap *m_copy        = nullptr;

    wxString    m_wiki_url;
    std::string m_last_key;
    bool        m_last_dark = false;

    // image decode cache
    std::string m_cached_image_id;
    bool        m_cached_dark = false;
    wxBitmap    m_cached_bmp;

    wxTimer *m_timer = nullptr;
    bool     m_hide  = false;
    wxPoint  m_request_pos;

    // "Copied!" confirmation: the two crossfade endpoints, plus the frame counter that walks
    // copy -> check -> copy. The images are rasterized on first use and dropped on a theme change.
    wxTimer *m_copy_timer = nullptr;
    double   m_copy_scale = 1.0; // backing scale m_copy_from/m_copy_to were rasterized at
    wxImage  m_copy_from;
    wxImage  m_copy_to;
    int      m_copy_step = 0;

    WindowShadow m_shadow; // soft drop shadow behind the card (Figma-spec defaults)
};

} // namespace Slic3r::GUI
