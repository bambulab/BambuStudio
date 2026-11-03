#include "NozzleDetailPopup.hpp"

#include <utility>

#include <wx/display.h>
#include <wx/panel.h>
#include <wx/sizer.h>

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"
#include "slic3r/GUI/Widgets/StateColor.hpp"
#include "slic3r/GUI/ExtruderHelpers.hpp" // NozzleConfig + format_nozzle_spec

namespace Slic3r::GUI {

namespace {
constexpr int CARD_RADIUS = 4;
constexpr int PADDING     = 16;
constexpr int COL_GAP     = 24;
constexpr int ROW_GAP     = 12;
constexpr int ROW_WIDTH   = 180;

struct Palette
{
    wxColour card_bg, divider, title, key, value;
};

Palette palette(bool dark)
{
    auto t = [dark](const wxColour &c) { return dark ? StateColor::darkModeColorFor(c) : c; };
    return {t(ThemeColor::White), t(ThemeColor::Grey300), t(ThemeColor::TextPrimary), t(ThemeColor::TextDisabled), t(ThemeColor::TextPrimary)};
}

// Split the extruder vector into left/right display columns with global nozzle numbers.
void split_columns(const std::vector<NozzleConfig> &ext, std::vector<std::pair<int, NozzleConfig>> &left, std::vector<std::pair<int, NozzleConfig>> &right)
{
    for (int i = 0; i < (int) ext.size(); ++i) {
        const int num = i + 1;
        (num % 2 ? left : right).push_back({num, ext[i]});
    }
}
} // namespace

NozzleDetailPopup::NozzleDetailPopup(wxWindow *parent) : RoundedPopupWindow(parent, CARD_RADIUS) {}

wxBoxSizer *NozzleDetailPopup::build_row(int number, const NozzleConfig &cfg)
{
    const Palette p   = palette(m_dark);
    auto         *row = new wxBoxSizer(wxHORIZONTAL);

    auto *key = new Label(this, wxString::Format(_L("Nozzle %d: "), number));
    key->SetFont(Label::Body_13);
    key->SetForegroundColour(p.key);
    key->SetBackgroundColour(p.card_bg);
    row->Add(key, 0, wxALIGN_CENTER_VERTICAL);

    auto *val = new Label(this, format_nozzle_spec(cfg));
    val->SetFont(Label::Body_13);
    val->SetForegroundColour(p.value);
    val->SetBackgroundColour(p.card_bg);
    row->Add(val, 0, wxALIGN_CENTER_VERTICAL);

    row->AddStretchSpacer();
    row->SetMinSize(FromDIP(wxSize(ROW_WIDTH, 0)));
    return row;
}

void NozzleDetailPopup::rebuild(const std::vector<NozzleConfig> &ext)
{
    m_dark          = wxGetApp().dark_mode();
    const Palette p = palette(m_dark);

    DestroyChildren();
    auto *v = new wxBoxSizer(wxVERTICAL);

    auto *title = new Label(this, _L("Nozzle Details"));
    title->SetFont(Label::Body_13);
    title->SetForegroundColour(p.title);
    title->SetBackgroundColour(p.card_bg);
    v->Add(title, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(PADDING));

    auto *divider = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    divider->SetBackgroundColour(p.divider);
    v->Add(divider, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));

    std::vector<std::pair<int, NozzleConfig>> left, right;
    split_columns(ext, left, right);

    auto add_col = [&](const std::vector<std::pair<int, NozzleConfig>> &items) {
        auto *c = new wxBoxSizer(wxVERTICAL);
        for (int i = 0; i < (int) items.size(); ++i) c->Add(build_row(items[i].first, items[i].second), 0, i ? wxTOP : 0, i ? FromDIP(ROW_GAP) : 0);
        return c;
    };

    auto *cols = new wxBoxSizer(wxHORIZONTAL);
    cols->Add(add_col(left), 0);
    if (!right.empty()) {
        cols->AddSpacer(FromDIP(COL_GAP));
        cols->Add(add_col(right), 0);
    }
    v->Add(cols, 0, wxALL, FromDIP(PADDING));

    SetSizer(v);
    Finalize(); // size to content + clip to the rounded silhouette (RoundedPopupWindow)
}

void NozzleDetailPopup::ShowTip(const std::vector<NozzleConfig> &ext, const wxRect &anchor)
{
    rebuild(ext);

    const wxSize sz   = GetSize();
    int          disp = wxDisplay::GetFromPoint(anchor.GetTopLeft());
    if (disp == wxNOT_FOUND) disp = 0;
    const wxRect area = wxDisplay(disp).GetClientArea();

    const int gap = FromDIP(8);
    wxPoint   pos(anchor.GetLeft() - gap - sz.x, anchor.GetTop()); // prefer the panel's left
    if (pos.x < area.GetLeft()) pos.x = anchor.GetRight() + gap;   // no room: flip to the right
    if (pos.x + sz.x > area.GetRight()) pos.x = area.GetRight() - sz.x;
    if (pos.x < area.GetLeft()) pos.x = area.GetLeft();
    if (pos.y + sz.y > area.GetBottom()) pos.y = area.GetBottom() - sz.y;
    if (pos.y < area.GetTop()) pos.y = area.GetTop();
    SetPosition(pos);

    Show(); // RoundedPopupWindow syncs the drop shadow on EVT_SHOW
}

void NozzleDetailPopup::HideTip() { Hide(); }

} // namespace Slic3r::GUI
