#include "libslic3r/libslic3r.h"
#include "IMSlider.hpp"
#include "libslic3r/GCode.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "I18N.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/AppConfig.hpp"
#include "GUI_Utils.hpp"
#include "MsgDialog.hpp"
#include "Tab.hpp"
#include "GUI_ObjectList.hpp"

#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/menu.h>
#include <wx/bmpcbox.h>
#include <wx/statline.h>
#include <wx/dcclient.h>
#include <wx/colordlg.h>

#include <cmath>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/split.hpp>
#include "Field.hpp"
#include "format.hpp"
#include "NotificationManager.hpp"
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui/imgui_internal.h>

namespace Slic3r {

using GUI::from_u8;
using GUI::into_u8;
using GUI::format_wxstr;

namespace GUI {

constexpr double min_delta_area = scale_(scale_(25));  // equal to 25 mm2
constexpr double miscalculation = scale_(scale_(1));   // equal to 1 mm2

static const float RIGHT_MARGIN   = 13.0f;
static const float BOTTOM_MARGIN  = 13.0f;
static const float TOP_MARGIN     =  3.0f;
static const float LEFT_MARGIN    = 13.0f + 100.0f;  // avoid thumbnail toolbar
static const ImVec2 HORIZONTAL_SLIDER_SIZE = ImVec2(470, 40);
static const ImVec2 VERTICAL_SLIDER_SIZE = ImVec2(65, 485);
static const ImVec2 MIN_RECT_SIZE  = ImVec2(81, 52);

bool equivalent_areas(const double& bottom_area, const double& top_area)
{
    return fabs(bottom_area - top_area) <= miscalculation;
}

bool check_color_change(PrintObject *object, size_t frst_layer_id, size_t layers_cnt, bool check_overhangs, std::function<bool(Layer *)> break_condition)
{
    double prev_area = area(object->get_layer(frst_layer_id)->lslices);

    bool detected = false;
    for (size_t i = frst_layer_id + 1; i < layers_cnt; i++) {
        Layer *layer    = object->get_layer(i);
        double cur_area = area(layer->lslices);

        // check for overhangs
        if (check_overhangs && cur_area > prev_area && !equivalent_areas(prev_area, cur_area)) break;

        // Check percent of the area decrease.
        // This value have to be more than min_delta_area and more then 10%
        if ((prev_area - cur_area > min_delta_area) && (cur_area / prev_area < 0.9)) {
            detected = true;
            if (break_condition(layer)) break;
        }

        prev_area = cur_area;
    }
    return detected;
}


static std::string gcode(Type type)
{
    const PrintConfig& config = GUI::wxGetApp().plater()->fff_print().config();
    switch (type) {
    //BBS
    //case ColorChange: return config.color_change_gcode;
    case PausePrint:  return config.machine_pause_gcode;
    //case Template:    return config.template_custom_gcode;
    default:          return "";
    }
}

static std::string short_and_splitted_time(const std::string &time)
{
    // Parse the dhms time format.
    int days    = 0;
    int hours   = 0;
    int minutes = 0;
    int seconds = 0;
    if (time.find('d') != std::string::npos)
        ::sscanf(time.c_str(), "%dd %dh %dm %ds", &days, &hours, &minutes, &seconds);
    else if (time.find('h') != std::string::npos)
        ::sscanf(time.c_str(), "%dh %dm %ds", &hours, &minutes, &seconds);
    else if (time.find('m') != std::string::npos)
        ::sscanf(time.c_str(), "%dm %ds", &minutes, &seconds);
    else if (time.find('s') != std::string::npos)
        ::sscanf(time.c_str(), "%ds", &seconds);

    // Format the dhm time.
    char buffer[64];
    if (days > 0)
        ::sprintf(buffer, "%dd%dh\n%dm", days, hours, minutes);
    else if (hours > 0) {
        if (hours < 10 && minutes < 10 && seconds < 10)
            ::sprintf(buffer, "%dh%dm%ds", hours, minutes, seconds);
        else if (hours > 10 && minutes > 10 && seconds > 10)
            ::sprintf(buffer, "%dh\n%dm\n%ds", hours, minutes, seconds);
        else if ((minutes < 10 && seconds > 10) || (minutes > 10 && seconds < 10))
            ::sprintf(buffer, "%dh\n%dm%ds", hours, minutes, seconds);
        else
            ::sprintf(buffer, "%dh%dm\n%ds", hours, minutes, seconds);
    } else if (minutes > 0) {
        if (minutes > 10 && seconds > 10)
            ::sprintf(buffer, "%dm\n%ds", minutes, seconds);
        else
            ::sprintf(buffer, "%dm%ds", minutes, seconds);
    } else
        ::sprintf(buffer, "%ds", seconds);
    return std::string(buffer);
}


std::string TickCodeInfo::get_color_for_tick(TickCode tick, Type type, const int extruder)
{
    if (mode == SingleExtruder && type == ColorChange && m_use_default_colors) {
#if 1
        if (ticks.empty()) return color_generator.get_opposite_color((*m_colors)[0]);

        auto before_tick_it = std::lower_bound(ticks.begin(), ticks.end(), tick);
        if (before_tick_it == ticks.end()) {
            while (before_tick_it != ticks.begin())
                if (--before_tick_it; before_tick_it->type == ColorChange) break;
            if (before_tick_it->type == ColorChange) return color_generator.get_opposite_color(before_tick_it->color);
            return color_generator.get_opposite_color((*m_colors)[0]);
        }

        if (before_tick_it == ticks.begin()) {
            const std::string &frst_color = (*m_colors)[0];
            if (before_tick_it->type == ColorChange) return color_generator.get_opposite_color(frst_color, before_tick_it->color);

            auto next_tick_it = before_tick_it;
            while (next_tick_it != ticks.end())
                if (++next_tick_it; next_tick_it->type == ColorChange) break;
            if (next_tick_it->type == ColorChange) return color_generator.get_opposite_color(frst_color, next_tick_it->color);

            return color_generator.get_opposite_color(frst_color);
        }

        std::string frst_color = "";
        if (before_tick_it->type == ColorChange)
            frst_color = before_tick_it->color;
        else {
            auto next_tick_it = before_tick_it;
            while (next_tick_it != ticks.end())
                if (++next_tick_it; next_tick_it->type == ColorChange) {
                    frst_color = next_tick_it->color;
                    break;
                }
        }

        while (before_tick_it != ticks.begin())
            if (--before_tick_it; before_tick_it->type == ColorChange) break;

        if (before_tick_it->type == ColorChange) {
            if (frst_color.empty()) return color_generator.get_opposite_color(before_tick_it->color);
            return color_generator.get_opposite_color(before_tick_it->color, frst_color);
        }

        if (frst_color.empty()) return color_generator.get_opposite_color((*m_colors)[0]);
        return color_generator.get_opposite_color((*m_colors)[0], frst_color);
#else
        const std::vector<std::string> &colors = ColorPrintColors::get();
        if (ticks.empty()) return colors[0];
        m_default_color_idx++;

        return colors[m_default_color_idx % colors.size()];
#endif
    }

    std::string color = (*m_colors)[extruder - 1];

    if (type == ColorChange) {
        if (!ticks.empty()) {
            auto before_tick_it = std::lower_bound(ticks.begin(), ticks.end(), tick);
            while (before_tick_it != ticks.begin()) {
                --before_tick_it;
                if (before_tick_it->type == ColorChange && before_tick_it->extruder == extruder) {
                    color = before_tick_it->color;
                    break;
                }
            }
        }

        //TODO
        //color = get_new_color(color);
    }
    return color;
}


bool TickCodeInfo::add_tick(const int tick, Type type, const int extruder, double print_z)
{
    std::string color;
    std::string extra;
    if (type == Custom) // custom Gcode
    {
        /*extra = get_custom_code(custom_gcode, print_z);
        if (extra.empty()) return false;
        custom_gcode = extra;*/
    } else if (type == PausePrint) {
        //BBS do not set pause extra message
        //extra = get_pause_print_msg(pause_print_msg, print_z);
        //if (extra.empty()) return false;
        pause_print_msg = extra;
    }
    else {
        color = get_color_for_tick(TickCode{ tick }, type, extruder);
        if (color.empty()) return false;
    }

    if (mode == SingleExtruder) m_use_default_colors = true;

    ticks.emplace(TickCode{tick, type, extruder, color, extra});

    return true;
}

bool TickCodeInfo::edit_tick(std::set<TickCode>::iterator it, double print_z)
{
    std::string edited_value;
    //TODO
    /* BBS
    if (it->type == ColorChange)
        edited_value = get_new_color(it->color);
    else if (it->type == PausePrint)
        edited_value = get_pause_print_msg(it->extra, print_z);
    else
        edited_value = get_custom_code(it->type == Template ? gcode(Template) : it->extra, print_z);
    */
    if (edited_value.empty()) return false;

    TickCode changed_tick = *it;
    if (it->type == ColorChange) {
        if (it->color == edited_value) return false;
        changed_tick.color = edited_value;
    } else if (it->type == Template) {
        if (gcode(Template) == edited_value) return false;
        changed_tick.extra = edited_value;
        changed_tick.type  = Custom;
    } else if (it->type == Custom || it->type == PausePrint) {
        if (it->extra == edited_value) return false;
        changed_tick.extra = edited_value;
    }

    ticks.erase(it);
    ticks.emplace(changed_tick);

    return true;
}

void TickCodeInfo::switch_code(Type type_from, Type type_to)
{
    for (auto it{ticks.begin()}, end{ticks.end()}; it != end;)
        if (it->type == type_from) {
            TickCode tick = *it;
            tick.type     = type_to;
            tick.extruder = 1;
            ticks.erase(it);
            it = ticks.emplace(tick).first;
        } else
            ++it;
}

bool TickCodeInfo::switch_code_for_tick(std::set<TickCode>::iterator it, Type type_to, const int extruder)
{
    const std::string color = get_color_for_tick(*it, type_to, extruder);
    if (color.empty()) return false;

    TickCode changed_tick = *it;
    changed_tick.type     = type_to;
    changed_tick.extruder = extruder;
    changed_tick.color    = color;

    ticks.erase(it);
    ticks.emplace(changed_tick);

    return true;
}

void TickCodeInfo::erase_all_ticks_with_code(Type type)
{
    for (auto it{ticks.begin()}, end{ticks.end()}; it != end;) {
        if (it->type == type)
            it = ticks.erase(it);
        else
            ++it;
    }
}

bool TickCodeInfo::has_tick_with_code(Type type)
{
    for (const TickCode &tick : ticks)
        if (tick.type == type) return true;

    return false;
}

bool TickCodeInfo::has_tick(int tick) { return ticks.find(TickCode{tick}) != ticks.end(); }

ConflictType TickCodeInfo::is_conflict_tick(const TickCode &tick, Mode out_mode, int only_extruder, double print_z)
{
    if ((tick.type == ColorChange && ((mode == SingleExtruder && out_mode == MultiExtruder) || (mode == MultiExtruder && out_mode == SingleExtruder))) ||
        (tick.type == ToolChange && (mode == MultiAsSingle && out_mode != MultiAsSingle)))
        return ctModeConflict;

    // check ColorChange tick
    if (tick.type == ColorChange) {
        // We should mark a tick as a "MeaninglessColorChange",
        // if it has a ColorChange for unused extruder from current print to end of the print
        std::set<int> used_extruders_for_tick = get_used_extruders_for_tick(tick.tick, only_extruder, print_z, out_mode);

        if (used_extruders_for_tick.find(tick.extruder) == used_extruders_for_tick.end()) return ctMeaninglessColorChange;

        // We should mark a tick as a "Redundant",
        // if it has a ColorChange for extruder that has not been used before
        if (mode == MultiAsSingle && tick.extruder != std::max<int>(only_extruder, 1)) {
            auto it = ticks.lower_bound(tick);
            if (it == ticks.begin() && it->type == ToolChange && tick.extruder == it->extruder) return ctNone;

            while (it != ticks.begin()) {
                --it;
                if (it->type == ToolChange && tick.extruder == it->extruder) return ctNone;
            }

            return ctRedundant;
        }
    }

    // check ToolChange tick
    if (mode == MultiAsSingle && tick.type == ToolChange) {
        // We should mark a tick as a "MeaninglessToolChange",
        // if it has a ToolChange to the same extruder
        auto it = ticks.find(tick);
        if (it == ticks.begin()) return tick.extruder == std::max<int>(only_extruder, 1) ? ctMeaninglessToolChange : ctNone;

        while (it != ticks.begin()) {
            --it;
            if (it->type == ToolChange) return tick.extruder == it->extruder ? ctMeaninglessToolChange : ctNone;
        }
    }

    return ctNone;
}

// Get used extruders for tick.
// Means all extruders(tools) which will be used during printing from current tick to the end
std::set<int> TickCodeInfo::get_used_extruders_for_tick(int tick, int only_extruder, double print_z, Mode force_mode /* = Undef*/) const
{
    Mode e_mode = !force_mode ? mode : force_mode;

    if (e_mode == MultiExtruder) {
        // #ys_FIXME: get tool ordering from _correct_ place
        const ToolOrdering &tool_ordering = GUI::wxGetApp().plater()->fff_print().get_tool_ordering();

        if (tool_ordering.empty()) return {};

        std::set<int> used_extruders;

        auto it_layer_tools = std::lower_bound(tool_ordering.begin(), tool_ordering.end(), LayerTools(print_z));
        for (; it_layer_tools != tool_ordering.end(); ++it_layer_tools) {
            const std::vector<unsigned> &extruders = it_layer_tools->extruders;
            for (const auto &extruder : extruders) used_extruders.emplace(extruder + 1);
        }

        return used_extruders;
    }

    const int default_initial_extruder = e_mode == MultiAsSingle ? std::max(only_extruder, 1) : 1;
    if (ticks.empty() || e_mode == SingleExtruder) return {default_initial_extruder};

    std::set<int> used_extruders;

    auto it_start = ticks.lower_bound(TickCode{tick});
    auto it       = it_start;
    if (it == ticks.begin() && it->type == ToolChange && tick != it->tick) // In case of switch of ToolChange to ColorChange, when tick exists,
                                                                           // we shouldn't change color for extruder, which will be deleted
    {
        used_extruders.emplace(it->extruder);
        if (tick < it->tick) used_extruders.emplace(default_initial_extruder);
    }

    while (it != ticks.begin()) {
        --it;
        if (it->type == ToolChange && tick != it->tick) {
            used_extruders.emplace(it->extruder);
            break;
        }
    }

    if (it == ticks.begin() && used_extruders.empty()) used_extruders.emplace(default_initial_extruder);

    for (it = it_start; it != ticks.end(); ++it)
        if (it->type == ToolChange && tick != it->tick) used_extruders.emplace(it->extruder);

    return used_extruders;
}

IMSlider::IMSlider(int lowerValue, int higherValue, int minValue, int maxValue, long style)
{
    m_lower_value  = lowerValue;
    m_higher_value = higherValue;
    m_min_value    = minValue;
    m_max_value    = maxValue;
    m_style        = style == wxSL_HORIZONTAL || style == wxSL_VERTICAL ? style : wxSL_HORIZONTAL;
    // BBS set to none style by default
    m_extra_style = style == wxSL_VERTICAL ? 0 : 0;
    m_selection   = ssUndef;

    m_ticks.set_extruder_colors(&m_extruder_colors);
}

bool IMSlider::init_texture()
{
    bool result = true;
    if (!is_horizontal()) {
        // BBS init image texture id
        result &= IMTexture::load_from_svg_file(Slic3r::resources_dir() + "/images/reset_normal.svg", 20, 20, m_reset_normal_id);
        result &= IMTexture::load_from_svg_file(Slic3r::resources_dir() + "/images/reset_hover.svg", 20, 20, m_reset_hover_id);
        result &= IMTexture::load_from_svg_file(Slic3r::resources_dir() + "/images/one_layer_on.svg", 24, 24, m_one_layer_on_id);
        result &= IMTexture::load_from_svg_file(Slic3r::resources_dir() + "/images/one_layer_on_hover.svg", 28, 28, m_one_layer_on_hover_id);
        result &= IMTexture::load_from_svg_file(Slic3r::resources_dir() + "/images/one_layer_off.svg", 28, 28, m_one_layer_off_id);
        result &= IMTexture::load_from_svg_file(Slic3r::resources_dir() + "/images/one_layer_off_hover.svg", 28, 28, m_one_layer_off_hover_id);
        result &= IMTexture::load_from_svg_file(Slic3r::resources_dir() + "/images/one_layer_arrow.svg", 28, 28, m_one_layer_arrow_id);
    }

    return result;
}

int IMSlider::GetActiveValue() const
{
    return m_selection == ssLower ?
    m_lower_value : m_selection == ssHigher ?
                m_higher_value : -1;
}


void IMSlider::SetLowerValue(const int lower_val)
{
    m_selection   = ssLower;
    m_lower_value = lower_val;
    correct_lower_value();
    set_as_dirty();
}

void IMSlider::SetHigherValue(const int higher_val)
{
    m_selection    = ssHigher;
    m_higher_value = higher_val;
    correct_higher_value();
    set_as_dirty();
}

void IMSlider::SetSelectionSpan(const int lower_val, const int higher_val)
{
    m_lower_value  = std::max(lower_val, m_min_value);
    m_higher_value = std::max(std::min(higher_val, m_max_value), m_lower_value);
    if (m_lower_value < m_higher_value) m_is_one_layer = false;

    set_as_dirty();
}

void IMSlider::SetMaxValue(const int max_value)
{
    m_max_value = max_value;
    set_as_dirty();
}

void IMSlider::SetSliderValues(const std::vector<double> &values)
{
    m_values = values;
}

Info IMSlider::GetTicksValues() const
{
    Info                            custom_gcode_per_print_z;
    std::vector<CustomGCode::Item> &values = custom_gcode_per_print_z.gcodes;

    const int val_size = m_values.size();
    if (!m_values.empty())
        for (const TickCode &tick : m_ticks.ticks) {
            if (tick.tick > val_size) break;
            values.emplace_back(CustomGCode::Item{m_values[tick.tick], tick.type, tick.extruder, tick.color, tick.extra});
        }

    if (m_force_mode_apply) custom_gcode_per_print_z.mode = m_mode;

    return custom_gcode_per_print_z;
}

void IMSlider::SetTicksValues(const Info &custom_gcode_per_print_z)
{
    if (m_values.empty()) {
        m_ticks.mode = m_mode;
        return;
    }

    const bool was_empty = m_ticks.empty();

    m_ticks.ticks.clear();
    const std::vector<CustomGCode::Item> &heights = custom_gcode_per_print_z.gcodes;
    for (auto h : heights) {
        int tick = get_tick_from_value(h.print_z);
        if (tick >= 0) m_ticks.ticks.emplace(TickCode{tick, h.type, h.extruder, h.color, h.extra});
    }

    if (!was_empty && m_ticks.empty())
        // Switch to the "Feature type"/"Tool" from the very beginning of a new object slicing after deleting of the old one
        post_ticks_changed_event();

    // init extruder sequence in respect to the extruders count
    if (m_ticks.empty()) m_extruders_sequence.init(m_extruder_colors.size());

    if (custom_gcode_per_print_z.mode && !custom_gcode_per_print_z.gcodes.empty()) m_ticks.mode = custom_gcode_per_print_z.mode;

    set_as_dirty();
}

void IMSlider::SetLayersTimes(const std::vector<float> &layers_times, float total_time)
{
    m_layers_times.clear();
    if (layers_times.empty()) return;
    m_layers_times.resize(layers_times.size(), 0.0);
    m_layers_times[0] = layers_times[0];
    for (size_t i = 1; i < layers_times.size(); i++) m_layers_times[i] = m_layers_times[i - 1] + layers_times[i];

    // Erase duplicates values from m_values and save it to the m_layers_values
    // They will be used for show the correct estimated time for MM print, when "No sparce layer" is enabled
    if (m_is_wipe_tower && m_values.size() != m_layers_times.size()) {
        m_layers_values = m_values;
        sort(m_layers_values.begin(), m_layers_values.end());
        m_layers_values.erase(unique(m_layers_values.begin(), m_layers_values.end()), m_layers_values.end());

        // When whipe tower is used to the end of print, there is one layer which is not marked in layers_times
        // So, add this value from the total print time value
        if (m_layers_values.size() != m_layers_times.size())
            for (size_t i = m_layers_times.size(); i < m_layers_values.size(); i++) m_layers_times.push_back(total_time);
        set_as_dirty();
        set_as_dirty();
    }
}

void IMSlider::SetLayersTimes(const std::vector<double> &layers_times)
{
    m_is_wipe_tower = false;
    m_layers_times  = layers_times;
    for (size_t i = 1; i < m_layers_times.size(); i++) m_layers_times[i] += m_layers_times[i - 1];
}

void IMSlider::SetDrawMode(bool is_sequential_print)
{
    m_draw_mode = is_sequential_print   ? dmSequentialFffPrint  : 
                                          dmRegular; 
}

void IMSlider::SetModeAndOnlyExtruder(const bool is_one_extruder_printed_model, const int only_extruder)
{
    m_mode = !is_one_extruder_printed_model ? MultiExtruder : only_extruder < 0 ? SingleExtruder : MultiAsSingle;
    if (!m_ticks.mode || (m_ticks.empty() && m_ticks.mode != m_mode)) m_ticks.mode = m_mode;
    m_only_extruder = only_extruder;

    UseDefaultColors(m_mode == SingleExtruder);

    m_is_wipe_tower = m_mode != SingleExtruder;
}

void IMSlider::SetExtruderColors( const std::vector<std::string>& extruder_colors)
{
    m_extruder_colors = extruder_colors;
}

bool IMSlider::IsNewPrint()
{
    const Print &print = GUI::wxGetApp().plater()->fff_print();
    std::string  idxs;
    for (auto object : print.objects()) idxs += std::to_string(object->id().id) + "_";

    if (idxs == m_print_obj_idxs) return false;

    m_print_obj_idxs = idxs;
    return true;
}

void IMSlider::post_ticks_changed_event(Type type /*= Custom*/)
{
    m_is_need_post_tick_changed_event = true;
}

void IMSlider::add_code_as_tick(Type type, int selected_extruder)
{
    if (m_selection == ssUndef) return;
    const int tick = m_selection == ssLower ? m_lower_value : m_higher_value;

    if (!check_ticks_changed_event(type)) {
        BOOST_LOG_TRIVIAL(trace) << "check ticks change event false";
        return;
    }

    if (type == ColorChange && gcode(ColorChange).empty()) GUI::wxGetApp().plater()->get_notification_manager()->push_notification(GUI::NotificationType::EmptyColorChangeCode);

    const int  extruder = selected_extruder > 0 ? selected_extruder : std::max<int>(1, m_only_extruder);
    const auto it       = m_ticks.ticks.find(TickCode{tick});

    if (it == m_ticks.ticks.end()) {
        // try to add tick
        if (!m_ticks.add_tick(tick, type, extruder, m_values[tick])) return;
    } else if (type == ToolChange || type == ColorChange) {
        // try to switch tick code to ToolChange or ColorChange accordingly
        if (!m_ticks.switch_code_for_tick(it, type, extruder)) return;
    } else
        return;

    post_ticks_changed_event(type);
}

bool IMSlider::check_ticks_changed_event(Type type)
{
    if (m_ticks.mode == m_mode || (type != ColorChange && type != ToolChange) ||
        (m_ticks.mode == SingleExtruder && m_mode == MultiAsSingle) || // All ColorChanges will be applied for 1st extruder
        (m_ticks.mode == MultiExtruder && m_mode == MultiAsSingle))    // Just mark ColorChanges for all unused extruders
        return true;

    if ((m_ticks.mode == SingleExtruder && m_mode == MultiExtruder) || (m_ticks.mode == MultiExtruder && m_mode == SingleExtruder)) {
        if (!m_ticks.has_tick_with_code(ColorChange)) return true;
        /*
        wxString message = (m_ticks.mode == SingleExtruder ? _L("The last color change data was saved for a single extruder printing.") :
                                                             _L("The last color change data was saved for a multi extruder printing.")) +
                           "\n" + _L("Your current changes will delete all saved color changes.") + "\n\n\t" + _L("Are you sure you want to continue?");

        
        GUI::MessageDialog msg(this, message, _L("Notice"), wxYES_NO);
        if (msg.ShowModal() == wxID_YES) {
            m_ticks.erase_all_ticks_with_code(ColorChange);
            post_ticks_changed_event(ColorChange);
        }
        */
        return false;
    }
    //          m_ticks_mode == MultiAsSingle
    if (m_ticks.has_tick_with_code(ToolChange)) {
        //wxString message = m_mode == SingleExtruder ?
        //                       (_L("The last color change data was saved for a multi extruder printing.") + "\n\n" +
        //                        _L("Select YES if you want to delete all saved tool changes, \n"
        //                           "NO if you want all tool changes switch to color changes, \n"
        //                           "or CANCEL to leave it unchanged.") +
        //                        "\n\n\t" + _L("Do you want to delete all saved tool changes?")) :
        //                       ( // MultiExtruder
        //                           _L("The last color change data was saved for a multi extruder printing with tool changes for whole print.") + "\n\n" +
        //                           _L("Your current changes will delete all saved extruder (tool) changes.") + "\n\n\t" + _L("Are you sure you want to continue?"));
        //BBS GUI::MessageDialog msg(this, message, _L("Notice"), wxYES_NO | (m_mode == SingleExtruder ? wxCANCEL : 0));
        //const int          answer = msg.ShowModal();
        //if (answer == wxID_YES) {
        //    m_ticks.erase_all_ticks_with_code(ToolChange);
        //    post_ticks_changed_event(ToolChange);
        //} else if (m_mode == SingleExtruder && answer == wxID_NO) {
        //    m_ticks.switch_code(ToolChange, ColorChange);
        //    post_ticks_changed_event(ColorChange);
        //}
        return false;
    }

    return true;
}


// switch on/off one layer mode
void IMSlider::switch_one_layer_mode()
{
    m_is_one_layer = !m_is_one_layer;
    if (!m_is_one_layer) {
        SetLowerValue(m_min_value);
        SetHigherValue(m_max_value);
    }
    m_selection == ssLower ? correct_lower_value() : correct_higher_value();
    if (m_selection == ssUndef) m_selection = ssHigher;
    set_as_dirty();
}

bool IMSlider::horizontal_slider(const char* str_id, int* value, int v_min, int v_max, ImVec2 size, float scale)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGui::SetWindowFontScale(1.0f / scale);

