#include "MixingKitsHelpDialog.hpp"

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/StateColor.hpp"
#include "format.hpp"
#include "wxExtensions.hpp"
#include "libslic3r/Model.hpp"

#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>


#include <algorithm>
#include <memory>

namespace Slic3r {
namespace GUI {

namespace {

const wxColour COLOR_BRAND("#00AE42");
const wxColour COLOR_BG_CARD("#F8F8F8");
const wxColour COLOR_BORDER("#EEEEEE");
const wxColour COLOR_TEXT_DARK("#262E30");
const wxColour COLOR_TEXT_MUTED("#909090");
const wxColour COLOR_DOT_BORDER("#DBDBDB");

// PLA Palette / GFA20 bases, matching pla_palette_cmykw_official_components().
const wxColour CMYKW_CYAN(0, 144, 218);
const wxColour CMYKW_MAGENTA(226, 69, 133);
const wxColour CMYKW_YELLOW(252, 227, 0);
const wxColour CMYKW_BLACK(0, 0, 0);
const wxColour CMYKW_WHITE(255, 255, 255);

wxColour dlg_bg() { return StateColor::darkModeColorFor(*wxWHITE); }
wxColour card_bg() { return StateColor::darkModeColorFor(COLOR_BG_CARD); }
wxColour text_dark() { return StateColor::darkModeColorFor(COLOR_TEXT_DARK); }
wxColour text_muted() { return StateColor::darkModeColorFor(COLOR_TEXT_MUTED); }

wxPanel* create_color_dots(wxWindow* parent, const std::vector<wxColour>& colors)
{
    auto* row = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    row->SetBackgroundColour(card_bg());
    row->SetBackgroundStyle(wxBG_STYLE_PAINT);

    const int d   = parent->FromDIP(18);
    const int gap = parent->FromDIP(8);
    const int n   = static_cast<int>(colors.size());
    const int w   = n > 0 ? n * d + (n - 1) * gap : 0;
    row->SetMinSize(wxSize(w, d + parent->FromDIP(2)));

    row->Bind(wxEVT_PAINT, [row, colors, d, gap](wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(row);
        dc.SetBackground(wxBrush(row->GetBackgroundColour()));
        dc.Clear();
        const int n_dots = static_cast<int>(colors.size());
        const int border_w = row->FromDIP(1);
        const double inset = border_w / 2.0;
        // GDI ellipses are not antialiased; use a graphics context when available.
        std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
        for (int i = 0; i < n_dots; ++i) {
            const int x = i * (d + gap);
            if (gc) {
                gc->SetPen(wxPen(COLOR_DOT_BORDER, border_w));
                gc->SetBrush(wxBrush(colors[i]));
                gc->DrawEllipse(x + inset, inset, d - 2 * inset, d - 2 * inset);
            } else {
                dc.SetPen(wxPen(COLOR_DOT_BORDER, border_w));
                dc.SetBrush(wxBrush(colors[i]));
                dc.DrawEllipse(x, 0, d, d);
            }
        }
    });
    return row;
}

} // namespace

MixingKitsHelpDialog::MixingKitsHelpDialog(wxWindow* parent, const OfficialMixingKitAddPlan& plan)
    : DPIDialog(parent, wxID_ANY, _L("How to use standard mixing kits"), wxDefaultPosition,
                wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
    , m_plan(plan)
{
    wxBitmap bmp = create_scaled_bitmap("BambuStudio", this, 16);
    if (bmp.IsOk()) {
        wxIcon icon;
        icon.CopyFromBitmap(bmp);
        SetIcon(icon);
    }

    build_ui();
    wxGetApp().UpdateDlgDarkUI(this);
    Layout();
    Fit();
    wrap_body_labels();
    Layout();
    Fit();
    CenterOnParent();
}

void MixingKitsHelpDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    (void)suggested_rect;
    if (m_btn_add) {
        const wxSize add_text = m_btn_add->GetTextExtent(m_btn_add->GetLabel());
        m_btn_add->SetMinSize(wxSize(std::max(FromDIP(168), add_text.GetWidth() + FromDIP(24)), FromDIP(32)));
        m_btn_add->SetCornerRadius(FromDIP(16));
        m_btn_add->Rescale();
    }
    if (m_btn_close) {
        m_btn_close->SetMinSize(wxSize(FromDIP(72), FromDIP(32)));
        m_btn_close->SetCornerRadius(FromDIP(16));
        m_btn_close->Rescale();
    }
    wrap_body_labels();
    Fit();
    Refresh();
}

void MixingKitsHelpDialog::wrap_body_labels()
{
    const int wrap_w = FromDIP(480);
    if (m_intro)
        m_intro->Wrap(wrap_w);
    if (m_closing)
        m_closing->Wrap(wrap_w);
    if (m_capacity_warning)
        m_capacity_warning->Wrap(wrap_w - FromDIP(22));

    const int pad = FromDIP(16);
    for (auto& item : m_kit_subtitles) {
        if (!item.first || !item.second)
            continue;
        int inner = item.first->GetClientSize().GetWidth() - 2 * pad - FromDIP(8);
        if (inner > FromDIP(180))
            inner = FromDIP(180);
        if (inner < FromDIP(110))
            inner = FromDIP(110);
        item.second->Wrap(inner);
        item.second->InvalidateBestSize();
        item.second->SetMinSize(item.second->GetBestSize());
        item.first->InvalidateBestSize();
    }
}

wxWindow* MixingKitsHelpDialog::create_kit_card(wxWindow* parent,
                                                const std::vector<wxColour>& colors,
                                                const wxString& title,
                                                const wxString& subtitle)
{
    auto* card = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    card->SetBackgroundStyle(wxBG_STYLE_PAINT);
    card->SetBackgroundColour(card_bg());
    card->SetMinSize(wxSize(FromDIP(220), -1));

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    const int pad = FromDIP(16);

    sizer->Add(create_color_dots(card, colors), 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, pad);

    auto* title_label = new Label(card, Label::Head_14, title, wxALIGN_CENTER);
    title_label->SetForegroundColour(text_dark());
    title_label->SetBackgroundColour(card_bg());
    sizer->Add(title_label, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, pad);

    auto* sub_label = new Label(card, Label::Body_12, subtitle, wxALIGN_CENTER);
    sub_label->SetForegroundColour(text_muted());
    sub_label->SetBackgroundColour(card_bg());
    sizer->Add(sub_label, 0, wxALIGN_CENTER_HORIZONTAL | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, pad);
    m_kit_subtitles.push_back({card, sub_label});

    card->SetSizer(sizer);

    card->Bind(wxEVT_PAINT, [card](wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(card);
        const wxSize sz = card->GetClientSize();
        dc.SetBackground(wxBrush(dlg_bg()));
        dc.Clear();
        const int radius = card->FromDIP(8);
        const int border_w = card->FromDIP(1);
        const double inset = border_w / 2.0;
        std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
        if (gc) {
            gc->SetPen(wxPen(StateColor::darkModeColorFor(COLOR_BORDER), border_w));
            gc->SetBrush(wxBrush(card_bg()));
            gc->DrawRoundedRectangle(inset, inset, sz.x - 2 * inset, sz.y - 2 * inset, radius);
        } else {
            dc.SetPen(wxPen(StateColor::darkModeColorFor(COLOR_BORDER), border_w));
            dc.SetBrush(wxBrush(card_bg()));
            dc.DrawRoundedRectangle(0, 0, sz.x, sz.y, radius);
        }
    });

    return card;
}

wxWindow* MixingKitsHelpDialog::create_capacity_warning(wxWindow* parent)
{
    auto* panel = new wxPanel(parent, wxID_ANY);
    panel->SetBackgroundColour(dlg_bg());
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    const bool can_delete = m_plan.can_add;
    const wxColour warning_color = can_delete ? wxColour("#FF6F00") : wxColour("#E84C4C");
    auto* warning_row = new wxBoxSizer(wxHORIZONTAL);
    // Reuse the shared warning / error icons so this hint matches the other prompts.
    auto* icon = new wxStaticBitmap(panel, wxID_ANY,
                                    create_scaled_bitmap(can_delete ? "warning" : "error", panel, 16));
    warning_row->Add(icon, 0, wxRIGHT, FromDIP(6));

    const wxString text = can_delete
        ? format_wxstr(_L("The project has too many filaments. Adding the mixing kit exceeds the %1% filament limit, so at least %2% project filament(s) must be deleted first. The following unused project filaments will be deleted automatically:"),
                       MAX_PHYSICAL_FILAMENT_COUNT, m_plan.required_delete_count)
        : format_wxstr(_L("The project has too many filaments. Adding the mixing kit exceeds the %1% filament limit. Please delete at least %2% filament(s) on the Prepare page, then add the kit again."),
                       MAX_PHYSICAL_FILAMENT_COUNT, m_plan.required_delete_count);
    m_capacity_warning = new Label(panel, Label::Body_14, text);
    m_capacity_warning->SetForegroundColour(warning_color);
    m_capacity_warning->SetBackgroundColour(dlg_bg());
    warning_row->Add(m_capacity_warning, 1);
    sizer->Add(warning_row, 0, wxEXPAND);

    if (can_delete && !m_plan.delete_indices.empty()) {
        auto* card = new wxPanel(panel, wxID_ANY);
        card->SetBackgroundColour(card_bg());
        card->SetBackgroundStyle(wxBG_STYLE_PAINT);
        card->Bind(wxEVT_PAINT, [card](wxPaintEvent&) {
            wxAutoBufferedPaintDC dc(card);
            const wxSize sz = card->GetClientSize();
            dc.SetBackground(wxBrush(dlg_bg()));
            dc.Clear();
            const int radius = card->FromDIP(8);
            std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
            if (gc) {
                gc->SetPen(*wxTRANSPARENT_PEN);
                gc->SetBrush(wxBrush(card_bg()));
                gc->DrawRoundedRectangle(0, 0, sz.x, sz.y, radius);
            } else {
                dc.SetPen(*wxTRANSPARENT_PEN);
                dc.SetBrush(wxBrush(card_bg()));
                dc.DrawRoundedRectangle(0, 0, sz.x, sz.y, radius);
            }
        });
        auto* card_sizer = new wxBoxSizer(wxVERTICAL);
        auto* title = new Label(card, Label::Body_12, _L("Existing project filaments"));
        title->SetForegroundColour(text_muted());
        title->SetBackgroundColour(card_bg());
        card_sizer->Add(title, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(12));

        auto* swatches = new wxBoxSizer(wxHORIZONTAL);
        for (size_t i = 0; i < m_plan.delete_indices.size(); ++i) {
            const size_t   cfg_idx = m_plan.delete_indices[i];
            const wxColour colour(i < m_plan.delete_colors.size() ? m_plan.delete_colors[i] : "#808080");
            const unsigned number  = unsigned(cfg_idx + 1);
            auto* swatch = new wxPanel(card, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(24), FromDIP(24)));
            swatch->SetMinSize(wxSize(FromDIP(24), FromDIP(24)));
            swatch->SetBackgroundStyle(wxBG_STYLE_PAINT);
            swatch->Bind(wxEVT_PAINT, [swatch, colour, number](wxPaintEvent&) {
                wxAutoBufferedPaintDC dc(swatch);
                const wxSize sz = swatch->GetClientSize();
                dc.SetPen(wxPen(StateColor::darkModeColorFor(COLOR_BORDER), 1));
                dc.SetBrush(wxBrush(colour.IsOk() ? colour : wxColour("#808080")));
                dc.DrawRectangle(0, 0, sz.x, sz.y);
                dc.SetFont(::Label::Body_12);
                const wxString num = wxString::Format("%u", number);
                const wxSize   text_sz = dc.GetTextExtent(num);
                dc.SetTextForeground(colour.GetLuminance() > 0.5 ? COLOR_TEXT_DARK : *wxWHITE);
                dc.DrawText(num, (sz.x - text_sz.x) / 2, (sz.y - text_sz.y) / 2);
            });
            swatches->Add(swatch, 0, wxRIGHT | wxBOTTOM, FromDIP(8));
        }
        card_sizer->Add(swatches, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(12));
        card_sizer->AddSpacer(FromDIP(4));
        card->SetSizer(card_sizer);
        sizer->Add(card, 0, wxLEFT | wxTOP, FromDIP(20));
    }

