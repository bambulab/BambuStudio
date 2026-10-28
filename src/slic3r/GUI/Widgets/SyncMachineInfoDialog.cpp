#include "SyncMachineInfoDialog.hpp"

#include "Button.hpp"
#include "StateColor.hpp"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevNozzleRack.h"
#include "slic3r/GUI/DeviceCore/DevNozzleSystem.h"
#include "slic3r/GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/ResolverSlotState.hpp"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_Utils.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/wxExtensions.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <wx/dcbuffer.h>
#include <wx/dcmemory.h>
#include <wx/gdicmn.h>
#include <wx/graphics.h>
#include <wx/sizer.h>
#include <wx/string.h>
#include <wx/timer.h>

namespace Slic3r ::GUI {

enum class NozzleCardState { Normal, Empty, Abnormal, Unknown, Lost };

// Data needed to render a single rack hotend column.
struct HotendEntry
{
    int                      index{};
    bool                     filament_loaded{false};
    std::vector<std::string> filament_colors;      // hex "#RRGGBB" per stop. 0 → unknown, 1 → single, 2+ → multi/gradient
    bool                     is_gradient{false};   // CTYPE_MULTI (linear blend) vs CTYPE_GRADIANT (pie wedges) — enum names are misleading
    std::string              filament_name;        // PLA, PETG...
    int                      filament_remain{100}; // 0..100, drives the spool's partial arc
    wxString                 nozzle_diameter;      // "0.4 mm"
    wxString                 nozzle_flow_type;     // "Standard" / "High Flow"
    bool                     syncing{false};       // true while the printer reports any of this hotend's filament as being read
    bool                     is_hotend{true};
    bool                     is_ext{false};        // filament fed from an external spool (virtual tray)
    NozzleCardState          nozzle_state{NozzleCardState::Normal};
};

namespace {
constexpr int kColW        = 104;
constexpr int kCardRadius  = 4;
constexpr int kHeaderH     = 16;
constexpr int kSpoolZoneH  = 84;
constexpr int kHotendZoneH = 40;
constexpr int kSpoolDiam   = 52;
constexpr int kSpoolRingR  = 23;
constexpr int kSpoolHoleR  = 9;
constexpr int kLoadingDiam = 52;
constexpr int kColTotalH   = kHeaderH + kSpoolZoneH + kHotendZoneH;

const wxColour kRingDonutEdge{0, 0, 0, 16}; // rgba(0,0,0,0.06) edge over the colored donut

// `C(...)` re-maps a light-mode color to its dark-mode pair at draw time
inline wxColour C(const wxColour &light) { return StateColor::darkModeColorFor(light); }

const wxString kColorFallbackSentinel("#D9D9D9"); // firmware "unknown filament color" sentinel

int nozzle_id(const DevNozzle &n)
{
    if (auto a = n.GetAddrId()) return a.value();
    return n.GetNozzleId();
}

// AMS slots whose read state this hotend follows: the current feed bind plus any
// distinct preload slot. Binds to an external spool are left out — no AMS unit
// sits behind them, so there is nothing to report.
std::vector<DevAmsSlotId> CollectRefreshSlots(MachineObject *obj, const DevNozzle &n)
{
    std::vector<DevAmsSlotId> slots;
    auto                      fila_sys = obj ? obj->GetFilaSystem() : nullptr;
    if (!fila_sys) return slots;

    auto append = [&](const DevAmsSlotId &slot) {
        // An external spool bind has no reader, so it must never reach the refresh command.
        if (fila_sys->IsVirtualTrayId(std::to_string(slot.first))) return;
        if (std::find(slots.begin(), slots.end(), slot) == slots.end()) slots.push_back(slot);
    };
    for (const auto &slot : n.GetBindAmsSlots()) append(slot);
    for (const auto &slot : n.GetPreloadBindAmsSlots()) append(slot);
    return slots;
}

// Resolve a HotendEntry from the live device model
HotendEntry make_hotend(MachineObject *obj, const DevNozzle &n)
{
    HotendEntry e;
    e.index            = nozzle_id(n);
    e.filament_loaded  = n.HasFilamentAtNozzleInlet();
    e.nozzle_diameter  = n.GetNozzleDiameterStr();
    e.nozzle_flow_type = (n.GetNozzleFlowType() == NozzleFlowType::H_FLOW) ? _L("High Flow") : _L("Standard");
    e.is_hotend        = n.IsHotend();

    // A rack nozzle whose holder still reports it seated but which is no longer
    // detected reads as "empty" via IsEmpty(); IsToolLostInHolder() is the only
    // predicate that separates a dropped nozzle from a truly empty slot.
    if (n.IsOnRack() && n.IsToolLostInHolder())
        e.nozzle_state = NozzleCardState::Lost;
    else if (n.IsEmpty())
        e.nozzle_state = NozzleCardState::Empty;
    else if (n.IsUnknown())
        e.nozzle_state = NozzleCardState::Unknown;
    else if (n.IsAbnormal())
        e.nozzle_state = NozzleCardState::Abnormal;

    if (e.nozzle_state != NozzleCardState::Normal) { e.filament_loaded = false; }

    const auto &slots = n.GetBindAmsSlots();
    if (!slots.empty() && obj) {
        auto tray_opt = obj->get_tray(slots[0].first, slots[0].second);
        if (tray_opt.has_value()) {
            const auto &tray = *tray_opt;
            if (auto fila_sys = obj->GetFilaSystem())
                e.is_ext = fila_sys->IsVirtualTrayId(std::to_string(slots[0].first));
            if (!tray.sub_brands.empty())
                e.filament_name = tray.sub_brands;
            else if (tray.m_fila_type == "PLA-S")
                e.filament_name = "Sup.PLA";
            else if (tray.m_fila_type == "PA-S")
                e.filament_name = "Sup.PA";
            else if (tray.m_fila_type == "ABS-S")
                e.filament_name = "Sup.ABS";
            else // resolve the sub-type suffix from the filament preset (external spools carry no sub_brands)
                e.filament_name = DevicePageFilamentSlotStateResolver::ResolveFilamentCardText(&tray, tray.m_fila_type);
            e.filament_remain = std::clamp(tray.remain, 1, 100); // floor at 1 so an empty-but-loaded spool still draws a sliver
            if (!tray.cols.empty()) {
                e.filament_colors = tray.cols;
                e.is_gradient     = (tray.ctype == DevFilaColorType::CTYPE_GRADIANT);
            }
        }
    }

    // Any of this hotend's slots being read keeps its card spinning, preload included.
    for (const auto &[ams_id, slot_id] : CollectRefreshSlots(obj, n)) {
        if (auto tray = obj->get_tray(ams_id, slot_id); tray.has_value() && tray->is_reading(obj)) {
            e.syncing = true;
            break;
        }
    }

    // Fallback: if the AMS lookup didn't produce a color stop, take the
    // nozzle's own color hint so the spool still picks up the right hue.
    if (e.filament_loaded && e.filament_colors.empty()) {
        const std::string c = n.GetFilamentColor();
        if (!c.empty()) e.filament_colors.push_back(c);
    }

    return e;
}

// Walk a MachineObject's nozzle system and return a HotendEntry per hotend
std::vector<HotendEntry> CollectHotendEntries(MachineObject *obj)
{
    std::vector<HotendEntry> entries;
    if (!obj) return entries;

    DevNozzleSystem *nozzle_sys = obj->GetNozzleSystem();
    if (!nozzle_sys) return entries;

    // rack type B/C cant't auto read rfid
    auto rack = nozzle_sys->GetNozzleRack();
    if (!rack) return entries;
    const auto rack_type = rack->GetRackType();
    if (rack_type != DevNozzleRack::RackType_B && rack_type != DevNozzleRack::RackType_C) return entries;

    std::map<int, const DevNozzle *> nozzles_by_id;
    for (const auto &kv : nozzle_sys->GetRackNozzles()) nozzles_by_id[nozzle_id(kv.second)] = &kv.second;
    for (const auto &kv : nozzle_sys->GetExtNozzles()) nozzles_by_id.emplace(nozzle_id(kv.second), &kv.second);

    // The map walks nozzles in ascending id — the order the card grid and the
    // poll update rely on. Add in that order; never index by id, which may be sparse.
    entries.reserve(nozzles_by_id.size());
    for (const auto &[_, np] : nozzles_by_id) entries.push_back(make_hotend(obj, *np));
    return entries;
}

wxColour ResolveFilamentColor(const std::string &hex_in)
{
    const wxColour fallback = C(ThemeColor::Grey400);
    if (hex_in.empty()) return fallback;

    wxString hex = wxString::FromUTF8(hex_in); // empty when hex_in is not valid UTF-8
    if (hex.IsEmpty()) return fallback;
    if (hex[0] != '#') hex.Prepend("#");

    wxColour parsed;
    if (!parsed.Set(hex)) return fallback;

    // The sentinel arrives alpha-padded ("D9D9D9FF"), so match on RGB alone —
    // comparing the whole string never fired and let it through as a real grey.
    const wxColour sentinel(kColorFallbackSentinel);
    if (parsed.Alpha() == wxALPHA_OPAQUE && parsed.Red() == sentinel.Red() && parsed.Green() == sentinel.Green() && parsed.Blue() == sentinel.Blue()) return fallback;

    return parsed;
}

// A donut wedge (outer radius r1, inner radius r0) spanning [a0, a1], CW.
// Mirrors the web wedgePath used for the single-color remain arc.
wxGraphicsPath DonutWedge(wxGraphicsContext *gc, int cx, int cy, double r1, double r0, double a0, double a1)
{
    wxGraphicsPath p = gc->CreatePath();
    p.MoveToPoint(cx + r1 * std::cos(a0), cy + r1 * std::sin(a0));
    p.AddArc(cx, cy, r1, a0, a1, true /* CW */);
    p.AddLineToPoint(cx + r0 * std::cos(a1), cy + r0 * std::sin(a1));
    p.AddArc(cx, cy, r0, a1, a0, false /* CCW back */);
    p.CloseSubpath();
    return p;
}

// ---------------------------------------------------------------------------
// Filament colour policy — a port of the device page's MaterialDonut
// (web: features/device-page/ams-filament-hotend/components/SpoolGraphic.tsx).
// Keep the two in sync:
//   single       flat disc
//   multi        concentric bands, colors[0] innermost .. colors[N-1] on the rim
//   gradient     radial blend over that same inner -> outer axis
//   translucent  any alpha < 255 puts a checkerboard under the paint, and the
//                paint keeps the firmware alpha (floored so a near-clear spool
//                still tints the checker instead of vanishing into it)
//   remain       every one of the above is confined to the remaining wedge
// ---------------------------------------------------------------------------
constexpr int    kCheckerTileVB = 6;   // checker tile, in the 52-unit spool viewBox
constexpr double kMinPaintAlpha = 0.2; // opacity floor for a near-clear spool

const wxColour kCheckerBg{0xDF, 0xEE, 0xE4};
const wxColour kCheckerFg{0x9E, 0xD3, 0xAE};

// Any partial alpha counts as translucent, not just a fully clear spool.
bool HasTranslucentColor(const std::vector<wxColour> &colors)
{
    return std::any_of(colors.begin(), colors.end(), [](const wxColour &c) { return c.Alpha() < wxALPHA_OPAQUE; });
}

// Firmware alpha drives the paint opacity, floored so the colour still reads.
wxColour SpoolPaintColor(const wxColour &c)
{
    const double a = std::max(kMinPaintAlpha, c.Alpha() / 255.0);
    return wxColour(c.Red(), c.Green(), c.Blue(), (unsigned char) std::lround(a * 255.0));
}

// 2x2 checker tile used as the translucent backdrop, in the device page tones.
wxBitmap MakeCheckerTile(int tile)
{
    const int  half = std::max(1, tile / 2);
    wxBitmap   bmp(half * 2, half * 2);
    wxMemoryDC dc(bmp);
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(kCheckerBg));
    dc.DrawRectangle(0, 0, half * 2, half * 2);
    dc.SetBrush(wxBrush(kCheckerFg));
    dc.DrawRectangle(0, 0, half, half);
    dc.DrawRectangle(half, half, half, half);
    dc.SelectObject(wxNullBitmap);
    return bmp;
}

// Paintable area of the colour donut: the whole [r0, r1] annulus for a full
// spool, otherwise only the remaining wedge (from 12 o'clock, CW). Filled with
// the default odd-even rule, so the two circles leave the hub cut out.
wxGraphicsPath SpoolFillArea(wxGraphicsContext *gc, int cx, int cy, double r1, double r0, int remain)
{
    if (remain >= 100) {
        wxGraphicsPath p = gc->CreatePath();
        p.AddCircle(cx, cy, r1);
        p.AddCircle(cx, cy, r0);
        return p;
    }
    const double start = -M_PI / 2;
    return DonutWedge(gc, cx, cy, r1, r0, start, start + (remain / 100.0) * 2 * M_PI);
}

// colors[0] is the innermost band. Painted outermost first, each band covering
// everything inside it, so neighbours share an edge without an antialias seam.
void FillSpoolBands(wxGraphicsContext *gc, int cx, int cy, double ring_r, double hole_r, int remain, const std::vector<wxColour> &colors)
{
    const double band = (ring_r - hole_r) / colors.size();
    for (size_t i = colors.size(); i-- > 0;) {
        const double r1 = (i + 1 == colors.size()) ? ring_r : hole_r + band * (i + 1);
        gc->SetBrush(wxBrush(SpoolPaintColor(colors[i])));
        gc->FillPath(SpoolFillArea(gc, cx, cy, r1, hole_r, remain));
    }
}

// Radial blend anchored so colors[0] lands on the hub edge and colors[N-1] on
// the rim; spreading the stops over the full radius instead would bury the
// first ones under the hub. Mirrors gradientStopOffset() on the web.
void FillSpoolGradient(wxGraphicsContext *gc, int cx, int cy, double ring_r, double hole_r, int remain, const std::vector<wxColour> &colors)
{
    const double            inner = ring_r > 0.0 ? hole_r / ring_r : 0.0;
    wxGraphicsGradientStops stops(SpoolPaintColor(colors.front()), SpoolPaintColor(colors.back()));
    // The ctor already pins colors[0] at offset 0, which gives the flat lead-in
    // across the hidden centre; these are the stops on the visible ring.
    for (size_t i = 0; i + 1 < colors.size(); ++i) stops.Add(SpoolPaintColor(colors[i]), inner + (1.0 - inner) * (double(i) / (colors.size() - 1)));

    gc->SetBrush(gc->CreateRadialGradientBrush(cx, cy, cx, cy, ring_r, stops));
    gc->FillPath(SpoolFillArea(gc, cx, cy, ring_r, hole_r, remain));
}

// Checker backdrop plus the colour layers, over the remaining part of the spool.
void PaintSpoolMaterial(wxGraphicsContext *gc, int cx, int cy, double ring_r, double hole_r, int remain, bool is_gradient, const std::vector<wxColour> &colors)
{
    if (colors.empty() || remain <= 0) return;

    if (HasTranslucentColor(colors)) {
        const int      tile     = std::max(2, (int) std::lround(ring_r * kCheckerTileVB / double(kSpoolRingR)));
        const wxBitmap tile_bmp = MakeCheckerTile(tile);
        gc->SetBrush(wxBrush(tile_bmp));
        gc->FillPath(SpoolFillArea(gc, cx, cy, ring_r, hole_r, remain));
    }

    if (is_gradient && colors.size() > 1)
        FillSpoolGradient(gc, cx, cy, ring_r, hole_r, remain, colors);
    else
        FillSpoolBands(gc, cx, cy, ring_r, hole_r, remain, colors); // one colour degenerates to a flat disc
}

// Spool ring renderer
void PaintSpoolDisc(wxGraphicsContext *gc, int cx, int cy, int outer_r, int hole_r, bool loaded, int remain, bool is_gradient, const std::vector<wxColour> &colors)
{
    const double ring_r = outer_r * (double(kSpoolRingR) / double(kSpoolDiam / 2)); // 23/26 of outer

    // Outer disc — fill + 1px edge
    gc->SetBrush(wxBrush(C(ThemeColor::Grey350)));
    gc->SetPen(wxPen(C(ThemeColor::Grey400), 1));
    gc->DrawEllipse(cx - outer_r, cy - outer_r, 2 * outer_r, 2 * outer_r);

    gc->SetPen(*wxTRANSPARENT_PEN);
    PaintSpoolMaterial(gc, cx, cy, ring_r, hole_r, remain, is_gradient, colors);

    // Faint edge over the colored donut, then the hub on top.
    gc->SetBrush(*wxTRANSPARENT_BRUSH);
    gc->SetPen(wxPen(C(kRingDonutEdge), 1));
    gc->DrawEllipse(cx - ring_r, cy - ring_r, 2 * ring_r, 2 * ring_r);

    // hole
    gc->SetBrush(wxBrush(C(*wxWHITE)));
    gc->SetPen(wxPen(C(ThemeColor::Grey350), 1));
    gc->DrawEllipse(cx - hole_r, cy - hole_r, 2 * hole_r, 2 * hole_r);
}

// Small grey "sticker" on the right of the spool, drawn on top of the colored
// band so it stays visible. Geometry matches the web spool_filament_tag_figma
// sticker path (52 viewBox: x 38..47.5, y 24.5..27.5), scaled to the disc.
void PaintSpoolTag(wxGraphicsContext *gc, int cx, int cy, int outer_r)
{
    const double s     = double(outer_r) / 26.0; // viewBox unit → device
    const double tag_w = 9.5 * s;
    const double tag_h = 3.0 * s;
    const double tag_x = cx + 12.0 * s; // 38 - 26 (center)
    const double tag_y = cy - tag_h / 2.0;
    gc->SetBrush(wxBrush(ThemeColor::Grey500));
    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->DrawRoundedRectangle(tag_x, tag_y, tag_w, tag_h, tag_h / 2.0);
}

// Truncate `name` with an ellipsis until it fits `avail` width. Returns the
// final display string, plus its measured width and height (so the caller
// can lay out without re-measuring).
struct MeasuredText
{
    wxString text;
    int      width;
    int      height;
};

MeasuredText MeasureNameTruncated(wxGraphicsContext *gc, const wxString &name_in, int avail)
{
    wxString name = name_in;
    double   tw = 0, th = 0, descent = 0, leading = 0;
    gc->GetTextExtent(name, &tw, &th, &descent, &leading);
    if (tw > avail) {
        while (name.length() > 2 && tw > avail) {
            name = name.Mid(0, name.length() - 1);
            gc->GetTextExtent(name + "...", &tw, &th, &descent, &leading);
        }
        name += "...";
    }
    return {name, (int) tw, (int) th};
}

} // namespace

