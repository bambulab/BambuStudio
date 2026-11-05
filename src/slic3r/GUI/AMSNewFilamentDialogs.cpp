#include "AMSNewFilamentDialogs.hpp"

#include "GUI_App.hpp"
#include "I18N.hpp"
#include "MsgDialog.hpp"
#include "EncodedFilament.hpp"
#include "FilamentBitmapUtils.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/ComboBox.hpp"

#include "DeviceCore/DevFilaSystem.h"
#include "DeviceCore/DevManager.h"
#include "fila_manager/wgtFilaManagerStore.h"

#include <wx/dcmemory.h>
#include <wx/dcgraph.h>
#include <wx/graphics.h>
#include <wx/statbmp.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include "libslic3r/Log.hpp"

namespace Slic3r { namespace GUI {

// colors[] with precedence over single color_code.
static std::vector<wxColour> _spool_colors(const Slic3r::GUI::FilamentSpool& sp)
{
    std::vector<wxColour> out;
    if (!sp.colors.empty()) {
        out.reserve(sp.colors.size());
        for (const std::string& hex : sp.colors)
            out.push_back(parse_hex_color(hex));
        return out;
    }
    if (!sp.color_code.empty())
        out.push_back(parse_hex_color(sp.color_code));
    return out;
}

static wxBitmap _make_spool_color_chip(wxWindow* ctx, const Slic3r::GUI::FilamentSpool& sp)
{
    const int size_px = ctx ? ctx->FromDIP(36) : 36;
    const double radius = std::max(2.0, std::round(size_px / 6.0));

    wxBitmap bmp(size_px, size_px);
#if defined(__WXMSW__) || defined(__WXOSX__)
    bmp.UseAlpha();
#endif
    wxMemoryDC dc(bmp);
    dc.SetBackground(*wxTRANSPARENT_BRUSH);
    dc.Clear();

    wxGraphicsContext* gc = wxGraphicsContext::Create(dc);
    if (!gc) { dc.SelectObject(wxNullBitmap); return bmp; }
    gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);

    wxGraphicsPath chip_path = gc->CreatePath();
    chip_path.AddRoundedRectangle(0, 0, size_px, size_px, radius);

    const auto colors = _spool_colors(sp);
    const bool is_multicolor = (sp.color_type == 1) && colors.size() > 1;
    const bool is_gradient   = (sp.color_type == 0) && colors.size() > 1;

    if (is_multicolor) {
        const int n = static_cast<int>(colors.size());
        const int base_strip = size_px / n;
        int x = 0;
        for (int i = 0; i < n; ++i) {
            const int w = (i == n - 1) ? (size_px - base_strip * (n - 1)) : base_strip;
            gc->PushState();
            gc->Clip(x, 0, w, size_px);
            gc->SetPen(*wxTRANSPARENT_PEN);
            gc->SetBrush(wxBrush(colors[i]));
            gc->FillPath(chip_path);
            gc->ResetClip();
            gc->PopState();
            x += w;
        }
    } else if (is_gradient) {
        const int n = static_cast<int>(colors.size());
        if (n == 2) {
            gc->PushState();
            gc->Clip(0, 0, size_px, size_px);
            wxGraphicsBrush gb = gc->CreateLinearGradientBrush(0, 0, size_px, 0, colors[0], colors[1]);
            gc->SetBrush(gb);
            gc->SetPen(*wxTRANSPARENT_PEN);
            gc->FillPath(chip_path);
            gc->ResetClip();
            gc->PopState();
        } else {
            const double seg_w = static_cast<double>(size_px) / (n - 1);
            for (int i = 0; i < n - 1; ++i) {
                const double x0 = i * seg_w;
                const double x1 = (i == n - 2) ? size_px : (i + 1) * seg_w;
                gc->PushState();
                gc->Clip(x0, 0, x1 - x0, size_px);
                wxGraphicsBrush gb = gc->CreateLinearGradientBrush(x0, 0, x1, 0, colors[i], colors[i + 1]);
                gc->SetBrush(gb);
                gc->SetPen(*wxTRANSPARENT_PEN);
                gc->FillPath(chip_path);
                gc->ResetClip();
                gc->PopState();
            }
        }
    } else {
        const wxColour fill = colors.empty() ? wxColour(0x88, 0x88, 0x88) : colors.front();
        gc->SetBrush(wxBrush(fill));
        gc->SetPen(*wxTRANSPARENT_PEN);
        gc->FillPath(chip_path);
    }

