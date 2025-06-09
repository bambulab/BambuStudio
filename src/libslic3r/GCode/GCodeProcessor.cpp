#include "libslic3r/libslic3r.h"
#include "libslic3r/Utils.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/LocalesUtils.hpp"
#include "libslic3r/format.hpp"
#include "GCodeProcessor.hpp"

#include <boost/log/trivial.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/nowide/cstdio.hpp>
#include <boost/filesystem/path.hpp>

#include <fast_float/fast_float.h>

#include <float.h>
#include <assert.h>
#include <regex>

#if __has_include(<charconv>)
    #include <charconv>
    #include <utility>
#endif

#include <chrono>

static const float DEFAULT_TOOLPATH_WIDTH = 0.4f;
static const float DEFAULT_TOOLPATH_HEIGHT = 0.2f;

static const float INCHES_TO_MM = 25.4f;
static const float MMMIN_TO_MMSEC = 1.0f / 60.0f;
static const float DRAW_ARC_TOLERANCE = 0.0125f;            //0.0125mm tolerance for drawing arc

static const float DEFAULT_ACCELERATION = 1500.0f; // Prusa Firmware 1_75mm_MK2
static const float DEFAULT_RETRACT_ACCELERATION = 1500.0f; // Prusa Firmware 1_75mm_MK2
static const float DEFAULT_TRAVEL_ACCELERATION = 1250.0f;

static const size_t MIN_EXTRUDERS_COUNT = 5;
static const float DEFAULT_FILAMENT_DIAMETER = 1.75f;
static const int   DEFAULT_FILAMENT_HRC = 0;
static const float DEFAULT_FILAMENT_DENSITY = 1.245f;
static const float DEFAULT_FILAMENT_COST = 29.99f;
static const int   DEFAULT_FILAMENT_VITRIFICATION_TEMPERATURE = 0;
static const Slic3r::Vec3f DEFAULT_EXTRUDER_OFFSET = Slic3r::Vec3f::Zero();

namespace Slic3r {

const std::vector<std::string> GCodeProcessor::ReservedTags = {
    " FEATURE: ",
    " WIPE_START",
    " WIPE_END",
    " LAYER_HEIGHT: ",
    " LINE_WIDTH: ",
    " CHANGE_LAYER",
    " COLOR_CHANGE",
    " PAUSE_PRINTING",
    " CUSTOM_GCODE",
    "_GP_FIRST_LINE_M73_PLACEHOLDER",
    "_GP_LAST_LINE_M73_PLACEHOLDER",
    "_GP_ESTIMATED_PRINTING_TIME_PLACEHOLDER",
    "_GP_TOTAL_LAYER_NUMBER_PLACEHOLDER",
    " WIPE_TOWER_START",
    " WIPE_TOWER_END",
    "_GP_FILAMENT_USED_WEIGHT_PLACEHOLDER",
    "_GP_FILAMENT_USED_VOLUME_PLACEHOLDER",
    "_GP_FILAMENT_USED_LENGTH_PLACEHOLDER",
    " MACHINE_START_GCODE_END",
    " MACHINE_END_GCODE_START",
    " NOZZLE_CHANGE_START",
    " NOZZLE_CHANGE_END"
};

const std::vector<std::string> GCodeProcessor::CustomTags = {
    " FLUSH_START",
    " FLUSH_END",
    " VFLUSH_START",
    " VFLUSH_END",
    " SKIPPABLE_START",
    " SKIPPABLE_END",
    " SKIPTYPE: "
};


const float GCodeProcessor::Wipe_Width = 0.05f;
const float GCodeProcessor::Wipe_Height = 0.05f;

bool GCodeProcessor::s_IsBBLPrinter = true;

#if ENABLE_GCODE_VIEWER_DATA_CHECKING
const std::string GCodeProcessor::Mm3_Per_Mm_Tag = "MM3_PER_MM:";
#endif // ENABLE_GCODE_VIEWER_DATA_CHECKING

static void set_option_value(ConfigOptionFloats& option, size_t id, float value)
{
    if (id < option.values.size())
        option.values[id] = static_cast<double>(value);
};

static void set_option_value(ConfigOptionFloatsNullable& option, size_t id, float value)
{
    if (id < option.values.size())
        option.values[id] = static_cast<double>(value);
};

static float get_option_value(const ConfigOptionFloats& option, size_t id)
{
    return option.values.empty() ? 0.0f :
        ((id < option.values.size()) ? static_cast<float>(option.values[id]) : static_cast<float>(option.values.back()));
}

static float get_option_value(const ConfigOptionFloatsNullable& option, size_t id)
{
    return option.values.empty() ? 0.0f :
        ((id < option.values.size()) ? static_cast<float>(option.values[id]) : static_cast<float>(option.values.back()));
}

static float estimated_acceleration_distance(float initial_rate, float target_rate, float acceleration)
{
    return (acceleration == 0.0f) ? 0.0f : (sqr(target_rate) - sqr(initial_rate)) / (2.0f * acceleration);
}

static float intersection_distance(float initial_rate, float final_rate, float acceleration, float distance)
{
    return (acceleration == 0.0f) ? 0.0f : (2.0f * acceleration * distance - sqr(initial_rate) + sqr(final_rate)) / (4.0f * acceleration);
}

static float speed_from_distance(float initial_feedrate, float distance, float acceleration)
{
    // to avoid invalid negative numbers due to numerical errors
    float value = std::max(0.0f, sqr(initial_feedrate) + 2.0f * acceleration * distance);
    return ::sqrt(value);
}

// Calculates the maximum allowable speed at this point when you must be able to reach target_velocity using the
// acceleration within the allotted distance.
static float max_allowable_speed(float acceleration, float target_velocity, float distance)
{
    // to avoid invalid negative numbers due to numerical errors
    float value = std::max(0.0f, sqr(target_velocity) - 2.0f * acceleration * distance);
    return std::sqrt(value);
}

static float acceleration_time_from_distance(float initial_feedrate, float distance, float acceleration)
{
    return (acceleration != 0.0f) ? (speed_from_distance(initial_feedrate, distance, acceleration) - initial_feedrate) / acceleration : 0.0f;
}

static int get_object_label_id(const std::string_view comment_1)
{
    std::string      comment(comment_1);
    auto pos = comment.find(":");
    std::string num_str = comment.substr(pos + 1);
    int id = -1;
    try {
        id = stoi(num_str);
    }
    catch (const std::exception &) {}
    return id;
}

static float get_z_height(const std::string_view comment_1)
{
    std::string comment(comment_1);
    auto pos = comment.find(":");
    std::string num_str = comment.substr(pos + 1);
    float print_z = 0.0f;
    try {
        print_z = stof(num_str);
    } catch (const std::exception &) {}
    return print_z;
}

CommandProcessor::CommandProcessor()
{
    root = std::make_unique<TrieNode>();
}

void CommandProcessor::register_command(const std::string& str, command_handler_t handler, bool early_quit)
{
    TrieNode* node = root.get();
    for (char ch : str) {
        auto iter = node->children.find(ch);
        if (iter == node->children.end()) {
            std::unique_ptr<TrieNode> new_node = std::make_unique<TrieNode>();
            auto raw_ptr = new_node.get();
            node->children[ch] = std::move(new_node);
            node = raw_ptr;
        }
        else {
            node = iter->second.get();
        }
    }
    if (node->handler != nullptr) {
        assert(false);// duplicated command
    }
    node->handler = handler;
    node->early_quit = early_quit;
}

bool CommandProcessor::process_comand(std::string_view cmd, const GCodeReader::GCodeLine& line)
{
    TrieNode* node = root.get();
    for (char ch : cmd) {
        if (node->early_quit && node->handler) {
            node->handler(line);
            return true;
        }
        auto iter = node->children.find(ch);
        if (iter == node->children.end()) {
            return false;
        }
        node = iter->second.get();
    }
    if (!node || !node->handler)
        return false;
    node->handler(line);
    return true;
}

void GCodeProcessor::CachedPosition::reset()
{
    std::fill(position.begin(), position.end(), FLT_MAX);
    feedrate = FLT_MAX;
}

void GCodeProcessor::CpColor::reset()
{
    counter = 0;
    current = 0;
}

float GCodeProcessor::Trapezoid::acceleration_time(float entry_feedrate, float acceleration) const
{
    return acceleration_time_from_distance(entry_feedrate, accelerate_until, acceleration);
}

float GCodeProcessor::Trapezoid::cruise_time() const
{
    return (cruise_feedrate != 0.0f) ? cruise_distance() / cruise_feedrate : 0.0f;
}

float GCodeProcessor::Trapezoid::deceleration_time(float distance, float acceleration) const
{
    return acceleration_time_from_distance(cruise_feedrate, (distance - decelerate_after), -acceleration);
}

float GCodeProcessor::Trapezoid::cruise_distance() const
{
    return decelerate_after - accelerate_until;
}

void GCodeProcessor::TimeBlock::calculate_trapezoid()
{
    trapezoid.cruise_feedrate = feedrate_profile.cruise;

    float accelerate_distance = std::max(0.0f, estimated_acceleration_distance(feedrate_profile.entry, feedrate_profile.cruise, acceleration));
    float decelerate_distance = std::max(0.0f, estimated_acceleration_distance(feedrate_profile.cruise, feedrate_profile.exit, -acceleration));
    float cruise_distance = distance - accelerate_distance - decelerate_distance;

    // Not enough space to reach the nominal feedrate.
    // This means no cruising, and we'll have to use intersection_distance() to calculate when to abort acceleration
    // and start braking in order to reach the exit_feedrate exactly at the end of this block.
    if (cruise_distance < 0.0f) {
        accelerate_distance = std::clamp(intersection_distance(feedrate_profile.entry, feedrate_profile.exit, acceleration, distance), 0.0f, distance);
        cruise_distance = 0.0f;
        trapezoid.cruise_feedrate = speed_from_distance(feedrate_profile.entry, accelerate_distance, acceleration);
    }

    trapezoid.accelerate_until = accelerate_distance;
    trapezoid.decelerate_after = accelerate_distance + cruise_distance;
}

float GCodeProcessor::TimeBlock::time() const
{
    return trapezoid.acceleration_time(feedrate_profile.entry, acceleration)
        + trapezoid.cruise_time()
        + trapezoid.deceleration_time(distance, acceleration);
}

void GCodeProcessor::TimeMachine::State::reset()
{
    feedrate = 0.0f;
    safe_feedrate = 0.0f;
    axis_feedrate = { 0.0f, 0.0f, 0.0f, 0.0f };
    abs_axis_feedrate = { 0.0f, 0.0f, 0.0f, 0.0f };
    //BBS
    enter_direction = { 0.0f, 0.0f, 0.0f };
    exit_direction = { 0.0f, 0.0f, 0.0f };
}

void GCodeProcessor::TimeMachine::CustomGCodeTime::reset()
{
    needed = false;
    cache = 0.0f;
    times = std::vector<std::pair<CustomGCode::Type, float>>();
}

void GCodeProcessor::TimeMachine::reset()
{
    enabled = false;
    acceleration = 0.0f;
    max_acceleration = 0.0f;
    retract_acceleration = 0.0f;
    max_retract_acceleration = 0.0f;
    travel_acceleration = 0.0f;
    max_travel_acceleration = 0.0f;
    extrude_factor_override_percentage = 1.0f;
    time = 0.0f;
    stop_times = std::vector<StopTime>();
    curr.reset();
    prev.reset();
    gcode_time.reset();
    blocks = std::vector<TimeBlock>();
    g1_times_cache = std::vector<G1LinesCacheItem>();
    std::fill(moves_time.begin(), moves_time.end(), 0.0f);
    std::fill(roles_time.begin(), roles_time.end(), 0.0f);
    layers_time = std::vector<float>();
    prepare_time = 0.0f;
    m_additional_time_buffer.clear();
}

void GCodeProcessor::TimeMachine::simulate_st_synchronize(float additional_time, ExtrusionRole target_role, block_handler_t block_handler)
{
    if (!enabled)
        return;

    calculate_time(0, additional_time,target_role,block_handler);
}

static void planner_forward_pass_kernel(GCodeProcessor::TimeBlock& prev, GCodeProcessor::TimeBlock& curr)
{
    // If the previous block is an acceleration block, but it is not long enough to complete the
    // full speed change within the block, we need to adjust the entry speed accordingly. Entry
    // speeds have already been reset, maximized, and reverse planned by reverse planner.
    // If nominal length is true, max junction speed is guaranteed to be reached. No need to recheck.
    if (!prev.flags.nominal_length) {
        if (prev.feedrate_profile.entry < curr.feedrate_profile.entry) {
            float entry_speed = std::min(curr.feedrate_profile.entry, max_allowable_speed(-prev.acceleration, prev.feedrate_profile.entry, prev.distance));

            // Check for junction speed change
            if (curr.feedrate_profile.entry != entry_speed) {
                curr.feedrate_profile.entry = entry_speed;
                curr.flags.recalculate = true;
            }
        }
    }
}

void planner_reverse_pass_kernel(GCodeProcessor::TimeBlock& curr, GCodeProcessor::TimeBlock& next)
{
    // If entry speed is already at the maximum entry speed, no need to recheck. Block is cruising.
    // If not, block in state of acceleration or deceleration. Reset entry speed to maximum and
    // check for maximum allowable speed reductions to ensure maximum possible planned speed.
    if (curr.feedrate_profile.entry != curr.max_entry_speed) {
        // If nominal length true, max junction speed is guaranteed to be reached. Only compute
        // for max allowable speed if block is decelerating and nominal length is false.
        if (!curr.flags.nominal_length && curr.max_entry_speed > next.feedrate_profile.entry)
            curr.feedrate_profile.entry = std::min(curr.max_entry_speed, max_allowable_speed(-curr.acceleration, next.feedrate_profile.entry, curr.distance));
        else
            curr.feedrate_profile.entry = curr.max_entry_speed;

        curr.flags.recalculate = true;
    }
}

static void recalculate_trapezoids(std::vector<GCodeProcessor::TimeBlock>& blocks)
{
    GCodeProcessor::TimeBlock* curr = nullptr;
    GCodeProcessor::TimeBlock* next = nullptr;

    for (size_t i = 0; i < blocks.size(); ++i) {
        GCodeProcessor::TimeBlock& b = blocks[i];

        curr = next;
        next = &b;

        if (curr != nullptr) {
            // Recalculate if current block entry or exit junction speed has changed.
            if (curr->flags.recalculate || next->flags.recalculate) {
                // NOTE: Entry and exit factors always > 0 by all previous logic operations.
                GCodeProcessor::TimeBlock block = *curr;
                block.feedrate_profile.exit = next->feedrate_profile.entry;
                block.calculate_trapezoid();
                curr->trapezoid = block.trapezoid;
                curr->flags.recalculate = false; // Reset current only to ensure next trapezoid is computed
            }
        }
    }

    // Last/newest block in buffer. Always recalculated.
    if (next != nullptr) {
        GCodeProcessor::TimeBlock block = *next;
        block.feedrate_profile.exit = next->safe_feedrate;
        block.calculate_trapezoid();
        next->trapezoid = block.trapezoid;
        next->flags.recalculate = false;
    }
}

void GCodeProcessor::TimeMachine::handle_time_block(const TimeBlock& block, float time, int activate_machine_idx, GCodeProcessorResult& result)
{
    if (block.skippable_type != SkipType::stNone)
        result.skippable_part_time[block.skippable_type] += block.time();
    result.moves[block.move_id].time[activate_machine_idx] = time;
}

GCodeProcessor::TimeMachine::AdditionalBuffer GCodeProcessor::TimeMachine::merge_adjacent_addtional_time_blocks(const AdditionalBuffer& buffer)
{
    AdditionalBuffer merged;
    if(buffer.empty())
        return merged;

    auto current_block = buffer.front();
    for(size_t idx = 1; idx < buffer.size(); ++idx){
        auto next_block = buffer[idx];
        if(current_block.first == next_block.first){
            current_block.second += next_block.second;
        }else{
            merged.push_back(current_block);
            current_block = next_block;
        }
    }
    merged.push_back(current_block);
    return merged;
}


void GCodeProcessor::TimeMachine::calculate_time(size_t keep_last_n_blocks, float additional_time, ExtrusionRole target_role, block_handler_t block_handler)
{
    if(!enabled)
        return;
    if(blocks.size() < 2){
        if (additional_time > 0)
            m_additional_time_buffer.emplace_back(target_role, additional_time);
        return ;
    }

    assert(keep_last_n_blocks <= blocks.size());

    AdditionalBuffer additional_buffer = m_additional_time_buffer;
    if(additional_time > 0)
        additional_buffer.emplace_back(target_role, additional_time);
    additional_buffer = merge_adjacent_addtional_time_blocks(additional_buffer);
    // forward_pass
    for (size_t i = 0; i + 1 < blocks.size(); ++i) {
        planner_forward_pass_kernel(blocks[i], blocks[i + 1]);
    }

    // reverse_pass
    for (int i = static_cast<int>(blocks.size()) - 1; i > 0; --i)
        planner_reverse_pass_kernel(blocks[i - 1], blocks[i]);

    recalculate_trapezoids(blocks);

    size_t n_blocks_process = blocks.size() - keep_last_n_blocks;
    size_t additional_buffer_idx = 0;

    for (size_t i = 0; i < n_blocks_process; ++i) {
        const TimeBlock& block = blocks[i];
        float block_time = block.time();

        if(additional_buffer_idx < additional_buffer.size()){
            ExtrusionRole buf_role = additional_buffer[additional_buffer_idx].first;
            float buf_time = additional_buffer[additional_buffer_idx].second;
            bool is_valid_block = (buf_role == ExtrusionRole::erNone) ||
                                  (buf_role == block.role);
            if (is_valid_block){
                block_time += buf_time;
                additional_buffer_idx += 1;
            }
        }

        time += block_time;
        block_handler(block, time);
        gcode_time.cache += block_time;
        //BBS: don't calculate travel of start gcode into travel time
        if (!block.flags.prepare_stage || block.move_type != EMoveType::Travel)
            moves_time[static_cast<size_t>(block.move_type)] += block_time;
        roles_time[static_cast<size_t>(block.role)] += block_time;
        if (block.layer_id >= layers_time.size()) {
            const size_t curr_size = layers_time.size();
            layers_time.resize(block.layer_id);
            for (size_t i = curr_size; i < layers_time.size(); ++i) {
                layers_time[i] = 0.0f;
            }
        }
        layers_time[block.layer_id - 1] += block_time;
        //BBS
        if (block.flags.prepare_stage)
            prepare_time += block_time;

        if(!g1_times_cache.empty() && g1_times_cache.back().id == block.g1_line_id)
            g1_times_cache.back().elapsed_time = time;
        else
            g1_times_cache.push_back({ block.g1_line_id, time });
        // update times for remaining time to printer stop placeholders
        auto it_stop_time = std::lower_bound(stop_times.begin(), stop_times.end(), block.g1_line_id,
            [](const StopTime& t, unsigned int value) { return t.g1_line_id < value; });
        if (it_stop_time != stop_times.end() && it_stop_time->g1_line_id == block.g1_line_id)
            it_stop_time->elapsed_time = time;
    }

    m_additional_time_buffer.clear();
    if(additional_buffer_idx<additional_buffer.size())
        m_additional_time_buffer.insert(m_additional_time_buffer.end(), additional_buffer.begin() + additional_buffer_idx, additional_buffer.end());

    if (keep_last_n_blocks)
        blocks.erase(blocks.begin(), blocks.begin() + n_blocks_process);
    else
        blocks.clear();
}

void GCodeProcessor::TimeProcessor::reset()
{
    extruder_unloaded = true;
    machine_envelope_processing_enabled = false;
    machine_limits = MachineEnvelopeConfig();
    filament_load_times = 0.0f;
    filament_unload_times = 0.0f;
    extruder_change_times = 0.0f;


    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        machines[i].reset();
    }
    machines[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)].enabled = true;
}