// ---------------------------------------------------------------------------
// HotendColumn
//
//   +--------------------+
//   | [1]                |   <-- header  : index pill only
//   |        ●●●●        |
//   |       ●    ●       |
//   |      ●  ◯   ● ━━   |   <-- spool   : disc + filament-tag + name below
//   |       ●    ●       |              when syncing: rotated sync_loading_arc fills this zone
//   |        ●●●●        |
//   |     Filament name  |
//   |  [hot]  0.4 mm     |   <-- hotend  : nozzle bitmap + diameter/flow
//   |         Standard   |
//   +--------------------+
//
// Filled grey rounded card, no outline. OnPaint draws the card, then each zone.
// ---------------------------------------------------------------------------
class HotendColumn : public wxPanel
{
public:
    HotendColumn(wxWindow *parent, const HotendEntry &entry);
    void UpdateEntry(const HotendEntry &entry);

    // True while the printer reports this hotend's filament as being read. The
    // printer ends a read, not this dialog, so a spinning card always means a
    // read is really in flight.
    bool IsSpinning() const { return m_entry.syncing; }

private:
    void OnPaint(wxPaintEvent &event);
    void UpdateAnimTimer();

    void PaintHeader(wxGraphicsContext *gc, const wxRect &r);
    void PaintSpool(wxGraphicsContext *gc, const wxRect &r, const std::vector<wxColour> &palette);
    void PaintHotend(wxGraphicsContext *gc, const wxRect &r);

