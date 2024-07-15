#ifndef slic3r_Smoothing_hpp_
#define slic3r_Smoothing_hpp_
#include "../libslic3r.h"

#include <math.h>

namespace Slic3r {

static const int   guassian_window_size               = 11;
static const int   guassian_r                         = 2;
static const int   guassian_stop_threshold            = 5;
static const float guassian_layer_time_stop_threshold = 3.0;
// if the layer time longer than this threshold, ignore it
static const float layer_time_ignore_threshold = 30.0;
static const int   max_steps_count             = 1000;

struct CoolingNode
{
    // extruder pos, line pos;
    std::vector<std::pair<int, int>> outwall_line;
    std::vector<std::pair<int, int>> innerwall_line;
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
    std::vector<double> guassian_filter;
    double              filter_sum = .0f;

public:
    std::vector<std::vector<OutwallCollection>> layers_wall_collection;
    std::vector<float>                               layers_cooling_time;
    std::vector<std::map<int, std::pair<int, int>>> objects_node_range;

    SmoothCalculator(const int print_size, const int objects_size)
    {
        guassian_filter_generator();
        objects_node_range.resize(objects_size);
        layers_wall_collection.resize(print_size);
        layers_cooling_time.resize(print_size);
    }

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

    void init_wall_collection(const int layer_id, const std::vector<int> &object_label)
    {
        for (size_t object_idx = 0; object_idx < object_label.size(); ++object_idx) {
            OutwallCollection object_level;
            object_level.object_id = object_label[object_idx];
            layers_wall_collection[layer_id].push_back(object_level);
        }
    }

    void init_object_node_range() {
        for (size_t object_id = 0; object_id < objects_node_range.size(); ++object_id) {

            for (size_t layer_id = 1; layer_id < layers_wall_collection.size(); ++layer_id) {
                const OutwallCollection &each_object = layers_wall_collection[layer_id][object_id];
                auto it = each_object.cooling_nodes.begin();
                while (it != each_object.cooling_nodes.end()) {
                    if (objects_node_range[object_id].count(it->first) == 0) {
                        objects_node_range[object_id].emplace(it->first, std::pair<int, int>(layer_id, layer_id));
                    } else {
                        objects_node_range[object_id][it->first].second = layer_id;
                    }
                    it++;
                }
            }
        }
    }

    // filter the data
    void layer_speed_filter(const int object_id, const int node_id)
    {
        int start_pos = guassian_filter.size() / 2;
        // first layer don't need to be smoothed
        int layer_id  = objects_node_range[object_id][node_id].first;
        int layer_end = objects_node_range[object_id][node_id].second;

        for (; layer_id <= layer_end; ++layer_id) {
            if (layers_wall_collection[layer_id][object_id].cooling_nodes.count(node_id) == 0)
                break;

            CoolingNode &node = layers_wall_collection[layer_id][object_id].cooling_nodes[node_id];

            if (node.outwall_line.empty())
                continue;

            double conv_sum = 0;
            for (int filter_pos_idx = 0; filter_pos_idx < guassian_filter.size(); ++filter_pos_idx) {
                int remap_data_pos = layer_id - start_pos + filter_pos_idx;

                if (remap_data_pos < 1)
                    remap_data_pos = 1;
                else if (remap_data_pos > layers_wall_collection.size() - 1)
                    remap_data_pos = layers_wall_collection.size() - 1;

                // some node may not start at layer 1
                double remap_data = node.filter_feedrate;
                if (!layers_wall_collection[remap_data_pos][object_id].cooling_nodes[node_id].outwall_line.empty())
                    remap_data = layers_wall_collection[remap_data_pos][object_id].cooling_nodes[node_id].filter_feedrate;

                conv_sum += guassian_filter[filter_pos_idx] * remap_data;
            }
            double filter_res = conv_sum / filter_sum;
            if (filter_res < node.filter_feedrate) node.filter_feedrate = filter_res;
        }
    }

    bool speed_filter_continue(const int object_id, const int node_id)
    {
        int layer_id  = objects_node_range[object_id][node_id].first;
        int layer_end = objects_node_range[object_id][node_id].second;

        for (; layer_id < layer_end; ++layer_id) {
            if (std::abs(layers_wall_collection[layer_id][object_id].cooling_nodes[node_id].outwall_line.empty()))
                continue;

            if (std::abs(layers_wall_collection[layer_id][object_id].cooling_nodes[node_id].filter_feedrate -
                         layers_wall_collection[layer_id + 1][object_id].cooling_nodes[node_id].filter_feedrate) >
                guassian_stop_threshold)
                return true;
        }
        return false;
    }

    void smooth_layer_speed()
    {
        init_object_node_range();

        for (size_t obj_id = 0; obj_id < objects_node_range.size(); ++obj_id) {
            auto it = objects_node_range[obj_id].begin();
            while (it != objects_node_range[obj_id].end()) {
                int step_count = 0;
                while (step_count < max_steps_count && speed_filter_continue(obj_id, it->first)) {
                    step_count++;
                    layer_speed_filter(obj_id, it->first);
                }
                it++;
            }
        }
    }

    // filter the data
    void filter_layer_time()
    {
        int start_pos = guassian_filter.size() / 2;
        // first layer don't need to be smoothed
        for (int layer_id = 1; layer_id < layers_cooling_time.size(); ++layer_id) {
            if (layers_cooling_time[layer_id] > layer_time_ignore_threshold)
                continue;

            double conv_sum = 0;
            for (int filter_pos_idx = 0; filter_pos_idx < guassian_filter.size(); ++filter_pos_idx) {
                int remap_data_pos = layer_id - start_pos + filter_pos_idx;

                if (remap_data_pos < 1)
                    remap_data_pos = 1;
                else if (remap_data_pos > layers_cooling_time.size() - 1)
                    remap_data_pos = layers_cooling_time.size() - 1;

                // if the layer time big enough, surface defact will disappear
                double data_temp = layers_cooling_time[remap_data_pos] > layer_time_ignore_threshold ? layer_time_ignore_threshold : layers_cooling_time[remap_data_pos];

                conv_sum += guassian_filter[filter_pos_idx] * data_temp;
            }
            double filter_res = conv_sum / filter_sum;
            filter_res        = filter_res > layer_time_ignore_threshold ? layer_time_ignore_threshold : filter_res;
            if (filter_res > layers_cooling_time[layer_id])
                layers_cooling_time[layer_id] = filter_res;
        }
    }

    bool layer_time_filter_continue()
    {
        for (int layer_id = 1; layer_id < layers_cooling_time.size() - 1; ++layer_id) {
            double layer_time     = layers_cooling_time[layer_id] > layer_time_ignore_threshold ? layer_time_ignore_threshold : layers_cooling_time[layer_id];
            double layer_time_cmp = layers_cooling_time[layer_id + 1] > layer_time_ignore_threshold ? layer_time_ignore_threshold : layers_cooling_time[layer_id + 1];

            if (std::abs(layer_time - layer_time_cmp) > guassian_layer_time_stop_threshold)
                return true;
        }
        return false;
    }

    void smooth_layer_time()
    {
        int step_count = 0;
        while (step_count < max_steps_count && layer_time_filter_continue()) {
            step_count++;
            filter_layer_time();
        }
    }
};

} // namespace Slic3r

#endif