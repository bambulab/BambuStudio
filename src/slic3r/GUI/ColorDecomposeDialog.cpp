#include "ColorDecomposeDialog.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <wx/sizer.h>
#include <wx/dcclient.h>
#include <wx/dcbuffer.h>
#include "wx/graphics.h"

#include "I18N.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/Label.hpp"
#include "wxExtensions.hpp"
#include "libslic3r/ColorDecomposeRecipe.hpp"

namespace Slic3r {
namespace GUI {

static const wxColour COLOR_BRAND("#00AE42");
static const wxColour COLOR_BORDER_NORMAL("#EEEEEE");
static const wxColour COLOR_BG_CARD("#F8F8F8");
static const wxColour COLOR_LABEL_GREY("#ACACAC");
static const wxColour COLOR_TEXT_DARK("#262E30");
static const wxColour COLOR_TITLE_TEXT("#2B3436");
static const wxColour COLOR_DIVIDER("#EEEEEE");

// Standard CMYW base colors
static const wxColour CMYW_CYAN(0, 255, 255);
static const wxColour CMYW_MAGENTA(255, 0, 255);
static const wxColour CMYW_YELLOW(255, 255, 0);
static const wxColour CMYW_WHITE(255, 255, 255);

// Standard RYBW base colors
static const wxColour RYBW_RED(255, 0, 0);
static const wxColour RYBW_YELLOW(255, 255, 0);
static const wxColour RYBW_BLUE(0, 0, 255);
static const wxColour RYBW_WHITE(255, 255, 255);

static size_t mode_index(DecomposeMode mode)
{
    return static_cast<size_t>(mode);
}

static ColorDecomposeRgb wx_colour_to_recipe_rgb(const wxColour& color)
{
    return {
        static_cast<unsigned char>(color.Red()),
        static_cast<unsigned char>(color.Green()),
        static_cast<unsigned char>(color.Blue())
    };
}

static wxColour hex_to_wx_colour(const std::string& hex, const wxColour& fallback)
{
    wxColour color(hex);
    return color.IsOk() ? color : fallback;
}

static bool same_rgb(const wxColour& lhs, const wxColour& rhs)
{
    return lhs.Red() == rhs.Red() && lhs.Green() == rhs.Green() && lhs.Blue() == rhs.Blue();
}

static DecomposeBaseColor standard_base_color_from_key(const std::string& key)
{
    if (key == "Cyan")    return DecomposeBaseColor::Cyan;
    if (key == "Magenta") return DecomposeBaseColor::Magenta;
    if (key == "Yellow")  return DecomposeBaseColor::Yellow;
    if (key == "White")   return DecomposeBaseColor::White;
    if (key == "Red")     return DecomposeBaseColor::Red;
    if (key == "Green")   return DecomposeBaseColor::Green;
    if (key == "Blue")    return DecomposeBaseColor::Blue;
    return DecomposeBaseColor::None;
}

static DecomposeBaseColor standard_base_color_for(DecomposeMode mode, const wxColour& color)
{
    if (mode == DecomposeMode::CMYW) {
        if (same_rgb(color, CMYW_CYAN))    return DecomposeBaseColor::Cyan;
        if (same_rgb(color, CMYW_MAGENTA)) return DecomposeBaseColor::Magenta;
        if (same_rgb(color, CMYW_YELLOW))  return DecomposeBaseColor::Yellow;
        if (same_rgb(color, CMYW_WHITE))   return DecomposeBaseColor::White;
    } else if (mode == DecomposeMode::RYBW) {
        if (same_rgb(color, RYBW_RED))     return DecomposeBaseColor::Red;
        if (same_rgb(color, RYBW_YELLOW))  return DecomposeBaseColor::Yellow;
        if (same_rgb(color, RYBW_BLUE))    return DecomposeBaseColor::Blue;
        if (same_rgb(color, RYBW_WHITE))   return DecomposeBaseColor::White;
    }
    return DecomposeBaseColor::None;
}

static ColorDecomposeResult to_dialog_result(const ColorDecomposeRecipeResult& recipe,
                                             const wxColour& fallback)
{
    ColorDecomposeResult result;
    result.mode = recipe.mode;
    result.matched_color = hex_to_wx_colour(recipe.matched_color_hex, fallback);
    for (const auto& comp_recipe : recipe.components) {
        DecomposeComponent comp;
        comp.colour = hex_to_wx_colour(comp_recipe.color_hex, fallback);
        comp.ratio = comp_recipe.ratio;
        comp.filament_index = static_cast<int>(comp_recipe.filament_index);
        comp.base_color = standard_base_color_from_key(comp_recipe.base_color);
        if (comp.base_color == DecomposeBaseColor::None)
            comp.base_color = standard_base_color_for(recipe.mode, comp.colour);
        result.components.push_back(comp);
    }
    return result;
}

static wxPanel* create_h_divider(wxWindow* parent, int fixed_width = -1)
{
    const int h = parent->FromDIP(1);
    int w = fixed_width > 0 ? fixed_width : -1;
    auto* panel = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(w, h));
    panel->SetMinSize(wxSize(w, h));
    if (fixed_width > 0)
        panel->SetMaxSize(wxSize(fixed_width, h));
    panel->SetBackgroundColour(StateColor::darkModeColorFor(COLOR_DIVIDER));
    return panel;
}

static wxStaticText* create_mode_group_label(wxWindow* parent, const wxString& text)
{
    auto* label = new wxStaticText(parent, wxID_ANY, text);
    label->SetFont(Label::Body_11);
    label->SetForegroundColour(StateColor::darkModeColorFor(COLOR_LABEL_GREY));
    return label;
}

static void match_parent_bg(wxWindow* w, const wxColour& bg)
{
    w->SetBackgroundColour(bg);
}


ColorDecomposeDialog::ColorDecomposeDialog(wxWindow* parent,
                                           int filament_idx,
                                           const wxColour& target_color,
                                           const std::vector<std::string>& physical_colors,
                                           const std::vector<std::string>& filament_names,
                                           const std::vector<std::string>& filament_types)
    : DPIDialog(parent, wxID_ANY, wxEmptyString, wxDefaultPosition,
                wxDefaultSize, wxBORDER_SIMPLE)
    , m_filament_idx(filament_idx)
    , m_target_color(target_color)
    , m_physical_colors(physical_colors)
    , m_filament_names(filament_names)
    , m_filament_types(filament_types)
{
    for (const auto& t : m_filament_types) {
        if (std::find(m_project_types.begin(), m_project_types.end(), t) == m_project_types.end())
            m_project_types.push_back(t);
    }

    if (m_filament_idx >= 0 && static_cast<size_t>(m_filament_idx) < m_filament_types.size())
        m_preferred_type = m_filament_types[m_filament_idx];
    else if (!m_project_types.empty())
        m_preferred_type = m_project_types.front();

    build_ui();
    wxGetApp().UpdateDlgDarkUI(this);

    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
        if (m_title_panel && m_title_panel->HasCapture())
            m_title_panel->ReleaseMouse();
        event.Skip();
    });

    compute_decomposition();
    update_matched_color_display();
}

void ColorDecomposeDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    (void)suggested_rect;
    Fit();
    Refresh();
}

wxBoxSizer* ColorDecomposeDialog::create_title_bar()
{
    m_title_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, FromDIP(36)));
    m_title_panel->SetMinSize(wxSize(-1, FromDIP(36)));
    m_title_panel->SetMaxSize(wxSize(-1, FromDIP(36)));
    m_title_panel->SetBackgroundStyle(wxBG_STYLE_PAINT);
    m_title_text = _L("Decompose Color");
    m_title_panel->Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
        wxBufferedPaintDC dc(m_title_panel);
        wxSize sz = m_title_panel->GetClientSize();
        wxColour grad_start = StateColor::darkModeColorFor(wxColour(255, 255, 255));
        wxColour grad_end   = StateColor::darkModeColorFor(wxColour(0xE9, 0xE9, 0xE9));
        dc.GradientFillLinear(wxRect(0, 0, sz.x, sz.y), grad_start, grad_end, wxBOTTOM);
        dc.SetFont(Label::Body_14);
        dc.SetTextForeground(StateColor::darkModeColorFor(COLOR_TITLE_TEXT));
        dc.SetBackgroundMode(wxTRANSPARENT);
        dc.DrawText(m_title_text, FromDIP(19), (sz.y - dc.GetTextExtent(m_title_text).y) / 2);
    });
    m_title_panel->Bind(wxEVT_LEFT_DOWN, &ColorDecomposeDialog::on_title_mouse_down, this);
    m_title_panel->Bind(wxEVT_MOTION, &ColorDecomposeDialog::on_title_mouse_move, this);
    m_title_panel->Bind(wxEVT_LEFT_UP, &ColorDecomposeDialog::on_title_mouse_up, this);

    auto* outer = new wxBoxSizer(wxVERTICAL);
    outer->Add(m_title_panel, 0, wxEXPAND);
    return outer;
}

