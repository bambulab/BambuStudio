#include "ColorDecomposeDialog.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <wx/sizer.h>
#include <wx/dc.h>
#include <wx/dcclient.h>
#include <wx/dcbuffer.h>
#include <wx/dcmemory.h>
#include "wx/graphics.h"

#include "I18N.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "format.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/DropDown.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/Label.hpp"
#include "wxExtensions.hpp"
#include "slic3r/Utils/WxFontUtils.hpp"
#include "ColorDecomposeSupport.hpp"
#include "libslic3r/ColorDecomposeRecipe.hpp"

namespace Slic3r {
namespace GUI {

static const wxColour COLOR_BRAND("#00AE42");
static const wxColour COLOR_BORDER_NORMAL("#EEEEEE");
static const wxColour COLOR_BG_CARD("#F8F8F8");
static const wxColour COLOR_LABEL_GREY("#ACACAC");
static const wxColour COLOR_TEXT_DARK("#262E30");
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

static wxColour pure_color_for_base(DecomposeBaseColor base)
{
    switch (base) {
    case DecomposeBaseColor::Cyan:    return CMYW_CYAN;
    case DecomposeBaseColor::Magenta: return CMYW_MAGENTA;
    case DecomposeBaseColor::Yellow:  return CMYW_YELLOW;
    case DecomposeBaseColor::White:   return CMYW_WHITE;
    case DecomposeBaseColor::Red:     return RYBW_RED;
    case DecomposeBaseColor::Blue:    return RYBW_BLUE;
    default:                          return *wxBLACK;
    }
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

static wxPanel* create_v_divider(wxWindow* parent)
{
    const int w = parent->FromDIP(1);
    auto* panel = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(w, -1));
    panel->SetMinSize(wxSize(w, parent->FromDIP(24)));
    panel->SetBackgroundColour(StateColor::darkModeColorFor(COLOR_DIVIDER));
    return panel;
}

static wxPanel* create_result_arrow(wxWindow* parent)
{
    const int w = parent->FromDIP(34);
    const int h = parent->FromDIP(12);
    auto* panel = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(w, h), wxBORDER_NONE);
    panel->SetMinSize(wxSize(w, h));
    panel->SetMaxSize(wxSize(w, h));
    panel->SetBackgroundStyle(wxBG_STYLE_PAINT);
    panel->Bind(wxEVT_PAINT, [panel](wxPaintEvent&) {
        wxPaintDC dc(panel);
        const wxSize sz = panel->GetClientSize();
        wxWindow* host = panel->GetParent();
        dc.SetBackground(wxBrush(host ? host->GetBackgroundColour() : *wxWHITE));
        dc.Clear();
        const wxColour c = StateColor::darkModeColorFor(COLOR_LABEL_GREY);
        const int pen_w = std::max(1, panel->FromDIP(1));
        dc.SetPen(wxPen(c, pen_w));
        const int y = sz.y / 2;
        const int head = panel->FromDIP(4);
        dc.DrawLine(0, y, sz.x - 1 - head / 2, y);
        dc.DrawLine(sz.x - 1, y, sz.x - 1 - head, y - head);
        dc.DrawLine(sz.x - 1, y, sz.x - 1 - head, y + head);
    });
    return panel;
}

static wxString format_rgb(const wxColour& color)
{
    return wxString::Format(_L("RGB(%d, %d, %d)"), color.Red(), color.Green(), color.Blue());
}

static void bind_rounded_card_paint(wxPanel* card)
{
    card->SetBackgroundStyle(wxBG_STYLE_PAINT);
    card->Bind(wxEVT_PAINT, [card](wxPaintEvent&) {
        wxBufferedPaintDC dc(card);
        wxSize sz = card->GetClientSize();
        dc.SetBackground(wxBrush(StateColor::darkModeColorFor(*wxWHITE)));
        dc.Clear();
        wxColour border_col = StateColor::darkModeColorFor(COLOR_BORDER_NORMAL);
        const int border_width = card->FromDIP(1);
        const double inset = border_width / 2.0;
        std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
        if (gc) {
            gc->SetPen(wxPen(border_col, border_width));
            gc->SetBrush(wxBrush(StateColor::darkModeColorFor(*wxWHITE)));
            gc->DrawRoundedRectangle(inset, inset, sz.x - 2 * inset, sz.y - 2 * inset, card->FromDIP(8));
        } else {
            dc.SetPen(wxPen(border_col, border_width));
            dc.SetBrush(wxBrush(StateColor::darkModeColorFor(*wxWHITE)));
            dc.DrawRoundedRectangle(0, 0, sz.x, sz.y, card->FromDIP(8));
        }
    });
}

static wxPanel* create_rounded_result_card(wxWindow* parent)
{
    auto* card = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    bind_rounded_card_paint(card);
    return card;
}

static wxStaticText* create_result_caption(wxWindow* parent, const wxString& text)
{
    auto* label = new wxStaticText(parent, wxID_ANY, text);
    label->SetFont(Label::Body_12);
    label->SetForegroundColour(StateColor::darkModeColorFor(COLOR_LABEL_GREY));
    return label;
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

// "PLA Basic" / "PLA Silk" / "PLA" -> "PLA"; "PETG Basic" -> "PETG"
static std::string filament_family(const std::string& type)
{
    if (type.empty())
        return {};
    const auto space = type.find(' ');
    if (space == std::string::npos)
        return type;
    return type.substr(0, space);
}

static bool family_matches(const std::string& type, const std::string& family)
{
    return !family.empty() && filament_family(type) == family;
}

static wxString family_display_label(const std::string& family)
{
    return wxString::FromUTF8(std::string(kDecomposeBambuPresetPrefix) + family);
}


ColorDecomposeDialog::ColorDecomposeDialog(wxWindow* parent,
                                           int filament_idx,
                                           const wxColour& target_color,
                                           const std::vector<std::string>& physical_colors,
                                           const std::vector<std::string>& filament_names,
                                           const std::vector<std::string>& filament_types,
                                           size_t current_filament_count,
                                           size_t max_filament_count,
                                           std::vector<size_t> physical_config_indices)
    : DPIDialog(parent, wxID_ANY, _L("Decompose Color"), wxDefaultPosition,
                wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
    , m_filament_idx(filament_idx)
    , m_target_color(target_color)
    , m_physical_colors(physical_colors)
    , m_filament_names(filament_names)
    , m_filament_types(filament_types)
    , m_current_filament_count(current_filament_count)
    , m_max_filament_count(max_filament_count)
    , m_physical_config_indices(std::move(physical_config_indices))
{
    if (m_filament_idx >= 0 && static_cast<size_t>(m_filament_idx) < m_filament_types.size())
        m_preferred_family = filament_family(m_filament_types[m_filament_idx]);
    if (m_preferred_family.empty()) {
        for (const auto& t : m_filament_types) {
            m_preferred_family = filament_family(t);
            if (!m_preferred_family.empty())
                break;
        }
    }

    build_ui();
    wxGetApp().UpdateDlgDarkUI(this);

    update_card_visibility();
    Fit();
    compute_decomposition();
    update_matched_color_display();
    update_ok_button_state();
}

void ColorDecomposeDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    (void)suggested_rect;
    update_matched_color_display();
    update_mode_card_contents();
    Fit();
    Refresh();
}

void ColorDecomposeDialog::build_ui()
{
    SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));

