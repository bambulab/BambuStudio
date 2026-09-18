#include "TextureImportOverLimitDialog.hpp"
#include "TextureImportUi.hpp"

#include "I18N.hpp"
#include "GUI_App.hpp"
#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"
#include "format.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/RadioBox.hpp"
#include "Widgets/StateColor.hpp"
#include "Widgets/StaticLine.hpp"

#include <wx/dc.h>
#include <wx/dcclient.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/scrolwin.h>
#include <wx/wrapsizer.h>
#include <wx/display.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <set>

namespace Slic3r {
namespace GUI {

namespace {

using Slic3r::GUI::texture_import_dark_or;
using Slic3r::GUI::texture_import_paint;
using Slic3r::GUI::texture_import_rgb_from_rgba;

wxColour dark_or(const wxColour& light, const wxColour& dark)
{
    return texture_import_dark_or(light, dark);
}

bool is_new_entry(const TextureFilamentEntry& entry)
{
    return entry.kind == TextureFilamentKind::NewPhysical ||
           entry.kind == TextureFilamentKind::NewMixed;
}

std::array<std::size_t, 3> rgb_from_rgba(const std::array<float, 4>& rgba)
{
    return texture_import_rgb_from_rgba(rgba);
}

wxColour wx_from_rgba(const std::array<float, 4>& rgba)
{
    const auto rgb = rgb_from_rgba(rgba);
    return wxColour((unsigned char)rgb[0], (unsigned char)rgb[1], (unsigned char)rgb[2]);
}

wxString format_area_percent(double area_ratio)
{
    double pct = area_ratio * 100.0;
    if (area_ratio > 0.0)
        pct = std::max(pct, 0.1);
    return wxString::Format("%.1f%%", pct);
}

double triangle_area(const std::array<float, 3>& a,
                     const std::array<float, 3>& b,
                     const std::array<float, 3>& c)
{
    const float ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
    const float vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
    const float nx = uy * vz - uz * vy;
    const float ny = uz * vx - ux * vz;
    const float nz = ux * vy - uy * vx;
    return 0.5 * std::sqrt(double(nx) * nx + double(ny) * ny + double(nz) * nz);
}

std::vector<double> compute_filament_areas(const TextureOverLimitInput& input)
{
    std::vector<double> areas(input.colors_rgba.size(), 0.0);
    std::map<std::array<std::size_t, 3>, int> color_to_filament;
    for (const auto& m : input.matches) {
        if (m.filament_index >= 0 && m.filament_index < (int)areas.size())
            color_to_filament[m.cluster_color] = m.filament_index;
    }

    const auto& mesh = input.painted;
    const size_t face_n = std::min(mesh.indices.size(), mesh.face_colors.size());
    if (face_n == 0 || mesh.vertices.empty()) {
        for (const auto& m : input.matches) {
            if (m.filament_index >= 0 && m.filament_index < (int)areas.size())
                areas[m.filament_index] += 1.0;
        }
        return areas;
    }

    for (size_t fi = 0; fi < face_n; ++fi) {
        auto it = color_to_filament.find(mesh.face_colors[fi]);
        if (it == color_to_filament.end())
            continue;
        const auto& idx = mesh.indices[fi];
        if (idx[0] < 0 || idx[1] < 0 || idx[2] < 0)
            continue;
        if ((size_t)idx[0] >= mesh.vertices.size() ||
            (size_t)idx[1] >= mesh.vertices.size() ||
            (size_t)idx[2] >= mesh.vertices.size())
            continue;
        areas[it->second] += triangle_area(mesh.vertices[idx[0]], mesh.vertices[idx[1]], mesh.vertices[idx[2]]);
    }
    return areas;
}

int display_number_of(const TextureOverLimitInput& input, int dialog_index)
{
    if (dialog_index < 0 || dialog_index >= (int)input.display_numbers.size())
        return dialog_index + 1;
    return input.display_numbers[dialog_index];
}

TextureOverLimitChip make_chip(const TextureOverLimitInput& input, int idx)
{
    TextureOverLimitChip chip;
    chip.dialog_index = idx;
    chip.display_number = display_number_of(input, idx);
    if (idx >= 0 && idx < (int)input.colors_rgba.size())
        chip.rgba = input.colors_rgba[idx];
    return chip;
}

std::vector<TextureOverLimitChip> chips_from_alive(const TextureOverLimitInput& input,
                                                   const std::vector<char>& alive,
                                                   const std::vector<double>& areas,
                                                   double total_area)
{
    std::vector<TextureOverLimitChip> chips;
    for (size_t i = 0; i < alive.size(); ++i) {
        if (!alive[i])
            continue;
        TextureOverLimitChip chip = make_chip(input, (int)i);
        if (total_area > 0.0 && i < areas.size())
            chip.area_ratio = areas[i] / total_area;
        chips.push_back(chip);
    }
    std::sort(chips.begin(), chips.end(), [](const TextureOverLimitChip& a, const TextureOverLimitChip& b) {
        return a.display_number < b.display_number;
    });
    return chips;
}

int count_alive(const std::vector<char>& alive)
{
    return (int)std::count(alive.begin(), alive.end(), 1);
}

std::vector<std::string> compute_family_types(const TextureOverLimitInput& input)
{
    std::vector<std::string> families(input.entries.size());
    for (size_t i = 0; i < input.entries.size(); ++i)
        families[i] = texture_entry_family_type(input.entries[i], input.entries);
    return families;
}

bool same_family(const std::vector<std::string>& families, int a, int b)
{
    if (a < 0 || b < 0 || a >= (int)families.size() || b >= (int)families.size())
        return false;
    return !families[a].empty() && families[a] == families[b];
}

int find_closest_alive(int source_index,
                       const std::vector<char>& alive,
                       const TextureOverLimitInput& input,
                       const std::vector<std::string>& families)
{
    if (source_index < 0 || source_index >= (int)families.size() ||
        source_index >= (int)input.colors_rgba.size())
        return -1;
    if (families[source_index].empty())
        return -1;

    const auto color = rgb_from_rgba(input.colors_rgba[source_index]);
    int best = -1;
    double best_de = std::numeric_limits<double>::max();
    for (size_t i = 0; i < alive.size() && i < input.colors_rgba.size(); ++i) {
        if (!alive[i] || (int)i == source_index)
            continue;
        if (!same_family(families, source_index, (int)i))
            continue;
        const double de = Slic3r::compute_delta_e(color, input.colors_rgba[i]);
        if (de < best_de) {
            best_de = de;
            best = (int)i;
        }
    }
    return best;
}

void remap_matches(std::vector<Slic3r::FilamentMatch>& matches,
                   int from,
                   int to,
                   const std::vector<std::array<float, 4>>& colors)
{
    if (from < 0 || to < 0 || from == to)
        return;
    for (auto& m : matches) {
        if (m.filament_index != from)
            continue;
        m.filament_index = to;
        if (to < (int)colors.size()) {
            m.filament_color = colors[to];
            m.delta_e = Slic3r::compute_delta_e(m.cluster_color, m.filament_color);
        }
    }
}

bool mixed_uses_index(const TextureFilamentEntry& entry, int dialog_index)
{
    if (entry.kind != TextureFilamentKind::NewMixed)
        return false;
    const unsigned want = (unsigned)(dialog_index + 1);
    for (unsigned comp : entry.mixed_components) {
        if (comp == want)
            return true;
    }
    return false;
}

void drop_dependent_mixed(int absorbed,
                          std::vector<char>& alive,
                          std::vector<Slic3r::FilamentMatch>& matches,
                          const TextureOverLimitInput& input,
                          const std::vector<std::string>& families)
{
    for (size_t i = 0; i < alive.size() && i < input.entries.size(); ++i) {
        if (!alive[i] || !mixed_uses_index(input.entries[i], absorbed))
            continue;
        const int dest = find_closest_alive((int)i, alive, input, families);
        if (dest >= 0)
            remap_matches(matches, (int)i, dest, input.colors_rgba);
        alive[i] = 0;
    }
}

void absorb_filament(int absorbed,
                     int survivor,
                     std::vector<char>& alive,
                     std::vector<Slic3r::FilamentMatch>& matches,
                     const TextureOverLimitInput& input,
                     const std::vector<std::string>& families,
                     std::vector<double>* areas)
{
    remap_matches(matches, absorbed, survivor, input.colors_rgba);
    if (areas && absorbed >= 0 && survivor >= 0 &&
        absorbed < (int)areas->size() && survivor < (int)areas->size())
        (*areas)[survivor] += (*areas)[absorbed];
    alive[absorbed] = 0;
    drop_dependent_mixed(absorbed, alive, matches, input, families);
}

void finish_plan(TextureOverLimitPlan& plan, size_t max_count, int remaining)
{
    plan.remaining_count = remaining;
    plan.fully_resolved = remaining >= 0 && (size_t)remaining <= max_count;
}

std::map<std::array<std::size_t, 3>, std::array<float, 3>>
color_map_from_matches(const std::vector<Slic3r::FilamentMatch>& matches,
                       const std::vector<std::array<float, 4>>& colors)
{
    std::map<std::array<std::size_t, 3>, std::array<float, 3>> color_map;
    for (const auto& m : matches) {
        if (m.filament_index < 0 || m.filament_index >= (int)colors.size())
            continue;
        color_map[m.cluster_color] = {
            colors[m.filament_index][0],
            colors[m.filament_index][1],
            colors[m.filament_index][2]
        };
    }
    return color_map;
}

class NumberedChipPanel : public wxPanel
{
public:
    NumberedChipPanel(wxWindow* parent, const TextureOverLimitChip& chip, bool show_percent)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE)
        , m_chip(chip)
        , m_show_percent(show_percent)
    {
        SetBackgroundColour(parent->GetBackgroundColour());
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        const int sq = FromDIP(24);
        if (show_percent)
            SetMinSize(wxSize(FromDIP(36), sq + FromDIP(18)));
        else
            SetMinSize(wxSize(sq, sq));
        Bind(wxEVT_PAINT, &NumberedChipPanel::on_paint, this);
    }

private:
    void on_paint(wxPaintEvent&)
    {
        texture_import_paint(this, [this](wxDC& dc) {
            const wxSize sz = GetClientSize();
            dc.SetBrush(wxBrush(GetBackgroundColour()));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawRectangle(0, 0, sz.x, sz.y);

            const int sq = FromDIP(24);
            const int r  = FromDIP(2);
            const int sq_x = std::max(0, (sz.x - sq) / 2);
            const wxColour fill = wx_from_rgba(m_chip.rgba);
            dc.SetBrush(wxBrush(fill));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawRoundedRectangle(sq_x, 0, sq, sq, r);

            wxFont font = Label::Body_12;
            dc.SetFont(font);
            dc.SetTextForeground(fill.GetLuminance() < 0.6 ? *wxWHITE : wxColour(0x26, 0x2E, 0x30));
            const wxString num = wxString::Format("%d", m_chip.display_number);
            const wxSize nsz = dc.GetTextExtent(num);
            dc.DrawText(num, sq_x + (sq - nsz.x) / 2, (sq - nsz.y) / 2);

            if (m_show_percent) {
                dc.SetFont(Label::Body_10);
                dc.SetTextForeground(dark_or(wxColour(0x6B, 0x6B, 0x6B), wxColour(0xB3, 0xB3, 0xB5)));
                const wxString pct_str = format_area_percent(m_chip.area_ratio);
                const wxSize psz = dc.GetTextExtent(pct_str);
                dc.DrawText(pct_str, std::max(0, (sz.x - psz.x) / 2), sq + FromDIP(2));
            }
        });
    }