void ColorDecomposeDialog::build_ui()
{
    SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));

    auto* main_sizer = new wxBoxSizer(wxVERTICAL);

    const int selector_side_margin = FromDIP(26);
    const int selector_top_gap = FromDIP(22);
    const int content_side_margin = FromDIP(30);
    const int target_section_top_gap = FromDIP(18);

    main_sizer->Add(create_title_bar(), 0, wxEXPAND);
    main_sizer->AddSpacer(selector_top_gap);
    main_sizer->Add(create_filament_selector(), 0, wxEXPAND | wxLEFT | wxRIGHT, selector_side_margin);
    main_sizer->AddSpacer(target_section_top_gap);
    main_sizer->Add(create_target_color_section(), 0, wxEXPAND | wxLEFT | wxRIGHT, content_side_margin);
    main_sizer->AddSpacer(FromDIP(16));
    main_sizer->Add(create_h_divider(this), 0, wxEXPAND | wxLEFT | wxRIGHT, content_side_margin);
    main_sizer->AddSpacer(FromDIP(16));
    main_sizer->Add(create_mode_selection_section(), 0, wxEXPAND | wxLEFT | wxRIGHT, content_side_margin);
    main_sizer->AddSpacer(FromDIP(16));
    main_sizer->Add(create_button_panel(), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, content_side_margin);

    SetSizer(main_sizer);
    SetMinSize(wxSize(FromDIP(477), FromDIP(380)));
    Fit();
    CenterOnParent();
}

wxBoxSizer* ColorDecomposeDialog::create_filament_selector()
{
    auto* sizer = new wxBoxSizer(wxHORIZONTAL);

    m_type_combo = new ComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                wxSize(-1, FromDIP(36)), 0, nullptr, wxCB_READONLY);
    m_type_combo->SetFont(Label::Body_13);

    int default_sel = 0;
    for (size_t i = 0; i < m_project_types.size(); ++i) {
        m_type_combo->Append(wxString::FromUTF8(m_project_types[i]));
        if (m_project_types[i] == m_preferred_type)
            default_sel = static_cast<int>(i);
    }
    if (!m_project_types.empty())
        m_type_combo->SetSelection(default_sel);

    m_type_combo->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent& evt) {
        evt.StopPropagation();
        int sel = m_type_combo->GetSelection();
        if (sel >= 0 && static_cast<size_t>(sel) < m_project_types.size())
            m_preferred_type = m_project_types[sel];
        compute_decomposition();
        update_matched_color_display();
    });

    sizer->Add(m_type_combo, 1, wxEXPAND);
    return sizer;
}

static wxPanel* create_color_swatch(wxWindow* parent, const wxColour& color, int size)
{
    auto* panel = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(size, size));
    panel->SetBackgroundColour(color);
    panel->SetMinSize(wxSize(size, size));
    return panel;
}

