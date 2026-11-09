#include "MixSupplementFilamentDialog.hpp"

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "Plater.hpp"
#include "PresetComboBoxes.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/StateColor.hpp"
#include "Widgets/StaticBox.hpp"
#include "wxExtensions.hpp"

#include <wx/sizer.h>

#include <algorithm>

namespace Slic3r {
namespace GUI {

namespace {

const wxColour COLOR_TEXT_DARK("#262E30");
const wxColour COLOR_TEXT_MUTED("#6B6B6B");
const wxColour COLOR_BORDER("#DBDBDB");

wxColour dlg_bg() { return StateColor::darkModeColorFor(*wxWHITE); }
wxColour text_dark() { return StateColor::darkModeColorFor(COLOR_TEXT_DARK); }
wxColour text_muted() { return StateColor::darkModeColorFor(COLOR_TEXT_MUTED); }

// Rough RGB distance; enough to keep "New filament" away from colors already listed.
bool colors_too_close(const wxColour& a, const wxColour& b)
{
    const int dr = int(a.Red()) - int(b.Red());
    const int dg = int(a.Green()) - int(b.Green());
    const int db = int(a.Blue()) - int(b.Blue());
    return dr * dr + dg * dg + db * db < 60 * 60;
}

} // namespace

MixSupplementFilamentDialog::MixSupplementFilamentDialog(wxWindow*                       parent,
                                                         const std::vector<Row>&         rows,
                                                         size_t                          max_rows,
                                                         const std::string&              fallback_preset,
                                                         const std::vector<std::string>& project_colors,
                                                         bool                            family_has_color)
    : DPIDialog(parent, wxID_ANY, _L("Add Filament"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
    , m_max_rows(max_rows)
    , m_fallback_preset(fallback_preset)
    , m_project_colors(project_colors)
{
    wxBitmap bmp = create_scaled_bitmap("BambuStudio", this, 16);
    if (bmp.IsOk()) {
        wxIcon icon;
        icon.CopyFromBitmap(bmp);
        SetIcon(icon);
    }

    build_ui(rows, family_has_color);
    wxGetApp().UpdateDlgDarkUI(this);
    relayout();
    CenterOnParent();
}

std::vector<MixSupplementFilamentDialog::Row> MixSupplementFilamentDialog::get_rows() const
{
    std::vector<Row> rows;
    rows.reserve(m_rows.size());
    for (const RowUi& ui : m_rows) {
        Row row;
        row.preset_name = ui.combo->get_selected_preset_name();
        row.color_hex   = ui.combo->get_color_hex();
        row.tag         = ui.tag;
        rows.push_back(std::move(row));
    }
    return rows;
}

void MixSupplementFilamentDialog::build_ui(const std::vector<Row>& rows, bool family_has_color)
{
    SetBackgroundColour(dlg_bg());

    auto*     main = new wxBoxSizer(wxVERTICAL);
    const int side = FromDIP(32);

    main->AddSpacer(FromDIP(24));

    m_message = new Label(this, Label::Body_14, family_has_color ?
        _L("Color mixing requires at least two filaments of the same type, but only one is available. "
           "The following filaments will be added based on the model colors:") :
        _L("Color mixing requires at least two filaments of the same type. "
           "The following filaments will be added based on the model colors:"));
    m_message->SetForegroundColour(text_dark());
    m_message->SetBackgroundColour(dlg_bg());
    main->Add(m_message, 0, wxLEFT | wxRIGHT, side);

    main->AddSpacer(FromDIP(12));

    m_rows_sizer = new wxBoxSizer(wxVERTICAL);
    main->Add(m_rows_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT, side);
    for (const Row& row : rows) {
        if (m_rows.size() >= m_max_rows)
            break;
        add_row(row);
    }

    m_btn_new = new Button(this, _L("New filament"));
    m_btn_new->SetBackgroundColor(StateColor(
        std::pair<wxColour, int>(StateColor::darkModeColorFor(wxColour("#F4F4F4")), StateColor::Disabled),
        std::pair<wxColour, int>(StateColor::darkModeColorFor(wxColour("#EEEEEE")), StateColor::Pressed),
        std::pair<wxColour, int>(StateColor::darkModeColorFor(wxColour("#F8F8F8")), StateColor::Hovered),
        std::pair<wxColour, int>(StateColor::darkModeColorFor(*wxWHITE), StateColor::Normal)));
    m_btn_new->SetBorderColor(StateColor(std::pair<wxColour, int>(StateColor::darkModeColorFor(COLOR_BORDER), StateColor::Normal)));
    m_btn_new->SetTextColor(StateColor(
        std::pair<wxColour, int>(StateColor::darkModeColorFor(wxColour("#ACACAC")), StateColor::Disabled),
        std::pair<wxColour, int>(text_dark(), StateColor::Normal)));
    m_btn_new->SetFont(Label::Body_14);
    m_btn_new->SetMinSize(wxSize(-1, FromDIP(32)));
    m_btn_new->SetCornerRadius(FromDIP(4));
    m_btn_new->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_new_filament(); });

    // Keep the button as wide as the filament cards, i.e. leave room for the delete column.
    auto* new_row = new wxBoxSizer(wxHORIZONTAL);
    new_row->Add(m_btn_new, 1, wxEXPAND);
    new_row->AddSpacer(FromDIP(8) + FromDIP(24));
    main->Add(new_row, 0, wxEXPAND | wxLEFT | wxRIGHT, side);

    main->AddSpacer(FromDIP(20));

    m_note = new Label(this, Label::Body_14,
        _L("Note: You can also delete or modify the newly added filaments in the drop-down list of the filament matching dialog."));
    m_note->SetForegroundColour(text_muted());
    m_note->SetBackgroundColour(dlg_bg());
    main->Add(m_note, 0, wxLEFT | wxRIGHT, side);

    main->AddSpacer(FromDIP(24));

    StateColor ok_bg(
        std::pair<wxColour, int>(wxColour(144, 144, 144), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
    StateColor ok_bd(
        std::pair<wxColour, int>(wxColour(144, 144, 144), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
    StateColor ok_text(std::pair<wxColour, int>(wxColour("#FFFFFE"), StateColor::Normal));

    m_btn_ok = new Button(this, _L("OK"));
    m_btn_ok->SetBackgroundColor(ok_bg);
    m_btn_ok->SetBorderColor(ok_bd);
    m_btn_ok->SetTextColor(ok_text);
    m_btn_ok->SetFont(Label::Body_14);
    m_btn_ok->SetMinSize(wxSize(FromDIP(72), FromDIP(32)));
    m_btn_ok->SetCornerRadius(FromDIP(16));
    m_btn_ok->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_OK); });

    m_btn_cancel = new Button(this, _L("Cancel"));
    m_btn_cancel->SetBackgroundColor(StateColor::darkModeColorFor(*wxWHITE));
    m_btn_cancel->SetBorderColor(StateColor::darkModeColorFor(wxColour("#CECECE")));
    m_btn_cancel->SetTextColor(text_dark());
    m_btn_cancel->SetFont(Label::Body_14);
    m_btn_cancel->SetMinSize(wxSize(FromDIP(72), FromDIP(32)));
    m_btn_cancel->SetCornerRadius(FromDIP(16));
    m_btn_cancel->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CANCEL); });

