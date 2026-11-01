#include "MixingKitsHelpDialog.hpp"

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/StateColor.hpp"
#include "wxExtensions.hpp"

#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <wx/panel.h>
#include <wx/sizer.h>

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

// PLA Basic CMYW bases, matching ColorDecomposeDialog.
const wxColour CMYW_CYAN(0, 255, 255);
const wxColour CMYW_MAGENTA(255, 0, 255);
const wxColour CMYW_YELLOW(255, 255, 0);
const wxColour CMYW_WHITE(255, 255, 255);

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
        for (int i = 0; i < n_dots; ++i) {
            const int x = i * (d + gap);
            dc.SetPen(wxPen(COLOR_DOT_BORDER, row->FromDIP(1)));
            dc.SetBrush(wxBrush(colors[i]));
            dc.DrawEllipse(x, 0, d, d);
        }
    });
    return row;
}

} // namespace

MixingKitsHelpDialog::MixingKitsHelpDialog(wxWindow* parent)
    : DPIDialog(parent, wxID_ANY, _L("How to use standard mixing kits"), wxDefaultPosition,
                wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
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

void MixingKitsHelpDialog::build_ui()
{
    SetBackgroundColour(dlg_bg());

    auto* main = new wxBoxSizer(wxVERTICAL);
    const int side = FromDIP(32);

    main->AddSpacer(FromDIP(24));

    auto* section = new Label(this, Label::Head_16, _L("What is the CMYW mixing kit?"));
    section->SetForegroundColour(COLOR_BRAND);
    section->SetBackgroundColour(dlg_bg());
    main->Add(section, 0, wxLEFT | wxRIGHT, side);

    main->AddSpacer(FromDIP(10));

    m_intro = new Label(this, Label::Body_14,
        _L("After official print testing, you only need a standard CMYW mixing kit to mix hundreds of color effects."));
    m_intro->SetForegroundColour(text_dark());
    m_intro->SetBackgroundColour(dlg_bg());
    main->Add(m_intro, 0, wxLEFT | wxRIGHT, side);

    main->AddSpacer(FromDIP(16));

    auto* cards = new wxBoxSizer(wxHORIZONTAL);
    cards->AddStretchSpacer();
    cards->Add(create_kit_card(this,
                               {CMYW_WHITE, CMYW_CYAN, CMYW_MAGENTA, CMYW_YELLOW},
                               _L("4-color CMYW"),
                               _L("Cyan · Magenta · Yellow · White")),
               0);
    cards->AddStretchSpacer();
    main->Add(cards, 0, wxEXPAND | wxLEFT | wxRIGHT, side);

    main->AddSpacer(FromDIP(24));

    m_closing = new Label(this, Label::Body_14,
        _L("Therefore, when importing a model, you can match colors using these kit filaments and their mixes."));
    m_closing->SetForegroundColour(text_dark());
    m_closing->SetBackgroundColour(dlg_bg());
    main->Add(m_closing, 0, wxLEFT | wxRIGHT, side);

    main->AddSpacer(FromDIP(16));

    StateColor btn_bg(
        std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
    StateColor btn_bd(
        std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
    StateColor btn_text(
        std::pair<wxColour, int>(wxColour("#FFFFFE"), StateColor::Normal));

    m_btn_add = new Button(this, _L("Add to project filament list"));
    m_btn_add->SetBackgroundColor(btn_bg);
    m_btn_add->SetBorderColor(btn_bd);
    m_btn_add->SetTextColor(btn_text);
    m_btn_add->SetFont(Label::Body_14);
    const wxSize add_text = m_btn_add->GetTextExtent(m_btn_add->GetLabel());
    m_btn_add->SetMinSize(wxSize(std::max(FromDIP(168), add_text.GetWidth() + FromDIP(24)), FromDIP(32)));
    m_btn_add->SetCornerRadius(FromDIP(16));
    m_btn_add->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_APPLY); });

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