void GCodeProcessor::TimeProcessor::post_process(const std::string& filename, std::vector<GCodeProcessorResult::MoveVertex>& moves, std::vector<size_t>& lines_ends, const TimeProcessContext& context)
{
    using namespace ExtruderPreHeating;
    FilePtr in{ boost::nowide::fopen(filename.c_str(), "rb") };
    if (in.f == nullptr)
        throw Slic3r::RuntimeError(std::string("Time estimator post process export failed.\nCannot open file for reading.\n"));

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(":  before process %1%") % filename.c_str();
    // temporary file to contain modified gcode
    std::string filename_in = filename;
    std::string filename_out = filename + ".postprocess";

    FilePtr out{ boost::nowide::fopen(filename_out.c_str(), "wb+") };
    if (out.f == nullptr) {
        throw Slic3r::RuntimeError(std::string("Time estimator post process export failed.\nCannot open file for writing.\n"));
    }

    auto time_in_minutes = [](float time_in_seconds) {
        assert(time_in_seconds >= 0.f);
        return int((time_in_seconds + 0.5f) / 60.0f);
    };

    auto time_in_last_minute = [](float time_in_seconds) {
        assert(time_in_seconds <= 60.0f);
        return time_in_seconds / 60.0f;
    };

    auto format_line_M73_main = [](const std::string& mask, int percent, int time) {
        char line_M73[64];
        sprintf(line_M73, mask.c_str(),
            std::to_string(percent).c_str(),
            std::to_string(time).c_str());
        return std::string(line_M73);
    };

    auto format_line_M73_stop_int = [](const std::string& mask, int time) {
        char line_M73[64];
        sprintf(line_M73, mask.c_str(), std::to_string(time).c_str());
        return std::string(line_M73);
    };

    auto format_line_exhaust_fan_control = [](const std::string& mask, int fan_index, int percent) {
        char line_fan[64] = { 0 };
        sprintf(line_fan, mask.c_str(),
            std::to_string(fan_index).c_str(),
            std::to_string(int((percent / 100.0) * 255)).c_str());
        return std::string(line_fan);
    };

    auto format_time_float = [](float time) {
        return Slic3r::float_to_string_decimal_point(time, 2);
    };

    auto format_line_M73_stop_float = [format_time_float](const std::string& mask, float time) {
        char line_M73[64];
        sprintf(line_M73, mask.c_str(), format_time_float(time).c_str());
        return std::string(line_M73);
    };

    auto format_line_M104 = [&context](int target_temp, int target_extruder = -1, const std::string& comment = std::string()) {
        std::string buffer = "M104";
        if (target_extruder != -1)
            buffer += (" T" + std::to_string(context.physical_extruder_map[target_extruder]));
        buffer += " S" + std::to_string(target_temp) + " N0"; // N0 means the gcode is generated by slicer
        if (!comment.empty())
            buffer += " ;" + comment;
        buffer += '\n';
        return buffer;
    };

    auto format_M73_remain_filament_changes = [](int filament_change_num, int total_filament_change)->std::string{
        char buf[64];
        snprintf(buf, sizeof(buf), "M73 E%d\n", total_filament_change - filament_change_num);
        return std::string(buf);
    };

    // do not insert gcode into machine start & end gcode
    unsigned int machine_start_gcode_end_line_id = (unsigned int)(-1); // mark the end line of machine start gcode
    unsigned int machine_end_gcode_start_line_id = (unsigned int)(-1); // mark the start line of machine end gcode
    std::vector<std::pair<unsigned int, unsigned int>> skippable_blocks;

    // keeps track of last exported pair <percent, remaining time>
    std::array<std::pair<int, int>, static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count)> last_exported_main;
    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        last_exported_main[i] = { 0, time_in_minutes(machines[i].time) };
    }

    // keeps track of last exported remaining time to next printer stop
    std::array<int, static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count)> last_exported_stop;
    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        last_exported_stop[i] = time_in_minutes(machines[i].time);
    }

    // replace placeholder lines with the proper final value
    // gcode_line is in/out parameter, to reduce expensive memory allocation
    auto process_placeholders = [&](std::string& gcode_line, int line_id) {
        unsigned int extra_lines_count = 0;

        auto format_filament_used_info = [](const std::string& info, std::map<size_t, double>val_per_extruder) {
            auto double_to_fmt_string = [](double num) -> std::string {
                char buf[20];
                sprintf(buf, "%.2f", num);
                return std::string(buf);
            };
            std::string buf = "; " + info + " : ";
            size_t idx = 0;
            for (auto item : val_per_extruder)
                buf += (idx++ == 0 ? double_to_fmt_string(item.second) : "," + double_to_fmt_string(item.second));
            buf += '\n';
            return buf;
        };

        // remove trailing '\n'
        auto line = std::string_view(gcode_line).substr(0, gcode_line.length() - 1);

        std::string ret;
        if (line.length() > 1) {
            line = line.substr(1);
            if (line == reserved_tag(ETags::First_Line_M73_Placeholder) || line == reserved_tag(ETags::Last_Line_M73_Placeholder)) {
                for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
                    const TimeMachine& machine = machines[i];
                    if (machine.enabled) {
                        // export pair <percent, remaining time>
                        ret += format_line_M73_main(machine.line_m73_main_mask.c_str(),
                            (line == reserved_tag(ETags::First_Line_M73_Placeholder)) ? 0 : 100,
                            (line == reserved_tag(ETags::First_Line_M73_Placeholder)) ? time_in_minutes(machine.time) : 0);
                        ++extra_lines_count;

                        // export remaining time to next printer stop
                        if (line == reserved_tag(ETags::First_Line_M73_Placeholder) && !machine.stop_times.empty()) {
                            int to_export_stop = time_in_minutes(machine.stop_times.front().elapsed_time);
                            ret += format_line_M73_stop_int(machine.line_m73_stop_mask.c_str(), to_export_stop);
                            last_exported_stop[i] = to_export_stop;
                            ++extra_lines_count;
                        }
                    }
                }
            }
            else if (line == reserved_tag(ETags::Estimated_Printing_Time_Placeholder)) {
                for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
                    const TimeMachine& machine = machines[i];
                    PrintEstimatedStatistics::ETimeMode mode = static_cast<PrintEstimatedStatistics::ETimeMode>(i);
                    if (mode == PrintEstimatedStatistics::ETimeMode::Normal || machine.enabled) {
                        char buf[128];
                        if (!s_IsBBLPrinter) {
                            // Klipper estimator
                            sprintf(buf, "; estimated printing time (normal mode) = %s\n",
                                get_time_dhms(machine.time).c_str());
                            ret += buf;
                        }
                        else {
                            // BBS estimator
                            sprintf(buf, "; model printing time: %s; total estimated time: %s\n",
                                get_time_dhms(machine.time - machine.prepare_time).c_str(),
                                get_time_dhms(machine.time).c_str());
                            ret += buf;
                        }
                    }
                }
            }
            //BBS: write total layer number
            else if (line == reserved_tag(ETags::Total_Layer_Number_Placeholder)) {
                char buf[128];
                sprintf(buf, "; total layer number: %zd\n", context.total_layer_num);
                ret += buf;
            }
            else if (line == reserved_tag(ETags::Used_Filament_Weight_Placeholder)) {
                std::map<size_t, double>total_weight_per_extruder;
                for (const auto& pair : context.used_filaments.total_volumes_per_filament) {
                    auto filament_id = pair.first;
                    auto volume = pair.second;
                    auto iter = std::find_if(context.filament_lists.begin(), context.filament_lists.end(), [filament_id](const Extruder& filament) { return filament.id() == filament_id; });
                    if (iter == context.filament_lists.end())
                        continue;
                    double weight = volume * iter->filament_density() * 0.001;
                    total_weight_per_extruder[filament_id] += weight;
                }

                ret += format_filament_used_info("total filament weight [g]", total_weight_per_extruder);
            }
            else if (line == reserved_tag(ETags::Used_Filament_Volume_Placeholder)) {
                std::map<size_t, double>total_volume_per_extruder;
                for (const auto &pair : context.used_filaments.total_volumes_per_filament) {
                    auto filament_id = pair.first;
                    auto volume = pair.second;
                    auto iter = std::find_if(context.filament_lists.begin(), context.filament_lists.end(), [filament_id](const Extruder& filament) { return filament.id() == filament_id; });
                    if (iter == context.filament_lists.end())
                        continue;
                    total_volume_per_extruder[filament_id] += volume;
                }
                ret += format_filament_used_info("total filament volume [cm^3]", total_volume_per_extruder);
            }
            else if (line == reserved_tag(ETags::Used_Filament_Length_Placeholder)) {
                std::map<size_t, double>total_length_per_extruder;
                for (const auto &pair : context.used_filaments.total_volumes_per_filament) {
                    auto filament_id = pair.first;
                    auto volume = pair.second;
                    auto iter = std::find_if(context.filament_lists.begin(), context.filament_lists.end(), [filament_id](const Extruder& filament) { return filament.id() == filament_id; });
                    if (iter == context.filament_lists.end())
                        continue;
                    double length = volume / (PI * sqr(0.5 * iter->filament_diameter()));
                    total_length_per_extruder[filament_id] += length;
                }
                ret += format_filament_used_info("total filament length [mm]", total_length_per_extruder);
            }
            else if (line == reserved_tag(ETags::MachineStartGCodeEnd)) {
                machine_start_gcode_end_line_id = line_id;
            }
            else if (line == reserved_tag(ETags::MachineEndGCodeStart)) {
                machine_end_gcode_start_line_id = line_id;
            }
            else if (line == custom_tags(CustomETags::SKIPPABLE_START)){
                skippable_blocks.emplace_back(0,0);
                skippable_blocks.back().first = line_id;
            }
            else if (line == custom_tags(CustomETags::SKIPPABLE_END)){
                skippable_blocks.back().second = line_id;
            }
        }

        if (!ret.empty())
            // Not moving the move operator on purpose, so that the gcode_line allocation will grow and it will not be reallocated after handful of lines are processed.
            gcode_line = ret;
        return std::tuple(!ret.empty(), (extra_lines_count == 0) ? extra_lines_count : extra_lines_count - 1);
    };


    // check for temporary lines
    auto is_temporary_decoration = [](const std::string_view gcode_line) {
        // remove trailing '\n'
        assert(!gcode_line.empty());
        assert(gcode_line.back() == '\n');

        // return true for decorations which are used in processing the gcode but that should not be exported into the final gcode
        // i.e.:
        // bool ret = gcode_line.substr(0, gcode_line.length() - 1) == ";" + Layer_Change_Tag;
        // ...
        // return ret;
        return false;
    };

    // Iterators for the normal and silent cached time estimate entry recently processed, used by process_line_G1.
    auto g1_times_cache_it = Slic3r::reserve_vector<std::vector<TimeMachine::G1LinesCacheItem>::const_iterator>(machines.size());
    for (const auto& machine : machines)
        g1_times_cache_it.emplace_back(machine.g1_times_cache.begin());

    // add lines M73 to exported gcode
    auto process_line_move = [
        // Lambdas, mostly for string formatting, all with an empty capture block.
        time_in_minutes, format_time_float, format_line_M73_main, format_line_M73_stop_int, format_line_M73_stop_float, time_in_last_minute, format_line_exhaust_fan_control,
            &self = std::as_const(*this),
            // Caches, to be modified
            &g1_times_cache_it, &last_exported_main, &last_exported_stop
    ]
    (std::string& gcode_buffer, const size_t g1_lines_counter) {
        unsigned int exported_lines_count = 0;
        for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
            const TimeMachine& machine = self.machines[i];
            if (machine.enabled) {
                // export pair <percent, remaining time>
                // Skip all machine.g1_times_cache below g1_lines_counter.
                auto& it = g1_times_cache_it[i];
                while (it != machine.g1_times_cache.end() && it->id < g1_lines_counter)
                    ++it;
                if (it != machine.g1_times_cache.end() && it->id == g1_lines_counter) {
                    std::pair<int, int> to_export_main = { int(100.0f * it->elapsed_time / machine.time),
                                                            time_in_minutes(machine.time - it->elapsed_time) };

                    if (last_exported_main[i] != to_export_main) {
                        gcode_buffer += format_line_M73_main(machine.line_m73_main_mask.c_str(),
                            to_export_main.first, to_export_main.second);
                        last_exported_main[i] = to_export_main;
                        ++exported_lines_count;
                    }
                    // export remaining time to next printer stop
                    auto it_stop = std::upper_bound(machine.stop_times.begin(), machine.stop_times.end(), it->elapsed_time,
                        [](float value, const TimeMachine::StopTime& t) { return value < t.elapsed_time; });
                    if (it_stop != machine.stop_times.end()) {
                        int to_export_stop = time_in_minutes(it_stop->elapsed_time - it->elapsed_time);
                        if (last_exported_stop[i] != to_export_stop) {
                            if (to_export_stop > 0) {
                                if (last_exported_stop[i] != to_export_stop) {
                                    gcode_buffer += format_line_M73_stop_int(machine.line_m73_stop_mask.c_str(), to_export_stop);
                                    last_exported_stop[i] = to_export_stop;
                                    ++exported_lines_count;
                                }
                            }
                            else {
                                bool is_last = false;
                                auto next_it = it + 1;
                                is_last |= (next_it == machine.g1_times_cache.end());

                                if (next_it != machine.g1_times_cache.end()) {
                                    auto next_it_stop = std::upper_bound(machine.stop_times.begin(), machine.stop_times.end(), next_it->elapsed_time,
                                        [](float value, const TimeMachine::StopTime& t) { return value < t.elapsed_time; });
                                    is_last |= (next_it_stop != it_stop);

                                    std::string time_float_str = format_time_float(time_in_last_minute(it_stop->elapsed_time - it->elapsed_time));
                                    std::string next_time_float_str = format_time_float(time_in_last_minute(it_stop->elapsed_time - next_it->elapsed_time));
                                    is_last |= (string_to_double_decimal_point(time_float_str) > 0. && string_to_double_decimal_point(next_time_float_str) == 0.);
                                }

                                if (is_last) {
                                    if (std::distance(machine.stop_times.begin(), it_stop) == static_cast<ptrdiff_t>(machine.stop_times.size() - 1))
                                        gcode_buffer += format_line_M73_stop_int(machine.line_m73_stop_mask.c_str(), to_export_stop);
                                    else
                                        gcode_buffer += format_line_M73_stop_float(machine.line_m73_stop_mask.c_str(), time_in_last_minute(it_stop->elapsed_time - it->elapsed_time));

                                    last_exported_stop[i] = to_export_stop;
                                    ++exported_lines_count;
                                }
                            }
                        }
                    }
                }
            }
        }
        return exported_lines_count;
    };

    // helper function to write to disk
    auto write_string = [](const std::string& file_path, FilePtr& out, std::string& str, size_t& out_file_pos, std::vector<size_t>* line_ends = nullptr) {
        fwrite((const void*)str.c_str(), 1, str.length(), out.f);
        if (ferror(out.f)) {
            out.close();
            boost::nowide::remove(file_path.c_str());
            throw Slic3r::RuntimeError(std::string("Time estimator post process export failed.\nIs the disk full?\n"));
        }
        if (line_ends != nullptr) {
            for (size_t idx = 0; idx < str.size(); ++idx) {
                if (str[idx] == '\n')
                    line_ends->emplace_back(out_file_pos + idx + 1);
            }
        }
        out_file_pos += str.size();
        str.clear();
        };

    /*
        Read the file according to the buffer block size, read one line of gcode from the buffer each time and process it.
        Write to a new file when the buffer is full.
        The callback function accepts the gcode content, line number, and buffer content
    */
    auto gcode_process = [&write_string](FilePtr& in, FilePtr& out, const std::string& filename_in, const std::string& filename_out, const std::function<void(std::string&, std::string&, int& line_id)>& gcode_line_handler, std::vector<size_t>* line_ends = nullptr, int buffer_size_in_kB = 64) {
        // Read the input stream 64kB at a time, extract lines and process them.
        std::vector<char> buffer(buffer_size_in_kB * 1024 * 1.5, 0);
        std::string export_line;
        std::string gcode_line;
        int line_id = 0;
        size_t out_file_pos = 0;
        // Line buffer.
        for (;;) {
            size_t cnt_read = ::fread(buffer.data(), 1, buffer.size(), in.f);
            if (::ferror(in.f))
                throw Slic3r::RuntimeError(std::string("Time estimator post process export failed.\nError while reading from file.\n"));
            bool eof = cnt_read == 0;
            auto it = buffer.begin();
            auto it_bufend = buffer.begin() + cnt_read;
            while (it != it_bufend || (eof && !gcode_line.empty())) {
                // Find end of line.
                bool eol = false;
                auto it_end = it;
                for (; it_end != it_bufend && !(eol = *it_end == '\r' || *it_end == '\n'); ++it_end);
                // End of line is indicated also if end of file was reached.
                eol |= eof && it_end == it_bufend;
                gcode_line.insert(gcode_line.end(), it, it_end);
                if (eol) {
                    ++line_id;
                    gcode_line += "\n";
                    gcode_line_handler(gcode_line, export_line, line_id);
                    export_line += gcode_line;
                    if (export_line.length() >= buffer_size_in_kB * 1024)
                        write_string(filename_out, out, export_line, out_file_pos, line_ends);
                    gcode_line.clear();
                }
                // Skip EOL.
                it = it_end;
                if (it != it_bufend && *it == '\r')
                    ++it;
                if (it != it_bufend && *it == '\n')
                    ++it;
            }
            if (eof)
                break;
        }
        if (!export_line.empty())
            write_string(filename_out, out, export_line, out_file_pos, line_ends);
        };

    auto handle_nozzle_change_line = [&filament_maps=context.filament_maps](const std::string& line, int& old_filament, int& next_filament, int& extruder_id)->bool {
        std::regex re(R"(OF(\d+)\s+NF(\d+))");
        std::smatch match;

        if (!std::regex_search(line, match, re))
            return false;

        old_filament = std::stoi(match[1]);
        next_filament = std::stoi(match[2]);
        extruder_id = filament_maps[next_filament];
        return true;
    };

    constexpr int buffer_size_in_KB = 64;
    std::vector<FilamentUsageBlock> filament_blocks;
    std::vector<ExtruderUsageBlcok> extruder_blocks = { ExtruderUsageBlcok() }; // the first use of extruder will not generate nozzle change tag, so manually add a dummy block
    std::vector<std::pair<unsigned int, unsigned int>> offsets;
    size_t g1_lines_counter = 0;
    lines_ends.clear();
    ExtruderUsageBlcok temp_construct_block; // the temperarily constructed block, will be pushed into container after initializing

    auto handle_filament_change = [&filament_blocks,&machine_start_gcode_end_line_id,&machine_end_gcode_start_line_id](int filament_id,int line_id){
        // skip the filaments change in machine start/end gcode
        if (machine_start_gcode_end_line_id == (unsigned int)(-1) && (unsigned int)(line_id)<machine_start_gcode_end_line_id ||
            machine_end_gcode_start_line_id != (unsigned int)(-1) && (unsigned int)(line_id)>machine_end_gcode_start_line_id)
            return;

        if (!filament_blocks.empty())
            filament_blocks.back().upper_gcode_id = line_id;
        filament_blocks.emplace_back(filament_id, line_id, -1);
        };

    auto gcode_time_handler = [&temp_construct_block,&filament_blocks, &extruder_blocks, &offsets, &handle_nozzle_change_line , & process_placeholders, &is_temporary_decoration, & process_line_move, & g1_lines_counter, & machine_start_gcode_end_line_id, & machine_end_gcode_start_line_id,handle_filament_change](std::string& gcode_line, std::string& gcode_buffer, int line_id) {
        auto [processed, lines_added_count] = process_placeholders(gcode_line,line_id);
        if (processed && lines_added_count > 0)
            offsets.push_back({ line_id, lines_added_count });
        if (!processed && !is_temporary_decoration(gcode_line)) {
            if (GCodeReader::GCodeLine::cmd_is(gcode_line, "G1") ||
                GCodeReader::GCodeLine::cmd_is(gcode_line, "G2") ||
                GCodeReader::GCodeLine::cmd_is(gcode_line, "G3") ||
                GCodeReader::GCodeLine::cmd_start_with(gcode_line, ";VG1")
                ) {
                // remove temporary lines, add lines M73 where needed
                unsigned int extra_lines_count = process_line_move(gcode_buffer, g1_lines_counter++);
                if (extra_lines_count > 0)
                    offsets.push_back({ line_id, extra_lines_count });
            }
            else if (GCodeReader::GCodeLine::cmd_start_with(gcode_line, "T")) {
                int fid;
                int skips = GCodeReader::skip_whitespaces(gcode_line.data()) - gcode_line.data();
                std::istringstream str(gcode_line.substr(skips + 1)); // skip white spaces and T
                str >> fid;
                if (!str.fail() && 0 <= fid && fid < 255) {
                    handle_filament_change(fid, line_id);
                }
            }
            else if (GCodeReader::GCodeLine::cmd_start_with(gcode_line, ";VT")) {
                int fid;
                int skips = GCodeReader::skip_whitespaces(gcode_line.data()) - gcode_line.data();
                std::istringstream str(gcode_line.substr(skips + 3)); // skip white spaces and ;VT
                str >> fid;
                if (!str.fail() && 0 <= fid && fid < 255) {
                    handle_filament_change(fid, line_id);
                }
            }
            else if (GCodeReader::GCodeLine::cmd_start_with(gcode_line, "M1020")) {
                size_t s_pos = gcode_line.find('S');
                if (s_pos != std::string::npos) {
                    std::istringstream str(gcode_line.substr(s_pos + 1)); // skip white spaces and T
                    int fid;
                    str >> fid;
                    if (!str.fail() && 0 <= fid && fid < 255) {
                        handle_filament_change(fid, line_id);
                    }
                }
            }
            else if (GCodeReader::GCodeLine::cmd_start_with(gcode_line, (std::string(";") + reserved_tag(ETags::NozzleChangeStart)).c_str())) {
                int prev_filament{ -1 }, next_filament{ -1 }, extruder_id{ -1 };
                handle_nozzle_change_line(gcode_line, prev_filament, next_filament, extruder_id);
                if (!extruder_blocks.empty()) {
                    extruder_blocks.back().initialize_step_2(line_id);
                }
            }
            else if (GCodeReader::GCodeLine::cmd_start_with(gcode_line, (std::string(";") + reserved_tag(ETags::NozzleChangeEnd)).c_str())) {
                int prev_filament{ -1 }, next_filament{ -1 }, extruder_id{ -1 };
                handle_nozzle_change_line(gcode_line, prev_filament, next_filament, extruder_id);
                if (!extruder_blocks.empty()) {
                    extruder_blocks.back().initialize_step_3(line_id, prev_filament, line_id);
                }
                temp_construct_block.initialize_step_1(extruder_id, line_id, next_filament);
                extruder_blocks.emplace_back(temp_construct_block);
                temp_construct_block.reset();
            }
        }
        };

    // we don't need to get line ends here because it's not the final end
    gcode_process(in, out, filename_in, filename_out, gcode_time_handler, nullptr, buffer_size_in_KB);

    // updates moves' gcode ids which have been modified by the insertion of the M73 lines
    handle_offsets_of_first_process(offsets, moves, filament_blocks, extruder_blocks, skippable_blocks, machine_start_gcode_end_line_id, machine_end_gcode_start_line_id);

    // If not initialized, use the time from the previous move.
    {
        std::optional<decltype(moves.begin())>iter;
        for (auto niter = moves.begin(); niter != moves.end(); ++niter) {
            if (!iter) {
                iter = niter;
                continue;
            }
            if (niter->time[0] == 0 && niter->time[1] == 0) {
                niter->time[0] = (*iter)->time[0];
                niter->time[1] = (*iter)->time[1];
            }
            ++(*iter);
        }
    }

    // stores then strings to be inserted. first key is line id ,second key is content
    InsertedLinesMap inserted_operation_lines;

    // save filament change block by extruder id
    std::unordered_map<int, std::vector<ExtruderUsageBlcok>> extruder_change_info;

    // collect the position to insert remaining filament changes
    {
        int curr_filament = -1;
        int total_filament_count = 0;
        for (const auto& fb : filament_blocks) {
            if (curr_filament != -1 && curr_filament != fb.filament_id)
                total_filament_count += 1;
            curr_filament = fb.filament_id;
        }
        curr_filament = -1;
        int curr_filament_change_num = 0;
        for (const auto& fb : filament_blocks) {
            int extruder_id = context.filament_maps[fb.filament_id];
            if (curr_filament != -1 && curr_filament != fb.filament_id) {
                curr_filament_change_num += 1;
                inserted_operation_lines[fb.lower_gcode_id].emplace_back(format_M73_remain_filament_changes(curr_filament_change_num, total_filament_count), InsertLineType::FilamentChangePredict);
            }
            curr_filament = fb.filament_id;
        }
    }

    if (!filament_blocks.empty()) {
        filament_blocks.back().upper_gcode_id = machine_end_gcode_start_line_id;
    }

    // After traversing the G-code, the first and last extruder blocks still have uncompleted initialization steps
    if (!extruder_blocks.empty()) {
        int first_filament = 0;
        int last_filament = 0;

        if (!filament_blocks.empty()) {
            first_filament = filament_blocks.front().filament_id;
            last_filament = filament_blocks.back().filament_id;
        }
        extruder_blocks.front().initialize_step_1(context.filament_maps[first_filament], machine_start_gcode_end_line_id, first_filament);
        extruder_blocks.back().initialize_step_2(machine_end_gcode_start_line_id);
        extruder_blocks.back().initialize_step_3(machine_end_gcode_start_line_id,last_filament,machine_end_gcode_start_line_id);
    }

    for (auto& block : extruder_blocks)
        extruder_change_info[block.extruder_id].emplace_back(block);

    if (context.enable_pre_heating) {
        // get the real speed mode used in slicing
        size_t valid_machine_id = 0;
        for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
            if (machines[i].enabled) {
                valid_machine_id = i;
                break;
            }
        }

        auto pre_cooling_injector = std::make_unique<PreCoolingInjector>(
            moves,
            context.filament_types,
            context.filament_maps,
            context.filament_nozzle_temp,
            context.physical_extruder_map,
            valid_machine_id,
            context.inject_time_threshold,
            context.pre_cooling_temp,
            context.cooling_rate,
            context.heating_rate,
            skippable_blocks,
            machine_start_gcode_end_line_id,
            machine_end_gcode_start_line_id
        );

        pre_cooling_injector->build_extruder_free_blocks(filament_blocks, extruder_blocks);
        pre_cooling_injector->process_pre_cooling_and_heating(inserted_operation_lines);
    }

    auto pre_operation_iter = inserted_operation_lines.begin();
    auto filament_change_handle = [&inserted_operation_lines, &pre_operation_iter,enable_pre_heating = context.enable_pre_heating](std::string& gcode_line, std::string& gcode_buffer, int line_id) {
        if (pre_operation_iter == inserted_operation_lines.end())
            return;
        if (line_id == pre_operation_iter->first) {
            for (auto& elem : pre_operation_iter->second) {
                const std::string& str = elem.first;
                const InsertLineType type = elem.second;
                switch (type)
                {
                    case InsertLineType::PlaceholderReplace:
                    case InsertLineType::TimePredict: break;  // these types above has been handled before
                    case InsertLineType::PreCooling:
                    case InsertLineType::PreHeating:
                    {
                        if (enable_pre_heating)
                            gcode_line += str;
                        break;
                    }
                    case InsertLineType::ExtruderChangePredict: break;
                    case InsertLineType::FilamentChangePredict:
                    {
                        gcode_line += str;
                        break;
                    }
                    default:
                        break;
                }
            }
            ++pre_operation_iter;
        }
    };

    filename_in = filename_out; // filename_out is opened in read|write mode. During second process ,we ues filename_out as input
    filename_out = filename + ".postprocessed";

    FilePtr new_out = boost::nowide::fopen(filename_out.c_str(), "wb");
    std::fseek(out.f, 0, SEEK_SET); // move to start of the file and start reading gcode as in

    gcode_process(out, new_out, filename_in, filename_out, filament_change_handle, &lines_ends, buffer_size_in_KB);
    new_out.close();

    // recollect gcode offset caused by inserted operations
    handle_offsets_of_second_process(inserted_operation_lines, moves);

    in.close();
    out.close();

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(":  after process %1%") % filename.c_str();

    if (boost::nowide::remove(filename_in.c_str()) != 0) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(":  Failed to remove the temporary G-code file %1%") % filename_in.c_str();
        throw Slic3r::RuntimeError(std::string("Failed to remove the temporary G-code file ") + filename_in + '\n' +
            "Is " + filename_in + " locked?" + '\n');
    }
    if (rename_file(filename_out, filename)) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(":  Failed to rename the output G-code file from %1% to %2%") % filename_out.c_str() % filename.c_str();
        throw Slic3r::RuntimeError(std::string("Failed to rename the output G-code file from ") + filename_out + " to " + filename + '\n' +
            "Is " + filename_out + " locked?" + '\n');
    }
}

void GCodeProcessor::TimeProcessor::handle_offsets_of_first_process(
    const std::vector<std::pair<unsigned int, unsigned int>>& offsets,
    std::vector<GCodeProcessorResult::MoveVertex>& moves,
    std::vector<ExtruderPreHeating::FilamentUsageBlock>& filament_blocks,
    std::vector<ExtruderPreHeating::ExtruderUsageBlcok>& extruder_blocks,
    std::vector<std::pair<unsigned int, unsigned int>>& skippable_blocks,
    unsigned int& machine_start_gcode_end_line_id,
    unsigned int& machine_end_gcode_start_line_id)
{
    // process moves
    {
        unsigned int curr_offset_id = 0, total_offset = 0;
        for (GCodeProcessorResult::MoveVertex& move : moves) {
            while (curr_offset_id < static_cast<unsigned int>(offsets.size()) && offsets[curr_offset_id].first <= move.gcode_id) {
                total_offset += offsets[curr_offset_id].second;
                ++curr_offset_id;
            }
            move.gcode_id += total_offset;
        }
    }

    std::vector<unsigned int> offset_line_id(offsets.size());
    std::vector<unsigned int> prefix_sum_offset(offsets.size());
    unsigned int sum = 0;
    for(size_t idx =0;idx<offsets.size();++idx){
        sum += offsets[idx].second;
        offset_line_id[idx] = offsets[idx].first;
        prefix_sum_offset[idx] = sum;

    }
    auto get_offset_before_line_id = [&offset_line_id, &prefix_sum_offset](unsigned int line_id)->unsigned int {
        if (line_id == (unsigned int)(-1))
            return 0;
        auto it = std::upper_bound(offset_line_id.begin(), offset_line_id.end(), line_id);
        if (it == offset_line_id.begin())
            return 0;
        size_t index = std::distance(offset_line_id.begin(), it) - 1;
        return prefix_sum_offset[index];
        };

    for (ExtruderPreHeating::FilamentUsageBlock& block : filament_blocks) {
        block.lower_gcode_id += get_offset_before_line_id(block.lower_gcode_id);
        block.upper_gcode_id += get_offset_before_line_id(block.upper_gcode_id);
    }
    for (ExtruderPreHeating::ExtruderUsageBlcok& block : extruder_blocks) {
        block.start_id += get_offset_before_line_id(block.start_id);
        block.end_id += get_offset_before_line_id(block.end_id);
        block.post_extrusion_start_id += get_offset_before_line_id(block.post_extrusion_start_id);
        block.post_extrusion_end_id += get_offset_before_line_id(block.post_extrusion_end_id);
    }

    for(auto& block: skippable_blocks){
        block.first += get_offset_before_line_id(block.first);
        block.second += get_offset_before_line_id(block.second);
    }

    machine_start_gcode_end_line_id += get_offset_before_line_id(machine_start_gcode_end_line_id);
    machine_end_gcode_start_line_id += get_offset_before_line_id(machine_end_gcode_start_line_id);
}

void GCodeProcessor::TimeProcessor::handle_offsets_of_second_process(const InsertedLinesMap& inserted_operation_lines, std::vector<GCodeProcessorResult::MoveVertex>& moves)
{
    int total_offset = 0;
    auto iter = inserted_operation_lines.begin();
    for (GCodeProcessorResult::MoveVertex& move : moves) {
        while (iter != inserted_operation_lines.end() && iter->first < move.gcode_id) {
            total_offset += iter->second.size();
            ++iter;
        }
        move.gcode_id += total_offset;
    }
}

void GCodeProcessor::UsedFilaments::reset()
{
    color_change_cache = 0.0f;
    volumes_per_color_change = std::vector<double>();

    model_extrude_cache = 0.0f;
    model_volumes_per_filament.clear();

    flush_per_filament.clear();

    role_cache = 0.0f;
    filaments_per_role.clear();

    wipe_tower_cache = 0.0f;
    wipe_tower_volumes_per_filament.clear();

    support_volume_cache = 0.0f;
    support_volumes_per_filament.clear();

    total_volume_cache = 0.0f;
    total_volumes_per_filament.clear();
}

void GCodeProcessor::UsedFilaments::increase_support_caches(double extruded_volume)
{
    support_volume_cache += extruded_volume;
    role_cache += extruded_volume;
    total_volume_cache += extruded_volume;
}

void GCodeProcessor::UsedFilaments::increase_model_caches(double extruded_volume)
{
    color_change_cache += extruded_volume;
    model_extrude_cache += extruded_volume;
    role_cache += extruded_volume;
    total_volume_cache += extruded_volume;
}

void GCodeProcessor::UsedFilaments::increase_wipe_tower_caches(double extruded_volume)
{
    wipe_tower_cache += extruded_volume;
    role_cache += extruded_volume;
    total_volume_cache += extruded_volume;
}

void GCodeProcessor::UsedFilaments::process_color_change_cache()
{
    if (color_change_cache != 0.0f) {
        volumes_per_color_change.push_back(color_change_cache);
        color_change_cache = 0.0f;
    }
}


void GCodeProcessor::UsedFilaments::process_total_volume_cache(GCodeProcessor* processor)
{
    size_t active_filament_id = processor->get_filament_id();
    if (total_volume_cache!= 0.0f) {
        if (total_volumes_per_filament.find(active_filament_id) != total_volumes_per_filament.end())
            total_volumes_per_filament[active_filament_id] += total_volume_cache;
        else
            total_volumes_per_filament[active_filament_id] = total_volume_cache;
        total_volume_cache = 0.0f;
    }
}

void GCodeProcessor::UsedFilaments::process_model_cache(GCodeProcessor* processor)
{
    size_t active_filament_id = processor->get_filament_id();
    if (model_extrude_cache != 0.0f) {
        if (model_volumes_per_filament.find(active_filament_id) != model_volumes_per_filament.end())
            model_volumes_per_filament[active_filament_id] += model_extrude_cache;
        else
            model_volumes_per_filament[active_filament_id] = model_extrude_cache;
        model_extrude_cache = 0.0f;
    }
}

void GCodeProcessor::UsedFilaments::process_wipe_tower_cache(GCodeProcessor* processor)
{
    size_t active_filament_id = processor->get_filament_id();
    if (wipe_tower_cache != 0.0f) {
        if (wipe_tower_volumes_per_filament.find(active_filament_id) != wipe_tower_volumes_per_filament.end())
            wipe_tower_volumes_per_filament[active_filament_id] += wipe_tower_cache;
        else
            wipe_tower_volumes_per_filament[active_filament_id] = wipe_tower_cache;
        wipe_tower_cache = 0.0f;
    }
}

void GCodeProcessor::UsedFilaments::process_support_cache(GCodeProcessor* processor)
{
    size_t active_filament_id = processor->get_filament_id();
    if (support_volume_cache != 0.0f){
        if (support_volumes_per_filament.find(active_filament_id) != support_volumes_per_filament.end())
            support_volumes_per_filament[active_filament_id] += support_volume_cache;
        else
            support_volumes_per_filament[active_filament_id] = support_volume_cache;
        support_volume_cache = 0.0f;
    }
}

void GCodeProcessor::UsedFilaments::update_flush_per_filament(size_t filament_id, float flush_volume)
{
    if (flush_volume != 0.f) {
        role_cache += flush_volume;
        if (flush_per_filament.find(filament_id) != flush_per_filament.end())
            flush_per_filament[filament_id] += flush_volume;
        else
            flush_per_filament[filament_id] = flush_volume;

        if (total_volumes_per_filament.find(filament_id) != total_volumes_per_filament.end())
            total_volumes_per_filament[filament_id] += flush_volume;
        else
            total_volumes_per_filament[filament_id] = flush_volume;
    }
}

void GCodeProcessor::UsedFilaments::process_role_cache(GCodeProcessor* processor)
{
    if (role_cache != 0.0f) {
        std::pair<double, double> filament = { 0.0f, 0.0f };

        double s = PI * sqr(0.5 * processor->m_result.filament_diameters[processor->get_filament_id()]);
        filament.first = role_cache / s * 0.001;
        filament.second = role_cache * processor->m_result.filament_densities[processor->get_filament_id()] * 0.001;

        ExtrusionRole active_role = processor->m_extrusion_role;
        if (filaments_per_role.find(active_role) != filaments_per_role.end()) {
            filaments_per_role[active_role].first += filament.first;
            filaments_per_role[active_role].second += filament.second;
        }
        else
            filaments_per_role[active_role] = filament;
        role_cache = 0.0f;
    }
}

void GCodeProcessor::UsedFilaments::process_caches(GCodeProcessor* processor)
{
    process_color_change_cache();
    process_model_cache(processor);
    process_role_cache(processor);
    process_wipe_tower_cache(processor);
    process_support_cache(processor);
    process_total_volume_cache(processor);
}

#if ENABLE_GCODE_VIEWER_STATISTICS
void GCodeProcessorResult::reset() {
    //BBS: add mutex for protection of gcode result
    lock();

    moves = std::vector<GCodeProcessorResult::MoveVertex>();
    printable_area = Pointfs();
    //BBS: add bed exclude area
    bed_exclude_area = Pointfs();
    //BBS: add toolpath_outside
    toolpath_outside = false;
    //BBS: add label_object_enabled
    label_object_enabled = false;
    timelapse_warning_code = 0;
    printable_height = 0.0f;
    settings_ids.reset();
    extruders_count = 0;
    extruder_colors = std::vector<std::string>();
    filament_diameters = std::vector<float>(MIN_EXTRUDERS_COUNT, DEFAULT_FILAMENT_DIAMETER);
    filament_densities = std::vector<float>(MIN_EXTRUDERS_COUNT, DEFAULT_FILAMENT_DENSITY);
    custom_gcode_per_print_z = std::vector<CustomGCode::Item>();
    spiral_vase_layers = std::vector<std::pair<float, std::pair<size_t, size_t>>>();
    time = 0;

    //BBS: add mutex for protection of gcode result
    unlock();
}
#else
void GCodeProcessorResult::reset() {
    //BBS: add mutex for protection of gcode result
    lock();

    moves.clear();
    lines_ends.clear();
    printable_area = Pointfs();
    //BBS: add bed exclude area
    bed_exclude_area = Pointfs();
    //BBS: add toolpath_outside
    toolpath_outside = false;
    //BBS: add label_object_enabled
    label_object_enabled = false;
    long_retraction_when_cut = false;
    timelapse_warning_code = 0;
    printable_height = 0.0f;
    settings_ids.reset();
    filaments_count = 0;
    extruder_colors = std::vector<std::string>();
    filament_diameters = std::vector<float>(MIN_EXTRUDERS_COUNT, DEFAULT_FILAMENT_DIAMETER);
    required_nozzle_HRC = std::vector<int>(MIN_EXTRUDERS_COUNT, DEFAULT_FILAMENT_HRC);
    filament_densities = std::vector<float>(MIN_EXTRUDERS_COUNT, DEFAULT_FILAMENT_DENSITY);
    filament_costs = std::vector<float>(MIN_EXTRUDERS_COUNT, DEFAULT_FILAMENT_COST);
    custom_gcode_per_print_z = std::vector<CustomGCode::Item>();
    spiral_vase_layers = std::vector<std::pair<float, std::pair<size_t, size_t>>>();
    layer_filaments.clear();
    filament_change_count_map.clear();
    skippable_part_time.clear();
    warnings.clear();

    //BBS: add mutex for protection of gcode result
    unlock();
    //BBS: add logs
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(" %1%: this=%2% reset finished")%__LINE__%this;
}
#endif // ENABLE_GCODE_VIEWER_STATISTICS

const std::vector<std::pair<GCodeProcessor::EProducer, std::string>> GCodeProcessor::Producers = {
    //BBS: BambuStudio is also "bambu". Otherwise the time estimation didn't work.
    //FIXME: Workaround and should be handled when do removing-bambu
    { EProducer::BambuStudio, SLIC3R_APP_NAME },
    { EProducer::BambuStudio, "generated by BambuStudio" }
    //{ EProducer::Slic3rPE,    "generated by Slic3r Bambu Edition" },
    //{ EProducer::Slic3r,      "generated by Slic3r" },
    //{ EProducer::SuperSlicer, "generated by SuperSlicer" },
    //{ EProducer::Cura,        "Cura_SteamEngine" },
    //{ EProducer::Simplify3D,  "G-Code generated by Simplify3D(R)" },
    //{ EProducer::CraftWare,   "CraftWare" },
    //{ EProducer::ideaMaker,   "ideaMaker" },
    //{ EProducer::KissSlicer,  "KISSlicer" }
};

unsigned int GCodeProcessor::s_result_id = 0;

bool GCodeProcessor::contains_reserved_tag(const std::string& gcode, std::string& found_tag)
{
    bool ret = false;

    GCodeReader parser;
    parser.parse_buffer(gcode, [&ret, &found_tag](GCodeReader& parser, const GCodeReader::GCodeLine& line) {
        std::string comment = line.raw();
        if (comment.length() > 2 && comment.front() == ';') {
            comment = comment.substr(1);
            for (const std::string& s : ReservedTags) {
                if (boost::starts_with(comment, s)) {
                    ret = true;
                    found_tag = comment;
                    parser.quit_parsing();
                    return;
                }
            }
        }
        });

    return ret;
}

bool GCodeProcessor::contains_reserved_tags(const std::string& gcode, unsigned int max_count, std::vector<std::string>& found_tag)
{
    max_count = std::max(max_count, 1U);

    bool ret = false;

    CNumericLocalesSetter locales_setter;

    GCodeReader parser;
    parser.parse_buffer(gcode, [&ret, &found_tag, max_count](GCodeReader& parser, const GCodeReader::GCodeLine& line) {
        std::string comment = line.raw();
        if (comment.length() > 2 && comment.front() == ';') {
            comment = comment.substr(1);
            for (const std::string& s : ReservedTags) {
                if (boost::starts_with(comment, s)) {
                    ret = true;
                    found_tag.push_back(comment);
                    if (found_tag.size() == max_count) {
                        parser.quit_parsing();
                        return;
                    }
                }
            }
        }
        });

    return ret;
}

GCodeProcessor::GCodeProcessor()
: m_options_z_corrector(m_result)
{
    reset();
    m_time_processor.machines[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)].line_m73_main_mask = "M73 P%s R%s\n";
    m_time_processor.machines[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)].line_m73_stop_mask = "M73 C%s\n";
    m_time_processor.machines[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Stealth)].line_m73_main_mask = "M73 Q%s S%s\n";
    m_time_processor.machines[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Stealth)].line_m73_stop_mask = "M73 D%s\n";

    register_commands();
}

void GCodeProcessor::register_commands()
{
    // !!! registered command must be upper case
    std::unordered_map<std::string, CommandProcessor::command_handler_t> command_handler_list = {
        {"G0", [this](const GCodeReader::GCodeLine& line) { process_G0(line); }}, // Move
        {"G1", [this](const GCodeReader::GCodeLine& line) { process_G1(line); }}, // Move
        {"G2", [this](const GCodeReader::GCodeLine& line) { process_G2_G3(line); }}, // Move
        {"G3", [this](const GCodeReader::GCodeLine& line) { process_G2_G3(line); }}, // Move
        {"G4", [this](const GCodeReader::GCodeLine& line) { process_G4(line); }}, // Delay

        {"G10", [this](const GCodeReader::GCodeLine& line) { process_G10(line); }}, // Retract
        {"G11", [this](const GCodeReader::GCodeLine& line) { process_G11(line); }}, // Unretract

        {"G20", [this](const GCodeReader::GCodeLine& line) { process_G20(line); }}, // Set Units to Inches
        {"G21", [this](const GCodeReader::GCodeLine& line) { process_G21(line); }}, // Set Units to Millimeters
        {"G22", [this](const GCodeReader::GCodeLine& line) { process_G22(line); }}, // Firmware controlled retract
        {"G23", [this](const GCodeReader::GCodeLine& line) { process_G23(line); }}, // Firmware controlled unretract
        {"G28", [this](const GCodeReader::GCodeLine& line) { process_G28(line); }}, // Move to origin
        {"G29", [this](const GCodeReader::GCodeLine& line) { process_G29(line); }},

        {"G90", [this](const GCodeReader::GCodeLine& line) { process_G90(line); }}, // Set to Absolute Positioning
        {"G91", [this](const GCodeReader::GCodeLine& line) { process_G91(line); }}, // Set to Relative Positioning
        {"G92", [this](const GCodeReader::GCodeLine& line) { process_G92(line); }}, // Set Position

        {"M1", [this](const GCodeReader::GCodeLine& line) { process_M1(line); }}, // Sleep or Conditional stop

        {"M82", [this](const GCodeReader::GCodeLine& line) { process_M82(line); }}, // Set extruder to absolute mode
        {"M83", [this](const GCodeReader::GCodeLine& line) { process_M83(line); }}, // Set extruder to relative mode

        {"M104", [this](const GCodeReader::GCodeLine& line) { process_M104(line); }}, // Set extruder temperature
        {"M106", [this](const GCodeReader::GCodeLine& line) { process_M106(line); }}, // Set fan speed
        {"M107", [this](const GCodeReader::GCodeLine& line) { process_M107(line); }}, // Disable fan
        {"M108", [this](const GCodeReader::GCodeLine& line) { process_M108(line); }}, // Set tool (Sailfish)
        {"M109", [this](const GCodeReader::GCodeLine& line) { process_M109(line); }}, // Set extruder temperature and wait

        {"M132", [this](const GCodeReader::GCodeLine& line) { process_M132(line); }}, // Recall stored home offsets
        {"M135", [this](const GCodeReader::GCodeLine& line) { process_M135(line); }}, // Set tool (MakerWare)

        {"M140", [this](const GCodeReader::GCodeLine& line) { process_M140(line); }}, // Set bed temperature
        {"M190", [this](const GCodeReader::GCodeLine& line) { process_M190(line); }}, // Wait bed temperature
        {"M191", [this](const GCodeReader::GCodeLine& line) { process_M191(line); }}, // Wait chamber temperature

        {"M201", [this](const GCodeReader::GCodeLine& line) { process_M201(line); }}, // Set max printing acceleration
        {"M203", [this](const GCodeReader::GCodeLine& line) { process_M203(line); }}, // Set maximum feedrate
        {"M204", [this](const GCodeReader::GCodeLine& line) { process_M204(line); }}, // Set default acceleration
        {"M205", [this](const GCodeReader::GCodeLine& line) { process_M205(line); }}, // Advanced settings
        {"M221", [this](const GCodeReader::GCodeLine& line) { process_M221(line); }}, // Set extrude factor override percentage

        {"M400", [this](const GCodeReader::GCodeLine& line) { process_M400(line); }}, // BBS delay
        {"M401", [this](const GCodeReader::GCodeLine& line) { process_M401(line); }}, // Repetier: Store x, y and z position
        {"M402", [this](const GCodeReader::GCodeLine& line) { process_M402(line); }}, // Repetier: Go to stored position
        {"M566", [this](const GCodeReader::GCodeLine& line) { process_M566(line); }}, // Set allowable instantaneous speed change
        {"M702", [this](const GCodeReader::GCodeLine& line) { process_M702(line); }}, // Unload the current filament into the MK3 MMU2 unit at the end of print.
        {"M1020", [this](const GCodeReader::GCodeLine& line) { process_M1020(line); }}, // Select Tool

        {"T", [this](const GCodeReader::GCodeLine& line) { process_T(line); }}, // Select Tool
        {"SYNC", [this](const GCodeReader::GCodeLine& line) { process_SYNC(line); }}, // SYNC TIME

        {"VG1", [this](const GCodeReader::GCodeLine& line) { process_VG1(line); }},
        {"VM104", [this](const GCodeReader::GCodeLine& line) { process_VM104(line); }},
        {"VM109", [this](const GCodeReader::GCodeLine& line) { process_VM109(line); }}
    };

    std::unordered_set<std::string>early_quit_commands = {
        "T"
    };

    auto to_lowercase = [](std::string str)->std::string {
        std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) {
            return std::tolower(c);
            });
        return str;
        };

    for (auto elem : command_handler_list) {
        auto& uppercase_cmd = elem.first;
        auto& handler = elem.second;
        bool early_quit = early_quit_commands.count(uppercase_cmd) > 0;
        m_command_processor.register_command(uppercase_cmd, handler,early_quit);
        if (auto lowercase_cmd = to_lowercase(uppercase_cmd); lowercase_cmd != uppercase_cmd)
            m_command_processor.register_command(lowercase_cmd, handler,early_quit);
    }
}

