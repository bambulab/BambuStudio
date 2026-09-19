#include "MultiDiameterSyncAmsDialog.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numeric>
#include <wx/statbmp.h>
#include <wx/dcgraph.h>
#include <wx/display.h>

#include "AmsMappingPopup.hpp"
#include "DeviceCore/DevManager.h"   // full DeviceManager definition (DeviceManager.hpp only forward-declares it)
#include "DeviceCore/DevMapping.h"
#include "DeviceCore/DevNozzleSystem.h" // DevNozzle flow-type helpers for the nozzle volume type
#include "DeviceManager.hpp"
#include "FilamentBitmapUtils.hpp" // create_filament_bitmap for src/dst colour swatches
#include "GUI_App.hpp"
#include "GuiColor.hpp"
#include "I18N.hpp"
#include "PartPlate.hpp" // PartPlate::get_extruders for the current plate's used filaments
#include "Widgets/ComboBox.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/StateColor.hpp"
#include "Widgets/StaticBox.hpp"

#include "libslic3r/Utils.hpp"        // format_diameter_to_str
#include "libslic3r/PrintConfig.hpp"  // multi-diameter helpers

namespace {

/// Per-filament source diameter of the current project. Mirrors libslic3r
/// nozzle_diameter_for_filament: prefer the per-filament selected nozzle diameter (only populated in
/// multi-diameter mode), otherwise fall back to the machine's single nozzle diameter. A mixed
/// filament stores several ';'-joined component diameters and has no single source diameter.
double source_diameter_for(const Slic3r::DynamicPrintConfig& cfg, int extruder)
{
    if (auto* sel = cfg.option<Slic3r::ConfigOptionStrings>("filament_diameter_map")) {
        if (extruder >= 0 && extruder < (int) sel->values.size()) {
            const std::string& s = sel->values[extruder];
            if (!s.empty() && s.find(';') == std::string::npos) {
                const double d = std::atof(s.c_str());
                if (d > 0.0)
                    return d;
            }
        }
    }
    if (auto* nd = cfg.option<Slic3r::ConfigOptionFloatsNullable>("nozzle_diameter"))
        if (!nd->values.empty())
            return nd->values.front();
    return Slic3r::primary_nozzle_diameter(cfg);
}

/// Source volume type ("Standard" / "High Flow") of a project filament. Prefers the per-filament
/// volume type the project carries (populated per filament in multi-diameter/volume mode); otherwise
/// falls back to matching the filament's diameter against the printer's per-nozzle volume-type list.
/// Empty when it can't be resolved.
std::string source_volume_for(const Slic3r::DynamicPrintConfig& cfg, int extruder)
{
    if (auto* fvm = cfg.option<Slic3r::ConfigOptionInts>("filament_volume_map")) {
        if (extruder >= 0 && extruder < (int) fvm->values.size()) {
            const auto vt = (Slic3r::NozzleVolumeType) fvm->values[extruder];
            return Slic3r::DevNozzle::GetNozzleFlowTypeString(Slic3r::DevNozzle::ToNozzleFlowType(vt));
        }
    }
    auto* nvt = cfg.option<Slic3r::ConfigOptionEnumsGeneric>("nozzle_volume_type");
    if (!nvt || nvt->values.empty())
        return "";
    const double dia = source_diameter_for(cfg, extruder);
    auto*        nd  = cfg.option<Slic3r::ConfigOptionFloatsNullable>("nozzle_diameter");
    size_t       idx = 0;
    if (nd && nd->values.size() == nvt->values.size())
        for (size_t i = 0; i < nd->values.size(); ++i)
            if (std::fabs(nd->values[i] - dia) < 1e-3) { idx = i; break; }
    const auto vt = (Slic3r::NozzleVolumeType) nvt->values[idx];
    return Slic3r::DevNozzle::GetNozzleFlowTypeString(Slic3r::DevNozzle::ToNozzleFlowType(vt));
}

/// True when a vendor/brand string denotes Bambu Lab (so the Bambu mark is shown before the name).
bool brand_is_bambu(const std::string& brand)
{
    return brand.find("Bambu") != std::string::npos || brand.find("bambu") != std::string::npos;
}

/// Clean display name from a filament preset name: drop the " @printer" suffix. Only Bambu names also
/// drop a leading "Bambu" token, since the Bambu mark carries the brand; other brands keep their full
/// name (they show no icon), e.g. "Bambu PLA Basic @X1C" -> "PLA Basic", "eSUN PLA+ @X1C" -> "eSUN PLA+".
std::string clean_filament_name(const std::string& preset_name, const std::string& brand)
{
    std::string s   = preset_name;
    const auto  at  = s.find(" @");
    if (at != std::string::npos)
        s = s.substr(0, at);
    if (brand_is_bambu(brand)) {
        const std::string first = brand.substr(0, brand.find(' ')); // "Bambu Lab" -> "Bambu"
        if (!first.empty() && s.rfind(first + " ", 0) == 0)
            s = s.substr(first.size() + 1);
    }
    return s;
}

} // namespace

namespace Slic3r::GUI {

/**
 * \brief Horizontal mapping row: source cell  →  dest cell  ▾.
 *
 * Each cell is a colour swatch plus a two-line block: a Bambu-brand mark (only for Bambu filaments)
 * ahead of the filament name, and below it the nozzle diameter and volume type. The destination
 * diameter is drawn in a warning colour when it differs from the source diameter. The swatch colours
 * come from MaterialSyncItem (set by the shared AmsMapingPopup wiring); the text is set on this item.
 * The whole widget is the click target.
 */
class MaterialSyncItemH : public MaterialSyncItem
{
public:
    MaterialSyncItemH(wxWindow* parent, wxColour mcolour, wxString mname, std::string filament_id)
        : MaterialSyncItem(parent, mcolour, mname, filament_id)
    {
        // Guarded: a missing resource must not crash the dialog (the mark is simply not drawn).
        try { m_bambu_bmp = create_scaled_bitmap("bambu_mark", this, 12); } catch (...) {}
        messure_size();
        Bind(wxEVT_MOTION, &MaterialSyncItemH::on_mouse_move, this);
        Bind(wxEVT_LEAVE_WINDOW, &MaterialSyncItemH::on_mouse_leave, this);
    }

    /// Source cell text: filament name, nozzle diameter, volume type, and whether it is Bambu-brand.
    void set_source(const wxString& name, const wxString& dia, const wxString& vol, bool bambu)
    {
        m_src_name = name; m_src_dia = dia; m_src_vol = vol; m_src_bambu = bambu; Refresh();
    }
    /// Destination cell text (the swatch colour still comes from the base set_ams_info).
    void set_dest(const wxString& name, const wxString& dia, const wxString& vol, bool bambu)
    {
        m_dst_name = name; m_dst_dia = dia; m_dst_vol = vol; m_dst_bambu = bambu; Refresh();
    }

    /// The number drawn on the source swatch: the original 1-based project filament slot. It can
    /// differ from the row position because the plate uses only a subset of the project filaments,
    /// so it is kept separate from the base index string (which stays the positional reset key).
    void set_index_label(const wxString& s) { m_index_label = s; Refresh(); }

    /// Shared width of the widest destination cell across rows, so every dest lines up on the same
    /// left edge, pushed as far right as still fits that widest cell.
    void set_max_dst(int w) { if (m_max_dst != w) { m_max_dst = w; Refresh(); } }
    /// Width of this row's destination cell (swatch + widest text line) in its current mapped state.
    int dst_cell_width()
    {
        wxBitmap   bmp(1, 1);
        wxMemoryDC dc(bmp);
        const bool     match = !m_ams_name.empty() && m_ams_name != "-";
        const wxString dname = match ? m_dst_name : _L("Unmapped");
        const wxString ddia  = match ? m_dst_dia  : wxString();
        const wxString dvol  = match ? m_dst_vol  : wxString();
        return FromDIP(30) + FromDIP(8) + cell_text_width(dc, dname, match && m_dst_bambu, ddia, dvol);
    }

