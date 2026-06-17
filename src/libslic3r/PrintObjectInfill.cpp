#include "Exception.hpp"
#include "Print.hpp"
#include "BoundingBox.hpp"
#include "ClipperUtils.hpp"
#include "Geometry.hpp"
#include "I18N.hpp"
#include "Layer.hpp"
#include "MutablePolygon.hpp"
#include "Surface.hpp"
#include "Slicing.hpp"
#include "Tesselate.hpp"
#include "TriangleMeshSlicer.hpp"
#include "Utils.hpp"
#include "Fill/FillAdaptive.hpp"
#include "Fill/FillLightning.hpp"
#include "Format/STL.hpp"
#include "InternalBridgeDetector.hpp"
#include "AABBTreeLines.hpp"

#include <float.h>
#include <string_view>
#include <utility>

#include <boost/log/trivial.hpp>

#include <tbb/parallel_for.h>
#include <tbb/concurrent_vector.h>
#include <tbb/concurrent_unordered_set.h>
#include <tbb/spin_mutex.h>
#include <tbb/task_group.h>

#include <Shiny/Shiny.h>

#include "format.hpp"

#define L(s) Slic3r::I18N::translate(s)

namespace Slic3r {

void PrintObject::prepare_infill()
{
    if (! this->set_started(posPrepareInfill))
        return;
    m_print->set_status(25, L("Generating infill regions"));
    if (m_typed_slices) {
        // To improve robustness of detect_surfaces_type() when reslicing (working with typed slices), see GH issue #7442.
        // The preceding step (perimeter generator) only modifies extra_perimeters and the extra perimeters are only used by discover_vertical_shells()
        // with more than a single region. If this step does not use Surface::extra_perimeters or Surface::extra_perimeters is always zero, it is safe
        // to reset to the untyped slices before re-runnning detect_surfaces_type().
        for (Layer *layer : m_layers) {
            layer->restore_untyped_slices_no_extra_perimeters();
            m_print->throw_if_canceled();
        }
    }

    std::vector<std::vector<SurfaceCollection>> slice_surfaces_cpy;
    this->detect_surfaces_type(slice_surfaces_cpy);
    m_print->throw_if_canceled();

    BOOST_LOG_TRIVIAL(info) << "Preparing fill surfaces..." << log_memory_info();
    for (auto *layer : m_layers)
        for (auto *region : layer->m_regions) {
            region->prepare_fill_surfaces();
            m_print->throw_if_canceled();
        }

    this->discover_vertical_shells();
    m_print->throw_if_canceled();

    this->process_external_surfaces();
    m_print->throw_if_canceled();

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
    for (size_t region_id = 0; region_id < this->num_printing_regions(); ++region_id) {
        for (const Layer *layer : m_layers) {
            LayerRegion *layerm = layer->m_regions[region_id];
            layerm->export_region_slices_to_svg_debug("6_discover_vertical_shells-final");
            layerm->export_region_fill_surfaces_to_svg_debug("6_discover_vertical_shells-final");
        }
    }
#endif

    this->discover_horizontal_shells();
    m_print->throw_if_canceled();

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
    for (size_t region_id = 0; region_id < this->num_printing_regions(); ++region_id) {
        for (const Layer *layer : m_layers) {
            LayerRegion *layerm = layer->m_regions[region_id];
            layerm->export_region_slices_to_svg_debug("7_discover_horizontal_shells-final");
            layerm->export_region_fill_surfaces_to_svg_debug("7_discover_horizontal_shells-final");
        }
    }
#endif

    if (m_config.interlocking_beam.value)
        discover_shell_for_perimeters();
    reset_slice_surfaces(slice_surfaces_cpy);
    this->clip_fill_surfaces();
    m_print->throw_if_canceled();

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
    for (size_t region_id = 0; region_id < this->num_printing_regions(); ++region_id) {
        for (const Layer *layer : m_layers) {
            LayerRegion *layerm = layer->m_regions[region_id];
            layerm->export_region_slices_to_svg_debug("8_clip_surfaces-final");
            layerm->export_region_fill_surfaces_to_svg_debug("8_clip_surfaces-final");
        }
    }
#endif

    this->bridge_over_infill();
    m_print->throw_if_canceled();

    this->combine_infill();
    m_print->throw_if_canceled();

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
    for (size_t region_id = 0; region_id < this->num_printing_regions(); ++region_id) {
        for (const Layer *layer : m_layers) {
            LayerRegion *layerm = layer->m_regions[region_id];
            layerm->export_region_slices_to_svg_debug("9_prepare_infill-final");
            layerm->export_region_fill_surfaces_to_svg_debug("9_prepare_infill-final");
        }
    }
    for (const Layer *layer : m_layers) {
        layer->export_region_slices_to_svg_debug("9_prepare_infill-final");
        layer->export_region_fill_surfaces_to_svg_debug("9_prepare_infill-final");
    }
#endif

    this->set_done(posPrepareInfill);
}

void PrintObject::infill()
{
    this->prepare_infill();

    if (this->set_started(posInfill)) {
        m_print->set_status(35, L("Generating infill toolpath"));

        const auto &adaptive_fill_octree = this->m_adaptive_fill_octrees.first;
        const auto &support_fill_octree  = this->m_adaptive_fill_octrees.second;

        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, m_layers.size()),
            [this, &adaptive_fill_octree = adaptive_fill_octree, &support_fill_octree = support_fill_octree](const tbb::blocked_range<size_t> &range) {
                for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++layer_idx) {
                    m_print->throw_if_canceled();
                    m_layers[layer_idx]->make_fills(adaptive_fill_octree.get(), support_fill_octree.get(), this->m_lightning_generator.get());
                }
            });
        m_print->throw_if_canceled();
        BOOST_LOG_TRIVIAL(debug) << "Filling layers in parallel - end";
        this->set_done(posInfill);
    }
}

void PrintObject::ironing()
{
    if (this->set_started(posIroning)) {
        BOOST_LOG_TRIVIAL(debug) << "Ironing in parallel - start";
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, m_layers.size()),
            [this](const tbb::blocked_range<size_t> &range) {
                for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++layer_idx) {
                    m_print->throw_if_canceled();
                    m_layers[layer_idx]->make_ironing();
                }
            });
        m_print->throw_if_canceled();
        BOOST_LOG_TRIVIAL(debug) << "Ironing in parallel - end";
        this->set_done(posIroning);
    }
}

std::pair<FillAdaptive::OctreePtr, FillAdaptive::OctreePtr> PrintObject::prepare_adaptive_infill_data(
    const std::vector<std::pair<const Surface *, float>> &surfaces_w_bottom_z) const
{
    using namespace FillAdaptive;

    auto [adaptive_line_spacing, support_line_spacing] = adaptive_fill_line_spacing(*this);
    if ((adaptive_line_spacing == 0. && support_line_spacing == 0.) || this->layers().empty())
        return std::make_pair(OctreePtr(), OctreePtr());

    indexed_triangle_set mesh = this->model_object()->raw_indexed_triangle_set();
    auto                 to_octree = transform_to_octree().toRotationMatrix();
    its_transform(mesh, to_octree * this->trafo_centered(), true);

    std::vector<std::vector<Vec3d>> overhangs(std::max(surfaces_w_bottom_z.size(), size_t(1)));
    tbb::parallel_for(tbb::blocked_range<int>(0, surfaces_w_bottom_z.size()),
        [this, &to_octree, &overhangs, &surfaces_w_bottom_z](const tbb::blocked_range<int> &range) {
            for (int surface_idx = range.begin(); surface_idx < range.end(); ++surface_idx) {
                std::vector<Vec3d> &out = overhangs[surface_idx];
                m_print->throw_if_canceled();
                append(out, triangulate_expolygon_3d(surfaces_w_bottom_z[surface_idx].first->expolygon, surfaces_w_bottom_z[surface_idx].second));
                for (Vec3d &p : out)
                    p = (to_octree * p).eval();
            }
        });
    for (size_t i = 1; i < overhangs.size(); ++i)
        append(overhangs.front(), std::move(overhangs[i]));

    return std::make_pair(
        adaptive_line_spacing ? build_octree(mesh, overhangs.front(), adaptive_line_spacing, false) : OctreePtr(),
        support_line_spacing ? build_octree(mesh, overhangs.front(), support_line_spacing, true) : OctreePtr());
}

FillLightning::GeneratorPtr PrintObject::prepare_lightning_infill_data()
{
    bool has_lightning_infill = false;
    for (size_t region_id = 0; region_id < this->num_printing_regions(); ++region_id)
        if (const PrintRegionConfig &config = this->printing_region(region_id).config(); config.sparse_infill_density > 0 && config.sparse_infill_pattern == ipLightning) {
            has_lightning_infill = true;
            break;
        }

    return has_lightning_infill ? FillLightning::build_generator(std::as_const(*this), [this]() -> void { this->throw_if_canceled(); })
                                : FillLightning::GeneratorPtr();
}


void PrintObject::reset_slice_surfaces(const std::vector<std::vector<SurfaceCollection>> &slice_surfaces_cpy){
    //reset infill surface and slice data back
    for (size_t region_id = 0; region_id < this->num_printing_regions(); ++ region_id) {
        for (size_t idx_layer = 0; idx_layer < m_layers.size(); ++ idx_layer) {

            Layer       *layer  = m_layers[idx_layer];
            LayerRegion *layerm = layer->m_regions[region_id];
            if (layerm->region().config().infill_instead_top_bottom_surfaces && layerm->region().config().sparse_infill_pattern == ipLockedZag) {
                layerm->slices = slice_surfaces_cpy[idx_layer][region_id];
                ExPolygons exps;
                layerm->fill_surfaces.keep_type(SurfaceType::stInternal, exps);
                layerm->fill_surfaces.append(exps, SurfaceType::stTop);
            }
        }
    }
}

