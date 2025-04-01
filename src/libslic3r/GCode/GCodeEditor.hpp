#ifndef slic3r_GCodeEditer_hpp_
#define slic3r_GCodeEditer_hpp_

#include "../libslic3r.h"
#include <map>
#include <string>
#include <cfloat>
#include <libslic3r/Point.hpp>
#include <libslic3r/Slicing.hpp>

namespace Slic3r {

class GCode;
class Layer;

struct CoolingLine
{
    enum Type {
        TYPE_SET_TOOL           = 1 << 0,
        TYPE_EXTRUDE_END        = 1 << 1,
        TYPE_OVERHANG_FAN_START = 1 << 2,
        TYPE_OVERHANG_FAN_END   = 1 << 3,
        TYPE_G0                 = 1 << 4,
        TYPE_G1                 = 1 << 5,
        TYPE_ADJUSTABLE         = 1 << 6,
        TYPE_EXTERNAL_PERIMETER = 1 << 7,
        // The line sets a feedrate.
        TYPE_HAS_F = 1 << 8,
        TYPE_WIPE  = 1 << 9,
        TYPE_G4    = 1 << 10,
        TYPE_G92   = 1 << 11,
        // BBS: add G2 G3 type
        TYPE_G2                     = 1 << 12,
        TYPE_G3                     = 1 << 13,
        TYPE_FORCE_RESUME_FAN       = 1 << 14,
        TYPE_SET_FAN_CHANGING_LAYER = 1 << 15,
        TYPE_OBJECT_START           = 1 << 16,
        TYPE_OBJECT_END             = 1 << 17,
    };

    CoolingLine(unsigned int type, size_t line_start, size_t line_end)
        : type(type), line_start(line_start), line_end(line_end), length(0.f), feedrate(0.f), origin_feedrate(0.f), time(0.f), time_max(0.f), slowdown(false)
    {}

    bool adjustable(bool slowdown_external_perimeters) const
    {
        return (this->type & TYPE_ADJUSTABLE) && (!(this->type & TYPE_EXTERNAL_PERIMETER) || slowdown_external_perimeters) && this->time < this->time_max;
    }

    bool adjustable() const { return (this->type & TYPE_ADJUSTABLE) && this->time < this->time_max; }

    size_t type;
    // Start of this line at the G-code snippet.
    size_t line_start;
    // End of this line at the G-code snippet.
    size_t line_end;
    // XY Euclidian length of this segment.
    float length;
    // Current feedrate, possibly adjusted.
    float feedrate;
    // Current duration of this segment.
    float time;
    // Maximum duration of this segment.
    float time_max;
    // If marked with the "slowdown" flag, the line has been slowed down.
    bool slowdown;
    // Current feedrate, possibly adjusted.
    float origin_feedrate = 0;
    float origin_time_max = 0;
    // Current duration of this segment.
    //float origin_time;
    bool  outwall_smooth_mark = false;
    int   object_id = -1;
    int   cooling_node_id = -1;
};

struct PerExtruderAdjustments
{
    // Calculate the total elapsed time per this extruder, adjusted for the slowdown.
    float elapsed_time_total() const
    {
        float time_total = 0.f;
        for (const CoolingLine &line : lines) time_total += line.time;
        return time_total;
    }
    // Calculate the total elapsed time when slowing down
    // to the minimum extrusion feed rate defined for the current material.
    float maximum_time_after_slowdown(bool slowdown_external_perimeters) const
    {
        float time_total = 0.f;
        for (const CoolingLine &line : lines)
            if (line.adjustable(slowdown_external_perimeters)) {
                if (line.time_max == FLT_MAX)
                    return FLT_MAX;
                else
                    time_total += line.time_max;
            } else
                time_total += line.time;
        return time_total;
    }
    // Calculate the adjustable part of the total time.
    float adjustable_time(bool slowdown_external_perimeters) const
    {
        float time_total = 0.f;
        for (const CoolingLine &line : lines)
            if (line.adjustable(slowdown_external_perimeters)) time_total += line.time;
        return time_total;
    }
    // Calculate the non-adjustable part of the total time.
    float non_adjustable_time(bool slowdown_external_perimeters) const
    {
        float time_total = 0.f;
        for (const CoolingLine &line : lines)
            if (!line.adjustable(slowdown_external_perimeters)) time_total += line.time;
        return time_total;
    }
    // Slow down the adjustable extrusions to the minimum feedrate allowed for the current extruder material.
    // Used by both proportional and non-proportional slow down.
    float slowdown_to_minimum_feedrate(bool slowdown_external_perimeters)
    {
        float time_total = 0.f;
        for (CoolingLine &line : lines) {
            if (line.adjustable(slowdown_external_perimeters)) {
                assert(line.time_max >= 0.f && line.time_max < FLT_MAX);
                line.slowdown = true;
                line.time     = line.time_max;
                line.feedrate = line.length / line.time;
            }
            time_total += line.time;
        }
        return time_total;
    }
    // Slow down each adjustable G-code line proportionally by a factor.
    // Used by the proportional slow down.
    float slow_down_proportional(float factor, bool slowdown_external_perimeters)
    {
        assert(factor >= 1.f);
        float time_total = 0.f;
        for (CoolingLine &line : lines) {
            if (line.adjustable(slowdown_external_perimeters)) {
                line.slowdown = true;
                line.time     = std::min(line.time_max, line.time * factor);
                line.feedrate = line.length / line.time;
            }
            time_total += line.time;
        }
        return time_total;
    }