    TextureOverLimitChip m_chip;
    bool m_show_percent = false;
};

void fill_chip_wrap(wxWindow* parent, wxSizer* sizer,
                    const std::vector<TextureOverLimitChip>& chips, bool show_percent)
{
    for (const auto& chip : chips) {
        auto* panel = new NumberedChipPanel(parent, chip, show_percent);
        sizer->Add(panel, 0, wxRIGHT | wxBOTTOM, parent->FromDIP(8));
    }
}

wxPanel* make_chip_section(wxWindow* parent,
                           const wxString& title,
                           const std::vector<TextureOverLimitChip>& chips,
                           bool show_percent)
{
    auto* block = new wxPanel(parent, wxID_ANY);
    block->SetBackgroundColour(parent->GetBackgroundColour());
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    auto* header = new wxBoxSizer(wxHORIZONTAL);
    auto* label = new wxStaticText(block, wxID_ANY, title);
    label->SetFont(Label::Body_13);
    label->SetForegroundColour(dark_or(wxColour(0xAC, 0xAC, 0xAC), wxColour(0x81, 0x81, 0x83)));
    label->SetBackgroundColour(block->GetBackgroundColour());
    header->Add(label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, parent->FromDIP(8));
    auto* line = new StaticLine(block);
    line->SetLineColour(dark_or(wxColour(0xEE, 0xEE, 0xEE), wxColour(0x54, 0x54, 0x5B)));
    header->Add(line, 1, wxALIGN_CENTER_VERTICAL);
    sizer->Add(header, 0, wxEXPAND | wxBOTTOM, parent->FromDIP(8));

    auto* wrap = new wxWrapSizer(wxHORIZONTAL);
    fill_chip_wrap(block, wrap, chips, show_percent);
    sizer->Add(wrap, 0, wxEXPAND);
    block->SetSizer(sizer);
    return block;
}

struct OverLimitSolveContext {
    std::vector<double>      areas;
    std::vector<std::string> families;
    double                   total_area = 0.0;
};

OverLimitSolveContext make_solve_context(const TextureOverLimitInput& input)
{
    OverLimitSolveContext ctx;
    ctx.areas = compute_filament_areas(input);
    ctx.families = compute_family_types(input);
    ctx.total_area = std::accumulate(ctx.areas.begin(), ctx.areas.end(), 0.0);
    return ctx;
}

wxString plan_unresolved_hint(const TextureOverLimitPlan& plan, size_t max_count, int original_count)
{
    if (plan.fully_resolved)
        return wxString();
    if (plan.remaining_count >= original_count)
        return _L("This plan cannot merge any filaments. Remaining filaments use different materials.");
    return format_wxstr(
        _L("This plan reduces to %1% filaments (limit %2%). Remaining filaments cannot be merged because they use different materials."),
        plan.remaining_count, (int)max_count);
}

TextureOverLimitPlan compute_merge_plan(const TextureOverLimitInput& input, OverLimitSolveContext ctx)
{
    TextureOverLimitPlan plan;
    plan.matches = input.matches;
    const size_t n = input.colors_rgba.size();
    std::vector<char> alive(n, 1);
    plan.before_chips = chips_from_alive(input, alive, ctx.areas, ctx.total_area);

    const size_t max_count = input.max_count;
    while ((size_t)count_alive(alive) > max_count) {
        int best_new = -1;
        int best_other = -1;
        double best_de = std::numeric_limits<double>::max();
        for (size_t i = 0; i < n && i < input.entries.size(); ++i) {
            if (!alive[i] || !is_new_entry(input.entries[i]))
                continue;
            for (size_t j = 0; j < n; ++j) {
                if (!alive[j] || j == i || !same_family(ctx.families, (int)i, (int)j))
                    continue;
                const double de = Slic3r::compute_delta_e(rgb_from_rgba(input.colors_rgba[i]),
                                                          input.colors_rgba[j]);
                if (de < best_de) {
                    best_de = de;
                    best_new = (int)i;
                    best_other = (int)j;
                }
            }
        }
        if (best_new < 0 || best_other < 0)
            break;

        int survivor = best_other;
        int absorbed = best_new;
        const bool other_is_new = best_other < (int)input.entries.size() &&
                                  is_new_entry(input.entries[best_other]);
        if (other_is_new) {
            const double area_new = best_new < (int)ctx.areas.size() ? ctx.areas[best_new] : 0.0;
            const double area_other = best_other < (int)ctx.areas.size() ? ctx.areas[best_other] : 0.0;
            if (area_new > area_other ||
                (area_new == area_other &&
                 display_number_of(input, best_new) < display_number_of(input, best_other))) {
                survivor = best_new;
                absorbed = best_other;
            }
        }
        absorb_filament(absorbed, survivor, alive, plan.matches, input, ctx.families, &ctx.areas);
    }

    plan.after_chips = chips_from_alive(input, alive, ctx.areas, ctx.total_area);
    finish_plan(plan, max_count, count_alive(alive));
    return plan;
}

TextureOverLimitPlan compute_discard_plan(const TextureOverLimitInput& input, OverLimitSolveContext ctx)
{
    TextureOverLimitPlan plan;
    plan.matches = input.matches;
    const size_t n = input.colors_rgba.size();
    std::vector<char> alive(n, 1);
    std::vector<char> discarded(n, 0);
    plan.before_chips = chips_from_alive(input, alive, ctx.areas, ctx.total_area);

    const size_t max_count = input.max_count;
    while ((size_t)count_alive(alive) > max_count) {
        int victim = -1;
        double best_area = std::numeric_limits<double>::max();
        int best_display = std::numeric_limits<int>::max();
        for (size_t i = 0; i < n && i < input.entries.size(); ++i) {
            if (!alive[i] || !is_new_entry(input.entries[i]))
                continue;
            if (find_closest_alive((int)i, alive, input, ctx.families) < 0)
                continue;
            const double area = i < ctx.areas.size() ? ctx.areas[i] : 0.0;
            const int disp = display_number_of(input, (int)i);
            if (area < best_area - 1e-12 || (std::abs(area - best_area) <= 1e-12 && disp < best_display)) {
                best_area = area;
                best_display = disp;
                victim = (int)i;
            }
        }
        if (victim < 0)
            break;
        const int dest = find_closest_alive(victim, alive, input, ctx.families);
        if (dest < 0)
            break;
        remap_matches(plan.matches, victim, dest, input.colors_rgba);
        alive[victim] = 0;
        discarded[victim] = 1;
        drop_dependent_mixed(victim, alive, plan.matches, input, ctx.families);
        for (size_t i = 0; i < alive.size(); ++i) {
            if (!alive[i] && !discarded[i] && i < input.entries.size() && is_new_entry(input.entries[i]))
                discarded[i] = 1;
        }
    }

    plan.kept_chips = chips_from_alive(input, alive, ctx.areas, ctx.total_area);
    plan.discarded_chips = chips_from_alive(input, discarded, ctx.areas, ctx.total_area);
    plan.after_chips = plan.kept_chips;
    finish_plan(plan, max_count, count_alive(alive));
    return plan;
}

} // namespace

TextureOverLimitPlan compute_texture_overlimit_merge_plan(const TextureOverLimitInput& input)
{
    return compute_merge_plan(input, make_solve_context(input));
}

TextureOverLimitPlan compute_texture_overlimit_discard_plan(const TextureOverLimitInput& input)
{
    return compute_discard_plan(input, make_solve_context(input));
}

TextureImportOverLimitDialog::TextureImportOverLimitDialog(wxWindow* parent, TextureOverLimitInput input)
    : DPIDialog(parent, wxID_ANY, _L("Error Prompt"), wxDefaultPosition, wxDefaultSize,
                wxCAPTION | wxCLOSE_BOX)
    , m_input(std::move(input))
{
    const OverLimitSolveContext ctx = make_solve_context(m_input);
    m_merge_plan = compute_merge_plan(m_input, ctx);
    m_discard_plan = compute_discard_plan(m_input, ctx);
    build_ui();
    apply_dialog_geometry(false);
    // macOS only has a real NSWindow after show; Windows can apply immediately.
    on_window_geometry(this, [this]() { apply_dialog_geometry(true); });
    wxGetApp().UpdateDlgDarkUI(this);
}

const std::vector<Slic3r::FilamentMatch>& TextureImportOverLimitDialog::selected_matches() const
{
    return m_mode == TextureOverLimitMode::MergeSimilar ? m_merge_plan.matches : m_discard_plan.matches;
}

void TextureImportOverLimitDialog::on_dpi_changed(const wxRect&)
{
    apply_dialog_geometry(IsShown());
    if (m_radio_merge)
        m_radio_merge->Rescale();
    if (m_radio_discard)
        m_radio_discard->Rescale();
    if (m_btn_ok) {
        m_btn_ok->SetMinSize(wxSize(FromDIP(60), FromDIP(24)));
        m_btn_ok->SetCornerRadius(FromDIP(12));
        style_primary_button(m_btn_ok);
    }
    if (m_btn_cancel) {
        m_btn_cancel->SetMinSize(wxSize(FromDIP(60), FromDIP(24)));
        m_btn_cancel->SetCornerRadius(FromDIP(12));
        style_secondary_button(m_btn_cancel);
    }
    Layout();
    Refresh();
    refresh_previews();
}

void TextureImportOverLimitDialog::style_primary_button(Button* btn)
{
    texture_import_style_primary_button(btn);
}

void TextureImportOverLimitDialog::style_secondary_button(Button* btn)
{
    texture_import_style_secondary_button(btn);
}

void TextureImportOverLimitDialog::on_sys_color_changed()
{
    apply_theme();
}

void TextureImportOverLimitDialog::apply_theme()
{
    wxGetApp().UpdateDlgDarkUI(this);
    const wxColour dialog_bg = dark_or(*wxWHITE, wxColour(0x2D, 0x2D, 0x31));
    const wxColour dialog_fg = dark_or(wxColour(0x26, 0x2E, 0x30), wxColour(0xEF, 0xEF, 0xF0));
    const wxColour card_bg = dark_or(wxColour(0xF8, 0xF8, 0xF8), wxColour(0x2D, 0x2D, 0x31));
    const wxColour preview_bg = dark_or(wxColour(238, 238, 238), wxColour(0x3E, 0x3E, 0x45));
    const wxColour tag_bg = dark_or(wxColour(255, 255, 255), wxColour(0x54, 0x54, 0x5B));
    const wxColour tag_fg = dark_or(wxColour(0x6B, 0x6B, 0x6B), wxColour(0xD0, 0xD0, 0xD2));
    const wxColour status_fg = dark_or(wxColour(0xE1, 0x47, 0x47), wxColour(0xFF, 0x8A, 0x80));
    SetBackgroundColour(dialog_bg);
    SetForegroundColour(dialog_fg);
    if (m_hint_label) {
        m_hint_label->SetForegroundColour(dialog_fg);
        m_hint_label->SetBackgroundColour(dialog_bg);
    }
    auto restyle_status = [&](Label* label) {
        if (!label)
            return;
        label->SetForegroundColour(status_fg);
        label->SetBackgroundColour(dialog_bg);
    };
    restyle_status(m_merge_status);
    restyle_status(m_discard_status);
    auto set_bg_recursive = [&](auto&& self, wxWindow* w, const wxColour& bg) -> void {
        if (!w)
            return;
        w->SetBackgroundColour(bg);
        for (wxWindow* child : w->GetChildren())
            self(self, child, bg);
    };
    set_bg_recursive(set_bg_recursive, m_merge_card, card_bg);
    set_bg_recursive(set_bg_recursive, m_discard_card, card_bg);
    const wxColour caption_fg = dark_or(wxColour(0xAC, 0xAC, 0xAC), wxColour(0x81, 0x81, 0x83));
    auto restyle_caption_labels = [&](auto&& self, wxWindow* w) -> void {
        if (!w)
            return;
        if (auto* st = dynamic_cast<wxStaticText*>(w)) {
            st->SetForegroundColour(caption_fg);
            if (wxWindow* parent = st->GetParent())
                st->SetBackgroundColour(parent->GetBackgroundColour());
        }
        for (wxWindow* child : w->GetChildren())
            self(self, child);
    };
    restyle_caption_labels(restyle_caption_labels, m_merge_card);
    restyle_caption_labels(restyle_caption_labels, m_discard_card);
    auto restyle_title = [&](wxStaticText* label) {
        if (!label)
            return;
        label->SetForegroundColour(dialog_fg);
        label->SetBackgroundColour(dialog_bg);
    };
    restyle_title(m_merge_title);
    restyle_title(m_discard_title);
    if (m_tag_merge)
        m_tag_merge->set_theme(tag_bg, tag_fg, preview_bg);
    if (m_tag_discard)
        m_tag_discard->set_theme(tag_bg, tag_fg, preview_bg);
    style_primary_button(m_btn_ok);
    style_secondary_button(m_btn_cancel);
    const int radius = FromDIP(8);
    const wxColour preview_bd = dark_or(wxColour(206, 206, 206), wxColour(0x54, 0x54, 0x5B));
    if (m_preview_merge)
        m_preview_merge->set_rounded_corners(TexturePreviewCanvas::RoundedCornerSide::All, radius, 1,
                                             GetBackgroundColour(), preview_bd);
    if (m_preview_discard)
        m_preview_discard->set_rounded_corners(TexturePreviewCanvas::RoundedCornerSide::All, radius, 1,
                                               GetBackgroundColour(), preview_bd);
    Layout();
    Refresh();
    refresh_previews();
}

void TextureImportOverLimitDialog::populate_preview(TexturePreviewCanvas* canvas,
                                                    const TextureOverLimitPlan& plan)
{
    if (!canvas)
        return;
    // render_mesh() used to require original vertices; seed both so FilamentMap
    // can draw the remeshed geometry and the camera bounding box is valid.
    canvas->set_mesh_data(m_input.painted.vertices, m_input.painted.indices);
    canvas->set_painted_mesh_data(m_input.painted.vertices, m_input.painted.indices);
    canvas->set_face_colors(m_input.painted.face_colors);
    canvas->set_filament_color_map(color_map_from_matches(plan.matches, m_input.colors_rgba));
    canvas->set_render_mode(TexturePreviewCanvas::RenderMode::FilamentMap);
    canvas->set_reset_overlay_align(TexturePreviewCanvas::ResetOverlayAlign::BottomRight);
    canvas->set_view_state(m_input.view, false);
    const int radius = FromDIP(8);
    canvas->set_rounded_corners(TexturePreviewCanvas::RoundedCornerSide::All, radius, 1,
                                GetBackgroundColour(),
                                dark_or(wxColour(206, 206, 206), wxColour(0x54, 0x54, 0x5B)));
}

void TextureImportOverLimitDialog::bind_preview_cameras()
{
    if (!m_preview_merge || !m_preview_discard)
        return;
    m_preview_merge->set_view_changed_callback(
        [this](const TexturePreviewCanvas::ViewState& state) {
            if (m_preview_discard)
                m_preview_discard->set_view_state(state, false);
        });
    m_preview_discard->set_view_changed_callback(
        [this](const TexturePreviewCanvas::ViewState& state) {
            if (m_preview_merge)
                m_preview_merge->set_view_state(state, false);
        });
}

void TextureImportOverLimitDialog::refresh_previews()
{
    if (m_preview_merge)
        m_preview_merge->Refresh();
    if (m_preview_discard)
        m_preview_discard->Refresh();
}

wxWindow* TextureImportOverLimitDialog::create_preview_card(wxWindow* parent,
                                                            const wxString& tag,
                                                            TexturePreviewCanvas*& canvas,
                                                            const TextureOverLimitPlan& plan)
{
    const int preview_radius = FromDIP(8);

    auto* half = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                             wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE);
    half->SetBackgroundColour(dark_or(wxColour(238, 238, 238), wxColour(0x3E, 0x3E, 0x45)));
    half->SetBackgroundStyle(wxBG_STYLE_PAINT);
    half->SetMinSize(wxSize(FromDIP(286), FromDIP(216)));
    half->Bind(wxEVT_PAINT, [preview_radius](wxPaintEvent& e) {
        auto* p = static_cast<wxPanel*>(e.GetEventObject());
        texture_import_paint(p, [p, preview_radius](wxDC& dc) {
            const wxSize sz = p->GetClientSize();
            const wxColour preview_bg = dark_or(wxColour(238, 238, 238), wxColour(0x3E, 0x3E, 0x45));
            const wxColour preview_bd = dark_or(wxColour(206, 206, 206), wxColour(0x54, 0x54, 0x5B));
            dc.SetBrush(wxBrush(p->GetParent()->GetBackgroundColour()));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawRectangle(0, 0, sz.x, sz.y);
            dc.SetBrush(wxBrush(preview_bg));
            dc.SetPen(wxPen(preview_bd, 1));
            dc.DrawRoundedRectangle(0, 0, sz.x, sz.y, preview_radius);
        });
    });

