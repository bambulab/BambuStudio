#include "FilamentView.hpp"

#include <algorithm>
#include <tuple>
#include <utility>

#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>
#include <wx/graphics.h>

#include "slic3r/GUI/FilamentBitmapUtils.hpp"
#include "slic3r/GUI/Widgets/StateColor.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"

namespace Slic3r::GUI {

namespace {
using Style = FilamentView::Style;

// The reel geometry is authored in this fixed artboard; it is scaled uniformly and centred into
// whatever size the widget is, so it keeps its shape at any size. These describe the artwork's
// coordinate space — change them only with new artwork, not to resize the widget.
constexpr double kArtworkW  = 28.0;
constexpr double kArtboardH = 26.0;

// The reel is lengthened by moving everything right of kSplitX (right flange and barrel end) further
// right, so the barrel can hold a few characters of label.
constexpr double kSplitX      = 14.0;
constexpr double kBarrelExtra = 12.0;
constexpr double kArtboardW   = kArtworkW + kBarrelExtra;
// Visible barrel at mid-height, i.e. the room a label has: from the outer stroke edge of the left
// flange ellipse to the inner stroke edge of the barrel's curved right end.
constexpr double kBarrelLeft  = 5.16316 + 3.38777 + 1.2;
constexpr double kBarrelRight = 23.357 + kBarrelExtra - 0.75;

/**
 * \brief Map an artwork x coordinate to the lengthened artboard.
 */
constexpr double stretch_x(double x) { return x < kSplitX ? x : x + kBarrelExtra; }

/**
 * \brief The design size (DIP) of a style: the source badge is a hair smaller than the destination spool.
 *
 * \param style  the drawn shape
 */
wxSize default_dip_size(Style style) { return style == Style::Square ? wxSize(24, 24) : wxSize(42, 28); }

/**
 * \brief Uniform scale + centre offsets placing the artboard in a box.
 */
struct Fit
{
    double dx, dy, s;
};

/**
 * \brief The fit that places the artboard inside a w x h box without distortion.
 *
 * \param w  box width
 * \param h  box height
 */
Fit fit_box(int w, int h)
{
    const double s = std::min(w / kArtboardW, h / kArtboardH);
    return {(w - kArtboardW * s) / 2.0, (h - kArtboardH * s) / 2.0, s};
}

/**
 * \brief Add the wound-filament barrel (between the flanges) to a path.
 *
 * \param p       the path to add to
 * \param ox, oy  artboard origin in the path's coordinates
 * \param s       artboard scale
 */
void add_barrel(wxGraphicsPath &p, double ox, double oy, double s)
{
    auto X = [&](double x) { return ox + stretch_x(x) * s; };
    auto Y = [&](double y) { return oy + y * s; };
    p.MoveToPoint(X(21.326), Y(3.28809));
    p.AddLineToPoint(X(6.03516), Y(3.64576));
    p.AddLineToPoint(X(6.47288), Y(4.4172));
    p.AddLineToPoint(X(7.78605), Y(8.08153));
    p.AddLineToPoint(X(8.22377), Y(15.9888));
    p.AddLineToPoint(X(7.34833), Y(19.8459));
    p.AddLineToPoint(X(6.03516), Y(22.1603));
    p.AddLineToPoint(X(21.326), Y(21.8026));
    p.AddCurveToPoint(X(22.4965), Y(19.6827), X(23.357), Y(16.4102), X(23.357), Y(12.7382));
    p.AddCurveToPoint(X(23.357), Y(8.83547), X(22.6248), Y(5.38401), X(21.326), Y(3.28809));
    p.CloseSubpath();
}

/**
 * \brief Draw a colour chip clipped to the spool barrel, so the spool shows every colour kind the
 *        flat source square does.
 *
 * \param gdc   the target
 * \param r     the widget rect the artboard fits into
 * \param chip  the colour chip bitmap
 */
void draw_barrel_clipped(wxGCDC &gdc, const wxRect &r, const wxBitmap &chip)
{
    wxGraphicsContext *gc = gdc.GetGraphicsContext();
    if (!gc || !chip.IsOk() || r.width <= 0 || r.height <= 0) return;
    // Fill the barrel path with the chip as a texture rather than clipping to a pixel mask: a mask has
    // no anti-aliasing, so its stair-stepped edge shows through the outline on the sloped barrel.
    const Fit f = fit_box(r.width, r.height);
    gc->PushState();
    gc->Translate(r.x, r.y); // the texture is anchored at the origin, so align it with the chip rect
    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->SetBrush(wxBrush(chip));
    wxGraphicsPath p = gc->CreatePath();
    add_barrel(p, f.dx, f.dy, f.s);
    gc->FillPath(p);
    gc->PopState();
}

/**
 * \brief The horizontal span of the visible barrel between the flanges, in widget coordinates.
 *
 * \param r  the widget rect the artboard fits into
 * \return  {left, width}
 */
std::pair<int, int> barrel_span(const wxRect &r)
{
    const Fit f = fit_box(r.width, r.height);
    const int l = int(r.x + f.dx + kBarrelLeft * f.s);
    return {l, int(r.x + f.dx + kBarrelRight * f.s) - l};
}

/**
 * \brief The largest font, no bigger than \p font, whose rendering of \p text fits \p max_width.
 *
 * \param dc         the DC the text will be drawn with
 * \param text       the label
 * \param font       the preferred (largest) font
 * \param max_width  available width in pixels
 * \return           \p font, shrunk as needed; never below a legible minimum, so very long text may still overflow
 */
wxFont fit_font(wxDC &dc, const wxString &text, wxFont font, int max_width)
{
    constexpr double kMinPointSize = 6.0;
    constexpr double kStep         = 0.5;
    dc.SetFont(font);
    while (dc.GetTextExtent(text).x > max_width && font.GetFractionalPointSize() - kStep >= kMinPointSize) {
        font.SetFractionalPointSize(font.GetFractionalPointSize() - kStep);
        dc.SetFont(font);
    }
    return font;
}

/**
 * \brief Draw the outline of the reel: barrel edge and both flanges.
 */
void draw_spool_outline(wxGCDC &gdc, const wxRect &r, const wxColour &outline)
{
    wxGraphicsContext *gc = gdc.GetGraphicsContext();
    if (!gc) return;
    const Fit    f  = fit_box(r.width, r.height);
    const double ox = r.x + f.dx, oy = r.y + f.dy, s = f.s;
    auto         X = [&](double x) { return ox + stretch_x(x) * s; };
    auto         Y = [&](double y) { return oy + y * s; };

    gc->SetBrush(*wxTRANSPARENT_BRUSH);

    wxGraphicsPath edge = gc->CreatePath();
    add_barrel(edge, ox, oy, s);
    gc->SetPen(wxPen(outline, 1.5 * s));
    gc->StrokePath(edge);

    gc->SetPen(wxPen(outline, 2.4 * s));
    wxGraphicsPath lflange = gc->CreatePath();
    lflange.AddEllipse(X(5.16316 - 3.38777), Y(12.9036 - 11.5716), 2 * 3.38777 * s, 2 * 11.5716 * s);
    gc->StrokePath(lflange);

    wxGraphicsPath rflange = gc->CreatePath();
    rflange.MoveToPoint(X(20.4902), Y(3.83873));
    rflange.AddCurveToPoint(X(21.0667), Y(2.50253), X(21.9279), Y(1.52441), X(22.688), Y(1.52441));
    rflange.AddCurveToPoint(X(24.6406), Y(1.52441), X(26.2234), Y(6.70518), X(26.2234), Y(13.096));
    rflange.AddCurveToPoint(X(26.2234), Y(19.4868), X(24.6406), Y(24.6675), X(22.688), Y(24.6675));
    rflange.AddCurveToPoint(X(21.8574), Y(24.6675), X(21.0937), Y(23.7299), X(20.4902), Y(22.1604));
    gc->StrokePath(rflange);
}
} // namespace

FilamentView::FilamentView(wxWindow *parent, Style style, const wxSize &size) : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE), m_style(style)
{
    SetInitialSize(size == wxDefaultSize ? FromDIP(default_dip_size(style)) : size);
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_PAINT, &FilamentView::on_paint, this);
}