    ImGuiContext& context = *GImGui;
    const ImGuiID id = window->GetID(str_id);

    ImVec2 padding(11, 7);
    float offset_of_handle = 10.0f / sqrt(2);
    ImVec2 pos = window->DC.CursorPos;
    const ImRect draw_region(pos, pos + size);
    ImGui::ItemSize(draw_region);

    // Draw background rect
    float fixed_bar_height = 18.0f;
    ImVec2 slider_bar_start = ImVec2(pos.x, pos.y + size.y - fixed_bar_height);
    ImVec2 slider_bar_size = ImVec2(size.x, fixed_bar_height);
    const ImRect bg_rect(slider_bar_start, slider_bar_start + slider_bar_size);
    const ImU32 bg_rect_col = IM_COL32(255, 255, 255, 255);
    ImGui::RenderFrame(bg_rect.Min, bg_rect.Max, bg_rect_col, false, 0.5 * fixed_bar_height);

    // Draw background of scroll line
    const ImRect scroll_rect(bg_rect.Min + padding, bg_rect.Max - padding);
    const ImU32 scroll_bg_col = IM_COL32(238, 238, 238, 255);
    ImGui::RenderFrame(scroll_rect.Min, scroll_rect.Max, scroll_bg_col, false, 0.5 * fixed_bar_height - padding.y);