wxBoxSizer* ColorDecomposeDialog::create_target_color_section()
{
    auto* sizer = new wxBoxSizer(wxHORIZONTAL);

    auto* label = new wxStaticText(this, wxID_ANY, _L("Target Color"));
    label->SetFont(Label::Head_14);
    label->SetForegroundColour(StateColor::darkModeColorFor(COLOR_TEXT_DARK));
    sizer->Add(label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(19));

    m_target_swatch = create_color_swatch(this, m_target_color, FromDIP(28));
    sizer->Add(m_target_swatch, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));

    m_target_rgb_text = new wxStaticText(this, wxID_ANY,
        wxString::Format("RGB: %d, %d, %d", m_target_color.Red(), m_target_color.Green(), m_target_color.Blue()));
    m_target_rgb_text->SetFont(Label::Body_13);
    m_target_rgb_text->SetForegroundColour(StateColor::darkModeColorFor(COLOR_TEXT_DARK));
    sizer->Add(m_target_rgb_text, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));

    auto* arrow_text = new wxStaticText(this, wxID_ANY, wxString::FromUTF8("\xe2\x86\x92"));
    arrow_text->SetForegroundColour(StateColor::darkModeColorFor(COLOR_TEXT_DARK));
    sizer->Add(arrow_text, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));

    m_matched_swatch = create_color_swatch(this, m_target_color, FromDIP(28));
    sizer->Add(m_matched_swatch, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));

    m_matched_rgb_text = new wxStaticText(this, wxID_ANY,
        wxString::Format("RGB: %d, %d, %d", m_target_color.Red(), m_target_color.Green(), m_target_color.Blue()));
    m_matched_rgb_text->SetFont(Label::Head_13);
    m_matched_rgb_text->SetForegroundColour(StateColor::darkModeColorFor(COLOR_TEXT_DARK));
    sizer->Add(m_matched_rgb_text, 0, wxALIGN_CENTER_VERTICAL);

    return sizer;
}

wxPanel* ColorDecomposeDialog::create_mode_card(wxWindow* parent, DecomposeMode mode,
                                                 const wxString& title)
{
    const int pad       = FromDIP(12);

    auto* card = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    card->SetBackgroundStyle(wxBG_STYLE_PAINT);

    auto* card_sizer = new wxBoxSizer(wxVERTICAL);

    auto* title_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto* title_label = new wxStaticText(card, wxID_ANY, title);
    title_label->SetFont(Label::Body_14);
    title_label->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#909090")));
    match_parent_bg(title_label, StateColor::darkModeColorFor(COLOR_BG_CARD));
    title_sizer->Add(title_label, 1, wxALIGN_CENTER_VERTICAL);

    auto* chk = new ::CheckBox(card);
    chk->SetValue(mode == m_selected_mode);
    match_parent_bg(chk, StateColor::darkModeColorFor(COLOR_BG_CARD));
    switch (mode) {
    case DecomposeMode::MaterialList: m_chk_material_list = chk; break;
    case DecomposeMode::CMYW:         m_chk_cmyw = chk; break;
    case DecomposeMode::RYBW:         m_chk_rybw = chk; break;
    }
    chk->Bind(wxEVT_TOGGLEBUTTON, [this, mode](wxCommandEvent&) {
        select_mode(mode);
    });
    title_sizer->Add(chk, 0, wxALIGN_CENTER_VERTICAL);

    card_sizer->Add(title_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);

    card_sizer->Add(create_h_divider(card), 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));

    auto* colors_sizer = new wxBoxSizer(wxHORIZONTAL);
    card_sizer->Add(colors_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, pad);

    auto& controls = m_mode_cards[mode_index(mode)];
    controls.card = card;
    controls.components_sizer = colors_sizer;

    card->SetSizer(card_sizer);
    card->SetMinSize(wxSize(FromDIP(128), FromDIP(111)));
    card->SetMaxSize(wxSize(FromDIP(128), FromDIP(111)));

    card->Bind(wxEVT_PAINT, [this, card, mode](wxPaintEvent&) {
        wxBufferedPaintDC dc(card);
        wxSize sz = card->GetClientSize();
        dc.SetBackground(wxBrush(StateColor::darkModeColorFor(*wxWHITE)));
        dc.Clear();

        bool selected = (m_selected_mode == mode);
        wxColour border_col = selected
            ? StateColor::darkModeColorFor(COLOR_BRAND)
            : StateColor::darkModeColorFor(COLOR_BORDER_NORMAL);
        const int border_width = FromDIP(selected ? 2 : 1);
        const double inset = border_width / 2.0;
        std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
        if (gc) {
            gc->SetPen(wxPen(border_col, border_width));
            gc->SetBrush(wxBrush(StateColor::darkModeColorFor(COLOR_BG_CARD)));
            gc->DrawRoundedRectangle(inset, inset, sz.x - 2 * inset, sz.y - 2 * inset, FromDIP(8));
        } else {
            const int fallback_inset = (border_width + 1) / 2;
            dc.SetPen(wxPen(border_col, border_width));
            dc.SetBrush(wxBrush(StateColor::darkModeColorFor(COLOR_BG_CARD)));
            dc.DrawRoundedRectangle(fallback_inset, fallback_inset, sz.x - 2 * fallback_inset, sz.y - 2 * fallback_inset, FromDIP(8));
        }
    });

    std::function<void(wxWindow*)> bind_click;
    bind_click = [this, mode, chk, &bind_click](wxWindow* w) {
        if (w == chk || dynamic_cast<::CheckBox*>(w))
            return;
        w->Bind(wxEVT_LEFT_UP, [this, mode](wxMouseEvent&) {
            select_mode(mode);
        });
        w->SetCursor(wxCursor(wxCURSOR_HAND));
        for (auto* child : w->GetChildren())
            bind_click(child);
    };
    bind_click(card);

    return card;
}