    auto* half_sizer = new wxBoxSizer(wxVERTICAL);
    auto* canvas_host = new wxPanel(half, wxID_ANY);
    canvas_host->SetBackgroundColour(dark_or(wxColour(238, 238, 238), wxColour(0x3E, 0x3E, 0x45)));
    wxGLAttributes canvas_attrs;
    canvas_attrs.PlatformDefaults().RGBA().DoubleBuffer().Depth(24).EndList();
    canvas = new TexturePreviewCanvas(canvas_host, canvas_attrs);
    auto* host_sizer = new wxBoxSizer(wxVERTICAL);
    host_sizer->Add(canvas, 1, wxEXPAND);
    canvas_host->SetSizer(host_sizer);

    auto* tag_panel = new PreviewTagPanel(canvas_host, tag,
                                          dark_or(wxColour(255, 255, 255), wxColour(0x54, 0x54, 0x5B)),
                                          dark_or(wxColour(0x6B, 0x6B, 0x6B), wxColour(0xD0, 0xD0, 0xD2)),
                                          dark_or(wxColour(238, 238, 238), wxColour(0x3E, 0x3E, 0x45)));
    if (&canvas == &m_preview_merge)
        m_tag_merge = tag_panel;
    else if (&canvas == &m_preview_discard)
        m_tag_discard = tag_panel;
    auto position_tag = [canvas_host, tag_panel]() {
        tag_panel->InvalidateBestSize();
        tag_panel->Fit();
        const wxSize host_sz = canvas_host->GetClientSize();
        const wxSize tag_sz = tag_panel->GetBestSize();
        tag_panel->SetSize(tag_sz);
        tag_panel->SetPosition(wxPoint(std::max(0, (host_sz.x - tag_sz.x) / 2), 0));
        tag_panel->Raise();
    };
    canvas_host->Bind(wxEVT_SIZE, [position_tag](wxSizeEvent& e) {
        e.Skip();
        position_tag();
    });
    position_tag();