    // Draw handle dynamically according to mouse position
    // set slideable region
    const ImRect slideable_region(ImVec2(scroll_rect.Min.x, bg_rect.Min.y), ImVec2(scroll_rect.Max.x, bg_rect.Max.y));
    // set active(slideable) region.
    const bool hovered = ImGui::ItemHoverable(slideable_region, id);
    if (hovered && context.IO.MouseDown[0])
    {
        ImGui::SetActiveID(id, window);
        ImGui::SetFocusID(id, window);
        ImGui::FocusWindow(window);
    }
    // update handle position and value & draw handle
    ImRect handle;
    const ImU32 handle_and_line_col = IM_COL32(0, 174, 66, 255);
    const ImU32 handle_and_line_border_col = IM_COL32(0, 98, 0, 255);
    const bool value_changed = slider_behavior(id, slideable_region, (const ImS32)v_min, (const ImS32)v_max, (ImS32*)value, &handle);
    ImVec2 handle_center = handle.GetCenter();

    // Draw slider border
    const ImRect scroll_line(scroll_rect.Min, ImVec2(handle_center.x, scroll_rect.Max.y));
    window->DrawList->AddNgon(handle_center, offset_of_handle + 1, handle_and_line_border_col, 4);
    window->DrawList->AddRect(scroll_line.Min - ImVec2(1, 1), scroll_line.Max + ImVec2(1, 1), handle_and_line_border_col, 0.5 * fixed_bar_height - padding.y);
    // Draw slider
    window->DrawList->AddNgonFilled(handle_center, offset_of_handle, handle_and_line_col, 4);
    window->DrawList->AddRectFilled(scroll_line.Min, scroll_line.Max, handle_and_line_col, 0.5 * fixed_bar_height - padding.y);