    // Inset 1px ring (stroked on slightly inset path so it stays inside).
    const bool dark = wxGetApp().dark_mode();
    const wxColour ring_color = dark ? wxColour(255, 255, 255, 26) : wxColour(0, 0, 0, 46);
    wxGraphicsPath ring_path = gc->CreatePath();
    ring_path.AddRoundedRectangle(0.5, 0.5, size_px - 1.0, size_px - 1.0, radius);
    gc->SetBrush(*wxTRANSPARENT_BRUSH);
    gc->SetPen(wxPen(ring_color, 1));
    gc->StrokePath(ring_path);

    delete gc;
    dc.SelectObject(wxNullBitmap);
    return bmp;
}

static wxString _spool_note_line(const Slic3r::GUI::FilamentSpool& sp)
{
    wxString note = sp.note.empty() ? "--" : wxString::FromUTF8(sp.note);
    constexpr size_t kMaxNote = 30;
    if (note.length() > kMaxNote)
        note = note.SubString(0, kMaxNote - 3) + "...";
    return _L("Remark") + ": " + note;
}

static wxBitmap _render_spool_row_bitmap(wxWindow* ctx,
                                         const Slic3r::GUI::FilamentSpool& sp,
                                         int width_px,
                                         bool dimmed = false,
                                         bool* note_truncated = nullptr)
{
    const int pad_x     = ctx->FromDIP(8);
    const int pad_y     = ctx->FromDIP(6);
    const int icon_px   = ctx->FromDIP(36);
    const int gap       = ctx->FromDIP(8);
    const int row_h     = ctx->FromDIP(52);
    const int line_h    = ctx->FromDIP(20);
    const int right_pad = ctx->FromDIP(12);

    const int total_w = std::max(width_px, ctx->FromDIP(260));
    const int total_h = row_h;

    wxBitmap bmp(total_w, total_h);
#ifdef __WXOSX__
    bmp.UseAlpha();
#endif
    wxMemoryDC dc(bmp);
    const bool dark_row = wxGetApp().dark_mode();
    dc.SetBackground(wxBrush(dark_row ? wxColour(0x2D, 0x2D, 0x31) : *wxWHITE));
    dc.Clear();
    dc.SetPen(*wxTRANSPARENT_PEN);

    wxBitmap icon = _make_spool_color_chip(ctx, sp);
    if (icon.IsOk())
        dc.DrawBitmap(icon, pad_x, (total_h - icon_px) / 2, true);

    const int text_x     = pad_x + icon_px + gap;
    const int weight_col = ctx->FromDIP(60);
    const int text_max_w = std::max(total_w - right_pad - weight_col - text_x, ctx->FromDIP(40));

    int remain_g = 0;
    if (sp.net_weight > 0.0)
        remain_g = static_cast<int>(std::round(sp.net_weight));
    else {
        const double total_net = sp.effective_total_net_weight();
        if (total_net > 0.0) {
            const int pct = std::max(0, std::min(100, sp.remain_percent));
            remain_g = static_cast<int>(total_net * pct / 100.0);
        }
    }
    const wxString weight_str = wxString::Format("%dg", remain_g);

    wxFont name_font = ::Label::Body_14;
    wxFont note_font = ::Label::Body_14;
    note_font.SetPointSize(std::max(8, note_font.GetPointSize() - 2));

    const int top_y    = pad_y;
    const int bottom_y = pad_y + line_h;

    // Name
    dc.SetFont(name_font);
    dc.SetTextForeground(dimmed ? wxColour(0xB0, 0xB0, 0xB0)
                                : (dark_row ? wxColour(0xE5, 0xE5, 0xE6) : wxColour(0x26, 0x2E, 0x30)));
    wxString name = spool_display_name(sp);
    if (dc.GetTextExtent(name).GetWidth() > text_max_w)
        name = wxControl::Ellipsize(name, dc, wxELLIPSIZE_END, text_max_w);
    dc.DrawText(name, text_x, top_y);

    // Weight (right-aligned, same font as name)
    const wxSize w_sz = dc.GetTextExtent(weight_str);
    dc.DrawText(weight_str, total_w - right_pad - w_sz.GetWidth(), top_y);

    // Note
    dc.SetFont(note_font);
    dc.SetTextForeground(dimmed ? wxColour(0xC0, 0xC0, 0xC0) : wxColour(0x90, 0x90, 0x90));
    constexpr size_t kNoteHardCap = 30;
    const bool note_hardcap_cut = !sp.note.empty() &&
        wxString::FromUTF8(sp.note).length() > kNoteHardCap;
    wxString note = _spool_note_line(sp);
    const int note_max_w = std::max(total_w - text_x - right_pad, ctx->FromDIP(40));
    bool is_note_truncated = note_hardcap_cut || dc.GetTextExtent(note).GetWidth() > note_max_w;
    if (dc.GetTextExtent(note).GetWidth() > note_max_w)
        note = wxControl::Ellipsize(note, dc, wxELLIPSIZE_END, note_max_w);
    if (note_truncated) *note_truncated = is_note_truncated;
    dc.DrawText(note, text_x, bottom_y);

    if (dimmed) {
        if (wxGraphicsContext* gc = wxGraphicsContext::Create(dc)) {
            gc->SetPen(*wxTRANSPARENT_PEN);
            gc->SetBrush(wxBrush(dark_row ? wxColour(0, 0, 0, 100) : wxColour(255, 255, 255, 140)));
            gc->DrawRectangle(0, 0, total_w, total_h);
            delete gc;
        }
    }

    dc.SelectObject(wxNullBitmap);
    return bmp;
}

// Single-line filament card: color chip + name + right-aligned weight + rounded border.
// Shared by AMSNewFilamentRecordedDlg, AMSNewOfficialFilamentDlg (only-hit card)
// and the hit/candidate card shown inside the linked combo's collapsed box.
//   width_px    : -1 => default FromDIP(280); otherwise the target pixel width.
//   with_border : draw the rounded outer border (off when the combo frames it).
//   with_chevron: paint a down-chevron on the right (collapsed combo display).
//   gray_bg     : fill background with the combo's light-gray (matches the collapsed
//                 box fill so the card blends in); off => white / dark card background.
static wxBitmap _make_filament_card_bitmap(wxWindow* ctx, const Slic3r::GUI::FilamentSpool& sp,
                                           int width_px = -1,
                                           bool with_border = true,
                                           bool with_chevron = false,
                                           bool gray_bg = false)
{
    const int pad_x     = ctx->FromDIP(8);
    const int icon_px   = ctx->FromDIP(36);
    const int gap       = ctx->FromDIP(8);
    const int row_h     = ctx->FromDIP(48);
    const int right_pad = ctx->FromDIP(12);
    const int total_w   = (width_px > 0) ? std::max(width_px, ctx->FromDIP(120))
                                         : std::max(ctx->FromDIP(280), ctx->FromDIP(240));
    const int total_h   = row_h;
    const int chevron_reserve = with_chevron ? ctx->FromDIP(20) : 0;

    wxBitmap bmp(total_w, total_h);
#ifdef __WXOSX__
    bmp.UseAlpha();
#endif
    wxMemoryDC dc(bmp);
    const bool dark_mode = wxGetApp().dark_mode();
    // gray_bg matches the collapsed combo fill (StateColor(248,248,248) => #36363C in dark)
    // so the card blends seamlessly; otherwise a plain white / dark card background.
    const wxColour bg = gray_bg ? (dark_mode ? wxColour(0x36, 0x36, 0x3C) : wxColour(0xF8, 0xF8, 0xF8))
                                : (dark_mode ? wxColour(0x2D, 0x2D, 0x31) : *wxWHITE);
    dc.SetBackground(wxBrush(bg));
    dc.Clear();
    dc.SetPen(*wxTRANSPARENT_PEN);

    // Color chip
    wxBitmap chip = _make_spool_color_chip(ctx, sp);
    if (chip.IsOk())
        dc.DrawBitmap(chip, pad_x, (total_h - icon_px) / 2, true);

    const int text_x     = pad_x + icon_px + gap;
    const int weight_col = ctx->FromDIP(60);
    const int text_max_w = std::max(total_w - right_pad - chevron_reserve - weight_col - text_x,
                                    ctx->FromDIP(40));

    // Weight: prefer net_weight, else total_net * remain_percent/100
    int remain_g = 0;
    if (sp.net_weight > 0.0) {
        remain_g = static_cast<int>(sp.net_weight);
    } else {
        const double total_net = sp.effective_total_net_weight();
        if (total_net > 0.0) {
            const int pct = std::max(0, std::min(100, sp.remain_percent));
            remain_g = static_cast<int>(total_net * pct / 100.0);
        }
    }
    const wxString weight_str = wxString::Format("%dg", remain_g);

    // Name (vertically centered, left)
    wxFont name_font = ::Label::Body_14;
    dc.SetFont(name_font);
    dc.SetTextForeground(dark_mode ? wxColour(0xE5, 0xE5, 0xE6) : wxColour(0x26, 0x2E, 0x30));
    wxString name = spool_display_name(sp);
    if (dc.GetTextExtent(name).GetWidth() > text_max_w)
        name = wxControl::Ellipsize(name, dc, wxELLIPSIZE_END, text_max_w);
    const int text_h = dc.GetTextExtent(name).GetHeight();
    const int top_y  = (total_h - text_h) / 2;
    dc.DrawText(name, text_x, top_y);

    // Weight (vertically centered, right-aligned, left of the chevron reserve)
    const wxSize w_sz = dc.GetTextExtent(weight_str);
    dc.DrawText(weight_str, total_w - right_pad - chevron_reserve - w_sz.GetWidth(), top_y);

    if (with_border || with_chevron) {
        if (wxGraphicsContext* gc = wxGraphicsContext::Create(dc)) {
            const int pen_w = std::max(1, ctx->FromDIP(1));
            // Rounded border — inset by 0.5px so the stroke stays inside the bitmap
            if (with_border) {
                gc->SetPen(wxPen(dark_mode ? wxColour(0x50, 0x50, 0x54) : wxColour(0xD0, 0xD0, 0xD0), pen_w));
                gc->SetBrush(*wxTRANSPARENT_BRUSH);
                gc->DrawRoundedRectangle(0.5, 0.5, total_w - 1.0, total_h - 1.0, ctx->FromDIP(6));
            }
            // Down-chevron on the right
            if (with_chevron) {
                gc->SetPen(wxPen(dark_mode ? wxColour(0xB0, 0xB0, 0xB2) : wxColour(0x6B, 0x6B, 0x6B), pen_w));
                gc->SetBrush(*wxTRANSPARENT_BRUSH);
                const double cw  = ctx->FromDIP(10);
                const double chh = ctx->FromDIP(3);
                const double cx  = total_w - right_pad - cw;
                const double cy  = total_h / 2.0;
                wxGraphicsPath path = gc->CreatePath();
                path.MoveToPoint(cx, cy - chh);
                path.AddLineToPoint(cx + cw / 2.0, cy + chh);
                path.AddLineToPoint(cx + cw, cy - chh);
                gc->StrokePath(path);
            }
            delete gc;
        }
    }

    dc.SelectObject(wxNullBitmap);
    return bmp;
}

// AMSNewOfficialFilamentDlg

AMSNewOfficialFilamentDlg::AMSNewOfficialFilamentDlg(wxWindow* parent)
    : DPIDialog(parent, wxID_ANY, wxEmptyString,
                wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
    create();
    wxGetApp().UpdateDlgDarkUI(this);
}

void AMSNewOfficialFilamentDlg::create()
{
    SetBackgroundColour(*wxWHITE);
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    // Title row: hint icon + bold title (text set per case in populate_link_combo())
    auto* title_row = new wxBoxSizer(wxHORIZONTAL);
    auto* info_bmp = new wxStaticBitmap(this, wxID_ANY,
        create_scaled_bitmap("ams_filament_hint", this, 16),
        wxDefaultPosition, wxSize(FromDIP(16), FromDIP(16)));
    m_title = new wxStaticText(this, wxID_ANY, wxEmptyString,
        wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    m_title->SetFont(::Label::Head_16);
    m_title->Wrap(FromDIP(340));
    title_row->Add(info_bmp, 0, wxALIGN_TOP | wxRIGHT, FromDIP(6));
    title_row->Add(m_title, 1, wxEXPAND);
    sizer->Add(title_row, 0, wxALL, FromDIP(12));

    // Only-hit case: single filament card (reuses recorded-dialog card style)
    m_hit_card = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap);
    sizer->Add(m_hit_card, 0, wxALIGN_CENTER_HORIZONTAL | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
    sizer->Hide(m_hit_card);

    // Hit+candidates case: "Matched filament" label + dropdown
    m_match_label = new wxStaticText(this, wxID_ANY, _L("Matched filament"),
        wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    m_match_label->SetFont(::Label::Body_13);
    m_match_label->SetForegroundColour(StateColor::darkModeColorFor(wxColour(0x90, 0x90, 0x90)));
    // Left/right = 12 to left-align with the combo box below; small bottom gap to it.
    sizer->Add(m_match_label, 0, wxLEFT | wxRIGHT, FromDIP(12));
    sizer->AddSpacer(FromDIP(6));
    sizer->Hide(m_match_label);

    // Tall enough for the collapsed box to show a ~48px filament card (the closed
    // box height does not auto-grow for the icon bitmap, so size it up front).
    m_combo_link = new ::ComboBox(this, wxID_ANY, wxEmptyString,
                                  wxDefaultPosition, wxSize(FromDIP(360), FromDIP(56)),
                                  0, nullptr, wxCB_READONLY);
    m_combo_link->SetMinSize(wxSize(FromDIP(360), FromDIP(56)));
    // Light-gray rounded box, no visible border (border == fill); the gray auto-adapts
    // to dark mode (#F8F8F8 -> #36363C) via StateColor. A single Normal entry also
    // drops the read-only combo's default green hover/focus states.
    m_combo_link->SetCornerRadius(FromDIP(8));
    StateColor combo_gray(std::make_pair(wxColour(248, 248, 248), (int)StateColor::Normal));
    m_combo_link->SetBackgroundColor(combo_gray);
    m_combo_link->SetBorderColor(combo_gray);
    m_combo_row = new wxBoxSizer(wxHORIZONTAL);
    m_combo_row->Add(m_combo_link, 1, wxEXPAND);
    sizer->Add(m_combo_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
    sizer->Hide(m_combo_row, true);

    m_combo_link->Bind(wxEVT_COMMAND_COMBOBOX_SELECTED,
                       &AMSNewOfficialFilamentDlg::on_combo_selected, this);

    // Bottom buttons: [Add as new filament] (secondary)  [Confirm] (primary)
    auto* btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    btn_sizer->AddStretchSpacer();

    m_btn_record_new = new Button(this, _L("Add as new filament"));
    m_btn_record_new->SetMinSize(AMS_MATERIALS_SETTING_BUTTON_SIZE);
    m_btn_record_new->SetCornerRadius(FromDIP(12));
    m_btn_record_new->SetBackgroundColor(StateColor(
        std::pair<wxColour, int>(wxColour(0xD0, 0xD0, 0xD0), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(0xE8, 0xE8, 0xE8), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(0xF0, 0xF0, 0xF1), StateColor::Normal)
    ));
    m_btn_record_new->SetBorderColor(wxColour(0xCF, 0xCF, 0xCF));
    m_btn_record_new->SetTextColor(wxColour(0x32, 0x3A, 0x3D));
    m_btn_record_new->Bind(wxEVT_BUTTON, &AMSNewOfficialFilamentDlg::on_record_new, this);
    btn_sizer->Add(m_btn_record_new, 0, wxRIGHT, FromDIP(12));

    m_btn_confirm = new Button(this, _L("Confirm"));
    m_btn_confirm->SetMinSize(AMS_MATERIALS_SETTING_BUTTON_SIZE);
    m_btn_confirm->SetCornerRadius(FromDIP(12));
    m_btn_confirm->SetBackgroundColor(StateColor(
        std::pair<wxColour, int>(wxColour(27, 136, 68),  StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(0, 174, 66),   StateColor::Normal)
    ));
    m_btn_confirm->SetBorderColor(wxColour(0, 174, 66));
    m_btn_confirm->SetTextColor(wxColour("#FFFFFE"));
    m_btn_confirm->Bind(wxEVT_BUTTON, &AMSNewOfficialFilamentDlg::on_confirm, this);
    btn_sizer->Add(m_btn_confirm, 0);

    sizer->Add(btn_sizer, 0, wxEXPAND | wxALL, FromDIP(12));

    SetSizer(sizer);
    Fit();
    Centre();
}

void AMSNewOfficialFilamentDlg::on_record_new(wxCommandEvent&)
{
    m_choice = Choice::RecordNew;
    EndModal(wxID_OK);
}

void AMSNewOfficialFilamentDlg::on_confirm(wxCommandEvent&)
{
    // Confirm = accept the shown hit, or link to the chosen candidate.
    // m_hit_spool_id / m_selected_candidate_id are maintained by
    // populate_link_combo() and on_combo_selected().
    m_choice = Choice::LinkExisting;
    EndModal(wxID_OK);
}

void AMSNewOfficialFilamentDlg::on_combo_selected(wxCommandEvent& evt)
{
    const int sel = evt.GetSelection();
    const int closed_w = m_combo_link->GetSize().GetWidth() - m_combo_link->FromDIP(16);

    // Re-click the already-selected candidate => deselect it, head reverts to the hit.
    // The widget already ran SetSelection(sel) before this handler, so GetSelection()
    // can't tell a re-click from a fresh pick — we compare against the index we tracked.
    if (sel == m_prev_combo_sel) {
        m_combo_link->SetSelection(-1);   // clears the list checkmark/highlight
        m_combo_link->SetLabel(wxEmptyString);
        m_combo_link->SetIcon(_make_filament_card_bitmap(
            m_combo_link, m_hit_spool, closed_w, /*with_border=*/false, /*with_chevron=*/true, /*gray_bg=*/true));
        m_prev_combo_sel        = -1;
        m_selected_candidate_id = 0;
        m_selected_link_spool_id.clear();
        return;
    }

    // Dropdown list holds candidates only. Show the selected candidate as a card in
    // the collapsed box (same style as the hit card, chevron on the right), overriding
    // whatever the widget stuffed into the header on selection.
    auto sit = m_combo_idx_to_spool.find(sel);
    if (sit != m_combo_idx_to_spool.end()) {
        m_combo_link->SetLabel(wxEmptyString);
        m_combo_link->SetIcon(_make_filament_card_bitmap(
            m_combo_link, sit->second, closed_w, /*with_border=*/false, /*with_chevron=*/true, /*gray_bg=*/true));
    }

    auto id_it = m_combo_idx_to_spool_id.find(sel);
    if (id_it != m_combo_idx_to_spool_id.end()) {
        m_selected_link_spool_id = id_it->second;
        try { m_selected_candidate_id = std::stoi(id_it->second); }
        catch (...) {
            BOOST_LOG_TRIVIAL(warning) << current_exception_message(); m_selected_candidate_id = 0; }
    } else {
        m_selected_candidate_id = 0;
    }
    m_prev_combo_sel = sel;
}

void AMSNewOfficialFilamentDlg::SetTrayContext(MachineObject* obj,
                                               const std::string& ams_id,
                                               const std::string& slot_id)
{
    m_obj     = obj;
    m_ams_id  = ams_id;
    m_slot_id = slot_id;

    // Layout (card vs dropdown) and button state are managed by populate_link_combo().
    populate_link_combo();

    Layout();
    Fit();
}

void AMSNewOfficialFilamentDlg::SetSoftMatchData(const SoftMatchPendingResponse& data)
{
    m_soft_match_data = data;
    populate_link_combo();
}

static FilamentSpool soft_match_item_to_spool(const SoftMatchFilamentItem& item)
{
    FilamentSpool sp;
    sp.spool_id      = std::to_string(item.id);
    sp.brand         = item.filament_vendor;
    sp.material_type = item.filament_type;
    sp.series        = item.filament_name;
    sp.color_code    = item.color;
    if (!sp.color_code.empty() && sp.color_code[0] == '#')
        sp.color_code = sp.color_code.substr(1);
    sp.colors        = item.colors;
    sp.color_type    = item.color_type;
    sp.net_weight    = item.net_weight;
    sp.initial_weight = item.total_net_weight;
    sp.remain_percent = (item.total_net_weight > 0)
        ? static_cast<int>(item.net_weight * 100.0 / item.total_net_weight)
        : 100;
    sp.note         = item.note;
    sp.tray_id_name = item.tray_id_name;
    sp.entry_method = item.create_type;
    return sp;
}

void AMSNewOfficialFilamentDlg::populate_link_combo()
{
    m_combo_link->Clear();
    m_combo_idx_to_spool_id.clear();
    m_combo_idx_to_spool.clear();
    m_selected_link_spool_id.clear();
    m_prev_combo_sel = -1;

    DevAmsTray* tray = (m_obj ? m_obj->get_ams_tray(m_ams_id, m_slot_id) : nullptr);
    const std::string tray_uuid = (tray && !tray->uuid.empty()) ? tray->uuid : "";

    const SoftMatchFilamentItem* hit_item = nullptr;
    for (const auto& hit : m_soft_match_data.hits) {
        if (hit.rfid == tray_uuid) { hit_item = &hit; break; }
    }

    auto* dlg_sizer = GetSizer();

    if (!hit_item) {
        m_hit_spool_id          = 0;
        m_selected_candidate_id = 0;
        m_only_hit              = false;
        m_title->SetLabel(_L("AMS detected a new filament. Add it to Filament Manager?"));
        m_title->Wrap(FromDIP(340));
        if (dlg_sizer) {
            dlg_sizer->Hide(m_hit_card);
            dlg_sizer->Hide(m_match_label);
            dlg_sizer->Hide(m_combo_row, true);
        }
        Layout();
        Fit();
        Centre();
        return;
    }

    const std::vector<SoftMatchFilamentItem>* cands = nullptr;
    for (const auto& cand_entry : m_soft_match_data.candidates) {
        if (cand_entry.pending_id == hit_item->id) {
            cands = &cand_entry.candidates;
            break;
        }
    }

    m_hit_spool_id          = hit_item->id;
    m_selected_candidate_id = 0;
    m_only_hit              = (cands == nullptr || cands->empty());

    const FilamentSpool hit_sp = soft_match_item_to_spool(*hit_item);
    m_hit_spool = hit_sp;   // kept so on_combo_selected can restore the hit head on toggle-off

    if (m_only_hit) {
        m_title->SetLabel(
            _L("A similar filament was found in Filament Manager and matched automatically."));
        m_title->Wrap(FromDIP(340));

        m_hit_card->SetBitmap(_make_filament_card_bitmap(this, hit_sp));

        if (dlg_sizer) {
            dlg_sizer->Show(m_hit_card);
            dlg_sizer->Hide(m_match_label);
            dlg_sizer->Hide(m_combo_row, true);
        }
    } else {
        m_title->SetLabel(
            _L("Multiple similar filaments were found in Filament Manager. Please confirm the match."));
        m_title->Wrap(FromDIP(340));

        // DropDown reserves ~30px on the left of each row for check icon + padding.
        const int check_reserve = m_combo_link->FromDIP(30);
        const int row_width = std::max(
            m_combo_link->GetSize().GetWidth() - check_reserve,
            m_combo_link->FromDIP(230));

        for (const auto& cand : *cands) {
            FilamentSpool sp = soft_match_item_to_spool(cand);
            bool note_truncated = false;
            wxBitmap row_bmp = _render_spool_row_bitmap(m_combo_link, sp, row_width,
                                                        /*dimmed=*/false, &note_truncated);
            int idx = m_combo_link->Append(wxEmptyString, row_bmp);
            if (idx < 0) continue;
            m_combo_idx_to_spool_id[idx] = std::to_string(cand.id);
            m_combo_idx_to_spool[idx]    = sp;

            if (note_truncated && !cand.note.empty())
                m_combo_link->SetItemTooltip(static_cast<unsigned int>(idx),
                    _L("Remark") + ": " + wxString::FromUTF8(cand.note));
        }

        const int closed_w = m_combo_link->GetSize().GetWidth() - m_combo_link->FromDIP(16);
        m_combo_link->SetLabel(wxEmptyString);
        m_combo_link->SetIcon(_make_filament_card_bitmap(
            m_combo_link, hit_sp, closed_w, /*with_border=*/false, /*with_chevron=*/true, /*gray_bg=*/true));
        m_combo_link->Enable(true);

        if (dlg_sizer) {
            dlg_sizer->Hide(m_hit_card);
            dlg_sizer->Show(m_match_label);
            dlg_sizer->Show(m_combo_row, true);
        }
    }

    Layout();
    Fit();
    Centre();
}

// AMSNewFilamentRecordedDlg

AMSNewFilamentRecordedDlg::AMSNewFilamentRecordedDlg(wxWindow* parent,
                                                     const FilamentSpool& sp)
    : DPIDialog(parent, wxID_ANY, _L("New Filament"),
                wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
    create(sp);
    wxGetApp().UpdateDlgDarkUI(this);
}

void AMSNewFilamentRecordedDlg::create(const FilamentSpool& sp)
{
    SetBackgroundColour(*wxWHITE);
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    // Title row: hint icon + bold info text
    auto* title_row = new wxBoxSizer(wxHORIZONTAL);
    auto* hint_bmp = new wxStaticBitmap(this, wxID_ANY,
        create_scaled_bitmap("ams_filament_hint", this, 16),
        wxDefaultPosition, wxSize(FromDIP(16), FromDIP(16)));
    auto* label = new wxStaticText(this, wxID_ANY,
        _L("New filament information has been recorded."),
        wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    label->SetFont(::Label::Head_16);
    label->Wrap(FromDIP(340));
    title_row->Add(hint_bmp, 0, wxALIGN_TOP | wxRIGHT, FromDIP(6));
    title_row->Add(label, 1, wxEXPAND);
    sizer->Add(title_row, 0, wxALL, FromDIP(12));

    // Card bitmap: color chip + name/weight row (shared helper)
    {
        auto* bmp_ctrl = new wxStaticBitmap(this, wxID_ANY, _make_filament_card_bitmap(this, sp));
        sizer->Add(bmp_ctrl, 0, wxALIGN_CENTER_HORIZONTAL | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
    }

    // OK button
    auto* btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    btn_sizer->AddStretchSpacer();
    auto* btn_ok = new Button(this, _L("OK"));
    btn_ok->SetMinSize(AMS_MATERIALS_SETTING_BUTTON_SIZE);
    btn_ok->SetCornerRadius(FromDIP(12));
    btn_ok->SetBackgroundColor(StateColor(
        std::pair<wxColour, int>(wxColour(27, 136, 68),  StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(0, 174, 66),   StateColor::Normal)
    ));
    btn_ok->SetBorderColor(wxColour(0, 174, 66));
    btn_ok->SetTextColor(wxColour("#FFFFFE"));
    btn_ok->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_OK); });
    btn_sizer->Add(btn_ok, 0);
    sizer->Add(btn_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));

    SetSizer(sizer);
    Fit();
    Centre();
}

}} // namespace Slic3r::GUI