bool GCodeProcessor::check_multi_extruder_gcode_valid(const std::vector<Polygons>      &unprintable_areas,
                                                      const std::vector<double>        &printable_heights,
                                                      const std::vector<int>           &filament_map,
                                                      const std::vector<std::set<int>> &unprintable_filament_types)
{
    m_result.limit_filament_maps.clear();
    m_result.gcode_check_result.reset();

    m_result.limit_filament_maps.resize(filament_map.size(), 0);

    auto to_2d = [](const Vec3d &pos) -> Point {
        Point ps(scale_(pos.x()), scale_(pos.y()));
        return ps;
    };

    struct GCodePosInfo
    {
        Points pos;
        float max_print_z;
    };
    std::map<int, std::map<int, GCodePosInfo>> gcode_path_pos; // object_id, filament_id, pos
    for (const GCodeProcessorResult::MoveVertex &move : m_result.moves) {
        if (move.type == EMoveType::Extrude/* || move.type == EMoveType::Travel*/) {
            if (move.is_arc_move_with_interpolation_points()) {
                for (int i = 0; i < move.interpolation_points.size(); i++) {
                    gcode_path_pos[move.object_label_id][int(move.extruder_id)].pos.emplace_back(to_2d(move.interpolation_points[i].cast<double>()));
                }
            }
            else {
                gcode_path_pos[move.object_label_id][int(move.extruder_id)].pos.emplace_back(to_2d(move.position.cast<double>()));
            }
            gcode_path_pos[move.object_label_id][int(move.extruder_id)].max_print_z = std::max(gcode_path_pos[move.object_label_id][int(move.extruder_id)].max_print_z, move.print_z);
        }
    }

    bool valid = true;
    Point plate_offset = Point(scale_(m_x_offset), scale_(m_y_offset));
    for (auto obj_iter = gcode_path_pos.begin(); obj_iter != gcode_path_pos.end(); ++obj_iter) {
        int object_label_id = obj_iter->first;
        const std::map<int, GCodePosInfo> &path_pos        = obj_iter->second;
        for (auto iter = path_pos.begin(); iter != path_pos.end(); ++iter) {
            int extruder_id = filament_map[iter->first] - 1;
            Polygon     path_poly(iter->second.pos);
            BoundingBox bbox = path_poly.bounding_box();

            // check printable area
            // Simplified use bounding_box, Accurate calculation is not efficient
            for (Polygon poly : unprintable_areas[extruder_id]) {
                poly.translate(plate_offset);
                if (poly.bounding_box().overlap(bbox)) {
                    m_result.gcode_check_result.error_code = 1;
                    std::pair<int, int>         filament_to_object_id;
                    filament_to_object_id.first = iter->first;
                    filament_to_object_id.second = object_label_id;
                    m_result.gcode_check_result.print_area_error_infos[extruder_id].push_back(filament_to_object_id);
                    valid = false;
                }
            }

            // check printable height
            if (iter->second.max_print_z > printable_heights[extruder_id]) {
                m_result.gcode_check_result.error_code |= (1 << 1);
                std::pair<int, int> filament_to_object_id;
                filament_to_object_id.first  = iter->first;
                filament_to_object_id.second = object_label_id;
                m_result.gcode_check_result.print_height_error_infos[extruder_id].push_back(filament_to_object_id);
                m_result.limit_filament_maps[iter->first] |= (1 << extruder_id);
                valid = false;
            }

            for (int i = 0; i < unprintable_areas.size(); ++i) {
                for (Polygon poly : unprintable_areas[i]) {
                    poly.translate(plate_offset);
                    if (!poly.bounding_box().overlap(bbox))
                        continue;

                    m_result.limit_filament_maps[iter->first] |= (1 << i);
                }
            }
        }
    }

    // apply unprintable filament type result
    for (int extruder_id = 0; extruder_id < unprintable_filament_types.size(); ++extruder_id) {
        const std::set<int> &filament_ids = unprintable_filament_types[extruder_id];
        for (int filament_id : filament_ids) {
            m_result.limit_filament_maps[filament_id] |= (1 << extruder_id);
        }
    };

    return valid;
}

void GCodeProcessor::apply_config(const PrintConfig& config)
{
    m_parser.apply_config(config);

    m_flavor = config.gcode_flavor;

    // BBS
    size_t filament_count = config.filament_diameter.values.size();
    m_result.filaments_count = filament_count;

    assert(config.nozzle_volume.size() == config.nozzle_diameter.size());
    m_nozzle_volume.resize(config.nozzle_volume.size());
    for (size_t idx = 0; idx < config.nozzle_volume.size(); ++idx)
        m_nozzle_volume[idx] = config.nozzle_volume.values[idx];

    m_filament_nozzle_temp.resize(filament_count);
    for (size_t idx = 0; idx < filament_count; ++idx)
        m_filament_nozzle_temp[idx] = config.nozzle_temperature.get_at(idx);

    m_filament_types.resize(filament_count);
    for (size_t idx = 0; idx < filament_count; ++idx)
        m_filament_types[idx] = config.filament_type.get_at(idx);

    m_hotend_cooling_rate = config.hotend_cooling_rate.values;
    m_hotend_heating_rate = config.hotend_heating_rate.values;
    m_filament_pre_cooling_temp = config.filament_pre_cooling_temperature.values;
    m_enable_pre_heating = config.enable_pre_heating;
    m_physical_extruder_map = config.physical_extruder_map.values;

    m_extruder_offsets.resize(filament_count);
    m_extruder_colors.resize(filament_count);
    m_result.filament_diameters.resize(filament_count);
    m_result.required_nozzle_HRC.resize(filament_count);
    m_result.filament_densities.resize(filament_count);
    m_result.filament_vitrification_temperature.resize(filament_count);
    m_result.filament_costs.resize(filament_count);
    m_extruder_temps.resize(filament_count);
    std::vector<NozzleType>(config.nozzle_type.size()).swap(m_result.nozzle_type);
    for (size_t idx = 0; idx < m_result.nozzle_type.size(); ++idx) {
        m_result.nozzle_type[idx] = NozzleType(config.nozzle_type.values[idx]);
    }

    std::vector<int> filament_map = config.filament_map.values; // 1 based idxs
    // if filament map has wrong length, set filament to master extruder_id
    filament_map.resize(filament_count, config.master_extruder_id.value);

    for (size_t i = 0; i < filament_count; ++ i) {
        m_extruder_offsets[i] = to_3d(config.extruder_offset.get_at(filament_map[i] - 1).cast<float>().eval(), 0.f);
        m_extruder_colors[i]            = static_cast<unsigned char>(i);
        m_result.filament_diameters[i]  = static_cast<float>(config.filament_diameter.get_at(i));
        m_result.required_nozzle_HRC[i] = static_cast<int>(config.required_nozzle_HRC.get_at(i));
        m_result.filament_densities[i]  = static_cast<float>(config.filament_density.get_at(i));
        m_result.filament_vitrification_temperature[i] = static_cast<float>(config.temperature_vitrification.get_at(i));
        m_result.filament_costs[i]      = static_cast<float>(config.filament_cost.get_at(i));
    }

    if (m_flavor == gcfMarlinLegacy || m_flavor == gcfMarlinFirmware || m_flavor == gcfKlipper) {
        m_time_processor.machine_limits = reinterpret_cast<const MachineEnvelopeConfig&>(config);
        if (m_flavor == gcfMarlinLegacy) {
            // Legacy Marlin does not have separate travel acceleration, it uses the 'extruding' value instead.
            m_time_processor.machine_limits.machine_max_acceleration_travel = m_time_processor.machine_limits.machine_max_acceleration_extruding;
        }
    }

    // Filament load / unload times are not specific to a firmware flavor. Let anybody use it if they find it useful.
    // As of now the fields are shown at the UI dialog in the same combo box as the ramming values, so they
    // are considered to be active for the single extruder multi-material printers only.
    m_time_processor.filament_load_times = static_cast<float>(config.machine_load_filament_time.value);
    m_time_processor.filament_unload_times = static_cast<float>(config.machine_unload_filament_time.value);
    m_time_processor.extruder_change_times = static_cast<float>(config.machine_switch_extruder_time.value);

    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        float max_acceleration = get_option_value(m_time_processor.machine_limits.machine_max_acceleration_extruding, i);
        m_time_processor.machines[i].max_acceleration = max_acceleration;
        m_time_processor.machines[i].acceleration = (max_acceleration > 0.0f) ? max_acceleration : DEFAULT_ACCELERATION;
        float max_retract_acceleration = get_option_value(m_time_processor.machine_limits.machine_max_acceleration_retracting, i);
        m_time_processor.machines[i].max_retract_acceleration = max_retract_acceleration;
        m_time_processor.machines[i].retract_acceleration = (max_retract_acceleration > 0.0f) ? max_retract_acceleration : DEFAULT_RETRACT_ACCELERATION;
        float max_travel_acceleration = get_option_value(m_time_processor.machine_limits.machine_max_acceleration_travel, i);
        m_time_processor.machines[i].max_travel_acceleration = max_travel_acceleration;
        m_time_processor.machines[i].travel_acceleration = (max_travel_acceleration > 0.0f) ? max_travel_acceleration : DEFAULT_TRAVEL_ACCELERATION;
    }

    const ConfigOptionFloat* initial_layer_print_height = config.option<ConfigOptionFloat>("initial_layer_print_height");
    if (initial_layer_print_height != nullptr)
        m_first_layer_height = std::abs(initial_layer_print_height->value);

    m_result.printable_height = config.printable_height;

    auto filament_maps = config.option<ConfigOptionInts>("filament_map");
    if (filament_maps != nullptr) {
        m_filament_maps = filament_maps->values;
        std::transform(m_filament_maps.begin(), m_filament_maps.end(), m_filament_maps.begin(), [](int value) {return value - 1; });
    }

    const ConfigOptionBool* spiral_vase = config.option<ConfigOptionBool>("spiral_mode");
    if (spiral_vase != nullptr)
        m_detect_layer_based_on_tag  = spiral_vase->value;

    const ConfigOptionBool *has_scarf_joint_seam = config.option<ConfigOptionBool>("has_scarf_joint_seam");
    if (has_scarf_joint_seam != nullptr)
        m_detect_layer_based_on_tag  = m_detect_layer_based_on_tag  || has_scarf_joint_seam->value;

}

void GCodeProcessor::apply_config(const DynamicPrintConfig& config)
{
    m_parser.apply_config(config);

    //BBS
    const ConfigOptionFloatsNullable* nozzle_volume = config.option<ConfigOptionFloatsNullable>("nozzle_volume");
    if (nozzle_volume != nullptr) {
        m_nozzle_volume.resize(nozzle_volume->size(), 0);
        for (size_t idx = 0; idx < nozzle_volume->size(); ++idx)
            m_nozzle_volume[idx] = nozzle_volume->values[idx];
    }

    const ConfigOptionIntsNullable* nozzle_temperature = config.option<ConfigOptionIntsNullable>("nozzle_temperature");
    if (nozzle_temperature != nullptr) {
        m_filament_nozzle_temp.resize(nozzle_temperature->size(), 0);
        for (size_t idx = 0; idx < nozzle_temperature->size(); ++idx)
            m_filament_nozzle_temp[idx] = nozzle_temperature->get_at(idx);
    }

    const ConfigOptionStrings* filament_type = config.option<ConfigOptionStrings>("filament_type");
    if (filament_type != nullptr) {
        m_filament_types.resize(filament_type->size());
        for (size_t idx = 0; idx < filament_type->size(); ++idx)
            m_filament_types[idx] = filament_type->get_at(idx);
    }

    const ConfigOptionFloatsNullable* hotend_cooling_rate = config.option<ConfigOptionFloatsNullable>("hotend_cooling_rate");
    if (hotend_cooling_rate != nullptr) {
        m_hotend_cooling_rate = hotend_cooling_rate->values;
    }

    const ConfigOptionFloatsNullable* hotend_heating_rate = config.option<ConfigOptionFloatsNullable>("hotend_heating_rate");
    if (hotend_heating_rate != nullptr) {
        m_hotend_heating_rate = hotend_heating_rate->values;
    }

    const ConfigOptionIntsNullable* filament_pre_cooling_temp = config.option<ConfigOptionIntsNullable>("filament_pre_cooling_temperature");
    if (filament_pre_cooling_temp != nullptr) {
        m_filament_pre_cooling_temp = filament_pre_cooling_temp->values;
    }

    const ConfigOptionBool* enable_pre_heating = config.option<ConfigOptionBool>("enable_pre_heating");
    if (enable_pre_heating != nullptr) {
        m_enable_pre_heating = enable_pre_heating->value;
    }

    const ConfigOptionInts* physical_extruder_map = config.option<ConfigOptionInts>("physical_extruder_map");
    if (physical_extruder_map != nullptr) {
        m_physical_extruder_map = physical_extruder_map->values;
    }

    const ConfigOptionEnumsGenericNullable* nozzle_type = config.option<ConfigOptionEnumsGenericNullable>("nozzle_type");
    if (nozzle_type != nullptr) {
        m_result.nozzle_type.resize(nozzle_type->size());
        for (size_t idx = 0; idx < nozzle_type->values.size(); ++idx) {
            m_result.nozzle_type[idx] = NozzleType(nozzle_type->values[idx]);
        }
    }

    const ConfigOptionEnum<GCodeFlavor>* gcode_flavor = config.option<ConfigOptionEnum<GCodeFlavor>>("gcode_flavor");
    if (gcode_flavor != nullptr)
        m_flavor = gcode_flavor->value;

    const ConfigOptionPoints* printable_area = config.option<ConfigOptionPoints>("printable_area");
    if (printable_area != nullptr)
        m_result.printable_area = printable_area->values;

    //BBS: add bed_exclude_area
    const ConfigOptionPoints* bed_exclude_area = config.option<ConfigOptionPoints>("bed_exclude_area");
    if (bed_exclude_area != nullptr)
        m_result.bed_exclude_area = bed_exclude_area->values;

    const ConfigOptionString* print_settings_id = config.option<ConfigOptionString>("print_settings_id");
    if (print_settings_id != nullptr)
        m_result.settings_ids.print = print_settings_id->value;

    const ConfigOptionStrings* filament_settings_id = config.option<ConfigOptionStrings>("filament_settings_id");
    if (filament_settings_id != nullptr)
        m_result.settings_ids.filament = filament_settings_id->values;

    const ConfigOptionString* printer_settings_id = config.option<ConfigOptionString>("printer_settings_id");
    if (printer_settings_id != nullptr)
        m_result.settings_ids.printer = printer_settings_id->value;

    // BBS
    m_result.filaments_count = config.option<ConfigOptionFloats>("filament_diameter")->values.size();

    const ConfigOptionFloats* filament_diameters = config.option<ConfigOptionFloats>("filament_diameter");
    if (filament_diameters != nullptr) {
        m_result.filament_diameters.clear();
        m_result.filament_diameters.resize(filament_diameters->values.size());
        for (size_t i = 0; i < filament_diameters->values.size(); ++i) {
            m_result.filament_diameters[i] = static_cast<float>(filament_diameters->values[i]);
        }
    }

    if (m_result.filament_diameters.size() < m_result.filaments_count) {
        for (size_t i = m_result.filament_diameters.size(); i < m_result.filaments_count; ++i) {
            m_result.filament_diameters.emplace_back(DEFAULT_FILAMENT_DIAMETER);
        }
    }

    const ConfigOptionInts *filament_HRC = config.option<ConfigOptionInts>("required_nozzle_HRC");
    if (filament_HRC != nullptr) {
        m_result.required_nozzle_HRC.clear();
        m_result.required_nozzle_HRC.resize(filament_HRC->values.size());
        for (size_t i = 0; i < filament_HRC->values.size(); ++i) { m_result.required_nozzle_HRC[i] = static_cast<float>(filament_HRC->values[i]); }
    }

    if (m_result.required_nozzle_HRC.size() < m_result.filaments_count) {
        for (size_t i = m_result.required_nozzle_HRC.size(); i < m_result.filaments_count; ++i) { m_result.required_nozzle_HRC.emplace_back(DEFAULT_FILAMENT_HRC);
        }
    }

    const ConfigOptionFloats* filament_densities = config.option<ConfigOptionFloats>("filament_density");
    if (filament_densities != nullptr) {
        m_result.filament_densities.clear();
        m_result.filament_densities.resize(filament_densities->values.size());
        for (size_t i = 0; i < filament_densities->values.size(); ++i) {
            m_result.filament_densities[i] = static_cast<float>(filament_densities->values[i]);
        }
    }

    if (m_result.filament_densities.size() < m_result.filaments_count) {
        for (size_t i = m_result.filament_densities.size(); i < m_result.filaments_count; ++i) {
            m_result.filament_densities.emplace_back(DEFAULT_FILAMENT_DENSITY);
        }
    }

    auto filament_maps = config.option<ConfigOptionInts>("filament_map");
    if (filament_maps != nullptr) {
        m_filament_maps = filament_maps->values;
        std::transform(m_filament_maps.begin(), m_filament_maps.end(), m_filament_maps.begin(), [](int value) {return value - 1; });
    }

    //BBS
    const ConfigOptionFloats* filament_costs = config.option<ConfigOptionFloats>("filament_cost");
    if (filament_costs != nullptr) {
        m_result.filament_costs.clear();
        m_result.filament_costs.resize(filament_costs->values.size());
        for (size_t i = 0; i < filament_costs->values.size(); ++i)
            m_result.filament_costs[i]=static_cast<float>(filament_costs->values[i]);
    }
    for (size_t i = m_result.filament_costs.size(); i < m_result.filaments_count; ++i) {
        m_result.filament_costs.emplace_back(DEFAULT_FILAMENT_COST);
    }

    //BBS
    const ConfigOptionInts* filament_vitrification_temperature = config.option<ConfigOptionInts>("temperature_vitrification");
    if (filament_vitrification_temperature != nullptr) {
        m_result.filament_vitrification_temperature.clear();
        m_result.filament_vitrification_temperature.resize(filament_vitrification_temperature->values.size());
        for (size_t i = 0; i < filament_vitrification_temperature->values.size(); ++i) {
            m_result.filament_vitrification_temperature[i] = static_cast<int>(filament_vitrification_temperature->values[i]);
        }
    }
    if (m_result.filament_vitrification_temperature.size() < m_result.filaments_count) {
        for (size_t i = m_result.filament_vitrification_temperature.size(); i < m_result.filaments_count; ++i) {
            m_result.filament_vitrification_temperature.emplace_back(DEFAULT_FILAMENT_VITRIFICATION_TEMPERATURE);
        }
    }

    const ConfigOptionPoints* extruder_offset = config.option<ConfigOptionPoints>("extruder_offset");
    const ConfigOptionBool* single_extruder_multi_material = config.option<ConfigOptionBool>("single_extruder_multi_material");
    if (extruder_offset != nullptr) {
        //BBS: for single extruder multi material, only use the offset of first extruder
        if (single_extruder_multi_material != nullptr && single_extruder_multi_material->getBool()) {
            Vec2f offset = extruder_offset->values[0].cast<float>();
            m_extruder_offsets.resize(m_result.filaments_count);
            for (size_t i = 0; i < m_result.filaments_count; ++i) {
                m_extruder_offsets[i] = { offset(0), offset(1), 0.0f };
            }
        }
        else {
            m_extruder_offsets.resize(extruder_offset->values.size());
            for (size_t i = 0; i < extruder_offset->values.size(); ++i) {
                Vec2f offset = extruder_offset->values[i].cast<float>();
                m_extruder_offsets[i] = { offset(0), offset(1), 0.0f };
            }
        }
    }

    if (m_extruder_offsets.size() < m_result.filaments_count) {
        for (size_t i = m_extruder_offsets.size(); i < m_result.filaments_count; ++i) {
            m_extruder_offsets.emplace_back(DEFAULT_EXTRUDER_OFFSET);
        }
    }

    // BBS
    const ConfigOptionStrings* filament_colour = config.option<ConfigOptionStrings>("filament_colour");
    if (filament_colour != nullptr && filament_colour->values.size() == m_result.extruder_colors.size()) {
        for (size_t i = 0; i < m_result.extruder_colors.size(); ++i) {
            if (m_result.extruder_colors[i].empty())
                m_result.extruder_colors[i] = filament_colour->values[i];
        }
    }

    if (m_result.extruder_colors.size() < m_result.filaments_count) {
        for (size_t i = m_result.extruder_colors.size(); i < m_result.filaments_count; ++i) {
            m_result.extruder_colors.emplace_back(std::string());
        }
    }

    // replace missing values with default
    for (size_t i = 0; i < m_result.extruder_colors.size(); ++i) {
        if (m_result.extruder_colors[i].empty())
            m_result.extruder_colors[i] = "#FF8000";
    }

    m_extruder_colors.resize(m_result.extruder_colors.size());
    for (size_t i = 0; i < m_result.extruder_colors.size(); ++i) {
        m_extruder_colors[i] = static_cast<unsigned char>(i);
    }

    m_extruder_temps.resize(m_result.filaments_count);

    const ConfigOptionFloat* machine_load_filament_time = config.option<ConfigOptionFloat>("machine_load_filament_time");
    if (machine_load_filament_time != nullptr)
        m_time_processor.filament_load_times = static_cast<float>(machine_load_filament_time->value);

    const ConfigOptionFloat* machine_unload_filament_time = config.option<ConfigOptionFloat>("machine_unload_filament_time");
    if (machine_unload_filament_time != nullptr)
        m_time_processor.filament_unload_times = static_cast<float>(machine_unload_filament_time->value);

    const ConfigOptionFloat* machine_switch_extruder_time = config.option<ConfigOptionFloat>("machine_switch_extruder_time");
    if (machine_switch_extruder_time != nullptr)
        m_time_processor.extruder_change_times = static_cast<float>(machine_switch_extruder_time->value);

    if (m_flavor == gcfMarlinLegacy || m_flavor == gcfMarlinFirmware || m_flavor == gcfKlipper) {
        const ConfigOptionFloatsNullable* machine_max_acceleration_x = config.option<ConfigOptionFloatsNullable>("machine_max_acceleration_x");
        if (machine_max_acceleration_x != nullptr)
            m_time_processor.machine_limits.machine_max_acceleration_x.values = machine_max_acceleration_x->values;

        const ConfigOptionFloatsNullable* machine_max_acceleration_y = config.option<ConfigOptionFloatsNullable>("machine_max_acceleration_y");
        if (machine_max_acceleration_y != nullptr)
            m_time_processor.machine_limits.machine_max_acceleration_y.values = machine_max_acceleration_y->values;

        const ConfigOptionFloatsNullable* machine_max_acceleration_z = config.option<ConfigOptionFloatsNullable>("machine_max_acceleration_z");
        if (machine_max_acceleration_z != nullptr)
            m_time_processor.machine_limits.machine_max_acceleration_z.values = machine_max_acceleration_z->values;

        const ConfigOptionFloatsNullable* machine_max_acceleration_e = config.option<ConfigOptionFloatsNullable>("machine_max_acceleration_e");
        if (machine_max_acceleration_e != nullptr)
            m_time_processor.machine_limits.machine_max_acceleration_e.values = machine_max_acceleration_e->values;

        const ConfigOptionFloatsNullable* machine_max_speed_x = config.option<ConfigOptionFloatsNullable>("machine_max_speed_x");
        if (machine_max_speed_x != nullptr)
            m_time_processor.machine_limits.machine_max_speed_x.values = machine_max_speed_x->values;

        const ConfigOptionFloatsNullable* machine_max_speed_y = config.option<ConfigOptionFloatsNullable>("machine_max_speed_y");
        if (machine_max_speed_y != nullptr)
            m_time_processor.machine_limits.machine_max_speed_y.values = machine_max_speed_y->values;

        const ConfigOptionFloatsNullable* machine_max_speed_z = config.option<ConfigOptionFloatsNullable>("machine_max_speed_z");
        if (machine_max_speed_z != nullptr)
            m_time_processor.machine_limits.machine_max_speed_z.values = machine_max_speed_z->values;

        const ConfigOptionFloatsNullable* machine_max_speed_e = config.option<ConfigOptionFloatsNullable>("machine_max_speed_e");
        if (machine_max_speed_e != nullptr)
            m_time_processor.machine_limits.machine_max_speed_e.values = machine_max_speed_e->values;

        const ConfigOptionFloatsNullable* machine_max_jerk_x = config.option<ConfigOptionFloatsNullable>("machine_max_jerk_x");
        if (machine_max_jerk_x != nullptr)
            m_time_processor.machine_limits.machine_max_jerk_x.values = machine_max_jerk_x->values;

        const ConfigOptionFloatsNullable* machine_max_jerk_y = config.option<ConfigOptionFloatsNullable>("machine_max_jerk_y");
        if (machine_max_jerk_y != nullptr)
            m_time_processor.machine_limits.machine_max_jerk_y.values = machine_max_jerk_y->values;

        const ConfigOptionFloatsNullable* machine_max_jerk_z = config.option<ConfigOptionFloatsNullable>("machine_max_jerkz");
        if (machine_max_jerk_z != nullptr)
            m_time_processor.machine_limits.machine_max_jerk_z.values = machine_max_jerk_z->values;

        const ConfigOptionFloatsNullable* machine_max_jerk_e = config.option<ConfigOptionFloatsNullable>("machine_max_jerk_e");
        if (machine_max_jerk_e != nullptr)
            m_time_processor.machine_limits.machine_max_jerk_e.values = machine_max_jerk_e->values;

        const ConfigOptionFloatsNullable* machine_max_acceleration_extruding = config.option<ConfigOptionFloatsNullable>("machine_max_acceleration_extruding");
        if (machine_max_acceleration_extruding != nullptr)
            m_time_processor.machine_limits.machine_max_acceleration_extruding.values = machine_max_acceleration_extruding->values;

        const ConfigOptionFloatsNullable* machine_max_acceleration_retracting = config.option<ConfigOptionFloatsNullable>("machine_max_acceleration_retracting");
        if (machine_max_acceleration_retracting != nullptr)
            m_time_processor.machine_limits.machine_max_acceleration_retracting.values = machine_max_acceleration_retracting->values;


        // Legacy Marlin does not have separate travel acceleration, it uses the 'extruding' value instead.
        const ConfigOptionFloatsNullable* machine_max_acceleration_travel = config.option<ConfigOptionFloatsNullable>(m_flavor == gcfMarlinLegacy
                                                                                                    ? "machine_max_acceleration_extruding"
                                                                                                    : "machine_max_acceleration_travel");
        if (machine_max_acceleration_travel != nullptr)
            m_time_processor.machine_limits.machine_max_acceleration_travel.values = machine_max_acceleration_travel->values;


        const ConfigOptionFloatsNullable* machine_min_extruding_rate = config.option<ConfigOptionFloatsNullable>("machine_min_extruding_rate");
        if (machine_min_extruding_rate != nullptr)
            m_time_processor.machine_limits.machine_min_extruding_rate.values = machine_min_extruding_rate->values;

        const ConfigOptionFloatsNullable* machine_min_travel_rate = config.option<ConfigOptionFloatsNullable>("machine_min_travel_rate");
        if (machine_min_travel_rate != nullptr)
            m_time_processor.machine_limits.machine_min_travel_rate.values = machine_min_travel_rate->values;
    }

    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        float max_acceleration = get_option_value(m_time_processor.machine_limits.machine_max_acceleration_extruding, i);
        m_time_processor.machines[i].max_acceleration = max_acceleration;
        m_time_processor.machines[i].acceleration = (max_acceleration > 0.0f) ? max_acceleration : DEFAULT_ACCELERATION;
        float max_retract_acceleration = get_option_value(m_time_processor.machine_limits.machine_max_acceleration_retracting, i);
        m_time_processor.machines[i].max_retract_acceleration = max_retract_acceleration;
        m_time_processor.machines[i].retract_acceleration = (max_retract_acceleration > 0.0f) ? max_retract_acceleration : DEFAULT_RETRACT_ACCELERATION;
        float max_travel_acceleration = get_option_value(m_time_processor.machine_limits.machine_max_acceleration_travel, i);
        m_time_processor.machines[i].max_travel_acceleration = max_travel_acceleration;
        m_time_processor.machines[i].travel_acceleration = (max_travel_acceleration > 0.0f) ? max_travel_acceleration : DEFAULT_TRAVEL_ACCELERATION;
    }

    if (m_flavor == gcfMarlinLegacy || m_flavor == gcfMarlinFirmware) {
        const ConfigOptionBool* silent_mode = config.option<ConfigOptionBool>("silent_mode");
        if (silent_mode != nullptr) {
            if (silent_mode->value && m_time_processor.machine_limits.machine_max_acceleration_x.values.size() > 1)
                enable_stealth_time_estimator(true);
        }
    }

    const ConfigOptionFloat* initial_layer_print_height = config.option<ConfigOptionFloat>("initial_layer_print_height");
    if (initial_layer_print_height != nullptr)
        m_first_layer_height = std::abs(initial_layer_print_height->value);

    const ConfigOptionFloat* printable_height = config.option<ConfigOptionFloat>("printable_height");
    if (printable_height != nullptr)
        m_result.printable_height = printable_height->value;

    const ConfigOptionBool* spiral_vase = config.option<ConfigOptionBool>("spiral_mode");
    if (spiral_vase != nullptr)
        m_detect_layer_based_on_tag  = spiral_vase->value;

    const ConfigOptionBool *has_scarf_joint_seam = config.option<ConfigOptionBool>("has_scarf_joint_seam");
    if (has_scarf_joint_seam != nullptr)
        m_detect_layer_based_on_tag = m_detect_layer_based_on_tag || has_scarf_joint_seam->value;

    const ConfigOptionEnumGeneric *bed_type = config.option<ConfigOptionEnumGeneric>("curr_bed_type");
    if (bed_type != nullptr)
        m_result.bed_type = (BedType)bed_type->value;

}

void GCodeProcessor::enable_stealth_time_estimator(bool enabled)
{
    m_time_processor.machines[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Stealth)].enabled = enabled;
}

void GCodeProcessor::reset()
{
    m_units = EUnits::Millimeters;
    m_global_positioning_type = EPositioningType::Absolute;
    m_e_local_positioning_type = EPositioningType::Absolute;
    m_extruder_offsets = std::vector<Vec3f>(MIN_EXTRUDERS_COUNT, Vec3f::Zero());
    m_flavor = gcfRepRapSprinter;
    m_nozzle_volume = {0.f,0.f};

    m_start_position = { 0.0f, 0.0f, 0.0f, 0.0f };
    m_end_position = { 0.0f, 0.0f, 0.0f, 0.0f };
    m_origin = { 0.0f, 0.0f, 0.0f, 0.0f };
    m_cached_position.reset();
    m_wiping = false;
    m_flushing = false;
    m_virtual_flushing = false;
    m_skippable = false;
    m_skippable_type = SkipType::stNone;
    m_wipe_tower = false;
    m_remaining_volume = { 0.f,0.f };
    // BBS: arc move related data
    m_move_path_type = EMovePathType::Noop_move;
    m_arc_center = Vec3f::Zero();

    m_line_id = 0;
    m_last_line_id = 0;
    m_feedrate = 0.0f;
    m_width = 0.0f;
    m_height = 0.0f;
    m_forced_width = 0.0f;
    m_forced_height = 0.0f;
    m_mm3_per_mm = 0.0f;
    m_fan_speed = 0.0f;

    m_extrusion_role = erNone;

    m_filament_id = {static_cast<unsigned char>(-1),static_cast<unsigned char>(-1)};
    m_last_filament_id = {static_cast<unsigned char>(-1),static_cast<unsigned char>(-1) };
    m_extruder_id = static_cast<unsigned char>(-1);
    m_extruder_colors.resize(MIN_EXTRUDERS_COUNT);
    for (size_t i = 0; i < MIN_EXTRUDERS_COUNT; ++i) {
        m_extruder_colors[i] = static_cast<unsigned char>(i);
    }
    m_extruder_temps.resize(MIN_EXTRUDERS_COUNT);
    for (size_t i = 0; i < MIN_EXTRUDERS_COUNT; ++i) {
        m_extruder_temps[i] = 0.0f;
    }

    m_physical_extruder_map.clear();
    m_filament_nozzle_temp.clear();
    m_enable_pre_heating = false;
    m_hotend_cooling_rate = m_hotend_heating_rate = { 2.f };

    m_highest_bed_temp = 0;

    m_extruded_last_z = 0.0f;
    m_zero_layer_height = 0.0f;
    m_first_layer_height = 0.0f;
    m_processing_start_custom_gcode = false;
    m_g1_line_id = 0;
    m_layer_id = 0;
    m_cp_color.reset();

    m_producer = EProducer::Unknown;

    m_time_processor.reset();
    m_used_filaments.reset();

    m_result.reset();
    m_result.id = ++s_result_id;

    m_last_default_color_id = 0;

    m_options_z_corrector.reset();

    m_detect_layer_based_on_tag = false;

    m_seams_count = 0;
#if ENABLE_GCODE_VIEWER_DATA_CHECKING
    m_mm3_per_mm_compare.reset();
    m_height_compare.reset();
    m_width_compare.reset();
#endif // ENABLE_GCODE_VIEWER_DATA_CHECKING
}