    // Draw label
    auto text_utf8 = into_u8(std::to_string(*value));
    ImVec2 text_content_size = ImGui::CalcTextSize(text_utf8.c_str(), NULL, false, -1.0f);
    //ImVec2 text_content_size = calc_text_size(std::to_string(*value));
    ImVec2 text_padding = ImVec2(5.0f, 1.0f);
    ImVec2 text_size = text_content_size + text_padding * 2;
    ImVec2 text_start = ImVec2(handle_center.x - 0.5 * text_size.x, pos.y);
    ImRect text_rect(text_start, text_start + text_size);
    ImGui::RenderFrame(text_rect.Min, text_rect.Max, bg_rect_col, false, 2.0f);
    ImVec2 pos_1 = ImVec2(text_rect.GetCenter().x - 3.5f, text_rect.Max.y);
    ImVec2 pos_2 = ImVec2(text_rect.GetCenter().x + 3.5f, text_rect.Max.y);
    ImVec2 pos_3 = ImVec2(text_rect.GetCenter().x, text_rect.Max.y + 6.06f);
    window->DrawList->AddTriangleFilled(pos_1, pos_2, pos_3, bg_rect_col);
    ImGui::RenderText(text_start + text_padding, std::to_string(*value).c_str());

    ImGui::SetWindowFontScale(1.0f);
    return value_changed;
}

void IMSlider::render_colored_band(const ImVec2& pos, ImRect main_band) {
    if (is_horizontal())
        return;
    if (m_draw_mode != dmRegular)
        return;
    if (m_ticks.empty() || m_mode == MultiExtruder)
        return;

    auto draw_band = [](const ImU32& clr, const ImRect& band_rc)
    {
        ImGui::RenderFrame(band_rc.Min, band_rc.Max, clr, false);
    };

    auto get_tick_pos = [this, main_band](int tick)
    {
        int v_min = GetMinValue();
        int v_max = GetMaxValue();
        float tick_pos_ratio = (v_max - v_min) != 0 ? ((float)(tick - v_min) / (float)(v_max - v_min)) : 0.0f;
        tick_pos_ratio = 1.0f - tick_pos_ratio;
        float  tick_pos = main_band.Min.y + (main_band.Max.y - main_band.Min.y) * tick_pos_ratio;
        return tick_pos;
    };

    float btn_offset = 35.0f;
    float tick_offset = 12.0f;
    ImVec2 btn_size = ImVec2(16.0f, 16.0f);
    ImVec2 tick_size = ImVec2(20.0f, 4.0f);

    ImRect band_rect(main_band);
    const int default_color_idx = m_mode == MultiAsSingle ? std::max<int>(m_only_extruder - 1, 0) : 0;
    std::array<float, 4>rgba = decode_color_to_float_array(m_extruder_colors[default_color_idx]);
    ImU32 band_clr = IM_COL32(rgba[0] * 255.0f, rgba[1] * 255.0f, rgba[2] * 255.0f, rgba[3] * 255.0f);
    draw_band(band_clr, band_rect);

    static float selected_tick_pos;
    static int selected_tick;
    std::set<TickCode>::const_iterator tick_it = m_ticks.ticks.begin();
    while (tick_it != m_ticks.ticks.end())
    {
        //get position from tick
        float  tick_pos = get_tick_pos(tick_it->tick);

        //draw colored band
        if (tick_it->type == ToolChange) {
            if ((m_mode == SingleExtruder) || (m_mode == MultiAsSingle))
            {
                //TODO:band_rect width need to be ajusted
                band_rect = ImRect(main_band.Min, ImVec2(main_band.Max.x, tick_pos));

                const std::string clr_str = m_mode == SingleExtruder ? tick_it->color : get_color_for_tool_change_tick(tick_it);

                if (!clr_str.empty()) {
                    std::array<float, 4>rgba = decode_color_to_float_array(clr_str);
                    ImU32 band_clr = IM_COL32(rgba[0] * 255.0f, rgba[1] * 255.0f, rgba[2] * 255.0f, rgba[3] * 255.0f);
                    draw_band(band_clr, band_rect);
                }
            }
        }

        //draw tick
        ImGui::PushID(tick_it->tick);
        std::wstring tick_btn_name = ImGui::TickIcon + boost::nowide::widen(std::string(""));
        ImVec2 tick_start_pos(main_band.GetCenter().x - tick_offset - tick_size.x / 2, tick_pos - tick_size.y / 2);
        if (button_with_pos(into_u8(tick_btn_name).c_str(), tick_size, tick_start_pos)) {
            //record clicked tick
            selected_tick_pos = tick_pos;
            selected_tick = tick_it->tick;
            m_selected_tick = true;
        }

        //draw pause icon
        if (tick_it->type == PausePrint && selected_tick != tick_it->tick) {
            std::wstring pause_btn_name = ImGui::GcodePauseIcon + boost::nowide::widen(std::string(""));
            if (button_with_pos(into_u8(pause_btn_name).c_str(), btn_size, ImVec2(main_band.GetCenter().x - btn_offset - btn_size.x / 2, tick_pos - btn_size.y / 2)))
            {
                int i = 0;
                //TODO:pause button reaction
            }
        }
        ImGui::PopID();
        ++tick_it;
    }

    //draw delete_tick icon
    if (m_selected_tick) {
        std::wstring tick_close_btn_name = ImGui::TickCloseIcon + boost::nowide::widen(std::string(""));
        if (button_with_pos(into_u8(tick_close_btn_name).c_str(), btn_size, ImVec2(main_band.GetCenter().x - btn_offset - btn_size.x / 2, selected_tick_pos - btn_size.y / 2)))
        {
            //delete tick
            auto it = m_ticks.ticks.find(TickCode{ selected_tick });
            if (it != m_ticks.ticks.end() && check_ticks_changed_event(it->type)) {
                Type type = it->type;
                m_ticks.ticks.erase(it);
                post_ticks_changed_event(type);
            }

            m_selected_tick = false;
        }
    }
}

bool IMSlider::vertical_slider(const char* str_id, int* higher_value, int* lower_value, std::string& higher_label, std::string& lower_label,
    int v_min, int v_max, ImVec2 size, SelectedSlider& selection, bool one_layer_flag, float scale)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImGui::SetWindowFontScale(1.0f / scale);
    if (window->SkipItems)
        return false;