    void render(wxDC& dc) override
    {
        m_match = !m_ams_name.empty() && m_ams_name != "-";
#ifdef __WXMSW__
        const wxSize size = GetSize();
        wxMemoryDC   memdc;
        wxBitmap     bmp(size.x, size.y);
        memdc.SelectObject(bmp);
        memdc.Blit({0, 0}, size, &dc, {0, 0});
        {
            wxGCDC gdc(memdc);
            draw(gdc);
        }
        memdc.SelectObject(wxNullBitmap);
        dc.DrawBitmap(bmp, 0, 0);
#else
        draw(dc);
#endif
    }

protected:
    void messure_size() override
    {
        // Fixed height (two text lines + padding), stretchable width so the row fills the card.
        const int h = FromDIP(56);
        SetMinSize(wxSize(FromDIP(260), h));
        SetMaxSize(wxSize(FromDIP(4000), h));
        SetSize(wxSize(GetSize().x <= 0 ? FromDIP(560) : GetSize().x, h));
    }

private:
    /// Raise the incompatibility tip only while the cursor sits on the amber diameter/volume pill;
    /// hovering elsewhere on the row shows nothing. Only toggled on boundary crossings to avoid flicker.
    void on_mouse_move(wxMouseEvent& e)
    {
        const bool over = m_pill_rect.Contains(e.GetPosition());
        if (over != m_pill_hover) {
            m_pill_hover = over;
            if (over)
                SetToolTip(_L("The current nozzle diameter is incompatible with the printer"));
            else
                UnsetToolTip();
        }
        e.Skip();
    }

    /// Drop the tip when the cursor leaves the row, since motion events stop at the edge.
    void on_mouse_leave(wxMouseEvent& e)
    {
        if (m_pill_hover) { m_pill_hover = false; UnsetToolTip(); }
        e.Skip();
    }

    /// Paint a filament colour swatch (source or destination) with a hairline Grey300 outline.
    /// ctype 0 renders a gradient; any other value renders a discrete multi-colour split. A fully
    /// transparent single slot falls back to a light Grey300 chip so it stays visible.
    void draw_swatch(wxDC& dc, const wxRect& r, const wxColour& col, const std::vector<wxColour>& cols, int ctype,
                     const wxString& index_label = {})
    {
        std::vector<wxColour> colors;
        if (cols.size() > 1)
            colors = cols;
        else
            colors.push_back(col.Alpha() == 0 ? ThemeColor::Grey300 : col);

        const bool     force_gradient = ctype == 0 && colors.size() > 1;
        const wxBitmap bmp            = create_filament_bitmap(colors, wxSize(r.width, r.height), force_gradient);
        if (bmp.IsOk())
            dc.DrawBitmap(bmp, r.x, r.y, true);

        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.SetPen(wxPen(ThemeColor::Grey300, 1));
        dc.DrawRectangle(r);

        // Filament index centred on the swatch; the label flips to white on dark fills and dark on
        // light fills so it stays legible against the theme-independent filament colour.
        if (!index_label.empty()) {
            const wxColour bg = col.Alpha() == 0 ? ThemeColor::Grey300 : col;
            dc.SetFont(::Label::Body_12);
            dc.SetTextForeground(bg.GetLuminance() < 0.6 ? ThemeColor::White : ThemeColor::TextPrimary);
            const wxSize ts = dc.GetTextExtent(index_label);
            dc.DrawText(index_label, r.x + (r.width - ts.x) / 2, r.y + (r.height - ts.y) / 2);
        }
    }

    /// The second line's text ("dia  vol"), so both the width helper and the painter agree on it.
    static wxString line2_text(const wxString& dia, const wxString& vol)
    {
        if (dia.empty()) return vol;
        return vol.empty() ? dia : dia + "  " + vol;
    }

    /// Width of a cell's text block (max of the two lines), for right-aligning the dest cell.
    int cell_text_width(wxDC& dc, const wxString& name, bool bambu, const wxString& dia, const wxString& vol)
    {
        dc.SetFont(::Label::Body_13);
        int l1 = (bambu && m_bambu_bmp.IsOk() ? m_bambu_bmp.GetWidth() + FromDIP(4) : 0) + dc.GetTextExtent(name).x;
        dc.SetFont(::Label::Body_11);
        int l2 = dc.GetTextExtent(line2_text(dia, vol)).x;
        return std::max(l1, l2);
    }

    /// Draw one cell: swatch at swatch_x, then the two-line text block starting at text_x. When
    /// mismatch is set, the diameter+volume line is drawn amber on a rounded "pill" background.
    void draw_cell(wxDC& dc, int swatch_x, int text_x, const wxColour& swcol, const std::vector<wxColour>& scols, int ctype,
                   const wxString& name, bool bambu, const wxString& dia, const wxString& vol, bool mismatch,
                   const wxColour& txt, const wxColour& sub, const wxString& index_label = {})
    {
        const wxSize size = GetSize();
        const int    sw   = FromDIP(30);
        draw_swatch(dc, wxRect(swatch_x, (size.y - sw) / 2, sw, sw), swcol, scols, ctype, index_label);

        dc.SetFont(::Label::Body_13);
        const int l1h = dc.GetTextExtent(name).y;
        dc.SetFont(::Label::Body_11);
        const int l2h = dc.GetTextExtent("0").y;
        const int gap_v = FromDIP(2);
        const int y1 = (size.y - l1h - l2h - gap_v) / 2;
        const int y2 = y1 + l1h + gap_v;

        // line 1: bambu mark (Bambu brand only) + name
        int x = text_x;
        if (bambu && m_bambu_bmp.IsOk()) {
            dc.DrawBitmap(m_bambu_bmp, x, y1 + (l1h - m_bambu_bmp.GetHeight()) / 2, true);
            x += m_bambu_bmp.GetWidth() + FromDIP(4);
        }
        dc.SetFont(::Label::Body_13);
        dc.SetTextForeground(txt);
        dc.DrawText(name, x, y1);

        // line 2: diameter + volume type. On a mismatch the whole line is amber on a pill background.
        dc.SetFont(::Label::Body_11);
        const wxString l2 = line2_text(dia, vol);
        if (!l2.empty()) {
            const wxSize l2sz = dc.GetTextExtent(l2);
            if (mismatch) {
                const int px = FromDIP(6);
                const int ph = l2sz.y + FromDIP(4);
                // Remember the pill's bounds so a hover here (and only here) can raise a tooltip.
                m_pill_rect = wxRect(text_x - px, y2 + (l2sz.y - ph) / 2, l2sz.x + 2 * px, ph);
                dc.SetPen(*wxTRANSPARENT_PEN);
                dc.SetBrush(wxBrush(wxColour(0xFF, 0xF7, 0xED)));                  // pale pill background
                dc.DrawRoundedRectangle(m_pill_rect, ph / 2);
                dc.SetTextForeground(wxColour(0xFA, 0x94, 0x4B));                  // amber text
            } else {
                dc.SetTextForeground(sub);
            }
            dc.DrawText(l2, text_x, y2);
        }
    }

