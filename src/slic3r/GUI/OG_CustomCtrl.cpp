#include "OG_CustomCtrl.hpp"
#include "OptionsGroup.hpp"
#include "ParamTooltip.hpp"
#include "Plater.hpp"
#include "GUI_App.hpp"
#include "MsgDialog.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/PresetBundle.hpp"

#include <wx/utils.h>
#include <set>
#include <boost/algorithm/string/split.hpp>
#include "libslic3r/Utils.hpp"
#include "I18N.hpp"
#include "format.hpp"
#include <slic3r/GUI/Widgets/Label.hpp>
#include "Widgets/TextInput.hpp"
#include "libslic3r/Log.hpp"

namespace Slic3r { namespace GUI {

 // BBS: modify param ui style
    constexpr int titleWidth = 20;
    constexpr int ctrlWidth = 50;

#define DISABLE_BLINKING
#define DISABLE_UNDO_SYS

static bool is_point_in_rect(const wxPoint& pt, const wxRect& rect)
{
    return  rect.GetLeft() <= pt.x && pt.x <= rect.GetRight() &&
            rect.GetTop() <= pt.y && pt.y <= rect.GetBottom();
}

static wxSize get_bitmap_size(const wxBitmap& bmp)
{
#ifdef __APPLE__
    return bmp.GetScaledSize();
#else
    return bmp.GetSize();
#endif
}

OG_CustomCtrl::OG_CustomCtrl(   wxWindow*            parent,
                                OptionsGroup*        og,
                                const wxPoint&       pos /* = wxDefaultPosition*/,
                                const wxSize&        size/* = wxDefaultSize*/,
                                const wxValidator&   val /* = wxDefaultValidator*/,
                                const wxString&      name/* = wxEmptyString*/) :
    wxPanel(parent, wxID_ANY, pos, size, /*wxWANTS_CHARS |*/ wxBORDER_NONE | wxTAB_TRAVERSAL),
    opt_group(og)
{
    if (!wxOSX)
        SetDoubleBuffered(true);// SetDoubleBuffered exists on Win and Linux/GTK, but is missing on OSX
    SetBackgroundColour(parent->GetBackgroundColour());

    // BBS: new font
    m_font = Label::Body_14;
    SetFont(m_font);
    m_em_unit   = em_unit(m_parent);
    m_v_gap   = lround(1.2 * m_em_unit);
    m_v_gap2  = lround(0.8 * m_em_unit);
    m_h_gap   = lround(0.2 * m_em_unit);

    //m_bmp_mode_sz       = get_bitmap_size(create_scaled_bitmap("mode_simple", this, wxOSX ? 10 : 12));
    m_bmp_blinking_sz   = get_bitmap_size(create_scaled_bitmap("blank_16", this));

    init_ctrl_lines();// from og.lines()
    refresh_nozzle_mirror_context();

    this->Bind(wxEVT_PAINT,     &OG_CustomCtrl::OnPaint, this);
    this->Bind(wxEVT_MOTION,    &OG_CustomCtrl::OnMotion, this);
    this->Bind(wxEVT_LEFT_DOWN, &OG_CustomCtrl::OnLeftDown, this);
    this->Bind(wxEVT_LEAVE_WINDOW, &OG_CustomCtrl::OnLeaveWin, this);
}

void OG_CustomCtrl::refresh_nozzle_mirror_context()
{
    PresetBundle* preset_bundle = wxGetApp().preset_bundle;
    m_multi_nozzle_mode_enabled = preset_bundle != nullptr &&
        multi_nozzle_diameter_mode_enabled(preset_bundle->project_config);
    m_nozzle_mirror_enabled = false;
    m_primary_nozzle_diameter = 0.0;
    m_nozzle_diameters.clear();

    const bool has_mirror_lines = std::any_of(ctrl_lines.begin(), ctrl_lines.end(), [](const CtrlLine& line) {
        return line.og_line.nozzle_diameter_mirror;
    });
    if (!has_mirror_lines)
        return;

    if (m_multi_nozzle_mode_enabled) {
        // Always list every supported diameter, independent of the nozzles currently installed or used.
        m_nozzle_diameters = { 0.2, 0.4, 0.6, 0.8 };
        m_primary_nozzle_diameter =
            primary_nozzle_diameter(preset_bundle->printers.get_edited_preset().config);
        m_nozzle_mirror_enabled = m_primary_nozzle_diameter > EPSILON;
        if (!m_nozzle_mirror_enabled)
            m_nozzle_diameters.clear();
    }

    for (CtrlLine& line : ctrl_lines)
        line.update_nozzle_mirror_height();
}

void OG_CustomCtrl::init_ctrl_lines()
{
    const std::vector<Line>& og_lines = opt_group->get_lines();
    for (const Line& line : og_lines)
    {
        if (line.is_separator()) {
            ctrl_lines.emplace_back(CtrlLine(0, this, line));
            continue;
        }

        if (line.full_width && (
            // description line
            line.widget != nullptr ||
            // description line with widget (button)
            !line.get_extra_widgets().empty())
            )
            continue;

        const std::vector<Option>& option_set = line.get_options();
        wxCoord height;

        // if we have a single option with no label, no sidetext just add it directly to sizer
        if (option_set.size() == 1 && opt_group->label_width == 0 && option_set.front().opt.full_width &&
            option_set.front().opt.sidetext.size() == 0 && option_set.front().side_widget == nullptr &&
            line.get_extra_widgets().size() == 0)
        {
            height = m_bmp_blinking_sz.GetHeight() + m_v_gap;
            ctrl_lines.emplace_back(CtrlLine(height, this, line, true));
        }
        else if (opt_group->label_width != 0 && (!line.label.IsEmpty() || option_set.front().opt.gui_type == ConfigOptionDef::GUIType::legend) )
        {
            wxSize label_sz = GetTextExtent(line.label);
            if (opt_group->split_multi_line) {
                if (option_set.size() > 1) // BBS
                    height = (label_sz.y + m_v_gap2) * option_set.size() + m_v_gap - m_v_gap2;
                else
                    height = label_sz.y * (label_sz.GetWidth() > int(opt_group->label_width * m_em_unit) ? 2 : 1) + m_v_gap;
            } else {
                height = label_sz.y * (label_sz.GetWidth() > int(opt_group->label_width * m_em_unit) ? 2 : 1) + m_v_gap;
            }
            ctrl_lines.emplace_back(CtrlLine(height, this, line, false, opt_group->staticbox));
        }
        else
            assert(false);
    }
}

int OG_CustomCtrl::get_height(const Line& line)
{
    for (auto ctrl_line : ctrl_lines)
        if (&ctrl_line.og_line == &line)
            return ctrl_line.height;

    return 0;
}

int OG_CustomCtrl::get_content_height(const Line& line)
{
    for (const CtrlLine& ctrl_line : ctrl_lines)
        if (&ctrl_line.og_line == &line)
            return ctrl_line.content_height();

    return get_height(line);
}

wxPoint OG_CustomCtrl::get_pos(const Line& line, Field* field_in/* = nullptr*/)
{
    // BBS: new layout
    wxCoord v_pos = 0;
    wxCoord h_pos = get_title_width() * m_em_unit;

    auto correct_line_height = [](int& line_height, wxWindow* win)
    {
        int win_height = win->GetSize().GetHeight();
        if (line_height < win_height)
            line_height = win_height;
    };

    auto correct_line_height_for_sizer = [this](int& line_height, wxSizer* sizer)
    {
        if (!sizer) return;

        int total_height = 0;
        auto children = sizer->GetChildren();
        for (auto child : children) {
            if (child->IsWindow()) {
                total_height += child->GetWindow()->GetSize().GetHeight() + m_v_gap2;
            }
        }
        if (total_height > 0) {
            total_height -= m_v_gap2;
            if (line_height < total_height)
                line_height = total_height;
        }
    };

    auto correct_horiz_pos = [this](int& h_pos, Field* field) {
        if (m_max_win_width > 0 && field->getWindow()) {
            int win_width = field->getWindow()->GetSize().GetWidth();
            if (dynamic_cast<CheckBox*>(field))
                win_width *= 0.5;
            h_pos += m_max_win_width - win_width;
        }
    };

    auto add_label_width = [&h_pos, this](CtrlLine &ctrl_line, wxString const & label, int label_width) {
        wxClientDC dc(this);
        dc.SetFont(m_font);
        wxString multiline_text;
        auto size = Label::split_lines(dc, label_width, label, multiline_text);
        if (label_width > 0) size.x = label_width;
        h_pos += size.x + m_h_gap;
        if (ctrl_line.height < size.y)
            ctrl_line.height = size.y;
    };

    auto add_buttons_width = [&h_pos, this] (int blinking_button_width) {
#ifndef DISABLE_BLINKING
#  ifndef DISABLE_UNDO_SYS
        h_pos += 3 * blinking_button_width;
#  else
        h_pos += 2 * blinking_button_width;
#  endif
#else
#  ifndef DISABLE_UNDO_SYS
        h_pos += 2 * blinking_button_width;
#  else
        h_pos += 1 * blinking_button_width;
#  endif
#endif
    };

    for (CtrlLine& ctrl_line : ctrl_lines) {
        if (&ctrl_line.og_line == &line)
        {
            // BBS: new layout
            // h_pos = m_bmp_mode_sz.GetWidth() + m_h_gap;
            if (line.near_label_widget_win) {
                wxSize near_label_widget_sz = line.near_label_widget_win->GetSize();
                if (field_in)
                    h_pos += near_label_widget_sz.GetWidth() + m_h_gap;
                else
                    break;
            }

            wxString label = line.label;
            if (opt_group->label_width != 0)
                add_label_width(ctrl_line, label, opt_group->label_width * m_em_unit);

            int blinking_button_width = m_bmp_blinking_sz.GetWidth() + m_h_gap;

            if (line.widget) {
#ifndef DISABLE_BLINKING
                h_pos += blinking_button_width;
#endif

                for (auto child : line.widget_sizer->GetChildren())
                    if (child->IsWindow())
                        correct_line_height(ctrl_line.height, child->GetWindow());
                break;
            }

            if (opt_group->option_label_at_right) // BBS: position buttons at right
                add_buttons_width(blinking_button_width);

            // If we have a single option with no sidetext
            const std::vector<Option>& option_set = line.get_options();
            if (option_set.size() == 1 && option_set.front().opt.sidetext.size() == 0 &&
                option_set.front().side_widget == nullptr && line.get_extra_widgets().size() == 0)
            {
                // BBS: new layout
                // h_pos += 3 * blinking_button_width;
                Field* field = opt_group->get_field(option_set.front().opt_id);
                if (field->getWindow())
                    correct_line_height(ctrl_line.height, field->getWindow());
                else
                    correct_line_height_for_sizer(ctrl_line.height, field->getSizer());
                correct_horiz_pos(h_pos, field);
                break;
            }

            bool is_multioption_line = option_set.size() > 1;
            for (auto opt : option_set) {
                Field* field = opt_group->get_field(opt.opt_id);
                if (field->getWindow())
                    correct_line_height(ctrl_line.height, field->getWindow());
                else
                    correct_line_height_for_sizer(ctrl_line.height, field->getSizer());

                if (!opt_group->option_label_at_right) { // BBS: position option label at right
                    ConfigOptionDef option = opt.opt;
                    // add label if any
                    if (is_multioption_line && !option.label.empty()) {
                        //!            To correct translation by context have to use wxGETTEXT_IN_CONTEXT macro from wxWidget 3.1.1
                        auto label = (option.label == L_CONTEXT("Top", "Layers") || option.label == L_CONTEXT("Bottom", "Layers")) ? _CTX(option.label, "Layers") :
                                                                                                                                     _(option.label);
                        // BBS
                        // label += ":";
                        add_label_width(ctrl_line, label, opt_group->sublabel_width * m_em_unit);
                        h_pos += 8;
                    }

                }

                if (field == field_in) {
                    correct_horiz_pos(h_pos, field);
                    break;
                }
                if (opt_group->split_multi_line) {// BBS
                    v_pos += (ctrl_line.height - m_v_gap + m_v_gap2) / option_set.size();
                } else {
                    // BBS: new layout
                    h_pos += field->getWindow()->GetSize().x;
                    add_buttons_width(blinking_button_width);
                    if (option_set.size() == 1 && option_set.front().opt.full_width)
                        break;

                    // add sidetext if any
                    if (!field->combine_side_text() && (!opt.opt.sidetext.empty() || opt_group->sidetext_width > 0))
                        h_pos += opt_group->sidetext_width * m_em_unit + m_h_gap;

                    if (opt.opt_id != option_set.back().opt_id) //! istead of (opt != option_set.back())
                        h_pos += lround(0.6 * m_em_unit);
                }
            }
            break;
        }
        if (ctrl_line.is_visible)
            v_pos += ctrl_line.height;
    }

    return wxPoint(h_pos, v_pos);
}

// BBS: draw multi-line title
static void draw_title(wxDC& dc, wxPoint pos, const wxString& text, const wxColour* color, int width)
{
    wxString multiline_text;
    if (width > 0 && dc.GetTextExtent(text).x > width) {
        multiline_text = text;

        size_t idx = size_t(-1);
        size_t start = 0;
        for (size_t i = 0; i < multiline_text.Len(); i++)
        {
            if (multiline_text[i] == ' ')
            {
                if (dc.GetTextExtent(multiline_text.SubString(start, i)).x < width)
                    idx = i;
                else {
                    if (idx == size_t(-1))
                        idx = i;
                    multiline_text[idx] = '\n';
                    start = idx + 1;
                    idx = size_t(-1);
                }
            }
        }
        if (idx != size_t(-1))
            multiline_text[idx] = '\n';
    }

    if (!text.IsEmpty()) {
        const wxString& out_text = multiline_text.IsEmpty() ? text : multiline_text;
        wxCoord text_width, text_height;
        dc.GetMultiLineTextExtent(out_text, &text_width, &text_height);

        wxColour old_clr = dc.GetTextForeground();
        wxFont old_font = dc.GetFont();
        dc.SetTextForeground(color ? *color :
#ifdef _WIN32
            wxGetApp().get_label_clr_default());
#else
            wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
#endif /* _WIN32 */
        dc.DrawText(out_text, pos);
        dc.SetTextForeground(old_clr);
        dc.SetFont(old_font);

        if (width < 1)
            width = text_width;
    }
}

void OG_CustomCtrl::OnPaint(wxPaintEvent&)
{
    // case, when custom controll is destroyed but doesn't deleted from the evet loop
    if(!this->opt_group->custom_ctrl)
        return;

    const bool had_multi_nozzle_mode = m_multi_nozzle_mode_enabled;
    const bool had_mirror_enabled = m_nozzle_mirror_enabled;
    const double old_primary_diameter = m_primary_nozzle_diameter;
    const std::vector<double> old_diameters = m_nozzle_diameters;
    refresh_nozzle_mirror_context();
    if (!m_nozzle_mirror_refresh_pending &&
        (had_multi_nozzle_mode != m_multi_nozzle_mode_enabled ||
         had_mirror_enabled != m_nozzle_mirror_enabled ||
         std::abs(old_primary_diameter - m_primary_nozzle_diameter) > EPSILON ||
         old_diameters != m_nozzle_diameters)) {
        m_nozzle_mirror_refresh_pending = true;
        CallAfter([this]() {
            m_nozzle_mirror_refresh_pending = false;
            recalculate_and_refresh();
        });
    }

    wxPaintDC dc(this);

    wxCoord h_pos = get_title_width() * m_em_unit;
    wxCoord v_pos = 0;
    // BBS: new layout
    if (!GetLabel().IsEmpty()) {
        dc.SetFont(Label::Head_16);
        wxColour color = StateColor::darkModeColorFor("#283436");
        draw_title(dc, {0, v_pos}, GetLabel(), &color, h_pos);
        dc.SetFont(m_font);
    }
    for (CtrlLine& line : ctrl_lines) {
        if (!line.is_visible)
            continue;
        line.render(dc, h_pos, v_pos);
        v_pos += line.height;
    }
}

void OG_CustomCtrl::OnMotion(wxMouseEvent& event)
{
    const wxPoint pos = event.GetLogicalPosition(wxClientDC(this));
    wxString tooltip;

    // BBS: rich param tooltip
    CtrlLine* focusedLine = nullptr;
    // BBS

    bool suppress_hyperlinks = false;

    for (CtrlLine& line : ctrl_lines) {
        if (!line.is_visible) continue;
        line.is_focused = is_point_in_rect(pos, line.rect_label);
        if (line.is_focused) {
            if (!suppress_hyperlinks && !line.og_line.label_path.empty())
                tooltip = OptionsGroup::get_url(line.og_line.label_path) + "\n\n";
            tooltip += line.og_line.label_tooltip;
            // BBS: rich param tooltip
            focusedLine = &line;
            // BBS
            break;
        }

        const std::vector<Option> &option_set = line.og_line.get_options();

        Field *first_field   = option_set.empty() ? nullptr : opt_group->get_field(option_set.front().opt_id);
        auto  *multi_variant = dynamic_cast<MultiVariantTextCtrl *>(first_field);

        for (size_t opt_idx = 0; opt_idx < line.rects_undo_icon.size(); opt_idx++)
            if (is_point_in_rect(pos, line.rects_undo_icon[opt_idx])) {
                Field *field = nullptr;
                if (multi_variant && opt_idx < multi_variant->m_text_ctrls.size()) {
                    field = multi_variant->m_text_ctrls[opt_idx].text_ctrl.get();
                } else if (opt_idx < option_set.size()) {
                    field = opt_group->get_field(option_set[opt_idx].opt_id);
                }
                if (field)
                    tooltip = *field->undo_tooltip();
                break;
            }
        for (size_t opt_idx = 0; opt_idx < line.rects_undo_to_sys_icon.size(); opt_idx++)
            if (is_point_in_rect(pos, line.rects_undo_to_sys_icon[opt_idx])) {
                Field *field = nullptr;
                if (multi_variant && opt_idx < multi_variant->m_text_ctrls.size()) {
                    field = multi_variant->m_text_ctrls[opt_idx].text_ctrl.get();
                } else if (opt_idx < option_set.size()) {
                    field = opt_group->get_field(option_set[opt_idx].opt_id);
                }
                if (field)
                    tooltip = *field->undo_to_sys_tooltip();
                break;
            }
        // Mirrored per-nozzle value boxes are read-only and derived from the base value.
        if (tooltip.IsEmpty() && line.og_line.nozzle_diameter_mirror && !option_set.empty()) {
            for (const wxRect& rect : line.rects_nozzle_mirror_values)
                if (is_point_in_rect(pos, rect)) {
                    const std::string& mirror_key = option_set.front().opt.opt_key;
                    if (mirror_key == "initial_layer_print_height")
                        tooltip = _L("Read-only, automatically calculated from the base initial layer height");
                    else if (mirror_key == "layer_height")
                        tooltip = _L("Read-only, automatically calculated from the base layer height");
                    else
                        tooltip = _L("Read-only, automatically calculated from the base value");
                    break;
                }
        }
        if (!tooltip.IsEmpty())
            break;
    }

    // Set tooltips with information for each icon
    // BBS: rich param tooltip
    if (focusedLine != nullptr) {
        const std::vector<Option>& focused_opts = focusedLine->og_line.get_options();
        std::string opt_key = focused_opts.empty() ? std::string() : focused_opts.front().opt_id;
        auto tag_pos = opt_key.find('#');
        if (tag_pos != std::string::npos) opt_key.erase(tag_pos);
        wxPoint pos2 = {focusedLine->rect_label.GetRight() + 16, focusedLine->rect_label.y + focusedLine->rect_label.height / 2};
        pos2 = ClientToScreen(pos2);
        if (!opt_key.empty() && ParamTooltip::ShowFor(opt_key, focusedLine->og_line.label_path, pos2, focusedLine->og_line.label, focusedLine->og_line.label_tooltip))
            tooltip.clear();
        else
            ParamTooltip::Hide();
    }
    else {
        ParamTooltip::Hide();
    }
    if (GetToolTipText() != tooltip)
        this->SetToolTip(tooltip);
    // BBS

    Refresh();
    Update();
    event.Skip();
}

void OG_CustomCtrl::OnLeftDown(wxMouseEvent& event)
{
    const wxPoint pos = event.GetLogicalPosition(wxClientDC(this));

    refresh_nozzle_mirror_context();
    for (CtrlLine& line : ctrl_lines) {
        if (!line.is_visible) continue;
        if (line.og_line.nozzle_diameter_mirror && m_nozzle_mirror_enabled &&
            !line.rect_nozzle_mirror.IsEmpty() && is_point_in_rect(pos, line.rect_nozzle_mirror)) {
            line.nozzle_mirror_expanded = !line.nozzle_mirror_expanded;
            line.update_nozzle_mirror_height();
            recalculate_and_refresh();
            return;
        }
        if (!line.rect_nozzle_group.IsEmpty() && is_point_in_rect(pos, line.rect_nozzle_group) &&
            line.nozzle_group_active()) {
            line.nozzle_group_expanded = !line.nozzle_group_expanded;
            const std::string& opt_key = line.og_line.get_options().front().opt_id;
            for (CtrlLine& child : ctrl_lines)
                if (child.og_line.nozzle_group_parent == opt_key)
                    child.update_visibility(m_mode);
            recalculate_and_refresh();
            return;
        }
        if (line.launch_browser())
            return;

        const std::vector<Option> &option_set = line.og_line.get_options();

        Field *first_field   = option_set.empty() ? nullptr : opt_group->get_field(option_set.front().opt_id);
        auto  *multi_variant = dynamic_cast<MultiVariantTextCtrl *>(first_field);
        for (size_t opt_idx = 0; opt_idx < line.rects_undo_icon.size(); opt_idx++)
            if (is_point_in_rect(pos, line.rects_undo_icon[opt_idx])) {
                Field *field = nullptr;
                if (multi_variant && opt_idx < multi_variant->m_text_ctrls.size()) {
                    field = multi_variant->m_text_ctrls[opt_idx].text_ctrl.get();
                } else if (opt_idx < option_set.size()) {
                    field = opt_group->get_field(option_set[opt_idx].opt_id);
                }
                if (field)
                    field->on_back_to_initial_value();
                event.Skip();
                return;
            }
        for (size_t opt_idx = 0; opt_idx < line.rects_undo_to_sys_icon.size(); opt_idx++)
            if (is_point_in_rect(pos, line.rects_undo_to_sys_icon[opt_idx])) {
                const std::vector<Option>& option_set = line.og_line.get_options();
                Field *field = nullptr;
                if (multi_variant && opt_idx < multi_variant->m_text_ctrls.size()) {
                    field = multi_variant->m_text_ctrls[opt_idx].text_ctrl.get();
                } else if (opt_idx < option_set.size()) {
                    field = opt_group->get_field(option_set[opt_idx].opt_id);
                }
                if (field)
                    field->on_back_to_sys_value();
                event.Skip();
                return;
            }
    }

    SetFocusIgnoringChildren();
}

void OG_CustomCtrl::OnLeaveWin(wxMouseEvent& event)
{
    for (CtrlLine& line : ctrl_lines)
        line.is_focused = false;

    // BBS: rich param tooltip
    ParamTooltip::Hide();

    Refresh();
    Update();
    event.Skip();
}

const OG_CustomCtrl::CtrlLine* OG_CustomCtrl::find_nozzle_group_header(const std::string& opt_key) const
{
    const auto it = std::find_if(ctrl_lines.begin(), ctrl_lines.end(), [&opt_key](const CtrlLine& line) {
        return line.og_line.nozzle_group_header && line.og_line.get_options().front().opt_id == opt_key;
    });
    return it == ctrl_lines.end() ? nullptr : &*it;
}

bool OG_CustomCtrl::update_visibility(ConfigOptionMode mode)
{
    m_mode = mode;
    refresh_nozzle_mirror_context();

    // BBS: new layout
    wxCoord    h_pos = (ctrlWidth + get_title_width() - titleWidth) * m_em_unit;
    wxCoord    h_pos2 = get_title_width() * m_em_unit;
    wxCoord    v_pos = 0;

    size_t invisible_lines = 0;
    for (CtrlLine& line : ctrl_lines) {
        line.update_visibility(mode);
        if (line.is_visible)
            v_pos += (wxCoord)line.height;
        else
            invisible_lines++;
    }
    // BBS: multi-line title
    SetFont(Label::Head_16);
    wxSize label_sz = GetTextExtent(GetLabel());
    SetFont(m_font);
    auto lineHeight = label_sz.y;
    while (label_sz.x > h_pos2) {
        label_sz.x -= h_pos2;
        label_sz.y += lineHeight;
    }
    if (v_pos < label_sz.y) v_pos = label_sz.y;

    this->SetMinSize(wxSize(h_pos, v_pos));

    return invisible_lines != ctrl_lines.size();
}

// BBS: call by Tab/Page
void OG_CustomCtrl::fixup_items_positions()
{
    if (GetParent() == nullptr || GetPosition().y + GetSize().y < GetParent()->GetSize().y)
        return;
    for (CtrlLine& line : ctrl_lines) {
        line.correct_items_positions();
    }
}

void OG_CustomCtrl::correct_window_position(wxWindow* win, const Line& line, Field* field/* = nullptr*/)
{
    wxPoint pos = get_pos(line, field);
    int line_height = get_content_height(line);
    if (opt_group->split_multi_line) { // BBS
        if (line.get_options().size() > 1)
            line_height = (line_height - m_v_gap + m_v_gap2) / line.get_options().size();
    }
    pos.y += std::max(0, int(0.5 * (line_height - win->GetSize().y)));
    win->SetPosition(pos);
};

void OG_CustomCtrl::correct_widgets_position(wxSizer* widget, const Line& line, Field* field/* = nullptr*/) {
    auto children = widget->GetChildren();
    wxPoint line_pos = get_pos(line, field);
    int line_height = get_content_height(line);
    for (auto child : children)
        if (child->IsWindow()) {
            wxPoint pos = line_pos;
            wxSize  sz = child->GetWindow()->GetSize();
            pos.y += std::max(0, int(0.5 * (line_height - sz.y)));
            if (line.extra_widget_sizer && widget == line.extra_widget_sizer)
                pos.x += m_h_gap;
            child->GetWindow()->SetPosition(pos);
            line_pos.x += sz.x + m_h_gap;
        }
};

void OG_CustomCtrl::init_max_win_width()
{
    if (opt_group->ctrl_horiz_alignment == wxALIGN_RIGHT && m_max_win_width == 0)
        for (CtrlLine& line : ctrl_lines) {
            if (int max_win_width = line.get_max_win_width();
                m_max_win_width < max_win_width)
                m_max_win_width = max_win_width;
        }
}

int OG_CustomCtrl::get_title_width()
{
    if (!GetLabel().IsEmpty())
        return titleWidth;
    else
        return 2;
}

void OG_CustomCtrl::set_max_win_width(int max_win_width)
{
    if (m_max_win_width == max_win_width)
        return;
    m_max_win_width = max_win_width;
    for (CtrlLine& line : ctrl_lines)
        line.correct_items_positions();

    GetParent()->Layout();
}


void OG_CustomCtrl::msw_rescale()
{
#ifdef __WXOSX__
    return;
#endif
    // BBS: new font
    m_font = Label::Body_14;
    SetFont(m_font);
    m_em_unit   = em_unit(m_parent);
    m_v_gap     = lround(1.2 * m_em_unit);
    m_v_gap2     = lround(0.8 * m_em_unit);
    m_h_gap     = lround(0.2 * m_em_unit);

    //m_bmp_mode_sz = create_scaled_bitmap("mode_simple", this, wxOSX ? 10 : 12).GetSize();
    m_bmp_blinking_sz = create_scaled_bitmap("blank_16", this).GetSize();

    m_max_win_width = 0;

    wxCoord    h_pos = (ctrlWidth + get_title_width() - titleWidth) * m_em_unit;
    wxCoord    h_pos2 = get_title_width() * m_em_unit;
    wxCoord    v_pos = 0;
    for (CtrlLine& line : ctrl_lines) {
        line.msw_rescale();
        if (line.is_visible)
            v_pos += (wxCoord)line.height;
    }
    // BBS: multi-line title
    SetFont(Label::Head_16);
    wxSize label_sz = GetTextExtent(GetLabel());
    SetFont(m_font);
    auto lineHeight = label_sz.y;
    while (label_sz.x > h_pos2) {
        label_sz.x -= h_pos2;
        label_sz.y += lineHeight;
    }
    if (v_pos < label_sz.y) v_pos = label_sz.y;
    // BBS: new layout
    this->SetMinSize(wxSize(h_pos, v_pos));

    GetParent()->Layout();
}

void OG_CustomCtrl::update_line_height_for_field(const t_config_option_key &opt_id)
{
    Field *field = opt_group->get_field(opt_id);
    if (!field) return;

    auto *multi_variant = dynamic_cast<MultiVariantTextCtrl *>(field);
    if (!multi_variant || !multi_variant->getSizer()) return;

    for (CtrlLine &ctrl_line : ctrl_lines) {
        const std::vector<Option> &option_set = ctrl_line.og_line.get_options();
        if (option_set.size() == 1 && option_set.front().opt_id == opt_id) {
            ctrl_line.update_multi_variant_height();
            recalculate_and_refresh();
            return;
        }
    }
}

void OG_CustomCtrl::recalculate_and_refresh()
{
    refresh_nozzle_mirror_context();

    for (CtrlLine& line : ctrl_lines) {
        line.update_field_visibility();
        if (line.is_visible) {
            line.correct_items_positions();
        }
    }

    wxCoord h_pos = (ctrlWidth + get_title_width() - titleWidth) * m_em_unit;
    wxCoord v_pos = 0;

    for (CtrlLine& line : ctrl_lines) {
        if (line.is_visible)
            v_pos += line.height;
    }

    wxCoord h_pos2 = get_title_width() * m_em_unit;
    SetFont(Label::Head_16);
    wxSize label_sz = GetTextExtent(GetLabel());
    SetFont(m_font);
    auto lineHeight = label_sz.y;
    while (label_sz.x > h_pos2) {
        label_sz.x -= h_pos2;
        label_sz.y += lineHeight;
    }
    if (v_pos < label_sz.y) v_pos = label_sz.y;

    this->SetMinSize(wxSize(h_pos, v_pos));

    Refresh();
    Update();

    wxWindow* parent = GetParent();
    if (parent) {
        parent->Layout();
        parent->Refresh();

        if (auto* scroll_win = dynamic_cast<wxScrolledWindow*>(parent)) {
            scroll_win->FitInside();
        }
    }
}

void OG_CustomCtrl::refresh_nozzle_mirror_values(const t_config_option_key& opt_id)
{
    const bool has_mirror_lines = std::any_of(ctrl_lines.begin(), ctrl_lines.end(), [](const CtrlLine& line) {
        return line.og_line.nozzle_diameter_mirror;
    });
    if (!has_mirror_lines)
        return;

    if (!opt_id.empty()) {
        const auto it = std::find_if(ctrl_lines.begin(), ctrl_lines.end(), [&opt_id](const CtrlLine& line) {
            const auto& options = line.og_line.get_options();
            return line.og_line.nozzle_diameter_mirror && !options.empty() && options.front().opt_id == opt_id;
        });
        if (it == ctrl_lines.end())
            return;
    }

    wxCoord old_height = 0;
    for (const CtrlLine& line : ctrl_lines)
        old_height += line.height;
    refresh_nozzle_mirror_context();

    wxCoord new_height = 0;
    for (const CtrlLine& line : ctrl_lines)
        new_height += line.height;
    if (old_height != new_height)
        recalculate_and_refresh();
    else
        Refresh();
}

void OG_CustomCtrl::sys_color_changed()
{
}

OG_CustomCtrl::CtrlLine::CtrlLine(  wxCoord         height,
                                    OG_CustomCtrl*  ctrl,
                                    const Line&     og_line,
                                    bool            draw_just_act_buttons /* = false*/,
                                    bool            draw_mode_bitmap/* = true*/):
    height(height),
    base_height(height),
    ctrl(ctrl),
    og_line(og_line),
    draw_just_act_buttons(draw_just_act_buttons),
    draw_mode_bitmap(draw_mode_bitmap)
{
    size_t initial_size = og_line.get_options().size();
    rects_undo_icon.reserve(initial_size > 0 ? 3 : 1);
    rects_undo_to_sys_icon.reserve(initial_size > 0 ? 3 : 1);

    for (size_t i = 0; i < initial_size; i++) {
        rects_undo_icon.emplace_back(wxRect());
        rects_undo_to_sys_icon.emplace_back(wxRect());
    }
}

int OG_CustomCtrl::CtrlLine::get_max_win_width()
{
    int max_win_width = 0;
    if (!draw_just_act_buttons) {
        const std::vector<Option>& option_set = og_line.get_options();
        for (auto opt : option_set) {
            Field* field = ctrl->opt_group->get_field(opt.opt_id);
            if (field && field->getWindow())
                max_win_width = field->getWindow()->GetSize().GetWidth();
        }
    }

    return max_win_width;
}

wxCoord OG_CustomCtrl::CtrlLine::content_height() const
{
    return og_line.nozzle_diameter_mirror ? base_height : height;
}

void OG_CustomCtrl::CtrlLine::update_nozzle_mirror_height()
{
    const bool available = og_line.nozzle_diameter_mirror && ctrl->m_nozzle_mirror_enabled;
    if (!available)
        nozzle_mirror_expanded = false;
    height = base_height;
    if (available && nozzle_mirror_expanded)
        height += base_height * wxCoord(ctrl->m_nozzle_diameters.size());
    if (!available)
        rect_nozzle_mirror = wxRect();
}

void OG_CustomCtrl::CtrlLine::correct_items_positions()
{
    if (draw_just_act_buttons || !is_visible)
        return;

    if (og_line.near_label_widget_win)
        ctrl->correct_window_position(og_line.near_label_widget_win, og_line);
    if (og_line.widget_sizer)
        ctrl->correct_widgets_position(og_line.widget_sizer, og_line);
    if (og_line.extra_widget_sizer)
        ctrl->correct_widgets_position(og_line.extra_widget_sizer, og_line);

    const std::vector<Option>& option_set = og_line.get_options();
    for (auto opt : option_set) {
        Field* field = ctrl->opt_group->get_field(opt.opt_id);
        if (!field)
            continue;

        auto  parent        = field->m_parent;
        auto *multi_variant = dynamic_cast<MultiVariantTextCtrl *>(field);
        if (multi_variant && multi_variant->getSizer()) {
            wxPoint  base_pos   = ctrl->get_pos(og_line, field);
            wxSizer *main_sizer = multi_variant->getSizer();
            auto     children   = main_sizer->GetChildren();
            int single_line_height = children.empty() ? 0 : (height - ctrl->m_v_gap + ctrl->m_v_gap2) / children.size();

            wxPoint current_pos = base_pos;
            current_pos.y += 2;

            for (auto child : children) {
                if (child->IsSizer()) {
                    auto h_sizer = child->GetSizer();
                    int  item_x  = current_pos.x;
                    for (auto item : h_sizer->GetChildren()) {
                        if (item->IsWindow()) {
                            wxWindow *win = item->GetWindow();
                            wxSize    sz  = win->GetSize();

                            wxPoint pos = wxPoint(item_x, current_pos.y);
                            pos.y += std::max(0, int(0.5 * (single_line_height - sz.y)));
                            win->SetPosition(pos);

                            item_x += sz.x + ctrl->m_h_gap;
                        }
                    }
                    current_pos.y += single_line_height;
                }
            }
            continue;
        }

        if (field->getSizer())
            ctrl->correct_widgets_position(field->getSizer(), og_line, field);
        else if (field->getWindow())
            ctrl->correct_window_position(field->getWindow(), og_line, field);
    }
}

void OG_CustomCtrl::CtrlLine::msw_rescale()
{
    height = base_height;

    // if we have a single option with no label, no sidetext
    if (draw_just_act_buttons)
        height = get_bitmap_size(create_scaled_bitmap("empty")).GetHeight();

    if (ctrl->opt_group->label_width != 0 && !og_line.label.IsEmpty()) {
        const std::vector<Option> &option_set = og_line.get_options();

        if (option_set.size() == 1) {
            Field *field               = ctrl->opt_group->get_field(option_set.front().opt_id);
            auto  *multi_variant_field = dynamic_cast<MultiVariantTextCtrl *>(field);

            if (multi_variant_field && multi_variant_field->getSizer()) {
                update_multi_variant_height();
                correct_items_positions();
                return;
            }
        }

        wxSize label_sz = ctrl->GetTextExtent(og_line.label);
        if (ctrl->opt_group->split_multi_line) { // BBS
            const std::vector<Option> &option_set = og_line.get_options();
            if (option_set.size() > 1)
                height = (label_sz.y + ctrl->m_v_gap2) * option_set.size() + ctrl->m_v_gap - ctrl->m_v_gap2;
            else
                height = label_sz.y * (label_sz.GetWidth() > int(ctrl->opt_group->label_width * ctrl->m_em_unit) ? 2 : 1) + ctrl->m_v_gap;
        } else {
            height = label_sz.y * (label_sz.GetWidth() > int(ctrl->opt_group->label_width * ctrl->m_em_unit) ? 2 : 1) + ctrl->m_v_gap;
        }
    }

    base_height = height;
    update_nozzle_mirror_height();
    correct_items_positions();
}

void OG_CustomCtrl::CtrlLine::update_visibility(ConfigOptionMode mode)
{
    if (og_line.is_separator())
        return;
    const std::vector<Option>& option_set = og_line.get_options();

    const ConfigOptionMode& line_mode = option_set.front().opt.mode;
    is_visible = og_line.toggle_visible && line_mode <= mode;
    if (is_visible && !og_line.nozzle_group_parent.empty()) {
        const CtrlLine* header = ctrl->find_nozzle_group_header(og_line.nozzle_group_parent);
        is_visible = header == nullptr || header->nozzle_group_expanded;
    }

    if (draw_just_act_buttons)
        return;

    if (og_line.near_label_widget_win)
        og_line.near_label_widget_win->Show(is_visible);
    if (og_line.widget_sizer)
        og_line.widget_sizer->ShowItems(is_visible);
    if (og_line.extra_widget_sizer)
        og_line.extra_widget_sizer->ShowItems(is_visible);

    update_field_visibility();
    correct_items_positions();
}

bool OG_CustomCtrl::CtrlLine::multi_nozzle_auto_active() const
{
    return og_line.multi_nozzle_auto && ctrl->m_multi_nozzle_mode_enabled;
}

bool OG_CustomCtrl::CtrlLine::nozzle_group_active() const
{
    if (!og_line.nozzle_group_header)
        return false;
    const std::string& opt_key = og_line.get_options().front().opt_id;
    return std::any_of(ctrl->ctrl_lines.begin(), ctrl->ctrl_lines.end(), [&opt_key](const CtrlLine& line) {
        return line.og_line.nozzle_group_parent == opt_key && line.og_line.toggle_visible;
    });
}

void OG_CustomCtrl::CtrlLine::render_nozzle_group_arrow(wxDC& dc, wxCoord h_pos, wxCoord v_pos)
{
    if (!nozzle_group_active()) {
        rect_nozzle_group = wxRect();
        return;
    }

    const int arrow_size = std::max(4, ctrl->m_em_unit / 3);
    const int arrow_x = h_pos - lround(1.2 * ctrl->m_em_unit);
    const int arrow_y = v_pos + (height - arrow_size) / 2;
    rect_nozzle_group = wxRect(arrow_x - 4, arrow_y - 4, arrow_size + 8, arrow_size + 8);

    wxPoint arrow[3];
    if (nozzle_group_expanded) {
        arrow[0] = wxPoint(arrow_x, arrow_y);
        arrow[1] = wxPoint(arrow_x + arrow_size, arrow_y);
        arrow[2] = wxPoint(arrow_x + arrow_size / 2, arrow_y + arrow_size);
    } else {
        arrow[0] = wxPoint(arrow_x, arrow_y);
        arrow[1] = wxPoint(arrow_x, arrow_y + arrow_size);
        arrow[2] = wxPoint(arrow_x + arrow_size, arrow_y + arrow_size / 2);
    }
    const wxBrush old_brush = dc.GetBrush();
    const wxPen old_pen = dc.GetPen();
    const wxColour arrow_color = StateColor::darkModeColorFor(ThemeColor::TextMuted);
    dc.SetBrush(wxBrush(arrow_color));
    dc.SetPen(wxPen(arrow_color));
    dc.DrawPolygon(3, arrow);
    dc.SetBrush(old_brush);
    dc.SetPen(old_pen);
}

void OG_CustomCtrl::CtrlLine::update_field_visibility()
{
    if (og_line.is_separator() || draw_just_act_buttons)
        return;

    const bool show_field = is_visible && !multi_nozzle_auto_active() && !nozzle_group_active();
    if (!show_field) {
        std::fill(rects_undo_icon.begin(), rects_undo_icon.end(), wxRect());
        std::fill(rects_undo_to_sys_icon.begin(), rects_undo_to_sys_icon.end(), wxRect());
    }
    const std::vector<Option>& option_set = og_line.get_options();
    for (auto opt : option_set) {
        Field* field = ctrl->opt_group->get_field(opt.opt_id);
        if (!field)
            continue;

        if (field->getSizer()) {
            auto children = field->getSizer()->GetChildren();
            for (auto child : children) {
                if (child->IsWindow()) {
                    child->GetWindow()->Show(show_field);
                } else if (child->IsSizer()) {
                    auto sub_children = child->GetSizer()->GetChildren();
                    for (auto sub_child : sub_children) {
                        if (sub_child->IsWindow()) {
                            sub_child->GetWindow()->Show(show_field);
                        }
                    }
                }
            }
        }
        else if (field->getWindow())
            field->getWindow()->Show(show_field);
    }
}

void OG_CustomCtrl::CtrlLine::render_separator(wxDC& dc, wxCoord v_pos)
{
    wxPoint begin(ctrl->m_h_gap, v_pos);
    wxPoint end(ctrl->GetSize().GetWidth() - ctrl->m_h_gap, v_pos);

    wxPen pen, old_pen = pen = dc.GetPen();
    pen.SetColour(*wxLIGHT_GREY);
    dc.SetPen(pen);
    dc.DrawLine(begin, end);
    dc.SetPen(old_pen);
}

void OG_CustomCtrl::CtrlLine::render_multi_nozzle_auto(wxDC& dc, Field* field)
{
    if (!multi_nozzle_auto_active() || field == nullptr)
        return;

    std::fill(rects_undo_icon.begin(), rects_undo_icon.end(), wxRect());
    std::fill(rects_undo_to_sys_icon.begin(), rects_undo_to_sys_icon.end(), wxRect());

    wxPoint box_pos;
    wxSize box_size;
    if (field->getWindow() != nullptr) {
        box_pos = field->getWindow()->GetPosition();
        box_size = field->getWindow()->GetSize();
    } else if (field->getSizer() != nullptr) {
        box_pos = field->getSizer()->GetPosition();
        box_size = field->getSizer()->GetSize();
    } else {
        return;
    }

    const wxBrush old_brush = dc.GetBrush();
    const wxPen old_pen = dc.GetPen();
    const wxColour old_text_color = dc.GetTextForeground();
    const wxColour box_color = StateColor::darkModeColorFor(ThemeColor::Grey250);
    const wxColour border_color = StateColor::darkModeColorFor(ThemeColor::Grey350);
    const wxColour text_color = StateColor::darkModeColorFor(ThemeColor::TextMuted);
    const wxString text = _L("Auto");
    dc.SetFont(ctrl->m_font);
    const wxSize text_size = dc.GetTextExtent(text);

    dc.SetBrush(wxBrush(box_color));
    dc.SetPen(wxPen(border_color));
    dc.DrawRoundedRectangle(box_pos.x, box_pos.y, box_size.x, box_size.y, 3);
    dc.SetTextForeground(text_color);
    dc.DrawText(text, box_pos.x + ctrl->m_em_unit / 2,
                box_pos.y + (box_size.y - text_size.y) / 2);

    dc.SetBrush(old_brush);
    dc.SetPen(old_pen);
    dc.SetTextForeground(old_text_color);
}

void OG_CustomCtrl::CtrlLine::render_nozzle_mirrors(wxDC& dc, wxCoord h_pos, wxCoord v_pos, Field* field)
{
    rects_nozzle_mirror_values.clear();
    if (!og_line.nozzle_diameter_mirror || !ctrl->m_nozzle_mirror_enabled) {
        rect_nozzle_mirror = wxRect();
        return;
    }

    const int arrow_size = std::max(4, ctrl->m_em_unit / 3);
    const int arrow_x = h_pos - lround(1.2 * ctrl->m_em_unit);
    const int arrow_y = v_pos + (base_height - arrow_size) / 2;
    rect_nozzle_mirror = wxRect(arrow_x - 4, arrow_y - 4, arrow_size + 8, arrow_size + 8);

    wxPoint arrow[3];
    if (nozzle_mirror_expanded) {
        arrow[0] = wxPoint(arrow_x, arrow_y);
        arrow[1] = wxPoint(arrow_x + arrow_size, arrow_y);
        arrow[2] = wxPoint(arrow_x + arrow_size / 2, arrow_y + arrow_size);
    } else {
        arrow[0] = wxPoint(arrow_x, arrow_y);
        arrow[1] = wxPoint(arrow_x, arrow_y + arrow_size);
        arrow[2] = wxPoint(arrow_x + arrow_size, arrow_y + arrow_size / 2);
    }
    const wxBrush old_brush = dc.GetBrush();
    const wxPen old_pen = dc.GetPen();
    const wxColour old_text_color = dc.GetTextForeground();
    const wxColour arrow_color = StateColor::darkModeColorFor(ThemeColor::TextMuted);
    dc.SetBrush(wxBrush(arrow_color));
    dc.SetPen(wxPen(arrow_color));
    dc.DrawPolygon(3, arrow);

    if (!nozzle_mirror_expanded || field == nullptr || field->getWindow() == nullptr) {
        dc.SetBrush(old_brush);
        dc.SetPen(old_pen);
        return;
    }

    const std::string& opt_key = og_line.get_options().front().opt.opt_key;
    auto* config_group = dynamic_cast<ConfigOptionsGroup*>(ctrl->opt_group);
    const DynamicPrintConfig* config = config_group == nullptr ? nullptr : config_group->get_config();
    if (config == nullptr) {
        dc.SetBrush(old_brush);
        dc.SetPen(old_pen);
        return;
    }

    double authored = 0.0;
    try {
        const wxString authored_text = boost::any_cast<wxString>(config_group->config_value(opt_key, -1, false));
        if (!authored_text.ToDouble(&authored)) {
            dc.SetBrush(old_brush);
            dc.SetPen(old_pen);
            return;
        }
    } catch (const boost::bad_any_cast&) {
        BOOST_LOG_TRIVIAL(warning) << current_exception_message();
        dc.SetBrush(old_brush);
        dc.SetPen(old_pen);
        return;
    }

    const bool is_line_width = opt_key == "line_width" ||
        opt_key.size() > 11 && opt_key.compare(opt_key.size() - 11, 11, "_line_width") == 0;
    const wxWindow* parent_window = field->getWindow();
    const wxSize box_size = parent_window->GetSize();
    const int box_x = parent_window->GetPosition().x;
    const wxColour box_color = StateColor::darkModeColorFor(ThemeColor::Grey250);
    const wxColour border_color = StateColor::darkModeColorFor(ThemeColor::Grey350);
    const wxColour text_color = StateColor::darkModeColorFor(ThemeColor::TextMuted);

    // Reuse the parent TextInput's inner geometry so value/mm columns line up.
    const ::TextInput* text_input = dynamic_cast<const ::TextInput*>(parent_window);
    const wxTextCtrl* text_ctrl = text_input ? text_input->GetTextCtrl() : nullptr;
    const wxPoint text_pos = text_ctrl ? text_ctrl->GetPosition() : wxPoint(ctrl->m_em_unit / 2, 0);
    const wxSize text_size = text_ctrl ? text_ctrl->GetSize() : box_size;
    int text_margin_x = 0;
    if (text_ctrl) {
        const wxPoint margins = text_ctrl->GetMargins();
        if (margins.x > 0)
            text_margin_x = margins.x;
    }

    const wxFont old_font = dc.GetFont();
    // TextInput paints sidetext with its own font (Body_12), not the value font (Body_14).
    const wxFont unit_font = parent_window->GetFont();
    const wxString unit_text = _(og_line.get_options().front().opt.sidetext);
    for (size_t i = 0; i < ctrl->m_nozzle_diameters.size(); ++i) {
        dc.SetFont(ctrl->m_font);
        const double diameter = ctrl->m_nozzle_diameters[i];
        const int row_y = v_pos + base_height * wxCoord(i + 1);
        const wxString diameter_label = from_u8(format_diameter_to_str(diameter)) + "mm " + _L("nozzle");
        const wxSize label_size = dc.GetTextExtent(diameter_label);
        const int label_x = h_pos + lround(1.5 * ctrl->m_em_unit);
        const int label_y = row_y + (base_height - label_size.y) / 2;

        dc.SetTextForeground(text_color);
        dc.DrawText(diameter_label, label_x, label_y);

        const int box_y = row_y + (base_height - box_size.y) / 2;
        dc.SetBrush(wxBrush(box_color));
        dc.SetPen(wxPen(border_color));
        dc.DrawRoundedRectangle(box_x, box_y, box_size.x, box_size.y, 3);
        rects_nozzle_mirror_values.emplace_back(box_x, box_y, box_size.x, box_size.y);

        const double value = is_line_width ?
            Flow::resolve_extrusion_width_for_nozzle(
                opt_key, *config, float(ctrl->m_primary_nozzle_diameter), float(diameter),
                ctrl->m_multi_nozzle_mode_enabled) :
            authored * diameter / ctrl->m_primary_nozzle_diameter;
        const wxString value_text = double_to_string(value);
        const wxSize value_size = dc.GetTextExtent(value_text);
        dc.DrawText(value_text, box_x + text_pos.x + text_margin_x,
                    box_y + text_pos.y + (text_size.y - value_size.y) / 2);

        dc.SetFont(unit_font);
        const wxSize unit_size = dc.GetTextExtent(unit_text);
        const int unit_x = text_ctrl ? box_x + text_pos.x + text_size.x
                                      : box_x + box_size.x - unit_size.x - ctrl->m_em_unit / 2;
        const int unit_y = text_ctrl ? box_y + (box_size.y + text_size.y) / 2 - unit_size.y
                                      : box_y + (box_size.y - unit_size.y) / 2;
        dc.DrawText(unit_text, unit_x, unit_y);
    }

    dc.SetBrush(old_brush);
    dc.SetPen(old_pen);
    dc.SetFont(old_font);
    dc.SetTextForeground(old_text_color);
}

void OG_CustomCtrl::CtrlLine::render(wxDC& dc, wxCoord h_pos, wxCoord v_pos)
{
    if (is_separator()) {
        render_separator(dc, v_pos);
        return;
    }

    Field* field = ctrl->opt_group->get_field(og_line.get_options().front().opt_id);
    render_nozzle_mirrors(dc, h_pos, v_pos, field);
    render_nozzle_group_arrow(dc, h_pos, v_pos);
    render_multi_nozzle_auto(dc, field);

    auto *multi_variant_field = dynamic_cast<MultiVariantTextCtrl *>(field);
    bool  is_multi_variant    = (multi_variant_field != nullptr);
    if (is_multi_variant) {
        size_t variant_count = multi_variant_field->m_text_ctrls.size();
        ensure_rects_size(variant_count);

        wxCoord label_actual_v_pos = v_pos;
        if (ctrl->opt_group->label_width != 0 && !og_line.label.IsEmpty()) {
            wxString label = og_line.label;
            wxColour blink_color = StateColor::darkModeColorFor("#00AE42");

            dc.SetFont(ctrl->m_font);
            wxString multiline_text;
            auto     label_size = Label::split_lines(dc, ctrl->opt_group->label_width * ctrl->m_em_unit, label, multiline_text);
            label_actual_v_pos  = v_pos + lround((height - label_size.y) / 2);
            const wxColour *label_color = field->blink() ? &blink_color : field->label_color();
            h_pos = draw_text(dc, wxPoint(h_pos, v_pos), label, 
                            label_color,
                            ctrl->opt_group->label_width * ctrl->m_em_unit, 
                            false, true);
        }

        if (multi_variant_field->getSizer()) {
            auto children = multi_variant_field->getSizer()->GetChildren();

            int total_child_count = 0;
            for (auto child : children) {
                if (child->IsSizer()) total_child_count++;
            }

            int single_line_height = total_child_count > 0 
                ? (height - ctrl->m_v_gap + ctrl->m_v_gap2) / total_child_count 
                : height;

            int buttons_total_height = single_line_height * total_child_count - ctrl->m_v_gap2;

            dc.SetFont(ctrl->m_font);
            wxString multiline_text;
            auto     label_size     = Label::split_lines(dc, ctrl->opt_group->label_width * ctrl->m_em_unit, og_line.label, multiline_text);
            int      label_center_y = label_actual_v_pos + label_size.y / 2;

            wxCoord current_v_pos = label_center_y - buttons_total_height / 2;
            size_t variant_idx = 0;

            for (auto child : children) {
                if (child->IsSizer()) {
                    auto h_sizer = child->GetSizer();

                    int max_item_height = 0;
                    for (auto item : h_sizer->GetChildren()) {
                        if (item->IsWindow()) {
                            int h = item->GetWindow()->GetSize().GetHeight();
                            if (h > max_item_height) max_item_height = h;
                        }
                    }

                    Field* variant_field = nullptr;
                    if (variant_idx < multi_variant_field->m_text_ctrls.size()) {
                        variant_field = multi_variant_field->m_text_ctrls[variant_idx].text_ctrl.get();
                    }

                    if (variant_field && variant_field->undo_to_sys_bitmap()) {
                        wxPoint btn_pos = wxPoint(h_pos, current_v_pos);
                        int bmp_height = get_bitmap_size(variant_field->undo_bitmap()->bmp()).GetHeight();
                        int vertical_offset = std::max(0, (single_line_height - bmp_height - 6) / 2);
                        btn_pos.y += vertical_offset;

                        draw_act_bmps(dc, btn_pos, 
                                     variant_field->undo_to_sys_bitmap()->bmp(), 
                                     variant_field->undo_bitmap()->bmp(), 
                                     variant_field->blink(), 
                                     variant_idx,
                                     true);
                    }

                    current_v_pos += single_line_height;
                    variant_idx++;
                }
            }
        }

        return;
    }

    bool suppress_hyperlinks = false;
    if (draw_just_act_buttons) {
        //BBS: GUI refactor
        if (field && field->undo_bitmap())
        //if (field)
            // BBS: new layout
            draw_act_bmps(dc, wxPoint(h_pos, v_pos), field->undo_to_sys_bitmap()->bmp(), field->undo_bitmap()->bmp(), field->blink());
        return;
    }

    if (og_line.near_label_widget_win)
        h_pos += og_line.near_label_widget_win->GetSize().x + ctrl->m_h_gap;

    const std::vector<Option>& option_set = og_line.get_options();

    wxString label = og_line.label;
    wxColour blink_color = StateColor::darkModeColorFor("#00AE42");
    bool is_url_string = false;
    if (ctrl->opt_group->label_width != 0 && !label.IsEmpty()) {
        const wxColour* text_clr = field ? field->label_color() : og_line.full_Label_color;
        for (const Option& opt : option_set) {
            Field* field = ctrl->opt_group->get_field(opt.opt_id);
            if (field && field->blink()) {
                text_clr = &blink_color;
                break;
            }
        }
        bool is_multi_extruder = false;
        if (ctrl->opt_group->draw_multi_extruder)
            for (const Option& opt : option_set)
                is_multi_extruder |= opt.opt_id.find_last_of('#') != std::string::npos;
        wxCoord icon_pos = h_pos;
        if (is_multi_extruder) {
            static ScalableBitmap multi_extruder(ctrl, "multi_extruder");
            h_pos = draw_act_bmps(dc, wxPoint(h_pos, v_pos), multi_extruder.bmp(), multi_extruder.bmp(), false);
        }
        is_url_string = !suppress_hyperlinks && !og_line.label_path.empty();
        // BBS
        wxCoord indent = og_line.subline ? lround(1.5 * ctrl->m_em_unit) : 0;
        h_pos = draw_text(dc, wxPoint(h_pos + indent, v_pos), label /* + ":" */, text_clr, icon_pos + ctrl->opt_group->label_width * ctrl->m_em_unit - h_pos, is_url_string, true);
        h_pos -= indent;
    }
    if (nozzle_group_active())
        return;

    // If there's a widget, build it and set result to the correct position.
#ifndef DISABLE_BLINKING
    if (og_line.widget != nullptr) {
        draw_blinking_bmp(dc, wxPoint(h_pos, v_pos), og_line.blink);
        return;
    }
#endif

    // If we're here, we have more than one option or a single option with sidetext
    // so we need a horizontal sizer to arrange these things

    auto add_field_width = [&h_pos, this] (Field* field) {
        if (field) {
            if (field->getSizer())
            {
                auto children = field->getSizer()->GetChildren();
                for (auto child : children)
                    if (child->IsWindow())
                        h_pos += child->GetWindow()->GetSize().x + ctrl->m_h_gap;
            }
            else if (field->getWindow()) {
                h_pos += field->getWindow()->GetSize().x + ctrl->m_h_gap;
            }
        }
    };

    auto draw_buttons = [&h_pos, &dc, &v_pos, this](Field* field, size_t bmp_rect_id = 0) {
        if (multi_nozzle_auto_active())
            return;
        if (field && field->undo_to_sys_bitmap()) {
            h_pos = draw_act_bmps(dc, wxPoint(h_pos, v_pos), field->undo_to_sys_bitmap()->bmp(), field->undo_bitmap()->bmp(), field->blink(), bmp_rect_id);
        }
#ifndef DISABLE_BLINKING
        else if (field && !field->undo_to_sys_bitmap() && field->blink())
            draw_blinking_bmp(dc, wxPoint(h_pos, v_pos), field->blink());
#endif
    };

    wxCoord h_pos2 = h_pos;
    // If we have a single option with no sidetext just add it directly to the grid sizer
    if (option_set.size() == 1 && option_set.front().opt.sidetext.size() == 0 &&
        option_set.front().side_widget == nullptr && og_line.get_extra_widgets().size() == 0)
    {
        // BBS: new layout
        if (ctrl->opt_group->option_label_at_right)
            draw_buttons(field);
        add_field_width(field);
        wxCoord h_pos3 = h_pos;
        if (!ctrl->opt_group->option_label_at_right)
            draw_buttons(field);
        // update width for full_width fields
        if (option_set.front().opt.full_width && field && field->getWindow())
            field->getWindow()->SetSize(ctrl->GetSize().x - h_pos2 + h_pos3 - h_pos - ctrl->m_em_unit * 3, -1);
        return;
    }

    size_t bmp_rect_id = 0;
    bool is_multioption_line = option_set.size() > 1;
    for (const Option& opt : option_set) {
        field = ctrl->opt_group->get_field(opt.opt_id);
        ConfigOptionDef option = opt.opt;
        if (ctrl->opt_group->option_label_at_right)
            draw_buttons(field, bmp_rect_id++);
        if (ctrl->opt_group->option_label_at_right) // BBS
            add_field_width(field);
        // add label if any
        if (is_multioption_line && !option.label.empty()) {
            //!            To correct translation by context have to use wxGETTEXT_IN_CONTEXT macro from wxWidget 3.1.1
            label = (option.label == L_CONTEXT("Top", "Layers") || option.label == L_CONTEXT("Bottom", "Layers")) ?
                    _CTX(option.label, "Layers") : _(option.label);
            //if (!ctrl->opt_group->option_label_at_right) // BBS
                //label += ":";

            if (is_url_string)
                is_url_string = false;
            else if(opt == option_set.front())
                is_url_string = !suppress_hyperlinks && !og_line.label_path.empty();
            wxColor c = StateColor::darkModeColorFor("#6B6B6B");
            h_pos = draw_text(dc, wxPoint(h_pos, v_pos), label, field ? (field->blink() ? &blink_color : &c) : nullptr, ctrl->opt_group->sublabel_width * ctrl->m_em_unit, is_url_string);
            h_pos += 8;
        }

        // BBS: new layout
        // add field
        if (option_set.size() == 1 && option_set.front().opt.full_width)
            break;
        if (!ctrl->opt_group->option_label_at_right) // BBS
            add_field_width(field);
        // add sidetext if any
        // BBS: new layout
        wxCoord offset = 0;
        if (!field->combine_side_text() && (!option.sidetext.empty() || ctrl->opt_group->sidetext_width > 0)) {
            wxCoord h_pos2 = h_pos + dc.GetTextExtent(_(option.sidetext)).x;
            h_pos = draw_text(dc, wxPoint(h_pos, v_pos), _(option.sidetext), nullptr, ctrl->opt_group->sidetext_width * ctrl->m_em_unit);
            offset = h_pos - h_pos2;
        }
        // BBS: new layout
        if (!ctrl->opt_group->option_label_at_right) {
            offset -= ctrl->m_h_gap; h_pos -= offset;
            draw_buttons(field, bmp_rect_id++);
            h_pos += offset;
        }

        if (opt.opt_id != option_set.back().opt_id) //! istead of (opt != option_set.back())
            h_pos += lround(0.6 * ctrl->m_em_unit);

        if (ctrl->opt_group->split_multi_line) { // BBS
            v_pos += (height - ctrl->m_v_gap + ctrl->m_v_gap2) / option_set.size();
            h_pos = h_pos2;
        }
    }
}

wxCoord OG_CustomCtrl::CtrlLine::draw_text(wxDC &dc, wxPoint pos, const wxString &text, const wxColour *color, int width, bool is_url/* = false*/, bool is_main/* = false*/)
{
    wxString multiline_text;
    auto size = Label::split_lines(dc, width, text, multiline_text);

    if (!text.IsEmpty()) {
        const wxString& out_text = multiline_text.IsEmpty() ? text : multiline_text;

        if (ctrl->opt_group->split_multi_line && !is_main) { // BBS
            const std::vector<Option> &option_set = og_line.get_options();
            pos.y = pos.y + lround(((height - ctrl->m_v_gap + ctrl->m_v_gap2) / option_set.size() - size.y) / 2);
        } else {
            pos.y = pos.y + lround((content_height() - size.y) / 2);
        }
        if (width > 0)
            rect_label = wxRect(pos, wxSize(size.x, size.y));

        wxColour old_clr = dc.GetTextForeground();
        wxFont old_font = dc.GetFont();
        wxColor clr_url = StateColor::darkModeColorFor("#00AE42");
        if (is_focused && is_url) {
        // temporary workaround for the OSX because of strange Bold font behavior on BigSerf
#ifdef __APPLE__
            dc.SetFont(old_font.Underlined());
#else
            dc.SetFont(old_font.Bold().Underlined());
#endif
            color = &clr_url;
        }
        dc.SetTextForeground(color ? *color :
#ifdef _WIN32
            wxGetApp().get_label_clr_default());
#else
            wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
#endif /* _WIN32 */
        dc.DrawText(out_text, pos);
        dc.SetTextForeground(old_clr);
        dc.SetFont(old_font);

        if (width < 1)
            width = size.x;
    }

    return pos.x + width + ctrl->m_h_gap;
}

wxPoint OG_CustomCtrl::CtrlLine::draw_blinking_bmp(wxDC& dc, wxPoint pos, bool is_blinking)
{
    wxBitmap bmp_blinking = create_scaled_bitmap(is_blinking ? "blank_16" : "empty", ctrl);
    wxCoord h_pos = pos.x;
    wxCoord v_pos = pos.y + lround((content_height() - get_bitmap_size(bmp_blinking).GetHeight()) / 2);

    dc.DrawBitmap(bmp_blinking, h_pos, v_pos);

    int bmp_dim = get_bitmap_size(bmp_blinking).GetWidth();

    h_pos += bmp_dim + ctrl->m_h_gap;
    return wxPoint(h_pos, v_pos);
}

wxCoord OG_CustomCtrl::CtrlLine::draw_act_bmps(wxDC& dc, wxPoint pos, const wxBitmap& bmp_undo_to_sys, const wxBitmap& bmp_undo, bool is_blinking, size_t rect_id, bool skip_vertical_adjust)
{
#ifndef DISABLE_BLINKING
    pos = draw_blinking_bmp(dc, pos, is_blinking);
#else
    if (!skip_vertical_adjust) {
        if (ctrl->opt_group->split_multi_line) { // BBS
            const std::vector<Option> &option_set = og_line.get_options();
            if (option_set.size() > 1)
                pos.y += lround(((height - ctrl->m_v_gap + ctrl->m_v_gap2) / option_set.size() - get_bitmap_size(bmp_undo).GetHeight()) / 2);
            else
                pos.y += lround((content_height() - get_bitmap_size(bmp_undo).GetHeight()) / 2);
        } else {
            pos.y += lround((content_height() - get_bitmap_size(bmp_undo).GetHeight()) / 2);
        }
    }
#endif
    wxCoord h_pos = pos.x;
    wxCoord v_pos = pos.y;

#ifndef DISABLE_UNDO_SYS
    //BBS: GUI refactor
    dc.DrawBitmap(bmp_undo_to_sys, h_pos, v_pos);

    int bmp_dim = get_bitmap_size(bmp_undo_to_sys).GetWidth();
    rects_undo_to_sys_icon[rect_id] = wxRect(h_pos, v_pos, bmp_dim, bmp_dim);

    h_pos += bmp_dim + ctrl->m_h_gap;
#endif
    dc.DrawBitmap(og_line.undo_to_sys ? bmp_undo_to_sys : bmp_undo, h_pos, v_pos);

    int bmp_dim2 = get_bitmap_size(bmp_undo).GetWidth();
    (og_line.undo_to_sys ? rects_undo_to_sys_icon[rect_id] : rects_undo_icon[rect_id]) = wxRect(h_pos, v_pos, bmp_dim2, bmp_dim2);

    h_pos += bmp_dim2 + ctrl->m_h_gap;

    return h_pos;
}

bool OG_CustomCtrl::CtrlLine::launch_browser() const
{
    if (!is_focused || og_line.label_path.empty())
        return false;

    return OptionsGroup::launch_browser(og_line.label_path);
}

void OG_CustomCtrl::CtrlLine::ensure_rects_size(size_t size)
{
    if (rects_undo_icon.size() != size) {
        rects_undo_icon.resize(size, wxRect());
    }
    if (rects_undo_to_sys_icon.size() != size) {
        rects_undo_to_sys_icon.resize(size, wxRect());
    }
}

void OG_CustomCtrl::CtrlLine::update_multi_variant_height()
{
    const std::vector<Option> &option_set = og_line.get_options();
    if (option_set.size() != 1) {
        return;
    }
    Field *field = ctrl->opt_group->get_field(option_set.front().opt_id);
    if (!field) {
        return;
    }

    auto *multi_variant_field = dynamic_cast<MultiVariantTextCtrl *>(field);
    if (!multi_variant_field || !multi_variant_field->getSizer()) {
        return;
    }

    int  total_height = 0;
    auto children     = multi_variant_field->getSizer()->GetChildren();

    for (auto child : children) {
        if (child->IsWindow()) {
            total_height += child->GetWindow()->GetSize().GetHeight() + ctrl->m_v_gap2;
        } else if (child->IsSizer()) {
            auto sub_children = child->GetSizer()->GetChildren();
            int  max_height   = 0;
            for (auto sub_child : sub_children) {
                if (sub_child->IsWindow()) {
                    int h = sub_child->GetWindow()->GetSize().GetHeight();
                    if (h > max_height) {
                        max_height = h;
                    }
                }
            }
            if (max_height > 0) {
                total_height += max_height + ctrl->m_v_gap2;
            }
        }
    }

    if (total_height > 0 && multi_variant_field->m_text_ctrls.size() > 1) {
        total_height -= ctrl->m_v_gap2;
    }

    wxSize label_sz  = ctrl->GetTextExtent(og_line.label);
    height = std::max(label_sz.y, total_height);
}

} // GUI
} // Slic3r