    auto* main_sizer = new wxBoxSizer(wxVERTICAL);

    const int selector_side_margin = FromDIP(26);
    const int selector_top_gap = FromDIP(22);
    const int content_side_margin = FromDIP(30);

    main_sizer->AddSpacer(selector_top_gap);
    main_sizer->Add(create_filament_selector(), 0, wxEXPAND | wxLEFT | wxRIGHT, selector_side_margin);
    main_sizer->AddSpacer(FromDIP(16));
    main_sizer->Add(create_mode_selection_section(), 0, wxEXPAND | wxLEFT | wxRIGHT, content_side_margin);
    main_sizer->AddSpacer(FromDIP(16));
    main_sizer->Add(create_button_panel(), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, content_side_margin);

    SetSizer(main_sizer);
    SetMinSize(wxSize(FromDIP(477), FromDIP(500)));
    Fit();
    CenterOnParent();
}

wxBoxSizer* ColorDecomposeDialog::create_filament_selector()
{
    auto* sizer = new wxBoxSizer(wxHORIZONTAL);

    m_type_combo = new ComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                wxSize(-1, FromDIP(36)), 0, nullptr, wxCB_READONLY);
    m_type_combo->SetFont(Label::Body_13);

    m_combo_item_families.clear();
    int default_sel = -1;
    std::set<std::string> seen_families;
    for (const auto& type : m_filament_types) {
        const std::string family = filament_family(type.empty() ? "PLA" : type);
        if (family.empty() || !seen_families.insert(family).second)
            continue;
        int idx = m_type_combo->Append(family_display_label(family));
        m_combo_item_families.push_back(family);
        if (family == m_preferred_family && default_sel < 0)
            default_sel = idx;
    }

    if (default_sel < 0 && !m_combo_item_families.empty())
        default_sel = 0;

    if (default_sel >= 0) {
        m_type_combo->SetSelection(default_sel);
        if (static_cast<size_t>(default_sel) < m_combo_item_families.size())
            m_preferred_family = m_combo_item_families[default_sel];
    }

    m_type_combo->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent& evt) {
        evt.StopPropagation();
        int sel = m_type_combo->GetSelection();
        if (sel >= 0 && static_cast<size_t>(sel) < m_combo_item_families.size())
            m_preferred_family = m_combo_item_families[sel];
        update_card_visibility();
        compute_decomposition();
        update_matched_color_display();
        update_ok_button_state();
    });

    sizer->Add(m_type_combo, 1, wxEXPAND);
    return sizer;
}

static std::string colour_to_hex(const wxColour& color)
{
    return wxString::Format("#%02X%02X%02X", color.Red(), color.Green(), color.Blue()).ToStdString();
}

// Copy the cached empty-label swatch, then draw the slot number at the
// 24 DIP icon size (get_extruder_color_icon would fill the full 32 DIP).
static wxBitmap make_decompose_swatch_bitmap(const wxColour& color, int size, int filament_id)
{
    wxBitmap* icon = get_extruder_color_icon(colour_to_hex(color), std::string(), size, size);
    if (!icon || !icon->IsOk())
        return wxBitmap();
    wxBitmap bmp = icon->GetSubBitmap(wxRect(0, 0, icon->GetWidth(), icon->GetHeight()));
    if (filament_id <= 0)
        return bmp;

    const wxString text = wxString::Format("%d", filament_id);
#ifdef __WXMSW__
    wxClientDC cdc((wxWindow*)wxGetApp().mainframe);
    wxMemoryDC dc(&cdc);
    dc.SelectObject(bmp);
#else
    wxMemoryDC dc(bmp);
#endif
    dc.SetFont(Label::Body_12);
    // Match the 24 DIP swatch era: fill ~22px, not the full 32 DIP icon.
    int label_box = size;
    if (wxWindow* host = (wxWindow*)wxGetApp().mainframe)
        label_box = host->FromDIP(24) - 2;
    else
        label_box = wxMax(1, size * 24 / 32 - 2);
    WxFontUtils::get_suitable_font_size(label_box, dc);
    dc.SetBackgroundMode(wxTRANSPARENT);
    const wxSize ts = dc.GetTextExtent(text);
    if (color.Alpha() == 0)
        dc.SetTextForeground(*wxBLACK);
    else
        dc.SetTextForeground(color.GetLuminance() < 0.51 ? *wxWHITE : *wxBLACK);
    dc.DrawText(text, (size - ts.x) / 2, (size - ts.y) / 2);
    dc.SelectObject(wxNullBitmap);
    return bmp;
}

static void set_swatch_bitmap(wxStaticBitmap* bmp, const wxColour& color, int size, int filament_id)
{
    if (!bmp)
        return;
    const wxBitmap icon = make_decompose_swatch_bitmap(color, size, filament_id);
    if (icon.IsOk())
        bmp->SetBitmap(icon);
    bmp->SetMinSize(wxSize(size, size));
    bmp->SetMaxSize(wxSize(size, size));
    bmp->SetSize(wxSize(size, size));
}