// This function analyzes slices of a region (SurfaceCollection slices).
// Each region slice (instance of Surface) is analyzed, whether it is supported or whether it is the top surface.
// Initially all slices are of type stInternal.
// Slices are compared against the top / bottom slices and regions and classified to the following groups:
// stTop          - Part of a region, which is not covered by any upper layer. This surface will be filled with a top solid infill.
// stBottomBridge - Part of a region, which is not fully supported, but it hangs in the air, or it hangs losely on a support or a raft.
// stBottom       - Part of a region, which is not supported by the same region, but it is supported either by another region, or by a soluble interface layer.
// stInternal     - Part of a region, which is supported by the same region type.
// If a part of a region is of stBottom and stTop, the stBottom wins.
void PrintObject::detect_surfaces_type(std::vector<std::vector<SurfaceCollection>> &slice_surfaces_cpy)
{
    BOOST_LOG_TRIVIAL(info) << "Detecting solid surfaces..." << log_memory_info();

    // Interface shells: the intersecting parts are treated as self standing objects supporting each other.
    // Each of the objects will have a full number of top / bottom layers, even if these top / bottom layers
    // are completely hidden inside a collective body of intersecting parts.
    // This is useful if one of the parts is to be dissolved, or if it is transparent and the internal shells
    // should be visible.
    bool spiral_mode      = this->print()->config().spiral_mode.value;
    bool interface_shells = ! spiral_mode && m_config.interface_shells.value;
    size_t num_layers     = spiral_mode ? std::min(size_t(this->printing_region(0).config().bottom_shell_layers), m_layers.size()) : m_layers.size();

    for (size_t region_id = 0; region_id < this->num_printing_regions(); ++ region_id) {
        BOOST_LOG_TRIVIAL(debug) << "Detecting solid surfaces for region " << region_id << " in parallel - start";
#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
        for (Layer *layer : m_layers)
            layer->m_regions[region_id]->export_region_fill_surfaces_to_svg_debug("1_detect_surfaces_type-initial");
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

        // If interface shells are allowed, the region->surfaces cannot be overwritten as they may be used by other threads.
        // Cache the result of the following parallel_loop.
        std::vector<Surfaces> surfaces_new;
        if (interface_shells)
            surfaces_new.assign(num_layers, Surfaces());

        slice_surfaces_cpy.resize(m_layers.size());
        // interface_shell 启用与否，决定着是否区分不同材料。开启后，不同材料间的接触面都会被识别为顶面、底面
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0,
            	spiral_mode ?
            		// In spiral vase mode, reserve the last layer for the top surface if more than 1 layer is planned for the vase bottom.
            		((num_layers > 1) ? num_layers - 1 : num_layers) :
            		// In non-spiral vase mode, go over all layers.
            		m_layers.size()),
                [this, spiral_mode, region_id, interface_shells, &surfaces_new, &slice_surfaces_cpy](const tbb::blocked_range<size_t> &range) {
                // BBS coconut: can't set to stBottom when soluable support is used, as the support may not be actaully generated, e.g. when "on build plate only" option is enabled. See github #3507.
                SurfaceType surface_type_bottom_other = stBottomBridge;
                for (size_t idx_layer = range.begin(); idx_layer < range.end(); ++ idx_layer) {
                    m_print->throw_if_canceled();
                    // BOOST_LOG_TRIVIAL(trace) << "Detecting solid surfaces for region " << region_id << " and layer " << layer->print_z;
                    Layer       *layer  = m_layers[idx_layer];
                    LayerRegion *layerm = layer->m_regions[region_id];
                    slice_surfaces_cpy[idx_layer].resize(layer->m_regions.size());
                    //record surface data
                    if (layerm->region().config().infill_instead_top_bottom_surfaces && layerm->region().config().sparse_infill_pattern == ipLockedZag) {
                        // layerm->fill_surfaces_copy = layerm->fill_expolygons;
                        slice_surfaces_cpy[idx_layer][region_id] = layerm->slices;
                    }
                    // comparison happens against the *full* slices (considering all regions)
                    // unless internal shells are requested
                    Layer       *upper_layer = (idx_layer + 1 < this->layer_count()) ? m_layers[idx_layer + 1] : nullptr;
                    Layer       *lower_layer = (idx_layer > 0) ? m_layers[idx_layer - 1] : nullptr;
                    // collapse very narrow parts (using the safety offset in the diff is not enough)
                    float        offset = layerm->flow(frExternalPerimeter).scaled_width() / 10.f;

                    bool detect_top = spiral_mode || layerm->region().config().top_shell_layers;
                    bool detect_bottom = spiral_mode || layerm->region().config().bottom_shell_layers;

                    // find top surfaces (difference between current surfaces
                    // of current layer and upper one)
                    Surfaces top;
                    if (detect_top) {
                        if (upper_layer) {
                            ExPolygons upper_slices = interface_shells ?
                                diff_ex(layerm->slices.surfaces, upper_layer->m_regions[region_id]->slices.surfaces, ApplySafetyOffset::Yes) :
                                diff_ex(layerm->slices.surfaces, upper_layer->lslices, ApplySafetyOffset::Yes);
                            surfaces_append(top, opening_ex(upper_slices, offset), stTop);
                        }
                        else {
                            // if no upper layer, all surfaces of this one are solid
                            // we clone surfaces because we're going to clear the slices collection
                            top = layerm->slices.surfaces;
                            for (Surface& surface : top)
                                surface.surface_type = stTop;
                        }
                    }

                    // Find bottom surfaces (difference between current surfaces of current layer and lower one).
                    Surfaces bottom;
                    if (detect_bottom) {
                        if (lower_layer) {
#if 0
                            //FIXME Why is this branch failing t\multi.t ?
                            Polygons lower_slices = interface_shells ?
                                to_polygons(lower_layer->get_region(region_id)->slices.surfaces) :
                                to_polygons(lower_layer->slices);
                            surfaces_append(bottom,
                                opening_ex(diff(layerm->slices.surfaces, lower_slices, true), offset),
                                surface_type_bottom_other);
#else
                            // Any surface lying on the void is a true bottom bridge (an overhang)
                            surfaces_append(
                                bottom,
                                opening_ex(
                                    diff_ex(layerm->slices.surfaces, lower_layer->lslices, ApplySafetyOffset::Yes),//完全悬空
                                    offset),
                                surface_type_bottom_other);
                            // if user requested internal shells, we need to identify surfaces
                            // lying on other slices not belonging to this region
                            if (interface_shells) {
                                // non-bridging bottom surfaces: any part of this layer lying
                                // on something else, excluding those lying on our own region
                                surfaces_append(
                                    bottom,
                                    opening_ex(
                                        diff_ex(
                                            intersection(layerm->slices.surfaces, lower_layer->lslices), // 先扣掉完全悬空
                                            lower_layer->m_regions[region_id]->slices.surfaces,//再扣掉同材料的区域
                                            ApplySafetyOffset::Yes),
                                        offset),
                                    stBottom);
                            }
#endif
                        }
                        else {
                            // if no lower layer, all surfaces of this one are solid
                            // we clone surfaces because we're going to clear the slices collection
                            bottom = layerm->slices.surfaces;
                            for (Surface& surface : bottom)
                                surface.surface_type = stBottom;
                        }
                    }

                    // now, if the object contained a thin membrane, we could have overlapping bottom
                    // and top surfaces; let's do an intersection to discover them and consider them
                    // as bottom surfaces (to allow for bridge detection)
                    if (! top.empty() && ! bottom.empty()) {
                        Polygons top_polygons = to_polygons(std::move(top));
                        top.clear();
                        surfaces_append(top, diff_ex(top_polygons, bottom), stTop);
                    }

        #ifdef SLIC3R_DEBUG_SLICE_PROCESSING
                    {
                        static int iRun = 0;
                        std::vector<std::pair<Slic3r::ExPolygons, SVG::ExPolygonAttributes>> expolygons_with_attributes;
                        expolygons_with_attributes.emplace_back(std::make_pair(union_ex(top),                           SVG::ExPolygonAttributes("green")));
                        expolygons_with_attributes.emplace_back(std::make_pair(union_ex(bottom),                        SVG::ExPolygonAttributes("brown")));
                        expolygons_with_attributes.emplace_back(std::make_pair(to_expolygons(layerm->slices.surfaces),  SVG::ExPolygonAttributes("black")));
                        SVG::export_expolygons(debug_out_path("1_detect_surfaces_type_%d_region%d-layer_%f.svg", iRun ++, region_id, layer->print_z).c_str(), expolygons_with_attributes);
                    }
        #endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

                    // save surfaces to layer
                    Surfaces &surfaces_out = interface_shells ? surfaces_new[idx_layer] : layerm->slices.surfaces;
                    Surfaces  surfaces_backup;
                    if (! interface_shells) {
                        surfaces_backup = std::move(surfaces_out);
                        surfaces_out.clear();
                    }
                    const Surfaces &surfaces_prev = interface_shells ? layerm->slices.surfaces : surfaces_backup;

                    // find internal surfaces (difference between top/bottom surfaces and others)
                    {
                        Polygons topbottom = to_polygons(top);
                        polygons_append(topbottom, to_polygons(bottom));
                        surfaces_append(surfaces_out, diff_ex(surfaces_prev, topbottom), stInternal);
                    }

                    surfaces_append(surfaces_out, std::move(top));
                    surfaces_append(surfaces_out, std::move(bottom));

        #ifdef SLIC3R_DEBUG_SLICE_PROCESSING
                    layerm->export_region_slices_to_svg_debug("detect_surfaces_type-final");
        #endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
                }
            }
        ); // for each layer of a region
        m_print->throw_if_canceled();

        if (interface_shells) {
            // Move surfaces_new to layerm->slices.surfaces
            for (size_t idx_layer = 0; idx_layer < num_layers; ++ idx_layer)
                m_layers[idx_layer]->m_regions[region_id]->slices.surfaces = std::move(surfaces_new[idx_layer]);
        }

        if (spiral_mode) {
        	if (num_layers > 1)
	        	// Turn the last bottom layer infill to a top infill, so it will be extruded with a proper pattern.
	        	m_layers[num_layers - 1]->m_regions[region_id]->slices.set_type(stTop);
	        for (size_t i = num_layers; i < m_layers.size(); ++ i)
	        	m_layers[i]->m_regions[region_id]->slices.set_type(stInternal);
        }

        BOOST_LOG_TRIVIAL(debug) << "Detecting solid surfaces for region " << region_id << " - clipping in parallel - start";
        // Fill in layerm->fill_surfaces by trimming the layerm->slices by the cummulative layerm->fill_surfaces.
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, m_layers.size()),
            [this, region_id](const tbb::blocked_range<size_t>& range) {
                for (size_t idx_layer = range.begin(); idx_layer < range.end(); ++ idx_layer) {
                    m_print->throw_if_canceled();
                    LayerRegion *layerm = m_layers[idx_layer]->m_regions[region_id];
                    layerm->slices_to_fill_surfaces_clipped();
#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
                    layerm->export_region_fill_surfaces_to_svg_debug("1_detect_surfaces_type-final");
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
                } // for each layer of a region
            });
        m_print->throw_if_canceled();
        BOOST_LOG_TRIVIAL(debug) << "Detecting solid surfaces for region " << region_id << " - clipping in parallel - end";
    } // for each this->print->region_count

    // Mark the object to have the region slices classified (typed, which also means they are split based on whether they are supported, bridging, top layers etc.)
    m_typed_slices = true;
}

void PrintObject::process_external_surfaces()
{
    BOOST_LOG_TRIVIAL(info) << "Processing external surfaces..." << log_memory_info();

    // Cached surfaces covered by some extrusion, defining regions, over which the from the surfaces one layer higher are allowed to expand.
    std::vector<Polygons> surfaces_covered;
    // Is there any printing region, that has zero infill? If so, then we don't want the expansion to be performed over the complete voids, but only
    // over voids, which are supported by the layer below.
    bool 				  has_voids = false;
	for (size_t region_id = 0; region_id < this->num_printing_regions(); ++ region_id)
		if (this->printing_region(region_id).config().sparse_infill_density == 0) {
			has_voids = true;
			break;
		}
	if (has_voids && m_layers.size() > 1) {
	    // All but stInternal fill surfaces will get expanded and possibly trimmed.
	    std::vector<unsigned char> layer_expansions_and_voids(m_layers.size(), false);
	    for (size_t layer_idx = 1; layer_idx < m_layers.size(); ++ layer_idx) {
	    	const Layer *layer = m_layers[layer_idx];
	    	bool expansions = false;
	    	bool voids      = false;
	    	for (const LayerRegion *layerm : layer->regions()) {
	    		for (const Surface &surface : layerm->fill_surfaces.surfaces) {
	    			if (surface.surface_type == stInternal)
	    				voids = true;
	    			else
	    				expansions = true;
	    			if (voids && expansions) {
	    				layer_expansions_and_voids[layer_idx] = true;
	    				goto end;
	    			}
	    		}
	    	}
		end:;
		}
	    BOOST_LOG_TRIVIAL(debug) << "Collecting surfaces covered with extrusions in parallel - start";
	    surfaces_covered.resize(m_layers.size() - 1, Polygons());
    	auto unsupported_width = - float(scale_(0.3 * EXTERNAL_INFILL_MARGIN));
	    tbb::parallel_for(
	        tbb::blocked_range<size_t>(0, m_layers.size() - 1),
	        [this, &surfaces_covered, &layer_expansions_and_voids, unsupported_width](const tbb::blocked_range<size_t>& range) {
	            for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx)
	            	if (layer_expansions_and_voids[layer_idx + 1]) {
		                m_print->throw_if_canceled();
		                Polygons voids;
		                for (const LayerRegion *layerm : m_layers[layer_idx]->regions()) {
		                	if (layerm->region().config().sparse_infill_density.value == 0.)
		                		for (const Surface &surface : layerm->fill_surfaces.surfaces)
		                			// Shrink the holes, let the layer above expand slightly inside the unsupported areas.
		                			polygons_append(voids, offset(surface.expolygon, unsupported_width));
		                }
		                surfaces_covered[layer_idx] = diff(m_layers[layer_idx]->lslices, voids);
	            	}
	        }
	    );
	    m_print->throw_if_canceled();
	    BOOST_LOG_TRIVIAL(debug) << "Collecting surfaces covered with extrusions in parallel - end";
	}

	for (size_t region_id = 0; region_id < this->num_printing_regions(); ++region_id) {
        BOOST_LOG_TRIVIAL(debug) << "Processing external surfaces for region " << region_id << " in parallel - start";
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, m_layers.size()),
            [this, &surfaces_covered, region_id](const tbb::blocked_range<size_t>& range) {
                for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
                    m_print->throw_if_canceled();
                    // BOOST_LOG_TRIVIAL(trace) << "Processing external surface, layer" << m_layers[layer_idx]->print_z;
                    m_layers[layer_idx]->get_region(int(region_id))->process_external_surfaces(
                    	(layer_idx == 0) ? nullptr : m_layers[layer_idx - 1],
                    	(layer_idx == 0 || surfaces_covered.empty() || surfaces_covered[layer_idx - 1].empty()) ? nullptr : &surfaces_covered[layer_idx - 1]);
                }
            }
        );
        m_print->throw_if_canceled();
        BOOST_LOG_TRIVIAL(debug) << "Processing external surfaces for region " << region_id << " in parallel - end";
    }
}