static inline const char* skip_whitespaces(const char *begin, const char *end) {
    for (; begin != end && (*begin == ' ' || *begin == '\t'); ++ begin);
    return begin;
}

static inline const char* remove_eols(const char *begin, const char *end) {
    for (; begin != end && (*(end - 1) == '\r' || *(end - 1) == '\n'); -- end);
    return end;
}

// Load a G-code into a stand-alone G-code viewer.
// throws CanceledException through print->throw_if_canceled() (sent by the caller as callback).
void GCodeProcessor::process_file(const std::string& filename, std::function<void()> cancel_callback)
{
    CNumericLocalesSetter locales_setter;

#if ENABLE_GCODE_VIEWER_STATISTICS
    m_start_time = std::chrono::high_resolution_clock::now();
#endif // ENABLE_GCODE_VIEWER_STATISTICS

    // pre-processing
    // parse the gcode file to detect its producer
    {
        m_parser.parse_file_raw(filename, [this](GCodeReader& reader, const char *begin, const char *end) {
            begin = skip_whitespaces(begin, end);
            if (begin != end && *begin == ';') {
                // Comment.
                begin = skip_whitespaces(++ begin, end);
                end   = remove_eols(begin, end);
                if (begin != end) {
                    if (m_producer == EProducer::Unknown) {
                        if (detect_producer(std::string_view(begin, end - begin))) {
                            m_parser.quit_parsing();
                        }
                    } else if (std::string(begin, end).find("CONFIG_BLOCK_END") != std::string::npos) {
                        m_parser.quit_parsing();
                    }
                }
            }
        });
        m_parser.reset();

        // if the gcode was produced by BambuStudio,
        // extract the config from it
        if (m_producer == EProducer::BambuStudio || m_producer == EProducer::Slic3rPE || m_producer == EProducer::Slic3r) {
            DynamicPrintConfig config;
            config.apply(FullPrintConfig::defaults());
            // Silently substitute unknown values by new ones for loading configurations from BambuStudio's own G-code.
            // Showing substitution log or errors may make sense, but we are not really reading many values from the G-code config,
            // thus a probability of incorrect substitution is low and the G-code viewer is a consumer-only anyways.
            config.load_from_gcode_file(filename, ForwardCompatibilitySubstitutionRule::EnableSilent);

            ConfigOptionStrings *filament_color = config.opt<ConfigOptionStrings>("filament_colour");
            ConfigOptionInts    *filament_map   = config.opt<ConfigOptionInts>("filament_map", true);
            if (filament_color && filament_color->size() != filament_map->size()) {
                filament_map->values.resize(filament_color->size(), 1);
            }

            apply_config(config);
        }
        else if (m_producer == EProducer::Simplify3D)
            apply_config_simplify3d(filename);
        else if (m_producer == EProducer::SuperSlicer)
            apply_config_superslicer(filename);
    }

    // process gcode
    m_result.filename = filename;
    m_result.id = ++s_result_id;
    // 1st move must be a dummy move
    m_result.moves.emplace_back(GCodeProcessorResult::MoveVertex());
    size_t parse_line_callback_cntr = 10000;
    m_parser.parse_file(filename, [this, cancel_callback, &parse_line_callback_cntr](GCodeReader& reader, const GCodeReader::GCodeLine& line) {
        if (-- parse_line_callback_cntr == 0) {
            // Don't call the cancel_callback() too often, do it every at every 10000'th line.
            parse_line_callback_cntr = 10000;
            if (cancel_callback)
                cancel_callback();
        }
        this->process_gcode_line(line, true);
    }, m_result.lines_ends);

    // Don't post-process the G-code to update time stamps.
    this->finalize(false);
}

void GCodeProcessor::initialize(const std::string& filename)
{
    assert(is_decimal_separator_point());

#if ENABLE_GCODE_VIEWER_STATISTICS
    m_start_time = std::chrono::high_resolution_clock::now();
#endif // ENABLE_GCODE_VIEWER_STATISTICS

    // process gcode
    m_result.filename = filename;
    m_result.id = ++s_result_id;
    // 1st move must be a dummy move
    m_result.moves.emplace_back();
}

void GCodeProcessor::process_buffer(const std::string &buffer)
{
    //FIXME maybe cache GCodeLine gline to be over multiple parse_buffer() invocations.
    m_parser.parse_buffer(buffer, [this](GCodeReader&, const GCodeReader::GCodeLine& line) {
        this->process_gcode_line(line, false);
    });
}

void GCodeProcessor::finalize(bool post_process)
{
    // update width/height of wipe moves
    for (GCodeProcessorResult::MoveVertex& move : m_result.moves) {
        if (move.type == EMoveType::Wipe) {
            move.width = Wipe_Width;
            move.height = Wipe_Height;
        }
    }

    // process the time blocks
    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        TimeMachine& machine = m_time_processor.machines[i];
        TimeMachine::CustomGCodeTime& gcode_time = machine.gcode_time;
        machine.calculate_time(0, 0, ExtrusionRole::erNone, [&result=m_result, i,&machine](const TimeBlock& block, int time) {
            machine.handle_time_block(block,time,i,result);
        });
        if (gcode_time.needed && gcode_time.cache != 0.0f)
            gcode_time.times.push_back({ CustomGCode::ColorChange, gcode_time.cache });
    }

    m_used_filaments.process_caches(this);

    update_estimated_times_stats();
    auto time_mode = m_result.print_statistics.modes[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)];

    auto it = std::find_if(time_mode.roles_times.begin(), time_mode.roles_times.end(), [](const std::pair<ExtrusionRole, float>& item) { return erCustom == item.first; });
    auto prepare_time = (it != time_mode.roles_times.end()) ? it->second : 0.0f;

    //update times for results
    for (size_t i = 0; i < m_result.moves.size(); i++) {
        //field layer_duration contains the layer id for the move in which the layer_duration has to be set.
        size_t layer_id = size_t(m_result.moves[i].layer_duration);
        std::vector<float>& layer_times = m_result.print_statistics.modes[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)].layers_times;
        if (layer_times.size() > layer_id - 1 && layer_id > 0)
            m_result.moves[i].layer_duration = layer_id == 1 ? std::max(0.f,layer_times[layer_id - 1] - prepare_time) : layer_times[layer_id - 1];
        else
            m_result.moves[i].layer_duration = 0;
    }

#if ENABLE_GCODE_VIEWER_DATA_CHECKING
    std::cout << "\n";
    m_mm3_per_mm_compare.output();
    m_height_compare.output();
    m_width_compare.output();
#endif // ENABLE_GCODE_VIEWER_DATA_CHECKING
    if (post_process){
        constexpr float inject_time_threshold = 30.f;
        TimeProcessContext context(
            m_used_filaments,
            m_filament_lists,
            m_filament_maps,
            m_filament_types,
            m_filament_nozzle_temp,
            m_physical_extruder_map,
            m_layer_id,
            m_hotend_cooling_rate,
            m_hotend_heating_rate,
            m_filament_pre_cooling_temp,
            inject_time_threshold,
            m_enable_pre_heating
        );
        m_time_processor.post_process(m_result.filename, m_result.moves, m_result.lines_ends, context);
    }
#if ENABLE_GCODE_VIEWER_STATISTICS
    m_result.time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - m_start_time).count();
#endif // ENABLE_GCODE_VIEWER_STATISTICS
    //BBS: update slice warning
    update_slice_warnings();
}

float GCodeProcessor::get_time(PrintEstimatedStatistics::ETimeMode mode) const
{
    return (mode < PrintEstimatedStatistics::ETimeMode::Count) ? m_time_processor.machines[static_cast<size_t>(mode)].time : 0.0f;
}

float GCodeProcessor::get_prepare_time(PrintEstimatedStatistics::ETimeMode mode) const
{
    return (mode < PrintEstimatedStatistics::ETimeMode::Count) ? m_time_processor.machines[static_cast<size_t>(mode)].prepare_time : 0.0f;
}

std::string GCodeProcessor::get_time_dhm(PrintEstimatedStatistics::ETimeMode mode) const
{
    return (mode < PrintEstimatedStatistics::ETimeMode::Count) ? short_time(get_time_dhms(m_time_processor.machines[static_cast<size_t>(mode)].time)) : std::string("N/A");
}

std::vector<std::pair<CustomGCode::Type, std::pair<float, float>>> GCodeProcessor::get_custom_gcode_times(PrintEstimatedStatistics::ETimeMode mode, bool include_remaining) const
{
    std::vector<std::pair<CustomGCode::Type, std::pair<float, float>>> ret;
    if (mode < PrintEstimatedStatistics::ETimeMode::Count) {
        const TimeMachine& machine = m_time_processor.machines[static_cast<size_t>(mode)];
        float total_time = 0.0f;
        for (const auto& [type, time] : machine.gcode_time.times) {
            float remaining = include_remaining ? machine.time - total_time : 0.0f;
            ret.push_back({ type, { time, remaining } });
            total_time += time;
        }
    }
    return ret;
}

std::vector<std::pair<EMoveType, float>> GCodeProcessor::get_moves_time(PrintEstimatedStatistics::ETimeMode mode) const
{
    std::vector<std::pair<EMoveType, float>> ret;
    if (mode < PrintEstimatedStatistics::ETimeMode::Count) {
        for (size_t i = 0; i < m_time_processor.machines[static_cast<size_t>(mode)].moves_time.size(); ++i) {
            float time = m_time_processor.machines[static_cast<size_t>(mode)].moves_time[i];
            if (time > 0.0f)
                ret.push_back({ static_cast<EMoveType>(i), time });
        }
    }
    return ret;
}

std::vector<std::pair<ExtrusionRole, float>> GCodeProcessor::get_roles_time(PrintEstimatedStatistics::ETimeMode mode) const
{
    std::vector<std::pair<ExtrusionRole, float>> ret;
    if (mode < PrintEstimatedStatistics::ETimeMode::Count) {
        for (size_t i = 0; i < m_time_processor.machines[static_cast<size_t>(mode)].roles_time.size(); ++i) {
            float time = m_time_processor.machines[static_cast<size_t>(mode)].roles_time[i];
            if (time > 0.0f)
                ret.push_back({ static_cast<ExtrusionRole>(i), time });
        }
    }
    return ret;
}

ConfigSubstitutions load_from_superslicer_gcode_file(const std::string& filename, DynamicPrintConfig& config, ForwardCompatibilitySubstitutionRule compatibility_rule)
{
    // for reference, see: ConfigBase::load_from_gcode_file()

    boost::nowide::ifstream ifs(filename);

    auto                      header_end_pos = ifs.tellg();
    ConfigSubstitutionContext substitutions_ctxt(compatibility_rule);
    size_t                    key_value_pairs = 0;

    ifs.seekg(0, ifs.end);
    auto file_length = ifs.tellg();
    auto data_length = std::min<std::fstream::pos_type>(65535, file_length - header_end_pos);
    ifs.seekg(file_length - data_length, ifs.beg);
    std::vector<char> data(size_t(data_length) + 1, 0);
    ifs.read(data.data(), data_length);
    ifs.close();
    key_value_pairs = ConfigBase::load_from_gcode_string_legacy(config, data.data(), substitutions_ctxt);

    if (key_value_pairs < 80)
        throw Slic3r::RuntimeError(format("Suspiciously low number of configuration values extracted from %1%: %2%", filename, key_value_pairs));

    return std::move(substitutions_ctxt.substitutions);
}

void GCodeProcessor::apply_config_superslicer(const std::string& filename)
{
    DynamicPrintConfig config;
    config.apply(FullPrintConfig::defaults());
    load_from_superslicer_gcode_file(filename, config, ForwardCompatibilitySubstitutionRule::EnableSilent);
    apply_config(config);
}

std::vector<float> GCodeProcessor::get_layers_time(PrintEstimatedStatistics::ETimeMode mode) const
{
    return (mode < PrintEstimatedStatistics::ETimeMode::Count) ?
        m_time_processor.machines[static_cast<size_t>(mode)].layers_time :
        std::vector<float>();
}

void GCodeProcessor::apply_config_simplify3d(const std::string& filename)
{
    struct BedSize
    {
        double x{ 0.0 };
        double y{ 0.0 };

        bool is_defined() const { return x > 0.0 && y > 0.0; }
    };

    BedSize bed_size;
    bool    producer_detected = false;

    m_parser.parse_file_raw(filename, [this, &bed_size, &producer_detected](GCodeReader& reader, const char* begin, const char* end) {

        auto extract_double = [](const std::string_view cmt, const std::string& key, double& out) {
            size_t pos = cmt.find(key);
            if (pos != cmt.npos) {
                pos = cmt.find(',', pos);
                if (pos != cmt.npos) {
                    out = string_to_double_decimal_point(cmt.substr(pos+1));
                    return true;
                }
            }
            return false;
        };

        auto extract_floats = [](const std::string_view cmt, const std::string& key, std::vector<float>& out) {
            size_t pos = cmt.find(key);
            if (pos != cmt.npos) {
                pos = cmt.find(',', pos);
                if (pos != cmt.npos) {
                    const std::string_view data_str = cmt.substr(pos + 1);
                    std::vector<std::string> values_str;
                    boost::split(values_str, data_str, boost::is_any_of("|,"), boost::token_compress_on);
                    for (const std::string& s : values_str) {
                        out.emplace_back(static_cast<float>(string_to_double_decimal_point(s)));
                    }
                    return true;
                }
            }
            return false;
        };

        begin = skip_whitespaces(begin, end);
        end   = remove_eols(begin, end);
        if (begin != end) {
            if (*begin == ';') {
                // Comment.
                begin = skip_whitespaces(++ begin, end);
                if (begin != end) {
                    std::string_view comment(begin, end - begin);
                    if (producer_detected) {
                        if (bed_size.x == 0.0 && comment.find("strokeXoverride") != comment.npos)
                            extract_double(comment, "strokeXoverride", bed_size.x);
                        else if (bed_size.y == 0.0 && comment.find("strokeYoverride") != comment.npos)
                            extract_double(comment, "strokeYoverride", bed_size.y);
                        else if (comment.find("filamentDiameters") != comment.npos) {
                            m_result.filament_diameters.clear();
                            extract_floats(comment, "filamentDiameters", m_result.filament_diameters);
                        } else if (comment.find("filamentDensities") != comment.npos) {
                            m_result.filament_densities.clear();
                            extract_floats(comment, "filamentDensities", m_result.filament_densities);
                        } else if (comment.find("extruderDiameter") != comment.npos) {
                            std::vector<float> extruder_diameters;
                            extract_floats(comment, "extruderDiameter", extruder_diameters);
                            m_result.filaments_count = extruder_diameters.size();
                        }
                    } else if (boost::starts_with(comment, "G-Code generated by Simplify3D(R)"))
                        producer_detected = true;
                }
            } else {
                // Some non-empty G-code line detected, stop parsing config comments.
                reader.quit_parsing();
            }
        }
    });

    if (m_result.filaments_count == 0)
        m_result.filaments_count = std::max<size_t>(1, std::min(m_result.filament_diameters.size(), m_result.filament_densities.size()));

    if (bed_size.is_defined()) {
        m_result.printable_area = {
            { 0.0, 0.0 },
            { bed_size.x, 0.0 },
            { bed_size.x, bed_size.y },
            { 0.0, bed_size.y }
        };
    }
}

void GCodeProcessor::process_gcode_line(const GCodeReader::GCodeLine& line, bool producers_enabled)
{
/* std::cout << line.raw() << std::endl; */

    ++m_line_id;

    // update start position
    m_start_position = m_end_position;

    const std::string_view cmd = line.cmd();
    //OrcaSlicer
    if (m_flavor == gcfKlipper)
    {
        if (boost::iequals(cmd, "SET_VELOCITY_LIMIT"))
        {
            process_SET_VELOCITY_LIMIT(line);
            return;
        }
    }

    if (cmd.length() > 1) {
        // process command lines
        m_command_processor.process_comand(cmd, line);
    }
    else {
        const std::string &comment = line.raw();
        if (comment.length() > 2 && comment.front() == ';')
        {
            std::string comment_content = comment.substr(1); // only format like ";V{cmd}" is valid
            if (comment_content[0] == 'V' || comment_content[0] == 'v') {
                GCodeReader reader;
                GCodeReader::GCodeLine new_line;
                reader.parse_line(comment_content, [&new_line](const auto& greader, const auto& gline) {
                    new_line = gline;
                    });
                m_command_processor.process_comand(new_line.cmd(), new_line);
            }
            else {
                // Process tags embedded into comments. Tag comments always start at the start of a line
                // with a comment and continue with a tag without any whitespace separator.
                process_tags(comment_content, producers_enabled);
            }
        }
    }
}

#if __has_include(<charconv>)
    template <typename T, typename = void>
    struct is_from_chars_convertible : std::false_type {};
    template <typename T>
    struct is_from_chars_convertible<T, std::void_t<decltype(std::from_chars(std::declval<const char*>(), std::declval<const char*>(), std::declval<T&>()))>> : std::true_type {};
#endif

// Returns true if the number was parsed correctly into out and the number spanned the whole input string.
template<typename T>
[[nodiscard]] static inline bool parse_number(const std::string_view sv, T &out)
{
    // https://www.bfilipek.com/2019/07/detect-overload-from-chars.html#example-stdfromchars
#if __has_include(<charconv>)
    // Visual Studio 19 supports from_chars all right.
    // OSX compiler that we use only implements std::from_chars just for ints.
    // GCC that we compile on does not provide <charconv> at all.
    if constexpr (is_from_chars_convertible<T>::value) {
        auto str_end = sv.data() + sv.size();
        auto [end_ptr, error_code] = std::from_chars(sv.data(), str_end, out);
        return error_code == std::errc() && end_ptr == str_end;
    }
    else
#endif
    {
        // Legacy conversion, which is costly due to having to make a copy of the string before conversion.
        try {
            assert(sv.size() < 1024);
            assert(sv.data() != nullptr);
            std::string str { sv };
            size_t read = 0;
            if constexpr (std::is_same_v<T, int>)
                out = std::stoi(str, &read);
            else if constexpr (std::is_same_v<T, long>)
                out = std::stol(str, &read);
            else if constexpr (std::is_same_v<T, float>)
                out = string_to_double_decimal_point(str, &read);
            else if constexpr (std::is_same_v<T, double>)
                out = string_to_double_decimal_point(str, &read);
            return str.size() == read;
        } catch (...) {
            return false;
        }
    }
}

int GCodeProcessor::get_gcode_last_filament(const std::string& gcode_str)
{
    int str_size = gcode_str.size();
    int start_index = 0;
    int end_index = 0;
    int out_filament = -1;
    while (end_index < str_size) {
        if (gcode_str[end_index] != '\n') {
            end_index++;
            continue;
        }

        if (end_index > start_index) {
            std::string line_str = gcode_str.substr(start_index, end_index - start_index);
            line_str.erase(0, line_str.find_first_not_of(" "));
            line_str.erase(line_str.find_last_not_of(" ") + 1);
            if (line_str.empty() || line_str[0] != 'T') {
                start_index = end_index + 1;
                end_index = start_index;
                continue;
            }

            int out = -1;
            if (parse_number(line_str.substr(1), out) && out >= 0 && out < 255)
                out_filament = out;
        }

        start_index = end_index + 1;
        end_index = start_index;
    }

    return out_filament;
}

//BBS: get last z position from gcode
bool GCodeProcessor::get_last_z_from_gcode(const std::string& gcode_str, double& z)
{
    int str_size = gcode_str.size();
    int start_index = 0;
    int end_index = 0;
    bool is_z_changed = false;
    while (end_index < str_size) {
        //find a full line
        if (gcode_str[end_index] != '\n') {
            end_index++;
            continue;
        }
        //parse the line
        if (end_index > start_index) {
            std::string line_str = gcode_str.substr(start_index, end_index - start_index);
            line_str.erase(0, line_str.find_first_not_of(" "));
            line_str.erase(line_str.find_last_not_of(";") + 1);
            line_str.erase(line_str.find_last_not_of(" ") + 1);

            //command which may have z movement
            if (line_str.size() > 4 && (line_str.find("G0 ") == 0
                                       || line_str.find("G1 ") == 0
                                       || line_str.find("G2 ") == 0
                                       || line_str.find("G3 ") == 0))
            {
                auto z_pos = line_str.find(" Z");
                double temp_z = 0;
                if (z_pos != line_str.npos
                    && z_pos + 2 < line_str.size()) {
                    // Try to parse the numeric value.
                    std::string z_sub = line_str.substr(z_pos + 2);
                    char* c = &z_sub[0];
                    char* end = c + sizeof(z_sub.c_str());

                    auto is_end_of_word = [](char c) {
                        return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == 0 || c == ';';
                    };

                    auto [pend, ec] = fast_float::from_chars(c, end, temp_z);
                    if (pend != c && is_end_of_word(*pend)) {
                        // The axis value has been parsed correctly.
                        z = temp_z;
                        is_z_changed = true;
                    }
                }
            }
        }
        //loop to handle next line
        start_index = end_index + 1;
        end_index = start_index;
    }
    return is_z_changed;
}

bool GCodeProcessor::get_last_position_from_gcode(const std::string &gcode_str, Vec3f &pos)
{
    int  str_size     = gcode_str.size();
    int  start_index  = 0;
    int  end_index    = 0;
    bool is_z_changed = false;
    while (end_index < str_size) {
        // find a full line
        if (gcode_str[end_index] != '\n') {
            end_index++;
            continue;
        }
        // parse the line
        if (end_index > start_index) {
            std::string line_str = gcode_str.substr(start_index, end_index - start_index);
            line_str.erase(0, line_str.find_first_not_of(" "));
            line_str.erase(line_str.find_last_not_of(";") + 1);
            line_str.erase(line_str.find_last_not_of(" ") + 1);

            // command which may have z movement
            if (line_str.size() > 5 && (line_str.find("G0 ") == 0 || line_str.find("G1 ") == 0 || line_str.find("G2 ") == 0 || line_str.find("G3 ") == 0)) {
                {
                    float &x      = pos.x();
                    auto   z_pos  = line_str.find(" X");
                    float  temp_z = 0;
                    if (z_pos != line_str.npos && z_pos + 2 < line_str.size()) {
                        // Try to parse the numeric value.
                        std::string z_sub = line_str.substr(z_pos + 2);
                        char       *c     = &z_sub[0];
                        char       *end   = c + sizeof(z_sub.c_str());

                        auto is_end_of_word = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == 0 || c == ';'; };

                        auto [pend, ec] = fast_float::from_chars(c, end, temp_z);
                        if (pend != c && is_end_of_word(*pend)) {
                            // The axis value has been parsed correctly.
                            x            = temp_z;
                            is_z_changed = true;
                        }
                    }
                }

                {
                    float &y      = pos.y();
                    auto   z_pos  = line_str.find(" Y");
                    float  temp_z = 0;
                    if (z_pos != line_str.npos && z_pos + 2 < line_str.size()) {
                        // Try to parse the numeric value.
                        std::string z_sub = line_str.substr(z_pos + 2);
                        char       *c     = &z_sub[0];
                        char       *end   = c + sizeof(z_sub.c_str());

                        auto is_end_of_word = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == 0 || c == ';'; };

                        auto [pend, ec] = fast_float::from_chars(c, end, temp_z);
                        if (pend != c && is_end_of_word(*pend)) {
                            // The axis value has been parsed correctly.
                            y            = temp_z;
                            is_z_changed = true;
                        }
                    }
                }

                {
                    float &z      = pos.z();
                    auto   z_pos  = line_str.find(" Z");
                    float  temp_z = 0;
                    if (z_pos != line_str.npos && z_pos + 2 < line_str.size()) {
                        // Try to parse the numeric value.
                        std::string z_sub = line_str.substr(z_pos + 2);
                        char       *c     = &z_sub[0];
                        char       *end   = c + sizeof(z_sub.c_str());

                        auto is_end_of_word = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == 0 || c == ';'; };

                        auto [pend, ec] = fast_float::from_chars(c, end, temp_z);
                        if (pend != c && is_end_of_word(*pend)) {
                            // The axis value has been parsed correctly.
                            z            = temp_z;
                            is_z_changed = true;
                        }
                    }
                }
            }
        }
        // loop to handle next line
        start_index = end_index + 1;
        end_index   = start_index;
    }
    return is_z_changed;
}

void GCodeProcessor::process_tags(const std::string_view comment, bool producers_enabled)
{
    static ExtrusionRole prev_role;
    // producers tags
    if (producers_enabled && process_producers_tags(comment))
        return;

    // extrusion role tag
    if (boost::starts_with(comment, reserved_tag(ETags::Role))) {
        set_extrusion_role(ExtrusionEntity::string_to_role(comment.substr(reserved_tag(ETags::Role).length())));
        if (m_extrusion_role == erExternalPerimeter)
            m_seams_detector.activate(true);
        m_processing_start_custom_gcode = (m_extrusion_role == erCustom && m_g1_line_id == 0);
        return;
    }

    // ; OBJECT_ID  start
    if (boost::starts_with(comment, " start printing object")) {
        m_object_label_id = get_object_label_id(comment);
        return;
    }

    // ; OBJECT_ID  end
    if (boost::starts_with(comment, " stop printing object")) {
        m_object_label_id = -1;
        return;
    }

    // ; Z_HEIGHT:
    if (boost::starts_with(comment, " Z_HEIGHT:")) {
        m_print_z = get_z_height(comment);
        return;
    }

    // wipe start tag
    if (boost::starts_with(comment, reserved_tag(ETags::Wipe_Start))) {
        m_wiping = true;
        return;
    }

    // wipe end tag
    if (boost::starts_with(comment, reserved_tag(ETags::Wipe_End))) {
        m_wiping = false;
        return;
    }

    if (boost::starts_with(comment, reserved_tag(ETags::Wipe_Tower_Start))) {
        m_wipe_tower = true;
        return;
    }

    if (boost::starts_with(comment, reserved_tag(ETags::Wipe_Tower_End))) {
        m_wipe_tower = false;
        m_used_filaments.process_wipe_tower_cache(this);
        return;
    }


    if (boost::starts_with(comment, custom_tags(CustomETags::SKIPPABLE_START))) {
        m_skippable = true;
        return;
    }

    if (boost::starts_with(comment, custom_tags(CustomETags::SKIPPABLE_END))) {
        m_skippable = false;
        m_skippable_type = SkipType::stNone;
        return;
    }

    // skippable type
    if (boost::starts_with(comment, custom_tags(CustomETags::SKIPPABLE_TYPE))) {
        std::string_view type =comment.substr(custom_tags(CustomETags::SKIPPABLE_TYPE).length());
        set_skippable_type(type);
        return;
    }

    //BBS: flush start tag
    if (boost::starts_with(comment, custom_tags(CustomETags::FLUSH_START))) {
        prev_role = m_extrusion_role;
        set_extrusion_role(erFlush);
        m_flushing = true;
        return;
    }

    //BBS: flush end tag
    if (boost::starts_with(comment, custom_tags(CustomETags::FLUSH_END))) {
        set_extrusion_role(prev_role);
        m_flushing = false;
        return;
    }

    if (boost::starts_with(comment, custom_tags(CustomETags::VFLUSH_START))) {
        prev_role = m_extrusion_role;
        set_extrusion_role(erFlush);
        m_virtual_flushing = true;
        return;
    }

    if (boost::starts_with(comment, custom_tags(CustomETags::VFLUSH_END))) {
        set_extrusion_role(prev_role);
        m_virtual_flushing = false;
        return;
    }

    if (!producers_enabled || m_producer == EProducer::BambuStudio) {
        // height tag
        if (boost::starts_with(comment, reserved_tag(ETags::Height))) {
            if (!parse_number(comment.substr(reserved_tag(ETags::Height).size()), m_forced_height))
                BOOST_LOG_TRIVIAL(error) << "GCodeProcessor encountered an invalid value for Height (" << comment << ").";
            return;
        }
        // width tag
        if (boost::starts_with(comment, reserved_tag(ETags::Width))) {
            if (!parse_number(comment.substr(reserved_tag(ETags::Width).size()), m_forced_width))
                BOOST_LOG_TRIVIAL(error) << "GCodeProcessor encountered an invalid value for Width (" << comment << ").";
            return;
        }
    }

    // color change tag
    if (boost::starts_with(comment, reserved_tag(ETags::Color_Change))) {
        unsigned char filament_id = 0;
        static std::vector<std::string> Default_Colors = {
            "#0B2C7A", // { 0.043f, 0.173f, 0.478f }, // bluish
            "#1C8891", // { 0.110f, 0.533f, 0.569f },
            "#AAF200", // { 0.667f, 0.949f, 0.000f },
            "#F5CE0A", // { 0.961f, 0.808f, 0.039f },
            "#D16830", // { 0.820f, 0.408f, 0.188f },
            "#942616", // { 0.581f, 0.149f, 0.087f }  // reddish
        };

        std::string color = Default_Colors[0];
        auto is_valid_color = [](const std::string& color) {
            auto is_hex_digit = [](char c) {
                return ((c >= '0' && c <= '9') ||
                        (c >= 'A' && c <= 'F') ||
                        (c >= 'a' && c <= 'f'));
            };

            if (color[0] != '#' || color.length() != 7)
                return false;
            for (int i = 1; i <= 6; ++i) {
                if (!is_hex_digit(color[i]))
                    return false;
            }
            return true;
        };

        std::vector<std::string> tokens;
        boost::split(tokens, comment, boost::is_any_of(","), boost::token_compress_on);
        if (tokens.size() > 1) {
            if (tokens[1][0] == 'T') {
                int eid;
                if (!parse_number(tokens[1].substr(1), eid) || eid < 0 || eid > 255) {
                    BOOST_LOG_TRIVIAL(error) << "GCodeProcessor encountered an invalid value for Color_Change (" << comment << ").";
                    return;
                }
                filament_id = static_cast<unsigned char>(eid);
            }
        }
        if (tokens.size() > 2) {
            if (is_valid_color(tokens[2]))
                color = tokens[2];
        }
        else {
            color = Default_Colors[m_last_default_color_id];
            ++m_last_default_color_id;
            if (m_last_default_color_id == Default_Colors.size())
                m_last_default_color_id = 0;
        }

        if (filament_id < m_extruder_colors.size())
            m_extruder_colors[filament_id] = static_cast<unsigned char>(m_extruder_offsets.size()) + m_cp_color.counter; // color_change position in list of color for preview
        ++m_cp_color.counter;
        if (m_cp_color.counter == UCHAR_MAX)
            m_cp_color.counter = 0;

        if (get_filament_id() == filament_id) {
            m_cp_color.current = m_extruder_colors[filament_id];
            store_move_vertex(EMoveType::Color_change);
            CustomGCode::Item item = { static_cast<double>(m_end_position[2]), CustomGCode::ColorChange, filament_id + 1, color, "" };
            m_result.custom_gcode_per_print_z.emplace_back(item);
            m_options_z_corrector.set();
            process_custom_gcode_time(CustomGCode::ColorChange);
            process_filaments(CustomGCode::ColorChange);
        }

        return;
    }

    // pause print tag
    if (comment == reserved_tag(ETags::Pause_Print)) {
        store_move_vertex(EMoveType::Pause_Print);
        CustomGCode::Item item = { static_cast<double>(m_end_position[2]), CustomGCode::PausePrint, get_filament_id() + 1, "", ""};
        m_result.custom_gcode_per_print_z.emplace_back(item);
        m_options_z_corrector.set();
        process_custom_gcode_time(CustomGCode::PausePrint);
        return;
    }

    // custom code tag
    if (comment == reserved_tag(ETags::Custom_Code)) {
        store_move_vertex(EMoveType::Custom_GCode);
        CustomGCode::Item item = { static_cast<double>(m_end_position[2]), CustomGCode::Custom, get_filament_id() + 1, "", ""};
        m_result.custom_gcode_per_print_z.emplace_back(item);
        m_options_z_corrector.set();
        return;
    }

    // layer change tag
    if (comment == reserved_tag(ETags::Layer_Change)) {
        ++m_layer_id;
        if (m_detect_layer_based_on_tag) {
            if (m_result.moves.empty() || m_result.spiral_vase_layers.empty())
                // add a placeholder for layer height. the actual value will be set inside process_G1() method
                m_result.spiral_vase_layers.push_back({ FLT_MAX, { 0, 0 } });
            else {
                const size_t move_id = m_result.moves.size() - 1 - m_seams_count;
                if (!m_result.spiral_vase_layers.empty())
                    m_result.spiral_vase_layers.back().second.second = move_id;
                // add a placeholder for layer height. the actual value will be set inside process_G1() method
                m_result.spiral_vase_layers.push_back({ FLT_MAX, { move_id, move_id } });
            }
        }
        return;
    }

#if ENABLE_GCODE_VIEWER_DATA_CHECKING
    // mm3_per_mm print tag
    if (boost::starts_with(comment, Mm3_Per_Mm_Tag)) {
        if (! parse_number(comment.substr(Mm3_Per_Mm_Tag.size()), m_mm3_per_mm_compare.last_tag_value))
            BOOST_LOG_TRIVIAL(error) << "GCodeProcessor encountered an invalid value for Mm3_Per_Mm (" << comment << ").";
        return;
    }
#endif // ENABLE_GCODE_VIEWER_DATA_CHECKING
}