    panel->SetSizer(sizer);
    return panel;
}

void MixingKitsHelpDialog::build_ui()
{
    SetBackgroundColour(dlg_bg());

    auto* main = new wxBoxSizer(wxVERTICAL);
    const int side = FromDIP(32);

    main->AddSpacer(FromDIP(24));

    auto* section = new Label(this, Label::Head_16, _L("What is the CMYKW mixing kit?"));
    section->SetForegroundColour(COLOR_BRAND);
    section->SetBackgroundColour(dlg_bg());
    main->Add(section, 0, wxLEFT | wxRIGHT, side);

    main->AddSpacer(FromDIP(10));

    m_intro = new Label(this, Label::Body_14,
        _L("After official print testing, you only need a standard CMYKW mixing kit to mix hundreds of color effects."));
    m_intro->SetForegroundColour(text_dark());
    m_intro->SetBackgroundColour(dlg_bg());
    main->Add(m_intro, 0, wxLEFT | wxRIGHT, side);

    main->AddSpacer(FromDIP(16));

    auto* cards = new wxBoxSizer(wxHORIZONTAL);
    cards->AddStretchSpacer();
    cards->Add(create_kit_card(this,
                               {CMYKW_CYAN, CMYKW_MAGENTA, CMYKW_YELLOW, CMYKW_BLACK, CMYKW_WHITE},
                               _L("5-color CMYKW"),
                               _L("Cyan · Magenta · Yellow · Black · White")),
               0);
    cards->AddStretchSpacer();
    main->Add(cards, 0, wxEXPAND | wxLEFT | wxRIGHT, side);

    main->AddSpacer(FromDIP(24));

    m_closing = new Label(this, Label::Body_14,
        _L("Therefore, when importing a model, you can match colors using these kit filaments and their mixes."));
    m_closing->SetForegroundColour(text_dark());
    m_closing->SetBackgroundColour(dlg_bg());
    main->Add(m_closing, 0, wxLEFT | wxRIGHT, side);

    if (m_plan.needs_delete()) {
        main->AddSpacer(FromDIP(16));
        main->Add(create_capacity_warning(this), 0, wxEXPAND | wxLEFT | wxRIGHT, side);
    }
    if (m_plan.kit_already_present) {
        main->AddSpacer(FromDIP(16));
        auto* existing_hint = new Label(this, Label::Body_14,
            _L("The mixing kit is already in the project filament list. Use it now?"));
        existing_hint->SetForegroundColour(text_muted());
        existing_hint->SetBackgroundColour(dlg_bg());
        main->Add(existing_hint, 0, wxLEFT | wxRIGHT, side);
    }

    main->AddSpacer(FromDIP(16));

    StateColor btn_bg(
        std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
    StateColor btn_bd(
        std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
    StateColor btn_text(
        std::pair<wxColour, int>(wxColour("#FFFFFE"), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour("#FFFFFE"), StateColor::Normal));

    m_btn_add = new Button(this, m_plan.kit_already_present ? _L("Use now") : _L("Add to project filament list"));
    m_btn_add->SetBackgroundColor(btn_bg);
    m_btn_add->SetBorderColor(btn_bd);
    m_btn_add->SetTextColor(btn_text);
    m_btn_add->SetFont(Label::Body_14);
    const wxSize add_text = m_btn_add->GetTextExtent(m_btn_add->GetLabel());
    m_btn_add->SetMinSize(wxSize(std::max(FromDIP(168), add_text.GetWidth() + FromDIP(24)), FromDIP(32)));
    m_btn_add->SetCornerRadius(FromDIP(16));
    m_btn_add->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_APPLY); });
    m_btn_add->Enable(m_plan.can_add);

    m_btn_close = new Button(this, _L("Close"));
    m_btn_close->SetBackgroundColor(StateColor::darkModeColorFor(*wxWHITE));
    m_btn_close->SetBorderColor(StateColor::darkModeColorFor(wxColour("#CECECE")));
    m_btn_close->SetTextColor(StateColor::darkModeColorFor(wxColour("#262E30")));
    m_btn_close->SetFont(Label::Body_14);
    m_btn_close->SetMinSize(wxSize(FromDIP(72), FromDIP(32)));
    m_btn_close->SetCornerRadius(FromDIP(16));
    m_btn_close->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CANCEL); });

    auto* btn_row = new wxBoxSizer(wxHORIZONTAL);
    btn_row->AddStretchSpacer();
    btn_row->Add(m_btn_add, 0);
    btn_row->AddSpacer(FromDIP(12));
    btn_row->Add(m_btn_close, 0);
    main->Add(btn_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, side);

    SetSizer(main);
    SetMinSize(wxSize(FromDIP(544), -1));
}

} // namespace GUI
} // namespace Slic3r