void PrintObject::discover_vertical_shells()
{
    PROFILE_FUNC();

    BOOST_LOG_TRIVIAL(info) << "Discovering vertical shells..." << log_memory_info();

    struct DiscoverVerticalShellsCacheEntry
    {
        // Collected polygons, offsetted
        Polygons    top_surfaces;
        Polygons    bottom_surfaces;
        Polygons    holes;
    };
    bool     spiral_mode      = this->print()->config().spiral_mode.value;
    size_t   num_layers       = spiral_mode ? std::min(size_t(this->printing_region(0).config().bottom_shell_layers), m_layers.size()) : m_layers.size();

    std::vector<DiscoverVerticalShellsCacheEntry> cache_top_botom_regions(num_layers, DiscoverVerticalShellsCacheEntry());
    bool top_bottom_surfaces_all_regions = this->num_printing_regions() > 1 && ! m_config.interface_shells.value;

    static constexpr float top_bottom_expansion_coeff = 0.05f;
    if (top_bottom_surfaces_all_regions) {
        // This is a multi-material print and interface_shells are disabled, meaning that the vertical shell thickness
        // is calculated over all materials.
        bool has_extra_layers = false;
        for (size_t region_id = 0; region_id < this->num_printing_regions(); ++region_id) {
            const PrintRegionConfig &config = this->printing_region(region_id).config();
            if (config.ensure_vertical_shell_thickness.value!=EnsureVerticalThicknessLevel::evtDisabled) {
                has_extra_layers = true;
                break;
            }
        }
        if (! has_extra_layers)
            // The "ensure vertical wall thickness" feature is not applicable to any of the regions. Quit.
            return;

        BOOST_LOG_TRIVIAL(debug) << "Discovering vertical shells in parallel - start : cache top / bottom";
        //FIXME Improve the heuristics for a grain size.
        size_t grain_size = std::max(num_layers / 16, size_t(1));
        // 关闭interface_shell，不区分不同材料，所以遍历顺序是按层遍历，层中遍历region
        // top区域包含墙，bottom区域包含墙，holes为稀疏填充区域
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, num_layers, grain_size),
            [this, &cache_top_botom_regions](const tbb::blocked_range<size_t>& range) {
                const std::initializer_list<SurfaceType> surfaces_bottom{ stBottom, stBottomBridge };
                const size_t num_regions = this->num_printing_regions();
                for (size_t idx_layer = range.begin(); idx_layer < range.end(); ++idx_layer) {
                    m_print->throw_if_canceled();
                    const Layer& layer = *m_layers[idx_layer];
                    DiscoverVerticalShellsCacheEntry& cache = cache_top_botom_regions[idx_layer];
                    // Simulate single set of perimeters over all merged regions.
                    float                             perimeter_offset = 0.f;
                    float                             perimeter_min_spacing = FLT_MAX;
                    for (size_t region_id = 0; region_id < num_regions; ++region_id) {
                        LayerRegion& layerm = *layer.m_regions[region_id];
                        float        top_bottom_expansion = float(layerm.flow(frSolidInfill).scaled_spacing()) * top_bottom_expansion_coeff;
                        // Top surfaces.
                        append(cache.top_surfaces, offset(layerm.slices.filter_by_type(stTop), top_bottom_expansion));
                        append(cache.bottom_surfaces, offset(layerm.slices.filter_by_types(surfaces_bottom), top_bottom_expansion));
                        unsigned int perimeters = 0;
                        for (const Surface& s : layerm.slices.surfaces)
                            perimeters = std::max<unsigned int>(perimeters, s.extra_perimeters);
                        perimeters += layerm.region().config().wall_loops.value;
                        // Then calculate the infill offset.
                        if (perimeters > 0) {
                            Flow extflow = layerm.flow(frExternalPerimeter);
                            Flow flow = layerm.flow(frPerimeter);
                            perimeter_offset = std::max(perimeter_offset,
                                0.5f * float(extflow.scaled_width() + extflow.scaled_spacing()) + (float(perimeters) - 1.f) * flow.scaled_spacing());
                            perimeter_min_spacing = std::min(perimeter_min_spacing, float(std::min(extflow.scaled_spacing(), flow.scaled_spacing())));
                        }
                        polygons_append(cache.holes, to_polygons(layerm.fill_expolygons));
                    }
                    // For a multi-material print, simulate perimeter / infill split as if only a single extruder has been used for the whole print.
                    if (perimeter_offset > 0.) {
                        // The layer.lslices are forced to merge by expanding them first.
                        // 对于多材料，按照最大墙宽度，再算一次稀疏区域
                        polygons_append(cache.holes, offset2(layer.lslices, 0.3f * perimeter_min_spacing, -perimeter_offset - 0.3f * perimeter_min_spacing));
                    }
                    // Save some computing time by reducing the number of polygons.
                    cache.top_surfaces = union_(cache.top_surfaces);
                    cache.bottom_surfaces = union_(cache.bottom_surfaces);
                    cache.holes = union_(cache.holes);
                }});
        m_print->throw_if_canceled();
        BOOST_LOG_TRIVIAL(debug) << "Discovering vertical shells in parallel - end : cache top / bottom";
    }

    // 逐region遍历
    for (size_t region_id = 0; region_id < this->num_printing_regions(); ++ region_id) {
        //FIXME Improve the heuristics for a grain size.
        const PrintRegion &region = this->printing_region(region_id);
        if (region.config().ensure_vertical_shell_thickness.value==EnsureVerticalThicknessLevel::evtDisabled)
            // This region will be handled by discover_horizontal_shells().
            continue;

        size_t grain_size = std::max(num_layers / 16, size_t(1));

        // 开启了interface_shell，代表顶底面计算时只有同region可以视为covered
        // 所以此时，对于某一个region，先逐层计算cache的top，bottom，由于稀疏填充是共用的，所以算一次即可
        if (! top_bottom_surfaces_all_regions) {
            // This is either a single material print, or a multi-material print and interface_shells are enabled, meaning that the vertical shell thickness
            // is calculated over a single material.
            BOOST_LOG_TRIVIAL(debug) << "Discovering vertical shells for region " << region_id << " in parallel - start : cache top / bottom";
            tbb::parallel_for(
                tbb::blocked_range<size_t>(0, num_layers, grain_size),
                [this, region_id, &cache_top_botom_regions](const tbb::blocked_range<size_t>& range) {
                    const std::initializer_list<SurfaceType> surfaces_bottom { stBottom, stBottomBridge };
                    for (size_t idx_layer = range.begin(); idx_layer < range.end(); ++ idx_layer) {
                        m_print->throw_if_canceled();
                        Layer       &layer                = *m_layers[idx_layer];
                        LayerRegion &layerm               = *layer.m_regions[region_id];
                        float        top_bottom_expansion = float(layerm.flow(frSolidInfill).scaled_spacing()) * top_bottom_expansion_coeff;
                        // Top surfaces.
                        auto &cache = cache_top_botom_regions[idx_layer];
                        cache.top_surfaces = offset(layerm.slices.filter_by_type(stTop), top_bottom_expansion);
//                        append(cache.top_surfaces, offset(layerm.fill_surfaces().filter_by_type(stTop), top_bottom_expansion));
                        // Bottom surfaces.
                        cache.bottom_surfaces = offset(layerm.slices.filter_by_types(surfaces_bottom), top_bottom_expansion);
//                        append(cache.bottom_surfaces, offset(layerm.fill_surfaces().filter_by_types(surfaces_bottom), top_bottom_expansion));
                        // Holes over all regions. Only collect them once, they are valid for all region_id iterations.
                        if (cache.holes.empty()) {
                            for (size_t region_id = 0; region_id < layer.regions().size(); ++ region_id)
                                polygons_append(cache.holes, to_polygons(layer.regions()[region_id]->fill_expolygons));
                        }
                    }
                });
            m_print->throw_if_canceled();
            BOOST_LOG_TRIVIAL(debug) << "Discovering vertical shells for region " << region_id << " in parallel - end : cache top / bottom";
        }

        BOOST_LOG_TRIVIAL(debug) << "Discovering vertical shells for region " << region_id << " in parallel - start : ensure vertical wall thickness";
        grain_size = 1;
        // 从第低到高按层遍历
#if USE_TBB_IN_INFILL
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, num_layers, grain_size),
            [this, region_id, &cache_top_botom_regions]
            (const tbb::blocked_range<size_t>& range) {
                // printf("discover_vertical_shells from %d to %d\n", range.begin(), range.end());
                for (size_t idx_layer = range.begin(); idx_layer < range.end(); ++ idx_layer) {
#else
        for (size_t idx_layer = 0; idx_layer < num_layers; ++idx_layer) {
#endif
                    m_print->throw_if_canceled();
#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
        			static size_t debug_idx = 0;
        			++ debug_idx;
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

                    Layer       	        *layer          = m_layers[idx_layer];
                    LayerRegion 	        *layerm         = layer->m_regions[region_id];
                    const PrintRegionConfig &region_config  = layerm->region().config();

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
                    layerm->export_region_slices_to_svg_debug("3_discover_vertical_shells-initial");
                    layerm->export_region_fill_surfaces_to_svg_debug("3_discover_vertical_shells-initial");
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

                    Flow         solid_infill_flow   = layerm->flow(frSolidInfill);
                    coord_t      infill_line_spacing = solid_infill_flow.scaled_spacing();
                    // Find a union of perimeters below / above this surface to guarantee a minimum shell thickness.
                    Polygons shell;
                    Polygons holes;
#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
                    ExPolygons shell_ex;
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
                    float min_perimeter_infill_spacing = float(infill_line_spacing) * 1.05f;
			        polygons_append(holes, cache_top_botom_regions[idx_layer].holes);
                    auto combine_holes = [&holes](const Polygons &holes2) {
                        if (holes.empty() || holes2.empty())
                            holes.clear();
                        else
                            holes = intersection(holes, holes2);
                    };
                    auto combine_shells = [&shell](const Polygons &shells2) {
                        if (shell.empty())
                            shell = std::move(shells2);
                        else if (! shells2.empty()) {
                            polygons_append(shell, shells2);
                            // Running the union_ using the Clipper library piece by piece is cheaper
                            // than running the union_ all at once.
                            shell = union_(shell);
                        }
                    };
                    static constexpr const bool one_more_layer_below_top_bottom_surfaces = false;
			        if (int n_top_layers = region_config.top_shell_layers.value; n_top_layers > 0) {
                        // Gather top regions projected to this layer.
                        coordf_t print_z = layer->print_z;
                        int i = int(idx_layer) + 1;
                        int itop = int(idx_layer) + n_top_layers;
                        bool at_least_one_top_projected = false;
	                    for (; i < int(cache_top_botom_regions.size()) &&
	                         (i < itop || m_layers[i]->print_z - print_z < region_config.top_shell_thickness - EPSILON);
	                        ++ i) {
                            at_least_one_top_projected = true;
	                        const DiscoverVerticalShellsCacheEntry &cache = cache_top_botom_regions[i];
                            if (region_config.ensure_vertical_shell_thickness.value != EnsureVerticalThicknessLevel::evtPartial) {
                                combine_holes(cache.holes);
                            }
                            combine_shells(cache.top_surfaces);
	                    }
                        if (!at_least_one_top_projected && i < int(cache_top_botom_regions.size())) {
                            // Lets consider this a special case - with only 1 top solid and minimal shell thickness settings, the
                            // boundaries of solid layers are not anchored over/under perimeters, so lets fix it by adding at least one
                            // perimeter width of area
                            Polygons anchor_area = intersection(expand(cache_top_botom_regions[idx_layer].top_surfaces,
                                                                       layerm->flow(frExternalPerimeter).scaled_spacing()),
                                                                to_polygons(m_layers[i]->lslices));
                            combine_shells(anchor_area);
                        }

                        if (one_more_layer_below_top_bottom_surfaces)
                            if (i < int(cache_top_botom_regions.size()) &&
                                (i <= itop || m_layers[i]->bottom_z() - print_z < region_config.top_shell_thickness - EPSILON))
                                combine_holes(cache_top_botom_regions[i].holes);
	                }
	                if (int n_bottom_layers = region_config.bottom_shell_layers.value; n_bottom_layers > 0) {
                        // Gather bottom regions projected to this layer.
                        coordf_t bottom_z = layer->bottom_z();
                        int i = int(idx_layer) - 1;
                        int ibottom = int(idx_layer) - n_bottom_layers;
                        bool at_least_one_bottom_projected = false;
	                    for (; i >= 0 &&
	                         (i > ibottom || bottom_z - m_layers[i]->bottom_z() < region_config.bottom_shell_thickness - EPSILON);
	                        -- i) {
                                at_least_one_bottom_projected = true;
	                        const DiscoverVerticalShellsCacheEntry &cache = cache_top_botom_regions[i];
                            if (region_config.ensure_vertical_shell_thickness.value != EnsureVerticalThicknessLevel::evtPartial) {
                                combine_holes(cache.holes);
                            }
                            combine_shells(cache.bottom_surfaces);
	                    }

                        if (!at_least_one_bottom_projected && i >= 0) {
                            Polygons anchor_area = intersection(expand(cache_top_botom_regions[idx_layer].bottom_surfaces,
                                                                       layerm->flow(frExternalPerimeter).scaled_spacing()),
                                                                to_polygons(m_layers[i]->lslices));
                            combine_shells(anchor_area);
                        }

                        if (one_more_layer_below_top_bottom_surfaces)
                            if (i >= 0 &&
                                (i > ibottom || bottom_z - m_layers[i]->print_z < region_config.bottom_shell_thickness - EPSILON))
                                combine_holes(cache_top_botom_regions[i].holes);
	                }
#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
                    shell_ex = union_safety_offset_ex(shell);
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

                    //if (shell.empty())
                    //    continue;

                    // Trim the shells region by the internal & internal void surfaces.
                    const Polygons    polygonsInternal = to_polygons(layerm->fill_surfaces.filter_by_types({ stInternal, stInternalVoid, stInternalSolid }));
                    shell = intersection(shell, polygonsInternal, ApplySafetyOffset::Yes);
                    polygons_append(shell, diff(polygonsInternal, holes));
                    if (shell.empty())
                        continue;

                    // Append the internal solids, so they will be merged with the new ones.
                    polygons_append(shell, to_polygons(layerm->fill_surfaces.filter_by_type(stInternalSolid)));

                    // These regions will be filled by a rectilinear full infill. Currently this type of infill
                    // only fills regions, which fit at least a single line. To avoid gaps in the sparse infill,
                    // make sure that this region does not contain parts narrower than the infill spacing width.
#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
                    Polygons shell_before = shell;
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
                    ExPolygons regularized_shell;
                    {
                        // Open to remove (filter out) regions narrower than a bit less than an infill extrusion line width.
                        // Such narrow regions are difficult to fill in with a gap fill algorithm (or Arachne), however they are most likely
                        // not needed for print stability / quality.
                        const float narrow_ensure_vertical_wall_thickness_region_radius = 0.5f * 0.65f * min_perimeter_infill_spacing;
                        // Then close gaps narrower than 1.2 * line width, such gaps are difficult to fill in with sparse infill,
                        // thus they will be merged into the solid infill.
                        const float narrow_sparse_infill_region_radius                  = 0.5f * 1.2f * min_perimeter_infill_spacing;
                        // Finally expand the infill a bit to remove tiny gaps between solid infill and the other regions.
                        const float tiny_overlap_radius                                 = 0.2f        * min_perimeter_infill_spacing;
                        regularized_shell = shrink_ex(offset2_ex(union_ex(shell),
                            // Open to remove (filter out) regions narrower than an infill extrusion line width.
                            -narrow_ensure_vertical_wall_thickness_region_radius,
                            // Then close gaps narrower than 1.2 * line width, such gaps are difficult to fill in with sparse infill.
                            narrow_ensure_vertical_wall_thickness_region_radius + narrow_sparse_infill_region_radius, ClipperLib::jtSquare),
                            // Finally expand the infill a bit to remove tiny gaps between solid infill and the other regions.
                            narrow_sparse_infill_region_radius - tiny_overlap_radius, ClipperLib::jtSquare);

                        Polygons object_volume;
                        Polygons internal_volume;
                        {
                            Polygons shrinked_bottom_slice = idx_layer > 0 ? to_polygons(m_layers[idx_layer - 1]->lslices) : Polygons{};
                            Polygons shrinked_upper_slice  = (idx_layer + 1) < m_layers.size() ?
                                                                 to_polygons(m_layers[idx_layer + 1]->lslices) :
                                                                 Polygons{};
                            object_volume = intersection(shrinked_bottom_slice, shrinked_upper_slice);
                            internal_volume = closing(polygonsInternal, float(SCALED_EPSILON));
                        }

                        // The regularization operation may cause scattered tiny drops on the smooth parts of the model, filter them out
                        // If the region checks both following conditions, it is removed:
                        //   1. the area is very small,
                        //      OR the area is quite small and it is fully wrapped in model (not visible)
                        //      the in-model condition is there due to small sloping surfaces, e.g. top of the hull of the benchy
                        //   2. the area does not fully cover an internal polygon
                        //         This is there mainly for a very thin parts, where the solid layers would be missing if the part area is quite small
                        regularized_shell.erase(std::remove_if(regularized_shell.begin(), regularized_shell.end(),
                                                               [&internal_volume, &min_perimeter_infill_spacing,
                                                                &object_volume](const ExPolygon &p) {
                                                                   return (p.area() < min_perimeter_infill_spacing * scaled(1.5) ||
                                                                           (p.area() < min_perimeter_infill_spacing * scaled(8.0) &&
                                                                            diff(to_polygons(p), object_volume).empty())) &&
                                                                          diff(internal_volume,
                                                                               expand(to_polygons(p), min_perimeter_infill_spacing))
                                                                                  .size() >= internal_volume.size();
                                                               }),
                                                regularized_shell.end());
                    }

                    if (regularized_shell.empty())
                        continue;

                    ExPolygons new_internal_solid = intersection_ex(polygonsInternal, regularized_shell);

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
                    {
                        Slic3r::SVG svg(debug_out_path("discover_vertical_shells-regularized-%d.svg", debug_idx), get_extents(shell_before));
                        // Source shell.
                        svg.draw(union_safety_offset_ex(shell_before));
                        // Shell trimmed to the internal surfaces.
                        svg.draw_outline(union_safety_offset_ex(shell), "black", "blue", scale_(0.05));
                        // Regularized infill region.
                        svg.draw_outline(new_internal_solid, "red", "magenta", scale_(0.05));
                        svg.Close();
                    }
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
                    // Trim the internal & internalvoid by the shell.
                    Slic3r::ExPolygons new_internal = diff_ex(layerm->fill_surfaces.filter_by_type(stInternal), regularized_shell);
                    Slic3r::ExPolygons new_internal_void = diff_ex(layerm->fill_surfaces.filter_by_type(stInternalVoid), regularized_shell);

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
                    {
                        SVG::export_expolygons(debug_out_path("discover_vertical_shells-new_internal-%d.svg", debug_idx), get_extents(shell), new_internal, "black", "blue", scale_(0.05));
        				SVG::export_expolygons(debug_out_path("discover_vertical_shells-new_internal_void-%d.svg", debug_idx), get_extents(shell), new_internal_void, "black", "blue", scale_(0.05));
        				SVG::export_expolygons(debug_out_path("discover_vertical_shells-new_internal_solid-%d.svg", debug_idx), get_extents(shell), new_internal_solid, "black", "blue", scale_(0.05));
                    }
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

                    // Assign resulting internal surfaces to layer.
                    layerm->fill_surfaces.keep_types({ stTop, stBottom, stBottomBridge });
                    layerm->fill_surfaces.append(new_internal,       stInternal);
                    layerm->fill_surfaces.append(new_internal_void,  stInternalVoid);
                    layerm->fill_surfaces.append(new_internal_solid, stInternalSolid);
                } // for each layer
#if USE_TBB_IN_INFILL
            });
