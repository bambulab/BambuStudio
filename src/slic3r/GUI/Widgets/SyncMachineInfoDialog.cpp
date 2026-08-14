#include "SyncMachineInfoDialog.hpp"

#include "Button.hpp"
#include "StateColor.hpp"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevNozzleRack.h"
#include "slic3r/GUI/DeviceCore/DevNozzleSystem.h"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_Utils.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/wxExtensions.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include <wx/dcbuffer.h>
#include <wx/gdicmn.h>
#include <wx/graphics.h>
#include <wx/sizer.h>
#include <wx/string.h>
#include <wx/timer.h>

namespace Slic3r ::GUI {

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
    bool                     syncing{false};       // drives the rotating arc in the card header
    bool                     is_hotend{true};
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

// Resolve a HotendEntry from the live device model
HotendEntry make_hotend(MachineObject *obj, const DevNozzle &n)
{
    HotendEntry e;
    e.index            = nozzle_id(n);
    e.filament_loaded  = n.HasFilamentInNozzle();
    e.nozzle_diameter  = n.GetNozzleDiameterStr();
    e.nozzle_flow_type = (n.GetNozzleFlowType() == NozzleFlowType::H_FLOW) ? _L("High Flow") : _L("Standard");
    e.is_hotend        = n.IsHotend();

    const auto &slots = n.GetBindAmsSlots();
    if (!slots.empty() && obj) {
        auto tray_opt = obj->get_tray(slots[0].first, slots[0].second);
        if (tray_opt.has_value()) {
            const auto &tray = *tray_opt;
            if (!tray.sub_brands.empty())
                e.filament_name = tray.sub_brands;
            else if (tray.m_fila_type == "PLA-S")
                e.filament_name = "Sup.PLA";
            else if (tray.m_fila_type == "PA-S")
                e.filament_name = "Sup.PA";
            else if (tray.m_fila_type == "ABS-S")
                e.filament_name = "Sup.ABS";
            else
                e.filament_name = tray.m_fila_type;
            e.filament_remain = std::clamp(tray.remain, 1, 100); // floor at 1 so an empty-but-loaded spool still draws a sliver
            if (!tray.cols.empty()) {
                e.filament_colors = tray.cols;
                // Enum names lie: CTYPE_MULTI renders as a blended linear gradient
                // (see AMSMaterialsSetting.cpp:1976) while CTYPE_GRADIANT renders
                // as pie wedges. We want the gradient flag set on the blended one.
                e.is_gradient = (tray.ctype == DevFilaColorType::CTYPE_MULTI);
            }
            e.syncing = tray.is_reading(obj->tray_reading_bits);
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

    // rack type B cant't auto read rfid
    auto rack = nozzle_sys->GetNozzleRack();
    if (!rack || rack->GetRackType() != DevNozzleRack::RackType_B) return entries;

    std::map<int, const DevNozzle *> nozzles_by_id;
    for (const auto &kv : nozzle_sys->GetRackNozzles()) nozzles_by_id[nozzle_id(kv.second)] = &kv.second;
    for (const auto &kv : nozzle_sys->GetExtNozzles()) nozzles_by_id.emplace(nozzle_id(kv.second), &kv.second);

    entries.resize(nozzles_by_id.size());
    for (const auto &[id, np] : nozzles_by_id) { entries[id] = make_hotend(obj, *np); }
    return entries;
}

wxColour ResolveFilamentColor(const std::string &hex_in)
{
    if (hex_in.empty()) return C(ThemeColor::Grey400);

    wxString hex = wxString::FromUTF8(hex_in);
    if (!hex.IsEmpty() && hex[0] != '#') hex.Prepend("#");
    if (hex.CmpNoCase(kColorFallbackSentinel) == 0) return C(ThemeColor::Grey400);

    wxColour parsed;
    if (!parsed.Set(hex)) return C(ThemeColor::Grey400);

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

// Spool ring renderer
void PaintSpoolDisc(wxGraphicsContext *gc, int cx, int cy, int outer_r, int hole_r, bool loaded, int remain, bool is_gradient, const std::vector<wxColour> &colors)
{
    const double ring_r = outer_r * (double(kSpoolRingR) / double(kSpoolDiam / 2)); // 23/26 of outer

    // Outer disc — fill + 1px edge
    gc->SetBrush(wxBrush(C(ThemeColor::Grey350)));
    gc->SetPen(wxPen(C(ThemeColor::Grey400), 1));
    gc->DrawEllipse(cx - outer_r, cy - outer_r, 2 * outer_r, 2 * outer_r);

    gc->SetPen(*wxTRANSPARENT_PEN);
    if (colors.size() > 1 && is_gradient) {
        // Simple linear gradient across the disc (top-left → bottom-right)
        wxGraphicsGradientStops stops(colors.front(), colors.back());
        for (size_t i = 1; i + 1 < colors.size(); ++i) stops.Add(colors[i], double(i) / (colors.size() - 1));
        const double d = ring_r * 0.70711; // ring_r / sqrt(2)
        gc->SetBrush(gc->CreateLinearGradientBrush(cx - d, cy - d, cx + d, cy + d, stops));
        gc->DrawEllipse(cx - ring_r, cy - ring_r, 2 * ring_r, 2 * ring_r);
    } else if (colors.size() > 1) {
        // Segmented multi
        gc->SetBrush(*wxTRANSPARENT_BRUSH);
        const double ring_w = (ring_r - hole_r) / colors.size();
        for (size_t i = 0; i < colors.size(); ++i) {
            const double r = ring_r - i * ring_w - ring_w / 2.0;
            gc->SetPen(wxPen(colors[i], std::max<int>(1, (int) std::lround(ring_w))));
            gc->DrawEllipse(cx - r, cy - r, 2 * r, 2 * r);
            gc->SetPen(*wxTRANSPARENT_PEN);
        }
    } else if (remain >= 100) {
        gc->SetBrush(wxBrush(colors[0]));
        gc->DrawEllipse(cx - ring_r, cy - ring_r, 2 * ring_r, 2 * ring_r);
    } else {
        const double start = -M_PI / 2;
        const double end   = start + (remain / 100.0) * 2 * M_PI;
        gc->SetBrush(wxBrush(colors[0]));
        gc->FillPath(DonutWedge(gc, cx, cy, ring_r, hole_r, start, end));
    }

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
//   |       ●    ●       |              when syncing: rotated refresh_nozzle.svg fills this zone
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
    bool IsSpinning() const { return m_spinning; }
    bool IsFilamentLoaded() const { return m_entry.filament_loaded; }

    // Spin starts on user click and runs for kAutoStopMs unless StopSpin() is
    // called sooner (poll observed filament_loaded or dialog tearing down).
    void StartSpin();
    void StopSpin();

private:
    void OnPaint(wxPaintEvent &event);

    void PaintHeader(wxGraphicsContext *gc, const wxRect &r);
    void PaintSpool(wxGraphicsContext *gc, const wxRect &r, const std::vector<wxColour> &palette);
    void PaintHotend(wxGraphicsContext *gc, const wxRect &r);

    static constexpr int kAutoStopMs = 2000;

    HotendEntry    m_entry;
    bool           m_spinning{false};
    double         m_spin_angle{0.0}; // radians, advanced while spinning
    wxTimer        m_anim_timer;      // 33ms tick that advances m_spin_angle
    wxTimer        m_auto_stop_timer; // single-shot 2s auto-clear
    ScalableBitmap m_nozzle_bmp;
    ScalableBitmap m_spinner_bmp; // sync_loading_arc.svg, rotated by m_spin_angle while spinning
};

HotendColumn::HotendColumn(wxWindow *parent, const HotendEntry &entry) : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE), m_entry(entry)
{
    SetBackgroundColour(parent->GetBackgroundColour());
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    // Without a min size the panel collapses to 0 height in the sizer
    SetMinSize(wxSize(FromDIP(kColW), FromDIP(kColTotalH)));

    m_nozzle_bmp        = ScalableBitmap(this, "dev_rack_nozzle_print_job", 24);
    m_spinner_bmp       = ScalableBitmap(this, "sync_loading_arc", kLoadingDiam);
    Bind(wxEVT_PAINT, &HotendColumn::OnPaint, this);

    m_anim_timer.Bind(wxEVT_TIMER, [this](wxTimerEvent &) {
        m_spin_angle += 2 * M_PI * (33.0 / 800.0);
        if (m_spin_angle > 2 * M_PI) m_spin_angle -= 2 * M_PI;
        Refresh();
    });
    m_auto_stop_timer.Bind(wxEVT_TIMER, [this](wxTimerEvent &) { StopSpin(); });
}

void HotendColumn::StartSpin()
{
    m_spinning = true;
    if (!m_anim_timer.IsRunning()) m_anim_timer.Start(33);
    m_auto_stop_timer.Start(kAutoStopMs, wxTIMER_ONE_SHOT);
    Refresh();
}

void HotendColumn::StopSpin()
{
    if (!m_spinning) return;
    m_spinning = false;
    m_anim_timer.Stop();
    m_auto_stop_timer.Stop();
    Refresh();
}

void HotendColumn::UpdateEntry(const HotendEntry &entry)
{
    m_entry = entry;
    if (m_entry.filament_loaded) StopSpin();

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

    gc->SetBrush(wxBrush(C(ThemeColor::Grey200)));
    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->DrawRoundedRectangle(card_rect.x, card_rect.y, card_rect.width, card_rect.height, radius);

    int y = card_rect.y;
    PaintHeader(gc.get(), wxRect(card_rect.x, y, card_rect.width, FromDIP(kHeaderH)));
    y += FromDIP(kHeaderH);

        PaintSpool(gc.get(), wxRect(card_rect.x, y, card_rect.width, FromDIP(kSpoolZoneH)), palette);
        y += FromDIP(kSpoolZoneH);

        // Separator between spool and hotend zones
        gc->SetPen(wxPen(C(ThemeColor::Grey350), 1));
        gc->StrokeLine(card_rect.x + FromDIP(4), y, card_rect.GetRight() - FromDIP(4), y);

        if (m_entry.is_hotend) PaintHotend(gc.get(), wxRect(card_rect.x, y, card_rect.width, FromDIP(kHotendZoneH)));
}

void HotendColumn::PaintHeader(wxGraphicsContext *gc, const wxRect &r)
{
    // Filled index pill, left-aligned. Pill: 14 DIP tall, Grey300 bg, bold text inside.
    wxFont f = *wxNORMAL_FONT;
    f.SetWeight(wxFONTWEIGHT_BOLD);
    f.SetPointSize(f.GetPointSize() - 1);
    gc->SetFont(f, C(ThemeColor::TextPrimary));

    wxString index = wxString::FromDouble(m_entry.index + 1);
    double tw = 0, th = 0, descent = 0, leading = 0;
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

    wxFont f = *wxNORMAL_FONT;
    f.SetPointSize(f.GetPointSize() - 1);
    gc->SetFont(f, C(ThemeColor::TextPrimary));
    const wxString raw_name = m_entry.filament_name.empty() ? wxString("--") : wxString::FromUTF8(m_entry.filament_name);
    auto [name, tw, th]     = MeasureNameTruncated(gc, raw_name, r.width - FromDIP(8));

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
    const wxBitmap &nb = m_nozzle_bmp.bmp();
    if (!nb.IsOk()) return;

    wxFont f = *wxNORMAL_FONT;
    f.SetPointSize(f.GetPointSize() - 1);
    gc->SetFont(f, C(ThemeColor::TextPrimary));
    double tw_d = 0, tw_f = 0, th = 0, descent = 0, leading = 0;
    gc->GetTextExtent(m_entry.nozzle_diameter, &tw_d, &th, &descent, &leading);
    gc->GetTextExtent(m_entry.nozzle_flow_type, &tw_f, &th, &descent, &leading);
    const double label_w = std::max(tw_d, tw_f);
    const int    gap     = FromDIP(4);
    const double group_w = nb.GetWidth() + gap + label_w;
    const double group_x = r.x + (r.width - group_w) / 2.0;

    gc->DrawBitmap(nb, group_x, r.y + (r.height - nb.GetHeight()) / 2.0, nb.GetWidth(), nb.GetHeight());

    const double label_x   = group_x + nb.GetWidth() + gap;
    const double stack_h   = th * 2 + FromDIP(2);
    const double stack_top = r.y + (r.height - stack_h) / 2.0;
    gc->DrawText(m_entry.nozzle_diameter, label_x, stack_top);
    gc->DrawText(m_entry.nozzle_flow_type, label_x, stack_top + th + FromDIP(2));
}

class SyncMachineInfoDialog : public DPIDialog
{
public:
    SyncMachineInfoDialog(wxWindow *parent, MachineObject *obj, const std::vector<HotendEntry> &entries, std::vector<std::vector<DevAmsSlotId>> unloaded_groups);
    void on_dpi_changed(const wxRect &) override {}

private:
    void BuildLayout(const std::vector<HotendEntry> &entries);
    void BuildCardGrid(const std::vector<HotendEntry> &entries, wxBoxSizer *root);
    void BuildButtonBar(wxBoxSizer *root);
    void OnRefreshClicked();
    void OnPollTick();
    void UpdateRefreshButtonLabel();

    MachineObject *m_obj{nullptr};

    std::vector<std::vector<DevAmsSlotId>> m_unloaded_groups;

    Button *m_confirm_btn{nullptr};
    Button *m_refresh_btn{nullptr};

    // Every card on the dialog
    std::vector<HotendColumn *> m_all_columns;

    wxTimer m_poll_timer;
    bool    m_refreshing{false};
};

SyncMachineInfoDialog::SyncMachineInfoDialog(wxWindow *parent, MachineObject *obj, const std::vector<HotendEntry> &entries, std::vector<std::vector<DevAmsSlotId>> unloaded_groups)
    : DPIDialog(parent, wxID_ANY, _L("Sync printer information"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
    , m_obj(obj)
    , m_unloaded_groups(std::move(unloaded_groups))
{
    SetBackgroundColour(C(ThemeColor::White));
    BuildLayout(entries);
    CenterOnParent();
    wxGetApp().UpdateDlgDarkUI(this);

    m_poll_timer.Bind(wxEVT_TIMER, [this](wxTimerEvent &) { OnPollTick(); });
    m_poll_timer.Start(1500);
}

// Refresh button: fire an RFID-read for every unloaded slot at once
void SyncMachineInfoDialog::OnRefreshClicked()
{
    if (m_refreshing || m_unloaded_groups.empty()) return;

    m_refreshing = true;
    for (auto *col : m_all_columns)
        if (col && !col->IsFilamentLoaded()) col->StartSpin();

    for (const auto &group : m_unloaded_groups)
        for (const auto &[ams_id, slot_id] : group) m_obj->command_ams_refresh_rfid2(ams_id, slot_id);

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

    const bool any_spinning = std::any_of(m_all_columns.begin(), m_all_columns.end(), [](HotendColumn *c) { return c && c->IsSpinning(); });
    if (!any_spinning) {
        m_refreshing = false;
        UpdateRefreshButtonLabel();
    }
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
    const wxColour kBrandGreenPressed{27, 136, 68};
    const wxColour kBrandGreenHovered{61, 203, 115};

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
    if (m_unloaded_groups.empty()) m_refresh_btn->Enable(false);
    m_refresh_btn->Bind(wxEVT_LEFT_DOWN, [this](auto &) { OnRefreshClicked(); });

    StateColor confirm_bg(std::pair<wxColour, int>(C(ThemeColor::Grey500), StateColor::Disabled), std::pair<wxColour, int>(kBrandGreenPressed, StateColor::Pressed),
                          std::pair<wxColour, int>(kBrandGreenHovered, StateColor::Hovered), std::pair<wxColour, int>(C(ThemeColor::BrandGreen), StateColor::Normal));
    // Solid primary button: border tracks the fill across states so the edge never shows through.
    StateColor confirm_border(std::pair<wxColour, int>(C(ThemeColor::Grey500), StateColor::Disabled), std::pair<wxColour, int>(kBrandGreenPressed, StateColor::Pressed),
                              std::pair<wxColour, int>(kBrandGreenHovered, StateColor::Hovered), std::pair<wxColour, int>(C(ThemeColor::BrandGreen), StateColor::Normal));
    StateColor confirm_text(std::pair<wxColour, int>(C(ThemeColor::White), StateColor::Normal));

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

// Collect the (ams_id, slot_id) pairs of every unloaded hotend, grouped by nozzle
std::vector<std::vector<DevAmsSlotId>> CollectUnloadedSlotGroups(MachineObject *obj)
{
    if (!obj) return {};
    DevNozzleSystem *nozzle_sys = obj->GetNozzleSystem();
    if (!nozzle_sys) return {};

    std::map<int, const DevNozzle *> nozzles_by_id;
    for (const auto &[_, nozzle] : nozzle_sys->GetRackNozzles()) nozzles_by_id[nozzle_id(nozzle)] = &nozzle;
    for (const auto &[_, nozzle] : nozzle_sys->GetExtNozzles()) nozzles_by_id.emplace(nozzle_id(nozzle), &nozzle);

    std::vector<std::vector<DevAmsSlotId>> groups;
    for (const auto &[_, nozzle] : nozzles_by_id) {
        if (!nozzle->IsHotend() || nozzle->HasFilamentInNozzle()) continue;
        std::vector<DevAmsSlotId> group;
        for (const auto &slot : nozzle->GetBindAmsSlots()) group.push_back(slot);
        groups.push_back(std::move(group));
    }

    return groups;
}

bool tryPopUpSyncMachineInfoDialog(MachineObject *obj)
{
    if (!obj) return true;

    std::vector<HotendEntry> entries = CollectHotendEntries(obj);
    if (entries.empty()) return true;

    std::vector<std::vector<DevAmsSlotId>> unloaded_groups = CollectUnloadedSlotGroups(obj);

#if 0
    // test cases
    entries = {
        {wxString("1"), true, {"#FF6B00"}, false, "PLA Basic", 100, "0.4 mm", _L("Standard"), false},
        {wxString("2"), true, {"#2E7CF6"}, false, "PETG-HF", 75, "0.4 mm", _L("High Flow"), true},
        {wxString("3"), true, {"#E1007F"}, false, "PolyLite PETG-HF", 25, "0.6 mm", _L("Standard"), false},
        {wxString("4"), true, {"#FF6B00", "#2E7CF6", "#00AE42", "#E1007F"}, false, "Multi-color", 100, "0.4 mm", _L("Standard"), false},
        {wxString("5"), true, {"#FF0080", "#FFD400"}, true, "Gradient", 100, "0.4 mm", _L("Standard"), false},
        {wxString("6"), true, {"#D9D9D9"}, false, "Unknown", 5, "0.2 mm", _L("Standard"), true},
        {wxString("7"), false, {"#6B6B6B"}, false, "", 100, "0.8 mm", _L("High Flow"), false},
    };
#endif

    const bool any_empty = std::any_of(entries.begin(), entries.end(), [](const HotendEntry &e) { return !e.filament_loaded; });
    if (!any_empty) return true;

    SyncMachineInfoDialog dlg(wxGetApp().plater_, obj, entries, std::move(unloaded_groups));
    return dlg.ShowModal() == wxID_OK;
}

} // namespace Slic3r::GUI