wxBoxSizer* ColorDecomposeDialog::create_mode_selection_section()
{
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    auto* section_label = new wxStaticText(this, wxID_ANY, _L("Select Color Decomposition"));
    section_label->SetFont(Label::Head_14);
    section_label->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#1F1F1F")));
    sizer->Add(section_label, 0, wxBOTTOM, FromDIP(4));

    auto* modes_sizer = new wxBoxSizer(wxHORIZONTAL);

    // --- Arbitrary mode column ---
    auto* arb_col = new wxBoxSizer(wxVERTICAL);
    {
        auto* arb_header_sizer = new wxBoxSizer(wxHORIZONTAL);
        arb_header_sizer->Add(create_mode_group_label(this, _L("Arbitrary Mode")),
                              0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(5));
        arb_header_sizer->Add(create_h_divider(this, FromDIP(88)), 0, wxALIGN_CENTER_VERTICAL);
        arb_col->Add(arb_header_sizer, 0, wxEXPAND | wxBOTTOM, FromDIP(8));

        m_card_material_list = create_mode_card(this, DecomposeMode::MaterialList,
            _L("Material List"));
        arb_col->Add(m_card_material_list, 0, wxEXPAND);
    }
    modes_sizer->Add(arb_col, 0, wxEXPAND | wxRIGHT, FromDIP(16));

    // --- Standard mode column ---
    auto* std_col = new wxBoxSizer(wxVERTICAL);
    {
        auto* std_header_sizer = new wxBoxSizer(wxHORIZONTAL);
        std_header_sizer->Add(create_mode_group_label(this, _L("Standard Mode")),
                              0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(5));
        std_header_sizer->Add(create_h_divider(this), 1, wxALIGN_CENTER_VERTICAL);
        std_col->Add(std_header_sizer, 0, wxEXPAND | wxBOTTOM, FromDIP(8));

        auto* cards_sizer = new wxBoxSizer(wxHORIZONTAL);

        m_card_cmyw = create_mode_card(this, DecomposeMode::CMYW, "CMYW");
        cards_sizer->Add(m_card_cmyw, 0, wxRIGHT, FromDIP(12));

        m_card_rybw = create_mode_card(this, DecomposeMode::RYBW, "RYBW");
        cards_sizer->Add(m_card_rybw, 0);

        std_col->Add(cards_sizer, 0, wxEXPAND);
    }
    modes_sizer->Add(std_col, 0, wxEXPAND);

    sizer->Add(modes_sizer, 0, wxEXPAND);
    return sizer;
}