#endif
        m_print->throw_if_canceled();
        BOOST_LOG_TRIVIAL(debug) << "Discovering vertical shells for region " << region_id << " in parallel - end";

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
		for (size_t idx_layer = 0; idx_layer < m_layers.size(); ++idx_layer) {
			LayerRegion *layerm = m_layers[idx_layer]->get_region(region_id);
			layerm->export_region_slices_to_svg_debug("4_discover_vertical_shells-final");
			layerm->export_region_fill_surfaces_to_svg_debug("4_discover_vertical_shells-final");
		}
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
    } // for each region

    // Write the profiler measurements to file
//    PROFILE_UPDATE();
//    PROFILE_OUTPUT(debug_out_path("discover_vertical_shells-profile.txt").c_str());
}



void PrintObject::discover_shell_for_perimeters()
{
    const size_t num_regions = this->num_printing_regions();

    tbb::parallel_for(tbb::blocked_range<size_t>(0, m_layers.size()),
        [this,num_regions](const tbb::blocked_range<size_t> &range){
            for (size_t idx_layer = range.begin(); idx_layer < range.end(); ++idx_layer) {
                Layer* layer = m_layers[idx_layer];
                if (!layer->lower_layer)
                    continue;
                Layer* lower_layer = layer->lower_layer;

                ExPolygons perimeter_areas;
                ExPolygons infill_areas;
                float max_line_width = 0;
                for (size_t region_id = 0; region_id < num_regions; ++region_id) {
                    LayerRegion* layerm = layer->m_regions[region_id];
                    Flow extflow = layerm->flow(frExternalPerimeter);
                    infill_areas.insert(infill_areas.end(), layerm->fill_expolygons.begin(), layerm->fill_expolygons.end());
                    max_line_width = std::max(max_line_width, 0.5f * float(extflow.scaled_width() + extflow.scaled_spacing()));
                }
                infill_areas = union_ex(infill_areas);
                perimeter_areas = offset_ex(diff_ex(layer->lslices, infill_areas), max_line_width);

                for (size_t region_id = 0; region_id < num_regions; ++region_id) {
                    LayerRegion* lower_layerm = lower_layer->m_regions[region_id];

                    ExPolygons new_perimeter_solid = intersection_ex(perimeter_areas, lower_layerm->fill_expolygons);
                    new_perimeter_solid.erase(std::remove_if(new_perimeter_solid.begin(), new_perimeter_solid.end(), [max_line_width](auto& expoly) {
                        return is_narrow_expolygon(expoly, 3 * max_line_width);
                        }), new_perimeter_solid.end());
                    if (new_perimeter_solid.empty())
                        continue;

                    ExPolygons old_internal = to_expolygons(lower_layerm->fill_surfaces.filter_by_type(stInternal));
                    ExPolygons old_internal_void = to_expolygons(lower_layerm->fill_surfaces.filter_by_type(stInternalVoid));
                    ExPolygons old_internal_solid = to_expolygons(lower_layerm->fill_surfaces.filter_by_type(stInternalSolid));
                    ExPolygons bottom_area        = to_expolygons(lower_layerm->fill_surfaces.filter_by_type(stBottom));

                    lower_layerm->fill_surfaces.remove_types({ stInternal,stInternalVoid,stInternalSolid });

                    ExPolygons new_internal_solid = diff_ex(union_ex(old_internal_solid, new_perimeter_solid), bottom_area);
                    ExPolygons new_internal = diff_ex(old_internal, new_perimeter_solid);
                    ExPolygons new_internal_void = diff_ex(old_internal_void, new_perimeter_solid);
                    lower_layerm->fill_surfaces.append(new_internal, stInternal);
                    lower_layerm->fill_surfaces.append(new_internal_void, stInternalVoid);
                    lower_layerm->fill_surfaces.append(new_internal_solid, stInternalSolid);
                }
            }
    });
}
// This method applies bridge flow to the first internal solid layer above sparse infill.
// This method applies bridge flow to the first internal solid layer above sparse infill.
#ifndef SLIC3R_PERIMETERS_ONLY_BUILD
void PrintObject::bridge_over_infill()
{
    BOOST_LOG_TRIVIAL(info) << "Bridge over infill - Start" << log_memory_info();

    // CandidateSurface存放一个需要桥接的区域
    struct CandidateSurface
    {
        CandidateSurface(const Surface     *original_surface,
            int                layer_index,
            Polygons           new_polys,
            const LayerRegion *region,
            double             bridge_angle)
            : original_surface(original_surface)
            , layer_index(layer_index)
            , new_polys(new_polys)
            , region(region)
            , bridge_angle(bridge_angle)
        {}
        const Surface     *original_surface;  // 下方需要生成桥接的surface
        int                layer_index;  // 下方生成桥接的层号
        Polygons           new_polys;    // 下方需要生成桥接的实心区域
        const LayerRegion *region;       // 下方需要生成桥接的region，主要提供参数
        double             bridge_angle; // 桥接方向
    };

    // 按层存放surface，存放着待桥接的信息
    std::map<size_t, std::vector<CandidateSurface>> surfaces_by_layer;

    // SECTION to gather and filter surfaces for expanding, and then cluster them by layer
    {
        tbb::concurrent_vector<CandidateSurface> candidate_surfaces;
#if USE_TBB_IN_INFILL
        tbb::parallel_for(tbb::blocked_range<size_t>(0, this->layers().size()), [po = static_cast<const PrintObject *>(this),
            &candidate_surfaces](tbb::blocked_range<size_t> r) {
                // 按层并行
                for (size_t lidx = r.begin(); lidx < r.end(); lidx++) {
#else
        auto po = static_cast<const PrintObject*>(this);
        for(size_t lidx =0;lidx<this->layers().size();++lidx){
#endif
                    const Layer *layer = po->get_layer(lidx);
                    if (layer->lower_layer == nullptr) {
                        continue;
                    }
                    double spacing = layer->regions().front()->flow(frSolidInfill).scaled_spacing();
                    // unsupported area will serve as a filter for polygons worth bridging.
                    Polygons   unsupported_area; // 下一层不提供支撑的区域
                    Polygons   lower_layer_solids;  // 下一层的实心区域，可以提供支撑
                    // 取当前层和下一层的数据
                    for (const LayerRegion *region : layer->lower_layer->regions()) {
                        Polygons fill_polys = to_polygons(region->fill_expolygons);
                        // initially consider the whole layer unsupported, but also gather solid layers to later cut off supported parts
                        unsupported_area.insert(unsupported_area.end(), fill_polys.begin(), fill_polys.end());
                        for (const Surface &surface : region->fill_surfaces.surfaces) {
                            if (surface.surface_type != stInternal || region->region().config().sparse_infill_density.value == 100) {
                                Polygons p = to_polygons(surface.expolygon);
                                lower_layer_solids.insert(lower_layer_solids.end(), p.begin(), p.end());
                            }
                        }
                    }
                    unsupported_area = closing(unsupported_area, float(SCALED_EPSILON));
                    // By expanding the lower layer solids, we avoid making bridges from the tiny internal overhangs that are (very likely) supported by previous layer solids
                    // NOTE that we cannot filter out polygons worth bridging by their area, because sometimes there is a very small internal island that will grow into large hole
                    lower_layer_solids = shrink(lower_layer_solids, 1 * spacing); // first remove thin regions that will not support anything
                    lower_layer_solids = expand(lower_layer_solids, (1 + 3) * spacing); // then expand back (opening), and further for parts supported by internal solids
                    // By shrinking the unsupported area, we avoid making bridges from narrow ensuring region along perimeters.
                    unsupported_area   = shrink(unsupported_area, 3 * spacing);
                    unsupported_area   = diff(unsupported_area, lower_layer_solids);

                    for (LayerRegion *region : layer->regions()) {
                        auto region_internal_solids =  region->fill_surfaces.filter_by_type(stInternalSolid); // 取当前层的实心区域
                        for (const Surface *s : region_internal_solids) {
                            Polygons unsupported         = intersection(to_polygons(s->expolygon), unsupported_area); // 当前层需要生成桥接的区域，通过当前层的实心区域与下一层的非实心区域求交得到
                            // The following flag marks those surfaces, which overlap with unuspported area, but at least part of them is supported.
                            // These regions can be filtered by area, because they for sure are touching solids on lower layers, and it does not make sense to bridge their tiny overhangs
                            bool     partially_supported = area(unsupported) < area(to_polygons(s->expolygon)) - EPSILON;
                            if (!unsupported.empty() && (!partially_supported || area(unsupported) > 3 * 3 * spacing * spacing)) {
                                Polygons worth_bridging = intersection(to_polygons(s->expolygon), expand(unsupported, 4 * spacing));
                                // after we extracted the part worth briding, we go over the leftovers and merge the tiny ones back, to not brake the surface too much
                                for (const Polygon& p : diff(to_polygons(s->expolygon), expand(worth_bridging, spacing))) {
                                    double area = p.area();
                                    if (area < spacing * scale_(12.0) && area > spacing * spacing) {
                                        worth_bridging.push_back(p);
                                    }
                                }
                                worth_bridging = intersection(closing(worth_bridging, float(SCALED_EPSILON)), s->expolygon);
                                // 对应哪个region下的那个surface需要生成桥接
                                candidate_surfaces.push_back(CandidateSurface(s, lidx, worth_bridging, region, 0));

#ifdef DEBUG_BRIDGE_OVER_INFILL
                                debug_draw(std::to_string(lidx) + "_candidate_surface_" + std::to_string(area(s->expolygon)),
                                    to_lines(region->layer()->lslices), to_lines(s->expolygon), to_lines(worth_bridging),
                                    to_lines(unsupported_area));
#endif
#ifdef DEBUG_BRIDGE_OVER_INFILL
                                debug_draw(std::to_string(lidx) + "_candidate_processing_" + std::to_string(area(unsupported)),
                                    to_lines(unsupported), to_lines(intersection(to_polygons(s->expolygon), expand(unsupported, 5 * spacing))),
                                    to_lines(diff(to_polygons(s->expolygon), expand(worth_bridging, spacing))),
                                    to_lines(unsupported_area));
#endif
                            }
                        }
                    }
                }
#if USE_TBB_IN_INFILL
                });
#endif

        // 按层重新存储
        for (const CandidateSurface &c : candidate_surfaces) {
            surfaces_by_layer[c.layer_index].push_back(c);
        }
    }

    // LIGHTNING INFILL SECTION - If lightning infill is used somewhere, we check the areas that are going to be bridges, and those that rely on the
    // lightning infill under them get expanded. This somewhat helps to ensure that most of the extrusions are anchored to the lightning infill at the ends.
    // It requires modifying this instance of print object in a specific way, so that we do not invalidate the pointers in our surfaces_by_layer structure.
    bool has_lightning_infill = false;
    for (size_t i = 0; i < this->num_printing_regions(); i++) {
        if (this->printing_region(i).config().sparse_infill_pattern == ipLightning) {
            has_lightning_infill = true;
            break;
        }
    }
    if (has_lightning_infill) {
        // Prepare backup data for the Layer Region infills. Before modfiyng the layer region, we backup its fill surfaces by moving! them into this map.
        // then a copy is created, modifiyed and passed to lightning infill generator. After generator is created, we restore the original state of the fills
        // again by moving the data from this map back to the layer regions. This ensures that pointers to surfaces stay valid.
        std::map<size_t, std::map<const LayerRegion *, SurfaceCollection>> backup_surfaces;
        for (size_t lidx = 0; lidx < this->layer_count(); lidx++) {
            backup_surfaces[lidx] = {};
        }

        tbb::parallel_for(tbb::blocked_range<size_t>(0, this->layers().size()), [po = this, &backup_surfaces,
            &surfaces_by_layer](tbb::blocked_range<size_t> r) {
                for (size_t lidx = r.begin(); lidx < r.end(); lidx++) {
                    if (surfaces_by_layer.find(lidx) == surfaces_by_layer.end())
                        continue;

                    Layer       *layer       = po->get_layer(lidx);
                    const Layer *lower_layer = layer->lower_layer;
                    if (lower_layer == nullptr)
                        continue;

                    Polygons lightning_fill;
                    for (LayerRegion *region : lower_layer->regions()) {
                        if (region->region().config().sparse_infill_pattern == ipLightning) {
                            Polygons lf = to_polygons(region->fill_surfaces.filter_by_type(stInternal));
                            lightning_fill.insert(lightning_fill.end(), lf.begin(), lf.end());
                        }
                    }

                    if (lightning_fill.empty())
                        continue;

                    for (LayerRegion *region : layer->regions()) {
                        backup_surfaces[lidx][region] = std::move(
                            region->fill_surfaces); // Make backup copy by move!! so that pointers in candidate surfaces stay valid
                        // Copy the surfaces back, this will make copy, but we will later discard it anyway
                        region->fill_surfaces = backup_surfaces[lidx][region];
                    }

                    for (LayerRegion *region : layer->regions()) {
                        ExPolygons sparse_infill = to_expolygons(region->fill_surfaces.filter_by_type(stInternal));
                        ExPolygons solid_infill  = to_expolygons(region->fill_surfaces.filter_by_type(stInternalSolid));

                        if (sparse_infill.empty()) {
                            break;
                        }
                        for (const auto &surface : surfaces_by_layer[lidx]) {
                            if (surface.region != region)
                                continue;
                            ExPolygons expansion = intersection_ex(sparse_infill, expand(surface.new_polys, scaled<float>(3.0)));
                            solid_infill.insert(solid_infill.end(), expansion.begin(), expansion.end());
                        }

                        solid_infill  = union_safety_offset_ex(solid_infill);
                        sparse_infill = diff_ex(sparse_infill, solid_infill);

                        region->fill_surfaces.remove_types({stInternalSolid, stInternal});
                        for (const ExPolygon &ep : solid_infill) {
                            region->fill_surfaces.surfaces.emplace_back(stInternalSolid, ep);
                        }
                        for (const ExPolygon &ep : sparse_infill) {
                            region->fill_surfaces.surfaces.emplace_back(stInternal, ep);
                        }
                    }
                }
            });

        // Use the modified surfaces to generate expanded lightning anchors
        this->m_lightning_generator = this->prepare_lightning_infill_data();

        // And now restore carefully the original surfaces, again using move to avoid reallocation and preserving the validity of the
        // pointers in surface candidates
        for (size_t lidx = 0; lidx < this->layer_count(); lidx++) {
            Layer *layer = this->get_layer(lidx);
            for (LayerRegion *region : layer->regions()) {
                if (backup_surfaces[lidx].find(region) != backup_surfaces[lidx].end()) {
                    region->fill_surfaces = std::move(backup_surfaces[lidx][region]);
                }
            }
        }
    }

    std::map<size_t, Polylines> infill_lines;
    // SECTION to generate infill polylines
    {
        std::vector<std::pair<const Surface *, float>> surfaces_w_bottom_z;
        for (const auto &pair : surfaces_by_layer) {
            for (const CandidateSurface &c : pair.second) {
                surfaces_w_bottom_z.emplace_back(c.original_surface, c.region->m_layer->bottom_z());
            }
        }

        this->m_adaptive_fill_octrees = this->prepare_adaptive_infill_data(surfaces_w_bottom_z);

        std::vector<size_t> layers_to_generate_infill;
        for (const auto &pair : surfaces_by_layer) {
            assert(pair.first > 0);
            infill_lines[pair.first - 1] = {};
            layers_to_generate_infill.push_back(pair.first - 1);
        }

        tbb::parallel_for(tbb::blocked_range<size_t>(0, layers_to_generate_infill.size()), [po = static_cast<const PrintObject *>(this),
            &layers_to_generate_infill,
            &infill_lines](tbb::blocked_range<size_t> r) {
                for (size_t job_idx = r.begin(); job_idx < r.end(); job_idx++) {
                    size_t lidx = layers_to_generate_infill[job_idx];
                    infill_lines.at(
                        lidx) = po->get_layer(lidx)->generate_sparse_infill_polylines_for_anchoring(po->m_adaptive_fill_octrees.first.get(),
                            po->m_adaptive_fill_octrees.second.get(),
                            po->m_lightning_generator.get());
                }
            });
#ifdef DEBUG_BRIDGE_OVER_INFILL
        for (const auto &il : infill_lines) {
            debug_draw(std::to_string(il.first) + "_infill_lines", to_lines(get_layer(il.first)->lslices), to_lines(il.second), {}, {});
        }
#endif
    }

    // cluster layers by depth needed for thick bridges. Each cluster is to be processed by single thread sequentially, so that bridges cannot appear one on another
    std::vector<std::vector<size_t>> clustered_layers_for_threads;
    float target_flow_height_factor = 0.9f;
    {
        std::vector<size_t> layers_with_candidates; // 存储所有需要生成桥接的层号
        std::map<size_t, Polygons> layer_area_covered_by_candidates; // 存储每一层，需要生成桥接区域的bbox的并集
        for (const auto& pair : surfaces_by_layer) {
            layers_with_candidates.push_back(pair.first);
            layer_area_covered_by_candidates[pair.first] = {};
        }

        // prepare inflated filter for each candidate on each layer. layers will be put into single thread cluster if they are close to each other (z-axis-wise)
        // and if the inflated AABB polygons overlap somewhere
        tbb::parallel_for(tbb::blocked_range<size_t>(0, layers_with_candidates.size()), [&layers_with_candidates, &surfaces_by_layer,
            &layer_area_covered_by_candidates](
                tbb::blocked_range<size_t> r) {
                    // 按层并行
                    for (size_t job_idx = r.begin(); job_idx < r.end(); job_idx++) {
                        size_t lidx = layers_with_candidates[job_idx];
                        for (const auto &candidate : surfaces_by_layer.at(lidx)) {
                            Polygon candiate_inflated_aabb = get_extents(candidate.new_polys).inflated(scale_(7)).polygon();
                            layer_area_covered_by_candidates.at(lidx) = union_(layer_area_covered_by_candidates.at(lidx),
                                Polygons{candiate_inflated_aabb});
                        }
                    }
            });

        // note: surfaces_by_layer is ordered map
        for (const auto &pair : surfaces_by_layer) {
            // 初次操作 || z方向距离较远 || 桥接区域无交集， 那么就可以重新划分一个组，否则分配到前一个组
            if (clustered_layers_for_threads.empty() ||
                this->get_layer(clustered_layers_for_threads.back().back())->print_z <
                this->get_layer(pair.first)->print_z -
                this->get_layer(pair.first)->regions()[0]->bridging_flow(frSolidInfill, true).height() * target_flow_height_factor -
                EPSILON ||
                intersection(layer_area_covered_by_candidates[clustered_layers_for_threads.back().back()],
                    layer_area_covered_by_candidates[pair.first])
                .empty()) {
                clustered_layers_for_threads.push_back({pair.first});
            } else {
                clustered_layers_for_threads.back().push_back(pair.first);
            }
        }

#ifdef DEBUG_BRIDGE_OVER_INFILL
        std::cout << "BRIDGE OVER INFILL CLUSTERED LAYERS FOR SINGLE THREAD" << std::endl;
        for (auto cluster : clustered_layers_for_threads) {
            std::cout << "CLUSTER: ";
            for (auto l : cluster) {
                std::cout << l << "  ";
            }
            std::cout << std::endl;
        }
#endif
    }

    // LAMBDA to gather areas with sparse infill deep enough that we can fit thick bridges there.
    auto gather_areas_w_depth = [target_flow_height_factor](const PrintObject *po, int lidx, float target_flow_height) {
        // Gather layers sparse infill areas, to depth defined by used bridge flow
        ExPolygons layers_sparse_infill{};
        ExPolygons not_sparse_infill{};
        double   bottom_z = po->get_layer(lidx)->print_z - target_flow_height * target_flow_height_factor - EPSILON;
        for (int i = int(lidx) - 1; i >= 0; --i) {
            // Stop iterating if layer is lower than bottom_z and at least one iteration was made
            const Layer *layer = po->get_layer(i);
            if (layer->print_z < bottom_z && i < int(lidx) - 1)
                break;

            for (const LayerRegion *region : layer->regions()) {
                bool has_low_density = region->region().config().sparse_infill_density.value < 100;
                for (const Surface &surface : region->fill_surfaces.surfaces) {
                    if ((surface.surface_type == stInternal && has_low_density) || surface.surface_type == stInternalVoid ) {
                        layers_sparse_infill.push_back(surface.expolygon);
                    } else {
                        not_sparse_infill.push_back(surface.expolygon);
                    }
                }
            }
        }
        // 收集一定z范围内的稀疏和实心区域，判断有没有交集，如果有交集，则不能使用thick bridge(thick bridge的流量会侵占实心区域)
        layers_sparse_infill = union_ex(layers_sparse_infill);
        layers_sparse_infill = closing_ex(layers_sparse_infill, float(SCALED_EPSILON));
        not_sparse_infill    = union_ex(not_sparse_infill);
        not_sparse_infill    = closing_ex(not_sparse_infill, float(SCALED_EPSILON));
        return diff(layers_sparse_infill, not_sparse_infill);
        };

    // LAMBDA do determine optimal bridging angle
    auto determine_bridging_angle = [](const Polygons &bridged_area, const Lines &anchors, InfillPattern dominant_pattern, double infill_direction) {
        AABBTreeLines::LinesDistancer<Line> lines_tree(anchors);

        // Check it the infill that require a fixed infill angle.
        switch (dominant_pattern) {
        case ip3DHoneycomb:
        case ipCrossHatch:
            return (infill_direction + 45.0) * 2.0 * M_PI / 360.;
        default: break;
        }

        std::map<double, int> counted_directions;
        for (const Polygon &p : bridged_area) {
            double acc_distance = 0;
            for (int point_idx = 0; point_idx < int(p.points.size()) - 1; ++point_idx) {
                Vec2d  start        = p.points[point_idx].cast<double>();
                Vec2d  next         = p.points[point_idx + 1].cast<double>();
                Vec2d  v            = next - start; // vector from next to current
                double dist_to_next = v.norm();
                acc_distance += dist_to_next;
                if (acc_distance > scaled(2.0)) {
                    acc_distance = 0.0;
                    v.normalize();
                    int   lines_count = int(std::ceil(dist_to_next / scaled(2.0)));
                    float step_size   = dist_to_next / lines_count;
                    for (int i = 0; i < lines_count; ++i) {
                        Point a                   = (start + v * (i * step_size)).cast<coord_t>();
                        auto [distance, index, p] = lines_tree.distance_from_lines_extra<false>(a);
                        double angle = lines_tree.get_line(index).orientation();
                        if (angle > PI) {
                            angle -= PI;
                        }
                        angle += PI * 0.5;
                        counted_directions[angle]++;
                    }
                }
            }
        }

        std::pair<double, int> best_dir{0, 0};
        // sliding window accumulation
        for (const auto &dir : counted_directions) {
            int    score_acc          = 0;
            double dir_acc            = 0;
            double window_start_angle = dir.first - PI * 0.1;
            double window_end_angle   = dir.first + PI * 0.1;
            for (auto dirs_window = counted_directions.lower_bound(window_start_angle);
                dirs_window != counted_directions.upper_bound(window_end_angle); dirs_window++) {
                dir_acc += dirs_window->first * dirs_window->second;
                score_acc += dirs_window->second;
            }
            // current span of directions is 0.5 PI to 1.5 PI (due to the aproach.). Edge values should also account for the
            //  opposite direction.
            if (window_start_angle < 0.5 * PI) {
                for (auto dirs_window = counted_directions.lower_bound(1.5 * PI - (0.5 * PI - window_start_angle));
                    dirs_window != counted_directions.end(); dirs_window++) {
                    dir_acc += dirs_window->first * dirs_window->second;
                    score_acc += dirs_window->second;
                }
            }
            if (window_start_angle > 1.5 * PI) {
                for (auto dirs_window = counted_directions.begin();
                    dirs_window != counted_directions.upper_bound(window_start_angle - 1.5 * PI); dirs_window++) {
                    dir_acc += dirs_window->first * dirs_window->second;
                    score_acc += dirs_window->second;
                }
            }

            if (score_acc > best_dir.second) {
                best_dir = {dir_acc / score_acc, score_acc};
            }
        }
        double bridging_angle = best_dir.first;
        if (bridging_angle == 0) {
            bridging_angle = 0.001;
        }
        switch (dominant_pattern) {
        case ipHilbertCurve: bridging_angle += 0.25 * PI; break;
        case ipOctagramSpiral: bridging_angle += (1.0 / 16.0) * PI; break;
        default: break;
        }

        return bridging_angle;
        };

    // LAMBDA that will fill given polygons with lines, exapand the lines to the nearest anchor, and reconstruct polygons from the newly
    // generated lines
    auto construct_anchored_polygon = [](Polygons bridged_area, Lines anchors, const Flow &bridging_flow, double bridging_angle) {
        auto lines_rotate = [](Lines &lines, double cos_angle, double sin_angle) {
            for (Line &l : lines) {
                double ax = double(l.a.x());
                double ay = double(l.a.y());
                l.a.x()   = coord_t(round(cos_angle * ax - sin_angle * ay));
                l.a.y()   = coord_t(round(cos_angle * ay + sin_angle * ax));
                double bx = double(l.b.x());
                double by = double(l.b.y());
                l.b.x()   = coord_t(round(cos_angle * bx - sin_angle * by));
                l.b.y()   = coord_t(round(cos_angle * by + sin_angle * bx));
            }
            };

        auto segments_overlap = [](coord_t alow, coord_t ahigh, coord_t blow, coord_t bhigh) {
            return (alow >= blow && alow <= bhigh) || (ahigh >= blow && ahigh <= bhigh) || (blow >= alow && blow <= ahigh) ||
                (bhigh >= alow && bhigh <= ahigh);
            };

        Polygons expanded_bridged_area{};
        double   aligning_angle = -bridging_angle + PI * 0.5;
        {
            polygons_rotate(bridged_area, aligning_angle);
            lines_rotate(anchors, cos(aligning_angle), sin(aligning_angle));
            BoundingBox bb_x = get_extents(bridged_area);
            BoundingBox bb_y = get_extents(anchors);

            const size_t n_vlines = (bb_x.max.x() - bb_x.min.x() + bridging_flow.scaled_spacing() - 1) / bridging_flow.scaled_spacing();
            std::vector<Line> vertical_lines(n_vlines);
            for (size_t i = 0; i < n_vlines; i++) {
                coord_t x           = bb_x.min.x() + i * bridging_flow.scaled_spacing();
                coord_t y_min       = bb_y.min.y() - bridging_flow.scaled_spacing();
                coord_t y_max       = bb_y.max.y() + bridging_flow.scaled_spacing();
                vertical_lines[i].a = Point{x, y_min};
                vertical_lines[i].b = Point{x, y_max};
            }

            auto anchors_and_walls_tree = AABBTreeLines::LinesDistancer<Line>{std::move(anchors)};
            auto bridged_area_tree      = AABBTreeLines::LinesDistancer<Line>{to_lines(bridged_area)};

            std::vector<std::vector<Line>> polygon_sections(n_vlines);
            for (size_t i = 0; i < n_vlines; i++) {
                auto area_intersections = bridged_area_tree.intersections_with_line<true>(vertical_lines[i]);
                for (int intersection_idx = 0; intersection_idx < int(area_intersections.size()) - 1; intersection_idx++) {
                    if (bridged_area_tree.outside(
                        (area_intersections[intersection_idx].first + area_intersections[intersection_idx + 1].first) / 2) < 0) {
                        polygon_sections[i].emplace_back(area_intersections[intersection_idx].first,
                            area_intersections[intersection_idx + 1].first);
                    }
                }
                auto anchors_intersections = anchors_and_walls_tree.intersections_with_line<true>(vertical_lines[i]);

                for (Line &section : polygon_sections[i]) {
                    auto maybe_below_anchor = std::upper_bound(anchors_intersections.rbegin(), anchors_intersections.rend(), section.a,
                        [](const Point &a, const std::pair<Point, size_t> &b) {
                            return a.y() > b.first.y();
                        });
                    if (maybe_below_anchor != anchors_intersections.rend()) {
                        section.a = maybe_below_anchor->first;
                        section.a.y() -= bridging_flow.scaled_width() * (0.5 + 0.5);
                    }

                    auto maybe_upper_anchor = std::upper_bound(anchors_intersections.begin(), anchors_intersections.end(), section.b,
                        [](const Point &a, const std::pair<Point, size_t> &b) {
                            return a.y() < b.first.y();
                        });
                    if (maybe_upper_anchor != anchors_intersections.end()) {
                        section.b = maybe_upper_anchor->first;
                        section.b.y() += bridging_flow.scaled_width() * (0.5 + 0.5);
                    }
                }

                for (int section_idx = 0; section_idx < int(polygon_sections[i].size()) - 1; section_idx++) {
                    Line &section_a = polygon_sections[i][section_idx];
                    Line &section_b = polygon_sections[i][section_idx + 1];
                    if (segments_overlap(section_a.a.y(), section_a.b.y(), section_b.a.y(), section_b.b.y())) {
                        section_b.a = section_a.a.y() < section_b.a.y() ? section_a.a : section_b.a;
                        section_b.b = section_a.b.y() < section_b.b.y() ? section_b.b : section_a.b;
                        section_a.a = section_a.b;
                    }
                }

                polygon_sections[i].erase(std::remove_if(polygon_sections[i].begin(), polygon_sections[i].end(),
                    [](const Line &s) { return s.a == s.b; }),
                    polygon_sections[i].end());
                std::sort(polygon_sections[i].begin(), polygon_sections[i].end(),
                    [](const Line &a, const Line &b) { return a.a.y() < b.b.y(); });
            }

            // reconstruct polygon from polygon sections
            struct TracedPoly
            {
                Points lows;
                Points highs;
            };

            std::vector<TracedPoly> current_traced_polys;
            for (const auto &polygon_slice : polygon_sections) {
                std::unordered_set<const Line *> used_segments;
                for (TracedPoly &traced_poly : current_traced_polys) {
                    auto candidates_begin = std::upper_bound(polygon_slice.begin(), polygon_slice.end(), traced_poly.lows.back(),
                        [](const Point &low, const Line &seg) { return seg.b.y() > low.y(); });
                    auto candidates_end   = std::upper_bound(polygon_slice.begin(), polygon_slice.end(), traced_poly.highs.back(),
                        [](const Point &high, const Line &seg) { return seg.a.y() > high.y(); });

                    bool segment_added = false;
                    for (auto candidate = candidates_begin; candidate != candidates_end && !segment_added; candidate++) {
                        if (used_segments.find(&(*candidate)) != used_segments.end()) {
                            continue;
                        }

                        if ((traced_poly.lows.back() - candidate->a).cast<double>().squaredNorm() <
                            36.0 * double(bridging_flow.scaled_spacing()) * bridging_flow.scaled_spacing()) {
                            traced_poly.lows.push_back(candidate->a);
                        } else {
                            traced_poly.lows.push_back(traced_poly.lows.back() + Point{bridging_flow.scaled_spacing() / 2, 0});
                            traced_poly.lows.push_back(candidate->a - Point{bridging_flow.scaled_spacing() / 2, 0});
                            traced_poly.lows.push_back(candidate->a);
                        }

                        if ((traced_poly.highs.back() - candidate->b).cast<double>().squaredNorm() <
                            36.0 * double(bridging_flow.scaled_spacing()) * bridging_flow.scaled_spacing()) {
                            traced_poly.highs.push_back(candidate->b);
                        } else {
                            traced_poly.highs.push_back(traced_poly.highs.back() + Point{bridging_flow.scaled_spacing() / 2, 0});
                            traced_poly.highs.push_back(candidate->b - Point{bridging_flow.scaled_spacing() / 2, 0});
                            traced_poly.highs.push_back(candidate->b);
                        }
                        segment_added = true;
                        used_segments.insert(&(*candidate));
                    }

                    if (!segment_added) {
                        // Zero overlapping segments, we just close this polygon
                        traced_poly.lows.push_back(traced_poly.lows.back() + Point{bridging_flow.scaled_spacing() / 2, 0});
                        traced_poly.highs.push_back(traced_poly.highs.back() + Point{bridging_flow.scaled_spacing() / 2, 0});
                        Polygon &new_poly = expanded_bridged_area.emplace_back(std::move(traced_poly.lows));
                        new_poly.points.insert(new_poly.points.end(), traced_poly.highs.rbegin(), traced_poly.highs.rend());
                        traced_poly.lows.clear();
                        traced_poly.highs.clear();
                    }
                }

                current_traced_polys.erase(std::remove_if(current_traced_polys.begin(), current_traced_polys.end(),
                    [](const TracedPoly &tp) { return tp.lows.empty(); }),
                    current_traced_polys.end());

                for (const auto &segment : polygon_slice) {
                    if (used_segments.find(&segment) == used_segments.end()) {
                        TracedPoly &new_tp = current_traced_polys.emplace_back();
                        new_tp.lows.push_back(segment.a - Point{bridging_flow.scaled_spacing() / 2, 0});
                        new_tp.lows.push_back(segment.a);
                        new_tp.highs.push_back(segment.b - Point{bridging_flow.scaled_spacing() / 2, 0});
                        new_tp.highs.push_back(segment.b);
                    }
                }
            }

            // add not closed polys
            for (TracedPoly &traced_poly : current_traced_polys) {
                Polygon &new_poly = expanded_bridged_area.emplace_back(std::move(traced_poly.lows));
                new_poly.points.insert(new_poly.points.end(), traced_poly.highs.rbegin(), traced_poly.highs.rend());
            }
            expanded_bridged_area = union_safety_offset(expanded_bridged_area);
        }

        polygons_rotate(expanded_bridged_area, -aligning_angle);
        return expanded_bridged_area;
        };

    tbb::parallel_for(tbb::blocked_range<size_t>(0, clustered_layers_for_threads.size()), [po = static_cast<const PrintObject *>(this),
        target_flow_height_factor, &surfaces_by_layer,
        &clustered_layers_for_threads,
        gather_areas_w_depth, &infill_lines,
        determine_bridging_angle,
        construct_anchored_polygon](
            tbb::blocked_range<size_t> r) {
                for (size_t cluster_idx = r.begin(); cluster_idx < r.end(); cluster_idx++) {
                    for (size_t job_idx = 0; job_idx < clustered_layers_for_threads[cluster_idx].size(); job_idx++) {
                        size_t       lidx  = clustered_layers_for_threads[cluster_idx][job_idx];
                        const Layer *layer = po->get_layer(lidx);
                        // this thread has exclusive access to all surfaces in layers enumerated in
                        // clustered_layers_for_threads[cluster_idx]

                        // Presort the candidate polygons. This will help choose the same angle for neighbournig surfaces, that
                        // would otherwise compete over anchoring sparse infill lines, leaving one area unachored
                        std::sort(surfaces_by_layer[lidx].begin(), surfaces_by_layer[lidx].end(),
                            [](const CandidateSurface &left, const CandidateSurface &right) {
                                auto a = get_extents(left.new_polys);
                                auto b = get_extents(right.new_polys);

                                if (a.min.x() == b.min.x()) {
                                    return a.min.y() < b.min.y();
                                };
                                return a.min.x() < b.min.x();
                            });
                        if (surfaces_by_layer[lidx].size() > 2) {
                            Vec2d origin = get_extents(surfaces_by_layer[lidx].front().new_polys).max.cast<double>();
                            std::stable_sort(surfaces_by_layer[lidx].begin() + 1, surfaces_by_layer[lidx].end(),
                                [origin](const CandidateSurface &left, const CandidateSurface &right) {
                                    auto a = get_extents(left.new_polys);
                                    auto b = get_extents(right.new_polys);

                                    return (origin - a.min.cast<double>()).squaredNorm() <
                                        (origin - b.min.cast<double>()).squaredNorm();
                                });
                        }

                        // Gather deep infill areas, where thick bridges fit
                        coordf_t spacing            = surfaces_by_layer[lidx].front().region->bridging_flow(frSolidInfill, true).scaled_spacing();
                        coordf_t target_flow_height = surfaces_by_layer[lidx].front().region->bridging_flow(frSolidInfill, true).height() *
                            target_flow_height_factor;
                        // 收集当前层中可以应用thick_bridge的区域
                        Polygons deep_infill_area = gather_areas_w_depth(po, lidx, target_flow_height);

                        {
                            // Now also remove area that has been already filled on lower layers by bridging expansion - For this
                            // reason we did the clustering of layers per thread.
                            Polygons filled_polyons_on_lower_layers;
                            double   bottom_z = layer->print_z - target_flow_height - EPSILON;
                            if (job_idx > 0) {
                                for (int lower_job_idx = job_idx - 1; lower_job_idx >= 0; lower_job_idx--) {
                                    size_t       lower_layer_idx = clustered_layers_for_threads[cluster_idx][lower_job_idx];
                                    const Layer *lower_layer     = po->get_layer(lower_layer_idx);
                                    if (lower_layer->print_z >= bottom_z) {
                                        for (const auto &c : surfaces_by_layer[lower_layer_idx]) {
                                            filled_polyons_on_lower_layers.insert(filled_polyons_on_lower_layers.end(), c.new_polys.begin(),
                                                c.new_polys.end());
                                        }
                                    } else {
                                        break;
                                    }
                                }
                            }
                            // 再减去别的层已经生成的桥接区域
                            deep_infill_area = diff(deep_infill_area, filled_polyons_on_lower_layers);
                        }
                        // 得到thick_bridge区域，bridge区域扩1.5倍
                        deep_infill_area = expand(deep_infill_area, spacing * 1.5);

                        // Now gather expansion polygons - internal infill on current layer, from which we can cut off anchors
                        Polygons lightning_area;
                        Polygons expansion_area; // 可以提供扩张的区域
                        Polygons total_fill_area; // 所有填充区域
                        Polygons top_area; // 顶面区域

                        for (LayerRegion *region : layer->regions()) {
                            Polygons internal_polys = to_polygons(region->fill_surfaces.filter_by_types({stInternal, stInternalSolid}));
                            expansion_area.insert(expansion_area.end(), internal_polys.begin(), internal_polys.end());
                            Polygons fill_polys = to_polygons(region->fill_expolygons);
                            total_fill_area.insert(total_fill_area.end(), fill_polys.begin(), fill_polys.end());
                            Polygons top_polys = to_polygons(region->fill_surfaces.filter_by_type(stTop));
                            top_area.insert(top_area.end(), top_polys.begin(), top_polys.end());
                            if (region->region().config().sparse_infill_pattern == ipLightning) {
                                Polygons l = to_polygons(region->fill_surfaces.filter_by_type(stInternal));
                                lightning_area.insert(lightning_area.end(), l.begin(), l.end());
                            }
                        }
                        total_fill_area   = closing(total_fill_area, float(SCALED_EPSILON));
                        expansion_area    = closing(expansion_area, float(SCALED_EPSILON));
                        expansion_area    = intersection(expansion_area, deep_infill_area);
                        Polylines anchors = intersection_pl(infill_lines[lidx - 1], shrink(expansion_area, spacing));
                        Polygons internal_unsupported_area = shrink(deep_infill_area, spacing * 4.5);

#ifdef DEBUG_BRIDGE_OVER_INFILL
                        debug_draw(std::to_string(lidx) + "_" + std::to_string(cluster_idx) + "_" + std::to_string(job_idx) + "_" + "_total_area",
                            to_lines(total_fill_area), to_lines(expansion_area), to_lines(deep_infill_area), to_lines(anchors));
#endif

                        std::vector<CandidateSurface> expanded_surfaces;
                        expanded_surfaces.reserve(surfaces_by_layer[lidx].size());
                        for (const CandidateSurface &candidate : surfaces_by_layer[lidx]) {
                            const Flow &flow              = candidate.region->bridging_flow(frSolidInfill, true);
                            Polygons    area_to_be_bridge = expand(candidate.new_polys, flow.scaled_spacing()); // 待生成桥接区域
                            area_to_be_bridge             = intersection(area_to_be_bridge, deep_infill_area);
                            ExPolygons area_to_be_bridge_ex = union_ex(area_to_be_bridge);
                            area_to_be_bridge_ex.erase(std::remove_if(area_to_be_bridge_ex.begin(), area_to_be_bridge_ex.end(),
                                [internal_unsupported_area](const ExPolygon &p) {
                                    return intersection({p}, internal_unsupported_area).empty();
                                }),
                                area_to_be_bridge_ex.end());

                            area_to_be_bridge = to_polygons(area_to_be_bridge_ex);

                            Polygons limiting_area = union_(area_to_be_bridge, expansion_area); // 桥接区域 + 可扩张区域

                            if (area_to_be_bridge.empty())
                                continue;

                            Polylines boundary_plines = to_polylines(expand(total_fill_area, 1.3 * flow.scaled_spacing()));
                            {
                                Polylines limiting_plines = to_polylines(expand(limiting_area, 0.3*flow.spacing()));
                                boundary_plines.insert(boundary_plines.end(), limiting_plines.begin(), limiting_plines.end());
                            }

#ifdef DEBUG_BRIDGE_OVER_INFILL
                            int r = rand();
                            debug_draw(std::to_string(lidx) + "_" + std::to_string(cluster_idx) + "_" + std::to_string(job_idx) + "_" +
                                "_anchors_" + std::to_string(r),
                                to_lines(area_to_be_bridge), to_lines(boundary_plines), to_lines(anchors), to_lines(expansion_area));
#endif

                            double bridging_angle = 0;
                            if (!anchors.empty()) {
                                bridging_angle = determine_bridging_angle(area_to_be_bridge, to_lines(anchors),
                                                                            candidate.region->region().config().sparse_infill_pattern.value,
                                                                            candidate.region->region().config().infill_direction.value);
                            } else {
                                // use expansion boundaries as anchors.
                                // Also, use Infill pattern that is neutral for angle determination, since there are no infill lines.
                                bridging_angle = determine_bridging_angle(area_to_be_bridge, to_lines(boundary_plines), InfillPattern::ipLine, 0);
                            }

                            boundary_plines.insert(boundary_plines.end(), anchors.begin(), anchors.end());
                            if (!lightning_area.empty() && !intersection(area_to_be_bridge, lightning_area).empty()) {
                                boundary_plines = intersection_pl(boundary_plines, expand(area_to_be_bridge, scale_(10)));
                            }
                            Polygons bridging_area = construct_anchored_polygon(area_to_be_bridge, to_lines(boundary_plines), flow, bridging_angle);

                            // Check collision with other expanded surfaces
                            {
                                bool     reconstruct       = false;
                                Polygons tmp_expanded_area = expand(bridging_area, 3.0 * flow.scaled_spacing());
                                for (const CandidateSurface &s : expanded_surfaces) {
                                    if (!intersection(s.new_polys, tmp_expanded_area).empty()) {
                                        bridging_angle = s.bridge_angle;
                                        reconstruct    = true;
                                        break;
                                    }
                                }
                                if (reconstruct) {
                                    bridging_area = construct_anchored_polygon(area_to_be_bridge, to_lines(boundary_plines), flow, bridging_angle);
                                }
                            }

                            bridging_area          = opening(bridging_area, flow.scaled_spacing());
                            bridging_area          = closing(bridging_area, flow.scaled_spacing());
                            bridging_area          = intersection(bridging_area, limiting_area);
                            bridging_area          = intersection(bridging_area, total_fill_area);
                            // BBS: substract top area
                            bridging_area          = diff(bridging_area, top_area);
                            // BBS: open and close again to filter some narrow parts
                            bridging_area          = opening(bridging_area, flow.scaled_spacing());
                            bridging_area          = closing(bridging_area, flow.scaled_spacing());
                            expansion_area         = diff(expansion_area, bridging_area);

#ifdef DEBUG_BRIDGE_OVER_INFILL
                            debug_draw(std::to_string(lidx) + "_" + std::to_string(cluster_idx) + "_" + std::to_string(job_idx) + "_" + "_expanded_bridging" +  std::to_string(r),
                                to_lines(layer->lslices), to_lines(boundary_plines), to_lines(candidate.new_polys), to_lines(bridging_area));
#endif

                            expanded_surfaces.push_back(CandidateSurface(candidate.original_surface, candidate.layer_index, bridging_area,
                                candidate.region, bridging_angle));
                        }
                        surfaces_by_layer[lidx].swap(expanded_surfaces);
                        expanded_surfaces.clear();
                    }
                }
        });

    BOOST_LOG_TRIVIAL(info) << "Bridge over infill - Directions and expanded surfaces computed" << log_memory_info();

    tbb::parallel_for(tbb::blocked_range<size_t>(0, this->layers().size()), [po = this, &surfaces_by_layer](tbb::blocked_range<size_t> r) {
        for (size_t lidx = r.begin(); lidx < r.end(); lidx++) {
            // 如果既不需要生成桥接，也不是桥接的下一层，不处理
            if (surfaces_by_layer.find(lidx) == surfaces_by_layer.end() && surfaces_by_layer.find(lidx + 1) == surfaces_by_layer.end())
                continue;
            Layer *layer = po->get_layer(lidx);

            Polygons cut_from_infill{}; // 桥接区域
            if (surfaces_by_layer.find(lidx) != surfaces_by_layer.end()) {
                for (const auto &surface : surfaces_by_layer.at(lidx)) {
                    cut_from_infill.insert(cut_from_infill.end(), surface.new_polys.begin(), surface.new_polys.end());
                }
            }

            Polygons additional_ensuring_areas{}; // 下一层为上一层桥接需要生成的区域
            if (surfaces_by_layer.find(lidx + 1) != surfaces_by_layer.end()) {
                for (const auto &surface : surfaces_by_layer.at(lidx + 1)) {
                    auto additional_area = diff(surface.new_polys,
                        shrink(surface.new_polys, surface.region->flow(frSolidInfill).scaled_spacing()));
                    additional_ensuring_areas.insert(additional_ensuring_areas.end(), additional_area.begin(), additional_area.end());
                }
            }

            for (LayerRegion *region : layer->regions()) {
                Surfaces new_surfaces;

                Polygons near_perimeters = to_polygons(union_safety_offset_ex(to_polygons(region->fill_surfaces.surfaces))); // 填充区域中，紧靠着外墙的区域
                near_perimeters          = diff(near_perimeters, shrink(near_perimeters, region->flow(frSolidInfill).scaled_spacing()));
                ExPolygons additional_ensuring = intersection_ex(additional_ensuring_areas, near_perimeters); // 紧靠着外墙，能够给上一层的桥接提供支撑的区域

                SurfacesPtr internal_infills = region->fill_surfaces.filter_by_type(stInternal);
                ExPolygons new_internal_infills = diff_ex(internal_infills, cut_from_infill); // 新的稀疏填充区域，去掉生成的桥接区域
                new_internal_infills            = diff_ex(new_internal_infills, additional_ensuring);
                for (const ExPolygon &ep : new_internal_infills) {
                    new_surfaces.emplace_back(stInternal, ep);
                }

                SurfacesPtr internal_solids = region->fill_surfaces.filter_by_type(stInternalSolid);
                if (surfaces_by_layer.find(lidx) != surfaces_by_layer.end()) {
                    for (const CandidateSurface &cs : surfaces_by_layer.at(lidx)) {
                        for (const Surface *surface : internal_solids) {
                            if (cs.original_surface == surface) {
                                Surface tmp{*surface, {}};
                                tmp.surface_type = stInternalBridge;
                                tmp.bridge_angle = cs.bridge_angle;
                                for (const ExPolygon &ep : intersection_ex(union_ex(cs.new_polys),region->fill_expolygons)) {
                                    new_surfaces.emplace_back(tmp, ep);
                                }
                                break;
                            }
                        }
                    }
                }

                ExPolygons new_internal_solids = to_expolygons(internal_solids);
                new_internal_solids.insert(new_internal_solids.end(), additional_ensuring.begin(), additional_ensuring.end());
                new_internal_solids = diff_ex(new_internal_solids, cut_from_infill);
                new_internal_solids = union_safety_offset_ex(new_internal_solids);
                for (const ExPolygon &ep : new_internal_solids) {
                    new_surfaces.emplace_back(stInternalSolid, ep);
                }

#ifdef DEBUG_BRIDGE_OVER_INFILL
                debug_draw("Aensuring_" + std::to_string(reinterpret_cast<uint64_t>(&region)), to_polylines(additional_ensuring),
                    to_polylines(near_perimeters), to_polylines(to_polygons(internal_infills)),
                    to_polylines(to_polygons(internal_solids)));
                debug_draw("Aensuring_" + std::to_string(reinterpret_cast<uint64_t>(&region)) + "_new", to_polylines(additional_ensuring),
                    to_polylines(near_perimeters), to_polylines(to_polygons(new_internal_infills)),
                    to_polylines(to_polygons(new_internal_solids)));
#endif

                region->fill_surfaces.remove_types({stInternalSolid, stInternal});
                region->fill_surfaces.append(new_surfaces);
            }
        }
});

    BOOST_LOG_TRIVIAL(info) << "Bridge over infill - End" << log_memory_info();

} // void PrintObject::bridge_over_infill()
#endif

// Only active if config->infill_only_where_needed. This step trims the sparse infill,
// so it acts as an internal support. It maintains all other infill types intact.
// Here the internal surfaces and perimeters have to be supported by the sparse infill.
//FIXME The surfaces are supported by a sparse infill, but the sparse infill is only as large as the area to support.
// Likely the sparse infill will not be anchored correctly, so it will not work as intended.
// Also one wishes the perimeters to be supported by a full infill.
// Idempotence of this method is guaranteed by the fact that we don't remove things from
// fill_surfaces but we only turn them into VOID surfaces, thus preserving the boundaries.
void PrintObject::clip_fill_surfaces()
{
    if (! PrintObject::infill_only_where_needed)
        return;
    bool has_infill = false;
    for (size_t i = 0; i < this->num_printing_regions(); ++ i)
        if (this->printing_region(i).config().sparse_infill_density > 0) {
            has_infill = true;
            break;
        }
    if (! has_infill)
        return;

    // We only want infill under ceilings; this is almost like an
    // internal support material.
    // Proceed top-down, skipping the bottom layer.
    Polygons upper_internal;
    for (int layer_id = int(m_layers.size()) - 1; layer_id > 0; -- layer_id) {
        Layer *layer       = m_layers[layer_id];
        Layer *lower_layer = m_layers[layer_id - 1];
        // Detect things that we need to support.
        // Cummulative fill surfaces.
        Polygons fill_surfaces;
        // Solid surfaces to be supported.
        Polygons overhangs;
        for (const LayerRegion *layerm : layer->m_regions)
            for (const Surface &surface : layerm->fill_surfaces.surfaces) {
                Polygons polygons = to_polygons(surface.expolygon);
                if (surface.is_solid())
                    polygons_append(overhangs, polygons);
                polygons_append(fill_surfaces, std::move(polygons));
            }
        Polygons lower_layer_fill_surfaces;
        Polygons lower_layer_internal_surfaces;
        for (const LayerRegion *layerm : lower_layer->m_regions)
            for (const Surface &surface : layerm->fill_surfaces.surfaces) {
                Polygons polygons = to_polygons(surface.expolygon);
                if (surface.surface_type == stInternal || surface.surface_type == stInternalVoid)
                    polygons_append(lower_layer_internal_surfaces, polygons);
                polygons_append(lower_layer_fill_surfaces, std::move(polygons));
            }
        // We also need to support perimeters when there's at least one full unsupported loop
        {
            // Get perimeters area as the difference between slices and fill_surfaces
            // Only consider the area that is not supported by lower perimeters
            Polygons perimeters = intersection(diff(layer->lslices, fill_surfaces), lower_layer_fill_surfaces);
            // Only consider perimeter areas that are at least one extrusion width thick.
            //FIXME Offset2 eats out from both sides, while the perimeters are create outside in.
            //Should the pw not be half of the current value?
            float pw = FLT_MAX;
            for (const LayerRegion *layerm : layer->m_regions)
                pw = std::min(pw, (float)layerm->flow(frPerimeter).scaled_width());
            // Append such thick perimeters to the areas that need support
            polygons_append(overhangs, opening(perimeters, pw));
        }
        // Merge the new overhangs, find new internal infill.
        polygons_append(upper_internal, std::move(overhangs));
        static constexpr const auto closing_radius = scaled<float>(2.f);
        upper_internal = intersection(
            // Regularize the overhang regions, so that the infill areas will not become excessively jagged.
            smooth_outward(
                closing(upper_internal, closing_radius, ClipperLib::jtSquare, 0.),
                scaled<coord_t>(0.1)),
            lower_layer_internal_surfaces);
        // Apply new internal infill to regions.
        for (LayerRegion *layerm : lower_layer->m_regions) {
            if (layerm->region().config().sparse_infill_density.value == 0)
                continue;
            Polygons internal;
            for (Surface &surface : layerm->fill_surfaces.surfaces)
                if (surface.surface_type == stInternal || surface.surface_type == stInternalVoid)
                    polygons_append(internal, std::move(surface.expolygon));
            layerm->fill_surfaces.remove_types({ stInternal, stInternalVoid });
            layerm->fill_surfaces.append(intersection_ex(internal, upper_internal, ApplySafetyOffset::Yes), stInternal);
            layerm->fill_surfaces.append(diff_ex        (internal, upper_internal, ApplySafetyOffset::Yes), stInternalVoid);
            // If there are voids it means that our internal infill is not adjacent to
            // perimeters. In this case it would be nice to add a loop around infill to
            // make it more robust and nicer. TODO.
#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
            layerm->export_region_fill_surfaces_to_svg_debug("6_clip_fill_surfaces");
#endif
        }
        m_print->throw_if_canceled();
    }
}

void PrintObject::discover_horizontal_shells()
{
    BOOST_LOG_TRIVIAL(trace) << "discover_horizontal_shells()";

    for (size_t region_id = 0; region_id < this->num_printing_regions(); ++region_id) {
        for (size_t i = 0; i < m_layers.size(); ++i) {
            m_print->throw_if_canceled();
            Layer* layer = m_layers[i];
            LayerRegion* layerm = layer->regions()[region_id];
            const PrintRegionConfig& region_config = layerm->region().config();
            // If ensure_vertical_shell_thickness, then the rest has already been performed by discover_vertical_shells().
            if (region_config.ensure_vertical_shell_thickness.value!=EnsureVerticalThicknessLevel::evtDisabled)
                continue;

            coordf_t print_z = layer->print_z;
            coordf_t bottom_z = layer->bottom_z();
            for (size_t idx_surface_type = 0; idx_surface_type < 3; ++idx_surface_type) {
                m_print->throw_if_canceled();
                SurfaceType type = (idx_surface_type == 0) ? stTop : (idx_surface_type == 1) ? stBottom : stBottomBridge;
                int num_solid_layers = (type == stTop) ? region_config.top_shell_layers.value : region_config.bottom_shell_layers.value;
                if (num_solid_layers == 0)
                    continue;
                // Find slices of current type for current layer.
                // Use slices instead of fill_surfaces, because they also include the perimeter area,
                // which needs to be propagated in shells; we need to grow slices like we did for
                // fill_surfaces though. Using both ungrown slices and grown fill_surfaces will
                // not work in some situations, as there won't be any grown region in the perimeter
                // area (this was seen in a model where the top layer had one extra perimeter, thus
                // its fill_surfaces were thinner than the lower layer's infill), however it's the best
                // solution so far. Growing the external slices by EXTERNAL_INFILL_MARGIN will put
                // too much solid infill inside nearly-vertical slopes.

                // Surfaces including the area of perimeters. Everything, that is visible from the top / bottom
                // (not covered by a layer above / below).
                // This does not contain the areas covered by perimeters!
                Polygons solid;
                for (const Surface& surface : layerm->slices.surfaces)
                    if (surface.surface_type == type)
                        polygons_append(solid, to_polygons(surface.expolygon));
                // Infill areas (slices without the perimeters).
                for (const Surface& surface : layerm->fill_surfaces.surfaces)
                    if (surface.surface_type == type)
                        polygons_append(solid, to_polygons(surface.expolygon));
                if (solid.empty())
                    continue;
                //                Slic3r::debugf "Layer %d has %s surfaces\n", $i, ($type == stTop) ? 'top' : 'bottom';

                                // Scatter top / bottom regions to other layers. Scattering process is inherently serial, it is difficult to parallelize without locking.
                for (int n = (type == stTop) ? int(i) - 1 : int(i) + 1;
                    (type == stTop) ?
                    (n >= 0 && (int(i) - n < num_solid_layers ||
                        print_z - m_layers[n]->print_z < region_config.top_shell_thickness.value - EPSILON)) :
                    (n < int(m_layers.size()) && (n - int(i) < num_solid_layers ||
                        m_layers[n]->bottom_z() - bottom_z < region_config.bottom_shell_thickness.value - EPSILON));
                    (type == stTop) ? --n : ++n)
                {
                    //                    Slic3r::debugf "  looking for neighbors on layer %d...\n", $n;
                                        // Reference to the lower layer of a TOP surface, or an upper layer of a BOTTOM surface.
                    LayerRegion* neighbor_layerm = m_layers[n]->regions()[region_id];

                    // find intersection between neighbor and current layer's surfaces
                    // intersections have contours and holes
                    // we update $solid so that we limit the next neighbor layer to the areas that were
                    // found on this one - in other words, solid shells on one layer (for a given external surface)
                    // are always a subset of the shells found on the previous shell layer
                    // this approach allows for DWIM in hollow sloping vases, where we want bottom
                    // shells to be generated in the base but not in the walls (where there are many
                    // narrow bottom surfaces): reassigning $solid will consider the 'shadow' of the
                    // upper perimeter as an obstacle and shell will not be propagated to more upper layers
                    //FIXME How does it work for stInternalBRIDGE? This is set for sparse infill. Likely this does not work.
                    Polygons new_internal_solid;
                    {
                        Polygons internal;
                        for (const Surface& surface : neighbor_layerm->fill_surfaces.surfaces)
                            if (surface.surface_type == stInternal || surface.surface_type == stInternalSolid)
                                polygons_append(internal, to_polygons(surface.expolygon));
                        new_internal_solid = intersection(solid, internal, ApplySafetyOffset::Yes);
                    }
                    if (new_internal_solid.empty()) {
                        // No internal solid needed on this layer. In order to decide whether to continue
                        // searching on the next neighbor (thus enforcing the configured number of solid
                        // layers, use different strategies according to configured infill density:
                        if (region_config.sparse_infill_density.value == 0 || region_config.ensure_vertical_shell_thickness.value == EnsureVerticalThicknessLevel::evtDisabled) {
                            // If user expects the object to be void (for example a hollow sloping vase),
                            // don't continue the search. In this case, we only generate the external solid
                            // shell if the object would otherwise show a hole (gap between perimeters of
                            // the two layers), and internal solid shells are a subset of the shells found
                            // on each previous layer.
                            goto EXTERNAL;
                        }
                        else {
                            // If we have internal infill, we can generate internal solid shells freely.
                            continue;
                        }
                    }

                    if (region_config.sparse_infill_density.value == 0) {
                        // if we're printing a hollow object we discard any solid shell thinner
                        // than a perimeter width, since it's probably just crossing a sloping wall
                        // and it's not wanted in a hollow print even if it would make sense when
                        // obeying the solid shell count option strictly (DWIM!)
                        float margin = float(neighbor_layerm->flow(frExternalPerimeter).scaled_width());
                        Polygons too_narrow = diff(
                            new_internal_solid,
                            opening(new_internal_solid, margin, margin + ClipperSafetyOffset, jtMiter, 5));
                        // Trim the regularized region by the original region.
                        if (!too_narrow.empty())
                            new_internal_solid = solid = diff(new_internal_solid, too_narrow);
                    }

                    // make sure the new internal solid is wide enough, as it might get collapsed
                    // when spacing is added in Fill.pm
                    {
                        //FIXME Vojtech: Disable this and you will be sorry.
                        float margin = 3.f * layerm->flow(frSolidInfill).scaled_width(); // require at least this size
                        // we use a higher miterLimit here to handle areas with acute angles
                        // in those cases, the default miterLimit would cut the corner and we'd
                        // get a triangle in $too_narrow; if we grow it below then the shell
                        // would have a different shape from the external surface and we'd still
                        // have the same angle, so the next shell would be grown even more and so on.
                        Polygons too_narrow = diff(
                            new_internal_solid,
                            opening(new_internal_solid, margin, margin + ClipperSafetyOffset, ClipperLib::jtMiter, 5));
                        if (!too_narrow.empty()) {
                            // grow the collapsing parts and add the extra area to  the neighbor layer
                            // as well as to our original surfaces so that we support this
                            // additional area in the next shell too
                            // make sure our grown surfaces don't exceed the fill area
                            Polygons internal;
                            for (const Surface& surface : neighbor_layerm->fill_surfaces.surfaces)
                                if (surface.is_internal() && !surface.is_bridge())
                                    polygons_append(internal, to_polygons(surface.expolygon));
                            polygons_append(new_internal_solid,
                                intersection(
                                    expand(too_narrow, +margin),
                                    // Discard bridges as they are grown for anchoring and we can't
                                    // remove such anchors. (This may happen when a bridge is being
                                    // anchored onto a wall where little space remains after the bridge
                                    // is grown, and that little space is an internal solid shell so
                                    // it triggers this too_narrow logic.)
                                    internal));
                            // solid = new_internal_solid;
                        }
                    }

                    // internal-solid are the union of the existing internal-solid surfaces
                    // and new ones
                    SurfaceCollection backup = std::move(neighbor_layerm->fill_surfaces);
                    polygons_append(new_internal_solid, to_polygons(backup.filter_by_type(stInternalSolid)));
                    ExPolygons internal_solid = union_ex(new_internal_solid);
                    // assign new internal-solid surfaces to layer
                    neighbor_layerm->fill_surfaces.set(internal_solid, stInternalSolid);
                    // subtract intersections from layer surfaces to get resulting internal surfaces
                    Polygons polygons_internal = to_polygons(std::move(internal_solid));
                    ExPolygons internal = diff_ex(backup.filter_by_type(stInternal), polygons_internal, ApplySafetyOffset::Yes);
                    // assign resulting internal surfaces to layer
                    neighbor_layerm->fill_surfaces.append(internal, stInternal);
                    polygons_append(polygons_internal, to_polygons(std::move(internal)));
                    // assign top and bottom surfaces to layer
                    backup.keep_types({ stTop, stBottom, stBottomBridge });
                    std::vector<SurfacesPtr> top_bottom_groups;
                    backup.group(&top_bottom_groups);
                    for (SurfacesPtr& group : top_bottom_groups)
                        neighbor_layerm->fill_surfaces.append(
                            diff_ex(group, polygons_internal),
                            // Use an existing surface as a template, it carries the bridge angle etc.
                            *group.front());
                }
            EXTERNAL:;
            } // foreach type (stTop, stBottom, stBottomBridge)
        } // for each layer
    } // for each region

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
    for (size_t region_id = 0; region_id < this->num_printing_regions(); ++region_id) {
        for (const Layer* layer : m_layers) {
            const LayerRegion* layerm = layer->m_regions[region_id];
            layerm->export_region_slices_to_svg_debug("5_discover_horizontal_shells");
            layerm->export_region_fill_surfaces_to_svg_debug("5_discover_horizontal_shells");
        } // for each layer
    } // for each region
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
}

// combine fill surfaces across layers to honor the "infill every N layers" option
// Idempotence of this method is guaranteed by the fact that we don't remove things from
// fill_surfaces but we only turn them into VOID surfaces, thus preserving the boundaries.
void PrintObject::combine_infill()
{
    // Work on each region separately.
    for (size_t region_id = 0; region_id < this->num_printing_regions(); ++ region_id) {
        const PrintRegion &region = this->printing_region(region_id);
        //BBS
        const bool enable_combine_infill = region.config().infill_combination.value;
        if (enable_combine_infill == false || region.config().sparse_infill_density == 0.)
            continue;
        // Limit the number of combined layers to the maximum height allowed by this regions' nozzle.
        //FIXME limit the layer height to max_layer_height
        double nozzle_diameter = std::min(
            this->print()->config().nozzle_diameter.get_at(region.config().sparse_infill_filament.value - 1),
            this->print()->config().nozzle_diameter.get_at(region.config().solid_infill_filament.value - 1));
        // define the combinations
        std::vector<size_t> combine(m_layers.size(), 0);
        {
            double current_height = 0.;
            size_t num_layers = 0;
            for (size_t layer_idx = 0; layer_idx < m_layers.size(); ++ layer_idx) {
                m_print->throw_if_canceled();
                const Layer *layer = m_layers[layer_idx];
                if (layer->id() == 0)
                    // Skip first print layer (which may not be first layer in array because of raft).
                    continue;
                // Check whether the combination of this layer with the lower layers' buffer
                // would exceed max layer height or max combined layer count.
                // BBS: automatically calculate how many layers should be combined
                if (current_height + layer->height >= nozzle_diameter + EPSILON) {
                    // Append combination to lower layer.
                    combine[layer_idx - 1] = num_layers;
                    current_height = 0.;
                    num_layers = 0;
                }
                current_height += layer->height;
                ++ num_layers;
            }

            // Append lower layers (if any) to uppermost layer.
            combine[m_layers.size() - 1] = num_layers;
        }

        // loop through layers to which we have assigned layers to combine
        for (size_t layer_idx = 0; layer_idx < m_layers.size(); ++ layer_idx) {
            m_print->throw_if_canceled();
            size_t num_layers = combine[layer_idx];
			if (num_layers <= 1)
                continue;
            // Get all the LayerRegion objects to be combined.
            std::vector<LayerRegion*> layerms;
            layerms.reserve(num_layers);
			for (size_t i = layer_idx + 1 - num_layers; i <= layer_idx; ++ i)
                layerms.emplace_back(m_layers[i]->regions()[region_id]);
            // We need to perform a multi-layer intersection, so let's split it in pairs.
            // Initialize the intersection with the candidates of the lowest layer.
            ExPolygons intersection = to_expolygons(layerms.front()->fill_surfaces.filter_by_type(stInternal));
            // Start looping from the second layer and intersect the current intersection with it.
            for (size_t i = 1; i < layerms.size(); ++ i)
                intersection = intersection_ex(layerms[i]->fill_surfaces.filter_by_type(stInternal), intersection);
            double area_threshold = layerms.front()->infill_area_threshold();
            if (! intersection.empty() && area_threshold > 0.)
                intersection.erase(std::remove_if(intersection.begin(), intersection.end(),
                    [area_threshold](const ExPolygon &expoly) { return expoly.area() <= area_threshold; }),
                    intersection.end());
            if (intersection.empty())
                continue;
//            Slic3r::debugf "  combining %d %s regions from layers %d-%d\n",
//                scalar(@$intersection),
//                ($type == stInternal ? 'internal' : 'internal-solid'),
//                $layer_idx-($every-1), $layer_idx;
            // intersection now contains the regions that can be combined across the full amount of layers,
            // so let's remove those areas from all layers.
            Polygons intersection_with_clearance;
            intersection_with_clearance.reserve(intersection.size());
            float clearance_offset =
                0.5f * layerms.back()->flow(frPerimeter).scaled_width() +
             // Because fill areas for rectilinear and honeycomb are grown
             // later to overlap perimeters, we need to counteract that too.
                ((region.config().sparse_infill_pattern == ipRectilinear   ||
                  region.config().sparse_infill_pattern == ipMonotonic     ||
                  region.config().sparse_infill_pattern == ipGrid          ||
                  region.config().sparse_infill_pattern == ip2DLattice     ||
                  region.config().sparse_infill_pattern == ipLine          ||
                  region.config().sparse_infill_pattern == ipHoneycomb) ? 1.5f : 0.5f) *
                    layerms.back()->flow(frSolidInfill).scaled_width();
            for (ExPolygon &expoly : intersection)
                polygons_append(intersection_with_clearance, offset(expoly, clearance_offset));
            for (LayerRegion *layerm : layerms) {
                Polygons internal = to_polygons(std::move(layerm->fill_surfaces.filter_by_type(stInternal)));
                layerm->fill_surfaces.remove_type(stInternal);
                layerm->fill_surfaces.append(diff_ex(internal, intersection_with_clearance), stInternal);
                if (layerm == layerms.back()) {
                    // Apply surfaces back with adjusted depth to the uppermost layer.
                    Surface templ(stInternal, ExPolygon());
                    templ.thickness = 0.;
                    for (LayerRegion *layerm2 : layerms)
                        templ.thickness += layerm2->layer()->height;
                    templ.thickness_layers = (unsigned short)layerms.size();
                    layerm->fill_surfaces.append(intersection, templ);
                } else {
                    // Save void surfaces.
                    layerm->fill_surfaces.append(
                        intersection_ex(internal, intersection_with_clearance),
                        stInternalVoid);
                }
            }
        }
    }
}


} // namespace Slic3r