    ImGuiContext& context = *GImGui;
    const ImGuiID id = window->GetID(str_id);

    ImVec2 padding(7, 11);
    float offset_of_handle = 10.0f / sqrt(2);
    float line_offset = 4.0f;
    float right_blank = 3.0f;
    float half_edge = 8.0f;     // triangle half edge
    ImVec2 pos = window->DC.CursorPos;
    const ImRect draw_region(pos, pos + size);
    ImGui::ItemSize(draw_region);

    float fixed_bar_width = 20.0f;
    ImVec2 text_content_size = ImVec2(28.0f, 30.0f);
    ImVec2 text_padding = ImVec2(4.0f, 0.0f);
    ImVec2 text_size = text_content_size + text_padding * 2;
    float  text_dummy_height = text_content_size.y / 2.0f;

    // calc slider bar size
    ImVec2 slider_bar_start = ImVec2(pos.x + size.x - fixed_bar_width - right_blank, pos.y + text_dummy_height);
    ImVec2 slider_bar_size = ImVec2(fixed_bar_width, size.y - text_dummy_height);

    // draw bg of slider
    const ImRect bg_rect(slider_bar_start, slider_bar_start + slider_bar_size);
    const ImU32 bg_rect_col = IM_COL32(255, 255, 255, 255);
    ImGui::RenderFrame(bg_rect.Min, bg_rect.Max, bg_rect_col, false, 0.5 * fixed_bar_width);

    // draw bg of scroll
    const ImRect scroll_rect(bg_rect.Min + padding, bg_rect.Max - padding);
    const ImU32 scroll_bg_col = IM_COL32(238, 238, 238, 255);
    ImGui::RenderFrame(scroll_rect.Min, scroll_rect.Max, scroll_bg_col, false, 0.5 * fixed_bar_width - padding.x);