    HotendEntry    m_entry;
    double         m_spin_angle{0.0};          // radians, advanced while spinning
    wxTimer        m_anim_timer;               // 33ms tick that advances m_spin_angle
    ScalableBitmap m_nozzle_bmp;               // hotend glyph for every state (this dialog's own icon)
    ScalableBitmap m_nozzle_error_badge_bmp;   // red "!" badge overlaid when abnormal (device-page hotend_error_badge)
    ScalableBitmap m_nozzle_unknown_badge_bmp; // grey "?" badge overlaid when unknown (device-page hotend_unknown_badge)
    ScalableBitmap m_nozzle_lost_badge_bmp;    // grey "!" badge overlaid when the tool is seated but dropped (device-page hotend_lost_badge)
    ScalableBitmap m_nozzle_empty_badge_bmp;   // grey "prohibited" ring overlaid when the rack slot is empty (device-page hotend_null prohibit)
    ScalableBitmap m_spinner_bmp;              // sync_loading_arc.svg, rotated by m_spin_angle while spinning
};

HotendColumn::HotendColumn(wxWindow *parent, const HotendEntry &entry) : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE), m_entry(entry)
{
    SetBackgroundColour(parent->GetBackgroundColour());
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    // Without a min size the panel collapses to 0 height in the sizer
    SetMinSize(wxSize(FromDIP(kColW), FromDIP(kColTotalH)));

    m_nozzle_bmp               = ScalableBitmap(this, "dev_rack_nozzle_print_job", 24);
    m_nozzle_error_badge_bmp   = ScalableBitmap(this, "hotend_error_badge", 16);
    m_nozzle_unknown_badge_bmp = ScalableBitmap(this, "hotend_unknown_badge", 16);
    m_nozzle_lost_badge_bmp    = ScalableBitmap(this, "hotend_lost_badge", 16);
    m_nozzle_empty_badge_bmp   = ScalableBitmap(this, "hotend_empty_badge", 16);
    m_spinner_bmp              = ScalableBitmap(this, "sync_loading_arc", kLoadingDiam);
    Bind(wxEVT_PAINT, &HotendColumn::OnPaint, this);

    m_anim_timer.Bind(wxEVT_TIMER, [this](wxTimerEvent &) {
        m_spin_angle += 2 * M_PI * (33.0 / 800.0);
        if (m_spin_angle > 2 * M_PI) m_spin_angle -= 2 * M_PI;
        Refresh();
    });
}