    void draw(wxDC& dc)
    {
        m_pill_rect = wxRect(); // recomputed below only if the dest cell paints its mismatch pill
        const wxSize   size = GetSize();
        const wxColour txt  = IsEnabled() ? StateColor::darkModeColorFor(ThemeColor::TextPrimary) : wxColour(0x90, 0x90, 0x90);
        const wxColour sub  = IsEnabled() ? StateColor::darkModeColorFor(ThemeColor::TextMuted)   : wxColour(0x90, 0x90, 0x90);
        const int pad = FromDIP(12);
        const int sw  = FromDIP(30);
        const int gap = FromDIP(8);

        const int tw = FromDIP(8), th = FromDIP(5);
        const int drop_x = size.x - pad - tw;

        // source cell, left-aligned; its swatch carries the project filament index (as in the sidebar list)
        draw_cell(dc, pad, pad + sw + gap, m_material_coloul, m_material_cols, m_material_ctype,
                  m_src_name, m_src_bambu, m_src_dia, m_src_vol, false, txt, sub,
                  m_index_label);

        // dest cell: shared left edge across rows, pushed as far right as the widest dest still fits
        // (leaving room for the dropdown); source stays left, so the two never collide.
        const wxString dname    = m_match ? m_dst_name : _L("Unmapped");
        const wxString ddia     = m_match ? m_dst_dia  : wxString();
        const wxString dvol     = m_match ? m_dst_vol  : wxString();
        const bool     dbambu   = m_match && m_dst_bambu;
        // "Not matching" = the mapped slot's nozzle differs from the source in diameter or volume type.
        const bool     mismatch = m_match &&
                                  ((!m_dst_dia.empty() && m_dst_dia != m_src_dia) ||
                                   (!m_src_vol.empty() && !m_dst_vol.empty() && m_dst_vol != m_src_vol));
        const int      dst_w    = m_max_dst > 0 ? m_max_dst : sw + gap + cell_text_width(dc, dname, dbambu, ddia, dvol);
        int            dleft    = drop_x - FromDIP(10) - dst_w;
        dleft = std::max(dleft, size.x / 2 + FromDIP(8)); // never cross into the source half
        draw_cell(dc, dleft, dleft + sw + gap, m_ams_coloul, m_ams_cols, m_ams_ctype,
                  dname, dbambu, ddia, dvol, mismatch, txt, sub);

        // arrow centred in the middle, between the left source and the right dest
        dc.SetFont(::Label::Head_14);
        dc.SetTextForeground(txt);
        const wxString arrow = wxString::FromUTF8("\xE2\x86\x92");
        dc.DrawText(arrow, (size.x - dc.GetTextExtent(arrow).x) / 2, (size.y - dc.GetTextExtent(arrow).y) / 2);

        // drop-down triangle, far right
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(txt));
        const int ty = (size.y - th) / 2;
        const wxPoint tri[3] = {{drop_x, ty}, {drop_x + tw, ty}, {drop_x + tw / 2, ty + th}};
        dc.DrawPolygon(3, tri);

        // border (green when selected)
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        if (m_selected)
            dc.SetPen(wxPen(ThemeColor::BrandGreen, FromDIP(2)));
        else
            dc.SetPen(wxPen(wxGetApp().dark_mode() ? wxColour(107, 107, 107) : wxColour(0xAC, 0xAC, 0xAC), FromDIP(1)));
        dc.DrawRoundedRectangle(0, 0, size.x, size.y, FromDIP(5));
    }

    wxBitmap m_bambu_bmp;
    wxString m_index_label; ///< source-swatch number: 1-based project filament slot (not the row position)
    wxString m_src_name, m_src_dia, m_src_vol;
    wxString m_dst_name, m_dst_dia, m_dst_vol;
    bool     m_src_bambu{false};
    bool     m_dst_bambu{false};
    int      m_max_dst{0}; ///< shared widest-dest-cell width (0 = not computed yet)
    wxRect   m_pill_rect;  ///< dest diameter/volume mismatch pill in client coords (empty when none)
    bool     m_pill_hover{false};
};

// Reparent every window a sizer lays out (recursing into nested sizers) onto a new parent window.
static void reparent_sizer_windows(wxSizer* s, wxWindow* parent)
{
    if (!s)
        return;
    for (auto* item : s->GetChildren()) {
        if (wxWindow* w = item->GetWindow())
            w->Reparent(parent);
        else if (wxSizer* sub = item->GetSizer())
            reparent_sizer_windows(sub, parent);
    }
}

MultiDiameterSyncAmsDialog::MultiDiameterSyncAmsDialog(wxWindow* parent, SyncInfo& info)
    : SyncAmsInfoDialog(parent, info, wxRESIZE_BORDER) // resizable frame; MSW bakes the border at creation
{
    // The sync flow verifies the connected/selected printer matches before opening this dialog,
    // so a same-printer direct-sync default is safe (deal_ok refines it per mode).
    m_result.direct_sync     = true;
    m_result.is_same_printer = true;
    apply_multi_diameter_layout();

    // Height-only resize: the content is fixed-width, so lock the width and bound the height between a
    // floor and the screen cap. The resize border itself is set at construction (see the base ctor).
    apply_resizable_bounds();
}

void MultiDiameterSyncAmsDialog::apply_scroll_height(int /*height*/)
{
    if (!m_scrolledWindow || !m_sizer_main)
        return;
    // Hidden panels (e.g. the Overwrite-mode mapping list) contribute 0 to the sizer's min size,
    // so the content height already tracks the active mode.
    m_scrolledWindow->Layout();
    const int content = m_sizer_main->GetMinSize().y;
    const int height  = std::clamp(content, FromDIP(240), max_scroll_height());
    m_scrolledWindow->SetMinSize(wxSize(-1, height));
    // No max cap: the dialog's own size hints bound the height, and the proportion chain lets the
    // scroll stretch with the dialog. A hard max here would pin min == max when content hits the cap.
    m_scrolledWindow->SetMaxSize(wxSize(-1, -1));
}

int MultiDiameterSyncAmsDialog::max_scroll_height() const
{
    const int idx    = wxDisplay::GetFromWindow(const_cast<MultiDiameterSyncAmsDialog*>(this));
    const int work_h = (idx != wxNOT_FOUND) ? wxDisplay(idx).GetClientArea().GetHeight() : FromDIP(1000);
    // Leave room for the title bar + button row so the scroll content never forces the dialog past
    // the work area; the dialog's own max (apply_resizable_bounds) is the harder screen bound.
    return std::max(FromDIP(300), work_h - FromDIP(160));
}

void MultiDiameterSyncAmsDialog::apply_resizable_bounds()
{
    const int  base_w = GetMinSize().x; // base pinned this as the design width; use it as the width floor
    const int  idx    = wxDisplay::GetFromWindow(this);
    const wxRect area  = (idx != wxNOT_FOUND) ? wxDisplay(idx).GetClientArea()
                                              : wxRect(0, 0, FromDIP(1400), FromDIP(1000));
    // Width: floor at the design width, ceiling a moderate step wider (never past the display). Height:
    // floor plus the display work area, decoupled from the dialog chrome so it can never collapse to
    // min == max. std::max guards a tiny screen from demanding a max below the floor.
    const int max_w = std::max(base_w, std::min(area.GetWidth() - FromDIP(30), base_w * 3 / 2));
    SetSizeHints(wxSize(base_w, FromDIP(240)), wxSize(max_w, area.GetHeight() - FromDIP(30)));
}

void MultiDiameterSyncAmsDialog::relax_content_width()
{
    if (!m_pages)
        return;
    // The base re-pins the content book to its design width on every mapping refresh; free it so it
    // stretches to the (now resizable) dialog width, keeping the design width as the floor.
    m_pages->SetMinSize(wxSize(GetMinSize().x, -1));
    m_pages->SetMaxSize(wxSize(-1, -1));
}

bool MultiDiameterSyncAmsDialog::apply_preview_size()
{
    if (!m_left_image_button || !m_right_image_button)
        return false;
    // The two cards split the content width; size each square preview to fill its half.
    const int content_w = usable_content_width();
    const int card_w    = (content_w - FromDIP(12)) / 2;      // 6px inner gap on each card edge
    const int s         = std::max(FromDIP(180), card_w - FromDIP(12));
    if (s == m_preview_px)
        return false;
    m_preview_px = s;
    for (wxButton* b : {m_left_image_button, m_right_image_button}) {
        b->SetMinSize(wxSize(s, s));
        b->SetSize(wxSize(s, s));
    }
    return true;
}