wxBoxSizer* ColorDecomposeDialog::create_button_panel()
{
    auto* sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->AddStretchSpacer();

    m_btn_cancel = new Button(this, _L("Cancel"));
    m_btn_cancel->SetBackgroundColor(StateColor::darkModeColorFor(*wxWHITE));
    m_btn_cancel->SetBorderColor(wxColour("#CECECE"));
    m_btn_cancel->SetTextColor(wxColour("#262E30"));
    m_btn_cancel->SetMinSize(wxSize(FromDIP(55), FromDIP(24)));
    m_btn_cancel->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CANCEL); });

    m_btn_ok = new Button(this, _L("OK"));
    m_btn_ok->SetBackgroundColor(wxColour("#00AE42"));
    m_btn_ok->SetBorderColor(wxColour("#00AE42"));
    m_btn_ok->SetTextColor(*wxWHITE);
    m_btn_ok->SetMinSize(wxSize(FromDIP(55), FromDIP(24)));
    m_btn_ok->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        EndModal(wxID_OK);
    });

    sizer->Add(m_btn_cancel, 0, wxRIGHT, FromDIP(12));
    sizer->Add(m_btn_ok, 0);

    return sizer;
}

void ColorDecomposeDialog::select_mode(DecomposeMode mode)
{
    m_selected_mode = mode;
    m_result = m_mode_results[mode_index(mode)];
    update_card_styles();
    update_matched_color_display();
}

void ColorDecomposeDialog::update_card_styles()
{
    if (m_card_material_list) m_card_material_list->Refresh();
    if (m_card_cmyw)          m_card_cmyw->Refresh();
    if (m_card_rybw)          m_card_rybw->Refresh();

    if (m_chk_material_list)
        m_chk_material_list->SetValue(m_selected_mode == DecomposeMode::MaterialList);
    if (m_chk_cmyw)
        m_chk_cmyw->SetValue(m_selected_mode == DecomposeMode::CMYW);
    if (m_chk_rybw)
        m_chk_rybw->SetValue(m_selected_mode == DecomposeMode::RYBW);
}