// Run the rotation only while a read is in flight, so the card never animates
// for a read the printer has already finished.
void HotendColumn::UpdateAnimTimer()
{
    if (IsSpinning()) {
        if (!m_anim_timer.IsRunning()) m_anim_timer.Start(33);
    } else {
        m_anim_timer.Stop();
    }
}

void HotendColumn::UpdateEntry(const HotendEntry &entry)
{
    m_entry = entry;

    // Re-check the anim timer so the card stops as soon as the printer clears
    // its reading flag — the flag, not this poll, decides when the read ends.
    UpdateAnimTimer();
    Refresh();
}

void HotendColumn::OnPaint(wxPaintEvent &)
{
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(GetBackgroundColour()));
    dc.Clear();

    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
    if (!gc) return;
    gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);

    std::vector<wxColour> palette;
    for (const auto &hex : m_entry.filament_colors) {
        wxColour c = ResolveFilamentColor(hex);
        if (c.IsOk()) palette.push_back(c);
    }

    const int    w      = GetClientSize().GetWidth();
    const int    card_w = FromDIP(kColW);
    const int    card_x = (w - card_w) / 2;
    const int    radius = FromDIP(kCardRadius);
    const wxRect card_rect(card_x, 0, card_w, FromDIP(kColTotalH));

    const double border_w = FromDIP(1);
    gc->SetBrush(wxBrush(C(ThemeColor::Grey200)));
    gc->SetPen(wxPen(C(ThemeColor::Grey400), border_w));
    // Inset by half the stroke so the border sits fully inside the card bounds (top/bottom edges aren't clipped).
    gc->DrawRoundedRectangle(card_rect.x + border_w / 2.0, card_rect.y + border_w / 2.0,
                             card_rect.width - border_w, card_rect.height - border_w, radius);

    int y = card_rect.y;
    PaintHeader(gc.get(), wxRect(card_rect.x, y, card_rect.width, FromDIP(kHeaderH)));
    y += FromDIP(kHeaderH);

    PaintSpool(gc.get(), wxRect(card_rect.x, y, card_rect.width, FromDIP(kSpoolZoneH)), palette);
    y += FromDIP(kSpoolZoneH);

    // Separator between spool and hotend zones
    gc->SetPen(wxPen(C(ThemeColor::Grey350), 1));
    gc->StrokeLine(card_rect.x + FromDIP(4), y, card_rect.GetRight() - FromDIP(4), y);

    if (m_entry.is_hotend || m_entry.nozzle_state != NozzleCardState::Normal) PaintHotend(gc.get(), wxRect(card_rect.x, y, card_rect.width, FromDIP(kHotendZoneH)));
}

