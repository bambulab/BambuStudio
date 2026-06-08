#include "Smoothing.hpp"

namespace Slic3r {

void SmoothCalculator::build_node(std::vector<OutwallCollection> &     wall_collection,
                                  const std::vector<int> &             object_label,
                                  const std::vector<PerExtruderAdjustments>  &per_extruder_adjustments)
{
    if (per_extruder_adjustments.empty())
        return;
    // BBS: update outwall feedrate
    // update feedrate of outwall after initial cooling process
    // initial and arrange node collection seq
    for (size_t object_idx = 0; object_idx < object_label.size(); ++object_idx) {
        OutwallCollection object_level;
        object_level.object_id = object_label[object_idx];
        wall_collection.push_back(object_level);
    }

    for (size_t extruder_idx = 0; extruder_idx < per_extruder_adjustments.size(); ++extruder_idx) {
        const PerExtruderAdjustments &extruder_adjustments = per_extruder_adjustments[extruder_idx];
        for (size_t line_idx = 0; line_idx < extruder_adjustments.lines.size(); ++line_idx) {
            const CoolingLine &line = extruder_adjustments.lines[line_idx];
            if (line.outwall_smooth_mark) {
                // search node id
                if (wall_collection[line.object_id].cooling_nodes.count(line.cooling_node_id) == 0) {
                    CoolingNode node;
                    wall_collection[line.object_id].cooling_nodes.emplace(line.cooling_node_id, node);
                }

                CoolingNode &node = wall_collection[line.object_id].cooling_nodes[line.cooling_node_id];
                if (line.type & CoolingLine::TYPE_EXTERNAL_PERIMETER) {
                    node.outwall_line.emplace_back(line_idx, extruder_idx);
                    if (node.max_feedrate < line.feedrate) {
                        node.max_feedrate    = line.feedrate;
                        node.filter_feedrate = node.max_feedrate;
                    }
                }

            }
        }
    }
}


static void exclude_participate_in_speed_slowdown(std::vector<std::pair<int, int>> &   lines,
                                                  std::vector<PerExtruderAdjustments>  &per_extruder_adjustments,
                                                  CoolingNode &                        node)
{
    // BBS: add protect, feedrate will be 0 if the outwall is overhang. just apply not adjust flage
    bool apply_speed = node.max_feedrate > 0 && node.filter_feedrate > 0;
    if (apply_speed) node.rate = node.filter_feedrate / node.max_feedrate;

    for (std::pair<int, int> line_pos : lines) {
        CoolingLine &line = per_extruder_adjustments[line_pos.second].lines[line_pos.first];
        if (apply_speed && line.feedrate > node.filter_feedrate) {
            line.feedrate = node.filter_feedrate;
            line.slowdown = true;
        }

        // not adjust outwal line speed
        line.type = line.type & (~CoolingLine::TYPE_ADJUSTABLE);
        // update time cost
        if (line.feedrate == 0 || line.length == 0)
            line.time = 0;
        else
            line.time = line.length / line.feedrate;
    }
}

float SmoothCalculator::recaculate_layer_time(int layer_id, std::vector<PerExtruderAdjustments> &extruder_adjustments)
{
    // rewrite feedrate
    for (size_t obj_id = 0; obj_id < layers_wall_collection[layer_id].size(); ++obj_id) {
        for (size_t node_id = 0; node_id < layers_wall_collection[layer_id][obj_id].cooling_nodes.size(); ++node_id) {
            CoolingNode &node = layers_wall_collection[layer_id][obj_id].cooling_nodes[node_id];
            // set outwall speed
            exclude_participate_in_speed_slowdown(node.outwall_line, extruder_adjustments, node);
        }
    }

    float layer_time = 0;
    for (PerExtruderAdjustments extruder : extruder_adjustments) {
       layer_time += extruder.collection_line_times_of_extruder();
    }

    return layer_time;
};

void SmoothCalculator::init_object_node_range()
{
    for (size_t object_id = 0; object_id < objects_node_range.size(); ++object_id) {
        for (size_t layer_id = 1; layer_id < layers_wall_collection.size(); ++layer_id) {
            const OutwallCollection &each_object = layers_wall_collection[layer_id][object_id];
            auto                     it          = each_object.cooling_nodes.begin();
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

void SmoothCalculator::smooth_layer_speed()
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

void SmoothCalculator::layer_speed_filter(const int object_id, const int node_id)
{
    int start_pos = guassian_filter.size() / 2;
    // first layer don't need to be smoothed
    int layer_start = objects_node_range[object_id][node_id].first;
    int layer_end   = objects_node_range[object_id][node_id].second;

    // BBS: some layers may empty as the support has indenpendent layer
    for (int layer_id = layer_start; layer_id <= layer_end; ++layer_id) {
        if (layers_wall_collection[layer_id].empty()) continue;

        if (layers_wall_collection[layer_id][object_id].cooling_nodes.count(node_id) == 0) break;

        CoolingNode &node = layers_wall_collection[layer_id][object_id].cooling_nodes[node_id];

        if (node.outwall_line.empty()) continue;

        double conv_sum = 0;
        for (int filter_pos_idx = 0; filter_pos_idx < guassian_filter.size(); ++filter_pos_idx) {
            int remap_data_pos = layer_id - start_pos + filter_pos_idx;

            if (remap_data_pos < layer_start)
                remap_data_pos = layer_start;
            else if (remap_data_pos > layer_end)
                remap_data_pos = layer_end;

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

bool SmoothCalculator::speed_filter_continue(const int object_id, const int node_id)
{
    int layer_id  = objects_node_range[object_id][node_id].first;
    int layer_end = objects_node_range[object_id][node_id].second;

    // BBS: some layers may empty as the support has indenpendent layer
    for (; layer_id < layer_end; ++layer_id) {
        if (std::abs(layers_wall_collection[layer_id + 1][object_id].cooling_nodes[node_id].filter_feedrate -
                     layers_wall_collection[layer_id][object_id].cooling_nodes[node_id].filter_feedrate) > guassian_stop_threshold)
            return true;
    }
    return false;
}

void SmoothCalculator::filter_layer_time()
{
    int start_pos = guassian_filter.size() / 2;
    // first layer don't need to be smoothed
    for (int layer_id = 1; layer_id < layers_cooling_time.size(); ++layer_id) {
        if (layers_cooling_time[layer_id] > layer_time_smoothing_threshold) continue;

        double conv_sum = 0;
        for (int filter_pos_idx = 0; filter_pos_idx < guassian_filter.size(); ++filter_pos_idx) {
            int remap_data_pos = layer_id - start_pos + filter_pos_idx;

            if (remap_data_pos < 1)
                remap_data_pos = 1;
            else if (remap_data_pos > layers_cooling_time.size() - 1)
                remap_data_pos = layers_cooling_time.size() - 1;

            // if the layer time big enough, surface defact will disappear
            double data_temp = layers_cooling_time[remap_data_pos] > layer_time_smoothing_threshold ? layer_time_smoothing_threshold : layers_cooling_time[remap_data_pos];

            conv_sum += guassian_filter[filter_pos_idx] * data_temp;
        }
        double filter_res = conv_sum / filter_sum;
        filter_res        = filter_res > layer_time_smoothing_threshold ? layer_time_smoothing_threshold : filter_res;
        if (filter_res > layers_cooling_time[layer_id]) layers_cooling_time[layer_id] = filter_res;
    }
}

bool SmoothCalculator::layer_time_filter_continue()
{
    for (int layer_id = 1; layer_id < layers_cooling_time.size() - 1; ++layer_id) {
        double layer_time     = layers_cooling_time[layer_id] > layer_time_smoothing_threshold ? layer_time_smoothing_threshold : layers_cooling_time[layer_id];
        double layer_time_cmp = layers_cooling_time[layer_id + 1] > layer_time_smoothing_threshold ? layer_time_smoothing_threshold : layers_cooling_time[layer_id + 1];

        if (std::abs(layer_time - layer_time_cmp) > guassian_layer_time_stop_threshold) return true;
    }
    return false;
}

void SmoothCalculator::smooth_layer_time()
{
    int step_count = 0;
    while (step_count < max_steps_count && layer_time_filter_continue()) {
        step_count++;
        filter_layer_time();
    }
}

} // namespace Slic3r