    auto* btn_row = new wxBoxSizer(wxHORIZONTAL);
    btn_row->AddStretchSpacer();
    btn_row->Add(m_btn_ok, 0);
    btn_row->AddSpacer(FromDIP(12));
    btn_row->Add(m_btn_cancel, 0);
    main->Add(btn_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, side);

    SetSizer(main);
    SetMinSize(wxSize(FromDIP(560), -1));
    update_buttons();
}

void MixSupplementFilamentDialog::add_row(const Row& row)
{
    RowUi ui;
    ui.tag = row.tag;

    // Same card as a physical filament row in the sidebar (Sidebar::init_filament_combo).
    ui.card = new StaticBox(this);
    ui.card->SetBackgroundColor(StateColor(std::make_pair(*wxWHITE, (int) StateColor::Normal)));
    ui.card->SetBackgroundColour(*wxWHITE);
    ui.card->SetCornerRadius(FromDIP(6));
    ui.card->SetBorderWidth(1);
    ui.card->SetBorderColor(StateColor(std::make_pair(*wxWHITE, (int) StateColor::Normal)));
    wxGetApp().UpdateDarkUIWin(ui.card);

    ui.combo = new VirtualFilamentComboBox(ui.card, row.preset_name, row.color_hex);
    ui.combo->SetCornerRadius(FromDIP(4));

    auto* card_sizer = new wxBoxSizer(wxHORIZONTAL);
    card_sizer->AddSpacer(FromDIP(10));
    card_sizer->Add(ui.combo->clr_picker, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
    card_sizer->Add(ui.combo, 1, wxALL | wxEXPAND, FromDIP(2))->SetMinSize({-1, FromDIP(30)});
    ui.card->SetSizer(card_sizer);

    StaticBox* card = ui.card;
    ui.combo->set_changed_callback([this, card]() {
        for (RowUi& r : m_rows)
            if (r.card == card)
                r.tag = -1;
    });

    ui.del_btn = new ScalableButton(this, wxID_ANY, "tree_delete");
    ui.del_btn->SetToolTip(_L("Delete"));
    ui.del_btn->SetMinSize(wxSize(FromDIP(24), FromDIP(24)));
    ui.del_btn->SetBackgroundColour(dlg_bg());
    // Destroying the button inside its own click handler is unsafe; defer the removal.
    ui.del_btn->Bind(wxEVT_BUTTON, [this, card](wxCommandEvent&) {
        CallAfter([this, card]() { remove_row(card); });
    });

    ui.sizer = new wxBoxSizer(wxHORIZONTAL);
    ui.sizer->Add(ui.card, 1, wxEXPAND);
    ui.sizer->AddSpacer(FromDIP(8));
    ui.sizer->Add(ui.del_btn, 0, wxALIGN_CENTER_VERTICAL);
    m_rows_sizer->Add(ui.sizer, 0, wxEXPAND | wxBOTTOM, FromDIP(8));

    m_rows.push_back(ui);
}

void MixSupplementFilamentDialog::remove_row(StaticBox* card)
{
    auto it = std::find_if(m_rows.begin(), m_rows.end(), [card](const RowUi& r) { return r.card == card; });
    if (it == m_rows.end())
        return;
    RowUi ui = *it;
    m_rows.erase(it);
    m_rows_sizer->Detach(ui.sizer);
    ui.sizer->Clear(false);
    delete ui.sizer;
    ui.card->Destroy();
    ui.del_btn->Destroy();
    update_buttons();
    relayout();
}

void MixSupplementFilamentDialog::on_new_filament()
{
    if (m_rows.size() >= m_max_rows)
        return;
    // Same defaults as the sidebar "+" button: next system color, preset copied from the last filament.
    Row row;
    row.preset_name = m_rows.empty() ? m_fallback_preset : m_rows.back().combo->get_selected_preset_name();
    row.color_hex   = next_distinct_color();
    add_row(row);
    wxGetApp().UpdateDlgDarkUI(this);
    update_buttons();
    relayout();
}

std::string MixSupplementFilamentDialog::next_distinct_color() const
{
    std::vector<wxColour> used;
    used.reserve(m_project_colors.size() + m_rows.size());
    for (const std::string& hex : m_project_colors)
        used.emplace_back(wxString::FromUTF8(hex));
    for (const RowUi& ui : m_rows)
        used.emplace_back(wxString::FromUTF8(ui.combo->get_color_hex()));

    // The system palette rotates globally; skip entries close to a color the project or this list already has.
    wxColour color;
    for (int i = 0; i < FILAMENT_SYSTEM_COLORS_NUM; ++i) {
        color = Plater::get_next_color_for_filament();
        const bool close = std::any_of(used.begin(), used.end(), [&color](const wxColour& c) {
            return c.IsOk() && colors_too_close(c, color);
        });
        if (!close)
            break;
    }
    return color.GetAsString(wxC2S_HTML_SYNTAX).ToStdString();
}

void MixSupplementFilamentDialog::update_buttons()
{
    if (m_btn_new)
        m_btn_new->Enable(m_rows.size() < m_max_rows);
    if (m_btn_ok)
        m_btn_ok->Enable(!m_rows.empty());
}

void MixSupplementFilamentDialog::relayout()
{
    const int wrap_w = FromDIP(560) - 2 * FromDIP(32);
    if (m_message)
        m_message->Wrap(wrap_w);
    if (m_note)
        m_note->Wrap(wrap_w);
    Layout();
    Fit();
    Refresh();
}

void MixSupplementFilamentDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    (void) suggested_rect;
    for (RowUi& ui : m_rows) {
        ui.combo->msw_rescale();
        ui.del_btn->msw_rescale();
    }
    for (Button* btn : {m_btn_new, m_btn_ok, m_btn_cancel})
        if (btn)
            btn->Rescale();
    SetMinSize(wxSize(FromDIP(560), -1));
    relayout();
}

void MixSupplementFilamentDialog::on_sys_color_changed()
{
    for (RowUi& ui : m_rows) {
        ui.combo->sys_color_changed();
        ui.del_btn->msw_rescale();
    }
    Refresh();
}

} // namespace GUI
} // namespace Slic3r