void HotendColumn::PaintHeader(wxGraphicsContext *gc, const wxRect &r)
{
    // Filled index pill, left-aligned. Pill: 14 DIP tall, Grey300 bg, bold text inside.
    wxFont f = *wxNORMAL_FONT;
    f.SetWeight(wxFONTWEIGHT_BOLD);
    f.SetPointSize(f.GetPointSize() - 1);
    gc->SetFont(f, C(ThemeColor::TextPrimary));

    wxString index = wxString::FromDouble(m_entry.index + 1);
    double   tw = 0, th = 0, descent = 0, leading = 0;
    gc->GetTextExtent(index, &tw, &th, &descent, &leading);

    const int    pad_x  = FromDIP(5);
    const int    pill_h = FromDIP(14);
    const int    pill_w = std::max((int) tw + 2 * pad_x, FromDIP(14));
    const int    pill_x = r.x + FromDIP(4);
    const int    pill_y = r.y + (r.height - pill_h) / 2;
    const double radius = pill_h / 2.0;
    gc->SetBrush(wxBrush(C(ThemeColor::Grey300)));
    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->DrawRoundedRectangle(pill_x, pill_y, pill_w, pill_h, radius);
    gc->DrawText(index, pill_x + (pill_w - tw) / 2.0, pill_y + (pill_h - th) / 2.0);

    // "Ext" tag pill after the index when the filament is fed from an external spool.
    if (m_entry.is_ext) {
        const wxString ext_txt = "Ext";
        double         ew = 0, eh = 0, ed = 0, el = 0;
        gc->GetTextExtent(ext_txt, &ew, &eh, &ed, &el);
        const int ext_w = (int) ew + 2 * pad_x;
        const int ext_x = pill_x + pill_w + FromDIP(4);
        gc->SetBrush(wxBrush(C(ThemeColor::Grey300)));
        gc->SetPen(*wxTRANSPARENT_PEN);
        gc->DrawRoundedRectangle(ext_x, pill_y, ext_w, pill_h, radius);
        gc->DrawText(ext_txt, ext_x + (ext_w - ew) / 2.0, pill_y + (pill_h - eh) / 2.0);
    }

    // Upper-right load indicator: filled brand-green dot when loaded,
    // filled dark-grey dot when empty.
    const double   dot_r = FromDIP(4);
    const double   dx    = r.GetRight() - FromDIP(12);
    const double   dy    = r.y + r.height / 2.0;
    const wxColour fill  = m_entry.filament_loaded ? C(ThemeColor::BrandGreen) : C(ThemeColor::Grey700);
    gc->SetBrush(wxBrush(fill));
    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->DrawEllipse(dx - dot_r, dy - dot_r, 2 * dot_r, 2 * dot_r);
}