static wxStaticBitmap* create_color_swatch(wxWindow* parent, const wxColour& color, int size, int filament_id)
{
    auto* bmp = new wxStaticBitmap(parent, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize(size, size));
    set_swatch_bitmap(bmp, color, size, filament_id);
    return bmp;
}

static wxWindow* create_result_component_swatch(wxWindow* parent, const wxColour& color,
                                                int size, int filament_id, bool show_new,
                                                bool reserve_new_space = false)
{
    if (!show_new && !reserve_new_space)
        return create_color_swatch(parent, color, size, filament_id);

    // Keep layout width at `size` so + / % stay aligned with mode cards.
    // Extra height is only above the swatch so mixed New / reuse rows share one baseline.
    const int overhang = parent->FromDIP(8);
    const int wrap_w = size;
    const int wrap_h = size + overhang;
    auto* panel = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(wrap_w, wrap_h));
    panel->SetMinSize(wxSize(wrap_w, wrap_h));
    panel->SetMaxSize(wxSize(wrap_w, wrap_h));
    panel->SetBackgroundStyle(wxBG_STYLE_PAINT);
    match_parent_bg(panel, StateColor::darkModeColorFor(*wxWHITE));

    const wxBitmap bmp = make_decompose_swatch_bitmap(color, size, filament_id);

    panel->Bind(wxEVT_PAINT, [panel, bmp, overhang, show_new](wxPaintEvent&) {
        wxBufferedPaintDC dc(panel);
        dc.SetBackground(wxBrush(panel->GetBackgroundColour()));
        dc.Clear();
        if (bmp.IsOk())
            dc.DrawBitmap(bmp, 0, overhang, true);
        if (!show_new)
            return;
        const wxString text = _L("New");
        dc.SetFont(Label::Body_8);
        const wxSize ts = dc.GetTextExtent(text);
        const int pad_x = panel->FromDIP(3);
        const int pad_y = panel->FromDIP(1);
        const int bw = ts.x + pad_x * 2;
        const int bh = wxMax(ts.y + pad_y * 2, panel->FromDIP(10));
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(StateColor::darkModeColorFor(COLOR_BRAND)));
        dc.DrawRoundedRectangle(0, 0, bw, bh, panel->FromDIP(6));
        dc.SetTextForeground(*wxWHITE);
        dc.DrawText(text, pad_x, (bh - ts.y) / 2);
    });
    return panel;
}

static wxPanel* create_component_plus_panel(wxWindow* parent, int plus_gap, int swatch_sz, const wxColour& bg)
{
    auto* plus_panel = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(plus_gap, swatch_sz));
    plus_panel->SetMinSize(wxSize(plus_gap, swatch_sz));
    plus_panel->SetMaxSize(wxSize(plus_gap, swatch_sz));
    plus_panel->SetBackgroundColour(bg);
    auto* plus_sizer = new wxBoxSizer(wxVERTICAL);
    auto* plus_label = new wxStaticText(plus_panel, wxID_ANY, "+");
    plus_label->SetFont(Label::Body_13);
    plus_label->SetForegroundColour(StateColor::darkModeColorFor(COLOR_TEXT_DARK));
    match_parent_bg(plus_label, bg);
    plus_sizer->AddStretchSpacer();
    plus_sizer->Add(plus_label, 0, wxALIGN_CENTER_HORIZONTAL);
    plus_sizer->AddStretchSpacer();
    plus_panel->SetSizer(plus_sizer);
    return plus_panel;
}

static void append_decompose_component(wxWindow* parent, wxBoxSizer* sizer,
                                       const wxColour& color, int ratio, int swatch_sz, int filament_id,
                                       const wxColour& bg, bool show_new,
                                       const std::function<void(wxWindow*)>& bind_select,
                                       bool reserve_new_space = false)
{
    auto* col = new wxBoxSizer(wxVERTICAL);
    auto* swatch = create_result_component_swatch(parent, color, swatch_sz, filament_id,
                                                 show_new, reserve_new_space);
    match_parent_bg(swatch, bg);
    if (bind_select)
        bind_select(swatch);
    col->Add(swatch, 0, wxALIGN_CENTER_HORIZONTAL);

    auto* ratio_text = new wxStaticText(parent, wxID_ANY, wxString::Format("%d%%", ratio));
    ratio_text->SetFont(Label::Body_13);
    ratio_text->SetForegroundColour(StateColor::darkModeColorFor(COLOR_TEXT_DARK));
    match_parent_bg(ratio_text, bg);
    if (bind_select)
        bind_select(ratio_text);
    col->Add(ratio_text, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, parent->FromDIP(4));
    sizer->Add(col, 0, wxALIGN_TOP);
}

static void append_decompose_plus(wxWindow* parent, wxBoxSizer* sizer, int plus_gap, int swatch_sz,
                                  const wxColour& bg, bool stretch_around_plus, int plus_top_pad,
                                  const std::function<void(wxWindow*)>& bind_select)
{
    if (stretch_around_plus)
        sizer->AddStretchSpacer();
    auto* plus_panel = create_component_plus_panel(parent, plus_gap, swatch_sz, bg);
    if (bind_select) {
        bind_select(plus_panel);
        const wxWindowList& children = plus_panel->GetChildren();
        for (wxWindowList::compatibility_iterator node = children.GetFirst(); node; node = node->GetNext())
            bind_select(node->GetData());
    }
    sizer->Add(plus_panel, 0, wxALIGN_TOP | wxTOP, plus_top_pad);
    if (stretch_around_plus)
        sizer->AddStretchSpacer();
}

static int decompose_components_inner_width(wxWindow* host, size_t count)
{
    const int pad = host->FromDIP(12);
    const int two_color_card = host->FromDIP(128);
    if (count <= 2)
        return two_color_card - 2 * pad;

    // 3+ colors: grow just enough for swatches and pluses. Do not keep the
    // 2-color stretch slack, or the cards get unnecessarily wide.
    const int swatch = host->FromDIP(32);
    const int plus = host->FromDIP(24);
    const int n = static_cast<int>(count);
    return n * swatch + (n - 1) * plus;
}