    // Sort the lines, adjustable first, higher feedrate first.
    // Used by non-proportional slow down.
    void sort_lines_by_decreasing_feedrate()
    {
        std::sort(lines.begin(), lines.end(), [](const CoolingLine &l1, const CoolingLine &l2) {
            bool adj1 = l1.adjustable();
            bool adj2 = l2.adjustable();
            return (adj1 == adj2) ? l1.feedrate > l2.feedrate : adj1;
        });
        for (n_lines_adjustable = 0; n_lines_adjustable < lines.size() && this->lines[n_lines_adjustable].adjustable(); ++n_lines_adjustable)
            ;
        time_non_adjustable = 0.f;
        for (size_t i = n_lines_adjustable; i < lines.size(); ++i) time_non_adjustable += lines[i].time;
    }

    // Calculate the maximum time stretch when slowing down to min_feedrate.
    // Slowdown to min_feedrate shall be allowed for this extruder's material.
    // Used by non-proportional slow down.
    float time_stretch_when_slowing_down_to_feedrate(float min_feedrate) const
    {
        float time_stretch = 0.f;
        assert(this->slow_down_min_speed < min_feedrate + EPSILON);
        for (size_t i = 0; i < n_lines_adjustable; ++i) {
            const CoolingLine &line = lines[i];
            if (line.feedrate > min_feedrate) time_stretch += line.time * (line.feedrate / min_feedrate - 1.f);
        }
        return time_stretch;
    }

    // Slow down all adjustable lines down to min_feedrate.
    // Slowdown to min_feedrate shall be allowed for this extruder's material.
    // Used by non-proportional slow down.
    void slow_down_to_feedrate(float min_feedrate)
    {
        assert(this->slow_down_min_speed < min_feedrate + EPSILON);
        for (size_t i = 0; i < n_lines_adjustable; ++i) {
            CoolingLine &line = lines[i];
            if (line.feedrate > min_feedrate) {
                line.time *= std::max(1.f, line.feedrate / min_feedrate);
                line.feedrate = min_feedrate;
                line.slowdown = true;
            }
        }
    }

    //collect lines time
    float collection_line_times_of_extruder() {
        float times = 0;
        for (const CoolingLine &line: lines) {
            times += line.time;
        }
        return times;
    }