    // set slideable region
    ImRect slideable_region(ImVec2(bg_rect.Min.x, scroll_rect.Min.y), ImVec2(bg_rect.Max.x, scroll_rect.Max.y));;
    ImRect higher_slideable_region = ImRect(slideable_region.Min, slideable_region.Max - ImVec2(0, offset_of_handle));
    ImRect lower_slideable_region = ImRect(slideable_region.Min + ImVec2(0, offset_of_handle), slideable_region.Max);
    ImRect triangle_slideable_region = ImRect(ImVec2(slideable_region.GetCenter().x - half_edge * 1.73f, slideable_region.Min.y), ImVec2(slideable_region.GetCenter().x, slideable_region.Max.y));

    // set active(draggable) region.
    const bool hovered = ImGui::ItemHoverable(slideable_region, id);
    if (hovered && context.IO.MouseDown[0]) {
        ImGui::SetActiveID(id, window);
        ImGui::SetFocusID(id, window);
        ImGui::FocusWindow(window);
    }
    // set initial position of the handles.
    ImS32 v_clamped = (v_min < v_max) ? ImClamp(*lower_value, v_min, v_max) : ImClamp(*lower_value, v_max, v_min);
    float handle_pos_ratio = (v_max - v_min) != 0 ? ((float)(v_clamped - v_min) / (float)(v_max - v_min)) : 0.0f;
    handle_pos_ratio = 1.0f - handle_pos_ratio;
    float  handle_pos = lower_slideable_region.Min.y + (lower_slideable_region.Max.y - lower_slideable_region.Min.y) * handle_pos_ratio;
    ImRect lower_handle = ImRect(lower_slideable_region.Min.x, handle_pos - offset_of_handle, lower_slideable_region.Max.x, handle_pos + offset_of_handle);

    v_clamped = (v_min < v_max) ? ImClamp(*higher_value, v_min, v_max) : ImClamp(*higher_value, v_max, v_min);
    handle_pos_ratio = (v_max - v_min) != 0 ? ((float)(v_clamped - v_min) / (float)(v_max - v_min)) : 0.0f;
    handle_pos_ratio = 1.0f - handle_pos_ratio;
    handle_pos = higher_slideable_region.Min.y + (higher_slideable_region.Max[1] - higher_slideable_region.Min.y) * handle_pos_ratio;
    ImRect higher_handle = ImRect(higher_slideable_region.Min.x, handle_pos - offset_of_handle, higher_slideable_region.Max.x, handle_pos + offset_of_handle);
    
    ImVec2 higher_handle_center = higher_handle.GetCenter();
    ImVec2 lower_handle_center = lower_handle.GetCenter();
    ImVec2 middle_center = higher_handle.GetCenter();

    ImRect triangle_handle = ImRect(ImVec2(middle_center.x - half_edge * 1.73f, middle_center.y - half_edge), ImVec2(middle_center.x, middle_center.y + half_edge));

    // draw colored band
    ImRect main_band(ImVec2(bg_rect.Min.x, scroll_rect.Min.y), ImVec2(bg_rect.Max.x, scroll_rect.Max.y));
    render_colored_band(pos, main_band);
   

    bool value_changed = false;
    const ImU32 handle_and_line_col = IM_COL32(0, 174, 66, 255);
    const ImU32 handle_and_line_border_col = IM_COL32(0, 98, 0, 255);
    if (!one_layer_flag) {
        // set a select-region where can select a handle when clicked
        bool higher_handle_clicked = false;
        bool lower_handle_clicked = false;
        higher_handle_clicked = (ImGui::ItemHoverable(higher_handle, id) && context.IO.MouseClicked[0]);
        lower_handle_clicked = (ImGui::ItemHoverable(lower_handle, id) && context.IO.MouseClicked[0]);

        // select higher handle by default
        if (selection == ssUndef) { selection = ssHigher; }
        if (higher_handle_clicked) { selection = ssHigher; }
        if (lower_handle_clicked) { selection = ssLower; }

        // update handle position and value
        if (selection == ssHigher)
            value_changed = slider_behavior(id, higher_slideable_region, v_min, v_max,
                higher_value, &higher_handle, ImGuiSliderFlags_Vertical);
        if (selection == ssLower)
            value_changed = slider_behavior(id, lower_slideable_region, v_min, v_max,
                lower_value, &lower_handle, ImGuiSliderFlags_Vertical);

        higher_handle_center = higher_handle.GetCenter();
        lower_handle_center = lower_handle.GetCenter();
        if (higher_handle_center.y + offset_of_handle > lower_handle_center.y && selection == ssHigher)
        {
            lower_handle = higher_handle;
            lower_handle.TranslateY(offset_of_handle);
            lower_handle_center.y = higher_handle_center.y + offset_of_handle;
            *lower_value = *higher_value;
        }
        if (higher_handle_center.y + offset_of_handle > lower_handle_center.y && selection == ssLower)
        {
            higher_handle = lower_handle;
            higher_handle.TranslateY(-offset_of_handle);
            higher_handle_center.y = lower_handle_center.y - offset_of_handle;
            *higher_value = *lower_value;
        }

        // judge whether to open menu
        if (is_clicked_in_rect(selection == ssHigher ? higher_handle : lower_handle, 1) == ClickedIn)
            m_show_menu = true;
        if (is_clicked_in_rect(selection == ssHigher ? higher_handle : lower_handle, 1) == ClickedOut)
            m_show_menu = false;

        // draw slider border
        const ImRect scroll_line(ImVec2(scroll_rect.Min.x, higher_handle_center.y), ImVec2(scroll_rect.Max.x, lower_handle_center.y));
        window->DrawList->AddRect(scroll_line.Min - ImVec2(1, 1), scroll_line.Max + ImVec2(1, 1), handle_and_line_border_col, 2.0f);
        window->DrawList->AddNgon(higher_handle_center, offset_of_handle + 1, handle_and_line_border_col, 4);
        window->DrawList->AddNgon(lower_handle_center, offset_of_handle + 1, handle_and_line_border_col, 4);
        // draw slider
        window->DrawList->AddRectFilled(scroll_line.Min, scroll_line.Max, handle_and_line_col, 2.0f);
        window->DrawList->AddNgonFilled(higher_handle_center, offset_of_handle, handle_and_line_col, 4);
        window->DrawList->AddNgonFilled(lower_handle_center, offset_of_handle, handle_and_line_col, 4);

        //draw cross lines
        if (selection == ssHigher) {
            window->DrawList->AddLine(higher_handle_center + ImVec2(-line_offset, 0.0f), higher_handle_center + ImVec2(line_offset, 0.0f), IM_COL32(255, 255, 255, 255));
            window->DrawList->AddLine(higher_handle_center + ImVec2(0.0f, -line_offset), higher_handle_center + ImVec2(0.0f, line_offset), IM_COL32(255, 255, 255, 255));
        }
        if (selection == ssLower) {
            window->DrawList->AddLine(lower_handle_center + ImVec2(-line_offset, 0.0f), lower_handle_center + ImVec2(line_offset, 0.0f), IM_COL32(255, 255, 255, 255));
            window->DrawList->AddLine(lower_handle_center + ImVec2(0.0f, -line_offset), lower_handle_center + ImVec2(0.0f, line_offset), IM_COL32(255, 255, 255, 255));
        }

        // draw higher label
        ImVec2 text_start = ImVec2(pos.x, higher_handle_center.y - text_size.y);
        ImRect text_rect(text_start, text_start + text_size);
        ImGui::RenderFrame(text_rect.Min, text_rect.Max, bg_rect_col, false, 2.0f);
        ImVec2 pos_1 = text_rect.Max;
        ImVec2 pos_2 = ImVec2(pos_1.x, higher_handle_center.y - 6.0f);
        ImVec2 pos_3 = ImVec2(higher_handle_center.x - 0.5 * fixed_bar_width, higher_handle_center.y);
        window->DrawList->AddTriangleFilled(pos_1, pos_2, pos_3, bg_rect_col);
        ImGui::RenderText(text_start + text_padding, higher_label.c_str());
        //draw lower label
        text_start = ImVec2(pos.x, lower_handle_center.y);
        text_rect = ImRect(text_start, text_start + text_size);
        ImGui::RenderFrame(text_rect.Min, text_rect.Max, bg_rect_col, false, 2.0f);
        pos_1 = ImVec2(text_rect.Max.x, text_rect.Min.y);
        pos_2 = ImVec2(pos_1.x, lower_handle_center.y + 6.0f);
        pos_3 = ImVec2(lower_handle_center.x - 0.5 * fixed_bar_width, lower_handle_center.y);
        window->DrawList->AddTriangleFilled(pos_1, pos_2, pos_3, bg_rect_col);
        ImGui::RenderText(text_start + text_padding, lower_label.c_str());
    }
    else {
        // update handle position
        value_changed = slider_behavior(id, triangle_slideable_region, v_min, v_max,
                higher_value, &triangle_handle, ImGuiSliderFlags_Vertical);
        ImVec2 triangle_center = triangle_handle.GetCenter();
        middle_center.y = triangle_center.y;

        // judge whether to open menu
        if (is_clicked_in_rect(triangle_handle, 1) == ClickedIn)
            m_show_menu = true;
        if (is_clicked_in_rect(triangle_handle, 1) == ClickedOut)
            m_show_menu = false;
        
        // draw handle
        ImVec2 pos_1 = triangle_center - ImVec2(0.5f * half_edge * 1.73f, half_edge);
        ImVec2 pos_2 = triangle_center - ImVec2(0.5f * half_edge * 1.73f, -half_edge);
        ImVec2 pos_3 = triangle_center + ImVec2(0.5f * half_edge * 1.73f, 0.0f);
        window->DrawList->AddRect(middle_center - ImVec2(0.5 * fixed_bar_width, 1.0f) - ImVec2(0.6f, 0.6f), middle_center + ImVec2(0.5f * fixed_bar_width + right_blank, 1.0f) + ImVec2(0.6f, 0.6f), handle_and_line_border_col, false, 1.0f);
        window->DrawList->AddTriangle(pos_1 - ImVec2(1.0f, 1.0f), pos_2 - ImVec2(1.0f, -1.0f), pos_3 + ImVec2(1.0f, 0.0f), handle_and_line_border_col);
        window->DrawList->AddRectFilled(middle_center - ImVec2(0.5 * fixed_bar_width, 1.0f), middle_center + ImVec2(0.5f * fixed_bar_width + right_blank, 1.0f), handle_and_line_col, false, 1.0f);
        window->DrawList->AddTriangleFilled(pos_1, pos_2, pos_3, handle_and_line_col);

        // draw cross lines
        ImVec2 pos_4 = ImVec2(middle_center.x - half_edge * 1.73f + half_edge / 1.73f, middle_center.y);
        window->DrawList->AddLine(pos_4 + ImVec2(-line_offset, 0.0f), pos_4 + ImVec2(line_offset, 0.0f), IM_COL32(255, 255, 255, 255));
        window->DrawList->AddLine(pos_4 + ImVec2(0.0, -line_offset), pos_4 + ImVec2(0.0, line_offset), IM_COL32(255, 255, 255, 255));

        // draw label
        ImVec2 text_start = ImVec2(pos.x, middle_center.y - 0.5 * text_size.y);
        ImRect text_rect = ImRect(text_start, text_start + text_size);
        ImGui::RenderFrame(text_rect.Min, text_rect.Max, bg_rect_col, false, 2.0f);
        ImGui::RenderText(text_start + text_padding, higher_label.c_str());
    }