void MultiDiameterSyncAmsDialog::apply_header_text_width()
{
    const int content_w = usable_content_width();
    const int pad       = FromDIP(20);                                     // 10px on each side of a line
    const int desc_w    = std::max(FromDIP(260), content_w - pad);
    const int warn_w    = std::max(FromDIP(260), content_w - pad - FromDIP(16 + 6)); // also loses icon + gap
    // Re-pin each auto-wrap line to a fixed width (min == max keeps its best height deterministic for a
    // one-pass layout) and re-break it now so the coming Layout picks up the new height.
    auto fit = [](Label* l, int w) {
        if (!l) return;
        l->SetMinSize(wxSize(w, -1));
        l->SetMaxSize(wxSize(w, -1));
        l->Wrap(w);
    };
    fit(m_header_desc, desc_w);
    fit(m_header_warn, warn_w);
}

int MultiDiameterSyncAmsDialog::usable_content_width() const
{
    // Measured from the dialog (not an inner panel) so a narrow drag shrinks content back: an inner
    // panel's width is held up by the very min sizes the callers set, so it could never shrink. Reserve
    // a little for a possible scrollbar so content never overruns the viewport.
    const int w = GetClientSize().x - FromDIP(72) - FromDIP(20); // 36px side margins + scrollbar reserve
    return w > FromDIP(100) ? w : FromDIP(675) - FromDIP(72) - FromDIP(20);
}

bool MultiDiameterSyncAmsDialog::Show(bool show)
{
    // The base Show() builds the mapping rows (set_default) only now, after Plater's pre-show height
    // pin, so re-fit once the content is final. This runs synchronously before the modal loop's
    // first paint, so there is no visible resize.
    const bool ret = SyncAmsInfoDialog::Show(show);
    if (show) {
        relax_content_width(); // safety net in case no mapping ran during the base show
        apply_scroll_height(0);
        Layout();
        Fit();
        CenterOnScreen();
    }
    return ret;
}