void HotendColumn::PaintSpool(wxGraphicsContext *gc, const wxRect &r, const std::vector<wxColour> &palette)
{
    const int cx = r.x + r.width / 2;
    const int cy = r.y + r.height / 2;

    if (IsSpinning()) {
        const wxBitmap &bmp = m_spinner_bmp.bmp();
        const double    bw  = bmp.GetWidth();
        const double    bh  = bmp.GetHeight();
        gc->PushState();
        gc->Translate(cx, cy);
        gc->Rotate(m_spin_angle);
        gc->DrawBitmap(bmp, -bw / 2.0, -bh / 2.0, bw, bh);
        gc->PopState();
        return;
    }

    const int diam = FromDIP(kSpoolDiam);

    // The label under the disc doubles as the status line for non-normal nozzles.
    wxColour label_colour = C(ThemeColor::TextPrimary);
    wxString filament_name = m_entry.filament_name.empty() ? wxString("--") : wxString::FromUTF8(m_entry.filament_name);

    wxFont f = *wxNORMAL_FONT;
    f.SetPointSize(f.GetPointSize() - 1);
    gc->SetFont(f, label_colour);
    auto [name, tw, th] = MeasureNameTruncated(gc, filament_name, r.width - FromDIP(8));

    const int gap_disc_name = FromDIP(4);
    const int group_h       = diam + gap_disc_name + th;
    const int group_top     = r.y + (r.height - group_h) / 2;
    const int disc_cy       = group_top + diam / 2;

    PaintSpoolDisc(gc, cx, disc_cy, diam / 2, FromDIP(kSpoolHoleR), m_entry.filament_loaded, m_entry.filament_remain, m_entry.is_gradient, palette);
    PaintSpoolTag(gc, cx, disc_cy, diam / 2);

    gc->DrawText(name, r.x + (r.width - tw) / 2.0, group_top + diam + gap_disc_name);
}

void HotendColumn::PaintHotend(wxGraphicsContext *gc, const wxRect &r)
{
    // The hotend glyph is always this dialog's own icon; abnormal / unknown just
    // get an error badge overlaid on top (device-page nozzle icons are a
    // different style and are not reused here).
    const wxBitmap &nb = m_nozzle_bmp.bmp();
    if (!nb.IsOk()) return;

    // An empty rack position has no nozzle, so there is no diameter / flow to
    // show — collapse the two-line label to a single "--".
    const bool     show_details = (m_entry.nozzle_state == NozzleCardState::Normal) || (m_entry.nozzle_state == NozzleCardState::Lost);
    const bool     unknown      = (m_entry.nozzle_state == NozzleCardState::Unknown);
    const wxString line1        = show_details ? m_entry.nozzle_diameter : unknown ? _L("Unknown") : wxString("--");
    const wxString line2        = (show_details) ? m_entry.nozzle_flow_type : wxString();

    wxFont f = *wxNORMAL_FONT;
    f.SetPointSize(f.GetPointSize() - 1);
    gc->SetFont(f, C(ThemeColor::TextPrimary));
    double tw_1 = 0, tw_2 = 0, th = 0, descent = 0, leading = 0;
    gc->GetTextExtent(line1, &tw_1, &th, &descent, &leading);
    if (!line2.empty()) gc->GetTextExtent(line2, &tw_2, &th, &descent, &leading);
    const double label_w = std::max(tw_1, tw_2);
    const int    gap     = FromDIP(4);
    const double group_w = nb.GetWidth() + gap + label_w;
    const double group_x = r.x + (r.width - group_w) / 2.0;

    const double nozzle_y = r.y + (r.height - nb.GetHeight()) / 2.0;
    gc->DrawBitmap(nb, group_x, nozzle_y, nb.GetWidth(), nb.GetHeight());

    // Overlay the device-page status badge on the nozzle: red "!" when abnormal,
    // grey "?" when unknown, grey "!" when dropped, grey prohibit ring when empty.
    const ScalableBitmap *badge_bmp = nullptr;
    if (m_entry.nozzle_state == NozzleCardState::Abnormal)     badge_bmp = &m_nozzle_error_badge_bmp;
    else if (m_entry.nozzle_state == NozzleCardState::Unknown) badge_bmp = &m_nozzle_unknown_badge_bmp;
    else if (m_entry.nozzle_state == NozzleCardState::Lost)
        badge_bmp = &m_nozzle_lost_badge_bmp;
    else if (m_entry.nozzle_state == NozzleCardState::Empty)
        badge_bmp = &m_nozzle_empty_badge_bmp;
    if (badge_bmp) {
        const wxBitmap &badge = badge_bmp->bmp();
        if (badge.IsOk()) {
            const double bx = group_x + (nb.GetWidth() - badge.GetWidth()) / 2.0;
            const double by = nozzle_y + (nb.GetHeight() - badge.GetHeight()) / 2.0;
            gc->DrawBitmap(badge, bx, by, badge.GetWidth(), badge.GetHeight());
        }
    }

    const double label_x = group_x + nb.GetWidth() + gap;
    if (line2.empty()) {
        gc->DrawText(line1, label_x, r.y + (r.height - th) / 2.0);
    } else {
        const double stack_h   = th * 2 + FromDIP(2);
        const double stack_top = r.y + (r.height - stack_h) / 2.0;
        gc->DrawText(line1, label_x, stack_top);
        gc->DrawText(line2, label_x, stack_top + th + FromDIP(2));
    }
}

class SyncMachineInfoDialog : public DPIDialog
{
public:
    SyncMachineInfoDialog(wxWindow *parent, MachineObject *obj, const std::vector<HotendEntry> &entries);
    void on_dpi_changed(const wxRect &) override {}

private:
    void BuildLayout(const std::vector<HotendEntry> &entries);
    void BuildCardGrid(const std::vector<HotendEntry> &entries, wxBoxSizer *root);
    void BuildButtonBar(wxBoxSizer *root);
    void OnRefreshClicked();
    void OnPollTick();
    void UpdateRefreshButtonLabel();