void ColorDecomposeDialog::update_mode_card_content(DecomposeMode mode)
{
    auto& controls = m_mode_cards[mode_index(mode)];
    auto* sizer = controls.components_sizer;
    auto* card = controls.card;
    if (!sizer || !card)
        return;

    sizer->Clear(true);
    const auto& components = m_mode_results[mode_index(mode)].components;
    const size_t count = components.size();
    if (count == 0) {
        card->Layout();
        card->Refresh();
        return;
    }

    const int swatch_sz = FromDIP(24);
    const int plus_gap  = FromDIP(24);
    const wxFont& ratio_font = Label::Body_13;
    auto bind_select = [this, mode](wxWindow* w) {
        w->Bind(wxEVT_LEFT_UP, [this, mode](wxMouseEvent&) {
            select_mode(mode);
        });
        w->SetCursor(wxCursor(wxCURSOR_HAND));
    };

    for (size_t i = 0; i < count; ++i) {
        auto* col = new wxBoxSizer(wxVERTICAL);
        auto* swatch = create_color_swatch(card, components[i].colour, swatch_sz);
        bind_select(swatch);
        col->Add(swatch, 0, wxALIGN_CENTER_HORIZONTAL);
        auto* ratio_text = new wxStaticText(card, wxID_ANY, wxString::Format("%d%%", components[i].ratio));
        ratio_text->SetFont(ratio_font);
        ratio_text->SetForegroundColour(StateColor::darkModeColorFor(COLOR_TEXT_DARK));
        match_parent_bg(ratio_text, StateColor::darkModeColorFor(COLOR_BG_CARD));
        bind_select(ratio_text);
        col->Add(ratio_text, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(4));
        sizer->Add(col, 0, wxALIGN_TOP);

        if (i + 1 < count) {
            sizer->AddStretchSpacer();
            auto* plus_panel = new wxPanel(card, wxID_ANY, wxDefaultPosition, wxSize(plus_gap, swatch_sz));
            plus_panel->SetMinSize(wxSize(plus_gap, swatch_sz));
            plus_panel->SetMaxSize(wxSize(plus_gap, swatch_sz));
            plus_panel->SetBackgroundColour(StateColor::darkModeColorFor(COLOR_BG_CARD));
            auto* plus_sizer = new wxBoxSizer(wxVERTICAL);
            auto* plus_label = new wxStaticText(plus_panel, wxID_ANY, "+");
            plus_label->SetFont(Label::Body_13);
            plus_label->SetForegroundColour(StateColor::darkModeColorFor(COLOR_TEXT_DARK));
            match_parent_bg(plus_label, StateColor::darkModeColorFor(COLOR_BG_CARD));
            bind_select(plus_panel);
            bind_select(plus_label);
            plus_sizer->AddStretchSpacer();
            plus_sizer->Add(plus_label, 0, wxALIGN_CENTER_HORIZONTAL);
            plus_sizer->AddStretchSpacer();
            plus_panel->SetSizer(plus_sizer);
            sizer->Add(plus_panel, 0, wxALIGN_TOP);
            sizer->AddStretchSpacer();
        }
    }

    const int card_width = FromDIP(128 + (count > 2 ? static_cast<int>(count - 2) * 31 : 0));
    card->SetMinSize(wxSize(card_width, FromDIP(111)));
    card->SetMaxSize(wxSize(card_width, FromDIP(111)));

    card->Layout();
    card->Refresh();
}

void ColorDecomposeDialog::update_mode_card_contents()
{
    update_mode_card_content(DecomposeMode::MaterialList);
    update_mode_card_content(DecomposeMode::CMYW);
    update_mode_card_content(DecomposeMode::RYBW);
    Layout();
    Fit();
}

void ColorDecomposeDialog::update_matched_color_display()
{
    if (!m_result.matched_color.IsOk())
        m_result.matched_color = m_target_color;

    if (m_matched_swatch) {
        m_matched_swatch->SetBackgroundColour(m_result.matched_color);
        m_matched_swatch->Refresh();
    }
    if (m_matched_rgb_text) {
        m_matched_rgb_text->SetLabel(wxString::Format("RGB: %d, %d, %d",
            m_result.matched_color.Red(), m_result.matched_color.Green(), m_result.matched_color.Blue()));
    }
}