void MultiDiameterSyncAmsDialog::apply_multi_diameter_layout()
{
    // One-time structural surgery on the base's twin-preview panel.
    if (!m_layout_applied) {
        // Drop the plate-picker row that sat below the two previews. The row sizer does not own
        // its windows, so hide the "Plate" caption (the ComboBox is re-parked below) before
        // freeing the sizer itself.
        if (m_two_thumbnail_panel_sizer && m_choose_plate_sizer) {
            const wxSizerItemList& row_cells = m_choose_plate_sizer->GetChildren();
            for (size_t j = 0; j < row_cells.size(); ++j)
                if (wxWindow* w = row_cells[j]->GetWindow())
                    if (w != m_combobox_plate)
                        w->Hide();
            m_two_thumbnail_panel_sizer->Detach(m_choose_plate_sizer);
            delete m_choose_plate_sizer;
            m_choose_plate_sizer = nullptr;
        }
        // Strip the horizontal preview row down to the image panel alone: this removes the
        // leading/trailing spacers and both swipe-button columns. Clear(false) deletes the row
        // items (and, through wxSizerItem::Free, the nested swipe-column sizers) but never the
        // windows they referenced, so the image panel and the swipe buttons stay alive.
        const wxSizerItemList& rows = m_two_thumbnail_panel_sizer->GetChildren();
        for (size_t i = 0; i < rows.size(); ++i) {
            if (wxSizer* row = rows[i]->GetSizer()) {
                row->Clear(false);
                // Let the two cards span the full content width (proportion 1) rather than sitting
                // centred in the gap the removed swipe columns left behind.
                row->Add(m_two_image_panel, 1, wxEXPAND | wxTOP, FromDIP(2));
            }
        }
        // The swipe buttons are windows owned by the panel (the removed sizers only referred to
        // them): hide them so the base's private update_swipe_button_state()/on_dpi_changed()
        // keep working on live, invisible buttons.
        m_swipe_left_button->Hide();
        m_swipe_right_button->Hide();
        // Park the plate ComboBox to the right of the "Original" title. The combo is re-parented
        // to the image panel first: a sizer positions its windows relative to the sizer's owner
        // window, so a window whose parent is a sibling would be placed at a shifted offset.
        if (m_left_title_sizer && m_combobox_plate && m_two_image_panel) {
            m_combobox_plate->Reparent(m_two_image_panel);
            m_left_title_sizer->Add(m_combobox_plate, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
        }
        // Wrap each mapping grid in a vertical-only scroll window so more than 5 filament rows
        // scroll instead of growing the dialog; the rows are centred inside.
        wrap_mapping_scroll(m_filament_panel, m_filament_panel_sizer, m_sizer_ams_mapping, m_mapping_scroll);
        wrap_mapping_scroll(m_fix_filament_panel, m_fix_filament_panel_sizer, m_fix_sizer_ams_mapping, m_fix_mapping_scroll);
        // Single-column mapping grids stretch their column so each row fills the card width.
        m_sizer_ams_mapping->AddGrowableCol(0);
        m_fix_sizer_ams_mapping->AddGrowableCol(0);
        // The base pins these mapping panels to a fixed 637px width, which left almost no right
        // margin once inset; free them so they span the 36px-inset content width like the preview.
        m_filament_panel->SetMinSize(wxSize(-1, -1));
        m_filament_panel->SetMaxSize(wxSize(-1, -1));
        m_fix_filament_panel->SetMinSize(wxSize(-1, -1));
        m_fix_filament_panel->SetMaxSize(wxSize(-1, -1));
        // Header (mode toggle), preview and mapping cards: hand StaticBox the semantic tokens and let
        // it map them through the dark palette at paint time. This dialog is cached and reused across
        // opens, so a pre-resolved colour would bake in the first-open theme and keep rendering the
        // dark card fill in a later light session (and vice-versa); paint-time mapping tracks the
        // active theme instead.
        const wxColour card_bg    = ThemeColor::Grey200;
        const wxColour surface_bg = ThemeColor::White;
        if (m_mode_panel) m_mode_panel->SetBackgroundColor(card_bg);
        m_filament_panel->SetBackgroundColor(card_bg);
        m_fix_filament_panel->SetBackgroundColor(card_bg);
        // The preview area holds two independent cards, so its wrappers are the plain dialog surface.
        m_two_thumbnail_panel->SetBackgroundColor(surface_bg);
        if (m_two_image_panel) m_two_image_panel->SetBackgroundColor(surface_bg);
        // Split the twin preview into two independent cards — "Original" (its header keeps the plate
        // combo) and "Mapped" — by reparenting each column's widgets into its own StaticBox card. The
        // base keeps driving the (live, reparented) preview buttons and titles.
        if (m_two_image_panel && m_two_image_panel_sizer && m_left_sizer_thumbnail && m_right_sizer_thumbnail) {
            auto* card_orig   = new StaticBox(m_two_image_panel);
            auto* card_mapped = new StaticBox(m_two_image_panel);
            m_preview_card_orig   = card_orig;   // kept so a width change can re-centre each preview
            m_preview_card_mapped = card_mapped;
            for (StaticBox* c : {card_orig, card_mapped}) {
                c->SetBorderWidth(0);
                c->SetBackgroundColor(card_bg);
            }
            // Detach the two columns before re-owning them (Clear would delete the sub-sizers).
            m_two_image_panel_sizer->Detach(m_left_sizer_thumbnail);
            m_two_image_panel_sizer->Detach(m_right_sizer_thumbnail);
            reparent_sizer_windows(m_left_sizer_thumbnail, card_orig);
            reparent_sizer_windows(m_right_sizer_thumbnail, card_mapped);
            card_orig->SetSizer(m_left_sizer_thumbnail);
            card_mapped->SetSizer(m_right_sizer_thumbnail);
            m_two_image_panel_sizer->Clear(false); // only spacers remain; safe (no sub-sizers left)
            m_two_image_panel_sizer->Add(card_orig, 1, wxEXPAND | wxRIGHT, FromDIP(6));
            m_two_image_panel_sizer->Add(card_mapped, 1, wxEXPAND | wxLEFT, FromDIP(6));
            // The "Original"/"After mapping" title labels kept the old preview panel's dark background
            // after reparenting; refresh_dynamic_colors() repaints them onto the card. They are plain
            // wx labels that, unlike StaticBox, do not remap on a theme switch, so it re-runs on show.
        }
        // Header text under the mode toggle: a mode-specific description line (refreshed by
        // on_mode_changed()) followed by a fixed warning line (icon + text). Stack them with the
        // toggle row in one vertical sizer (the toggle row keeps its own centring).
        if (m_mode_panel && m_mode_panel->GetSizer()) {
            wxSizer* toggle = m_mode_panel->GetSizer();
            m_mode_panel->SetSizer(nullptr, false); // detach the toggle row without deleting it
            auto* header_v = new wxBoxSizer(wxVERTICAL);
            header_v->Add(toggle, 0, wxEXPAND);

            m_header_desc = new Label(m_mode_panel, wxEmptyString, LB_AUTO_WRAP);
            m_header_desc->SetFont(::Label::Body_12);
            m_header_desc->SetMinSize(wxSize(FromDIP(520), -1));
            m_header_desc->SetMaxSize(wxSize(FromDIP(520), -1));
            m_header_desc->SetForegroundColour(StateColor::darkModeColorFor(ThemeColor::TextPrimary));
            header_v->Add(m_header_desc, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(10));

            auto* warn_sizer = new wxBoxSizer(wxHORIZONTAL);
            auto* warn_icon  = new wxStaticBitmap(m_mode_panel, wxID_ANY, create_scaled_bitmap("warning", m_mode_panel, 16),
                                                  wxDefaultPosition, wxSize(FromDIP(16), FromDIP(16)));
            m_header_warn = new Label(m_mode_panel,
                _L("After modification, the filament list cannot be restored."),
                LB_AUTO_WRAP);
            m_header_warn->SetFont(::Label::Body_12);
            m_header_warn->SetMinSize(wxSize(FromDIP(520), -1));
            m_header_warn->SetMaxSize(wxSize(FromDIP(520), -1));
            m_header_warn->SetForegroundColour(StateColor::darkModeColorFor(ThemeColor::TextPrimary));
            warn_sizer->Add(warn_icon, 0, wxALIGN_TOP | wxRIGHT, FromDIP(6));
            warn_sizer->Add(m_header_warn, 1, wxEXPAND);
            header_v->Add(warn_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));
            m_mode_panel->SetSizer(header_v);
        }
        // Shared 36px side margins so header, preview and mapping all span the same content width.
        for (wxWindow* panel : {static_cast<wxWindow*>(m_mode_panel),
                                static_cast<wxWindow*>(m_filament_panel),
                                static_cast<wxWindow*>(m_fix_filament_panel)})
            if (panel)
                if (wxSizerItem* it = m_sizer_main->GetItem(panel)) {
                    it->SetBorder(FromDIP(36));
                    it->SetFlag(wxEXPAND | wxLEFT | wxRIGHT);
                }
        // Position the twin-preview panel directly above the mapping panel (order is stable hereafter).
        if (m_sizer_main && m_two_thumbnail_panel && m_filament_panel) {
            m_sizer_main->Detach(m_two_thumbnail_panel);
            int                    index = 0;
            const wxSizerItemList& items = m_sizer_main->GetChildren();
            for (size_t i = 0; i < items.size(); ++i)
                if (items[i]->GetWindow() == m_filament_panel) { index = (int) i; break; }
            m_sizer_main->Insert(index, m_two_thumbnail_panel, FromDIP(0), wxEXPAND | wxLEFT | wxRIGHT, FromDIP(36));
        }
        // 8px vertical gaps between the header, preview and mapping sections.
        auto insert_gap_before = [this](wxWindow* w) {
            if (!w) return;
            const wxSizerItemList& items = m_sizer_main->GetChildren();
            for (size_t i = 0; i < items.size(); ++i)
                if (items[i]->GetWindow() == w) { m_sizer_main->InsertSpacer(i, FromDIP(8)); return; }
        };
        insert_gap_before(m_mode_panel);
        insert_gap_before(m_two_thumbnail_panel);
        insert_gap_before(m_filament_panel);
        // Advanced settings start folded; clicking the section header unfolds them.
        set_advanced_settings_expanded(false);
        // Drop the explanatory hint labels above and below the advanced-settings expander; the
        // expander itself stays. They would otherwise reappear on every mode switch.
        hide_advanced_settings_hint_labels();
        // The mapping scroll surfaces and reparented preview titles are plain wx widgets that cannot
        // self-map on a theme switch the way the StaticBox cards do; re-tint them now and on every
        // show, since this dialog is cached and can outlive a light/dark switch.
        Bind(wxEVT_SHOW, [this](wxShowEvent& e) { if (e.IsShown()) refresh_dynamic_colors(); e.Skip(); });
        refresh_dynamic_colors();

        // Let a taller dialog flow down to the mapping list so more rows appear (instead of only adding
        // whitespace). The stretch chain is m_sizer_this -> m_pages -> m_show_page -> m_scrolledWindow
        // (already proportion 1) -> m_filament_panel; the inner scroll is proportion 1 inside the card.
        // Nothing here inflates a minimum, so the dialog still shrinks back to the ~5-row floor.
        if (m_sizer_this && m_pages)
            if (wxSizerItem* it = m_sizer_this->GetItem(static_cast<wxWindow*>(m_pages)))
                it->SetProportion(1);
        if (wxSizerItem* it = m_sizer_main->GetItem(static_cast<wxWindow*>(m_filament_panel)))
            it->SetProportion(1);
        // StaticBox does not re-lay-out its sizer on resize, so when the card is stretched, lay it out
        // by hand so the inner scroll fills the new height.
        m_filament_panel->Bind(wxEVT_SIZE, [this](wxSizeEvent& e) {
            e.Skip();
            m_filament_panel->Layout();
        });

        // Width reflow. The header, preview and mapping cards are StaticBox and do not re-lay-out their
        // own sizers, and the previews/labels carry width-driven min sizes that would otherwise pin the
        // content wide and clip it on a narrow drag. So drive everything from the dialog's own size:
        // recompute the previews and header text from the (non-inflated) dialog width, then re-lay-out
        // outer-to-inner so the boxes can actually shrink back.
        Bind(wxEVT_SIZE, [this](wxSizeEvent& e) {
            e.Skip();
            if (!m_layout_applied)
                return;
            const bool grew = apply_preview_size();
            apply_header_text_width();
            Layout();
            for (wxWindow* p : {static_cast<wxWindow*>(m_mode_panel),
                                static_cast<wxWindow*>(m_two_thumbnail_panel),
                                static_cast<wxWindow*>(m_two_image_panel),
                                m_preview_card_orig, m_preview_card_mapped,
                                static_cast<wxWindow*>(m_filament_panel)})
                if (p) p->Layout();
            // Rescale the plate renders only when the size actually changed, and only once shown with
            // valid data, so a drag does not churn the bitmaps every pixel.
            if (grew && m_cur_input_thumbnail_data.is_valid() && IsShownOnScreen())
                update_final_thumbnail_data();
        });

        m_layout_applied = true;
    }

    // Keep the mapping rows capped to 5 rows (scroll for the rest), re-fitted on DPI change.
    refresh_mapping_scroll(m_mapping_scroll, m_sizer_ams_mapping, m_filaments.size());
    refresh_mapping_scroll(m_fix_mapping_scroll, m_fix_sizer_ams_mapping, m_fix_filaments.size());

    // Equal-size previews sized to fill each half-width card at the current width (re-applied on DPI
    // change and on resize). Kept square so the plate thumbnail is not stretched; the card centres it.
    // set_default_normal() rescales the bitmap to the button size, so the render grows with it.
    apply_preview_size();

    // The plate ComboBox parked beside the "Original" title makes the left title row taller than
    // the plain right title; reserve the combo's height on the right title so both previews align.
    // Width stays free (-1) so the longer "After overwriting" label keeps to one line.
    if (m_right_sizer_thumbnail && m_combobox_plate) {
        if (wxSizerItem* right_title = m_right_sizer_thumbnail->GetItem((size_t) 0))
            right_title->SetMinSize(wxSize(-1, m_combobox_plate->GetBestSize().y));
    }
    if (m_two_image_panel) {
        m_two_image_panel->Layout();
        m_two_image_panel->Fit();
    }

    if (m_scrolledWindow) {
        // Size the scroll area to the visible content (clamped to the screen) instead of the base's
        // fixed height; the ctor enables vertical resize around this adaptive height.
        apply_scroll_height(0);
        m_scrolledWindow->Layout();
        m_scrolledWindow->FitInside();
    }
    Layout();
}

void MultiDiameterSyncAmsDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    SyncAmsInfoDialog::on_dpi_changed(suggested_rect);
    apply_multi_diameter_layout();
    apply_resizable_bounds(); // the display/DPI may have changed; refresh the height cap
}

void MultiDiameterSyncAmsDialog::refresh_dynamic_colors()
{
    const wxColour card_bg  = StateColor::darkModeColorFor(ThemeColor::Grey200);
    const wxColour title_fg = StateColor::darkModeColorFor(ThemeColor::TextMuted);
    for (wxScrolledWindow* s : {m_mapping_scroll, m_fix_mapping_scroll})
        if (s) s->SetBackgroundColour(card_bg);
    // Left title is the first item of the left title row; the right title the first item of the
    // right column sizer.
    auto repaint_title = [&](wxSizer* title_sizer) {
        if (!title_sizer) return;
        if (wxSizerItem* it = title_sizer->GetItem((size_t) 0))
            if (wxWindow* w = it->GetWindow()) {
                w->SetBackgroundColour(card_bg);
                w->SetForegroundColour(title_fg);
            }
    };
    repaint_title(m_left_title_sizer);
    repaint_title(m_right_sizer_thumbnail);
}

void MultiDiameterSyncAmsDialog::wrap_mapping_scroll(wxWindow* owner, wxBoxSizer* owner_sizer,
                                                     wxFlexGridSizer* grid, wxScrolledWindow*& out_scroll)
{
    // Move the mapping grid out of the panel sizer into a vertical-only scroll window. The rows are
    // re-parented to the scroll window by the callers so scrolling pans them in place. The scroll
    // window keeps its content width (no horizontal scrollbar) and is centred in the panel; its
    // height is capped in refresh_mapping_scroll so more than 5 rows scroll.
    owner_sizer->Detach(grid);
    out_scroll = new wxScrolledWindow(owner, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    out_scroll->SetScrollRate(0, 20);
    out_scroll->EnableScrolling(false, true);
    // The scroll is the mapping card's surface: a light-gray card (dark-aware); its rows fill the width.
    out_scroll->SetBackgroundColour(StateColor::darkModeColorFor(ThemeColor::Grey200));
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(grid, 1, wxEXPAND, 0);
    out_scroll->SetSizer(sizer);
    owner_sizer->Add(out_scroll, 1, wxEXPAND | wxALL, FromDIP(6));
}

void MultiDiameterSyncAmsDialog::refresh_mapping_scroll(wxScrolledWindow* scroll, wxFlexGridSizer* grid,
                                                       size_t filament_rows)
{
    if (!scroll || !grid)
        return;
    // A wxScrolledWindow reports its best height as its min height in the scroll direction, so the
    // height cap is what makes more than 5 rows scroll instead of growing the dialog.
    const wxSize grid_min  = grid->GetMinSize();
    const int    grid_rows = (int) grid->GetChildren().size();
    const bool   has_toggle = grid_rows > (int) filament_rows; // the priority row is one grid row
    int          height     = grid_min.y;
    if (filament_rows > 5 && grid_rows > 0)
        height = grid_min.y * (has_toggle ? 6 : 5) / grid_rows;
    // Floor only: the ~5-row height is the minimum. The card is proportion 1, so ScrollLayout
    // stretches the list to fill a taller dialog (no vertical scrollbar) instead of capping it.
    scroll->SetMinSize(wxSize(-1, height));
    scroll->SetMaxSize(wxSize(-1, -1));
    scroll->Layout();
    scroll->FitInside();
    scroll->Scroll(0, 0);
}

std::vector<bool> MultiDiameterSyncAmsDialog::default_map_opt(MachineObject* obj) const
{
    const auto&  full_config = wxGetApp().preset_bundle->full_config();
    const size_t nozzle_nums = full_config.option<ConfigOptionFloatsNullable>("nozzle_diameter")->values.size();
    std::vector<bool> map_opt = (nozzle_nums > 1) ? std::vector<bool>{true, true, true, true}
                                                  : std::vector<bool>{false, true, false, true};
    if (obj && has_selector(obj)) { map_opt[2] = false; map_opt[3] = false; }
    return map_opt;
}

void MultiDiameterSyncAmsDialog::reset_and_sync_ams_list()
{
    // Tear down the previous build (DestroyChildren owns the widgets; the Material wrappers here).
    for (auto& kv : m_materialList)
        delete kv.second;
    m_materialList.clear();
    m_filaments.clear();
    m_sizer_ams_mapping->Clear(false);
    wxWindow* owner = m_mapping_scroll ? static_cast<wxWindow*>(m_mapping_scroll) : m_filament_panel;
    owner->DestroyChildren();
    m_priority_combo = nullptr; // destroyed with the children above; rebuilt below
    m_sizer_ams_mapping->SetCols(1);

    build_priority_toggle();
    build_sync_rows(owner, m_sizer_ams_mapping, m_materialList, m_filaments, true);

    // Auto-map by colour/type (base matcher, no diameter constraint); paints each row's dest half.
    DeviceManager* dev = wxGetApp().getDeviceManager();
    if (MachineObject* obj = dev ? dev->get_selected_machine() : nullptr)
        do_ams_mapping(obj);

    m_filament_panel->Show();
    m_sizer_ams_mapping->Layout();
    m_filament_panel_sizer->Layout();
    refresh_mapping_scroll(m_mapping_scroll, m_sizer_ams_mapping, m_filaments.size());
}

void MultiDiameterSyncAmsDialog::generate_override_fix_ams_list()
{
    if (m_generate_fix_sizer_ams_mapping)
        return;
    m_generate_fix_sizer_ams_mapping = true;

    for (auto& kv : m_fix_materialList)
        delete kv.second;
    m_fix_materialList.clear();
    m_fix_filaments.clear();
    m_fix_sizer_ams_mapping->Clear(false);
    wxWindow* owner = m_fix_mapping_scroll ? static_cast<wxWindow*>(m_fix_mapping_scroll) : m_fix_filament_panel;
    owner->DestroyChildren();
    m_fix_sizer_ams_mapping->SetCols(1);

    // Override mode shows the same horizontal rows, display-only (no click-to-remap).
    build_sync_rows(owner, m_fix_sizer_ams_mapping, m_fix_materialList, m_fix_filaments, false);

    // Preview the overwrite with an auto-map painted onto the display rows, honouring the active
    // scheme so the override preview matches the main mapping (Precision-first gates on diameter+flow).
    DeviceManager* dev = wxGetApp().getDeviceManager();
    if (MachineObject* obj = dev ? dev->get_selected_machine() : nullptr) {
        std::vector<FilamentInfo> result;
        DevMappingUtil::ams_filament_mapping(obj, m_fix_filaments, result, default_map_opt(obj), std::vector<int>(),
                                             wxGetApp().app_config->get_bool("ams_sync_match_full_use_color_dist") ? false : true,
                                             use_color_over_subtype(), nozzle_match_weight());
        paint_items(obj, m_fix_materialList, result);
    }
    m_fix_filament_panel->Show();
    m_fix_sizer_ams_mapping->Layout();
    if (m_fix_filament_panel_sizer)
        m_fix_filament_panel_sizer->Layout();
    refresh_mapping_scroll(m_fix_mapping_scroll, m_fix_sizer_ams_mapping, m_fix_filaments.size());
}

void MultiDiameterSyncAmsDialog::collect_project_filaments(std::vector<std::string>& materials,
                                                           std::vector<std::string>& display_materials,
                                                           std::vector<std::string>& display_names,
                                                           std::vector<std::string>& brands,
                                                           std::vector<std::string>& filament_ids) const
{
    auto preset_bundle = wxGetApp().preset_bundle;
    for (auto& filament_name : preset_bundle->filament_presets) {
        for (int i = 0; i < (int) preset_bundle->filaments.size(); ++i) {
            Preset* preset = &preset_bundle->filaments.preset(i);
            if (!preset || filament_name.compare(preset->name) != 0)
                continue;
            std::string display_type;
            materials.push_back(preset->config.get_filament_type(display_type));
            display_materials.push_back(display_type);
            filament_ids.push_back(preset->filament_id);
            auto*             vendor = dynamic_cast<ConfigOptionStrings*>(preset->config.option("filament_vendor"));
            const std::string brand  = (vendor && !vendor->values.empty()) ? vendor->values[0] : std::string();
            brands.push_back(brand);
            display_names.push_back(clean_filament_name(preset->name, brand));
        }
    }
}

/**
 * \brief The filaments the given plate uses, as ascending 0-based ids.
 *
 * \param specify_plate_idx   plate to read; negative selects the current plate
 *
 * Empty when the plate has no printable objects.
 */
static std::vector<int> plate_used_filaments(int specify_plate_idx)
{
    auto            &plate_list = wxGetApp().plater()->get_partplate_list();
    PartPlate       *plate      = specify_plate_idx < 0 ? plate_list.get_curr_plate() : plate_list.get_plate(specify_plate_idx);
    std::vector<int> ids;
    if (!plate) return ids;

    for (int e : plate->get_extruders(true)) // get_extruders returns 1-based ids
        ids.push_back(e - 1);
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

void MultiDiameterSyncAmsDialog::build_sync_rows(wxWindow* panel, wxFlexGridSizer* sizer,
                                                 MaterialHash& material_list, std::vector<FilamentInfo>& filaments,
                                                 bool interactive)
{
    std::vector<std::string> materials, display_materials, display_names, brands, filament_ids;
    collect_project_filaments(materials, display_materials, display_names, brands, filament_ids);
    const auto& cfg = wxGetApp().preset_bundle->full_config();

    // Mixed filaments are virtual blended slots with no physical spool, so they cannot be mapped to a tray.
    auto* is_mixed_opt = wxGetApp().preset_bundle->project_config.option<ConfigOptionBools>("filament_is_mixed");

    // Only the filaments the selected plate actually uses, so unused project filaments are hidden.
    const std::vector<int> extruders  = plate_used_filaments(get_specify_plate_idx());
    int item_index = 1;
    for (int extruder : extruders) {
        if (extruder < 0 || extruder >= (int) materials.size())
            continue;
        if (is_mixed_opt && extruder < (int) is_mixed_opt->values.size() && is_mixed_opt->values[extruder])
            continue;
        const std::string colour_str = wxGetApp().preset_bundle->project_config.opt_string("filament_colour", (unsigned int) extruder);
        const wxColour    colour     = DevAmsTray::decode_color(colour_str);
        const double      src_dia    = source_diameter_for(cfg, extruder);

        auto* item = new MaterialSyncItemH(panel, colour, _L(display_materials[extruder]), filament_ids[extruder]);
        // Source cell text: clean filament name (falls back to type), diameter, volume type, Bambu
        // mark by brand. The volume type is matched from the filament's selected nozzle diameter.
        wxString src_name = from_u8(display_names[extruder]);
        if (src_name.IsEmpty()) src_name = _L(display_materials[extruder]);
        const std::string src_vol = source_volume_for(cfg, extruder);
        item->set_source(src_name, from_u8(format_diameter_to_str(src_dia)),
                         src_vol.empty() ? wxString() : _L(src_vol), brand_is_bambu(brands[extruder]));
        // Swatch shows the original project filament slot (1-based), not the row position, so it
        // matches the sidebar list even when the plate uses only a subset of the project filaments.
        item->set_index_label(from_u8(std::to_string(extruder + 1)));

        FilamentInfo info;
        info.id                 = extruder;
        info.type               = materials[extruder];
        info.brand              = brands[extruder];
        info.filament_id        = filament_ids[extruder];
        info.color              = colour_str;
        info.nozzle_diameter    = src_dia;
        info.nozzle_volume_type = src_vol; // raw flow-type string; feeds the precision-first diameter+flow gate
        filaments.push_back(info);

        Material* material_item = new Material();
        material_item->id   = extruder;
        material_item->item = item;
        material_list[extruder] = material_item;

        const std::string item_index_str = std::to_string(item_index++);
        if (interactive)
            wire_sync_item(item, extruder, materials, item_index_str);  // sets index + click-to-popup
        else
            item->set_material_index_str(item_index_str);

        sizer->Add(item, 0, wxEXPAND | wxTOP, FromDIP(4));
    }
}

// Share the widest destination-cell width across a row list so every dest lines up on one left edge.
static void align_dest_column(MaterialHash& list)
{
    int max_dst = 0;
    for (auto& kv : list)
        if (auto* it = dynamic_cast<MaterialSyncItemH*>(kv.second->item))
            max_dst = std::max(max_dst, it->dst_cell_width());
    for (auto& kv : list)
        if (auto* it = dynamic_cast<MaterialSyncItemH*>(kv.second->item))
            it->set_max_dst(max_dst);
}

/**
 * \brief Resolve a printer slot's filament id to the project preset that syncing would select.
 *
 * Picks the same preset the sync picks — the compatible base preset carrying this id — so the
 * mapped-side preview matches the filament a sync will write. Null when no compatible preset matches.
 *
 * \param filament_id   the slot's filament id (tray info index)
 */
static const Preset* resolve_slot_preset(const std::string& filament_id)
{
    if (filament_id.empty())
        return nullptr;
    auto& filaments = wxGetApp().preset_bundle->filaments;
    for (auto it = filaments.begin(); it != filaments.end(); ++it) {
        Preset& p = *it;
        if (p.is_compatible && filaments.get_preset_base(p) == &p && p.filament_id == filament_id)
            return &p;
    }
    return nullptr;
}

void MultiDiameterSyncAmsDialog::paint_items(MachineObject* obj, MaterialHash& list, const std::vector<FilamentInfo>& result)
{
    // Reflect a mapping result onto the horizontal rows. The swatch colour is set via the base
    // set_ams_info; the dest cell's name / diameter / volume type / brand come from the matched slot
    // (the matcher result itself does not carry them), looked up by ams/slot id.
    const std::vector<FilamentInfo> slots = obj ? DevMappingUtil::enumerate_dest_slots(obj) : std::vector<FilamentInfo>{};
    auto find_slot = [&](const FilamentInfo& f) -> const FilamentInfo* {
        for (const auto& s : slots)
            if (s.ams_id == f.ams_id && s.slot_id == f.slot_id)
                return &s;
        return nullptr;
    };
    for (auto& kv : list) {
        auto* item = dynamic_cast<MaterialSyncItemH*>(kv.second->item);
        if (!item)
            continue;
        const FilamentInfo* mapped = nullptr;
        for (const auto& r : result)
            if (r.id == kv.second->id) { mapped = &r; break; }

        if (mapped && mapped->tray_id >= 0) {
            const wxColour col = mapped->color.empty() ? wxColour(0xCE, 0xCE, 0xCE) : DevAmsTray::decode_color(mapped->color);
            std::vector<wxColour> cols;
            for (const auto& c : mapped->colors) cols.push_back(DevAmsTray::decode_color(c));
            item->set_ams_info(col, wxGetApp().transition_tridid(mapped->tray_id, std::nullopt, obj), mapped->ctype, cols, true);

            const FilamentInfo* slot = find_slot(*mapped);
            const double        d    = slot ? slot->nozzle_diameter   : mapped->nozzle_diameter;
            const std::string   type = slot ? slot->type              : mapped->type;
            const std::string   vol  = slot ? slot->nozzle_volume_type : mapped->nozzle_volume_type;
            const std::string   fid  = slot && !slot->filament_id.empty() ? slot->filament_id : mapped->filament_id;

            // Recover a full name + brand from the slot filament's preset (falls back to the material
            // type; Bambu system filament ids start with "GF" when no preset/vendor is found).
            wxString name  = _L(type);
            bool     bambu = fid.rfind("GF", 0) == 0;
            if (const Preset* p = resolve_slot_preset(fid)) {
                auto*             v     = dynamic_cast<const ConfigOptionStrings*>(p->config.option("filament_vendor"));
                const std::string brand = (v && !v->values.empty()) ? v->values[0] : std::string();
                name  = from_u8(clean_filament_name(p->name, brand));
                bambu = brand_is_bambu(brand);
            }
            item->set_dest(name, d > 0.0 ? from_u8(format_diameter_to_str(d)) : wxString(),
                           vol.empty() ? wxString() : _L(vol), bambu);
        } else {
            item->reset_ams_info();
            item->set_dest(wxString(), wxString(), wxString(), false);
        }
    }
    align_dest_column(list); // recompute the shared dest column now that every dest is set
}

void MultiDiameterSyncAmsDialog::on_set_finish_mapping(wxCommandEvent& evt)
{
    SyncAmsInfoDialog::on_set_finish_mapping(evt);  // updates m_ams_mapping_result + dest colour/name
    DeviceManager* dev = wxGetApp().getDeviceManager();
    paint_items(dev ? dev->get_selected_machine() : nullptr, m_materialList, m_ams_mapping_result);  // dest diameter
}

void MultiDiameterSyncAmsDialog::on_mode_changed()
{
    if (!m_header_desc)
        return;
    m_header_desc->SetLabel(is_override_mode()
        ? _L("Overwrite the project filaments list with the filaments in the printer.")
        : _L("Match the project filaments with the filaments in the printer according to the matching mode. After syncing, the mapped results will be used."));
    // Re-wrap the description at its fixed width so its new height feeds the enclosing size pass.
    if (m_mode_panel)
        m_mode_panel->Layout();
}

void MultiDiameterSyncAmsDialog::update_when_change_plate(int idx)
{
    SyncAmsInfoDialog::update_when_change_plate(idx); // preview + selected-plate index + relayout
    // The base only swaps the preview; rebuild the rows so the source list tracks the chosen plate.
    reset_and_sync_ams_list();
    if (m_generate_fix_sizer_ams_mapping) { // rebuild the override list only when it already exists
        m_generate_fix_sizer_ams_mapping = false;
        generate_override_fix_ams_list();
    }
    // The rebuilds force-show their own list; Overwrite mode hides the match list, so restore that.
    if (is_override_mode()) m_filament_panel->Hide();
    if (m_scrolledWindow) {
        apply_scroll_height(0);
        m_scrolledWindow->Layout();
        m_scrolledWindow->FitInside();
    }
    Layout();
}

void MultiDiameterSyncAmsDialog::build_priority_toggle()
{
    wxWindow* owner = m_mapping_scroll ? static_cast<wxWindow*>(m_mapping_scroll) : m_filament_panel;
    auto* title = new Label(owner, _L("Filament matching"));
    title->SetFont(::Label::Head_13);
    title->SetForegroundColour(StateColor::darkModeColorFor(ThemeColor::TextPrimary));

    m_priority_combo = new ::ComboBox(owner, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                      wxSize(FromDIP(140), FromDIP(26)), 0, nullptr, wxCB_READONLY);
    m_priority_combo->Append(_L("Color first"));     // FilamentMatchScheme::ColorFirst
    m_priority_combo->Append(_L("Precision first")); // FilamentMatchScheme::PrecisionFirst
    m_priority_combo->SetSelection(m_scheme == FilamentMatchScheme::PrecisionFirst ? 1 : 0);
    m_priority_combo->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) {
        m_scheme = (m_priority_combo->GetSelection() == 0) ? FilamentMatchScheme::ColorFirst
                                                           : FilamentMatchScheme::PrecisionFirst;
        DeviceManager* dev = wxGetApp().getDeviceManager();
        if (MachineObject* obj = dev ? dev->get_selected_machine() : nullptr) {
            do_ams_mapping(obj);
            // The preview repaints from a cached per-filament colour table that is only rebuilt on
            // demand; force the rebuild so the rendered object colour follows the new mapping
            // (same refresh the reset-all path performs).
            update_final_thumbnail_data();
        }
        // do_ams_mapping's internal Fit() can collapse the width-freed mapping panel; re-layout the
        // scrolled window so it expands back to the full content width (the reset path does the same).
        m_sizer_ams_mapping->Layout();
        if (m_filament_panel_sizer) m_filament_panel_sizer->Layout();
        refresh_mapping_scroll(m_mapping_scroll, m_sizer_ams_mapping, m_filaments.size());
        if (m_scrolledWindow) { m_scrolledWindow->Layout(); m_scrolledWindow->FitInside(); }
    });

    auto* sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(title, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    sizer->Add(m_priority_combo, 0, wxALIGN_CENTER_VERTICAL, 0);
    m_sizer_ams_mapping->Add(sizer, 0, wxALIGN_LEFT | wxLEFT | wxTOP | wxBOTTOM, FromDIP(8));
}

bool MultiDiameterSyncAmsDialog::do_ams_mapping(MachineObject* obj_)
{
    // A fresh auto-map must start from an empty result. The base matcher (ams_filament_mapping)
    // *appends* one entry per project filament and never clears, and the base assumes the caller
    // cleared beforehand (it only clears in reinit_dialog / on timeout). Re-running it here on a
    // stale result — a filament-matching mode switch, or the fork's own re-sync — would otherwise
    // grow m_ams_mapping_result by N every time, leaving duplicate/stale ids for the dest-paint,
    // get_ams_mapping_result and deal_ok to trip over (the earlier m_filaments[k] out-of-bounds).
    m_ams_mapping_result.clear();
    // The base matcher and its preview plumbing do the work: Precision-first sets nozzle_match_weight()
    // to Require, so the matcher gates candidates on diameter+flow before colour and leaves a filament
    // unmapped when no compatible slot exists. We only add the dest name / diameter / volume onto the rows.
    const bool ok = SyncAmsInfoDialog::do_ams_mapping(obj_);
    // The base's show_thumbnail_page() (reached here) re-pins the content book to the design width, so
    // free it again before the layout settles — otherwise a re-map would snap a widened dialog back.
    relax_content_width();
    if (obj_)
        paint_items(obj_, m_materialList, m_ams_mapping_result);
    return ok;
}

void MultiDiameterSyncAmsDialog::deal_ok()
{
    if (is_override_mode()) {
        m_result.direct_sync = true;  // 直接覆盖: overwrite directly, no per-slot mapping
        return;
    }
    m_result.direct_sync = false;
    m_result.sync_maps.clear();

    // Record the mapped slot's own nozzle diameter so the sync targets the right nozzle.
    DeviceManager* dev = wxGetApp().getDeviceManager();
    MachineObject* obj = dev ? dev->get_selected_machine() : nullptr;
    const std::vector<FilamentInfo> slots = obj ? DevMappingUtil::enumerate_dest_slots(obj) : std::vector<FilamentInfo>{};
    auto slot_dia = [&](const FilamentInfo& f) -> double {
        for (const auto& s : slots)
            if (s.ams_id == f.ams_id && s.slot_id == f.slot_id)
                return s.nozzle_diameter;
        return f.nozzle_diameter;
    };
    for (const auto& f : m_ams_mapping_result) {
        if (f.id < 0)
            continue;
        auto& slot   = m_result.sync_maps[f.id];
        slot.ams_id  = f.ams_id;
        slot.slot_id = f.slot_id;
        const double d = slot_dia(f);
        if (d > 0.)
            slot.nozzle_diameter = std::to_string(d);
    }
}

} // namespace Slic3r::GUI