    MachineObject *m_obj{nullptr};

    Button *m_confirm_btn{nullptr};
    Button *m_refresh_btn{nullptr};

    // Every card on the dialog
    std::vector<HotendColumn *> m_all_columns;

    // The button stays busy this long after a click, so even an instant printer
    // answer reads as a taken click. A read still in flight holds it past that,
    // the way the AMS panel's own refresh button behaves.
    static constexpr int                  kRefreshHoldMs = 2000;
    std::chrono::steady_clock::time_point m_refresh_hold_until{};

    wxTimer m_poll_timer;
    bool    m_refreshing{false};
};

SyncMachineInfoDialog::SyncMachineInfoDialog(wxWindow *parent, MachineObject *obj, const std::vector<HotendEntry> &entries)
    : DPIDialog(parent, wxID_ANY, _L("Sync printer information"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE), m_obj(obj)
{
    SetBackgroundColour(C(ThemeColor::White));
    BuildLayout(entries);
    CenterOnParent();
    wxGetApp().UpdateDlgDarkUI(this);

    m_poll_timer.Bind(wxEVT_TIMER, [this](wxTimerEvent &) { OnPollTick(); });
    m_poll_timer.Start(100);
}

// Refresh button: ask the printer to re-read the filament in every AMS unit that
// feeds the rack. One command per unit — the printer sweeps that unit's slots by
// itself and reports each one as it is read, which is what spins the cards.
void SyncMachineInfoDialog::OnRefreshClicked()
{
    if (m_refreshing || !m_obj) return;

    auto fila_sys = m_obj->GetFilaSystem();
    if (!fila_sys) return;

    m_refreshing         = true;
    m_refresh_hold_until = std::chrono::steady_clock::now() + std::chrono::milliseconds(kRefreshHoldMs);

    for (const auto &[_, ams_unit] : fila_sys->GetAmsList()) {
        if (ams_unit && ams_unit->GetAmsType() == DevAmsType::ZMULTI_B) m_obj->command_ams_read_all(ams_unit->GetAmdIdInt(), 1);
    }

    UpdateRefreshButtonLabel();
}

void SyncMachineInfoDialog::UpdateRefreshButtonLabel()
{
    m_refresh_btn->SetLabel(m_refreshing ? _L("Refreshing") : _L("Refresh"));
    m_refresh_btn->Enable(!m_refreshing);
}

// Live poll: re-walk the device model, push fresh entries onto each card
void SyncMachineInfoDialog::OnPollTick()
{
    std::vector<HotendEntry> fresh = CollectHotendEntries(m_obj);

    for (size_t i = 0; i < fresh.size() && i < m_all_columns.size(); ++i) {
        auto *col = m_all_columns[i];
        col->UpdateEntry(fresh[i]);
    }

    if (!m_refreshing) return;

    // The printer owns the end of a read, so the button stays busy while any card
    // spins — plus the click window, which covers the printer's answer arriving.
    const bool any_spinning = std::any_of(m_all_columns.begin(), m_all_columns.end(), [](HotendColumn *c) { return c && c->IsSpinning(); });
    const bool holding      = std::chrono::steady_clock::now() < m_refresh_hold_until;
    if (any_spinning || holding) return;

    m_refreshing = false;
    UpdateRefreshButtonLabel();
}

void SyncMachineInfoDialog::BuildLayout(const std::vector<HotendEntry> &entries)
{
    auto *root = new wxBoxSizer(wxVERTICAL);
    root->AddSpacer(FromDIP(16));

    BuildCardGrid(entries, root);
    BuildButtonBar(root);

    SetSizer(root);
    root->Fit(this);
    Layout();
}

void SyncMachineInfoDialog::BuildCardGrid(const std::vector<HotendEntry> &hotends, wxBoxSizer *root)
{
    m_all_columns.clear();
    m_all_columns.reserve(hotends.size());

    for (const auto &hotend : hotends) m_all_columns.push_back(new HotendColumn(this, hotend));

    // nozzle layout
    //  2, 4, 6
    // 1, 3, 5, 7
    auto add_row = [&](const std::vector<size_t> &indices) {
        auto *row = new wxBoxSizer(wxHORIZONTAL);
        row->AddStretchSpacer();
        for (size_t i : indices) row->Add(m_all_columns[i], 0, wxLEFT | wxRIGHT, FromDIP(6));
        row->AddStretchSpacer();
        root->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(16));
    };

    std::vector<size_t> evens, odds;
    for (size_t i = 0; i < hotends.size(); ++i) {
        if (hotends[i].index % 2 == 0)
            evens.push_back(i);
        else
            odds.push_back(i);
    }

    if (!odds.empty()) add_row(odds);
    if (!evens.empty() && !odds.empty()) root->AddSpacer(FromDIP(12));
    if (!evens.empty()) add_row(evens);

    root->AddSpacer(FromDIP(16));
}