void FilamentView::set_colors(const std::vector<wxColour> &colors, bool gradient)
{
    m_colors   = colors;
    m_gradient = gradient;
    Refresh();
}

void FilamentView::set_label(const wxString &label)
{
    if (m_label == label) return;
    m_label = label;
    Refresh();
}

void FilamentView::set_style(Style style)
{
    if (m_style == style) return;
    m_style = style;
    Refresh();
}

void FilamentView::on_paint(wxPaintEvent &)
{
    wxAutoBufferedPaintDC dc(this);
    const wxColour        bg = GetParent() ? GetParent()->GetBackgroundColour() : GetBackgroundColour();
    dc.SetBackground(wxBrush(bg));
    dc.Clear();

    wxGCDC gdc(dc);

    const wxSize sz    = GetClientSize();
    const int    inset = FromDIP(1);
    const wxRect r(inset, inset, sz.x - 2 * inset, sz.y - 2 * inset);

    std::vector<wxColour> colors = m_colors;
    if (colors.empty()) colors.push_back(StateColor::darkModeColorFor(ThemeColor::Grey300));

    // One colour chip from the shared helper covers single / multi / opacity / gradient.
    const wxBitmap chip = create_filament_bitmap(colors, r.GetSize(), m_gradient && colors.size() > 1);

    if (m_style == Style::Square) {
        const wxColour badge_border = StateColor::darkModeColorFor(ThemeColor::Grey350);
        if (chip.IsOk()) gdc.DrawBitmap(chip, r.x, r.y, true); // source: the helper's rectangle, drawn flat
        gdc.SetBrush(*wxTRANSPARENT_BRUSH);
        gdc.SetPen(wxPen(badge_border, 1));
        gdc.DrawRectangle(r);
    } else {
        const wxColour reel_outline = ThemeColor::Grey450;
        draw_barrel_clipped(gdc, r, chip); // dest: same chip, clipped to the reel barrel
        draw_spool_outline(gdc, r, reel_outline);
    }

    if (!m_label.empty()) {
        const wxColour base   = colors.front();
        const bool     onDark = base.Alpha() > 200 && base.GetLuminance() < 0.6; // translucent/light -> dark text
        gdc.SetTextForeground(onDark ? ThemeColor::White : ThemeColor::TextPrimary);
        int left = r.x, width = r.width;
        if (m_style == Style::Spool) {
            std::tie(left, width) = barrel_span(r);
            // a long label (e.g. "EXT") gets a smaller font so it stays inside the barrel, clear of the outline
            gdc.SetFont(fit_font(gdc, m_label, Label::Body_10, width - 2 * FromDIP(1)));
        } else {
            gdc.SetFont(Label::Body_13);
        }
        const wxSize ts = gdc.GetTextExtent(m_label);
        gdc.DrawText(m_label, left + (width - ts.x) / 2, r.y + (r.height - ts.y) / 2);
    }
}

} // namespace Slic3r::GUI
