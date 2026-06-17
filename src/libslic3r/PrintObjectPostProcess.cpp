#include "Print.hpp"

#include "ClipperUtils.hpp"
#include "I18N.hpp"
#include "Layer.hpp"
#include "Utils.hpp"

#include <boost/log/trivial.hpp>

#include <tbb/parallel_for.h>
#include <tbb/spin_mutex.h>

#define L(s) Slic3r::I18N::translate(s)

namespace Slic3r {

static const float g_min_overhang_percent_for_lift = 0.3f;

#define REGISTER_SUPPORTS_FOR_LIFT

void PrintObject::clear_overhangs_for_lift()
{
    if (!m_shared_object) {
        for (Layer* l : m_layers)
            l->loverhangs.clear();
    }
}

void PrintObject::detect_overhangs_for_lift()
{
    if (this->set_started(posDetectOverhangsForLift)) {
        const float min_overlap = m_config.line_width * g_min_overhang_percent_for_lift;
        size_t num_layers = this->layer_count();
        size_t num_raft_layers = m_slicing_params.raft_layers();

        m_print->set_status(71, L("Detect overhangs for auto-lift"));

        this->clear_overhangs_for_lift();

        tbb::spin_mutex layer_storage_mutex;
        tbb::parallel_for(tbb::blocked_range<size_t>(num_raft_layers + 1, num_layers), [this, min_overlap](const tbb::blocked_range<size_t> &range) {
            for (size_t layer_id = range.begin(); layer_id < range.end(); ++layer_id) {
                Layer &layer       = *m_layers[layer_id];
                Layer &lower_layer = *layer.lower_layer;

                ExPolygons overhangs = diff_ex(layer.lslices, offset_ex(lower_layer.lslices, scale_(min_overlap)));
                layer.loverhangs     = std::move(offset2_ex(overhangs, -0.1f * scale_(m_config.line_width), 0.1f * scale_(m_config.line_width)));

#ifdef REGISTER_SUPPORTS_FOR_LIFT
                // register all supports to avoidance region for lift
                if (layer_id < m_support_layers.size()) {
                    auto &support_layer = m_support_layers[layer_id];
                    if (support_layer) {
                        if (!support_layer->support_islands.empty()) {
                            append(layer.loverhangs,
                                   std::move(offset2_ex(support_layer->support_islands, -0.1f * scale_(m_config.line_width), 0.1f * scale_(m_config.line_width))));
                        } else {
                            ExPolygons support_infill_polygons = union_ex(support_layer->support_fills.polygons_covered_by_spacing(double(coord_t(SCALED_EPSILON))));
                            append(layer.loverhangs, std::move(offset2_ex(support_infill_polygons, -0.1f * scale_(m_config.line_width), 0.1f * scale_(m_config.line_width))));
                        }
                    }
                }
#endif
                layer.loverhangs_bbox = get_extents(layer.loverhangs);
            }
        });
        this->set_done(posDetectOverhangsForLift);
    }
}

void PrintObject::simplify_extrusion_path()
{
    if (this->set_started(posSimplifyWall)) {
        m_print->set_status(75, L("Optimizing toolpath"));
        BOOST_LOG_TRIVIAL(debug) << "Simplify wall extrusion path of object in parallel - start";
        //BBS: walls
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, m_layers.size()),
            [this](const tbb::blocked_range<size_t>& range) {
                for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
                    m_print->throw_if_canceled();
                    m_layers[layer_idx]->simplify_wall_extrusion_path();
                }
            }
        );
        m_print->throw_if_canceled();
        BOOST_LOG_TRIVIAL(debug) << "Simplify wall extrusion path of object in parallel - end";
        this->set_done(posSimplifyWall);
    }

    if (this->set_started(posSimplifyInfill)) {
        m_print->set_status(75, L("Optimizing toolpath"));
        BOOST_LOG_TRIVIAL(debug) << "Simplify infill extrusion path of object in parallel - start";
        //BBS: infills
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, m_layers.size()),
            [this](const tbb::blocked_range<size_t>& range) {
                for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++layer_idx) {
                    m_print->throw_if_canceled();
                    m_layers[layer_idx]->simplify_infill_extrusion_path();
                }
            }
        );
        m_print->throw_if_canceled();
        BOOST_LOG_TRIVIAL(debug) << "Simplify infill extrusion path of object in parallel - end";
        this->set_done(posSimplifyInfill);
    }

    if (this->set_started(posSimplifySupportPath)) {
        m_print->set_status(75, L("Optimizing toolpath"));
        BOOST_LOG_TRIVIAL(debug) << "Simplify extrusion path of support in parallel - start";
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, m_support_layers.size()),
            [this](const tbb::blocked_range<size_t>& range) {
                for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
                    m_print->throw_if_canceled();
                    m_support_layers[layer_idx]->simplify_support_extrusion_path();
                }
            }
        );
        m_print->throw_if_canceled();
        BOOST_LOG_TRIVIAL(debug) << "Simplify extrusion path of support in parallel - end";
        this->set_done(posSimplifySupportPath);
    }
}

} // namespace Slic3r