wxBoxSizer* ColorDecomposeDialog::create_result_section()
{
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    auto* title = new wxStaticText(this, wxID_ANY, _L("Decomposition result for this combination"));
    title->SetFont(Label::Head_14);
    title->SetForegroundColour(StateColor::darkModeColorFor(COLOR_TEXT_DARK));
    sizer->Add(title, 0, wxBOTTOM, FromDIP(8));

    auto* row = new wxBoxSizer(wxHORIZONTAL);
    const int swatch_sz = FromDIP(32);
    const int pad = FromDIP(12);
    const wxColour box_bg = StateColor::darkModeColorFor(*wxWHITE);

    auto* src_col = new wxBoxSizer(wxVERTICAL);
    auto* filament_caption = create_result_caption(this, _L("Filament Color"));
    src_col->Add(filament_caption, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, FromDIP(8));

    m_filament_card = create_rounded_result_card(this);
    auto* filament_sizer = new wxBoxSizer(wxVERTICAL);
    auto* filament_inner = new wxBoxSizer(wxVERTICAL);
    m_target_swatch = create_color_swatch(m_filament_card, m_target_color, swatch_sz, 0);
    match_parent_bg(m_target_swatch, box_bg);
    filament_inner->Add(m_target_swatch, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, FromDIP(4));
    m_target_rgb_text = new wxStaticText(m_filament_card, wxID_ANY, format_rgb(m_target_color));
    m_target_rgb_text->SetFont(Label::Body_12);
    m_target_rgb_text->SetForegroundColour(StateColor::darkModeColorFor(COLOR_LABEL_GREY));
    match_parent_bg(m_target_rgb_text, box_bg);
    filament_inner->Add(m_target_rgb_text, 0, wxALIGN_CENTER_HORIZONTAL);
    filament_sizer->AddSpacer(pad);
    filament_sizer->AddStretchSpacer(1);
    filament_sizer->Add(filament_inner, 0, wxALIGN_CENTER_HORIZONTAL | wxLEFT | wxRIGHT, pad);
    filament_sizer->AddStretchSpacer(1);
    filament_sizer->AddSpacer(pad);
    m_filament_card->SetSizer(filament_sizer);
    filament_sizer->SetSizeHints(m_filament_card);
    src_col->Add(m_filament_card, 0, wxALIGN_CENTER_HORIZONTAL);

    row->Add(src_col, 0, wxALIGN_TOP);

    auto* arrow_col = new wxBoxSizer(wxVERTICAL);
    const int caption_band = filament_caption->GetBestSize().GetHeight() + FromDIP(8);
    arrow_col->AddSpacer(caption_band);
    arrow_col->AddStretchSpacer(1);
    m_result_arrow = create_result_arrow(this);
    arrow_col->Add(m_result_arrow, 0, wxALIGN_CENTER_HORIZONTAL | wxLEFT | wxRIGHT, FromDIP(16));
    arrow_col->AddStretchSpacer(1);
    row->Add(arrow_col, 0, wxEXPAND);

    auto* dst_col = new wxBoxSizer(wxVERTICAL);
    m_decomposed_label = create_result_caption(this, _L("Decomposed Colors"));
    dst_col->Add(m_decomposed_label, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, FromDIP(8));

    m_decomposed_container = create_rounded_result_card(this);

    auto* box_sizer = new wxBoxSizer(wxHORIZONTAL);

    auto* mixed_col = new wxBoxSizer(wxVERTICAL);
    m_matched_swatch = create_color_swatch(m_decomposed_container, m_target_color, swatch_sz, 0);
    match_parent_bg(m_matched_swatch, box_bg);
    mixed_col->Add(m_matched_swatch, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, FromDIP(4));
    m_matched_rgb_text = new wxStaticText(m_decomposed_container, wxID_ANY, format_rgb(m_target_color));
    m_matched_rgb_text->SetFont(Label::Body_12);
    m_matched_rgb_text->SetForegroundColour(StateColor::darkModeColorFor(COLOR_LABEL_GREY));
    match_parent_bg(m_matched_rgb_text, box_bg);
    mixed_col->Add(m_matched_rgb_text, 0, wxALIGN_CENTER_HORIZONTAL);
    box_sizer->Add(mixed_col, 0, wxALIGN_CENTER_VERTICAL | wxALL, pad);

    m_result_v_divider = create_v_divider(m_decomposed_container);
    box_sizer->Add(m_result_v_divider, 0, wxEXPAND | wxTOP | wxBOTTOM, pad);

    const int comp_pad_h = FromDIP(32);
    m_result_components_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto* components_outer = new wxBoxSizer(wxHORIZONTAL);
    components_outer->Add(m_result_components_sizer, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, comp_pad_h);
    box_sizer->Add(components_outer, 0, wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM, pad);

    m_decomposed_container->SetSizer(box_sizer);

    dst_col->Add(m_decomposed_container, 0, wxALIGN_CENTER_HORIZONTAL);
    row->Add(dst_col, 0, wxALIGN_TOP);
    row->AddStretchSpacer(1);

    sizer->Add(row, 0, wxEXPAND);
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
    chk->Bind(wxEVT_TOGGLEBUTTON, [this, mode](wxCommandEvent& e) {
        select_mode(mode);
        e.Skip();  // let CheckBox::update() re-sync its bitmap to GetValue()
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
    card->SetMinSize(wxSize(FromDIP(128), -1));
    card->SetMaxSize(wxSize(FromDIP(128), -1));

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
    section_label->SetForegroundColour(StateColor::darkModeColorFor(COLOR_TEXT_DARK));
    sizer->Add(section_label, 0, wxBOTTOM, FromDIP(4));

    auto* modes_sizer = new wxBoxSizer(wxHORIZONTAL);

    // --- Arbitrary mode column (wrapped in a panel so the whole column hides together) ---
    m_arb_column_panel = new wxPanel(this, wxID_ANY);
    m_arb_column_panel->SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));
    auto* arb_col = new wxBoxSizer(wxVERTICAL);
    {
        auto* arb_header_sizer = new wxBoxSizer(wxHORIZONTAL);
        arb_header_sizer->Add(create_mode_group_label(m_arb_column_panel, _L("Project Filament")),
                              0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(5));
        arb_header_sizer->Add(create_h_divider(m_arb_column_panel, FromDIP(88)), 0, wxALIGN_CENTER_VERTICAL);
        arb_col->Add(arb_header_sizer, 0, wxEXPAND | wxBOTTOM, FromDIP(8));

        m_card_material_list = create_mode_card(m_arb_column_panel, DecomposeMode::MaterialList,
            _L("Material List"));
        arb_col->Add(m_card_material_list, 0, wxEXPAND);
    }
    m_arb_column_panel->SetSizer(arb_col);
    modes_sizer->Add(m_arb_column_panel, 0, wxEXPAND | wxRIGHT, FromDIP(16));

    // --- Standard mode column ---
    m_std_column_panel = new wxPanel(this, wxID_ANY);
    m_std_column_panel->SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));
    auto* std_col = new wxBoxSizer(wxVERTICAL);
    {
        auto* std_header_sizer = new wxBoxSizer(wxHORIZONTAL);
        std_header_sizer->Add(create_mode_group_label(m_std_column_panel, _L("Standard Palette")),
                              0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(5));
        std_header_sizer->Add(create_h_divider(m_std_column_panel), 1, wxALIGN_CENTER_VERTICAL);
        std_col->Add(std_header_sizer, 0, wxEXPAND | wxBOTTOM, FromDIP(8));

        auto* cards_sizer = new wxBoxSizer(wxHORIZONTAL);

        m_card_cmyw = create_mode_card(m_std_column_panel, DecomposeMode::CMYW, "CMYW");
        cards_sizer->Add(m_card_cmyw, 0, wxRIGHT, FromDIP(12));

        m_card_rybw = create_mode_card(m_std_column_panel, DecomposeMode::RYBW, "RYBW");
        cards_sizer->Add(m_card_rybw, 0);

        std_col->Add(cards_sizer, 0, wxEXPAND);
    }
    m_std_column_panel->SetSizer(std_col);
    modes_sizer->Add(m_std_column_panel, 0, wxEXPAND);

    sizer->Add(modes_sizer, 0, wxEXPAND);

    m_no_card_warning_panel = new wxPanel(this, wxID_ANY);
    m_no_card_warning_panel->SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));
    auto* no_card_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto* no_card_bmp = new wxStaticBitmap(m_no_card_warning_panel, wxID_ANY,
        create_scaled_bitmap("obj_warning", m_no_card_warning_panel, 16),
        wxDefaultPosition, wxSize(FromDIP(16), FromDIP(16)));
    m_no_card_warning_text = new wxStaticText(m_no_card_warning_panel, wxID_ANY,
        _L("At least two filaments of the same material type are required for decomposition"));
    m_no_card_warning_text->SetFont(Label::Body_13);
    m_no_card_warning_text->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#E6A817")));
    m_no_card_warning_text->Wrap(FromDIP(400));
    no_card_sizer->Add(no_card_bmp, 0, wxALIGN_TOP | wxRIGHT, FromDIP(6));
    no_card_sizer->Add(m_no_card_warning_text, 1, wxEXPAND);
    m_no_card_warning_panel->SetSizer(no_card_sizer);
    m_no_card_warning_panel->Hide();
    sizer->Add(m_no_card_warning_panel, 0, wxEXPAND | wxTOP, FromDIP(8));

    m_basic_warning_panel = new wxPanel(this, wxID_ANY);
    m_basic_warning_panel->SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));
    auto* basic_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_basic_warning_text = new wxStaticText(m_basic_warning_panel, wxID_ANY, wxEmptyString);
    m_basic_warning_text->SetFont(Label::Body_13);
    m_basic_warning_text->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#6B6B6B")));
    m_basic_warning_text->Wrap(FromDIP(400));
    basic_sizer->Add(m_basic_warning_text, 1, wxEXPAND);
    m_basic_warning_panel->SetSizer(basic_sizer);
    m_basic_warning_panel->Hide();
    sizer->Add(m_basic_warning_panel, 0, wxEXPAND | wxTOP, FromDIP(8));

    sizer->Add(create_result_section(), 0, wxEXPAND | wxTOP, FromDIP(16));

    m_limit_warning_panel = new wxPanel(this, wxID_ANY);
    m_limit_warning_panel->SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));
    auto* warning_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto* warn_bmp = new wxStaticBitmap(m_limit_warning_panel, wxID_ANY,
        create_scaled_bitmap("obj_warning", m_limit_warning_panel, 16),
        wxDefaultPosition, wxSize(FromDIP(16), FromDIP(16)));
    m_limit_warning_text = new wxStaticText(m_limit_warning_panel, wxID_ANY, wxEmptyString);
    m_limit_warning_text->SetFont(Label::Body_13);
    m_limit_warning_text->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#D32F2F")));
    m_limit_warning_text->Wrap(FromDIP(400));
    warning_sizer->Add(warn_bmp, 0, wxALIGN_TOP | wxRIGHT, FromDIP(6));
    warning_sizer->Add(m_limit_warning_text, 1, wxEXPAND);
    m_limit_warning_panel->SetSizer(warning_sizer);
    m_limit_warning_panel->Hide();
    sizer->Add(m_limit_warning_panel, 0, wxEXPAND | wxTOP, FromDIP(8));

    return sizer;
}

