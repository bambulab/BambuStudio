#include "Print.hpp"

#include "ClipperUtils.hpp"
#include "I18N.hpp"
#include "Layer.hpp"
#include "Surface.hpp"
#include "Utils.hpp"

#include <cstdio>
#include <map>
#include <utility>

#include <boost/log/trivial.hpp>

#include <tbb/parallel_for.h>

#define L(s) Slic3r::I18N::translate(s)

namespace Slic3r {

void PrintObject::merge_layer_node(const size_t layer_id, int &max_merged_id, std::map<int, std::vector<std::pair<int, int>>> &node_record)
{
    Layer *this_layer = m_layers[layer_id];
    std::vector<LoopNode> &loop_nodes = this_layer->loop_nodes;
    for (size_t idx = 0; idx < loop_nodes.size(); ++idx) {
        //new cool node
        if (loop_nodes[idx].lower_node_id.empty()) {
            max_merged_id++;
            loop_nodes[idx].merged_id = max_merged_id;
            std::vector<std::pair<int, int>> node_pos;
            node_pos.emplace_back(layer_id, idx);
            node_record.emplace(max_merged_id, node_pos);
            continue;
        }

        //it should finds key in map
        if (loop_nodes[idx].lower_node_id.size() == 1) {
            loop_nodes[idx].merged_id = m_layers[layer_id - 1]->loop_nodes[loop_nodes[idx].lower_node_id.front()].merged_id;
            node_record[loop_nodes[idx].merged_id].emplace_back(layer_id, idx);
            continue;
        }

        //min index
        int min_merged_id = -1;
        std::vector<int> appear_id;
        for (size_t lower_idx = 0; lower_idx < loop_nodes[idx].lower_node_id.size(); ++lower_idx) {
            int id = m_layers[layer_id - 1]->loop_nodes[loop_nodes[idx].lower_node_id[lower_idx]].merged_id;
            if (min_merged_id == -1 || min_merged_id > id)
                min_merged_id = id;
            appear_id.push_back(id);
        }

        loop_nodes[idx].merged_id = min_merged_id;
        node_record[min_merged_id].emplace_back(layer_id, idx);

        //update other node merged id
        for (size_t appear_node_idx = 0; appear_node_idx < appear_id.size(); ++appear_node_idx) {
            if (appear_id[appear_node_idx] == min_merged_id)
                continue;

            auto it = node_record.find(appear_id[appear_node_idx]);
            //protect
            if (it == node_record.end())
                continue;

            std::vector<std::pair<int, int>> &appear_node_pos = it->second;

            for (size_t node_idx = 0; node_idx < appear_node_pos.size(); ++node_idx) {
                int node_layer = appear_node_pos[node_idx].first;
                int node_pos = appear_node_pos[node_idx].second;

                LoopNode &node = m_layers[node_layer]->loop_nodes[node_pos];

                node.merged_id = min_merged_id;
                node_record[min_merged_id].emplace_back(node_layer, node_pos);
            }
            node_record.erase(it);
        }
    }
}

// 1) Merges typed region slices into stInternal type.
// 2) Increases an "extra perimeters" counter at region slices where needed.
// 3) Generates perimeters, gap fills and fill regions (fill regions of type stInternal).
void PrintObject::make_perimeters()
{
    // prerequisites
    this->slice();

    if (! this->set_started(posPerimeters))
        return;

    m_print->set_status(15, L("Generating walls"));
    BOOST_LOG_TRIVIAL(info) << "Generating walls..." << log_memory_info();

    // Revert the typed slices into untyped slices.
    if (m_typed_slices) {
        for (Layer *layer : m_layers) {
            layer->restore_untyped_slices();
            m_print->throw_if_canceled();
        }
        m_typed_slices = false;
    }

    // compare each layer to the one below, and mark those slices needing
    // one additional inner perimeter, like the top of domed objects-

    // this algorithm makes sure that at least one perimeter is overlapping
    // but we don't generate any extra perimeter if fill density is zero, as they would be floating
    // inside the object - infill_only_where_needed should be the method of choice for printing
    // hollow objects
    for (size_t region_id = 0; region_id < this->num_printing_regions(); ++ region_id) {
        const PrintRegion &region = this->printing_region(region_id);
        //BBS: remove extra_perimeters, always false
        //if (! region.config().extra_perimeters || region.config().wall_loops == 0 || region.config().sparse_infill_density == 0 || this->layer_count() < 2)
            continue;

        BOOST_LOG_TRIVIAL(debug) << "Generating extra perimeters for region " << region_id << " in parallel - start";
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, m_layers.size() - 1),
            [this, &region, region_id](const tbb::blocked_range<size_t>& range) {
                for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
                    m_print->throw_if_canceled();
                    LayerRegion &layerm                     = *m_layers[layer_idx]->get_region(region_id);
                    const LayerRegion &upper_layerm         = *m_layers[layer_idx+1]->get_region(region_id);
                    const Polygons upper_layerm_polygons    = to_polygons(upper_layerm.slices.surfaces);
                    const double total_loop_length      = total_length(upper_layerm_polygons);
                    const coord_t perimeter_spacing     = layerm.flow(frPerimeter).scaled_spacing();
                    const Flow ext_perimeter_flow       = layerm.flow(frExternalPerimeter);
                    const coord_t ext_perimeter_width   = ext_perimeter_flow.scaled_width();
                    const coord_t ext_perimeter_spacing = ext_perimeter_flow.scaled_spacing();

                    for (Surface &slice : layerm.slices.surfaces) {
                        for (;;) {
                            const coord_t perimeters_thickness = ext_perimeter_width/2 + ext_perimeter_spacing/2
                                + (region.config().wall_loops-1 + slice.extra_perimeters) * perimeter_spacing;
                            const coord_t critical_area_depth = coord_t(perimeter_spacing * 1.5);
                            const Polygons critical_area = diff(
                                offset(slice.expolygon, float(- perimeters_thickness)),
                                offset(slice.expolygon, float(- perimeters_thickness - critical_area_depth))
                            );
                            const Polylines intersection = intersection_pl(to_polylines(upper_layerm_polygons), critical_area);
                            if (total_length(intersection) <=  total_loop_length*0.3)
                                break;
                            ++ slice.extra_perimeters;
                        }
                        #ifdef DEBUG
                            if (slice.extra_perimeters > 0)
                                printf("  adding %d more perimeter(s) at layer %zu\n", slice.extra_perimeters, layer_idx);
                        #endif
                    }
                }
            });
        m_print->throw_if_canceled();
        BOOST_LOG_TRIVIAL(debug) << "Generating extra perimeters for region " << region_id << " in parallel - end";
    }

    BOOST_LOG_TRIVIAL(debug) << "Generating perimeters in parallel - start";
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, m_layers.size()),
        [this](const tbb::blocked_range<size_t>& range) {
            for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
                m_print->throw_if_canceled();
                m_layers[layer_idx]->make_perimeters();
            }
        }
    );
    m_print->throw_if_canceled();
    BOOST_LOG_TRIVIAL(debug) << "Generating perimeters in parallel - end";

    if (this->m_print->m_config.z_direction_outwall_speed_continuous) {
        BOOST_LOG_TRIVIAL(debug) << "Calculating perimeters connection in parallel - start";
        tbb::parallel_for(tbb::blocked_range<size_t>(0, m_layers.size()), [this](const tbb::blocked_range<size_t> &range) {
            for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++layer_idx) {
                m_print->throw_if_canceled();
                if (layer_idx > 1)
                    m_layers[layer_idx]->calculate_perimeter_continuity(m_layers[layer_idx - 1]->loop_nodes);
            }
        });

        m_print->throw_if_canceled();
        BOOST_LOG_TRIVIAL(debug) << "Calculating perimeters connection in parallel - end";

        BOOST_LOG_TRIVIAL(debug) << "Calculating cooling nodes - start";

        int max_merged_id = -1;
        std::map<int, std::vector<std::pair<int, int>>> node_record;
        for (size_t layer_idx = 1; layer_idx < m_layers.size(); ++layer_idx) {
            m_print->throw_if_canceled();
            merge_layer_node(layer_idx, max_merged_id, node_record);
        }
        m_print->throw_if_canceled();
        BOOST_LOG_TRIVIAL(debug) << "Calculating cooling nodes - end";

        BOOST_LOG_TRIVIAL(debug) << "Recrod cooling_node id for each extrusion in parallel - start";
        tbb::parallel_for(tbb::blocked_range<size_t>(0, m_layers.size()), [this](const tbb::blocked_range<size_t> &range) {
            for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++layer_idx) {
                m_print->throw_if_canceled();
                if (layer_idx > 1)
                    m_layers[layer_idx]->recrod_cooling_node_for_each_extrusion();
            }
        });

        m_print->throw_if_canceled();
        BOOST_LOG_TRIVIAL(debug) << "Recrod cooling_node id for each extrusion in parallel - end";
    }
    this->set_done(posPerimeters);
}

} // namespace Slic3r
