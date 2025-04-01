#ifndef slic3r_Smoothing_hpp_
#define slic3r_Smoothing_hpp_
#include "../libslic3r.h"
#include <libslic3r/GCode/GCodeEditor.hpp>
#include <math.h>

namespace Slic3r {

static const int   guassian_window_size               = 11;
static const int   guassian_r                         = 2;
static const int   guassian_stop_threshold            = 5;
static const float guassian_layer_time_stop_threshold = 3.0;
static const int   max_steps_count             = 1000;

struct CoolingNode
{
    // extruder pos, line pos;
    std::vector<std::pair<int, int>> outwall_line;
    float                            max_feedrate    = 0;
    float                            filter_feedrate = 0;
    double                           rate            = 1;
};

struct OutwallCollection
{
    int                        object_id;
    std::map<int, CoolingNode> cooling_nodes;
};

class SmoothCalculator
{

public:
    std::vector<std::map<int, std::pair<int, int>>> objects_node_range;
    std::vector<std::vector<OutwallCollection>> layers_wall_collection;
    std::vector<float>                          layers_cooling_time;

    SmoothCalculator(const int objects_size, const double gap_limit) : layer_time_smoothing_threshold(gap_limit)
    {
        guassian_filter_generator();
        objects_node_range.resize(objects_size);
    }

    SmoothCalculator(const int objects_size)
    {
        guassian_filter_generator();
        objects_node_range.resize(objects_size);
    }

    void append_data(const std::vector<OutwallCollection> &wall_collection, float cooling_time)
    {
        layers_wall_collection.push_back(wall_collection);
        layers_cooling_time.push_back(cooling_time);
    }

    void append_data(const std::vector<OutwallCollection> &wall_collection)
    {
        layers_wall_collection.push_back(wall_collection);
    }

    void build_node(std::vector<OutwallCollection> &wall_collection, const std::vector<int> &object_label, const std::vector<PerExtruderAdjustments> &per_extruder_adjustments);

    float recaculate_layer_time(int layer_id, std::vector<PerExtruderAdjustments> &extruder_adjustments);

    void smooth_layer_speed();

private:
    // guassian filter
    double guassian_function(double x, double r) {
        return exp(-x * x / (2 * r * r)) / (r * sqrt(2 * PI));
    }

    void guassian_filter_generator() {
        double r = guassian_r;
        int    half_win_size = guassian_window_size / 2;
        for (int start = -half_win_size; start <= half_win_size; ++start) {
            double y = guassian_function(start, r);
            filter_sum += y;
            guassian_filter.push_back(y);
        }
    }

    void init_object_node_range();

    // filter the data
    void layer_speed_filter(const int object_id, const int node_id);

    bool speed_filter_continue(const int object_id, const int node_id);

    // filter the data
    void filter_layer_time();

    bool layer_time_filter_continue();
    void smooth_layer_time();

    std::vector<double>                             guassian_filter;
    double                                          filter_sum                     = .0f;
    float                                           layer_time_smoothing_threshold = 30.0f;
};

} // namespace Slic3r

#endif