wxBoxSizer* ColorDecomposeDialog::create_button_panel()
{
    auto* sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->AddStretchSpacer();

    m_btn_cancel = new Button(this, _L("Cancel"));
    m_btn_cancel->SetBackgroundColor(StateColor::darkModeColorFor(*wxWHITE));
    m_btn_cancel->SetBorderColor(StateColor::darkModeColorFor(wxColour("#CECECE")));
    m_btn_cancel->SetTextColor(StateColor::darkModeColorFor(wxColour("#262E30")));
    m_btn_cancel->SetMinSize(wxSize(FromDIP(55), FromDIP(24)));
    m_btn_cancel->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CANCEL); });

    m_btn_ok = new Button(this, _L("OK"));
    m_btn_ok->SetBackgroundColor(StateColor(
        std::make_pair(wxColour("#C2C2C2"), (int) StateColor::Disabled),
        std::make_pair(wxColour("#00AE42"), (int) StateColor::Normal)));
    m_btn_ok->SetBorderColor(StateColor(
        std::make_pair(wxColour("#C2C2C2"), (int) StateColor::Disabled),
        std::make_pair(wxColour("#00AE42"), (int) StateColor::Normal)));
    m_btn_ok->SetTextColor(StateColor(
        std::make_pair(*wxWHITE, (int) StateColor::Disabled),
        std::make_pair(*wxWHITE, (int) StateColor::Normal)));
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
    update_basic_support_warning();
    update_ok_button_state();
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