    ImGui::SetWindowFontScale(1.0f);

    return value_changed;
}

bool IMSlider::render(int canvas_width, int canvas_height)
{
    bool result = false;
    ImGuiWrapper &imgui = *wxGetApp().imgui();
    /* style and colors */
    ImGui::PushStyleVar(ImGuiStyleVar_::ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleColor(ImGuiCol_::ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0.682f, 0.259f, 1.0f));

    int windows_flag = ImGuiWindowFlags_NoTitleBar
                       | ImGuiWindowFlags_NoCollapse
                       | ImGuiWindowFlags_NoMove
                       | ImGuiWindowFlags_NoResize
                       | ImGuiWindowFlags_NoScrollbar
                       | ImGuiWindowFlags_NoScrollWithMouse;

    float scale = (float) wxGetApp().em_unit() / 10.0f;

    if (is_horizontal()) {
        // use maxium slider
        //float  pos_x = std::max(float(canvas_width - MIN_RECT_SIZE.x - HORIZONTAL_SLIDER_SIZE.x), 0.0f);
        //ImVec2 size  = ImVec2(std::min(HORIZONTAL_SLIDER_SIZE.x, canvas_width - MIN_RECT_SIZE.x), HORIZONTAL_SLIDER_SIZE.y);
        float  pos_x = LEFT_MARGIN;
        float  pos_y = std::max(float(canvas_height - HORIZONTAL_SLIDER_SIZE.y - BOTTOM_MARGIN), 0.0f);
        ImVec2 size  = ImVec2(std::max(0.0f, canvas_width - MIN_RECT_SIZE.x - LEFT_MARGIN), HORIZONTAL_SLIDER_SIZE.y);
        imgui.set_next_window_pos(pos_x, pos_y, ImGuiCond_Always);
        imgui.begin(std::string("moves_slider"), windows_flag);
        int value = GetHigherValue();
        if (horizontal_slider("moves_slider", &value, GetMinValue(), GetMaxValue(), size, scale)) {
            result = true;
            SetHigherValue(value);
        }
        imgui.end();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.68f, 0.26f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.68f, 0.26f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
        float  pos_x = std::max(float(canvas_width - VERTICAL_SLIDER_SIZE.x - RIGHT_MARGIN), 0.0f);
        //float  pos_y = std::max(float(canvas_height - VERTICAL_SLIDER_SIZE.y - MIN_RECT_SIZE.y), 0.0f);
        //ImVec2 size  = ImVec2(VERTICAL_SLIDER_SIZE.x, std::min(VERTICAL_SLIDER_SIZE.y, canvas_height - MIN_RECT_SIZE.y));
        // use maxium slider
        float pos_y = TOP_MARGIN;
        ImVec2 size  = ImVec2(VERTICAL_SLIDER_SIZE.x, std::max(0.0f,canvas_height - MIN_RECT_SIZE.y - TOP_MARGIN));
        imgui.set_next_window_pos(pos_x, pos_y, ImGuiCond_Always);
        imgui.begin(std::string("laysers_slider"), windows_flag);

        render_menu();

        int higher_value = GetHigherValue();
        int lower_value = GetLowerValue();
        std::string higher_label = get_label(m_higher_value);
        std::string lower_label  = get_label(m_lower_value);
        int temp_higher_value    = higher_value;
        int temp_lower_value     = lower_value;
        if (vertical_slider("laysers_slider", &higher_value, &lower_value, higher_label, lower_label, GetMinValue(), GetMaxValue(),
                  size, m_selection, is_one_layer(), scale)) {
            if (temp_higher_value != higher_value)
                SetHigherValue(higher_value);
            if (temp_lower_value != lower_value)
                SetLowerValue(lower_value);
            result = true;
        }
        ImGui::PopStyleVar(1);
        ImGui::PopStyleColor(3);

        ImGui::Dummy(ImVec2(8,0));
        ImGui::SameLine(44);
        ImTextureID normal_id = is_one_layer() ? m_one_layer_on_id : m_one_layer_off_id;
        ImTextureID hover_id  = is_one_layer() ? m_one_layer_on_hover_id : m_one_layer_off_hover_id;
        if (ImGui::ImageButton3(normal_id, hover_id, ImVec2(28, 28))) {
            switch_one_layer_mode();
        }
        imgui.end();
    }

    ImGui::PopStyleVar(1);
    ImGui::PopStyleColor(2);
    return result;
}