    half_sizer->Add(canvas_host, 1, wxEXPAND | wxALL, 1);
    half->SetSizer(half_sizer);
    populate_preview(canvas, plan);
    return half;
}

wxPanel* TextureImportOverLimitDialog::create_merge_card(wxWindow* parent)
{
    auto* card = new wxPanel(parent, wxID_ANY);
    card->SetBackgroundColour(dark_or(wxColour(0xF8, 0xF8, 0xF8), wxColour(0x2D, 0x2D, 0x31)));
    card->SetMinSize(wxSize(FromDIP(363), FromDIP(216)));
    card->SetBackgroundStyle(wxBG_STYLE_PAINT);
    card->Bind(wxEVT_PAINT, [](wxPaintEvent& e) {
        auto* p = static_cast<wxPanel*>(e.GetEventObject());
        texture_import_paint(p, [p](wxDC& dc) {
            const wxSize sz = p->GetClientSize();
            const wxColour bg = dark_or(wxColour(0xF8, 0xF8, 0xF8), wxColour(0x2D, 0x2D, 0x31));
            const wxColour bd = dark_or(wxColour(0xEE, 0xEE, 0xEE), wxColour(0x54, 0x54, 0x5B));
            dc.SetBrush(wxBrush(p->GetParent()->GetBackgroundColour()));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawRectangle(0, 0, sz.x, sz.y);
            dc.SetBrush(wxBrush(bg));
            dc.SetPen(wxPen(bd, 1));
            dc.DrawRoundedRectangle(0, 0, sz.x, sz.y, p->FromDIP(8));
        });
    });
    card->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) { select_mode(TextureOverLimitMode::MergeSimilar); });

    auto* scroll = new wxScrolledWindow(card, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    scroll->SetBackgroundColour(card->GetBackgroundColour());
    scroll->SetScrollRate(0, FromDIP(8));
    auto* inner = new wxBoxSizer(wxVERTICAL);
    inner->Add(make_chip_section(scroll, _L("Before merge"), m_merge_plan.before_chips, false),
               0, wxEXPAND | wxBOTTOM, FromDIP(12));
    inner->Add(make_chip_section(scroll, _L("After merge"), m_merge_plan.after_chips, false),
               0, wxEXPAND);
    scroll->SetSizer(inner);
    scroll->Bind(wxEVT_SIZE, [scroll](wxSizeEvent& e) {
        e.Skip();
        scroll->FitInside();
    });

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(scroll, 1, wxEXPAND | wxALL, FromDIP(12));
    card->SetSizer(sizer);
    m_merge_card = card;
    m_merge_scroll = scroll;
    return card;
}