void ColorDecomposeDialog::update_card_visibility()
{
    const bool is_pla = (m_preferred_family == kDecomposePlaShortType);
    const bool show_arb  = can_mix_material_list();
    const bool show_cmyw = is_pla;
    const bool show_rybw = is_pla;

    if (m_arb_column_panel)   m_arb_column_panel->Show(show_arb);
    if (m_card_material_list) m_card_material_list->Show(show_arb);
    if (m_std_column_panel)   m_std_column_panel->Show(show_cmyw || show_rybw);
    if (m_card_cmyw)          m_card_cmyw->Show(show_cmyw);
    if (m_card_rybw)          m_card_rybw->Show(show_rybw);

    bool any_visible = show_arb || show_cmyw || show_rybw;
    if (any_visible) {
        bool cur_visible = false;
        if (m_selected_mode == DecomposeMode::MaterialList && show_arb)  cur_visible = true;
        if (m_selected_mode == DecomposeMode::CMYW && show_cmyw)         cur_visible = true;
        if (m_selected_mode == DecomposeMode::RYBW && show_rybw)         cur_visible = true;
        if (!cur_visible) {
            if (show_arb)       select_mode(DecomposeMode::MaterialList);
            else if (show_cmyw) select_mode(DecomposeMode::CMYW);
            else                select_mode(DecomposeMode::RYBW);
        }
    }

    update_basic_support_warning();
    Layout();
    update_ok_button_state();
}

int ColorDecomposeDialog::mixable_family_count() const
{
    int count = 0;
    for (size_t i = 0; i < m_filament_types.size(); ++i) {
        if (static_cast<int>(i) == m_filament_idx)
            continue;
        if (family_matches(m_filament_types[i], m_preferred_family))
            ++count;
    }
    return count;
}

bool ColorDecomposeDialog::can_mix_material_list() const
{
    return mixable_family_count() >= 2;
}

bool ColorDecomposeDialog::has_usable_card() const
{
    return (m_card_material_list && m_card_material_list->IsShown())
        || (m_card_cmyw && m_card_cmyw->IsShown())
        || (m_card_rybw && m_card_rybw->IsShown());
}

void ColorDecomposeDialog::update_basic_support_warning()
{
    if (!m_basic_warning_panel || !m_basic_warning_text)
        return;

    const bool show = (m_preferred_family == kDecomposePlaShortType)
        && (m_selected_mode == DecomposeMode::CMYW || m_selected_mode == DecomposeMode::RYBW);
    const bool was_shown = m_basic_warning_panel->IsShown();
    if (!show) {
        if (was_shown) {
            m_basic_warning_panel->Hide();
            Layout();
        }
        return;
    }

    const wxString text = (m_selected_mode == DecomposeMode::CMYW)
        ? _L("A closer mix is calculated from four base colors: cyan, magenta, yellow, and white. Currently only PLA Basic is supported. If matching filaments are not in the project, they will be added automatically.")
        : _L("A closer mix is calculated from four base colors: red, yellow, blue, and white. Currently only PLA Basic is supported. If matching filaments are not in the project, they will be added automatically.");
    m_basic_warning_text->SetLabel(text);
    m_basic_warning_panel->Show();
    Layout();
    const int avail = m_basic_warning_text->GetClientSize().x;
    if (avail > FromDIP(50))
        m_basic_warning_text->Wrap(avail);
    Layout();
    if (!was_shown)
        Fit();
}

void ColorDecomposeDialog::update_filament_limit_warning()
{
    if (!m_limit_warning_panel || !m_limit_warning_text)
        return;

    size_t missing_new = 0;
    if (m_missing_calculator) {
        missing_new = m_missing_calculator(m_result);
    } else {
        const size_t source_physical_idx = m_filament_idx >= 0 ? static_cast<size_t>(m_filament_idx) : size_t(-1);
        const std::vector<size_t>* indices =
            m_physical_config_indices.empty() ? nullptr : &m_physical_config_indices;
        missing_new = count_decompose_new_physical_filaments(
            m_result, m_physical_colors, m_filament_types, source_physical_idx, indices);
    }
    // A result with fewer than 2 components (e.g. target color is already a
    // standard base color shown as "100%") creates no mixed filament and no new
    // physical filament, so it can never exceed the limit.
    const bool creates_mixed = m_result.components.size() >= 2;
    // +1 for the mixed filament slot that will be created after decomposition.
    const size_t needed = m_current_filament_count + missing_new + 1;
    const bool blocked = creates_mixed && needed > m_max_filament_count;

    const bool was_shown = m_limit_warning_panel->IsShown();

    if (!blocked) {
        if (was_shown) {
            m_limit_warning_panel->Hide();
            Layout();
            Fit();
        }
        return;
    }

    wxString mode_name;
    switch (m_selected_mode) {
    case DecomposeMode::CMYW:         mode_name = "CMYW"; break;
    case DecomposeMode::RYBW:         mode_name = "RYBW"; break;
    case DecomposeMode::MaterialList: mode_name = _L("Material List"); break;
    }

    const wxString warning_text = format_wxstr(
        _L("The material list supports at most %1% colors. After %2% decomposition, the material count would exceed %1%. Please delete unused filaments on the main screen before decomposing."),
        m_max_filament_count, mode_name);

    // Show first so the panel is laid out and the text control gets its real
    // width, then wrap to that width so the paragraph fills the content area.
    m_limit_warning_panel->Show();
    Layout();
    const int avail = m_limit_warning_text->GetClientSize().x;
    m_limit_warning_text->SetLabel(warning_text);
    if (avail > FromDIP(50))
        m_limit_warning_text->Wrap(avail);

    Layout();
    // Only resize when the warning panel actually toggled from hidden to shown.
    // While already visible, switching modes must not re-Fit the dialog, which
    // would make it jump on every card switch. Fit keeps the user-moved position.
    if (!was_shown) {
        Fit();
    }
}