    // Extruder, for which the G-code will be adjusted.
    unsigned int extruder_id = 0;
    // Is the cooling slow down logic enabled for this extruder's material?
    bool cooling_slow_down_enabled = false;
    // Slow down the print down to slow_down_min_speed if the total layer time is below slow_down_layer_time.
    float slow_down_layer_time = 0.f;
    // Minimum print speed allowed for this extruder.
    float slow_down_min_speed = 0.f;

    // Parsed lines.
    std::vector<CoolingLine> lines;
    // The following two values are set by sort_lines_by_decreasing_feedrate():
    // Number of adjustable lines, at the start of lines.
    size_t n_lines_adjustable = 0;
    // Non-adjustable time of lines starting with n_lines_adjustable.
    float time_non_adjustable = 0;
    // Current total time for this extruder.
    float time_total = 0;
    // Maximum time for this extruder, when the maximum slow down is applied.
    float time_maximum = 0;

    // Temporaries for processing the slow down. Both thresholds go from 0 to n_lines_adjustable.
    size_t idx_line_begin = 0;
    size_t idx_line_end   = 0;
};
// A standalone G-code filter, to control cooling of the print.
// The G-code is processed per layer. Once a layer is collected, fan start / stop commands are edited
// and the print is modified to stretch over a minimum layer time.
//
// The simple it sounds, the actual implementation is significantly more complex.
// Namely, for a multi-extruder print, each material may require a different cooling logic.
// For example, some materials may not like to print too slowly, while with some materials 
// we may slow down significantly.
//
class GCodeEditor {
public:
    GCodeEditor(GCode &gcodegen);
    void        reset(const Vec3d &position);
    void set_current_extruder(unsigned int extruder_id)
    {
        m_current_extruder     = extruder_id;
        m_parse_gcode_extruder = extruder_id;
    }
    std::string process_layer(std::string &&                       gcode,
                              const size_t                         layer_id,
                              std::vector<PerExtruderAdjustments> &per_extruder_adjustments,
                              const std::vector<int> &             object_label,
                              const bool                           flush,
                              const bool                           spiral_vase);

    // float       calculate_layer_slowdown(std::vector<PerExtruderAdjustments> &per_extruder_adjustments);
    // Apply slow down over G-code lines stored in per_extruder_adjustments, enable fan if needed.
    // Returns the adjusted G-code.
    std::string write_layer_gcode(const std::string &gcode, size_t layer_id, float layer_time, std::vector<PerExtruderAdjustments> &per_extruder_adjustments);

private :
	GCodeEditor& operator=(const GCodeEditor&) = delete;
    std::vector<PerExtruderAdjustments> parse_layer_gcode(const std::string &                       gcode,
                                                          std::vector<float> &                      current_pos,
                                                          const std::vector<int> &                  object_label,
                                                          bool                                      spiral_vase,
                                                          bool                                      join_z_smooth);

    // G-code snippet cached for the support layers preceding an object layer.
    std::string                 m_gcode;
    // Internal data.
    // BBS: X,Y,Z,E,F,I,J
    std::vector<char>           m_axis;
    std::vector<float>          m_current_pos;
    // Current known fan speed or -1 if not known yet.
    int                         m_fan_speed;
    int                         m_additional_fan_speed;
    // Cached from GCodeWriter.
    // Printing extruder IDs, zero based.
    std::vector<unsigned int>   m_extruder_ids;
    // Highest of m_extruder_ids plus 1.
    unsigned int                m_num_extruders { 0 };
    const std::string           m_toolchange_prefix;
    // Referencs GCode::m_config, which is FullPrintConfig. While the PrintObjectConfig slice of FullPrintConfig is being modified,
    // the PrintConfig slice of FullPrintConfig is constant, thus no thread synchronization is required.
    const PrintConfig          &m_config;
    unsigned int                m_current_extruder;
    unsigned int                m_parse_gcode_extruder;
    //BBS: current fan speed
    int                         m_current_fan_speed;
    //BBS:
    bool                        m_set_fan_changing_layer = false;
    bool                        m_set_addition_fan_changing_layer = false;
};

}

#endif