wxPanel* TextureImportOverLimitDialog::create_discard_card(wxWindow* parent)
{
    auto* card = new wxPanel(parent, wxID_ANY);
    card->SetBackgroundColour(dark_or(wxColour(0xF8, 0xF8, 0xF8), wxColour(0x2D, 0x2D, 0x31)));
    card->SetMinSize(wxSize(FromDIP(363), FromDIP(222)));
    card->SetBackgroundStyle(wxBG_STYLE_PAINT);
    card->Bind(wxEVT_PAINT, [](wxPaintEvent& e) {
        auto* p = static_cast<wxPanel*>(e.GetEventObject());
        texture_import_paint(p, [p](wxDC& dc) {
            const wxSize sz = p->GetClientSize();
            const wxColour bg = dark_or(wxColour(0xF8, 0xF8, 0xF8), wxColour(0x2D, 0x2D, 0x31));
            const wxColour bd = dark_or(wxColour(0xEE, 0xEE, 0xEE), wxColour(0x54, 0x54, 0x5B));
            dc.SetBrush(wxBrush(p->GetParent()->GetBackgroundColour()));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawRectangle(0, 0, sz.x, sz.y);
            dc.SetBrush(wxBrush(bg));
            dc.SetPen(wxPen(bd, 1));
            dc.DrawRoundedRectangle(0, 0, sz.x, sz.y, p->FromDIP(8));
        });
    });
    card->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) { select_mode(TextureOverLimitMode::DiscardSmallArea); });

    auto* scroll = new wxScrolledWindow(card, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    scroll->SetBackgroundColour(card->GetBackgroundColour());
    scroll->SetScrollRate(0, FromDIP(8));
    auto* inner = new wxBoxSizer(wxVERTICAL);
    inner->Add(make_chip_section(scroll, _L("Kept filaments"), m_discard_plan.kept_chips, true),
               0, wxEXPAND | wxBOTTOM, FromDIP(12));
    inner->Add(make_chip_section(scroll, _L("Discarded filaments"), m_discard_plan.discarded_chips, true),
               0, wxEXPAND);
    scroll->SetSizer(inner);
    scroll->Bind(wxEVT_SIZE, [scroll](wxSizeEvent& e) {
        e.Skip();
        scroll->FitInside();
    });

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(scroll, 1, wxEXPAND | wxALL, FromDIP(12));
    card->SetSizer(sizer);
    m_discard_card = card;
    m_discard_scroll = scroll;
    return card;
}