bool GCodeProcessor::process_producers_tags(const std::string_view comment)
{
    switch (m_producer)
    {
    case EProducer::Slic3rPE:
    case EProducer::Slic3r:
    case EProducer::SuperSlicer:
    case EProducer::BambuStudio: { return process_bambuslicer_tags(comment); }
    case EProducer::Cura:        { return process_cura_tags(comment); }
    case EProducer::Simplify3D:  { return process_simplify3d_tags(comment); }
    case EProducer::CraftWare:   { return process_craftware_tags(comment); }
    case EProducer::ideaMaker:   { return process_ideamaker_tags(comment); }
    case EProducer::KissSlicer:  { return process_kissslicer_tags(comment); }
    default:                     { return false; }
    }
}

bool GCodeProcessor::process_bambuslicer_tags(const std::string_view comment)
{
    return false;
}

bool GCodeProcessor::process_cura_tags(const std::string_view comment)
{
    // TYPE -> extrusion role
    std::string tag = "TYPE:";
    size_t pos = comment.find(tag);
    if (pos != comment.npos) {
        const std::string_view type = comment.substr(pos + tag.length());
        if (type == "SKIRT")
            set_extrusion_role(erSkirt);
        else if (type == "WALL-OUTER")
            set_extrusion_role(erExternalPerimeter);
        else if (type == "WALL-INNER")
            set_extrusion_role(erPerimeter);
        else if (type == "SKIN")
            set_extrusion_role(erSolidInfill);
        else if (type == "FILL")
            set_extrusion_role(erInternalInfill);
        else if (type == "SUPPORT")
            set_extrusion_role(erSupportMaterial);
        else if (type == "SUPPORT-INTERFACE")
            set_extrusion_role(erSupportMaterialInterface);
        else if (type == "PRIME-TOWER")
            set_extrusion_role(erWipeTower);
        else {
            set_extrusion_role(erNone);
            BOOST_LOG_TRIVIAL(warning) << "GCodeProcessor found unknown extrusion role: " << type;
        }

        if (m_extrusion_role == erExternalPerimeter)
            m_seams_detector.activate(true);

        return true;
    }

    // flavor
    tag = "FLAVOR:";
    pos = comment.find(tag);
    if (pos != comment.npos) {
        const std::string_view flavor = comment.substr(pos + tag.length());
        if (flavor == "BFB")
            m_flavor = gcfMarlinLegacy; // is this correct ?
        else if (flavor == "Mach3")
            m_flavor = gcfMach3;
        else if (flavor == "Makerbot")
            m_flavor = gcfMakerWare;
        else if (flavor == "UltiGCode")
            m_flavor = gcfMarlinLegacy; // is this correct ?
        else if (flavor == "Marlin(Volumetric)")
            m_flavor = gcfMarlinLegacy; // is this correct ?
        else if (flavor == "Griffin")
            m_flavor = gcfMarlinLegacy; // is this correct ?
        else if (flavor == "Repetier")
            m_flavor = gcfRepetier;
        else if (flavor == "RepRap")
            m_flavor = gcfRepRapFirmware;
        else if (flavor == "Marlin")
            m_flavor = gcfMarlinLegacy;
        else
            BOOST_LOG_TRIVIAL(warning) << "GCodeProcessor found unknown flavor: " << flavor;

        return true;
    }

    // layer
    tag = "LAYER:";
    pos = comment.find(tag);
    if (pos != comment.npos) {
        ++m_layer_id;
        return true;
    }

    return false;
}

bool GCodeProcessor::process_simplify3d_tags(const std::string_view comment)
{
    // extrusion roles

    // in older versions the comments did not contain the key 'feature'
    std::string_view cmt = comment;
    size_t pos = cmt.find(" feature");
    if (pos == 0)
        cmt.remove_prefix(8);

    // ; skirt
    pos = cmt.find(" skirt");
    if (pos == 0) {
        set_extrusion_role(erSkirt);
        return true;
    }

    // ; outer perimeter
    pos = cmt.find(" outer perimeter");
    if (pos == 0) {
        set_extrusion_role(erExternalPerimeter);
        m_seams_detector.activate(true);
        return true;
    }

    // ; inner perimeter
    pos = cmt.find(" inner perimeter");
    if (pos == 0) {
        set_extrusion_role(erPerimeter);
        return true;
    }

    // ; gap fill
    pos = cmt.find(" gap fill");
    if (pos == 0) {
        set_extrusion_role(erGapFill);
        return true;
    }

    // ; infill
    pos = cmt.find(" infill");
    if (pos == 0) {
        set_extrusion_role(erInternalInfill);
        return true;
    }

    // ; solid layer
    pos = cmt.find(" solid layer");
    if (pos == 0) {
        set_extrusion_role(erSolidInfill);
        return true;
    }

    // ; bridge
    pos = cmt.find(" bridge");
    if (pos == 0) {
        set_extrusion_role(erBridgeInfill);
        return true;
    }

    // ; support
    pos = cmt.find(" support");
    if (pos == 0) {
        set_extrusion_role(erSupportMaterial);
        return true;
    }

    // ; dense support
    pos = cmt.find(" dense support");
    if (pos == 0) {
        set_extrusion_role(erSupportMaterialInterface);
        return true;
    }

    // ; prime pillar
    pos = cmt.find(" prime pillar");
    if (pos == 0) {
        set_extrusion_role(erWipeTower);
        return true;
    }

    // ; ooze shield
    pos = cmt.find(" ooze shield");
    if (pos == 0) {
        set_extrusion_role(erNone); // Missing mapping
        return true;
    }

    // ; raft
    pos = cmt.find(" raft");
    if (pos == 0) {
        set_extrusion_role(erSupportMaterial);
        return true;
    }

    // ; internal single extrusion
    pos = cmt.find(" internal single extrusion");
    if (pos == 0) {
        set_extrusion_role(erNone); // Missing mapping
        return true;
    }

    // geometry
    // ; tool
    std::string tag = " tool";
    pos = cmt.find(tag);
    if (pos == 0) {
        const std::string_view data = cmt.substr(pos + tag.length());
        std::string h_tag = "H";
        size_t h_start = data.find(h_tag);
        size_t h_end = data.find_first_of(' ', h_start);
        std::string w_tag = "W";
        size_t w_start = data.find(w_tag);
        size_t w_end = data.find_first_of(' ', w_start);
        if (h_start != data.npos) {
            if (!parse_number(data.substr(h_start + 1, (h_end != data.npos) ? h_end - h_start - 1 : h_end), m_forced_height))
                BOOST_LOG_TRIVIAL(error) << "GCodeProcessor encountered an invalid value for Height (" << comment << ").";
        }
        if (w_start != data.npos) {
            if (!parse_number(data.substr(w_start + 1, (w_end != data.npos) ? w_end - w_start - 1 : w_end), m_forced_width))
                BOOST_LOG_TRIVIAL(error) << "GCodeProcessor encountered an invalid value for Width (" << comment << ").";
        }

        return true;
    }

    // ; layer
    tag = " layer";
    pos = cmt.find(tag);
    if (pos == 0) {
        // skip lines "; layer end"
        const std::string_view data = cmt.substr(pos + tag.length());
        size_t end_start = data.find("end");
        if (end_start == data.npos)
            ++m_layer_id;

        return true;
    }

    return false;
}

bool GCodeProcessor::process_craftware_tags(const std::string_view comment)
{
    // segType -> extrusion role
    std::string tag = "segType:";
    size_t pos = comment.find(tag);
    if (pos != comment.npos) {
        const std::string_view type = comment.substr(pos + tag.length());
        if (type == "Skirt")
            set_extrusion_role(erSkirt);
        else if (type == "Perimeter")
            set_extrusion_role(erExternalPerimeter);
        else if (type == "HShell")
            set_extrusion_role(erNone); // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
        else if (type == "InnerHair")
            set_extrusion_role(erNone); // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
        else if (type == "Loop")
            set_extrusion_role(erNone); // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
        else if (type == "Infill")
            set_extrusion_role(erInternalInfill);
        else if (type == "Raft")
            set_extrusion_role(erSkirt);
        else if (type == "Support")
            set_extrusion_role(erSupportMaterial);
        else if (type == "SupportTouch")
            set_extrusion_role(erSupportMaterial);
        else if (type == "SoftSupport")
            set_extrusion_role(erSupportMaterialInterface);
        else if (type == "Pillar")
            set_extrusion_role(erWipeTower);
        else {
            set_extrusion_role(erNone);
            BOOST_LOG_TRIVIAL(warning) << "GCodeProcessor found unknown extrusion role: " << type;
        }

        if (m_extrusion_role == erExternalPerimeter)
            m_seams_detector.activate(true);

        return true;
    }

    // layer
    pos = comment.find(" Layer #");
    if (pos == 0) {
        ++m_layer_id;
        return true;
    }

    return false;
}

bool GCodeProcessor::process_ideamaker_tags(const std::string_view comment)
{
    // TYPE -> extrusion role
    std::string tag = "TYPE:";
    size_t pos = comment.find(tag);
    if (pos != comment.npos) {
        const std::string_view type = comment.substr(pos + tag.length());
        if (type == "RAFT")
            set_extrusion_role(erSkirt);
        else if (type == "WALL-OUTER")
            set_extrusion_role(erExternalPerimeter);
        else if (type == "WALL-INNER")
            set_extrusion_role(erPerimeter);
        else if (type == "SOLID-FILL")
            set_extrusion_role(erSolidInfill);
        else if (type == "FILL")
            set_extrusion_role(erInternalInfill);
        else if (type == "BRIDGE")
            set_extrusion_role(erBridgeInfill);
        else if (type == "SUPPORT")
            set_extrusion_role(erSupportMaterial);
        else {
            set_extrusion_role(erNone);
            BOOST_LOG_TRIVIAL(warning) << "GCodeProcessor found unknown extrusion role: " << type;
        }

        if (m_extrusion_role == erExternalPerimeter)
            m_seams_detector.activate(true);

        return true;
    }

    // geometry
    // width
    tag = "WIDTH:";
    pos = comment.find(tag);
    if (pos != comment.npos) {
        if (!parse_number(comment.substr(pos + tag.length()), m_forced_width))
            BOOST_LOG_TRIVIAL(error) << "GCodeProcessor encountered an invalid value for Width (" << comment << ").";
        return true;
    }

    // height
    tag = "HEIGHT:";
    pos = comment.find(tag);
    if (pos != comment.npos) {
        if (!parse_number(comment.substr(pos + tag.length()), m_forced_height))
            BOOST_LOG_TRIVIAL(error) << "GCodeProcessor encountered an invalid value for Height (" << comment << ").";
        return true;
    }

    // layer
    pos = comment.find("LAYER:");
    if (pos == 0) {
        ++m_layer_id;
        return true;
    }

    return false;
}

bool GCodeProcessor::process_kissslicer_tags(const std::string_view comment)
{
    // extrusion roles

    // ; 'Raft Path'
    size_t pos = comment.find(" 'Raft Path'");
    if (pos == 0) {
        set_extrusion_role(erSkirt);
        return true;
    }

    // ; 'Support Interface Path'
    pos = comment.find(" 'Support Interface Path'");
    if (pos == 0) {
        set_extrusion_role(erSupportMaterialInterface);
        return true;
    }

    // ; 'Travel/Ironing Path'
    pos = comment.find(" 'Travel/Ironing Path'");
    if (pos == 0) {
        set_extrusion_role(erIroning);
        return true;
    }

    // ; 'Support (may Stack) Path'
    pos = comment.find(" 'Support (may Stack) Path'");
    if (pos == 0) {
        set_extrusion_role(erSupportMaterial);
        return true;
    }

    // ; 'Perimeter Path'
    pos = comment.find(" 'Perimeter Path'");
    if (pos == 0) {
        set_extrusion_role(erExternalPerimeter);
        m_seams_detector.activate(true);
        return true;
    }

    // ; 'Pillar Path'
    pos = comment.find(" 'Pillar Path'");
    if (pos == 0) {
        set_extrusion_role(erNone); // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
        return true;
    }

    // ; 'Destring/Wipe/Jump Path'
    pos = comment.find(" 'Destring/Wipe/Jump Path'");
    if (pos == 0) {
        set_extrusion_role(erNone); // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
        return true;
    }

    // ; 'Prime Pillar Path'
    pos = comment.find(" 'Prime Pillar Path'");
    if (pos == 0) {
        set_extrusion_role(erNone); // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
        return true;
    }

    // ; 'Loop Path'
    pos = comment.find(" 'Loop Path'");
    if (pos == 0) {
        set_extrusion_role(erNone); // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
        return true;
    }

    // ; 'Crown Path'
    pos = comment.find(" 'Crown Path'");
    if (pos == 0) {
        set_extrusion_role(erNone); // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
        return true;
    }

    // ; 'Solid Path'
    pos = comment.find(" 'Solid Path'");
    if (pos == 0) {
        set_extrusion_role(erNone);
        return true;
    }

    // ; 'Stacked Sparse Infill Path'
    pos = comment.find(" 'Stacked Sparse Infill Path'");
    if (pos == 0) {
        set_extrusion_role(erInternalInfill);
        return true;
    }

    // ; 'Sparse Infill Path'
    pos = comment.find(" 'Sparse Infill Path'");
    if (pos == 0) {
        set_extrusion_role(erSolidInfill);
        return true;
    }

    // geometry

    // layer
    pos = comment.find(" BEGIN_LAYER_");
    if (pos == 0) {
        ++m_layer_id;
        return true;
    }

    return false;
}

bool GCodeProcessor::detect_producer(const std::string_view comment)
{
    for (const auto& [id, search_string] : Producers) {
        size_t pos = comment.find(search_string);
        if (pos != comment.npos) {
            m_producer = id;
            //BOOST_LOG_TRIVIAL(info) << "Detected gcode producer: " << search_string;
            return true;
        }
    }
    return false;
}

void GCodeProcessor::process_G0(const GCodeReader::GCodeLine& line)
{
    process_G1(line);
}

void GCodeProcessor::process_G1(const GCodeReader::GCodeLine& line)
{
    int filament_id = get_filament_id();
    int last_filament_id = get_last_filament_id();
    float filament_diameter = (static_cast<size_t>(filament_id) < m_result.filament_diameters.size()) ? m_result.filament_diameters[filament_id] : m_result.filament_diameters.back();
    float filament_radius = 0.5f * filament_diameter;
    float area_filament_cross_section = static_cast<float>(M_PI) * sqr(filament_radius);
    auto absolute_position = [this, area_filament_cross_section](Axis axis, const GCodeReader::GCodeLine& lineG1) {
        bool is_relative = (m_global_positioning_type == EPositioningType::Relative);
        if (axis == E)
            is_relative |= (m_e_local_positioning_type == EPositioningType::Relative);

        if (lineG1.has(Slic3r::Axis(axis))) {
            float lengthsScaleFactor = (m_units == EUnits::Inches) ? INCHES_TO_MM : 1.0f;
            float ret = lineG1.value(Slic3r::Axis(axis)) * lengthsScaleFactor;
            return is_relative ? m_start_position[axis] + ret : m_origin[axis] + ret;
        }
        else
            return m_start_position[axis];
    };

    auto move_type = [this](const AxisCoords& delta_pos) {
        EMoveType type = EMoveType::Noop;

        if (m_wiping)
            type = EMoveType::Wipe;
        else if (delta_pos[E] < 0.0f)
            type = (delta_pos[X] != 0.0f || delta_pos[Y] != 0.0f || delta_pos[Z] != 0.0f) ? EMoveType::Travel : EMoveType::Retract;
        else if (delta_pos[E] > 0.0f) {
            if (delta_pos[X] == 0.0f && delta_pos[Y] == 0.0f)
                type = (delta_pos[Z] == 0.0f) ? EMoveType::Unretract : EMoveType::Travel;
            else if (delta_pos[X] != 0.0f || delta_pos[Y] != 0.0f)
                type = EMoveType::Extrude;
        }
        else if (delta_pos[X] != 0.0f || delta_pos[Y] != 0.0f || delta_pos[Z] != 0.0f)
            type = EMoveType::Travel;

        return type;
    };

    ++m_g1_line_id;

    // enable processing of lines M201/M203/M204/M205
    m_time_processor.machine_envelope_processing_enabled = true;

    // updates axes positions from line
    for (unsigned char a = X; a <= E; ++a) {
        m_end_position[a] = absolute_position((Axis)a, line);
    }

    // updates feedrate from line, if present
    if (line.has_f())
        m_feedrate = line.f() * MMMIN_TO_MMSEC;

    // calculates movement deltas
    float max_abs_delta = 0.0f;
    AxisCoords delta_pos;
    for (unsigned char a = X; a <= E; ++a) {
        delta_pos[a] = m_end_position[a] - m_start_position[a];
        max_abs_delta = std::max<float>(max_abs_delta, std::abs(delta_pos[a]));
    }

    // no displacement, return
    if (max_abs_delta == 0.0f)
        return;

    EMoveType type = move_type(delta_pos);
    if (type == EMoveType::Extrude) {
        float delta_xyz = std::sqrt(sqr(delta_pos[X]) + sqr(delta_pos[Y]) + sqr(delta_pos[Z]));
        float volume_extruded_filament = area_filament_cross_section * delta_pos[E];
        float area_toolpath_cross_section = volume_extruded_filament / delta_xyz;

        if(m_extrusion_role == ExtrusionRole::erSupportMaterial || m_extrusion_role == ExtrusionRole::erSupportMaterialInterface || m_extrusion_role ==ExtrusionRole::erSupportTransition)
            m_used_filaments.increase_support_caches(volume_extruded_filament);
        else if (m_extrusion_role==ExtrusionRole::erWipeTower) {
            m_used_filaments.increase_wipe_tower_caches(volume_extruded_filament);
        }
        else {
            // save extruded volume to the cache
            m_used_filaments.increase_model_caches(volume_extruded_filament);
        }
        // volume extruded filament / tool displacement = area toolpath cross section
        m_mm3_per_mm = area_toolpath_cross_section;
#if ENABLE_GCODE_VIEWER_DATA_CHECKING
        m_mm3_per_mm_compare.update(area_toolpath_cross_section, m_extrusion_role);
#endif // ENABLE_GCODE_VIEWER_DATA_CHECKING

        if (m_forced_height > 0.0f)
            m_height = m_forced_height;
        else {
            if (m_end_position[Z] > m_extruded_last_z + EPSILON)
                m_height = m_end_position[Z] - m_extruded_last_z;
        }

        if (m_height == 0.0f)
            m_height = DEFAULT_TOOLPATH_HEIGHT;

        if (m_end_position[Z] == 0.0f)
            m_end_position[Z] = m_height;

        m_extruded_last_z = m_end_position[Z];
        m_options_z_corrector.update(m_height);

#if ENABLE_GCODE_VIEWER_DATA_CHECKING
        m_height_compare.update(m_height, m_extrusion_role);
#endif // ENABLE_GCODE_VIEWER_DATA_CHECKING

        if (m_forced_width > 0.0f)
            m_width = m_forced_width;
        else if (m_extrusion_role == erExternalPerimeter)
            // cross section: rectangle
            m_width = delta_pos[E] * static_cast<float>(M_PI * sqr(1.05f * filament_radius)) / (delta_xyz * m_height);
        else if (m_extrusion_role == erBridgeInfill || m_extrusion_role == erNone)
            // cross section: circle
            m_width = static_cast<float>(m_result.filament_diameters[filament_id]) * std::sqrt(delta_pos[E] / delta_xyz);
        else
            // cross section: rectangle + 2 semicircles
            m_width = delta_pos[E] * static_cast<float>(M_PI * sqr(filament_radius)) / (delta_xyz * m_height) + static_cast<float>(1.0 - 0.25 * M_PI) * m_height;

        if (m_width == 0.0f)
            m_width = DEFAULT_TOOLPATH_WIDTH;

        // clamp width to avoid artifacts which may arise from wrong values of m_height
        m_width = std::min(m_width, std::max(2.0f, 4.0f * m_height));

#if ENABLE_GCODE_VIEWER_DATA_CHECKING
        m_width_compare.update(m_width, m_extrusion_role);
#endif // ENABLE_GCODE_VIEWER_DATA_CHECKING
    }
    else if (type == EMoveType::Unretract && m_flushing) {
        int extruder_id = get_extruder_id();
        float volume_flushed_filament = area_filament_cross_section * delta_pos[E];
        if (m_remaining_volume[extruder_id] > volume_flushed_filament)
        {
            m_used_filaments.update_flush_per_filament(last_filament_id, volume_flushed_filament);
            m_remaining_volume[extruder_id] -= volume_flushed_filament;
        }
        else {
            m_used_filaments.update_flush_per_filament(last_filament_id, m_remaining_volume[extruder_id]);
            m_used_filaments.update_flush_per_filament(filament_id, volume_flushed_filament - m_remaining_volume[extruder_id]);
            m_remaining_volume[extruder_id] = 0.f;
        }
    }

    // time estimate section
    auto move_length = [](const AxisCoords& delta_pos) {
        float sq_xyz_length = sqr(delta_pos[X]) + sqr(delta_pos[Y]) + sqr(delta_pos[Z]);
        return (sq_xyz_length > 0.0f) ? std::sqrt(sq_xyz_length) : std::abs(delta_pos[E]);
    };

    auto is_extrusion_only_move = [](const AxisCoords& delta_pos) {
        return delta_pos[X] == 0.0f && delta_pos[Y] == 0.0f && delta_pos[Z] == 0.0f && delta_pos[E] != 0.0f;
    };

    float distance = move_length(delta_pos);
    assert(distance != 0.0f);
    float inv_distance = 1.0f / distance;

    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        TimeMachine& machine = m_time_processor.machines[i];
        if (!machine.enabled)
            continue;

        TimeMachine::State& curr = machine.curr;
        TimeMachine::State& prev = machine.prev;
        std::vector<TimeBlock>& blocks = machine.blocks;

        curr.feedrate = (delta_pos[E] == 0.0f) ?
            minimum_travel_feedrate(static_cast<PrintEstimatedStatistics::ETimeMode>(i), m_feedrate) :
            minimum_feedrate(static_cast<PrintEstimatedStatistics::ETimeMode>(i), m_feedrate);

        //BBS: calculeta enter and exit direction
        curr.enter_direction = { static_cast<float>(delta_pos[X]), static_cast<float>(delta_pos[Y]), static_cast<float>(delta_pos[Z]) };
        float norm = curr.enter_direction.norm();
        if (!is_extrusion_only_move(delta_pos))
            curr.enter_direction = curr.enter_direction / norm;
        curr.exit_direction = curr.enter_direction;

        TimeBlock block;
        block.move_type = type;
        block.skippable_type = m_skippable_type;
        //BBS: don't calculate travel time into extrusion path, except travel inside start and end gcode.
        block.role = (type != EMoveType::Travel || m_extrusion_role == erCustom) ? m_extrusion_role : erNone;
        block.distance = distance;
        block.move_id = m_result.moves.size(); // new move will be pushed back at the end of the func, so use size of move as idx
        block.g1_line_id = m_g1_line_id;
        block.layer_id = std::max<unsigned int>(1, m_layer_id);
        block.flags.prepare_stage = m_processing_start_custom_gcode;

        //BBS: limite the cruise according to centripetal acceleration
        //Only need to handle when both prev and curr segment has movement in x-y plane
        if ((prev.exit_direction(0) != 0.0f || prev.exit_direction(1) != 0.0f) &&
            (curr.enter_direction(0) != 0.0f || curr.enter_direction(1) != 0.0f)) {
            Vec3f v1 = prev.exit_direction;
            v1(2, 0) = 0.0f;
            v1.normalize();
            Vec3f v2 = curr.enter_direction;
            v2(2, 0) = 0.0f;
            v2.normalize();
            float norm_diff = (v2 - v1).norm();
            //BBS: don't need to consider limitation of centripetal acceleration
            //when angle changing is larger than 28.96 degree or two lines are almost collinear.
            //Attention!!! these two value must be same with MC side.
            if (norm_diff < 0.5f && norm_diff > 0.00001f) {
                //BBS: calculate angle
                float dot = v1(0) * v2(0) + v1(1) * v2(1);
                float cross = v1(0) * v2(1) - v1(1) * v2(0);
                float angle = float(atan2(double(cross), double(dot)));
                float sin_theta_2 = sqrt((1.0f - cos(angle)) * 0.5f);
                float r = sqrt(sqr(delta_pos[X]) + sqr(delta_pos[Y])) * 0.5 / sin_theta_2;
                float acc = get_acceleration(static_cast<PrintEstimatedStatistics::ETimeMode>(i));
                curr.feedrate = std::min(curr.feedrate, sqrt(acc * r));
            }
        }

        // calculates block cruise feedrate
        float min_feedrate_factor = 1.0f;
        for (unsigned char a = X; a <= E; ++a) {
            curr.axis_feedrate[a] = curr.feedrate * delta_pos[a] * inv_distance;
            if (a == E)
                curr.axis_feedrate[a] *= machine.extrude_factor_override_percentage;

            curr.abs_axis_feedrate[a] = std::abs(curr.axis_feedrate[a]);
            if (curr.abs_axis_feedrate[a] != 0.0f) {
                float axis_max_feedrate = get_axis_max_feedrate(static_cast<PrintEstimatedStatistics::ETimeMode>(i), static_cast<Axis>(a), m_extruder_id);
                if (axis_max_feedrate != 0.0f) min_feedrate_factor = std::min<float>(min_feedrate_factor, axis_max_feedrate / curr.abs_axis_feedrate[a]);
            }
        }
        //BBS: update curr.feedrate
        curr.feedrate *= min_feedrate_factor;
        block.feedrate_profile.cruise = curr.feedrate;

        if (min_feedrate_factor < 1.0f) {
            for (unsigned char a = X; a <= E; ++a) {
                curr.axis_feedrate[a] *= min_feedrate_factor;
                curr.abs_axis_feedrate[a] *= min_feedrate_factor;
            }
        }

        // calculates block acceleration
        float acceleration =
            (type == EMoveType::Travel) ? get_travel_acceleration(static_cast<PrintEstimatedStatistics::ETimeMode>(i)) :
            (is_extrusion_only_move(delta_pos) ?
                get_retract_acceleration(static_cast<PrintEstimatedStatistics::ETimeMode>(i)) :
                get_acceleration(static_cast<PrintEstimatedStatistics::ETimeMode>(i)));

        //BBS
        for (unsigned char a = X; a <= E; ++a) {
            float axis_max_acceleration = get_axis_max_acceleration(static_cast<PrintEstimatedStatistics::ETimeMode>(i), static_cast<Axis>(a), m_extruder_id);
            if (acceleration * std::abs(delta_pos[a]) * inv_distance > axis_max_acceleration)
                acceleration = axis_max_acceleration / (std::abs(delta_pos[a]) * inv_distance);
        }

        block.acceleration = acceleration;

        // calculates block exit feedrate
        curr.safe_feedrate = block.feedrate_profile.cruise;

        for (unsigned char a = X; a <= E; ++a) {
            float axis_max_jerk = get_axis_max_jerk(static_cast<PrintEstimatedStatistics::ETimeMode>(i), static_cast<Axis>(a));
            if (curr.abs_axis_feedrate[a] > axis_max_jerk)
                curr.safe_feedrate = std::min(curr.safe_feedrate, axis_max_jerk);
        }

        block.feedrate_profile.exit = curr.safe_feedrate;

        static const float PREVIOUS_FEEDRATE_THRESHOLD = 0.0001f;

        // calculates block entry feedrate
        float vmax_junction = curr.safe_feedrate;
        if (!blocks.empty() && prev.feedrate > PREVIOUS_FEEDRATE_THRESHOLD) {
            bool prev_speed_larger = prev.feedrate > block.feedrate_profile.cruise;
            float smaller_speed_factor = prev_speed_larger ? (block.feedrate_profile.cruise / prev.feedrate) : (prev.feedrate / block.feedrate_profile.cruise);
            // Pick the smaller of the nominal speeds. Higher speed shall not be achieved at the junction during coasting.
            vmax_junction = prev_speed_larger ? block.feedrate_profile.cruise : prev.feedrate;

            float v_factor = 1.0f;
            bool limited = false;

            for (unsigned char a = X; a <= E; ++a) {
                // Limit an axis. We have to differentiate coasting from the reversal of an axis movement, or a full stop.
                if (a == X) {
                    Vec3f exit_v = prev.feedrate * (prev.exit_direction);
                    if (prev_speed_larger)
                        exit_v *= smaller_speed_factor;
                    Vec3f entry_v = block.feedrate_profile.cruise * (curr.enter_direction);
                    Vec3f jerk_v = entry_v - exit_v;
                    jerk_v = Vec3f(abs(jerk_v.x()), abs(jerk_v.y()), abs(jerk_v.z()));
                    Vec3f max_xyz_jerk_v = get_xyz_max_jerk(static_cast<PrintEstimatedStatistics::ETimeMode>(i));

                    for (size_t i = 0; i < 3; i++)
                    {
                        if (jerk_v[i] > max_xyz_jerk_v[i]) {
                            v_factor *= max_xyz_jerk_v[i] / jerk_v[i];
                            jerk_v *= v_factor;
                            limited = true;
                        }
                    }
                }
                else if (a == Y || a == Z) {
                    continue;
                }
                else {
                    float v_exit = prev.axis_feedrate[a];
                    float v_entry = curr.axis_feedrate[a];

                    if (prev_speed_larger)
                        v_exit *= smaller_speed_factor;

                    if (limited) {
                        v_exit *= v_factor;
                        v_entry *= v_factor;
                    }

                    // Calculate the jerk depending on whether the axis is coasting in the same direction or reversing a direction.
                    float jerk =
                        (v_exit > v_entry) ?
                        (((v_entry > 0.0f) || (v_exit < 0.0f)) ?
                            // coasting
                            (v_exit - v_entry) :
                            // axis reversal
                            std::max(v_exit, -v_entry)) :
                        // v_exit <= v_entry
                        (((v_entry < 0.0f) || (v_exit > 0.0f)) ?
                            // coasting
                            (v_entry - v_exit) :
                            // axis reversal
                            std::max(-v_exit, v_entry));


                    float axis_max_jerk = get_axis_max_jerk(static_cast<PrintEstimatedStatistics::ETimeMode>(i), static_cast<Axis>(a));
                    if (jerk > axis_max_jerk) {
                        v_factor *= axis_max_jerk / jerk;
                        limited = true;
                    }
                }
            }

            if (limited)
                vmax_junction *= v_factor;

            // Now the transition velocity is known, which maximizes the shared exit / entry velocity while
            // respecting the jerk factors, it may be possible, that applying separate safe exit / entry velocities will achieve faster prints.
            float vmax_junction_threshold = vmax_junction * 0.99f;

            // Not coasting. The machine will stop and start the movements anyway, better to start the segment from start.
            if (prev.safe_feedrate > vmax_junction_threshold && curr.safe_feedrate > vmax_junction_threshold)
                vmax_junction = curr.safe_feedrate;
        }

        float v_allowable = max_allowable_speed(-acceleration, curr.safe_feedrate, block.distance);
        block.feedrate_profile.entry = std::min(vmax_junction, v_allowable);

        block.max_entry_speed = vmax_junction;
        block.flags.nominal_length = (block.feedrate_profile.cruise <= v_allowable);
        block.flags.recalculate = true;
        block.safe_feedrate = curr.safe_feedrate;

        // calculates block trapezoid
        block.calculate_trapezoid();

        // updates previous
        prev = curr;

        blocks.push_back(block);

        if (blocks.size() > TimeProcessor::Planner::refresh_threshold) {
            machine.calculate_time(TimeProcessor::Planner::queue_size, 0, erNone, [&result=m_result, i,&machine](const TimeBlock& block, int time) {
                machine.handle_time_block(block,time,i,result);
            });
        }
    }

    if (m_seams_detector.is_active()) {
        // check for seam starting vertex
        if (type == EMoveType::Extrude && m_extrusion_role == erExternalPerimeter && !m_seams_detector.has_first_vertex()) {
            //BBS: m_result.moves.back().position has plate offset, must minus plate offset before calculate the real seam position
            const Vec3f real_first_pos = Vec3f(m_result.moves.back().position.x() - m_x_offset, m_result.moves.back().position.y() - m_y_offset, m_result.moves.back().position.z());
            m_seams_detector.set_first_vertex(real_first_pos - m_extruder_offsets[filament_id]);
        } else if (type == EMoveType::Extrude && m_extrusion_role == erExternalPerimeter && m_detect_layer_based_on_tag) {
            const Vec3f real_last_pos = Vec3f(m_result.moves.back().position.x() - m_x_offset, m_result.moves.back().position.y() - m_y_offset,
                                              m_result.moves.back().position.z());
            const Vec3f new_pos       = real_last_pos - m_extruder_offsets[filament_id];
            // We may have sloped loop, drop any previous start pos if we have z increment
            const std::optional<Vec3f> first_vertex = m_seams_detector.get_first_vertex();
            if (new_pos.z() > first_vertex->z()) {
                m_seams_detector.set_first_vertex(new_pos);
            }
        }
        // check for seam ending vertex and store the resulting move
        else if ((type != EMoveType::Extrude || (m_extrusion_role != erExternalPerimeter && m_extrusion_role != erOverhangPerimeter)) && m_seams_detector.has_first_vertex()) {
            auto set_end_position = [this](const Vec3f& pos) {
                m_end_position[X] = pos.x(); m_end_position[Y] = pos.y(); m_end_position[Z] = pos.z();
            };

            const Vec3f curr_pos(m_end_position[X], m_end_position[Y], m_end_position[Z]);
            //BBS: m_result.moves.back().position has plate offset, must minus plate offset before calculate the real seam position
            const Vec3f real_last_pos = Vec3f(m_result.moves.back().position.x() - m_x_offset, m_result.moves.back().position.y() - m_y_offset, m_result.moves.back().position.z());
            const Vec3f new_pos = real_last_pos - m_extruder_offsets[filament_id];
            const std::optional<Vec3f> first_vertex = m_seams_detector.get_first_vertex();
            // the threshold value = 0.0625f == 0.25 * 0.25 is arbitrary, we may find some smarter condition later

            if ((new_pos - *first_vertex).squaredNorm() < 0.0625f) {
                set_end_position(0.5f * (new_pos + *first_vertex));
                store_move_vertex(EMoveType::Seam);
                set_end_position(curr_pos);
            }

            m_seams_detector.activate(false);
        }
    }
    else if (type == EMoveType::Extrude && m_extrusion_role == erExternalPerimeter) {
        m_seams_detector.activate(true);
        Vec3f plate_offset = {(float) m_x_offset, (float) m_y_offset, 0.0f};
        m_seams_detector.set_first_vertex(m_result.moves.back().position - m_extruder_offsets[filament_id] - plate_offset);
    }

    if (m_detect_layer_based_on_tag && !m_result.spiral_vase_layers.empty()) {
        if (delta_pos[Z] >= 0.0 && type == EMoveType::Extrude) {
            const float current_z = static_cast<float>(m_end_position[Z]);
            // replace layer height placeholder with correct value
            if (m_result.spiral_vase_layers.back().first == FLT_MAX) {
                m_result.spiral_vase_layers.back().first = current_z;
            } else {
                m_result.spiral_vase_layers.back().first = std::max(m_result.spiral_vase_layers.back().first, current_z);
            }
        }
        if (!m_result.moves.empty())
            m_result.spiral_vase_layers.back().second.second = m_result.moves.size() - 1 - m_seams_count;
    }

    // store move
    store_move_vertex(type);
}