void SyncMachineInfoDialog::BuildButtonBar(wxBoxSizer *root)
{
    StateColor refresh_bg(std::pair<wxColour, int>(C(ThemeColor::Grey250), StateColor::Disabled), std::pair<wxColour, int>(C(ThemeColor::Grey350), StateColor::Pressed),
                          std::pair<wxColour, int>(C(ThemeColor::Grey300), StateColor::Hovered), std::pair<wxColour, int>(C(ThemeColor::White), StateColor::Normal));
    StateColor refresh_border(std::pair<wxColour, int>(C(ThemeColor::Grey400), StateColor::Disabled), std::pair<wxColour, int>(C(ThemeColor::Grey400), StateColor::Normal));
    StateColor refresh_text(std::pair<wxColour, int>(C(ThemeColor::TextPrimary), StateColor::Normal));

    m_refresh_btn   = new Button(this, _L("Refreshing"), "", 0, 0, wxID_ANY);
    const wxSize bw = m_refresh_btn->GetBestSize();
    m_refresh_btn->SetMinSize(wxSize(std::max(bw.GetWidth(), FromDIP(70)), FromDIP(24)));
    m_refresh_btn->SetLabel(_L("Refresh"));
    m_refresh_btn->SetCornerRadius(FromDIP(12));
    m_refresh_btn->SetBackgroundColor(refresh_bg);
    m_refresh_btn->SetBorderColor(refresh_border);
    m_refresh_btn->SetTextColor(refresh_text);
    m_refresh_btn->Bind(wxEVT_LEFT_DOWN, [this](auto &) { OnRefreshClicked(); });

    StateColor confirm_bg(std::pair<wxColour, int>(C(ThemeColor::Grey500), StateColor::Disabled), std::pair<wxColour, int>(C(ThemeColor::BrandGreenPressed), StateColor::Pressed),
                          std::pair<wxColour, int>(C(ThemeColor::BrandGreenHovered), StateColor::Hovered),
                          std::pair<wxColour, int>(C(ThemeColor::BrandGreen), StateColor::Normal));
    // Solid primary button: border tracks the fill across states so the edge never shows through.
    StateColor confirm_border(std::pair<wxColour, int>(C(ThemeColor::Grey500), StateColor::Disabled),
                              std::pair<wxColour, int>(C(ThemeColor::BrandGreenPressed), StateColor::Pressed),
                              std::pair<wxColour, int>(C(ThemeColor::BrandGreenHovered), StateColor::Hovered),
                              std::pair<wxColour, int>(C(ThemeColor::BrandGreen), StateColor::Normal));
    StateColor confirm_text(std::pair<wxColour, int>(ThemeColor::WhiteFixed, StateColor::Normal));

    m_confirm_btn = new Button(this, _L("Confirm"), "", 0, 0, wxID_ANY);
    m_confirm_btn->SetMinSize(wxSize(FromDIP(70), FromDIP(24)));
    m_confirm_btn->SetCornerRadius(FromDIP(12));
    m_confirm_btn->SetBackgroundColor(confirm_bg);
    m_confirm_btn->SetBorderColor(confirm_border);
    m_confirm_btn->SetTextColor(confirm_text);
    m_confirm_btn->SetFocus();
    m_confirm_btn->Bind(wxEVT_LEFT_DOWN, [this](auto &) { EndModal(wxID_OK); });

    auto *button_sizer = new wxBoxSizer(wxHORIZONTAL);
    button_sizer->AddStretchSpacer();
    button_sizer->Add(m_refresh_btn, 0, wxALL, FromDIP(10));
    button_sizer->Add(m_confirm_btn, 0, wxALL, FromDIP(10));
    root->Add(button_sizer, 0, wxEXPAND);
}

bool tryPopUpSyncMachineInfoDialog(MachineObject *obj)
{
    if (!obj) return true;

    std::vector<HotendEntry> entries = CollectHotendEntries(obj);
    if (entries.empty()) return true;

#if 0
    // clang-format off
    entries = {
        {1, false, {}, false, "", 100, "0.4 mm", _L("Standard"), false, false, NozzleCardState::Empty}, // empty / 空 (bug) — keeps any_empty true

        // single colour: opaque / transparent / translucent
        {2, true, {"#FF6B00"},   false, "Single",      100, "0.4 mm", _L("Standard"), false, true, NozzleCardState::Normal}, // opaque
        {3, true, {"#00000000"}, false, "Transparent", 100, "0.4 mm", _L("Standard"), false, true, NozzleCardState::Empty}, // α=00 → checkerboard (透明)
        {4, true, {"#FF6B0080"}, false, "Translucent", 100, "0.4 mm", _L("Standard"), false, true, NozzleCardState::Abnormal}, // α≈50% → tinted checker (半透明)

        // multicolour (拼色): concentric bands, colors[0] innermost
        {5, true, {"#FF6B00", "#2E7CF6", "#00AE42", "#E1007F"}, false, "Multi x4", 100, "0.6 mm", _L("Standard"), false, true, NozzleCardState::Unknown},          // 4 bands
        {6, true, {"#FF6B00", "#2E7CF6", "#00AE42", "#E1007F", "#FFD400"}, false, "Multi x5", 40, "0.6 mm", _L("Standard"), false, true, NozzleCardState::Normal}, // N>4 + partial remain
        {7, true, {"#FF6B0080", "#2E7CF680"}, false, "Multi clear", 100, "0.6 mm", _L("Standard"), false, true, NozzleCardState::Lost},                          // translucent bands (拼色+透明)

        // gradient: smooth radial blend, hub → rim
        {8, true, {"#FF0080", "#FFD400", "#00AE42"}, true, "Gradient x3", 100, "0.4 mm", _L("Standard"), false, true, NozzleCardState::Normal}, // 3 stops
        {9, true, {"#FF008080", "#FFD40080"}, true, "Gradient clear", 60, "0.4 mm", _L("Standard"), false, true, NozzleCardState::Normal},      // translucent + partial remain
    };
    // clang-format on
#endif

    const bool any_empty = std::any_of(entries.begin(), entries.end(), [](const HotendEntry &e) { return !e.filament_loaded; });
    if (!any_empty) return true;

    SyncMachineInfoDialog dlg(wxGetApp().plater_, obj, entries);
    return dlg.ShowModal() == wxID_OK;
}

} // namespace Slic3r::GUI