wxWindow* TextureImportOverLimitDialog::create_option_block(wxWindow* parent,
                                                            TextureOverLimitMode mode,
                                                            const wxString& title,
                                                            RadioBox*& radio)
{
    auto* block = new wxPanel(parent, wxID_ANY);
    block->SetBackgroundColour(GetBackgroundColour());
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    auto* title_row = new wxBoxSizer(wxHORIZONTAL);
    radio = new RadioBox(block);
    radio->SetValue(mode == m_mode);
    radio->Bind(wxEVT_TOGGLEBUTTON, [this, mode](wxCommandEvent&) { select_mode(mode); });
    auto* label = new wxStaticText(block, wxID_ANY, title);
    label->SetFont(Label::Body_14);
    label->SetForegroundColour(dark_or(wxColour(0x26, 0x2E, 0x30), wxColour(0xEF, 0xEF, 0xF0)));
    label->SetBackgroundColour(block->GetBackgroundColour());
    label->Bind(wxEVT_LEFT_DOWN, [this, mode](wxMouseEvent&) { select_mode(mode); });
    if (mode == TextureOverLimitMode::MergeSimilar)
        m_merge_title = label;
    else
        m_discard_title = label;
    title_row->Add(radio, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    title_row->Add(label, 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(title_row, 0, wxEXPAND | wxBOTTOM, FromDIP(8));

    const TextureOverLimitPlan& plan =
        mode == TextureOverLimitMode::MergeSimilar ? m_merge_plan : m_discard_plan;
    const wxString status = plan_unresolved_hint(plan, m_input.max_count, (int)m_input.colors_rgba.size());
    if (!status.empty()) {
        auto* status_label = new Label(block, Label::Body_12, status);
        status_label->SetForegroundColour(dark_or(wxColour(0xE1, 0x47, 0x47), wxColour(0xFF, 0x8A, 0x80)));
        status_label->SetBackgroundColour(block->GetBackgroundColour());
        status_label->Wrap(FromDIP(650));
        if (mode == TextureOverLimitMode::MergeSimilar)
            m_merge_status = status_label;
        else
            m_discard_status = status_label;
        sizer->Add(status_label, 0, wxEXPAND | wxLEFT | wxBOTTOM, FromDIP(24));
    }

    auto* body = new wxBoxSizer(wxHORIZONTAL);
    if (mode == TextureOverLimitMode::MergeSimilar) {
        body->Add(create_merge_card(block), 0, wxRIGHT, FromDIP(12));
        body->Add(create_preview_card(block, _L("After merging"), m_preview_merge, m_merge_plan), 1, wxEXPAND);
    } else {
        body->Add(create_discard_card(block), 0, wxRIGHT, FromDIP(12));
        body->Add(create_preview_card(block, _L("After merging"), m_preview_discard, m_discard_plan), 1, wxEXPAND);
    }
    sizer->Add(body, 1, wxEXPAND | wxLEFT, FromDIP(24));
    block->SetSizer(sizer);
    return block;
}


void TextureImportOverLimitDialog::select_mode(TextureOverLimitMode mode)
{
    m_mode = mode;
    if (m_radio_merge)
        m_radio_merge->SetValue(mode == TextureOverLimitMode::MergeSimilar);
    if (m_radio_discard)
        m_radio_discard->SetValue(mode == TextureOverLimitMode::DiscardSmallArea);
}

void TextureImportOverLimitDialog::apply_dialog_geometry(bool center)
{
    const wxSize min_client(FromDIP(738), FromDIP(680));
    const wxSize target_client(FromDIP(738), FromDIP(680));
    SetMinClientSize(min_client);

    // Before the native window exists (macOS pre-SHOW), SetClientSize is a no-op.
    if (center || IsShown()) {
        if (m_hint_label)
            m_hint_label->Wrap(FromDIP(650));
        if (m_merge_status)
            m_merge_status->Wrap(FromDIP(650));
        if (m_discard_status)
            m_discard_status->Wrap(FromDIP(650));
        const wxSize cur = GetClientSize();
        const wxSize next(std::max(cur.x, target_client.x),
                          std::max(cur.y, target_client.y));
        if (next != cur)
            SetClientSize(next);
        Layout();
        if (center)
            CenterOnParent();
        refresh_previews();
    }
}

void TextureImportOverLimitDialog::build_ui()
{
    SetBackgroundColour(dark_or(*wxWHITE, wxColour(0x2D, 0x2D, 0x31)));
    auto* main = new wxBoxSizer(wxVERTICAL);

    const int count = (int)m_input.colors_rgba.size();
    m_hint_label = new Label(this, Label::Body_14,
        format_wxstr(
            _L("The project supports up to %1% filaments. Current count is %2%, which exceeds the limit. Please choose how to proceed."),
            (int)m_input.max_count, count));
    m_hint_label->SetForegroundColour(dark_or(wxColour(0x26, 0x2E, 0x30), wxColour(0xEF, 0xEF, 0xF0)));
    m_hint_label->SetBackgroundColour(GetBackgroundColour());
    main->Add(m_hint_label, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(24));

    main->Add(create_option_block(this, TextureOverLimitMode::MergeSimilar,
                                  _L("Prioritize merging similar-color filaments"), m_radio_merge),
              1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(24));
    main->Add(create_option_block(this, TextureOverLimitMode::DiscardSmallArea,
                                  _L("Prioritize merging small-area filaments"), m_radio_discard),
              1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(16));

    auto* btns = new wxBoxSizer(wxHORIZONTAL);
    btns->AddStretchSpacer();
    m_btn_ok = new Button(this, _L("OK"));
    m_btn_ok->SetMinSize(wxSize(FromDIP(60), FromDIP(24)));
    m_btn_ok->SetCornerRadius(FromDIP(12));
    style_primary_button(m_btn_ok);
    m_btn_ok->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_OK); });
    m_btn_cancel = new Button(this, _L("Cancel"));
    m_btn_cancel->SetMinSize(wxSize(FromDIP(60), FromDIP(24)));
    m_btn_cancel->SetCornerRadius(FromDIP(12));
    style_secondary_button(m_btn_cancel);
    m_btn_cancel->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CANCEL); });
    btns->Add(m_btn_ok, 0, wxRIGHT, FromDIP(16));
    btns->Add(m_btn_cancel, 0);
    main->Add(btns, 0, wxEXPAND | wxALL, FromDIP(24));

    SetSizer(main);
    bind_preview_cameras();
    Layout();
}

}} // namespace Slic3r::GUI