void GCodeProcessor::process_VG1(const GCodeReader::GCodeLine& line)
{
    int filament_id = get_filament_id();
    int last_filament_id = get_last_filament_id();
    float filament_diameter = (static_cast<size_t>(filament_id) < m_result.filament_diameters.size()) ? m_result.filament_diameters[filament_id] : m_result.filament_diameters.back();
    float filament_radius = 0.5f * filament_diameter;
    float area_filament_cross_section = static_cast<float>(M_PI) * sqr(filament_radius);

    auto absolute_position = [this, area_filament_cross_section](Axis axis, const GCodeReader::GCodeLine& lineG1) {
        bool is_relative = (m_global_positioning_type == EPositioningType::Relative);
        if (axis == E)
            is_relative |= (m_e_local_positioning_type == EPositioningType::Relative);

        if (lineG1.has(Slic3r::Axis(axis))) {
            float lengthsScaleFactor = (m_units == EUnits::Inches) ? INCHES_TO_MM : 1.0f;
            float ret = lineG1.value(Slic3r::Axis(axis)) * lengthsScaleFactor;
            return is_relative ? m_start_position[axis] + ret : m_origin[axis] + ret;
        }
        else
            return m_start_position[axis];
        };

    auto move_type = [this](const AxisCoords& delta_pos) {
        EMoveType type = EMoveType::Noop;

        if (m_wiping)
            type = EMoveType::Wipe;
        else if (delta_pos[E] < 0.0f)
            type = (delta_pos[X] != 0.0f || delta_pos[Y] != 0.0f || delta_pos[Z] != 0.0f) ? EMoveType::Travel : EMoveType::Retract;
        else if (delta_pos[E] > 0.0f) {
            if (delta_pos[X] == 0.0f && delta_pos[Y] == 0.0f)
                type = (delta_pos[Z] == 0.0f) ? EMoveType::Unretract : EMoveType::Travel;
            else if (delta_pos[X] != 0.0f || delta_pos[Y] != 0.0f)
                type = EMoveType::Extrude;
        }
        else if (delta_pos[X] != 0.0f || delta_pos[Y] != 0.0f || delta_pos[Z] != 0.0f)
            type = EMoveType::Travel;

        return type;
        };

    ++m_g1_line_id;

    // enable processing of lines M201/M203/M204/M205
    m_time_processor.machine_envelope_processing_enabled = true;

    // updates axes positions from line
    for (unsigned char a = X; a <= E; ++a) {
        m_end_position[a] = absolute_position((Axis)a, line);
    }

    // updates feedrate from line, if present
    if (line.has_f())
        m_feedrate = line.f() * MMMIN_TO_MMSEC;

    // calculates movement deltas
    float max_abs_delta = 0.0f;
    AxisCoords delta_pos;
    for (unsigned char a = X; a <= E; ++a) {
        delta_pos[a] = m_end_position[a] - m_start_position[a];
        max_abs_delta = std::max<float>(max_abs_delta, std::abs(delta_pos[a]));
    }

    // no displacement, return
    if (max_abs_delta == 0.0f)
        return;

    EMoveType type = move_type(delta_pos);
    if (type == EMoveType::Extrude) {
        float delta_xyz = std::sqrt(sqr(delta_pos[X]) + sqr(delta_pos[Y]) + sqr(delta_pos[Z]));
        float volume_extruded_filament = area_filament_cross_section * delta_pos[E];
        float area_toolpath_cross_section = volume_extruded_filament / delta_xyz;

        if(m_extrusion_role == ExtrusionRole::erSupportMaterial || m_extrusion_role == ExtrusionRole::erSupportMaterialInterface || m_extrusion_role ==ExtrusionRole::erSupportTransition)
            m_used_filaments.increase_support_caches(volume_extruded_filament);
        else if (m_extrusion_role==ExtrusionRole::erWipeTower) {
            m_used_filaments.increase_wipe_tower_caches(volume_extruded_filament);
        }
        else {
            // save extruded volume to the cache
            m_used_filaments.increase_model_caches(volume_extruded_filament);
        }
        // volume extruded filament / tool displacement = area toolpath cross section
        m_mm3_per_mm = area_toolpath_cross_section;
#if ENABLE_GCODE_VIEWER_DATA_CHECKING
        m_mm3_per_mm_compare.update(area_toolpath_cross_section, m_extrusion_role);
#endif // ENABLE_GCODE_VIEWER_DATA_CHECKING

        if (m_forced_height > 0.0f)
            m_height = m_forced_height;
        else {
            if (m_end_position[Z] > m_extruded_last_z + EPSILON)
                m_height = m_end_position[Z] - m_extruded_last_z;
        }

        if (m_height == 0.0f)
            m_height = DEFAULT_TOOLPATH_HEIGHT;

        if (m_end_position[Z] == 0.0f)
            m_end_position[Z] = m_height;

        m_extruded_last_z = m_end_position[Z];
        m_options_z_corrector.update(m_height);

#if ENABLE_GCODE_VIEWER_DATA_CHECKING
        m_height_compare.update(m_height, m_extrusion_role);
#endif // ENABLE_GCODE_VIEWER_DATA_CHECKING

        if (m_forced_width > 0.0f)
            m_width = m_forced_width;
        else if (m_extrusion_role == erExternalPerimeter)
            // cross section: rectangle
            m_width = delta_pos[E] * static_cast<float>(M_PI * sqr(1.05f * filament_radius)) / (delta_xyz * m_height);
        else if (m_extrusion_role == erBridgeInfill || m_extrusion_role == erNone)
            // cross section: circle
            m_width = static_cast<float>(m_result.filament_diameters[filament_id]) * std::sqrt(delta_pos[E] / delta_xyz);
        else
            // cross section: rectangle + 2 semicircles
            m_width = delta_pos[E] * static_cast<float>(M_PI * sqr(filament_radius)) / (delta_xyz * m_height) + static_cast<float>(1.0 - 0.25 * M_PI) * m_height;

        if (m_width == 0.0f)
            m_width = DEFAULT_TOOLPATH_WIDTH;

        // clamp width to avoid artifacts which may arise from wrong values of m_height
        m_width = std::min(m_width, std::max(2.0f, 4.0f * m_height));

#if ENABLE_GCODE_VIEWER_DATA_CHECKING
        m_width_compare.update(m_width, m_extrusion_role);
#endif // ENABLE_GCODE_VIEWER_DATA_CHECKING
    }
    else if (EMoveType::Unretract == type && m_virtual_flushing) {
        int extruder_id = get_extruder_id();
        float volume_flushed_filament = area_filament_cross_section * delta_pos[E];
        if (m_remaining_volume[extruder_id] > volume_flushed_filament)
        {
            m_used_filaments.update_flush_per_filament(last_filament_id, volume_flushed_filament);
            m_remaining_volume[extruder_id] -= volume_flushed_filament;
        }
        else {
            m_used_filaments.update_flush_per_filament(last_filament_id, m_remaining_volume[extruder_id]);
            m_used_filaments.update_flush_per_filament(filament_id, volume_flushed_filament - m_remaining_volume[extruder_id]);
            m_remaining_volume[extruder_id] = 0.f;
        }
    }

    if (line.has_f())
        m_feedrate = line.f() * MMMIN_TO_MMSEC;

    // time estimate section
    auto move_length = [](const AxisCoords& delta_pos) {
        float sq_xyz_length = sqr(delta_pos[X]) + sqr(delta_pos[Y]) + sqr(delta_pos[Z]);
        return (sq_xyz_length > 0.0f) ? std::sqrt(sq_xyz_length) : std::abs(delta_pos[E]);
    };

    auto is_extrusion_only_move = [](const AxisCoords& delta_pos) {
        return delta_pos[X] == 0.0f && delta_pos[Y] == 0.0f && delta_pos[Z] == 0.0f && delta_pos[E] != 0.0f;
    };

    float distance = move_length(delta_pos);
    assert(distance != 0.0f);
    float inv_distance = 1.0f / distance;

    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        TimeMachine& machine = m_time_processor.machines[i];
        if (!machine.enabled)
            continue;

        TimeMachine::State& curr = machine.curr;
        TimeMachine::State& prev = machine.prev;
        std::vector<TimeBlock>& blocks = machine.blocks;

        curr.feedrate = (delta_pos[E] == 0.0f) ?
            minimum_travel_feedrate(static_cast<PrintEstimatedStatistics::ETimeMode>(i), m_feedrate) :
            minimum_feedrate(static_cast<PrintEstimatedStatistics::ETimeMode>(i), m_feedrate);

        //BBS: calculeta enter and exit direction
        curr.enter_direction = { static_cast<float>(delta_pos[X]), static_cast<float>(delta_pos[Y]), static_cast<float>(delta_pos[Z]) };
        float norm = curr.enter_direction.norm();
        if (!is_extrusion_only_move(delta_pos))
            curr.enter_direction = curr.enter_direction / norm;
        curr.exit_direction = curr.enter_direction;

        TimeBlock block;
        block.move_type = type;
        block.skippable_type = m_skippable_type;
        //BBS: don't calculate travel time into extrusion path, except travel inside start and end gcode.
        block.role = (type != EMoveType::Travel || m_extrusion_role == erCustom) ? m_extrusion_role : erNone;
        block.distance = distance;
        block.move_id = m_result.moves.size();
        block.g1_line_id = m_g1_line_id;
        block.layer_id = std::max<unsigned int>(1, m_layer_id);
        block.flags.prepare_stage = m_processing_start_custom_gcode;

        //BBS: limite the cruise according to centripetal acceleration
        //Only need to handle when both prev and curr segment has movement in x-y plane
        if ((prev.exit_direction(0) != 0.0f || prev.exit_direction(1) != 0.0f) &&
            (curr.enter_direction(0) != 0.0f || curr.enter_direction(1) != 0.0f)) {
            Vec3f v1 = prev.exit_direction;
            v1(2, 0) = 0.0f;
            v1.normalize();
            Vec3f v2 = curr.enter_direction;
            v2(2, 0) = 0.0f;
            v2.normalize();
            float norm_diff = (v2 - v1).norm();
            //BBS: don't need to consider limitation of centripetal acceleration
            //when angle changing is larger than 28.96 degree or two lines are almost collinear.
            //Attention!!! these two value must be same with MC side.
            if (norm_diff < 0.5f && norm_diff > 0.00001f) {
                //BBS: calculate angle
                float dot = v1(0) * v2(0) + v1(1) * v2(1);
                float cross = v1(0) * v2(1) - v1(1) * v2(0);
                float angle = float(atan2(double(cross), double(dot)));
                float sin_theta_2 = sqrt((1.0f - cos(angle)) * 0.5f);
                float r = sqrt(sqr(delta_pos[X]) + sqr(delta_pos[Y])) * 0.5 / sin_theta_2;
                float acc = get_acceleration(static_cast<PrintEstimatedStatistics::ETimeMode>(i));
                curr.feedrate = std::min(curr.feedrate, sqrt(acc * r));
            }
        }

        // calculates block cruise feedrate
        float min_feedrate_factor = 1.0f;
        for (unsigned char a = X; a <= E; ++a) {
            curr.axis_feedrate[a] = curr.feedrate * delta_pos[a] * inv_distance;
            if (a == E)
                curr.axis_feedrate[a] *= machine.extrude_factor_override_percentage;

            curr.abs_axis_feedrate[a] = std::abs(curr.axis_feedrate[a]);
            if (curr.abs_axis_feedrate[a] != 0.0f) {
                float axis_max_feedrate = get_axis_max_feedrate(static_cast<PrintEstimatedStatistics::ETimeMode>(i), static_cast<Axis>(a), m_extruder_id);
                if (axis_max_feedrate != 0.0f) min_feedrate_factor = std::min<float>(min_feedrate_factor, axis_max_feedrate / curr.abs_axis_feedrate[a]);
            }
        }
        //BBS: update curr.feedrate
        curr.feedrate *= min_feedrate_factor;
        block.feedrate_profile.cruise = curr.feedrate;

        if (min_feedrate_factor < 1.0f) {
            for (unsigned char a = X; a <= E; ++a) {
                curr.axis_feedrate[a] *= min_feedrate_factor;
                curr.abs_axis_feedrate[a] *= min_feedrate_factor;
            }
        }

        // calculates block acceleration
        float acceleration =
            (type == EMoveType::Travel) ? get_travel_acceleration(static_cast<PrintEstimatedStatistics::ETimeMode>(i)) :
            (is_extrusion_only_move(delta_pos) ?
                get_retract_acceleration(static_cast<PrintEstimatedStatistics::ETimeMode>(i)) :
                get_acceleration(static_cast<PrintEstimatedStatistics::ETimeMode>(i)));

        //BBS
        for (unsigned char a = X; a <= E; ++a) {
            float axis_max_acceleration = get_axis_max_acceleration(static_cast<PrintEstimatedStatistics::ETimeMode>(i), static_cast<Axis>(a), m_extruder_id);
            if (acceleration * std::abs(delta_pos[a]) * inv_distance > axis_max_acceleration)
                acceleration = axis_max_acceleration / (std::abs(delta_pos[a]) * inv_distance);
        }

        block.acceleration = acceleration;

        // calculates block exit feedrate
        curr.safe_feedrate = block.feedrate_profile.cruise;

        for (unsigned char a = X; a <= E; ++a) {
            float axis_max_jerk = get_axis_max_jerk(static_cast<PrintEstimatedStatistics::ETimeMode>(i), static_cast<Axis>(a));
            if (curr.abs_axis_feedrate[a] > axis_max_jerk)
                curr.safe_feedrate = std::min(curr.safe_feedrate, axis_max_jerk);
        }

        block.feedrate_profile.exit = curr.safe_feedrate;

        static const float PREVIOUS_FEEDRATE_THRESHOLD = 0.0001f;

        // calculates block entry feedrate
        float vmax_junction = curr.safe_feedrate;
        if (!blocks.empty() && prev.feedrate > PREVIOUS_FEEDRATE_THRESHOLD) {
            bool prev_speed_larger = prev.feedrate > block.feedrate_profile.cruise;
            float smaller_speed_factor = prev_speed_larger ? (block.feedrate_profile.cruise / prev.feedrate) : (prev.feedrate / block.feedrate_profile.cruise);
            // Pick the smaller of the nominal speeds. Higher speed shall not be achieved at the junction during coasting.
            vmax_junction = prev_speed_larger ? block.feedrate_profile.cruise : prev.feedrate;

            float v_factor = 1.0f;
            bool limited = false;

            for (unsigned char a = X; a <= E; ++a) {
                // Limit an axis. We have to differentiate coasting from the reversal of an axis movement, or a full stop.
                if (a == X) {
                    Vec3f exit_v = prev.feedrate * (prev.exit_direction);
                    if (prev_speed_larger)
                        exit_v *= smaller_speed_factor;
                    Vec3f entry_v = block.feedrate_profile.cruise * (curr.enter_direction);
                    Vec3f jerk_v = entry_v - exit_v;
                    jerk_v = Vec3f(abs(jerk_v.x()), abs(jerk_v.y()), abs(jerk_v.z()));
                    Vec3f max_xyz_jerk_v = get_xyz_max_jerk(static_cast<PrintEstimatedStatistics::ETimeMode>(i));

                    for (size_t i = 0; i < 3; i++)
                    {
                        if (jerk_v[i] > max_xyz_jerk_v[i]) {
                            v_factor *= max_xyz_jerk_v[i] / jerk_v[i];
                            jerk_v *= v_factor;
                            limited = true;
                        }
                    }
                }
                else if (a == Y || a == Z) {
                    continue;
                }
                else {
                    float v_exit = prev.axis_feedrate[a];
                    float v_entry = curr.axis_feedrate[a];

                    if (prev_speed_larger)
                        v_exit *= smaller_speed_factor;

                    if (limited) {
                        v_exit *= v_factor;
                        v_entry *= v_factor;
                    }

                    // Calculate the jerk depending on whether the axis is coasting in the same direction or reversing a direction.
                    float jerk =
                        (v_exit > v_entry) ?
                        (((v_entry > 0.0f) || (v_exit < 0.0f)) ?
                            // coasting
                            (v_exit - v_entry) :
                            // axis reversal
                            std::max(v_exit, -v_entry)) :
                        // v_exit <= v_entry
                        (((v_entry < 0.0f) || (v_exit > 0.0f)) ?
                            // coasting
                            (v_entry - v_exit) :
                            // axis reversal
                            std::max(-v_exit, v_entry));


                    float axis_max_jerk = get_axis_max_jerk(static_cast<PrintEstimatedStatistics::ETimeMode>(i), static_cast<Axis>(a));
                    if (jerk > axis_max_jerk) {
                        v_factor *= axis_max_jerk / jerk;
                        limited = true;
                    }
                }
            }

            if (limited)
                vmax_junction *= v_factor;

            // Now the transition velocity is known, which maximizes the shared exit / entry velocity while
            // respecting the jerk factors, it may be possible, that applying separate safe exit / entry velocities will achieve faster prints.
            float vmax_junction_threshold = vmax_junction * 0.99f;

            // Not coasting. The machine will stop and start the movements anyway, better to start the segment from start.
            if (prev.safe_feedrate > vmax_junction_threshold && curr.safe_feedrate > vmax_junction_threshold)
                vmax_junction = curr.safe_feedrate;
        }

        float v_allowable = max_allowable_speed(-acceleration, curr.safe_feedrate, block.distance);
        block.feedrate_profile.entry = std::min(vmax_junction, v_allowable);

        block.max_entry_speed = vmax_junction;
        block.flags.nominal_length = (block.feedrate_profile.cruise <= v_allowable);
        block.flags.recalculate = true;
        block.safe_feedrate = curr.safe_feedrate;

        // calculates block trapezoid
        block.calculate_trapezoid();

        // updates previous
        prev = curr;

        blocks.push_back(block);

        if (blocks.size() > TimeProcessor::Planner::refresh_threshold) {
            machine.calculate_time(TimeProcessor::Planner::queue_size, 0, erNone, [&result=m_result, i,&machine](const TimeBlock& block, int time) {
                machine.handle_time_block(block,time,i,result);
            });
        }
    }

    // store move
    store_move_vertex(type);
}

// BBS: this function is absolutely new for G2 and G3 gcode
void  GCodeProcessor::process_G2_G3(const GCodeReader::GCodeLine& line)
{
    int filament_id = get_filament_id();
    float filament_diameter = (static_cast<size_t>(filament_id) < m_result.filament_diameters.size()) ? m_result.filament_diameters[filament_id] : m_result.filament_diameters.back();
    float filament_radius = 0.5f * filament_diameter;
    float area_filament_cross_section = static_cast<float>(M_PI) * sqr(filament_radius);
    auto absolute_position = [this, area_filament_cross_section](Axis axis, const GCodeReader::GCodeLine& lineG2_3) {
        bool is_relative = (m_global_positioning_type == EPositioningType::Relative);
        if (axis == E)
            is_relative |= (m_e_local_positioning_type == EPositioningType::Relative);

        if (lineG2_3.has(Slic3r::Axis(axis))) {
            float lengthsScaleFactor = (m_units == EUnits::Inches) ? INCHES_TO_MM : 1.0f;
            float ret = lineG2_3.value(Slic3r::Axis(axis)) * lengthsScaleFactor;
            if (axis == I)
                return m_start_position[X] + ret;
            else if (axis == J)
                return m_start_position[Y] + ret;
            else
                return is_relative ? m_start_position[axis] + ret : m_origin[axis] + ret;
        }
        else {
            if (axis == I)
                return m_start_position[X];
            else if (axis == J)
                return m_start_position[Y];
            else
                return m_start_position[axis];
        }
    };

    auto move_type = [this](const float& delta_E) {
        if (delta_E == 0.0f)
            return EMoveType::Travel;
        else
            return EMoveType::Extrude;
    };

     auto arc_interpolation = [this](const Vec3f& start_pos, const Vec3f& end_pos, const Vec3f& center_pos, const bool is_ccw) {
         float radius = ArcSegment::calc_arc_radius(start_pos, center_pos);
         //BBS: radius is too small to draw
         if (radius <= DRAW_ARC_TOLERANCE) {
             m_interpolation_points.resize(0);
             return;
         }
         float radian_step = 2 * acos((radius - DRAW_ARC_TOLERANCE) / radius);
         float num = ArcSegment::calc_arc_radian(start_pos, end_pos, center_pos, is_ccw) / radian_step;
         float z_step = (num < 1)? end_pos.z() - start_pos.z() : (end_pos.z() - start_pos.z()) / num;
         radian_step = is_ccw ? radian_step : -radian_step;
         int interpolation_num = floor(num);

         m_interpolation_points.resize(interpolation_num, Vec3f::Zero());
         Vec3f delta = start_pos - center_pos;
         for (auto i = 0; i < interpolation_num; i++) {
             float cos_val = cos((i+1) * radian_step);
             float sin_val = sin((i+1) * radian_step);
             m_interpolation_points[i] = Vec3f(center_pos.x() + delta.x() * cos_val - delta.y() * sin_val,
                                               center_pos.y() + delta.x() * sin_val + delta.y() * cos_val,
                                               start_pos.z() + (i + 1) * z_step);
         }
     };

    ++m_g1_line_id;

    //BBS: enable processing of lines M201/M203/M204/M205
    m_time_processor.machine_envelope_processing_enabled = true;

    //BBS: get axes positions from line
    for (unsigned char a = X; a <= E; ++a) {
        m_end_position[a] = absolute_position((Axis)a, line);
    }
    //BBS: G2 G3 line but has no I and J axis, invalid G code format
    if (!line.has(I) && !line.has(J))
        return;
    //BBS: P mode, but xy position is not same, or P is not 1, invalid G code format
    if (line.has(P) &&
        (m_start_position[X] != m_end_position[X] ||
         m_start_position[Y] != m_end_position[Y] ||
         ((int)line.p()) != 1))
        return;

    m_arc_center = Vec3f(absolute_position(I, line),absolute_position(J, line),m_start_position[Z]);
    //BBS: G2 is CW direction, G3 is CCW direction
    const std::string_view cmd = line.cmd();
    m_move_path_type = (::atoi(&cmd[1]) == 2) ? EMovePathType::Arc_move_cw : EMovePathType::Arc_move_ccw;
    //BBS: get arc length,interpolation points and radian in X-Y plane
    Vec3f start_point = Vec3f(m_start_position[X], m_start_position[Y], m_start_position[Z]);
    Vec3f end_point = Vec3f(m_end_position[X], m_end_position[Y], m_end_position[Z]);
    float arc_length;
    if (!line.has(P))
        arc_length = ArcSegment::calc_arc_length(start_point, end_point, m_arc_center, (m_move_path_type == EMovePathType::Arc_move_ccw));
    else
        arc_length = ((int)line.p()) * 2 * PI * (start_point - m_arc_center).norm();
    //BBS: Attention! arc_onterpolation does not support P mode while P is not 1.
    arc_interpolation(start_point, end_point, m_arc_center, (m_move_path_type == EMovePathType::Arc_move_ccw));
    float radian = ArcSegment::calc_arc_radian(start_point, end_point, m_arc_center, (m_move_path_type == EMovePathType::Arc_move_ccw));
    Vec3f start_dir = Circle::calc_tangential_vector(start_point, m_arc_center, (m_move_path_type == EMovePathType::Arc_move_ccw));
    Vec3f end_dir = Circle::calc_tangential_vector(end_point, m_arc_center, (m_move_path_type == EMovePathType::Arc_move_ccw));

    //BBS: updates feedrate from line, if present
    if (line.has_f())
        m_feedrate = line.f() * MMMIN_TO_MMSEC;

    //BBS: calculates movement deltas
    AxisCoords delta_pos;
    for (unsigned char a = X; a <= E; ++a) {
        delta_pos[a] = m_end_position[a] - m_start_position[a];
    }

    //BBS: no displacement, return
    if (arc_length == 0.0f && delta_pos[Z] == 0.0f)
        return;

    EMoveType type = move_type(delta_pos[E]);


    float delta_xyz = std::sqrt(sqr(arc_length) + sqr(delta_pos[Z]));
    if (type == EMoveType::Extrude) {
        float volume_extruded_filament = area_filament_cross_section * delta_pos[E];
        float area_toolpath_cross_section = volume_extruded_filament / delta_xyz;

        if(m_extrusion_role == ExtrusionRole::erSupportMaterial || m_extrusion_role == ExtrusionRole::erSupportMaterialInterface || m_extrusion_role ==ExtrusionRole::erSupportTransition)
            m_used_filaments.increase_support_caches(volume_extruded_filament);
        else if (m_extrusion_role == ExtrusionRole::erWipeTower) {
            //BBS: save wipe tower volume to the cache
            m_used_filaments.increase_wipe_tower_caches(volume_extruded_filament);
        }
        else {
            //BBS: save extruded volume to the cache
            m_used_filaments.increase_model_caches(volume_extruded_filament);
        }
        //BBS: volume extruded filament / tool displacement = area toolpath cross section
        m_mm3_per_mm = area_toolpath_cross_section;
#if ENABLE_GCODE_VIEWER_DATA_CHECKING
        m_mm3_per_mm_compare.update(area_toolpath_cross_section, m_extrusion_role);
#endif // ENABLE_GCODE_VIEWER_DATA_CHECKING

        if (m_forced_height > 0.0f)
            m_height = m_forced_height;
        else {
            if (m_end_position[Z] > m_extruded_last_z + EPSILON)
                m_height = m_end_position[Z] - m_extruded_last_z;
        }

        if (m_height == 0.0f)
            m_height = DEFAULT_TOOLPATH_HEIGHT;

        if (m_end_position[Z] == 0.0f)
            m_end_position[Z] = m_height;

        m_extruded_last_z = m_end_position[Z];
        m_options_z_corrector.update(m_height);

#if ENABLE_GCODE_VIEWER_DATA_CHECKING
        m_height_compare.update(m_height, m_extrusion_role);
#endif // ENABLE_GCODE_VIEWER_DATA_CHECKING

        if (m_forced_width > 0.0f)
            m_width = m_forced_width;
        else if (m_extrusion_role == erExternalPerimeter)
            //BBS: cross section: rectangle
            m_width = delta_pos[E] * static_cast<float>(M_PI * sqr(1.05f * filament_radius)) / (delta_xyz * m_height);
        else if (m_extrusion_role == erBridgeInfill || m_extrusion_role == erNone)
            //BBS: cross section: circle
            m_width = static_cast<float>(m_result.filament_diameters[filament_id]) * std::sqrt(delta_pos[E] / delta_xyz);
        else
            //BBS: cross section: rectangle + 2 semicircles
            m_width = delta_pos[E] * static_cast<float>(M_PI * sqr(filament_radius)) / (delta_xyz * m_height) + static_cast<float>(1.0 - 0.25 * M_PI) * m_height;

        if (m_width == 0.0f)
            m_width = DEFAULT_TOOLPATH_WIDTH;

        //BBS: clamp width to avoid artifacts which may arise from wrong values of m_height
        m_width = std::min(m_width, std::max(2.0f, 4.0f * m_height));

#if ENABLE_GCODE_VIEWER_DATA_CHECKING
        m_width_compare.update(m_width, m_extrusion_role);
#endif // ENABLE_GCODE_VIEWER_DATA_CHECKING
    }

    //BBS: time estimate section
    assert(delta_xyz != 0.0f);
    float inv_distance = 1.0f / delta_xyz;
    float radius = ArcSegment::calc_arc_radius(start_point, m_arc_center);

    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        TimeMachine& machine = m_time_processor.machines[i];
        if (!machine.enabled)
            continue;

        TimeMachine::State& curr = machine.curr;
        TimeMachine::State& prev = machine.prev;
        std::vector<TimeBlock>& blocks = machine.blocks;

        curr.feedrate = (type == EMoveType::Travel) ?
            minimum_travel_feedrate(static_cast<PrintEstimatedStatistics::ETimeMode>(i), m_feedrate) :
            minimum_feedrate(static_cast<PrintEstimatedStatistics::ETimeMode>(i), m_feedrate);

        //BBS: calculeta enter and exit direction
        curr.enter_direction = start_dir;
        curr.exit_direction = end_dir;

        TimeBlock block;
        block.move_type = type;
        block.skippable_type = m_skippable_type;
        //BBS: don't calculate travel time into extrusion path, except travel inside start and end gcode.
        block.role = (type != EMoveType::Travel || m_extrusion_role == erCustom) ? m_extrusion_role : erNone;
        block.distance = delta_xyz;
        block.move_id = m_result.moves.size();
        block.g1_line_id = m_g1_line_id;
        block.layer_id = std::max<unsigned int>(1, m_layer_id);
        block.flags.prepare_stage = m_processing_start_custom_gcode;

        // BBS: calculates block cruise feedrate
        // For arc move, we need to limite the cruise according to centripetal acceleration which is
        // same with acceleration in x-y plane. Because arc move part is only on x-y plane, we use x-y acceleration directly
        float centripetal_acceleration = get_acceleration(static_cast<PrintEstimatedStatistics::ETimeMode>(i));
        float max_feedrate_by_centri_acc = sqrtf(centripetal_acceleration * radius) / (arc_length * inv_distance);
        curr.feedrate = std::min(curr.feedrate, max_feedrate_by_centri_acc);

        float min_feedrate_factor = 1.0f;
        for (unsigned char a = X; a <= E; ++a) {
            if (a == X || a == Y)
                //BBS: use resultant feedrate in x-y plane
                curr.axis_feedrate[a] = curr.feedrate * arc_length * inv_distance;
            else if (a == Z)
                curr.axis_feedrate[a] = curr.feedrate * delta_pos[a] * inv_distance;
            else
                curr.axis_feedrate[a] *= machine.extrude_factor_override_percentage;

            curr.abs_axis_feedrate[a] = std::abs(curr.axis_feedrate[a]);
            if (curr.abs_axis_feedrate[a] != 0.0f) {
                float axis_max_feedrate = get_axis_max_feedrate(static_cast<PrintEstimatedStatistics::ETimeMode>(i), static_cast<Axis>(a), m_extruder_id);
                if (axis_max_feedrate != 0.0f) min_feedrate_factor = std::min<float>(min_feedrate_factor, axis_max_feedrate / curr.abs_axis_feedrate[a]);
            }
        }
        curr.feedrate *= min_feedrate_factor;
        block.feedrate_profile.cruise = curr.feedrate;
        if (min_feedrate_factor < 1.0f) {
            for (unsigned char a = X; a <= E; ++a) {
                curr.axis_feedrate[a] *= min_feedrate_factor;
                curr.abs_axis_feedrate[a] *= min_feedrate_factor;
            }
        }

        //BBS: calculates block acceleration
        float acceleration = (type == EMoveType::Travel) ?
                              get_travel_acceleration(static_cast<PrintEstimatedStatistics::ETimeMode>(i)) :
                              get_acceleration(static_cast<PrintEstimatedStatistics::ETimeMode>(i));
        float min_acc_factor = 1.0f;
        AxisCoords axis_acc;
        for (unsigned char a = X; a <= Z; ++a) {
            if (a == X || a == Y)
                //BBS: use resultant feedrate in x-y plane
                axis_acc[a] = acceleration * arc_length * inv_distance;
            else
                axis_acc[a] = acceleration * std::abs(delta_pos[a]) * inv_distance;

            if (axis_acc[a] != 0.0f) {
                float axis_max_acceleration = get_axis_max_acceleration(static_cast<PrintEstimatedStatistics::ETimeMode>(i), static_cast<Axis>(a), m_extruder_id);
                if (axis_max_acceleration != 0.0f && axis_acc[a] > axis_max_acceleration) min_acc_factor = std::min<float>(min_acc_factor, axis_max_acceleration / axis_acc[a]);
            }
        }
        block.acceleration = acceleration * min_acc_factor;

        //BBS: calculates block exit feedrate
        for (unsigned char a = X; a <= E; ++a) {
            float axis_max_jerk = get_axis_max_jerk(static_cast<PrintEstimatedStatistics::ETimeMode>(i), static_cast<Axis>(a));
            if (curr.abs_axis_feedrate[a] > axis_max_jerk)
                curr.safe_feedrate = std::min(curr.safe_feedrate, axis_max_jerk);
        }
        block.feedrate_profile.exit = curr.safe_feedrate;

        //BBS: calculates block entry feedrate
        static const float PREVIOUS_FEEDRATE_THRESHOLD = 0.0001f;
        float vmax_junction = curr.safe_feedrate;
        if (!blocks.empty() && prev.feedrate > PREVIOUS_FEEDRATE_THRESHOLD) {
            bool prev_speed_larger = prev.feedrate > block.feedrate_profile.cruise;
            float smaller_speed_factor = prev_speed_larger ? (block.feedrate_profile.cruise / prev.feedrate) : (prev.feedrate / block.feedrate_profile.cruise);
            //BBS: Pick the smaller of the nominal speeds. Higher speed shall not be achieved at the junction during coasting.
            vmax_junction = prev_speed_larger ? block.feedrate_profile.cruise : prev.feedrate;

            float v_factor = 1.0f;
            bool limited = false;

            for (unsigned char a = X; a <= E; ++a) {
                //BBS: Limit an axis. We have to differentiate coasting from the reversal of an axis movement, or a full stop.
                if (a == X) {
                    Vec3f exit_v = prev.feedrate * (prev.exit_direction);
                    if (prev_speed_larger)
                        exit_v *= smaller_speed_factor;
                    Vec3f entry_v = block.feedrate_profile.cruise * (curr.enter_direction);
                    Vec3f jerk_v = entry_v - exit_v;
                    jerk_v = Vec3f(abs(jerk_v.x()), abs(jerk_v.y()), abs(jerk_v.z()));
                    Vec3f max_xyz_jerk_v = get_xyz_max_jerk(static_cast<PrintEstimatedStatistics::ETimeMode>(i));

                    for (size_t i = 0; i < 3; i++)
                    {
                        if (jerk_v[i] > max_xyz_jerk_v[i]) {
                            v_factor *= max_xyz_jerk_v[i] / jerk_v[i];
                            jerk_v *= v_factor;
                            limited = true;
                        }
                    }
                }
                else if (a == Y || a == Z) {
                    continue;
                }
                else {
                    float v_exit = prev.axis_feedrate[a];
                    float v_entry = curr.axis_feedrate[a];

                    if (prev_speed_larger)
                        v_exit *= smaller_speed_factor;

                    if (limited) {
                        v_exit *= v_factor;
                        v_entry *= v_factor;
                    }

                    //BBS: Calculate the jerk depending on whether the axis is coasting in the same direction or reversing a direction.
                    float jerk =
                        (v_exit > v_entry) ?
                        (((v_entry > 0.0f) || (v_exit < 0.0f)) ?
                            //BBS: coasting
                            (v_exit - v_entry) :
                            //BBS: axis reversal
                            std::max(v_exit, -v_entry)) :
                        (((v_entry < 0.0f) || (v_exit > 0.0f)) ?
                            //BBS: coasting
                            (v_entry - v_exit) :
                            //BBS: axis reversal
                            std::max(-v_exit, v_entry));


                    float axis_max_jerk = get_axis_max_jerk(static_cast<PrintEstimatedStatistics::ETimeMode>(i), static_cast<Axis>(a));
                    if (jerk > axis_max_jerk) {
                        v_factor *= axis_max_jerk / jerk;
                        limited = true;
                    }
                }
            }

            if (limited)
                vmax_junction *= v_factor;

            //BBS: Now the transition velocity is known, which maximizes the shared exit / entry velocity while
            // respecting the jerk factors, it may be possible, that applying separate safe exit / entry velocities will achieve faster prints.
            float vmax_junction_threshold = vmax_junction * 0.99f;

            //BBS: Not coasting. The machine will stop and start the movements anyway, better to start the segment from start.
            if ((prev.safe_feedrate > vmax_junction_threshold) && (curr.safe_feedrate > vmax_junction_threshold))
                vmax_junction = curr.safe_feedrate;
        }

        float v_allowable = max_allowable_speed(-acceleration, curr.safe_feedrate, block.distance);
        block.feedrate_profile.entry = std::min(vmax_junction, v_allowable);

        block.max_entry_speed = vmax_junction;
        block.flags.nominal_length = (block.feedrate_profile.cruise <= v_allowable);
        block.flags.recalculate = true;
        block.safe_feedrate = curr.safe_feedrate;

        //BBS: calculates block trapezoid
        block.calculate_trapezoid();

        //BBS: updates previous
        prev = curr;

        blocks.push_back(block);

        if (blocks.size() > TimeProcessor::Planner::refresh_threshold) {
            machine.calculate_time(TimeProcessor::Planner::queue_size, 0, erNone, [&result=m_result, i,&machine](const TimeBlock& block, int time) {
                machine.handle_time_block(block,time,i,result);
            });
        }
    }

    //BBS: seam detector
    Vec3f plate_offset = {(float) m_x_offset, (float) m_y_offset, 0.0f};

    if (m_seams_detector.is_active()) {
        //BBS: check for seam starting vertex
        if (type == EMoveType::Extrude && m_extrusion_role == erExternalPerimeter && !m_seams_detector.has_first_vertex()) {
            m_seams_detector.set_first_vertex(m_result.moves.back().position - m_extruder_offsets[get_filament_id()] - plate_offset);
        } else if (type == EMoveType::Extrude && m_extrusion_role == erExternalPerimeter && m_detect_layer_based_on_tag) {
            const Vec3f real_last_pos = Vec3f(m_result.moves.back().position.x() - m_x_offset, m_result.moves.back().position.y() - m_y_offset,
                                              m_result.moves.back().position.z());
            const Vec3f new_pos       = real_last_pos - m_extruder_offsets[filament_id];
            // We may have sloped loop, drop any previous start pos if we have z increment
            const std::optional<Vec3f> first_vertex = m_seams_detector.get_first_vertex();
            if (new_pos.z() > first_vertex->z()) { m_seams_detector.set_first_vertex(new_pos); }
        }
        //BBS: check for seam ending vertex and store the resulting move
        else if ((type != EMoveType::Extrude || (m_extrusion_role != erExternalPerimeter && m_extrusion_role != erOverhangPerimeter)) && m_seams_detector.has_first_vertex()) {
            auto set_end_position = [this](const Vec3f& pos) {
                m_end_position[X] = pos.x(); m_end_position[Y] = pos.y(); m_end_position[Z] = pos.z();
            };
            const Vec3f curr_pos(m_end_position[X], m_end_position[Y], m_end_position[Z]);
            const Vec3f new_pos = m_result.moves.back().position - m_extruder_offsets[filament_id] - plate_offset;
            const std::optional<Vec3f> first_vertex = m_seams_detector.get_first_vertex();
            //BBS: the threshold value = 0.0625f == 0.25 * 0.25 is arbitrary, we may find some smarter condition later

            if ((new_pos - *first_vertex).squaredNorm() < 0.0625f) {
                set_end_position(0.5f * (new_pos + *first_vertex));
                store_move_vertex(EMoveType::Seam);
                set_end_position(curr_pos);
            }

            m_seams_detector.activate(false);
        }
    }
    else if (type == EMoveType::Extrude && m_extrusion_role == erExternalPerimeter) {
        m_seams_detector.activate(true);
        m_seams_detector.set_first_vertex(m_result.moves.back().position - m_extruder_offsets[filament_id] - plate_offset);
    }

    //BBS: some layer may only has G3/G3, update right layer height
    if (m_detect_layer_based_on_tag && !m_result.spiral_vase_layers.empty()) {
        if (delta_pos[Z] >= 0.0 && type == EMoveType::Extrude && m_result.spiral_vase_layers.back().first == FLT_MAX) {
            // replace layer height placeholder with correct value
            m_result.spiral_vase_layers.back().first = static_cast<float>(m_end_position[Z]);
        }
        if (!m_result.moves.empty())
            m_result.spiral_vase_layers.back().second.second = m_result.moves.size() - 1 - m_seams_count;
    }

    //BBS: store move
    store_move_vertex(type, m_move_path_type);
}