void IMSlider::render_menu()
{
    ImGuiWrapper::push_menu_style();
    std::vector<std::string> colors = wxGetApp().plater()->get_extruder_colors_from_plater_config();
    int extruder_num = colors.size();

    if (m_show_menu) {
        ImGui::OpenPopup("slider_menu_popup");
    }

    if (ImGui::BeginPopup("slider_menu_popup")) {
        bool selected = false;
        ImGui::MenuItem(_u8L("Add Pause").c_str(), "", &selected);
        if (selected) { add_code_as_tick(PausePrint); }

        if (ImGui::BeginMenu(_u8L("Change Filament").c_str())) {
            for (int i = 0; i < extruder_num; i++) {
                std::array<float, 4>rgba = decode_color_to_float_array(colors[i]);
                ImU32 icon_clr = IM_COL32(rgba[0] * 255.0f, rgba[1] * 255.0f, rgba[2] * 255.0f, rgba[3] * 255.0f);
                if (menu_item_with_icon((_u8L("Filament ") + std::to_string(i + 1)).c_str(), "", icon_clr, false, true))
                    add_code_as_tick(ToolChange, i + 1);
            }
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }
    ImGuiWrapper::pop_menu_style();
}

void IMSlider::correct_lower_value()
{
    if (m_lower_value < m_min_value)
        m_lower_value = m_min_value;
    else if (m_lower_value > m_max_value)
        m_lower_value = m_max_value;

    if ((m_lower_value >= m_higher_value && m_lower_value <= m_max_value) || m_is_one_layer) m_higher_value = m_lower_value;
}

void IMSlider::correct_higher_value()
{
    if (m_higher_value > m_max_value)
        m_higher_value = m_max_value;
    else if (m_higher_value < m_min_value)
        m_higher_value = m_min_value;

    if ((m_higher_value <= m_lower_value && m_higher_value >= m_min_value) || m_is_one_layer) m_lower_value = m_higher_value;
}

bool IMSlider::is_wipe_tower_layer(int tick) const
{
    if (!m_is_wipe_tower || tick >= (int) m_values.size()) return false;
    if (tick == 0 || (tick == (int) m_values.size() - 1 && m_values[tick] > m_values[tick - 1])) return false;
    if ((m_values[tick - 1] == m_values[tick + 1] && m_values[tick] < m_values[tick + 1]) ||
        (tick > 0 && m_values[tick] < m_values[tick - 1])) // if there is just one wiping on the layer
        return true;

    return false;
}

std::string IMSlider::get_label(int tick, LabelType label_type)
{
    const size_t value = tick;

    if (m_label_koef == 1.0 && m_values.empty()) {
        std::to_string(value);
    }
    if (value >= m_values.size()) return "error";

    auto get_layer_number = [this](int value, LabelType label_type) {
        if (label_type == ltEstimatedTime && m_layers_times.empty()) return size_t(-1);
        double layer_print_z = m_values[is_wipe_tower_layer(value) ? std::max<int>(value - 1, 0) : value];
        auto   it            = std::lower_bound(m_layers_values.begin(), m_layers_values.end(), layer_print_z - epsilon());
        if (it == m_layers_values.end()) {
            it = std::lower_bound(m_values.begin(), m_values.end(), layer_print_z - epsilon());
            if (it == m_values.end()) return size_t(-1);
            return size_t(value);
        }
        return size_t(it - m_layers_values.begin());
    };

    if (m_draw_mode == dmSequentialGCodeView) {
        return std::to_string(tick);

    } else {
        if (label_type == ltEstimatedTime) {
            if (m_is_wipe_tower) {
                size_t layer_number = get_layer_number(value, label_type);
                return (layer_number == size_t(-1) || layer_number == m_layers_times.size()) ? "" : short_and_splitted_time(get_time_dhms(m_layers_times[layer_number]));
            }
            return value < m_layers_times.size() ? short_and_splitted_time(get_time_dhms(m_layers_times[value])) : "";
        }

        char layer_height[64];
        ::sprintf(layer_height, "%.2f", m_values.empty() ? m_label_koef * value : m_values[value]);
        if (label_type == ltHeight) return std::string(layer_height);
        if (label_type == ltHeightWithLayer) {
            size_t layer_number = m_is_wipe_tower ? get_layer_number(value, label_type) + 1 : (m_values.empty() ? value : value + 1);
            char   buffer[64];
            ::sprintf(buffer, "%5s\n%5s", std::to_string(layer_number).c_str(), layer_height);
            return std::string(buffer);
        }
    }

    return "";
}

double IMSlider::get_double_value(const SelectedSlider &selection)
{
    if (m_values.empty() || m_lower_value < 0) return 0.0;
    if (m_values.size() <= size_t(m_higher_value)) {
        correct_higher_value();
        return m_values.back();
    }
    return m_values[selection == ssLower ? m_lower_value : m_higher_value];
}

int IMSlider::get_tick_from_value(double value, bool force_lower_bound /* = false*/)
{
    std::vector<double>::iterator it;
    if (m_is_wipe_tower && !force_lower_bound)
        it = std::find_if(m_values.begin(), m_values.end(), [value](const double &val) { return fabs(value - val) <= epsilon(); });
    else
        it = std::lower_bound(m_values.begin(), m_values.end(), value - epsilon());

    if (it == m_values.end()) return -1;
    return int(it - m_values.begin());
}

std::string IMSlider::get_color_for_tool_change_tick(std::set<TickCode>::const_iterator it) const
{
    const int current_extruder = it->extruder == 0 ? std::max<int>(m_only_extruder, 1) : it->extruder;

    auto it_n = it;
    while (it_n != m_ticks.ticks.begin()) {
        --it_n;
        if (it_n->type == ColorChange && it_n->extruder == current_extruder)
            return it_n->color;
    }

    if ((current_extruder > 0 && (current_extruder - 1) < m_extruder_colors.size()))
    {
        return m_extruder_colors[current_extruder - 1]; // return a color for a specific extruder from the colors list
    }
    return "";
}

// Get active extruders for tick.
// Means one current extruder for not existing tick OR
// 2 extruders - for existing tick (extruder before ToolChange and extruder of current existing tick)
// Use those values to disable selection of active extruders
std::array<int, 2> IMSlider::get_active_extruders_for_tick(int tick) const
{
    int                default_initial_extruder = m_mode == MultiAsSingle ? std::max<int>(1, m_only_extruder) : 1;
    std::array<int, 2> extruders                = {default_initial_extruder, -1};
    if (m_ticks.empty()) return extruders;

    auto it = m_ticks.ticks.lower_bound(TickCode{tick});

    if (it != m_ticks.ticks.end() && it->tick == tick) // current tick exists
        extruders[1] = it->extruder;

    while (it != m_ticks.ticks.begin()) {
        --it;
        if (it->type == ToolChange) {
            extruders[0] = it->extruder;
            break;
        }
    }

    return extruders;
}

}

} // Slic3r