void ColorDecomposeDialog::set_missing_physical_calculator(std::function<size_t(const ColorDecomposeResult&)> fn)
{
    m_missing_calculator = std::move(fn);
    update_ok_button_state();
}

void ColorDecomposeDialog::set_preview_id_calculator(std::function<DecomposePreviewIds(const ColorDecomposeResult&)> fn)
{
    m_preview_id_calculator = std::move(fn);
    update_matched_color_display();
    update_mode_card_contents();
}

DecomposePreviewIds ColorDecomposeDialog::preview_ids_for(const ColorDecomposeResult& result) const
{
    if (m_preview_id_calculator)
        return m_preview_id_calculator(result);
    return preview_decompose_filament_ids(
        result, m_filament_idx, m_current_filament_count,
        m_physical_colors, m_filament_types, m_physical_config_indices);
}

void ColorDecomposeDialog::update_ok_button_state()
{
    if (!m_btn_ok) return;
    update_filament_limit_warning();
    const bool any_card_visible = has_usable_card();
    const bool blocked = m_limit_warning_panel && m_limit_warning_panel->IsShown();
    const bool mode_usable = (m_selected_mode != DecomposeMode::MaterialList) || can_mix_material_list();
    const bool show_no_card = !any_card_visible
        || (m_selected_mode == DecomposeMode::MaterialList && !mode_usable);
    if (m_no_card_warning_panel) {
        const bool was_shown = m_no_card_warning_panel->IsShown();
        m_no_card_warning_panel->Show(show_no_card);
        if (show_no_card && m_no_card_warning_text) {
            Layout();
            const int avail = m_no_card_warning_text->GetClientSize().x;
            if (avail > FromDIP(50))
                m_no_card_warning_text->Wrap(avail);
            if (!was_shown)
                Fit();
        }
    }
    m_btn_ok->Enable(any_card_visible && mode_usable && !blocked);
    update_matched_color_display();
    Layout();
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

    const int swatch_sz = FromDIP(32);
    const int plus_gap  = FromDIP(24);
    auto bind_select = [this, mode](wxWindow* w) {
        w->Bind(wxEVT_LEFT_UP, [this, mode](wxMouseEvent&) {
            select_mode(mode);
        });
        w->SetCursor(wxCursor(wxCURSOR_HAND));
    };

    const auto preview = preview_ids_for(m_mode_results[mode_index(mode)]);

    for (size_t i = 0; i < count; ++i) {
        const int filament_id = (mode == DecomposeMode::MaterialList && i < preview.component_ids.size())
            ? preview.component_ids[i] : 0;
        append_decompose_component(card, sizer, components[i].colour, components[i].ratio,
                                   swatch_sz, filament_id, StateColor::darkModeColorFor(COLOR_BG_CARD),
                                   false, bind_select);

        if (i + 1 < count) {
            append_decompose_plus(card, sizer, plus_gap, swatch_sz,
                                  StateColor::darkModeColorFor(COLOR_BG_CARD), true, 0, bind_select);
        }
    }

    const int card_width = decompose_components_inner_width(card, count) + 2 * FromDIP(12);
    if (wxSizer* card_sizer = card->GetSizer())
        card_sizer->SetSizeHints(card);
    const int card_height = wxMax(card->GetBestSize().GetHeight(), card->GetMinHeight());
    card->SetMinSize(wxSize(card_width, card_height));
    card->SetMaxSize(wxSize(card_width, -1));

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

bool ColorDecomposeDialog::is_existing_physical_id(int preview_id) const
{
    if (preview_id <= 0)
        return false;
    if (m_physical_config_indices.empty())
        return preview_id >= 1 && static_cast<size_t>(preview_id) <= m_physical_colors.size();
    for (size_t cfg_idx : m_physical_config_indices) {
        if (static_cast<int>(cfg_idx + 1) == preview_id)
            return true;
    }
    return false;
}

void ColorDecomposeDialog::update_matched_color_display()
{
    if (!m_result.matched_color.IsOk())
        m_result.matched_color = m_target_color;

    const auto preview = preview_ids_for(m_result);
    const int swatch_sz = FromDIP(32);
    set_swatch_bitmap(m_target_swatch, m_target_color, swatch_sz, 0);
    set_swatch_bitmap(m_matched_swatch, m_result.matched_color, swatch_sz, 0);

    if (m_matched_rgb_text) {
        m_matched_rgb_text->SetLabel(format_rgb(m_result.matched_color));
        match_parent_bg(m_matched_rgb_text, StateColor::darkModeColorFor(*wxWHITE));
    }

    if (m_result_components_sizer && m_decomposed_container) {
        m_result_components_sizer->Clear(true);
        const auto& components = m_result.components;
        const wxColour box_bg = StateColor::darkModeColorFor(*wxWHITE);
        const int plus_gap = FromDIP(24);
        bool any_new = false;
        for (size_t i = 0; i < components.size(); ++i) {
            const int filament_id = (i < preview.component_ids.size()) ? preview.component_ids[i] : 0;
            if (filament_id > 0 && !is_existing_physical_id(filament_id))
                any_new = true;
        }
        const int plus_top_pad = any_new ? m_decomposed_container->FromDIP(8) : 0;
        for (size_t i = 0; i < components.size(); ++i) {
            const int filament_id = (i < preview.component_ids.size()) ? preview.component_ids[i] : 0;
            const bool show_new = filament_id > 0 && !is_existing_physical_id(filament_id);
            append_decompose_component(m_decomposed_container, m_result_components_sizer,
                                       components[i].colour, components[i].ratio, swatch_sz,
                                       filament_id, box_bg, show_new, {}, any_new);

            if (i + 1 < components.size()) {
                append_decompose_plus(m_decomposed_container, m_result_components_sizer,
                                      plus_gap, swatch_sz, box_bg, true, plus_top_pad, {});
            }
        }
        if (!components.empty())
            m_result_components_sizer->SetMinSize(
                decompose_components_inner_width(m_decomposed_container, components.size()), -1);
        else
            m_result_components_sizer->SetMinSize(wxDefaultSize);
        m_decomposed_container->Layout();
        if (wxSizer* box = m_decomposed_container->GetSizer())
            box->SetSizeHints(m_decomposed_container);
        m_decomposed_container->Refresh();
    }

    const bool has_result = has_usable_card();
    if (m_result_arrow)
        m_result_arrow->Show(has_result);
    if (m_decomposed_label)
        m_decomposed_label->Show(has_result);
    if (m_decomposed_container)
        m_decomposed_container->Show(has_result);

    if (m_filament_card) {
        if (has_result && m_decomposed_container) {
            if (wxSizer* fs = m_filament_card->GetSizer())
                fs->SetSizeHints(m_filament_card);
            const int w = m_filament_card->GetBestSize().GetWidth();
            const int h = m_decomposed_container->GetBestSize().GetHeight();
            m_filament_card->SetMinSize(wxSize(w, h));
            m_filament_card->SetMaxSize(wxSize(w, h));
        } else {
            m_filament_card->SetMinSize(wxDefaultSize);
            m_filament_card->SetMaxSize(wxDefaultSize);
            if (wxSizer* fs = m_filament_card->GetSizer())
                fs->SetSizeHints(m_filament_card);
        }
        m_filament_card->Layout();
        m_filament_card->Refresh();
    }
}

bool ColorDecomposeDialog::try_build_single_base_result(DecomposeMode mode, ColorDecomposeResult& out) const
{
    // Gate by family, matching card visibility: CMYW and RYBW only for PLA.
    if (mode == DecomposeMode::CMYW || mode == DecomposeMode::RYBW) {
        if (m_preferred_family != kDecomposePlaShortType)
            return false;
    } else {
        return false;
    }

    static const DecomposeBaseColor cmyw_bases[] = {
        DecomposeBaseColor::Cyan, DecomposeBaseColor::Magenta,
        DecomposeBaseColor::Yellow, DecomposeBaseColor::White
    };
    static const DecomposeBaseColor rybw_bases[] = {
        DecomposeBaseColor::Red, DecomposeBaseColor::Yellow,
        DecomposeBaseColor::Blue, DecomposeBaseColor::White
    };
    const DecomposeBaseColor* bases = (mode == DecomposeMode::CMYW) ? cmyw_bases : rybw_bases;
    const size_t base_count = (mode == DecomposeMode::CMYW)
        ? sizeof(cmyw_bases) / sizeof(cmyw_bases[0])
        : sizeof(rybw_bases) / sizeof(rybw_bases[0]);

    const std::string target_hex = decompose_normalize_color_hex(
        m_target_color.GetAsString(wxC2S_HTML_SYNTAX).ToStdString());

    for (size_t i = 0; i < base_count; ++i) {
        const DecomposeBaseColor base = bases[i];
        DecomposeOfficialComponent official =
            lookup_decompose_official_component(kDecomposePlaBasicType, base, pure_color_for_base(base));
        if (decompose_normalize_color_hex(official.color_hex) != target_hex)
            continue;

        out = ColorDecomposeResult{};
        out.mode = mode;
        out.matched_color = hex_to_wx_colour(official.color_hex, m_target_color);
        DecomposeComponent comp;
        comp.colour = out.matched_color;
        comp.ratio = 100;
        comp.filament_index = -1;
        comp.base_color = base;
        out.components.push_back(comp);
        return true;
    }
    return false;
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

    std::vector<ColorDecomposePhysicalFilament> family_filaments;
    family_filaments.reserve(physical_filaments.size());
    for (const auto& filament : physical_filaments) {
        if (family_matches(filament.type, m_preferred_family))
            family_filaments.push_back(filament);
    }

    const ColorDecomposeRgb target_rgb = wx_colour_to_recipe_rgb(m_target_color);

    // Empty preferred type: family_filaments is already filtered, so the
    // recipe fallback (candidates < 2 -> use the passed list) cannot mix PETG into PLA.
    auto material_recipe = recommend_from_physical_filaments(target_rgb, family_filaments, std::string());
    if (material_recipe.valid) {
        m_mode_results[mode_index(DecomposeMode::MaterialList)] =
            to_dialog_result(material_recipe, m_target_color);
    } else {
        std::vector<DecomposeComponent> components;
        for (size_t i = 0; i < std::min<size_t>(2, family_filaments.size()); ++i) {
            DecomposeComponent comp;
            comp.colour = wxColour(family_filaments[i].color_hex);
            comp.ratio = 50;
            comp.filament_index = static_cast<int>(family_filaments[i].filament_index);
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

    ColorDecomposeResult single_base;
    if (try_build_single_base_result(DecomposeMode::CMYW, single_base)) {
        m_mode_results[mode_index(DecomposeMode::CMYW)] = single_base;
    } else {
        auto cmyw_recipe = lookup_standard_recipe(target_rgb, ColorDecomposeRecipeMode::CMYW, kDecomposePlaBasicType);
        m_mode_results[mode_index(DecomposeMode::CMYW)] = cmyw_recipe.valid
            ? to_dialog_result(cmyw_recipe, m_target_color)
            : fallback_result(DecomposeMode::CMYW, {
                {CMYW_YELLOW, 50, -1, DecomposeBaseColor::Yellow},
                {CMYW_CYAN,   50, -1, DecomposeBaseColor::Cyan}
            });
    }

    if (try_build_single_base_result(DecomposeMode::RYBW, single_base)) {
        m_mode_results[mode_index(DecomposeMode::RYBW)] = single_base;
    } else {
        auto rybw_recipe = lookup_standard_recipe(target_rgb, ColorDecomposeRecipeMode::RYBW, kDecomposePlaBasicType);
        m_mode_results[mode_index(DecomposeMode::RYBW)] = rybw_recipe.valid
            ? to_dialog_result(rybw_recipe, m_target_color)
            : fallback_result(DecomposeMode::RYBW, {
                {RYBW_YELLOW, 50, -1, DecomposeBaseColor::Yellow},
                {RYBW_BLUE,   50, -1, DecomposeBaseColor::Blue}
            });
    }

    m_result = m_mode_results[mode_index(m_selected_mode)];
    update_mode_card_contents();
    update_ok_button_state();
}

} // namespace GUI
} // namespace Slic3r