//BBS
void GCodeProcessor::process_G4(const GCodeReader::GCodeLine& line)
{
    float value_s = 0.0;
    float value_p = 0.0;
    if (line.has_value('S', value_s) || line.has_value('P', value_p)) {
        value_s += value_p * 0.001;
        simulate_st_synchronize(value_s);
    }
}

//BBS
void GCodeProcessor::process_G29(const GCodeReader::GCodeLine& line)
{
    //BBS: hardcode 260 seconds for G29
    //Todo: use a machine related setting when we have second kind of BBL printer
    const float value_s = 260.0;
    simulate_st_synchronize(value_s);
}

void GCodeProcessor::process_G10(const GCodeReader::GCodeLine& line)
{
    // stores retract move
    store_move_vertex(EMoveType::Retract);
}

void GCodeProcessor::process_G11(const GCodeReader::GCodeLine& line)
{
    // stores unretract move
    store_move_vertex(EMoveType::Unretract);
}

void GCodeProcessor::process_G20(const GCodeReader::GCodeLine& line)
{
    m_units = EUnits::Inches;
}

void GCodeProcessor::process_G21(const GCodeReader::GCodeLine& line)
{
    m_units = EUnits::Millimeters;
}

void GCodeProcessor::process_G22(const GCodeReader::GCodeLine& line)
{
    // stores retract move
    store_move_vertex(EMoveType::Retract);
}

void GCodeProcessor::process_G23(const GCodeReader::GCodeLine& line)
{
    // stores unretract move
    store_move_vertex(EMoveType::Unretract);
}

void GCodeProcessor::process_G28(const GCodeReader::GCodeLine& line)
{
    std::string_view cmd = line.cmd();
    std::string new_line_raw = { cmd.data(), cmd.size() };
    bool found = false;
    if (line.has('X')) {
        new_line_raw += " X0";
        found = true;
    }
    if (line.has('Y')) {
        new_line_raw += " Y0";
        found = true;
    }
    if (line.has('Z')) {
        new_line_raw += " Z0";
        found = true;
    }
    if (!found)
        new_line_raw += " X0  Y0  Z0";

    GCodeReader::GCodeLine new_gline;
    GCodeReader reader;
    reader.parse_line(new_line_raw, [&](GCodeReader& reader, const GCodeReader::GCodeLine& gline) { new_gline = gline; });
    process_G1(new_gline);
}

void GCodeProcessor::process_G90(const GCodeReader::GCodeLine& line)
{
    m_global_positioning_type = EPositioningType::Absolute;
}

void GCodeProcessor::process_G91(const GCodeReader::GCodeLine& line)
{
    m_global_positioning_type = EPositioningType::Relative;
}

void GCodeProcessor::process_G92(const GCodeReader::GCodeLine& line)
{
    float lengths_scale_factor = (m_units == EUnits::Inches) ? INCHES_TO_MM : 1.0f;
    bool any_found = false;

    if (line.has_x()) {
        m_origin[X] = m_end_position[X] - line.x() * lengths_scale_factor;
        any_found = true;
    }

    if (line.has_y()) {
        m_origin[Y] = m_end_position[Y] - line.y() * lengths_scale_factor;
        any_found = true;
    }

    if (line.has_z()) {
        m_origin[Z] = m_end_position[Z] - line.z() * lengths_scale_factor;
        any_found = true;
    }

    if (line.has_e()) {
        // extruder coordinate can grow to the point where its float representation does not allow for proper addition with small increments,
        // we set the value taken from the G92 line as the new current position for it
        m_end_position[E] = line.e() * lengths_scale_factor;
        any_found = true;
    }
    else
        simulate_st_synchronize();

    if (!any_found && !line.has_unknown_axis()) {
        // The G92 may be called for axes that PrusaSlicer does not recognize, for example see GH issue #3510,
        // where G92 A0 B0 is called although the extruder axis is till E.
        for (unsigned char a = X; a <= E; ++a) {
            m_origin[a] = m_end_position[a];
        }
    }
}

void GCodeProcessor::process_M1(const GCodeReader::GCodeLine& line)
{
    simulate_st_synchronize();
}

void GCodeProcessor::process_M82(const GCodeReader::GCodeLine& line)
{
    m_e_local_positioning_type = EPositioningType::Absolute;
}

void GCodeProcessor::process_M83(const GCodeReader::GCodeLine& line)
{
    m_e_local_positioning_type = EPositioningType::Relative;
}

void GCodeProcessor::process_M104(const GCodeReader::GCodeLine& line)
{
    int filament_id = get_filament_id();
    float new_temp;
    if (line.has_value('S', new_temp))
        m_extruder_temps[filament_id] = new_temp;
}

void GCodeProcessor::process_VM104(const GCodeReader::GCodeLine& line)
{
    process_M104(line);
}

void GCodeProcessor::process_M106(const GCodeReader::GCodeLine& line)
{
    //BBS: for Bambu machine ,we both use M106 P1 and M106 to indicate the part cooling fan
    //So we must not ignore M106 P1
    if (!line.has('P') || (line.has('P') && line.p() == 1.0f)) {
        // The absence of P means the print cooling fan, so ignore anything else.
        float new_fan_speed;
        if (line.has_value('S', new_fan_speed))
            m_fan_speed = (100.0f / 255.0f) * new_fan_speed;
        else
            m_fan_speed = 100.0f;
    }
}

void GCodeProcessor::process_M107(const GCodeReader::GCodeLine& line)
{
    m_fan_speed = 0.0f;
}

void GCodeProcessor::process_M108(const GCodeReader::GCodeLine& line)
{
    // These M-codes are used by Sailfish to change active tool.
    // They have to be processed otherwise toolchanges will be unrecognised

    if (m_flavor != gcfSailfish)
        return;

    std::string cmd = line.raw();
    size_t pos = cmd.find("T");
    if (pos != std::string::npos)
        process_T(cmd.substr(pos));
}

void GCodeProcessor::process_M109(const GCodeReader::GCodeLine& line)
{
    int filament_id = get_filament_id();
    float new_temp;
    if (line.has_value('R', new_temp)) {
        float val;
        if (line.has_value('T', val)) {
            size_t eid = static_cast<size_t>(val);
            if (eid < m_extruder_temps.size())
                m_extruder_temps[eid] = new_temp;
        }
        else
            m_extruder_temps[filament_id] = new_temp;
    }
    else if (line.has_value('S', new_temp))
        m_extruder_temps[filament_id] = new_temp;
}

void GCodeProcessor::process_VM109(const GCodeReader::GCodeLine& line)
{
    process_M109(line);
}

void GCodeProcessor::process_M132(const GCodeReader::GCodeLine& line)
{
    // This command is used by Makerbot to load the current home position from EEPROM
    // see: https://github.com/makerbot/s3g/blob/master/doc/GCodeProtocol.md

    if (line.has('X'))
        m_origin[X] = 0.0f;

    if (line.has('Y'))
        m_origin[Y] = 0.0f;

    if (line.has('Z'))
        m_origin[Z] = 0.0f;

    if (line.has('E'))
        m_origin[E] = 0.0f;
}

void GCodeProcessor::process_M135(const GCodeReader::GCodeLine& line)
{
    // These M-codes are used by MakerWare to change active tool.
    // They have to be processed otherwise toolchanges will be unrecognised

    if (m_flavor != gcfMakerWare)
        return;

    std::string cmd = line.raw();
    size_t pos = cmd.find("T");
    if (pos != std::string::npos)
        process_T(cmd.substr(pos));
}

void GCodeProcessor::process_M140(const GCodeReader::GCodeLine& line)
{
    float new_temp;
    if (line.has_value('S', new_temp))
        m_highest_bed_temp = m_highest_bed_temp < (int)new_temp ? (int)new_temp : m_highest_bed_temp;
}

void GCodeProcessor::process_M190(const GCodeReader::GCodeLine& line)
{
    float new_temp;
    if (line.has_value('S', new_temp))
        m_highest_bed_temp = m_highest_bed_temp < (int)new_temp ? (int)new_temp : m_highest_bed_temp;
}

void GCodeProcessor::process_M191(const GCodeReader::GCodeLine& line)
{
    float chamber_temp = 0;
    const float wait_chamber_temp_time = 720.0;
    // BBS: when chamber_temp>40,caculate time required for heating
    if (line.has_value('S', chamber_temp) && chamber_temp > 40)
        simulate_st_synchronize(wait_chamber_temp_time);
}


void GCodeProcessor::process_M201(const GCodeReader::GCodeLine& line)
{
    // see http://reprap.org/wiki/G-code#M201:_Set_max_printing_acceleration
    float factor = ((m_flavor != gcfRepRapSprinter && m_flavor != gcfRepRapFirmware) && m_units == EUnits::Inches) ? INCHES_TO_MM : 1.0f;
    int indx_limit = m_time_processor.machine_limits.machine_max_acceleration_x.size() / 2;
    for (size_t index = 0; index < indx_limit; index += 2) {
        for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
            if (static_cast<PrintEstimatedStatistics::ETimeMode>(i) == PrintEstimatedStatistics::ETimeMode::Normal || m_time_processor.machine_envelope_processing_enabled) {
                if (line.has_x()) set_option_value(m_time_processor.machine_limits.machine_max_acceleration_x, index + i, line.x() * factor);

                if (line.has_y()) set_option_value(m_time_processor.machine_limits.machine_max_acceleration_y, index + i, line.y() * factor);

                if (line.has_z()) set_option_value(m_time_processor.machine_limits.machine_max_acceleration_z, index + i, line.z() * factor);

                if (line.has_e()) set_option_value(m_time_processor.machine_limits.machine_max_acceleration_e, index + i, line.e() * factor);
            }
        }
    }
}

void GCodeProcessor::process_M203(const GCodeReader::GCodeLine& line)
{
    // see http://reprap.org/wiki/G-code#M203:_Set_maximum_feedrate
    if (m_flavor == gcfRepetier)
        return;

    // see http://reprap.org/wiki/G-code#M203:_Set_maximum_feedrate
    // http://smoothieware.org/supported-g-codes
    float factor = (m_flavor == gcfMarlinLegacy || m_flavor == gcfMarlinFirmware || m_flavor == gcfSmoothie || m_flavor == gcfKlipper) ? 1.0f : MMMIN_TO_MMSEC;

    //BBS:
    int indx_limit = m_time_processor.machine_limits.machine_max_speed_x.size() / 2;
    for (size_t index = 0; index < indx_limit; index += 2) {
        for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
            if (static_cast<PrintEstimatedStatistics::ETimeMode>(i) == PrintEstimatedStatistics::ETimeMode::Normal || m_time_processor.machine_envelope_processing_enabled) {
                if (line.has_x())
                    set_option_value(m_time_processor.machine_limits.machine_max_speed_x, index + i, line.x() * factor);

                if (line.has_y())
                    set_option_value(m_time_processor.machine_limits.machine_max_speed_y, index + i, line.y() * factor);

                if (line.has_z())
                    set_option_value(m_time_processor.machine_limits.machine_max_speed_z, index + i, line.z() * factor);

                if (line.has_e())
                    set_option_value(m_time_processor.machine_limits.machine_max_speed_e, index + i, line.e() * factor);
            }
        }
    }
}

void GCodeProcessor::process_M204(const GCodeReader::GCodeLine& line)
{
    float value;
    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        if (static_cast<PrintEstimatedStatistics::ETimeMode>(i) == PrintEstimatedStatistics::ETimeMode::Normal ||
            m_time_processor.machine_envelope_processing_enabled) {
            if (line.has_value('S', value)) {
                // Legacy acceleration format. This format is used by the legacy Marlin, MK2 or MK3 firmware
                // It is also generated by PrusaSlicer to control acceleration per extrusion type
                // (perimeters, first layer etc) when 'Marlin (legacy)' flavor is used.
                set_acceleration(static_cast<PrintEstimatedStatistics::ETimeMode>(i), value);
                set_travel_acceleration(static_cast<PrintEstimatedStatistics::ETimeMode>(i), value);
                if (line.has_value('T', value))
                    set_retract_acceleration(static_cast<PrintEstimatedStatistics::ETimeMode>(i), value);
            }
            else {
                // New acceleration format, compatible with the upstream Marlin.
                if (line.has_value('P', value))
                    set_acceleration(static_cast<PrintEstimatedStatistics::ETimeMode>(i), value);
                if (line.has_value('R', value))
                    set_retract_acceleration(static_cast<PrintEstimatedStatistics::ETimeMode>(i), value);
                if (line.has_value('T', value))
                    // Interpret the T value as the travel acceleration in the new Marlin format.
                    set_travel_acceleration(static_cast<PrintEstimatedStatistics::ETimeMode>(i), value);
            }
        }
    }
}

void GCodeProcessor::process_M205(const GCodeReader::GCodeLine& line)
{
    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        if (static_cast<PrintEstimatedStatistics::ETimeMode>(i) == PrintEstimatedStatistics::ETimeMode::Normal ||
            m_time_processor.machine_envelope_processing_enabled) {
            if (line.has_x()) {
                float max_jerk = line.x();
                set_option_value(m_time_processor.machine_limits.machine_max_jerk_x, i, max_jerk);
                set_option_value(m_time_processor.machine_limits.machine_max_jerk_y, i, max_jerk);
            }

            if (line.has_y())
                set_option_value(m_time_processor.machine_limits.machine_max_jerk_y, i, line.y());

            if (line.has_z())
                set_option_value(m_time_processor.machine_limits.machine_max_jerk_z, i, line.z());

            if (line.has_e())
                set_option_value(m_time_processor.machine_limits.machine_max_jerk_e, i, line.e());

            float value;
            if (line.has_value('S', value))
                set_option_value(m_time_processor.machine_limits.machine_min_extruding_rate, i, value);

            if (line.has_value('T', value))
                set_option_value(m_time_processor.machine_limits.machine_min_travel_rate, i, value);
        }
    }
}

void GCodeProcessor::process_SET_VELOCITY_LIMIT(const GCodeReader::GCodeLine& line)
{
    // handle SQUARE_CORNER_VELOCITY
    std::regex pattern("\\sSQUARE_CORNER_VELOCITY\\s*=\\s*([0-9]*\\.*[0-9]*)");
    std::smatch matches;
    if (std::regex_search(line.raw(), matches, pattern) && matches.size() == 2) {
        float _jerk = 0;
        try
        {
            _jerk = std::stof(matches[1]);
        }
        catch (...) {}
        for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
            set_option_value(m_time_processor.machine_limits.machine_max_jerk_x, i, _jerk);
            set_option_value(m_time_processor.machine_limits.machine_max_jerk_y, i, _jerk);
        }
    }

    pattern = std::regex("\\sACCEL\\s*=\\s*([0-9]*\\.*[0-9]*)");
    if (std::regex_search(line.raw(), matches, pattern) && matches.size() == 2) {
        float _accl = 0;
        try
        {
            _accl = std::stof(matches[1]);
        }
        catch (...) {}
        for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
            set_acceleration(static_cast<PrintEstimatedStatistics::ETimeMode>(i), _accl);
            set_travel_acceleration(static_cast<PrintEstimatedStatistics::ETimeMode>(i), _accl);
        }
    }

    pattern = std::regex("\\sVELOCITY\\s*=\\s*([0-9]*\\.*[0-9]*)");
    if (std::regex_search(line.raw(), matches, pattern) && matches.size() == 2) {
        float _speed = 0;
        try
        {
            _speed = std::stof(matches[1]);
        }
        catch (...) {}
        for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
            set_option_value(m_time_processor.machine_limits.machine_max_speed_x, i, _speed);
            set_option_value(m_time_processor.machine_limits.machine_max_speed_y, i, _speed);
        }
    }

}

void GCodeProcessor::process_M221(const GCodeReader::GCodeLine& line)
{
    float value_s;
    float value_t;
    if (line.has_value('S', value_s) && !line.has_value('T', value_t)) {
        value_s *= 0.01f;
        for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
            m_time_processor.machines[i].extrude_factor_override_percentage = value_s;
        }
    }
}

void GCodeProcessor::process_M400(const GCodeReader::GCodeLine& line)
{
    float value_s = 0.0;
    float value_p = 0.0;
    if (line.has_value('S', value_s) || line.has_value('P', value_p)) {
        value_s += value_p * 0.001;
        simulate_st_synchronize(value_s);
    }
}

void GCodeProcessor::process_M401(const GCodeReader::GCodeLine& line)
{
    if (m_flavor != gcfRepetier)
        return;

    for (unsigned char a = 0; a <= 3; ++a) {
        m_cached_position.position[a] = m_start_position[a];
    }
    m_cached_position.feedrate = m_feedrate;
}

void GCodeProcessor::process_M402(const GCodeReader::GCodeLine& line)
{
    if (m_flavor != gcfRepetier)
        return;

    // see for reference:
    // https://github.com/repetier/Repetier-Firmware/blob/master/src/ArduinoAVR/Repetier/Printer.cpp
    // void Printer::GoToMemoryPosition(bool x, bool y, bool z, bool e, float feed)

    bool has_xyz = !(line.has('X') || line.has('Y') || line.has('Z'));

    float p = FLT_MAX;
    for (unsigned char a = X; a <= Z; ++a) {
        if (has_xyz || line.has(a)) {
            p = m_cached_position.position[a];
            if (p != FLT_MAX)
                m_start_position[a] = p;
        }
    }

    p = m_cached_position.position[E];
    if (p != FLT_MAX)
        m_start_position[E] = p;

    p = FLT_MAX;
    if (!line.has_value(4, p))
        p = m_cached_position.feedrate;

    if (p != FLT_MAX)
        m_feedrate = p;
}

void GCodeProcessor::process_M566(const GCodeReader::GCodeLine& line)
{
    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        if (line.has_x())
            set_option_value(m_time_processor.machine_limits.machine_max_jerk_x, i, line.x() * MMMIN_TO_MMSEC);

        if (line.has_y())
            set_option_value(m_time_processor.machine_limits.machine_max_jerk_y, i, line.y() * MMMIN_TO_MMSEC);

        if (line.has_z())
            set_option_value(m_time_processor.machine_limits.machine_max_jerk_z, i, line.z() * MMMIN_TO_MMSEC);

        if (line.has_e())
            set_option_value(m_time_processor.machine_limits.machine_max_jerk_e, i, line.e() * MMMIN_TO_MMSEC);
    }
}

void GCodeProcessor::process_M702(const GCodeReader::GCodeLine& line)
{
    int filament_id = get_filament_id();
    if (line.has('C')) {
        // MK3 MMU2 specific M code:
        // M702 C is expected to be sent by the custom end G-code when finalizing a print.
        // The MK3 unit shall unload and park the active filament into the MMU2 unit.
        m_time_processor.extruder_unloaded = true;
        simulate_st_synchronize(get_filament_unload_time(filament_id));
    }
}


void GCodeProcessor::process_SYNC(const GCodeReader::GCodeLine& line)
{
    float time = 0;
    if (line.has_value('T', time) ) {
        simulate_st_synchronize(time);
    }
}


void GCodeProcessor::process_T(const GCodeReader::GCodeLine& line)
{
    process_T(line.cmd());
}

void GCodeProcessor::process_M1020(const GCodeReader::GCodeLine &line)
{
    int curr_filament_id = get_filament_id(false);
    int curr_extruder_id = get_extruder_id(false);
    if (line.raw().length() > 5) {
        std::string filament_id_str = line.raw().substr(7);
        if (filament_id_str.empty())
            return;

        int eid = 0;
        eid = std::stoi(filament_id_str);
        if (eid < 0 || eid > 254) {
            // M1020-1 is a valid gcode line for RepRap Firmwares (used to deselects all tools)
            if ((m_flavor != gcfRepRapFirmware && m_flavor != gcfRepRapSprinter) || eid != -1)
                BOOST_LOG_TRIVIAL(error) << "Invalid M1020 command (" << line.raw() << ").";
        }
        else {
            if (eid >= m_result.filaments_count)
                BOOST_LOG_TRIVIAL(error) << "Invalid M1020 command (" << line.raw() << ").";
            process_filament_change(eid);
        }
    }
}

void GCodeProcessor::process_T(const std::string_view command)
{
    int curr_filament_id = get_filament_id(false);
    int curr_extruder_id = get_extruder_id(false);
    //TODO: multi switch
    if (command.length() > 1) {
        int eid = 0;
        if (! parse_number(command.substr(1), eid) || eid < 0 || eid > 254) {
            //BBS: T255, T1000 and T1100 is used as special command for BBL machine and does not cost time. return directly
            if ((m_flavor == gcfMarlinLegacy || m_flavor == gcfMarlinFirmware) && (command == "Tx" || command == "Tc" || command == "T?" ||
                 eid == 1000 || eid == 1100 || eid == 255))
                return;

            // T-1 is a valid gcode line for RepRap Firmwares (used to deselects all tools)
            if ((m_flavor != gcfRepRapFirmware && m_flavor != gcfRepRapSprinter) || eid != -1)
                BOOST_LOG_TRIVIAL(error) << "Invalid T command (" << command << ").";
        }
        else {
            if (eid >= m_result.filaments_count)
                BOOST_LOG_TRIVIAL(error) << "Invalid T command (" << command << ").";
            process_filament_change(eid);
        }
    }
}


void GCodeProcessor::init_filament_maps_and_nozzle_type_when_import_only_gcode()
{
    if (m_filament_maps.empty()) {
        m_filament_maps.assign((int) EnforcerBlockerType::ExtruderMax, 1);
    }
    if (m_result.nozzle_type.empty()) {
        m_result.nozzle_type.assign((int) EnforcerBlockerType::ExtruderMax, NozzleType::ntUndefine);
    }
}

void GCodeProcessor::process_filament_change(int id)
{
    assert(id < m_result.filaments_count);
    int prev_extruder_id = get_extruder_id(false);
    int prev_filament_id = get_filament_id(false);
    int next_extruder_id = m_filament_maps[id];
    int next_filament_id = id;
    float extra_time = 0;

    if (prev_filament_id == next_filament_id)
        return;

    if (prev_extruder_id != -1)
        m_last_filament_id[prev_extruder_id] = prev_filament_id;

    if (prev_extruder_id == next_extruder_id) {
        // don't need extruder change
        assert(prev_extruder_id != -1);
        process_filaments(CustomGCode::ToolChange);
        m_filament_id[next_extruder_id] = next_filament_id;
        m_result.lock();
        m_result.print_statistics.total_filament_changes += 1;
        m_result.unlock();
        extra_time += get_filament_unload_time(static_cast<size_t>(prev_filament_id));
        m_time_processor.extruder_unloaded = false;
        extra_time += get_filament_load_time(static_cast<size_t>(next_filament_id));
    }
    else {
        if (prev_extruder_id == -1) {
            // initialize
            m_extruder_id = next_extruder_id;
            m_filament_id[next_extruder_id] = next_filament_id;
            m_time_processor.extruder_unloaded = false;
            extra_time += get_filament_load_time(static_cast<size_t>(next_filament_id));
        }
        else {
            //first process cache generated by last extruder
            process_filaments(CustomGCode::ToolChange);
            //switch to current extruder
            m_extruder_id = next_extruder_id;
            if (m_last_filament_id[next_extruder_id] == (unsigned char)(-1)) {
                //no filament in current extruder
                m_filament_id[next_extruder_id] = next_filament_id;
                m_time_processor.extruder_unloaded = false;
                extra_time += get_filament_load_time(static_cast<size_t>(next_filament_id));
            }
            else if (m_last_filament_id[next_extruder_id] != next_filament_id) {
                //need to change filament
                m_filament_id[next_extruder_id] = next_filament_id;
                m_result.lock();
                m_result.print_statistics.total_filament_changes += 1;
                m_result.unlock();
                extra_time += get_filament_unload_time(static_cast<size_t>(prev_filament_id));
                m_time_processor.extruder_unloaded = false;
                extra_time += get_filament_load_time(static_cast<size_t>(next_filament_id));
            }
            m_result.lock();
            m_result.print_statistics.total_extruder_changes++;
            m_result.unlock();
            extra_time += get_extruder_change_time(next_extruder_id);
        }
    }
    m_cp_color.current = m_extruder_colors[next_filament_id];
    // store tool change move
    store_move_vertex(EMoveType::Tool_change);

    // construct a new time block to handle filament change
    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        TimeMachine& machine = m_time_processor.machines[i];
        if (!machine.enabled)
            continue;
        TimeBlock block;
        block.skippable_type = m_skippable_type;
        block.move_id = m_result.moves.size() - 1;
        block.role = erFlush;
        block.move_type = EMoveType::Tool_change;
        block.layer_id = std::max<unsigned int>(1, m_layer_id);
        block.g1_line_id = m_g1_line_id;
        block.flags.prepare_stage = m_processing_start_custom_gcode;
        block.distance = 0;
        block.calculate_trapezoid();

        // when do st_sync, we will clear all of the blocks without keeping last n blocks, so we can directly add the new block into the blocks
        machine.blocks.push_back(block);
    }

    simulate_st_synchronize(extra_time, erFlush);
}