void ColorDecomposeDialog::compute_decomposition()
{
    auto fallback_result = [this](DecomposeMode mode, const std::vector<DecomposeComponent>& components) {
        ColorDecomposeResult result;
        result.mode = mode;
        result.components = components;
        int total = 0;
        double r = 0.0, g = 0.0, b = 0.0;
        for (const auto& comp : result.components)
            total += comp.ratio;
        if (total <= 0)
            total = 100;
        for (const auto& comp : result.components) {
            const double w = static_cast<double>(comp.ratio) / total;
            r += comp.colour.Red() * w;
            g += comp.colour.Green() * w;
            b += comp.colour.Blue() * w;
        }
        result.matched_color = result.components.empty()
            ? m_target_color
            : wxColour(static_cast<unsigned char>(std::clamp(r, 0.0, 255.0)),
                       static_cast<unsigned char>(std::clamp(g, 0.0, 255.0)),
                       static_cast<unsigned char>(std::clamp(b, 0.0, 255.0)));
        return result;
    };

    std::vector<ColorDecomposePhysicalFilament> physical_filaments;
    physical_filaments.reserve(m_physical_colors.size());
    for (size_t i = 0; i < m_physical_colors.size(); ++i) {
        if (m_filament_idx >= 0 && i == static_cast<size_t>(m_filament_idx))
            continue;
        ColorDecomposePhysicalFilament filament;
        filament.color_hex = m_physical_colors[i];
        filament.name = i < m_filament_names.size() ? m_filament_names[i] : "";
        filament.type = i < m_filament_types.size() ? m_filament_types[i] : "";
        filament.filament_index = static_cast<unsigned int>(i + 1);
        physical_filaments.push_back(std::move(filament));
    }

    const ColorDecomposeRgb target_rgb = wx_colour_to_recipe_rgb(m_target_color);

    auto material_recipe = recommend_from_physical_filaments(target_rgb, physical_filaments, m_preferred_type);
    if (material_recipe.valid) {
        m_mode_results[mode_index(DecomposeMode::MaterialList)] =
            to_dialog_result(material_recipe, m_target_color);
    } else {
        std::vector<DecomposeComponent> components;
        for (size_t i = 0; i < std::min<size_t>(2, physical_filaments.size()); ++i) {
            DecomposeComponent comp;
            comp.colour = wxColour(physical_filaments[i].color_hex);
            comp.ratio = 50;
            comp.filament_index = static_cast<int>(physical_filaments[i].filament_index);
            components.push_back(comp);
        }
        if (components.empty()) {
            components.push_back({m_target_color, 100, -1});
        } else if (components.size() == 1) {
            components.front().ratio = 100;
        }
        m_mode_results[mode_index(DecomposeMode::MaterialList)] =
            fallback_result(DecomposeMode::MaterialList, components);
    }

    auto cmyw_recipe = lookup_standard_recipe(target_rgb, ColorDecomposeRecipeMode::CMYW, m_preferred_type);
    m_mode_results[mode_index(DecomposeMode::CMYW)] = cmyw_recipe.valid
        ? to_dialog_result(cmyw_recipe, m_target_color)
        : fallback_result(DecomposeMode::CMYW, {
            {CMYW_YELLOW, 50, -1, DecomposeBaseColor::Yellow},
            {CMYW_CYAN,   50, -1, DecomposeBaseColor::Cyan}
        });

    auto rybw_recipe = lookup_standard_recipe(target_rgb, ColorDecomposeRecipeMode::RYBW, m_preferred_type);
    m_mode_results[mode_index(DecomposeMode::RYBW)] = rybw_recipe.valid
        ? to_dialog_result(rybw_recipe, m_target_color)
        : fallback_result(DecomposeMode::RYBW, {
            {RYBW_YELLOW, 50, -1, DecomposeBaseColor::Yellow},
            {RYBW_BLUE,   50, -1, DecomposeBaseColor::Blue}
        });

    m_result = m_mode_results[mode_index(m_selected_mode)];
    update_mode_card_contents();
}

void ColorDecomposeDialog::on_title_mouse_down(wxMouseEvent& event)
{
    if (m_title_panel->HasCapture())
        m_title_panel->ReleaseMouse();

    m_title_panel->CaptureMouse();
    wxPoint pt = m_title_panel->ClientToScreen(event.GetPosition());
    wxPoint origin = GetPosition();
    m_drag_delta = wxPoint(pt.x - origin.x, pt.y - origin.y);
}

void ColorDecomposeDialog::on_title_mouse_move(wxMouseEvent& event)
{
    if (event.Dragging() && event.LeftIsDown() && m_title_panel->HasCapture()) {
        wxPoint pt = m_title_panel->ClientToScreen(event.GetPosition());
        Move(wxPoint(pt.x - m_drag_delta.x, pt.y - m_drag_delta.y));
    }
    event.Skip();
}

void ColorDecomposeDialog::on_title_mouse_up(wxMouseEvent& event)
{
    if (m_title_panel->HasCapture())
        m_title_panel->ReleaseMouse();
    event.Skip();
}

} // namespace GUI
} // namespace Slic3r