void GCodeProcessor::store_move_vertex(EMoveType type, EMovePathType path_type)
{
    int filament_id = get_filament_id();
    m_last_line_id = (type == EMoveType::Color_change || type == EMoveType::Pause_Print || type == EMoveType::Custom_GCode) ?
        m_line_id + 1 :
        ((type == EMoveType::Seam) ? m_last_line_id : m_line_id);

    //BBS: apply plate's and extruder's offset to arc interpolation points
    if (path_type == EMovePathType::Arc_move_cw ||
        path_type == EMovePathType::Arc_move_ccw) {
        for (size_t i = 0; i < m_interpolation_points.size(); i++)
            m_interpolation_points[i] =
                Vec3f(m_interpolation_points[i].x() + m_x_offset,
                      m_interpolation_points[i].y() + m_y_offset,
                      m_processing_start_custom_gcode ? m_first_layer_height : m_interpolation_points[i].z()) +
                m_extruder_offsets[filament_id];
    }

    m_result.moves.push_back({
        type,
        m_extrusion_role,
        //BBS: add arc move related data
        path_type,
        static_cast<unsigned char>(filament_id),
        m_cp_color.current,
        m_last_line_id,
        static_cast<float>(m_end_position[E] - m_start_position[E]),
        m_feedrate,
        m_width,
        m_height,
        m_mm3_per_mm,
        m_fan_speed,
        m_extruder_temps[filament_id],
        static_cast<float>(m_layer_id), //layer_duration: set later
        {0.f,0.f}, // prefix sum of move time to this move : set later
        //BBS: add plate's offset to the rendering vertices
        Vec3f(m_end_position[X] + m_x_offset, m_end_position[Y] + m_y_offset, m_processing_start_custom_gcode ? m_first_layer_height : m_end_position[Z]) + m_extruder_offsets[filament_id],
        Vec3f(m_arc_center(0, 0) + m_x_offset, m_arc_center(1, 0) + m_y_offset, m_arc_center(2, 0)) + m_extruder_offsets[filament_id],
        m_interpolation_points,
        m_object_label_id,
        m_print_z
    });

    if (type == EMoveType::Seam) {
        m_seams_count++;
    }

    // stores stop time placeholders for later use
    if (type == EMoveType::Color_change || type == EMoveType::Pause_Print) {
        for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
            TimeMachine& machine = m_time_processor.machines[i];
            if (!machine.enabled)
                continue;

            machine.stop_times.push_back({ m_g1_line_id, 0.0f });
        }
    }
}

void GCodeProcessor::set_extrusion_role(ExtrusionRole role)
{
    m_used_filaments.process_role_cache(this);
    if (role == erFloatingVerticalShell) {
        m_extrusion_role = erSolidInfill;
    }
    else {
        m_extrusion_role = role;
    }
}

void GCodeProcessor::set_skippable_type(const std::string_view type)
{
    if (!m_skippable){
        m_skippable_type = SkipType::stNone;
        return;
    }
    auto iter = skip_type_map.find(type);
    if(iter!=skip_type_map.end()) {
        m_skippable_type = iter->second;
    } else {
        m_skippable_type = SkipType::stOther;
    }
}

float GCodeProcessor::minimum_feedrate(PrintEstimatedStatistics::ETimeMode mode, float feedrate) const
{
    if (m_time_processor.machine_limits.machine_min_extruding_rate.empty())
        return feedrate;

    return std::max(feedrate, get_option_value(m_time_processor.machine_limits.machine_min_extruding_rate, static_cast<size_t>(mode)));
}

float GCodeProcessor::minimum_travel_feedrate(PrintEstimatedStatistics::ETimeMode mode, float feedrate) const
{
    if (m_time_processor.machine_limits.machine_min_travel_rate.empty())
        return feedrate;

    return std::max(feedrate, get_option_value(m_time_processor.machine_limits.machine_min_travel_rate, static_cast<size_t>(mode)));
}

float GCodeProcessor::get_axis_max_feedrate(PrintEstimatedStatistics::ETimeMode mode, Axis axis, int extruder_id) const
{
    int matched_pos = extruder_id * 2;
    switch (axis)
    {
    case X: { return get_option_value(m_time_processor.machine_limits.machine_max_speed_x, matched_pos + static_cast<size_t>(mode)); }
    case Y: { return get_option_value(m_time_processor.machine_limits.machine_max_speed_y, matched_pos + static_cast<size_t>(mode)); }
    case Z: { return get_option_value(m_time_processor.machine_limits.machine_max_speed_z, matched_pos + static_cast<size_t>(mode)); }
    case E: { return get_option_value(m_time_processor.machine_limits.machine_max_speed_e, matched_pos + static_cast<size_t>(mode)); }
    default: { return 0.0f; }
    }
}

float GCodeProcessor::get_axis_max_acceleration(PrintEstimatedStatistics::ETimeMode mode, Axis axis, int extruder_id) const
{
    int matched_pos = extruder_id * 2;
    switch (axis)
    {
    case X: { return get_option_value(m_time_processor.machine_limits.machine_max_acceleration_x, matched_pos + static_cast<size_t>(mode)); }
    case Y: { return get_option_value(m_time_processor.machine_limits.machine_max_acceleration_y, matched_pos + static_cast<size_t>(mode)); }
    case Z: { return get_option_value(m_time_processor.machine_limits.machine_max_acceleration_z, matched_pos + static_cast<size_t>(mode)); }
    case E: { return get_option_value(m_time_processor.machine_limits.machine_max_acceleration_e, matched_pos + static_cast<size_t>(mode)); }
    default: { return 0.0f; }
    }
}

float GCodeProcessor::get_axis_max_jerk(PrintEstimatedStatistics::ETimeMode mode, Axis axis) const
{
    switch (axis)
    {
    case X: { return get_option_value(m_time_processor.machine_limits.machine_max_jerk_x, static_cast<size_t>(mode)); }
    case Y: { return get_option_value(m_time_processor.machine_limits.machine_max_jerk_y, static_cast<size_t>(mode)); }
    case Z: { return get_option_value(m_time_processor.machine_limits.machine_max_jerk_z, static_cast<size_t>(mode)); }
    case E: { return get_option_value(m_time_processor.machine_limits.machine_max_jerk_e, static_cast<size_t>(mode)); }
    default: { return 0.0f; }
    }
}

Vec3f GCodeProcessor::get_xyz_max_jerk(PrintEstimatedStatistics::ETimeMode mode) const
{
    return Vec3f(get_option_value(m_time_processor.machine_limits.machine_max_jerk_x, static_cast<size_t>(mode)),
        get_option_value(m_time_processor.machine_limits.machine_max_jerk_y, static_cast<size_t>(mode)),
        get_option_value(m_time_processor.machine_limits.machine_max_jerk_z, static_cast<size_t>(mode)));
}

float GCodeProcessor::get_retract_acceleration(PrintEstimatedStatistics::ETimeMode mode) const
{
    size_t id = static_cast<size_t>(mode);
    return (id < m_time_processor.machines.size()) ? m_time_processor.machines[id].retract_acceleration : DEFAULT_RETRACT_ACCELERATION;
}

void GCodeProcessor::set_retract_acceleration(PrintEstimatedStatistics::ETimeMode mode, float value)
{
    size_t id = static_cast<size_t>(mode);
    if (id < m_time_processor.machines.size()) {
        m_time_processor.machines[id].retract_acceleration = (m_time_processor.machines[id].max_retract_acceleration == 0.0f) ? value :
            // Clamp the acceleration with the maximum.
            std::min(value, m_time_processor.machines[id].max_retract_acceleration);
    }
}

float GCodeProcessor::get_acceleration(PrintEstimatedStatistics::ETimeMode mode) const
{
    size_t id = static_cast<size_t>(mode);
    return (id < m_time_processor.machines.size()) ? m_time_processor.machines[id].acceleration : DEFAULT_ACCELERATION;
}

void GCodeProcessor::set_acceleration(PrintEstimatedStatistics::ETimeMode mode, float value)
{
    size_t id = static_cast<size_t>(mode);
    if (id < m_time_processor.machines.size()) {
        m_time_processor.machines[id].acceleration = (m_time_processor.machines[id].max_acceleration == 0.0f) ? value :
            // Clamp the acceleration with the maximum.
            std::min(value, m_time_processor.machines[id].max_acceleration);
    }
}

float GCodeProcessor::get_travel_acceleration(PrintEstimatedStatistics::ETimeMode mode) const
{
    size_t id = static_cast<size_t>(mode);
    return (id < m_time_processor.machines.size()) ? m_time_processor.machines[id].travel_acceleration : DEFAULT_TRAVEL_ACCELERATION;
}

void GCodeProcessor::set_travel_acceleration(PrintEstimatedStatistics::ETimeMode mode, float value)
{
    size_t id = static_cast<size_t>(mode);
    if (id < m_time_processor.machines.size()) {
        m_time_processor.machines[id].travel_acceleration = (m_time_processor.machines[id].max_travel_acceleration == 0.0f) ? value :
            // Clamp the acceleration with the maximum.
            std::min(value, m_time_processor.machines[id].max_travel_acceleration);
    }
}

float GCodeProcessor::get_filament_load_time(size_t extruder_id)
{
    //BBS: change load time to machine config and all extruder has same value
    return m_time_processor.extruder_unloaded ? 0.0f : m_time_processor.filament_load_times;
}

float GCodeProcessor::get_filament_unload_time(size_t extruder_id)
{
    //BBS: change unload time to machine config and all extruder has same value
    return m_time_processor.extruder_unloaded ? 0.0f : m_time_processor.filament_unload_times;
}

float GCodeProcessor::get_extruder_change_time(size_t extruder_id)
{
    //TODO: all extruder has the same value ?
    return m_time_processor.extruder_change_times;
}

//BBS
int GCodeProcessor::get_filament_vitrification_temperature(size_t extrude_id)
{
    if (extrude_id < m_result.filament_vitrification_temperature.size())
        return m_result.filament_vitrification_temperature[extrude_id];
    else
        return 0;
}

void GCodeProcessor::process_custom_gcode_time(CustomGCode::Type code)
{
    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        TimeMachine& machine = m_time_processor.machines[i];
        if (!machine.enabled)
            continue;

        TimeMachine::CustomGCodeTime& gcode_time = machine.gcode_time;
        gcode_time.needed = true;
        //FIXME this simulates st_synchronize! is it correct?
        // The estimated time may be longer than the real print time.
        machine.simulate_st_synchronize(0, erNone, [&result=m_result, i,&machine](const TimeBlock& block, int time) {
            machine.handle_time_block(block,time,i,result);
        });
        if (gcode_time.cache != 0.0f) {
            gcode_time.times.push_back({ code, gcode_time.cache });
            gcode_time.cache = 0.0f;
        }
    }
}

void GCodeProcessor::process_filaments(CustomGCode::Type code)
{
    if (code == CustomGCode::ColorChange)
        m_used_filaments.process_color_change_cache();

    if (code == CustomGCode::ToolChange) {
        m_used_filaments.process_model_cache(this);
        m_used_filaments.process_support_cache(this);
        m_used_filaments.process_total_volume_cache(this);
        //BBS: reset remaining filament
        size_t last_extruder_id = get_extruder_id();
        m_remaining_volume[last_extruder_id] = m_nozzle_volume[last_extruder_id];
    }
}

void GCodeProcessor::simulate_st_synchronize(float additional_time, ExtrusionRole target_role)
{
    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        TimeMachine& machine = m_time_processor.machines[i];
        if (!machine.enabled)
            continue;

        machine.simulate_st_synchronize(additional_time, target_role, [&result=m_result, i,&machine](const TimeBlock& block, int time) {
            machine.handle_time_block(block,time,i,result);
        });
    }
}

void GCodeProcessor::update_estimated_times_stats()
{
    auto update_mode = [this](PrintEstimatedStatistics::ETimeMode mode) {
        PrintEstimatedStatistics::Mode& data = m_result.print_statistics.modes[static_cast<size_t>(mode)];
        data.time = get_time(mode);
        data.prepare_time = get_prepare_time(mode);
        data.custom_gcode_times = get_custom_gcode_times(mode, true);
        data.moves_times = get_moves_time(mode);
        data.roles_times = get_roles_time(mode);
        data.layers_times = get_layers_time(mode);
    };

    update_mode(PrintEstimatedStatistics::ETimeMode::Normal);
    if (m_time_processor.machines[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Stealth)].enabled)
        update_mode(PrintEstimatedStatistics::ETimeMode::Stealth);
    else
        m_result.print_statistics.modes[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Stealth)].reset();

    m_result.print_statistics.volumes_per_color_change  = m_used_filaments.volumes_per_color_change;
    m_result.print_statistics.model_volumes_per_extruder      = m_used_filaments.model_volumes_per_filament;
    m_result.print_statistics.wipe_tower_volumes_per_extruder = m_used_filaments.wipe_tower_volumes_per_filament;
    m_result.print_statistics.support_volumes_per_extruder = m_used_filaments.support_volumes_per_filament;
    m_result.print_statistics.flush_per_filament      = m_used_filaments.flush_per_filament;
    m_result.print_statistics.used_filaments_per_role   = m_used_filaments.filaments_per_role;
    m_result.print_statistics.total_volumes_per_extruder = m_used_filaments.total_volumes_per_filament;
}

//BBS: ugly code...
void GCodeProcessor::update_slice_warnings()
{
    m_result.warnings.clear();

    auto get_used_filaments = [this]() {
        std::vector<size_t> used_filaments;
        used_filaments.reserve(m_used_filaments.total_volumes_per_filament.size());
        for (auto& item : m_used_filaments.total_volumes_per_filament) {
            used_filaments.push_back(item.first);
        }
        return used_filaments;
    };

    auto used_filaments = get_used_filaments();
    assert(!used_filaments.empty());
    GCodeProcessorResult::SliceWarning warning;
    warning.level = 1;
    if (m_highest_bed_temp != 0) {
        for (size_t i = 0; i < used_filaments.size(); i++) {
            int temperature = get_filament_vitrification_temperature(used_filaments[i]);
            if (temperature != 0 && m_highest_bed_temp >= temperature)
                warning.params.push_back(std::to_string(used_filaments[i]));
        }
    }

    if (!warning.params.empty()) {
        warning.level       = 3;
        warning.msg         = BED_TEMP_TOO_HIGH_THAN_FILAMENT;
        warning.error_code  = "1000C001";
        m_result.warnings.push_back(warning);
    }

    //bbs:HRC checker
    warning.params.clear();
    warning.level=1;

    std::vector<int> nozzle_hrc_lists(m_result.nozzle_type.size(), 0);
    // store the nozzle hrc of each extruder
    for (size_t idx = 0; idx < m_result.nozzle_type.size(); ++idx)
        nozzle_hrc_lists[idx] = Print::get_hrc_by_nozzle_type(m_result.nozzle_type[idx]);

    for (size_t idx = 0; idx < used_filaments.size(); ++idx) {
        int filament_hrc = 0;

        if (used_filaments[idx] < m_result.required_nozzle_HRC.size())
            filament_hrc = m_result.required_nozzle_HRC[used_filaments[idx]];

        int filament_extruder_id = m_filament_maps[used_filaments[idx]];
        int extruder_hrc = nozzle_hrc_lists[filament_extruder_id];

        BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": Check HRC: filament:%1%, hrc=%2%, extruder:%3%, hrc:%4%") % used_filaments[idx] % filament_hrc % filament_extruder_id % extruder_hrc;

        if (extruder_hrc!=0 && extruder_hrc < filament_hrc)
            warning.params.push_back(std::to_string(used_filaments[idx]));
    }

    if (!warning.params.empty()) {
        warning.level      = 3;
        warning.msg = NOZZLE_HRC_CHECKER;
        warning.error_code = "1000C002";
        m_result.warnings.push_back(warning);
    }

    // bbs:HRC checker
    warning.params.clear();
    warning.level = 1;
    if (!m_result.support_traditional_timelapse) {
        warning.level      = 2;
        warning.msg        = NOT_SUPPORT_TRADITIONAL_TIMELAPSE;
        warning.error_code = "10018003";
        m_result.warnings.push_back(warning);

        // Compatible with older version for A series
        warning.level      = 3;
        warning.error_code = "1000C003";
        m_result.warnings.push_back(warning);
    }

    if (m_result.timelapse_warning_code != 0) {
        if (m_result.timelapse_warning_code & 1) {
            warning.level      = 1;
            warning.msg        = NOT_GENERATE_TIMELAPSE;
            warning.error_code = "10014001";
            m_result.warnings.push_back(warning);
        }
        if ((m_result.timelapse_warning_code >> 1) & 1) {
            warning.level      = 1;
            warning.msg        = NOT_GENERATE_TIMELAPSE;
            warning.error_code = "10014002";
            m_result.warnings.push_back(warning);
        }
        if ((m_result.timelapse_warning_code >> 2) & 1) {
            warning.level      = 2;
            warning.msg        = SMOOTH_TIMELAPSE_WITHOUT_PRIME_TOWER;
            warning.error_code = "10018004";
            m_result.warnings.push_back(warning);
        }
    }

    m_result.warnings.shrink_to_fit();
}

int GCodeProcessor::get_filament_id(bool force_initialize)const
{
    int extruder_id = get_extruder_id(force_initialize);
    if (extruder_id == -1)
        return force_initialize ? 0 : -1;

    if (m_filament_id[extruder_id] == (unsigned char)(-1))
        return force_initialize ? 0 : -1;

    return static_cast<int>(m_filament_id[extruder_id]);
}

int GCodeProcessor::get_last_filament_id(bool force_initialize)const
{
    int extruder_id = get_extruder_id(force_initialize);
    if (extruder_id == -1)
        return force_initialize ? 0 : -1;

    if (m_last_filament_id[extruder_id] == (unsigned char)(-1))
        return force_initialize ? 0 : -1;

    return static_cast<int>(m_last_filament_id[extruder_id]);
}

int GCodeProcessor::get_extruder_id(bool force_initialize)const
{
    if (m_extruder_id == (unsigned char)(-1))
        return force_initialize ? 0 : -1;
    return static_cast<int>(m_extruder_id);
}

void GCodeProcessor::PreCoolingInjector::process_pre_cooling_and_heating(TimeProcessor::InsertedLinesMap& inserted_operation_lines)
{
    auto get_nozzle_temp = [this](int filament_id,bool from_or_to) {
        if (filament_id == -1)
            return from_or_to ? 140 : 0; // default temp
        return filament_nozzle_temps[filament_id];
        };

    std::map<int, std::vector<ExtruderFreeBlock>> per_extruder_free_blocks;

    for (auto& block : m_extruder_free_blocks)
        per_extruder_free_blocks[block.extruder_id].emplace_back(block);

    for (auto& elem : per_extruder_free_blocks) {
        int extruder_id = elem.first;
        auto& extruder_free_blcoks = elem.second;
        for (auto iter = extruder_free_blcoks.begin(); iter != extruder_free_blcoks.end(); ++iter) {
            bool is_end = std::next(iter) == extruder_free_blcoks.end();
            bool apply_pre_cooling = true;
            bool apply_pre_heating = is_end ? false : true;
            float curr_temp = get_nozzle_temp(iter->last_filament_id,true);
            float target_temp = get_nozzle_temp(iter->next_filament_id, false);
            inject_cooling_heating_command(inserted_operation_lines, *iter, curr_temp, target_temp, apply_pre_cooling, apply_pre_heating);
        }
    }
}

void GCodeProcessor::PreCoolingInjector::build_extruder_free_blocks(const std::vector<ExtruderPreHeating::FilamentUsageBlock>& filament_usage_blocks, const std::vector<ExtruderPreHeating::ExtruderUsageBlcok>& extruder_usage_blocks)
{
    if (extruder_usage_blocks.size() <= 1)
        build_by_filament_blocks(filament_usage_blocks);
    else
        build_by_extruder_blocks(extruder_usage_blocks);
}

void GCodeProcessor::PreCoolingInjector::inject_cooling_heating_command(TimeProcessor::InsertedLinesMap& inserted_operation_lines, const ExtruderFreeBlock& block, float curr_temp, float target_temp, bool pre_cooling, bool pre_heating)
{
    auto format_line_M104 = [&physical_extruder_map = this->physical_extruder_map](int target_temp, int target_extruder = -1, const std::string& comment = std::string()) {
        std::string buffer = "M104";
        if (target_extruder != -1)
            buffer += (" T" + std::to_string(physical_extruder_map[target_extruder]));
        buffer += " S" + std::to_string(target_temp) + " N0"; // N0 means the gcode is generated by slicer
        if (!comment.empty())
            buffer += " ;" + comment;
        buffer += '\n';
        return buffer;
        };

    auto is_pre_cooling_valid = [&nozzle_temps = this->filament_nozzle_temps, &pre_cooling_temps = this->filament_pre_cooling_temps](int idx) ->bool {
        if(idx < 0)
            return false;
        return pre_cooling_temps[idx] > 0 && pre_cooling_temps[idx] < nozzle_temps[idx];
        };

    auto get_partial_free_cooling_thres = [&nozzle_temps = this->filament_nozzle_temps, &pre_cooling_temps = this->filament_pre_cooling_temps](int idx) -> float{
        if(idx < 0)
            return 30.f;
        return nozzle_temps[idx] - (float)(pre_cooling_temps[idx]);
        };

    auto gcode_move_comp = [](const GCodeProcessorResult::MoveVertex& a, unsigned int gcode_id) {
        return a.gcode_id < gcode_id;
        };

    auto find_skip_block_end = [&skippable_blocks = this->skippable_blocks](unsigned int gcode_id) -> unsigned int{
        auto it = std::upper_bound(
            skippable_blocks.begin(), skippable_blocks.end(), gcode_id,
            [](unsigned int id, const std::pair<unsigned int, unsigned int>&block) { return id < block.first; }
        );
        if (it != skippable_blocks.begin()) {
            auto candidate = std::prev(it);
            if (gcode_id >= candidate->first && gcode_id <= candidate->second)
                return candidate->second;
        }
        return 0;
    };

    auto find_skip_block_start = [&skippable_blocks = this->skippable_blocks](unsigned int gcode_id) -> unsigned int {
        auto it = std::upper_bound(
            skippable_blocks.begin(), skippable_blocks.end(), gcode_id,
            [](unsigned int id, const std::pair<unsigned int, unsigned int>&block) { return id < block.first; }
        );
        if (it != skippable_blocks.begin()) {
            auto candidate = std::prev(it);
            if (gcode_id >= candidate->first && gcode_id <= candidate->second)
                return candidate->first;
        }
        return 0;
    };

    auto adjust_iter = [&](std::vector<GCodeProcessorResult::MoveVertex>::const_iterator iter,
                       const std::vector<GCodeProcessorResult::MoveVertex>::const_iterator& begin,
                       const std::vector<GCodeProcessorResult::MoveVertex>::const_iterator& end,
                       bool forward) -> std::vector<GCodeProcessorResult::MoveVertex>::const_iterator
    {
        if (forward) {
            while (iter != end) {
                unsigned current_id = iter->gcode_id;
                unsigned skip_block_end = find_skip_block_end(current_id);
                if(skip_block_end == 0)
                    break;
                iter = std::lower_bound(iter, end, skip_block_end + 1, gcode_move_comp);
            }
        }
        else {
            while (iter != begin) {
                unsigned current_id = iter->gcode_id;
                unsigned skip_block_start = find_skip_block_start(current_id);
                if(skip_block_start == 0)
                    break;
                auto new_iter = std::lower_bound(begin, iter, skip_block_start, gcode_move_comp);
                if(new_iter == begin)
                    break;
                iter = std::prev(new_iter);
            }
        }
        return iter;
    };

    if (!pre_cooling && !pre_heating && block.free_upper_gcode_id <= block.free_lower_gcode_id)
        return;

    auto move_iter_lower = std::lower_bound(moves.begin(), moves.end(), block.free_lower_gcode_id, gcode_move_comp);
    auto move_iter_upper = std::lower_bound(moves.begin(), moves.end(), block.free_upper_gcode_id, gcode_move_comp); // closed iter

    if (move_iter_lower == moves.end() || move_iter_upper == moves.begin())
        return;
    --move_iter_upper;

    auto partial_free_move_lower = std::lower_bound(moves.begin(), moves.end(), block.partial_free_lower_id, gcode_move_comp);
    auto partial_free_move_upper = std::lower_bound(moves.begin(), moves.end(), block.partial_free_upper_id, gcode_move_comp); // closed iter

    if (partial_free_move_lower == moves.end() || partial_free_move_upper == moves.begin())
        return;
    --partial_free_move_upper;

    if (move_iter_lower >= move_iter_upper)
        return;

    bool apply_cooling_when_partial_free = is_pre_cooling_valid(block.last_filament_id) && pre_cooling;

    float partial_free_time_gap = partial_free_move_upper->time[valid_machine_id] - partial_free_move_lower->time[valid_machine_id]; // time of partial free
    float complete_free_time_gap = move_iter_upper->time[valid_machine_id] - move_iter_lower->time[valid_machine_id]; // time of complete free

    if (apply_cooling_when_partial_free && partial_free_time_gap + complete_free_time_gap < inject_time_threshold)
        return;

    if (!apply_cooling_when_partial_free && complete_free_time_gap < inject_time_threshold)
        return;

    float ext_heating_rate = heating_rate[block.extruder_id];
    float ext_cooling_rate = cooling_rate[block.extruder_id];

    if (apply_cooling_when_partial_free) {
        float max_cooling_temp = std::min(curr_temp, std::min(get_partial_free_cooling_thres(block.last_filament_id), partial_free_time_gap * ext_cooling_rate));
        curr_temp -= max_cooling_temp; // set the temperature after doing cooling when post-extruding
        inserted_operation_lines[block.partial_free_lower_id].emplace_back(format_line_M104(curr_temp, block.extruder_id, "Multi extruder pre cooling in post extrusion"), TimeProcessor::InsertLineType::PreCooling);
    }

    if (pre_cooling && !pre_heating) {
        // only perform cooling
        if (target_temp >= curr_temp)
            return;
        inserted_operation_lines[block.free_lower_gcode_id].emplace_back(format_line_M104(target_temp, block.extruder_id, "Multi extruder pre cooling"), TimeProcessor::InsertLineType::PreCooling);
        return;
    }
    if (!pre_cooling && pre_heating) {
        // only perform heating
        if (target_temp <= curr_temp)
            return;
        float heating_start_time = move_iter_upper->time[valid_machine_id] - (target_temp - curr_temp) / ext_heating_rate;
        auto heating_move_iter = std::upper_bound(move_iter_lower, move_iter_upper + 1, heating_start_time, [valid_machine_id = this->valid_machine_id](float time, const GCodeProcessorResult::MoveVertex& a) {return time < a.time[valid_machine_id]; });
        if (heating_move_iter == move_iter_lower) {
            inserted_operation_lines[block.free_lower_gcode_id].emplace_back(format_line_M104(target_temp, block.extruder_id, "Multi extruder pre heating"), TimeProcessor::InsertLineType::PreHeating);
        }
        else {
            --heating_move_iter;
            heating_move_iter = adjust_iter(heating_move_iter, move_iter_lower, move_iter_upper, false);
            inserted_operation_lines[heating_move_iter->gcode_id].emplace_back(format_line_M104(target_temp, block.extruder_id, "Multi extruder pre heating"), TimeProcessor::InsertLineType::PreHeating);
        }
        return;
    }
    // perform cooling first and then perform heating
    float mid_temp = std::max(0.f, (curr_temp * ext_heating_rate + target_temp * ext_cooling_rate - complete_free_time_gap * ext_cooling_rate * ext_heating_rate) / (ext_cooling_rate + ext_heating_rate));
    float heating_temp = target_temp - mid_temp;
    float heating_start_time = move_iter_upper->time[valid_machine_id] - heating_temp / ext_heating_rate;
    auto heating_move_iter = std::upper_bound(move_iter_lower, move_iter_upper + 1, heating_start_time, [valid_machine_id = this->valid_machine_id](float time, const GCodeProcessorResult::MoveVertex& a) {return time < a.time[valid_machine_id]; });
    if (heating_move_iter == move_iter_lower)
        return;
    --heating_move_iter;
    heating_move_iter = adjust_iter(heating_move_iter, move_iter_lower, move_iter_upper, false);

    // get the insert pos of heat cmd and recalculate time gap and delta temp
    float real_cooling_time = heating_move_iter->time[valid_machine_id] - move_iter_lower->time[valid_machine_id];
    int real_delta_temp = std::min((int)(real_cooling_time * ext_cooling_rate), (int)curr_temp);
    if (real_delta_temp == 0)
        return;
    inserted_operation_lines[block.free_lower_gcode_id].emplace_back(format_line_M104(curr_temp - real_delta_temp, block.extruder_id, "Multi extruder pre cooling"), TimeProcessor::InsertLineType::PreCooling);
    inserted_operation_lines[heating_move_iter->gcode_id].emplace_back(format_line_M104(target_temp, block.extruder_id, "Multi extruder pre heating"), TimeProcessor::InsertLineType::PreHeating);
}

void GCodeProcessor::PreCoolingInjector::build_by_filament_blocks(const std::vector<ExtruderPreHeating::FilamentUsageBlock>& filament_usage_blocks_)
{
    m_extruder_free_blocks.clear();
    std::vector<std::vector<ExtruderPreHeating::FilamentUsageBlock>> per_extruder_usage_blocks(2);
    for (auto& block : filament_usage_blocks_)
        per_extruder_usage_blocks[filament_maps[block.filament_id]].emplace_back(block);

    ExtruderPreHeating::FilamentUsageBlock start_filament_block(-1, 0, machine_start_gcode_end_id);
    ExtruderPreHeating::FilamentUsageBlock end_filament_block(-1, machine_end_gcode_start_id, std::numeric_limits<unsigned int>::max());

    for (auto& blocks : per_extruder_usage_blocks) {
        blocks.insert(blocks.begin(), start_filament_block);
        blocks.emplace_back(end_filament_block);
    }

    for(size_t extruder_id =0 ;extruder_id<per_extruder_usage_blocks.size();++extruder_id){
        const auto& filament_blocks = per_extruder_usage_blocks[extruder_id];

        for (auto iter = filament_blocks.begin(); iter < filament_blocks.end(); ++iter) {
            auto niter = std::next(iter);
            if (niter == filament_blocks.end())
                break;
            ExtruderFreeBlock block;
            block.free_lower_gcode_id = iter->upper_gcode_id;
            block.last_filament_id = iter->filament_id;
            block.free_upper_gcode_id = niter->lower_gcode_id;
            block.next_filament_id = niter->filament_id;
            block.extruder_id = extruder_id;
            block.partial_free_lower_id = block.free_lower_gcode_id;
            block.partial_free_upper_id = block.free_lower_gcode_id;
            m_extruder_free_blocks.emplace_back(block);
        }
    }

    sort(m_extruder_free_blocks.begin(), m_extruder_free_blocks.end(), [](const auto& a, const auto& b) {
        return a.free_lower_gcode_id < b.free_lower_gcode_id || (a.free_lower_gcode_id == b.free_lower_gcode_id && a.free_upper_gcode_id < b.free_upper_gcode_id);
        });
}

void GCodeProcessor::PreCoolingInjector::build_by_extruder_blocks(const std::vector<ExtruderPreHeating::ExtruderUsageBlcok>& extruder_usage_blocks_)
{
    m_extruder_free_blocks.clear();
    std::vector<std::vector<ExtruderPreHeating::ExtruderUsageBlcok>> per_extruder_usage_blocks(2);
    for (auto& block : extruder_usage_blocks_)
        per_extruder_usage_blocks[block.extruder_id].emplace_back(block);

    for(size_t extruder_id =0;extruder_id<per_extruder_usage_blocks.size();++extruder_id){
        auto& blocks = per_extruder_usage_blocks[extruder_id];
        ExtruderPreHeating::ExtruderUsageBlcok start_filament_block;
        start_filament_block.initialize_step_1(extruder_id, 0, -1);
        start_filament_block.initialize_step_2(machine_start_gcode_end_id);
        start_filament_block.initialize_step_3(machine_start_gcode_end_id, -1, machine_start_gcode_end_id);

        ExtruderPreHeating::ExtruderUsageBlcok end_filament_block;
        end_filament_block.initialize_step_1(extruder_id, machine_end_gcode_start_id, -1);
        end_filament_block.initialize_step_2(std::numeric_limits<int>::max());
        end_filament_block.initialize_step_3(std::numeric_limits<int>::max(), -1, std::numeric_limits<int>::max());

        blocks.insert(blocks.begin(), start_filament_block);
        blocks.emplace_back(end_filament_block);
    }

    for (size_t extruder_id = 0; extruder_id < per_extruder_usage_blocks.size(); ++extruder_id) {
        const auto& extruder_usage_blocks = per_extruder_usage_blocks[extruder_id];
        for (auto iter = extruder_usage_blocks.begin(); iter != extruder_usage_blocks.end(); ++iter) {
            auto niter = std::next(iter);
            if (niter == extruder_usage_blocks.end())
                break;
            ExtruderFreeBlock block;
            block.free_lower_gcode_id = iter->end_id;
            block.last_filament_id = iter->end_filament;
            block.free_upper_gcode_id = niter->start_id;
            block.next_filament_id = niter->start_filament;
            block.extruder_id = extruder_id;
            block.partial_free_lower_id = iter->post_extrusion_start_id;
            block.partial_free_upper_id = iter->post_extrusion_end_id;
            m_extruder_free_blocks.emplace_back(block);
        }
    }

    sort(m_extruder_free_blocks.begin(), m_extruder_free_blocks.end(), [](const auto& a, const auto& b) {
        return a.free_lower_gcode_id < b.free_lower_gcode_id || (a.free_lower_gcode_id == b.free_lower_gcode_id && a.free_upper_gcode_id < b.free_upper_gcode_id);
        });
}

} /* namespace Slic3r */

