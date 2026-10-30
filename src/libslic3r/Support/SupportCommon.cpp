///|/ Copyright (c) Prusa Research 2023 Vojtěch Bubník @bubnikv, Pavel Mikuš @Godrak
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "../ClipperUtils.hpp"
// #include "../ClipperZUtils.hpp"
#include "../ExtrusionEntityCollection.hpp"
#include "../Layer.hpp"
#include "../Print.hpp"
#include "../Fill/FillBase.hpp"
#include "../MutablePolygon.hpp"
#include "../Geometry.hpp"
#include "../Point.hpp"
#include "clipper/clipper_z.hpp"

#include <cmath>
#include <limits>
#include <boost/container/static_vector.hpp>

#include <tbb/parallel_for.h>

#include "SupportCommon.hpp"
#include "SupportLayer.hpp"
#include "SupportParameters.hpp"

// #define SLIC3R_DEBUG

// Make assert active if SLIC3R_DEBUG
#ifdef SLIC3R_DEBUG
    #define DEBUG
    #define _DEBUG
    #undef NDEBUG
    #include "../utils.hpp"
    #include "../SVG.hpp"
#endif

#include <cassert>
#include "libslic3r/Log.hpp"

namespace Slic3r {

// how much we extend support around the actual contact area
//FIXME this should be dependent on the nozzle diameter!
#define SUPPORT_MATERIAL_MARGIN 1.5

//#define SUPPORT_SURFACES_OFFSET_PARAMETERS ClipperLib::jtMiter, 3.
//#define SUPPORT_SURFACES_OFFSET_PARAMETERS ClipperLib::jtMiter, 1.5
#define SUPPORT_SURFACES_OFFSET_PARAMETERS ClipperLib::jtSquare, 0.


// Drop null and empty layers so the compacted vector only carries real support geometry.
static void compact_layers(SupportGeneratorLayersPtr &vec)
{
    vec.erase(std::remove_if(vec.begin(), vec.end(),
        [](const SupportGeneratorLayer *ptr) { return ptr == nullptr || ptr->polygons.empty(); }), vec.end());
}

// Convert some of the intermediate layers into top/bottom interface layers as well as base interface layers.
std::pair<SupportGeneratorLayersPtr, SupportGeneratorLayersPtr> generate_interface_layers(
    const PrintObjectConfig           &config,
    const SupportParameters           &support_params,
    const SupportGeneratorLayersPtr   &bottom_contacts,
    const SupportGeneratorLayersPtr   &top_contacts,
    // Input / output, will be merged with output. Only provided for Organic supports.
    SupportGeneratorLayersPtr         &top_interface_layers,
    SupportGeneratorLayersPtr         &top_base_interface_layers,
    // Input, will be trimmed with the newly created interface layers.
    SupportGeneratorLayersPtr         &intermediate_layers,
    SupportGeneratorLayerStorage      &layer_storage)
{
    std::pair<SupportGeneratorLayersPtr, SupportGeneratorLayersPtr> base_and_interface_layers;

    if (! intermediate_layers.empty() && support_params.has_interfaces()) {
        // For all intermediate layers, collect top contact surfaces, which are not further than support_material_interface_layers.
        BOOST_LOG_TRIVIAL(debug) << "in parallel - start";
        const bool                 snug_supports          = support_params.support_style == smsSnug;
        const bool                 smooth_supports        = support_params.support_style != smsGrid;
        SupportGeneratorLayersPtr &interface_layers       = base_and_interface_layers.first;
        SupportGeneratorLayersPtr &base_interface_layers  = base_and_interface_layers.second;

        interface_layers.assign(intermediate_layers.size(), nullptr);
        if (support_params.has_base_interfaces())
            base_interface_layers.assign(intermediate_layers.size(), nullptr);
        const auto smoothing_distance    = support_params.support_material_interface_flow.scaled_spacing() * 1.5;
        const auto minimum_island_radius = support_params.support_material_interface_flow.scaled_spacing() / support_params.interface_density;
        const auto closing_distance      = smoothing_distance; // scaled<float>(config.support_material_closing_radius.value);
        // Insert a new layer into base_interface_layers, if intersection with base exists.
        auto insert_layer = [&layer_storage, smooth_supports, closing_distance, smoothing_distance, minimum_island_radius](
                SupportGeneratorLayer &intermediate_layer, Polygons &bottom, Polygons &&top, SupportGeneratorLayer *top_interface_layer,
                const Polygons *subtract, SupporLayerType type) -> SupportGeneratorLayer* {
            bool has_top_interface = top_interface_layer && ! top_interface_layer->polygons.empty();
            assert(! bottom.empty() || ! top.empty() || has_top_interface);
            // Merge top into bottom, unite them with a safety offset.
            append(bottom, std::move(top));
            // Merge top / bottom interfaces. For snug supports, merge using closing distance and regularize (close concave corners).
            bottom = intersection(
                smooth_supports ?
                    smooth_outward(closing(std::move(bottom), closing_distance + minimum_island_radius, closing_distance, SUPPORT_SURFACES_OFFSET_PARAMETERS), smoothing_distance) :
                    union_safety_offset(std::move(bottom)),
                intermediate_layer.polygons);
            if (has_top_interface) {
                // Don't trim the precomputed Organic supports top interface with base layer
                // as the precomputed top interface likely expands over multiple tree tips.
                bottom = union_(std::move(top_interface_layer->polygons), bottom);
                top_interface_layer->polygons.clear();
            }
            if (! bottom.empty()) {
                //FIXME Remove non-printable tiny islands, let them be printed using the base support.
                //bottom = opening(std::move(bottom), minimum_island_radius);
                if (! bottom.empty()) {
                    SupportGeneratorLayer &layer_new = top_interface_layer ? *top_interface_layer : layer_storage.allocate(type);
                    layer_new.polygons   = std::move(bottom);
                    layer_new.print_z    = intermediate_layer.print_z;
                    layer_new.bottom_z   = intermediate_layer.bottom_z;
                    layer_new.height     = intermediate_layer.height;
                    layer_new.bridging   = intermediate_layer.bridging;
                    // Subtract the interface from the base regions.
                    intermediate_layer.polygons = diff(intermediate_layer.polygons, layer_new.polygons);
                    if (subtract)
                        // Trim the base interface layer with the interface layer.
                        layer_new.polygons = diff(std::move(layer_new.polygons), *subtract);
                    //FIXME filter layer_new.polygons islands by a minimum area?
        //                  $interface_area = [ grep abs($_->area) >= $area_threshold, @$interface_area ];
                    return &layer_new;
                }
            }
            return nullptr;
        };
        const bool istree = is_tree(config.support_type);
        tbb::parallel_for(tbb::blocked_range<int>(0, int(intermediate_layers.size())),
            [&bottom_contacts, &top_contacts, &top_interface_layers, &top_base_interface_layers, &intermediate_layers, &insert_layer, &support_params,
             snug_supports, &interface_layers, &base_interface_layers, &istree](const tbb::blocked_range<int>& range) {
                // Gather the top / bottom contact layers intersecting with num_interface_layers resp. num_interface_layers_only intermediate layers above / below
                // this intermediate layer.
                // Index of the first top contact layer intersecting the current intermediate layer.
                auto idx_top_contact_first        = -1;
                // Index of the first bottom contact layer intersecting the current intermediate layer.
                auto idx_bottom_contact_first     = -1;
                // Index of the first top interface layer intersecting the current intermediate layer.
                auto idx_top_interface_first      = -1;
                // Index of the first top contact interface layer intersecting the current intermediate layer.
                auto idx_top_base_interface_first = -1;
                auto num_intermediate = int(intermediate_layers.size());
                for (int idx_intermediate_layer = range.begin(); idx_intermediate_layer < range.end(); ++ idx_intermediate_layer) {
                    SupportGeneratorLayer &intermediate_layer = *intermediate_layers[idx_intermediate_layer];
                    Polygons polygons_top_contact_projected_interface;
                    Polygons polygons_top_contact_projected_base;
                    Polygons polygons_bottom_contact_projected_interface;
                    Polygons polygons_bottom_contact_projected_base;
                    if (support_params.num_top_interface_layers > 0) {
                        if (istree) {
                            // Top Z coordinate of a slab, over which we are collecting the top / bottom contact surfaces
                            coordf_t top_z = intermediate_layers[std::min(num_intermediate - 1, idx_intermediate_layer + int(support_params.num_top_interface_layers) - 1)]
                                                 ->print_z;
                            coordf_t top_inteface_z = std::numeric_limits<coordf_t>::max();
                            if (support_params.num_top_base_interface_layers > 0)
                                // Some top base interface layers will be generated.
                                top_inteface_z =
                                    support_params.num_top_interface_layers_only() == 0 ?
                                        // Only base interface layers to generate.
                                        -std::numeric_limits<coordf_t>::max() :
                                        intermediate_layers[std::min(num_intermediate - 1, idx_intermediate_layer + int(support_params.num_top_interface_layers_only()) - 1)]
                                            ->print_z;
                            // Move idx_top_contact_first up until above the current print_z.
                            idx_top_contact_first = idx_higher_or_equal(top_contacts, idx_top_contact_first, [&intermediate_layer](const SupportGeneratorLayer *layer) {
                                return layer->print_z >= intermediate_layer.print_z;
                            }); //  - EPSILON
                            // Collect the top contact areas above this intermediate layer, below top_z.
                            for (int idx_top_contact = idx_top_contact_first; idx_top_contact < int(top_contacts.size()); ++idx_top_contact) {
                                const SupportGeneratorLayer &top_contact_layer = *top_contacts[idx_top_contact];
                                // FIXME maybe this adds one interface layer in excess?
                                if (top_contact_layer.bottom_z - EPSILON > top_z) break;
                                polygons_append(top_contact_layer.bottom_z - EPSILON > top_inteface_z ? polygons_top_contact_projected_base :
                                                                                                        polygons_top_contact_projected_interface,
                                                // For snug supports, project the overhang polygons covering the whole overhang, so that they will merge without a gap with support
                                                // polygons of the other layers. For grid supports, merging of support regions will be performed by the projection into grid.
                                                snug_supports ? *top_contact_layer.overhang_polygons : top_contact_layer.polygons);
                            }
                        } else {
                            // Move idx_top_contact_first up until above the current print_z.
                            idx_top_contact_first = idx_higher_or_equal(top_contacts, idx_top_contact_first, [&intermediate_layer](const SupportGeneratorLayer *layer){ return layer->print_z >= intermediate_layer.print_z; }); //  - EPSILON
                            // Collect the top contact areas above this intermediate layer, below top_z.
                            for (int idx_top_contact = idx_top_contact_first; idx_top_contact < int(top_contacts.size()); ++ idx_top_contact) {
                                const SupportGeneratorLayer &top_contact_layer = *top_contacts[idx_top_contact];
                                const bool is_top_contact = is_approx(top_contact_layer.bottom_z, intermediate_layers[num_intermediate - 1]->print_z);
                                if (is_top_contact) {
                                    if (idx_intermediate_layer > num_intermediate - int(support_params.num_top_interface_layers))
                                        polygons_append(polygons_top_contact_projected_interface, snug_supports ? *top_contact_layer.overhang_polygons : top_contact_layer.polygons);
                                    else if (idx_intermediate_layer > num_intermediate - int(support_params.num_top_interface_layers) - int(support_params.num_top_base_interface_layers))
                                        polygons_append(polygons_top_contact_projected_base, snug_supports ? *top_contact_layer.overhang_polygons : top_contact_layer.polygons);
                                } else {
                                    if (top_contact_layer.print_z - EPSILON <
                                        intermediate_layers[std::min(int(idx_intermediate_layer + support_params.num_top_interface_layers - 1), num_intermediate - 1)]->print_z)
                                        polygons_append(polygons_top_contact_projected_interface, snug_supports ? *top_contact_layer.overhang_polygons : top_contact_layer.polygons);
                                    else if (top_contact_layer.print_z - EPSILON < intermediate_layers[std::min(int(idx_intermediate_layer + support_params.num_top_interface_layers -
                                                                                                                    1 + support_params.num_top_base_interface_layers),num_intermediate - 1)]->print_z)
                                        polygons_append(polygons_top_contact_projected_base, snug_supports ? *top_contact_layer.overhang_polygons : top_contact_layer.polygons);
                                }
                            }
                        }
                    }
                    if (support_params.num_bottom_interface_layers > 0) {
                        // Bottom Z coordinate of a slab, over which we are collecting the top / bottom contact surfaces
                        coordf_t bottom_z           = intermediate_layers[std::max(0, idx_intermediate_layer - int(support_params.num_bottom_interface_layers) + 1)]->bottom_z;
                        coordf_t bottom_interface_z = - std::numeric_limits<coordf_t>::max();
                        if (support_params.num_bottom_base_interface_layers > 0)
                            // Some bottom base interface layers will be generated.
                            bottom_interface_z = support_params.num_bottom_interface_layers_only() == 0 ?
                                // Only base interface layers to generate.
                                std::numeric_limits<coordf_t>::max() :
                                intermediate_layers[std::max(0, idx_intermediate_layer - int(support_params.num_bottom_interface_layers_only()))]->bottom_z;
                        // Move idx_bottom_contact_first up until touching bottom_z.
                        idx_bottom_contact_first = idx_higher_or_equal(bottom_contacts, idx_bottom_contact_first, [bottom_z](const SupportGeneratorLayer *layer){ return layer->print_z >= bottom_z - EPSILON; });
                        // Collect the top contact areas above this intermediate layer, below top_z.
                        for (int idx_bottom_contact = idx_bottom_contact_first; idx_bottom_contact < int(bottom_contacts.size()); ++ idx_bottom_contact) {
                            const SupportGeneratorLayer &bottom_contact_layer = *bottom_contacts[idx_bottom_contact];
                            if (bottom_contact_layer.print_z - EPSILON > intermediate_layer.bottom_z)
                                break;
                            polygons_append(bottom_contact_layer.print_z - EPSILON > bottom_interface_z ? polygons_bottom_contact_projected_interface : polygons_bottom_contact_projected_base, bottom_contact_layer.polygons);
                        }
                    }
                    auto resolve_same_layer = [](SupportGeneratorLayersPtr &layers, int &idx, coordf_t print_z) -> SupportGeneratorLayer* {
                        if (! layers.empty()) {
                            idx = idx_higher_or_equal(layers, idx, [print_z](const SupportGeneratorLayer *layer) { return layer->print_z > print_z - EPSILON; });
                            if (idx < int(layers.size()) && layers[idx]->print_z < print_z + EPSILON)
                                return layers[idx];
                        }
                        return nullptr;
                    };
                    SupportGeneratorLayer *top_interface_layer      = resolve_same_layer(top_interface_layers, idx_top_interface_first, intermediate_layer.print_z);
                    SupportGeneratorLayer *top_base_interface_layer = resolve_same_layer(top_base_interface_layers, idx_top_base_interface_first, intermediate_layer.print_z);
                    SupportGeneratorLayer *interface_layer          = nullptr;
                    if (! polygons_bottom_contact_projected_interface.empty() || ! polygons_top_contact_projected_interface.empty() ||
                        (top_interface_layer && ! top_interface_layer->polygons.empty())) {
                        interface_layer = insert_layer(
                            intermediate_layer, polygons_bottom_contact_projected_interface, std::move(polygons_top_contact_projected_interface), top_interface_layer,
                            nullptr, polygons_top_contact_projected_interface.empty() ? sltBottomInterface : sltTopInterface);
                        interface_layers[idx_intermediate_layer] = interface_layer;
                    }
                    if (! polygons_bottom_contact_projected_base.empty() || ! polygons_top_contact_projected_base.empty() ||
                        (top_base_interface_layer && ! top_base_interface_layer->polygons.empty()))
                        base_interface_layers[idx_intermediate_layer] = insert_layer(
                            intermediate_layer, polygons_bottom_contact_projected_base, std::move(polygons_top_contact_projected_base), top_base_interface_layer,
                            interface_layer ? &interface_layer->polygons : nullptr, sltBase);
                }
            });

        if (support_params.num_top_base_interface_layers > 1 && !istree)
            tbb::parallel_for(tbb::blocked_range<int>(1, int(base_interface_layers.size())),
                              [&base_interface_layers, &top_contacts, &support_params, &intermediate_layers](const tbb::blocked_range<int> &range) {
                                  for (int layer_id = range.begin(); layer_id < range.end(); ++layer_id) {
                                      auto &base_interface_layer = base_interface_layers[layer_id];
                                      if (!base_interface_layer) return;

                                      for (const auto &contact : top_contacts) {
                                          if (is_approx(contact->bottom_z,
                                                        intermediate_layers[std::min(layer_id - 1 + support_params.num_top_interface_layers, intermediate_layers.size() - 1)]
                                                            ->print_z, 0.01) &&
                                              overlaps(base_interface_layer->polygons, contact->polygons)) {
                                              base_interface_layer->up = true;
                                              break;
                                          }
                                      }
                                  }
                              });

        // Compress contact_out, remove the nullptr items.
        // The parallel_for above may not have merged all the interface and base_interface layers
        // generated by the Organic supports code, do it here.
        auto merge_remove_empty = [](SupportGeneratorLayersPtr &in1, SupportGeneratorLayersPtr &in2) {
            compact_layers(in1);
            compact_layers(in2);
            if (in2.empty())
                return std::move(in1);
            else if (in1.empty())
                return std::move(in2);
            else {
                SupportGeneratorLayersPtr out(in1.size() + in2.size(), nullptr);
                std::merge(in1.begin(), in1.end(), in2.begin(), in2.end(), out.begin(), [](auto* l, auto* r) { return l->print_z < r->print_z; });
                return out;
            }
        };
        interface_layers      = merge_remove_empty(interface_layers,      top_interface_layers);
        base_interface_layers = merge_remove_empty(base_interface_layers, top_base_interface_layers);
        BOOST_LOG_TRIVIAL(debug) << "in parallel - end";
    } else if (support_params.has_interfaces()) {
        // Organic supports can place roofs that have no tree body underneath at all, which happens for
        // overhangs sitting only a couple of layers above the bed. The loop above is driven by
        // intermediate_layers, so an empty base would drop the already computed roofs and leave a single
        // contact layer floating above empty layers. Pass them through instead.
        compact_layers(top_interface_layers);
        compact_layers(top_base_interface_layers);
        base_and_interface_layers.first  = std::move(top_interface_layers);
        base_and_interface_layers.second = std::move(top_base_interface_layers);
    }

    return base_and_interface_layers;
}

SupportGeneratorLayersPtr generate_raft_base(
    const PrintObject                 &object,
    const SupportParameters           &support_params,
	const SlicingParameters			  &slicing_params,
    const SupportGeneratorLayersPtr   &top_contacts,
    const SupportGeneratorLayersPtr   &interface_layers,
    const SupportGeneratorLayersPtr   &base_interface_layers,
    const SupportGeneratorLayersPtr   &base_layers,
    SupportGeneratorLayerStorage      &layer_storage)
{
    // If there is brim to be generated, calculate the trimming regions.
    Polygons brim;
    if (object.has_brim()) {
        // The object does not have a raft.
        // Calculate the area covered by the brim.
        const BrimType brim_type       = object.config().brim_type;
        const bool     brim_outer      = brim_type == btOuterOnly || brim_type == btOuterAndInner;
        const bool     brim_inner      = brim_type == btInnerOnly || brim_type == btOuterAndInner;
        // BBS: the pattern of raft and brim are the same, thus the brim can be serpated by support raft.
        const auto     brim_object_gap = scaled<float>(object.config().brim_object_gap.value);
        //const auto     brim_object_gap = scaled<float>(object.config().brim_object_gap.value + object.config().brim_width.value);
        for (const ExPolygon &ex : object.layers().front()->lslices) {
            if (brim_outer && brim_inner)
                polygons_append(brim, offset(ex, brim_object_gap));
            else {
                if (brim_outer)
                    polygons_append(brim, offset(ex.contour, brim_object_gap, ClipperLib::jtRound, float(scale_(0.1))));
                else
                    brim.emplace_back(ex.contour);
                if (brim_inner) {
                    Polygons holes = ex.holes;
                    polygons_reverse(holes);
                    holes = shrink(holes, brim_object_gap, ClipperLib::jtRound, float(scale_(0.1)));
                    polygons_reverse(holes);
                    polygons_append(brim, std::move(holes));
                } else
                    polygons_append(brim, ex.holes);
            }
        }
        brim = union_(brim);
    }

    // How much to inflate the support columns to be stable. This also applies to the 1st layer, if no raft layers are to be printed.
    const float inflate_factor_fine = float(scale_((slicing_params.raft_layers() > 1) ? 0.5 : EPSILON));
    float  inflate_factor_1st_layer = float(scale_(object.config().raft_first_layer_expansion));
    SupportGeneratorLayer       *contacts         = top_contacts         .empty() ? nullptr : top_contacts         .front();
    SupportGeneratorLayer       *interfaces       = interface_layers     .empty() ? nullptr : interface_layers     .front();
    SupportGeneratorLayer       *base_interfaces  = base_interface_layers.empty() ? nullptr : base_interface_layers.front();
    SupportGeneratorLayer       *columns_base     = base_layers          .empty() ? nullptr : base_layers          .front();
    if (contacts != nullptr && contacts->print_z > std::max(slicing_params.first_print_layer_height, slicing_params.raft_contact_top_z) + EPSILON)
        // This is not the raft contact layer.
        contacts = nullptr;
    if (interfaces != nullptr && interfaces->bottom_print_z() > slicing_params.raft_interface_top_z + EPSILON)
        // This is not the raft column base layer.
        interfaces = nullptr;
    if (base_interfaces != nullptr && base_interfaces->bottom_print_z() > slicing_params.raft_interface_top_z + EPSILON)
        // This is not the raft column base layer.
        base_interfaces = nullptr;
    if (columns_base != nullptr && columns_base->bottom_print_z() > slicing_params.raft_interface_top_z + EPSILON)
        // This is not the raft interface layer.
        columns_base = nullptr;

    Polygons interface_polygons;
    if (contacts != nullptr && ! contacts->polygons.empty())
        polygons_append(interface_polygons, expand(contacts->polygons, inflate_factor_fine, SUPPORT_SURFACES_OFFSET_PARAMETERS));
    if (interfaces != nullptr && ! interfaces->polygons.empty())
        polygons_append(interface_polygons, expand(interfaces->polygons, inflate_factor_fine, SUPPORT_SURFACES_OFFSET_PARAMETERS));
    if (base_interfaces != nullptr && ! base_interfaces->polygons.empty())
        polygons_append(interface_polygons, expand(base_interfaces->polygons, inflate_factor_fine, SUPPORT_SURFACES_OFFSET_PARAMETERS));

    // Output vector.
    SupportGeneratorLayersPtr raft_layers;

    if (slicing_params.raft_layers() > 1) {
        Polygons base;
        Polygons columns;
        Polygons first_layer;
        if (columns_base != nullptr) {
            if (columns_base->bottom_print_z() > slicing_params.raft_interface_top_z - EPSILON) {
                // Classic supports with colums above the raft interface.
                base = columns_base->polygons;
                columns = base;
                if (! interface_polygons.empty())
                    // Trim the 1st layer columns with the inflated interface polygons.
                    columns = diff(columns, interface_polygons);
            } else {
                // Organic supports with raft on print bed.
                assert(is_approx(columns_base->print_z, slicing_params.first_print_layer_height));
                first_layer = columns_base->polygons;
            }
        }
        if (! interface_polygons.empty()) {
            // Merge the untrimmed columns base with the expanded raft interface, to be used for the support base and interface.
            base = union_(base, interface_polygons);
        }
        // Do not add the raft contact layer, only add the raft layers below the contact layer.
        // Insert the 1st layer.
        {
            SupportGeneratorLayer &new_layer = layer_storage.allocate((slicing_params.base_raft_layers > 0) ? sltRaftBase : sltRaftInterface);
            raft_layers.push_back(&new_layer);
            new_layer.print_z = slicing_params.first_print_layer_height;
            new_layer.height  = slicing_params.first_print_layer_height;
            new_layer.bottom_z = 0.;
            first_layer = union_(std::move(first_layer), base);
            if (inflate_factor_1st_layer < 0) { //means auto
                inflate_factor_1st_layer = scale_(2.);
                new_layer.polygons       = expand(first_layer, inflate_factor_1st_layer - inflate_factor_fine);
            } else
                new_layer.polygons = inflate_factor_1st_layer > inflate_factor_fine ? expand(first_layer, inflate_factor_1st_layer - inflate_factor_fine) : first_layer;
        }
        // Insert the base layers.
        for (size_t i = 1; i < slicing_params.base_raft_layers; ++ i) {
            coordf_t print_z = raft_layers.back()->print_z;
            SupportGeneratorLayer &new_layer  = layer_storage.allocate_unguarded(SupporLayerType::sltRaftBase);
            raft_layers.push_back(&new_layer);
            new_layer.print_z  = print_z + slicing_params.base_raft_layer_height;
            new_layer.height   = slicing_params.base_raft_layer_height;
            new_layer.bottom_z = print_z;
            new_layer.polygons = base;
        }
        // Insert the interface layers.
        for (size_t i = 1; i < slicing_params.interface_raft_layers; ++ i) {
            coordf_t print_z = raft_layers.back()->print_z;
            SupportGeneratorLayer &new_layer = layer_storage.allocate_unguarded(SupporLayerType::sltRaftInterface);
            raft_layers.push_back(&new_layer);
            new_layer.print_z = print_z + slicing_params.interface_raft_layer_height;
            new_layer.height  = slicing_params.interface_raft_layer_height;
            new_layer.bottom_z = print_z;
            new_layer.polygons = interface_polygons;
            //FIXME misusing contact_polygons for support columns.
            new_layer.contact_polygons = std::make_unique<Polygons>(columns);
        }
    } else {
        if (columns_base != nullptr) {
            // Expand the bases of the support columns in the 1st layer.
            Polygons &raft     = columns_base->polygons;
            Polygons  trimming;
            // BBS: if first layer of support is intersected with object island, it must have the same function as brim unless in nobrim mode.
            // brim_object_gap is changed to 0 by default, it's no longer appropriate to use it to determine the gap of first layer support.
            //if (object.has_brim())
            //    trimming = offset(object.layers().front()->lslices, (float)scale_(object.config().brim_object_gap.value), SUPPORT_SURFACES_OFFSET_PARAMETERS);
            //else
            trimming = offset(object.layers().front()->lslices, (float)scale_(support_params.gap_xy_first_layer), SUPPORT_SURFACES_OFFSET_PARAMETERS);
            if (inflate_factor_1st_layer < 0) { //means auto
                //for (const auto &expoly : to_expolygons(raft)) {
                //    //TODO: it's hard to get the exact support height here
                //    inflate_factor_1st_layer = std::max(0.,
                //                                        scale_(expoly.map_moment_to_expansion(max_speed, contacts_with_height.empty() ? 0 : contacts_with_height[0]->print_z)));
                //}
                inflate_factor_1st_layer = scale_(2.);
            }
            if (inflate_factor_1st_layer > inflate_factor_fine + SCALED_EPSILON) {
                inflate_factor_1st_layer = inflate_factor_1st_layer - inflate_factor_fine;
                // Inflate in multiple steps to avoid leaking of the support 1st layer through object walls.
                auto  nsteps = std::max(5, int(ceil(inflate_factor_1st_layer / support_params.first_layer_flow.scaled_width())));
                float step   = inflate_factor_1st_layer / nsteps;
                for (int i = 0; i < nsteps; ++ i)
                    raft = diff(expand(raft, step), trimming);
            } else
                raft = diff(raft, trimming);
            if (! interface_polygons.empty())
                columns_base->polygons = diff(columns_base->polygons, interface_polygons);
        }
        if (! brim.empty()) {
            if (columns_base)
                columns_base->polygons = diff(columns_base->polygons, brim);
            if (contacts)
                contacts->polygons = diff(contacts->polygons, brim);
            if (interfaces)
                interfaces->polygons = diff(interfaces->polygons, brim);
            if (base_interfaces)
                base_interfaces->polygons = diff(base_interfaces->polygons, brim);
        }
    }

    return raft_layers;
}

static inline void fill_expolygon_generate_paths(
    ExtrusionEntitiesPtr    &dst,
    ExPolygon              &&expolygon,
    Fill                    *filler,
    const FillParams        &fill_params,
    float                    density,
    ExtrusionRole            role,
    const Flow              &flow)
{
    Surface surface(stInternal, std::move(expolygon));
    Polylines polylines;
    try {
        assert(!fill_params.use_arachne);
        polylines = filler->fill_surface(&surface, fill_params);
    } catch (InfillFailedException &) {
    }
    extrusion_entities_append_paths(
        dst,
        std::move(polylines),
        role,
        flow.mm3_per_mm(), flow.width(), flow.height());
}

static inline void fill_expolygons_generate_paths(
    ExtrusionEntitiesPtr    &dst,
    ExPolygons             &&expolygons,
    Fill                    *filler,
    const FillParams        &fill_params,
    float                    density,
    ExtrusionRole            role,
    const Flow              &flow)
{
    for (ExPolygon &expoly : expolygons)
        fill_expolygon_generate_paths(dst, std::move(expoly), filler, fill_params, density, role, flow);
}

static inline void fill_expolygons_generate_paths(
    ExtrusionEntitiesPtr    &dst,
    ExPolygons             &&expolygons,
    Fill                    *filler,
    float                    density,
    ExtrusionRole            role,
    const Flow              &flow)
{
    FillParams fill_params;
    fill_params.density     = density;
    fill_params.dont_adjust = true;
    fill_expolygons_generate_paths(dst, std::move(expolygons), filler, fill_params, density, role, flow);
}

static Polylines draw_perimeters(const ExPolygon &expoly, double clip_length)
{
    // Draw the perimeters.
    Polylines polylines;
    polylines.reserve(expoly.holes.size() + 1);
    for (size_t i = 0; i <= expoly.holes.size();  ++ i) {
        Polyline pl(i == 0 ? expoly.contour.points : expoly.holes[i - 1].points);
        pl.points.emplace_back(pl.points.front());
        if (i > 0)
            // It is a hole, reverse it.
            pl.reverse();
        // so that all contours are CCW oriented.
        pl.clip_end(clip_length);
        polylines.emplace_back(std::move(pl));
    }
    return polylines;
}

void tree_supports_generate_paths(
    ExtrusionEntitiesPtr    &dst,
    const Polygons          &polygons,
    const Flow              &flow,
    const SupportParameters &support_params)
{
    // Offset expolygon inside, returns number of expolygons collected (0 or 1).
    // Vertices of output paths are marked with Z = source contour index of the expoly.
    // Vertices at the intersection of source contours are marked with Z = -1.
    auto shrink_expolygon_with_contour_idx = [](const Slic3r::ExPolygon &expoly, const float delta, ClipperLib::JoinType joinType, double miterLimit, ClipperLib_Z::Paths &out) -> int
    {
        assert(delta > 0);
        auto append_paths_with_z = [](ClipperLib::Paths &src, coord_t contour_idx, ClipperLib_Z::Paths &dst) {
            dst.reserve(next_highest_power_of_2(dst.size() + src.size()));
            for (const ClipperLib::Path &contour : src) {
                ClipperLib_Z::Path tmp;
                tmp.reserve(contour.size());
                for (const Point &p : contour)
                    tmp.emplace_back(p.x(), p.y(), contour_idx);
                dst.emplace_back(std::move(tmp));
            }
        };

        // 1) Offset the outer contour.
        ClipperLib_Z::Paths contours;
        {
            ClipperLib::ClipperOffset co;
            if (joinType == jtRound)
                co.ArcTolerance = miterLimit;
            else
                co.MiterLimit = miterLimit;
            co.ShortestEdgeLength = double(delta * 0.005);
            co.AddPath(expoly.contour.points, joinType, ClipperLib::etClosedPolygon);
            ClipperLib::Paths contours_raw;
            co.Execute(contours_raw, - delta);
            if (contours_raw.empty())
                // No need to try to offset the holes.
                return 0;
            append_paths_with_z(contours_raw, 0, contours);
        }

        if (expoly.holes.empty()) {
            // No need to subtract holes from the offsetted expolygon, we are done.
            append(out, std::move(contours));
        } else {
            // 2) Offset the holes one by one, collect the offsetted holes.
            ClipperLib_Z::Paths holes;
            {
                for (const Polygon &hole : expoly.holes) {
                    ClipperLib::ClipperOffset co;
                    if (joinType == jtRound)
                        co.ArcTolerance = miterLimit;
                    else
                        co.MiterLimit = miterLimit;
                    co.ShortestEdgeLength = double(delta * 0.005);
                    co.AddPath(hole.points, joinType, ClipperLib::etClosedPolygon);
                    ClipperLib::Paths out2;
                    // Execute reorients the contours so that the outer most contour has a positive area. Thus the output
                    // contours will be CCW oriented even though the input paths are CW oriented.
                    // Offset is applied after contour reorientation, thus the signum of the offset value is reversed.
                    co.Execute(out2, delta);
                    append_paths_with_z(out2, 1 + (&hole - expoly.holes.data()), holes);
                }
            }

            // 3) Subtract holes from the contours.
            if (holes.empty()) {
                // No hole remaining after an offset. Just copy the outer contour.
                append(out, std::move(contours));
            } else {
                // Negative offset. There is a chance, that the offsetted hole intersects the outer contour.
                // Subtract the offsetted holes from the offsetted contours.
                ClipperLib_Z::Clipper clipper;
                clipper.ZFillFunction([](const ClipperLib_Z::IntPoint &e1bot, const ClipperLib_Z::IntPoint &e1top, const ClipperLib_Z::IntPoint &e2bot, const ClipperLib_Z::IntPoint &e2top, ClipperLib_Z::IntPoint &pt) {
                        //pt.z() = std::max(std::max(e1bot.z(), e1top.z()), std::max(e2bot.z(), e2top.z()));
                        // Just mark the intersection.
                        pt.z() = -1;
                    });
                clipper.AddPaths(contours, ClipperLib_Z::ptSubject, true);
                clipper.AddPaths(holes,    ClipperLib_Z::ptClip,    true);
                ClipperLib_Z::Paths output;
                clipper.Execute(ClipperLib_Z::ctDifference, output, ClipperLib_Z::pftNonZero, ClipperLib_Z::pftNonZero);
                if (! output.empty()) {
                    append(out, std::move(output));
                } else {
                    // The offsetted holes have eaten up the offsetted outer contour.
                    return 0;
                }
            }
        }

        return 1;
    };

    const double spacing = flow.scaled_spacing();
    // Clip the sheath path to avoid the extruder to get exactly on the first point of the loop.
    const double clip_length = spacing * 0.15;
    const double anchor_length = spacing * 6.;
    ClipperLib_Z::Paths anchor_candidates;
    for (ExPolygon& expoly : closing_ex(polygons, float(SCALED_EPSILON), float(SCALED_EPSILON + 0.5 * flow.scaled_width()))) {
        std::unique_ptr<ExtrusionEntityCollection> eec;
        ExPolygons                                 regions_to_draw_inner_wall{expoly};
        double                                     area = expoly.area();
        if (support_params.tree_branch_diameter_double_wall_area_scaled > 0)
            if (area > support_params.tree_branch_diameter_double_wall_area_scaled) {
                BOOST_LOG_TRIVIAL(debug)<< "TreeSupports: double wall area: " << area<< " > " << support_params.tree_branch_diameter_double_wall_area_scaled;
                eec = std::make_unique<ExtrusionEntityCollection>();
                // Don't reorder internal / external loops of the same island.
                eec->no_sort = true;
                // Make the tree branch stable by adding another perimeter.
                ExPolygons level2 = offset2_ex({expoly}, -1.5 * flow.scaled_width(), 0.5 * flow.scaled_width());
                if (level2.size() > 0) {
                    regions_to_draw_inner_wall = level2;
                    extrusion_entities_append_paths(eec->entities, draw_perimeters(expoly, clip_length), ExtrusionRole::erSupportMaterial, flow.mm3_per_mm(), flow.width(), flow.height(),
                            // Disable reversal of the path, always start with the anchor, always print CCW.
                            false);
                    expoly = level2.front();
                }
            }
        for (ExPolygon &expoly : regions_to_draw_inner_wall)
        {
            // Try to produce one more perimeter to place the seam anchor.
            // First genrate a 2nd perimeter loop as a source for anchor candidates.
            // The anchor candidate points are annotated with an index of the source contour or with -1 if on intersection.
            anchor_candidates.clear();
            shrink_expolygon_with_contour_idx(expoly, flow.scaled_width(), DefaultJoinType, 1.2, anchor_candidates);
            // Orient all contours CW.
            for (auto &path : anchor_candidates)
                if (ClipperLib_Z::Area(path) > 0) std::reverse(path.begin(), path.end());

            // Draw the perimeters.
            Polylines polylines;
            polylines.reserve(expoly.holes.size() + 1);
            for (int idx_loop = 0; idx_loop < int(expoly.num_contours()); ++idx_loop) {
                // Open the loop with a seam.
                const Polygon &loop = expoly.contour_or_hole(idx_loop);
                Polyline       pl(loop.points);
                // Orient all contours CW, because the anchor will be added to the end of polyline while we want to start a loop with the anchor.
                if (idx_loop == 0)
                    // It is an outer contour.
                    pl.reverse();
                pl.points.emplace_back(pl.points.front());
                pl.clip_end(clip_length);
                if (pl.size() < 2) continue;
                // Find the foot of the seam point on anchor_candidates. Only pick an anchor point that was created by offsetting the source contour.
                ClipperLib_Z::Path *closest_contour = nullptr;
                Vec2d               closest_point;
                int                 closest_point_idx = -1;
                double              d2min             = std::numeric_limits<double>::max();
                Vec2d               seam_pt           = pl.back().cast<double>();
                for (ClipperLib_Z::Path &path : anchor_candidates)
                    for (int i = 0; i < int(path.size()); ++i) {
                        int j = next_idx_modulo(i, path);
                        if (path[i].z() == idx_loop || path[j].z() == idx_loop) {
                            Vec2d pi(path[i].x(), path[i].y());
                            Vec2d pj(path[j].x(), path[j].y());
                            Vec2d v  = pj - pi;
                            Vec2d w  = seam_pt - pi;
                            auto  l2 = v.squaredNorm();
                            auto  t  = std::clamp((l2 == 0) ? 0 : v.dot(w) / l2, 0., 1.);
                            if ((path[i].z() == idx_loop || t > EPSILON) && (path[j].z() == idx_loop || t < 1. - EPSILON)) {
                                // Closest point.
                                Vec2d  fp = pi + v * t;
                                double d2 = (fp - seam_pt).squaredNorm();
                                if (d2 < d2min) {
                                    d2min             = d2;
                                    closest_contour   = &path;
                                    closest_point     = fp;
                                    closest_point_idx = i;
                                }
                            }
                        }
                    }
                if (d2min < sqr(flow.scaled_width() * 3.) && Slic3r::area(regions_to_draw_inner_wall) + scale_(EPSILON) > area && !shrink_ex({expoly}, -2.*flow.scaled_width()).empty()) {
                    // Try to cut an anchor from the closest_contour.
                    // Both closest_contour and pl are CW oriented.
                    // Walk the CW contour backwards, so once pl is reversed the anchor lies ahead of the seam in
                    // the CCW print direction. A double wall above starts its inner loop at the same seam and runs
                    // CCW too, so it is laid on top of the anchor instead of moving away from it.
                    pl.points.emplace_back(closest_point.cast<coord_t>());
                    const ClipperLib_Z::Path &path             = *closest_contour;
                    const int                 num_points       = int(path.size());
                    double                    remaining_length = std::min(anchor_length - (seam_pt - closest_point).norm(), pl.length() / 12.);
                    // Walk at most once around the contour, stopping where it leaves the source loop.
                    Vec2d p = closest_point;
                    for (int j = closest_point_idx, step = 0; remaining_length > 0 && step < num_points;
                         ++ step, j = prev_idx_modulo(j, num_points)) {
                        const Vec2d  pj(path[j].x(), path[j].y());
                        const double l = (pj - p).norm();
                        if (remaining_length <= l) {
                            pl.points.emplace_back((p + (pj - p) * (remaining_length / l)).cast<coord_t>());
                            break;
                        }
                        if (l > EPSILON)
                            pl.points.emplace_back(path[j].x(), path[j].y());
                        remaining_length -= l;
                        if (path[j].z() != idx_loop)
                            break;
                        p = pj;
                    }
                }
                // Start with the anchor.
                pl.reverse();
                polylines.emplace_back(std::move(pl));
            }

            ExtrusionEntitiesPtr &out = eec ? eec->entities : dst;
            extrusion_entities_append_paths(out, std::move(polylines), ExtrusionRole::erSupportMaterial, flow.mm3_per_mm(), flow.width(), flow.height(),
                                            // Disable reversal of the path, always start with the anchor, always print CCW.
                                            false);
        }
        if (eec) {
            // Loops were appended external first. Print the internal loop first, except in multi nozzle
            // diameter mode, which keeps the external loop first.
            if (! support_params.multi_nozzle_diameter_mode)
                std::reverse(eec->entities.begin(), eec->entities.end());
            dst.emplace_back(eec.release());
        }
    }
}

void fill_expolygons_with_sheath_generate_paths(
    ExtrusionEntitiesPtr    &dst,
    const Polygons          &polygons,
    Fill                    *filler,
    float                    density,
    ExtrusionRole            role,
    const Flow              &flow,
    const SupportParameters& support_params,
    bool                     with_sheath,
    bool                     no_sort)
{
    if (polygons.empty())
        return;

    if (with_sheath) {
        if (density == 0) {
            tree_supports_generate_paths(dst, polygons, flow, support_params);
            return;
        }
    }
    else {
        fill_expolygons_generate_paths(dst, closing_ex(polygons, float(SCALED_EPSILON)), filler, density, role, flow);
        return;
    }

    FillParams fill_params;
    fill_params.density     = density;
    fill_params.dont_adjust = true;

    const double spacing = flow.scaled_spacing();
    // Clip the sheath path to avoid the extruder to get exactly on the first point of the loop.
    const double clip_length = spacing * 0.15;

    for (ExPolygon &expoly : closing_ex(polygons, float(SCALED_EPSILON), float(SCALED_EPSILON + 0.5*flow.scaled_width()))) {
        // Don't reorder the skirt and its infills.
        std::unique_ptr<ExtrusionEntityCollection> eec;
        if (no_sort) {
            eec = std::make_unique<ExtrusionEntityCollection>();
            eec->no_sort = true;
        }
        ExtrusionEntitiesPtr &out = no_sort ? eec->entities : dst;
        extrusion_entities_append_paths(out, draw_perimeters(expoly, clip_length), ExtrusionRole::erSupportMaterial, flow.mm3_per_mm(), flow.width(), flow.height());
        // Fill in the rest.
        fill_expolygons_generate_paths(out, offset_ex(expoly, float(-0.4 * spacing)), filler, fill_params, density, role, flow);
        if (no_sort && ! eec->empty())
            dst.emplace_back(eec.release());
    }
}

// Support layers, partially processed.
struct SupportGeneratorLayerExtruded
{
    SupportGeneratorLayerExtruded& operator=(SupportGeneratorLayerExtruded &&rhs) {
        this->layer = rhs.layer;
        this->extrusions = std::move(rhs.extrusions);
        m_polygons_to_extrude = std::move(rhs.m_polygons_to_extrude);
        rhs.layer = nullptr;
        return *this;
    }

    bool empty() const {
        return layer == nullptr || layer->polygons.empty();
    }

    void set_polygons_to_extrude(Polygons &&polygons) {
        if (m_polygons_to_extrude == nullptr)
            m_polygons_to_extrude = std::make_unique<Polygons>(std::move(polygons));
        else
            *m_polygons_to_extrude = std::move(polygons);
    }
    Polygons& polygons_to_extrude() { return (m_polygons_to_extrude == nullptr) ? layer->polygons : *m_polygons_to_extrude; }
    const Polygons& polygons_to_extrude() const { return (m_polygons_to_extrude == nullptr) ? layer->polygons : *m_polygons_to_extrude; }

    bool could_merge(const SupportGeneratorLayerExtruded &other) const {
        return ! this->empty() && ! other.empty() &&
            std::abs(this->layer->height - other.layer->height) < EPSILON &&
            this->layer->bridging == other.layer->bridging;
    }

    // Merge regions, perform boolean union over the merged polygons.
    void merge(SupportGeneratorLayerExtruded &&other) {
        assert(this->could_merge(other));
        // 1) Merge the rest polygons to extrude, if there are any.
        if (other.m_polygons_to_extrude != nullptr) {
            if (m_polygons_to_extrude == nullptr) {
                // This layer has no extrusions generated yet, if it has no m_polygons_to_extrude (its area to extrude was not reduced yet).
                assert(this->extrusions.empty());
                m_polygons_to_extrude = std::make_unique<Polygons>(this->layer->polygons);
            }
            Slic3r::polygons_append(*m_polygons_to_extrude, std::move(*other.m_polygons_to_extrude));
            *m_polygons_to_extrude = union_safety_offset(*m_polygons_to_extrude);
            other.m_polygons_to_extrude.reset();
        } else if (m_polygons_to_extrude != nullptr) {
            assert(other.m_polygons_to_extrude == nullptr);
            // The other layer has no extrusions generated yet, if it has no m_polygons_to_extrude (its area to extrude was not reduced yet).
            assert(other.extrusions.empty());
            Slic3r::polygons_append(*m_polygons_to_extrude, other.layer->polygons);
            *m_polygons_to_extrude = union_safety_offset(*m_polygons_to_extrude);
        }
        // 2) Merge the extrusions.
        this->extrusions.insert(this->extrusions.end(), other.extrusions.begin(), other.extrusions.end());
        other.extrusions.clear();
        // 3) Merge the infill polygons.
        Slic3r::polygons_append(this->layer->polygons, std::move(other.layer->polygons));
        this->layer->polygons = union_safety_offset(this->layer->polygons);
        other.layer->polygons.clear();
    }

    void polygons_append(Polygons &dst) const {
        if (layer != NULL && ! layer->polygons.empty())
            Slic3r::polygons_append(dst, layer->polygons);
    }

    // The source layer. It carries the height and extrusion type (bridging / non bridging, extrusion height).
    SupportGeneratorLayer  *layer { nullptr };
    // Collect extrusions. They will be exported sorted by the bottom height.
    ExtrusionEntitiesPtr                  extrusions;

private:
    // In case the extrusions are non-empty, m_polygons_to_extrude may contain the rest areas yet to be filled by additional support.
    // This is useful mainly for the loop interfaces, which are generated before the zig-zag infills.
    std::unique_ptr<Polygons>             m_polygons_to_extrude;
};

typedef std::vector<SupportGeneratorLayerExtruded*> SupportGeneratorLayerExtrudedPtrs;

struct LoopInterfaceProcessor
{
    LoopInterfaceProcessor(coordf_t circle_r) :
        n_contact_loops(0),
        circle_radius(circle_r),
        circle_distance(circle_r * 3.)
    {
        // Shape of the top contact area.
        circle.points.reserve(6);
        for (size_t i = 0; i < 6; ++ i) {
            double angle = double(i) * M_PI / 3.;
            circle.points.push_back(Point(circle_radius * cos(angle), circle_radius * sin(angle)));
        }
    }

    // Generate loop contacts at the top_contact_layer,
    // trim the top_contact_layer->polygons with the areas covered by the loops.
    void generate(SupportGeneratorLayerExtruded &top_contact_layer, const Flow &interface_flow_src) const;

    int         n_contact_loops;
    coordf_t    circle_radius;
    coordf_t    circle_distance;
    Polygon     circle;
};

void LoopInterfaceProcessor::generate(SupportGeneratorLayerExtruded &top_contact_layer, const Flow &interface_flow_src) const
{
    if (n_contact_loops == 0 || top_contact_layer.empty())
        return;

    Flow flow = interface_flow_src.with_height(top_contact_layer.layer->height);

    Polygons overhang_polygons;
    if (top_contact_layer.layer->overhang_polygons != nullptr)
        overhang_polygons = std::move(*top_contact_layer.layer->overhang_polygons);

    // Generate the outermost loop.
    // Find centerline of the external loop (or any other kind of extrusions should the loop be skipped)
    ExPolygons top_contact_expolygons = offset_ex(union_ex(top_contact_layer.layer->polygons), - 0.5f * flow.scaled_width());

    // Grid size and bit shifts for quick and exact to/from grid coordinates manipulation.
    coord_t circle_grid_resolution = 1;
    coord_t circle_grid_powerof2 = 0;
    {
        // epsilon to account for rounding errors
        coord_t circle_grid_resolution_non_powerof2 = coord_t(2. * circle_distance + 3.);
        while (circle_grid_resolution < circle_grid_resolution_non_powerof2) {
            circle_grid_resolution <<= 1;
            ++ circle_grid_powerof2;
        }
    }

    struct PointAccessor {
        const Point* operator()(const Point &pt) const { return &pt; }
    };
    typedef ClosestPointInRadiusLookup<Point, PointAccessor> ClosestPointLookupType;

    Polygons loops0;
    {
        // find centerline of the external loop of the contours
        // Only consider the loops facing the overhang.
        Polygons external_loops;
        // Holes in the external loops.
        Polygons circles;
        Polygons overhang_with_margin = offset(union_ex(overhang_polygons), 0.5f * flow.scaled_width());
        for (ExPolygons::iterator it_contact_expoly = top_contact_expolygons.begin(); it_contact_expoly != top_contact_expolygons.end(); ++ it_contact_expoly) {
            // Store the circle centers placed for an expolygon into a regular grid, hashed by the circle centers.
            ClosestPointLookupType circle_centers_lookup(coord_t(circle_distance - SCALED_EPSILON));
            Points circle_centers;
            Point  center_last;
            // For each contour of the expolygon, start with the outer contour, continue with the holes.
            for (size_t i_contour = 0; i_contour <= it_contact_expoly->holes.size(); ++ i_contour) {
                Polygon     &contour = (i_contour == 0) ? it_contact_expoly->contour : it_contact_expoly->holes[i_contour - 1];
                const Point *seg_current_pt = nullptr;
                coordf_t     seg_current_t  = 0.;
                if (! intersection_pl(contour.split_at_first_point(), overhang_with_margin).empty()) {
                    // The contour is below the overhang at least to some extent.
                    //FIXME ideally one would place the circles below the overhang only.
                    // Walk around the contour and place circles so their centers are not closer than circle_distance from each other.
                    if (circle_centers.empty()) {
                        // Place the first circle.
                        seg_current_pt = &contour.points.front();
                        seg_current_t  = 0.;
                        center_last    = *seg_current_pt;
                        circle_centers_lookup.insert(center_last);
                        circle_centers.push_back(center_last);
                    }
                    for (Points::const_iterator it = contour.points.begin() + 1; it != contour.points.end(); ++it) {
                        // Is it possible to place a circle on this segment? Is it not too close to any of the circles already placed on this contour?
                        const Point &p1 = *(it-1);
                        const Point &p2 = *it;
                        // Intersection of a ray (p1, p2) with a circle placed at center_last, with radius of circle_distance.
                        const Vec2d v_seg(coordf_t(p2(0)) - coordf_t(p1(0)), coordf_t(p2(1)) - coordf_t(p1(1)));
                        const Vec2d v_cntr(coordf_t(p1(0) - center_last(0)), coordf_t(p1(1) - center_last(1)));
                        coordf_t a = v_seg.squaredNorm();
                        coordf_t b = 2. * v_seg.dot(v_cntr);
                        coordf_t c = v_cntr.squaredNorm() - circle_distance * circle_distance;
                        coordf_t disc = b * b - 4. * a * c;
                        if (disc > 0.) {
                            // The circle intersects a ray. Avoid the parts of the segment inside the circle.
                            coordf_t t1 = (-b - sqrt(disc)) / (2. * a);
                            coordf_t t2 = (-b + sqrt(disc)) / (2. * a);
                            coordf_t t0 = (seg_current_pt == &p1) ? seg_current_t : 0.;
                            // Take the lowest t in <t0, 1.>, excluding <t1, t2>.
                            coordf_t t;
                            if (t0 <= t1)
                                t = t0;
                            else if (t2 <= 1.)
                                t = t2;
                            else {
                                // Try the following segment.
                                seg_current_pt = nullptr;
                                continue;
                            }
                            seg_current_pt = &p1;
                            seg_current_t  = t;
                            center_last    = Point(p1(0) + coord_t(v_seg(0) * t), p1(1) + coord_t(v_seg(1) * t));
                            // It has been verified that the new point is far enough from center_last.
                            // Ensure, that it is far enough from all the centers.
                            std::pair<const Point*, coordf_t> circle_closest = circle_centers_lookup.find(center_last);
                            if (circle_closest.first != nullptr) {
                                -- it;
                                continue;
                            }
                        } else {
                            // All of the segment is outside the circle. Take the first point.
                            seg_current_pt = &p1;
                            seg_current_t  = 0.;
                            center_last    = p1;
                        }
                        // Place the first circle.
                        circle_centers_lookup.insert(center_last);
                        circle_centers.push_back(center_last);
                    }
                    external_loops.push_back(std::move(contour));
                    for (const Point &center : circle_centers) {
                        circles.push_back(circle);
                        circles.back().translate(center);
                    }
                }
            }
        }
        // Apply a pattern to the external loops.
        loops0 = diff(external_loops, circles);
    }

    Polylines loop_lines;
    {
        // make more loops
        Polygons loop_polygons = loops0;
        for (int i = 1; i < n_contact_loops; ++ i)
            polygons_append(loop_polygons,
                opening(
                    loops0,
                    i * flow.scaled_spacing() + 0.5f * flow.scaled_spacing(),
                    0.5f * flow.scaled_spacing()));
        // Clip such loops to the side oriented towards the object.
        // Collect split points, so they will be recognized after the clipping.
        // At the split points the clipped pieces will be stitched back together.
        loop_lines.reserve(loop_polygons.size());
        std::unordered_map<Point, int, PointHash> map_split_points;
        for (Polygons::const_iterator it = loop_polygons.begin(); it != loop_polygons.end(); ++ it) {
            assert(map_split_points.find(it->first_point()) == map_split_points.end());
            map_split_points[it->first_point()] = -1;
            loop_lines.push_back(it->split_at_first_point());
        }
        loop_lines = intersection_pl(loop_lines, expand(overhang_polygons, scale_(SUPPORT_MATERIAL_MARGIN)));
        // Because a closed loop has been split to a line, loop_lines may contain continuous segments split to 2 pieces.
        // Try to connect them.
        for (int i_line = 0; i_line < int(loop_lines.size()); ++ i_line) {
            Polyline &polyline = loop_lines[i_line];
            auto it = map_split_points.find(polyline.first_point());
            if (it != map_split_points.end()) {
                // This is a stitching point.
                // If this assert triggers, multiple source polygons likely intersected at this point.
                assert(it->second != -2);
                if (it->second < 0) {
                    // First occurence.
                    it->second = i_line;
                } else {
                    // Second occurence. Join the lines.
                    Polyline &polyline_1st = loop_lines[it->second];
                    assert(polyline_1st.first_point() == it->first || polyline_1st.last_point() == it->first);
                    if (polyline_1st.first_point() == it->first)
                        polyline_1st.reverse();
                    polyline_1st.append(std::move(polyline));
                    it->second = -2;
                }
                continue;
            }
            it = map_split_points.find(polyline.last_point());
            if (it != map_split_points.end()) {
                // This is a stitching point.
                // If this assert triggers, multiple source polygons likely intersected at this point.
                assert(it->second != -2);
                if (it->second < 0) {
                    // First occurence.
                    it->second = i_line;
                } else {
                    // Second occurence. Join the lines.
                    Polyline &polyline_1st = loop_lines[it->second];
                    assert(polyline_1st.first_point() == it->first || polyline_1st.last_point() == it->first);
                    if (polyline_1st.first_point() == it->first)
                        polyline_1st.reverse();
                    polyline.reverse();
                    polyline_1st.append(std::move(polyline));
                    it->second = -2;
                }
            }
        }
        // Remove empty lines.
        remove_degenerate(loop_lines);
    }

    // add the contact infill area to the interface area
    // note that growing loops by $circle_radius ensures no tiny
    // extrusions are left inside the circles; however it creates
    // a very large gap between loops and contact_infill_polygons, so maybe another
    // solution should be found to achieve both goals
    // Store the trimmed polygons into a separate polygon set, so the original infill area remains intact for
    // "modulate by layer thickness".
    top_contact_layer.set_polygons_to_extrude(diff(top_contact_layer.layer->polygons, offset(loop_lines, float(circle_radius * 1.1))));

    // Transform loops into ExtrusionPath objects.
    extrusion_entities_append_paths(
        top_contact_layer.extrusions,
        std::move(loop_lines),
        ExtrusionRole::erSupportMaterialInterface, flow.mm3_per_mm(), flow.width(), flow.height());
}

#ifdef SLIC3R_DEBUG
static std::string dbg_index_to_color(int idx)
{
    if (idx < 0)
        return "yellow";
    idx = idx % 3;
    switch (idx) {
        case 0: return "red";
        case 1: return "green";
        default: return "blue";
    }
}
#endif /* SLIC3R_DEBUG */

// When extruding a bottom interface layer over an object, the bottom interface layer is extruded in a thin air, therefore
// it is being extruded with a bridging flow to not shrink excessively (the die swell effect).
// Tiny extrusions are better avoided and it is always better to anchor the thread to an existing support structure if possible.
// Therefore the bottom interface spots are expanded a bit. The expanded regions may overlap with another bottom interface layers,
// leading to over extrusion, where they overlap. The over extrusion is better avoided as it often makes the interface layers
// to stick too firmly to the object.
//
// Modulate thickness (increase bottom_z) of extrusions_in_out generated for this_layer
// if they overlap with overlapping_layers, whose print_z is above this_layer.bottom_z() and below this_layer.print_z.
// 只保留靠近 keep_bbox 的线段（两侧各多留一段）。keep_bbox 需比实际关心的区域外扩 1.5 倍偏移距离，
// 这样截断后再偏移，结果在关心区域内与原来一致。
static void keep_polylines_near_bbox(Polylines &polylines, const BoundingBox &keep_bbox)
{
    auto segment_near = [&keep_bbox](const Point &a, const Point &b) {
        return std::max(a.x(), b.x()) >= keep_bbox.min.x() && std::min(a.x(), b.x()) <= keep_bbox.max.x() &&
               std::max(a.y(), b.y()) >= keep_bbox.min.y() && std::min(a.y(), b.y()) <= keep_bbox.max.y();
    };
    Polylines out;
    out.reserve(polylines.size());
    std::vector<char> keep;
    for (Polyline &polyline : polylines) {
        const Points &points = polyline.points;
        if (points.size() < 2) {
            out.emplace_back(std::move(polyline));
            continue;
        }
        const size_t num_segments = points.size() - 1;
        keep.assign(num_segments, 0);
        bool all_near = true;
        for (size_t i = 0; i < num_segments; ++ i)
            if (segment_near(points[i], points[i + 1])) {
                keep[i] = 1;
                if (i > 0)
                    keep[i - 1] = 1;
                if (i + 1 < num_segments)
                    keep[i + 1] |= 2;
            } else
                all_near = false;
        if (all_near) {
            out.emplace_back(std::move(polyline));
            continue;
        }
        for (size_t i = 0; i < num_segments;) {
            if (! keep[i]) {
                ++ i;
                continue;
            }
            size_t j = i;
            while (j < num_segments && keep[j])
                ++ j;
            out.emplace_back(Points(points.begin() + i, points.begin() + j + 1));
            i = j;
        }
    }
    polylines = std::move(out);
}

static void modulate_extrusion_paths_by_overlapping_layers(
    // Extrusions generated for this_layer.
    ExtrusionEntitiesPtr                               &extrusions_in_out,
    const SupportGeneratorLayer          &this_layer,
    // Multiple layers overlapping with this_layer, sorted bottom up.
    const SupportGeneratorLayersPtr      &overlapping_layers)
{
    size_t n_overlapping_layers = overlapping_layers.size();
    if (n_overlapping_layers == 0 || extrusions_in_out.empty())
        // The extrusions do not overlap with any other extrusion.
        return;

    // Get the initial extrusion parameters.
    ExtrusionPath *extrusion_path_template = dynamic_cast<ExtrusionPath*>(extrusions_in_out.front());
    assert(extrusion_path_template != nullptr);
    ExtrusionRole extrusion_role  = extrusion_path_template->role();
    float         extrusion_width = extrusion_path_template->width;

    struct ExtrusionPathFragment
    {
        ExtrusionPathFragment() : mm3_per_mm(-1), width(-1), height(-1) {};
        ExtrusionPathFragment(double mm3_per_mm, float width, float height) : mm3_per_mm(mm3_per_mm), width(width), height(height) {};

        Polylines       polylines;
        double          mm3_per_mm;
        float           width;
        float           height;
    };

    // Split the extrusions by the overlapping layers, reduce their extrusion rate.
    // The last path_fragment is from this_layer.
    std::vector<ExtrusionPathFragment> path_fragments(
        n_overlapping_layers + 1,
        ExtrusionPathFragment(extrusion_path_template->mm3_per_mm, extrusion_path_template->width, extrusion_path_template->height));
    // Don't use it, it will be released.
    extrusion_path_template = nullptr;

#ifdef SLIC3R_DEBUG
    static int iRun = 0;
    ++ iRun;
    BoundingBox bbox;
    for (size_t i_overlapping_layer = 0; i_overlapping_layer < n_overlapping_layers; ++ i_overlapping_layer) {
        const SupportGeneratorLayer &overlapping_layer = *overlapping_layers[i_overlapping_layer];
        bbox.merge(get_extents(overlapping_layer.polygons));
    }
    for (ExtrusionEntitiesPtr::const_iterator it = extrusions_in_out.begin(); it != extrusions_in_out.end(); ++ it) {
        ExtrusionPath *path = dynamic_cast<ExtrusionPath*>(*it);
        assert(path != nullptr);
        bbox.merge(get_extents(path->polyline));
    }
    SVG svg(debug_out_path("support-fragments-%d-%lf.svg", iRun, this_layer.print_z).c_str(), bbox);
    const float transparency = 0.5f;
    // Filled polygons for the overlapping regions.
    svg.draw(union_ex(this_layer.polygons), dbg_index_to_color(-1), transparency);
    for (size_t i_overlapping_layer = 0; i_overlapping_layer < n_overlapping_layers; ++ i_overlapping_layer) {
        const SupportGeneratorLayer &overlapping_layer = *overlapping_layers[i_overlapping_layer];
        svg.draw(union_ex(overlapping_layer.polygons), dbg_index_to_color(int(i_overlapping_layer)), transparency);
    }
    // Contours of the overlapping regions.
    svg.draw(to_polylines(this_layer.polygons), dbg_index_to_color(-1), scale_(0.2));
    for (size_t i_overlapping_layer = 0; i_overlapping_layer < n_overlapping_layers; ++ i_overlapping_layer) {
        const SupportGeneratorLayer &overlapping_layer = *overlapping_layers[i_overlapping_layer];
        svg.draw(to_polylines(overlapping_layer.polygons), dbg_index_to_color(int(i_overlapping_layer)), scale_(0.1));
    }
    // Fill extrusion, the source.
    for (ExtrusionEntitiesPtr::const_iterator it = extrusions_in_out.begin(); it != extrusions_in_out.end(); ++ it) {
        ExtrusionPath *path = dynamic_cast<ExtrusionPath*>(*it);
        std::string color_name;
        switch ((it - extrusions_in_out.begin()) % 9) {
            case 0: color_name = "magenta"; break;
            case 1: color_name = "deepskyblue"; break;
            case 2: color_name = "coral"; break;
            case 3: color_name = "goldenrod"; break;
            case 4: color_name = "orange"; break;
            case 5: color_name = "olivedrab"; break;
            case 6: color_name = "blueviolet"; break;
            case 7: color_name = "brown"; break;
            default: color_name = "orchid"; break;
        }
        svg.draw(path->polyline, color_name, scale_(0.2));
    }
#endif /* SLIC3R_DEBUG */

    // End points of the original paths.
    std::vector<std::pair<Point, Point>> path_ends;
    // Collect the paths of this_layer.
    {
        Polylines &polylines = path_fragments.back().polylines;
        for (ExtrusionEntity *ee : extrusions_in_out) {
            ExtrusionPath *path = dynamic_cast<ExtrusionPath*>(ee);
            assert(path != nullptr);
            polylines.emplace_back(Polyline(std::move(path->polyline)));
            path_ends.emplace_back(std::pair<Point, Point>(polylines.back().points.front(), polylines.back().points.back()));
            delete path;
        }
    }
    // Destroy the original extrusion paths, their polylines were moved to path_fragments already.
    // This will be the destination for the new paths.
    extrusions_in_out.clear();

    // Fragment the path segments by overlapping layers. The overlapping layers are sorted by an increasing print_z.
    // Trim by the highest overlapping layer first.
    // 重叠层外扩后碰不到路径时跳过裁剪，结果不变。
    BoundingBox paths_bbox;
    for (const Polyline &polyline : path_fragments.back().polylines)
        if (! polyline.points.empty())
            paths_bbox.merge(get_extents(polyline));
    for (int i_overlapping_layer = int(n_overlapping_layers) - 1; i_overlapping_layer >= 0; -- i_overlapping_layer) {
        const SupportGeneratorLayer &overlapping_layer = *overlapping_layers[i_overlapping_layer];
        ExtrusionPathFragment &frag = path_fragments[i_overlapping_layer];
        BoundingBox trimming_bbox = get_extents(overlapping_layer.polygons);
        // miter 尖角最多伸出 DefaultMiterLimit 倍偏移距离
        if (trimming_bbox.defined)
            trimming_bbox.offset(coord_t(DefaultMiterLimit * scale_(0.5 * extrusion_width)) + SCALED_EPSILON);
        if (paths_bbox.defined && trimming_bbox.defined && paths_bbox.overlap(trimming_bbox)) {
            Polygons polygons_trimming = offset(union_ex(overlapping_layer.polygons), float(scale_(0.5*extrusion_width)));
            frag.polylines = intersection_pl(path_fragments.back().polylines, polygons_trimming);
            path_fragments.back().polylines = diff_pl(path_fragments.back().polylines, polygons_trimming);
        }
        // Adjust the extrusion parameters for a reduced layer height and a non-bridging flow (nozzle_dmr = -1, does not matter).
        assert(this_layer.print_z > overlapping_layer.print_z);
        frag.height = float(this_layer.print_z - overlapping_layer.print_z);
        frag.mm3_per_mm = Flow(frag.width, frag.height, -1.f).mm3_per_mm();
#ifdef SLIC3R_DEBUG
        svg.draw(frag.polylines, dbg_index_to_color(i_overlapping_layer), scale_(0.1));
#endif /* SLIC3R_DEBUG */
    }

#ifdef SLIC3R_DEBUG
    svg.draw(path_fragments.back().polylines, dbg_index_to_color(-1), scale_(0.1));
    svg.Close();
#endif /* SLIC3R_DEBUG */

    // Now chain the split segments using hashing and a nearly exact match, maintaining the order of segments.
    // Create a single ExtrusionPath or ExtrusionEntityCollection per source ExtrusionPath.
    // Map of fragment start/end points to a pair of <i_overlapping_layer, i_polyline_in_layer>
    // Because a non-exact matching is used for the end points, a multi-map is used.
    // As the clipper library may reverse the order of some clipped paths, store both ends into the map.
    struct ExtrusionPathFragmentEnd
    {
        ExtrusionPathFragmentEnd(size_t alayer_idx, size_t apolyline_idx, bool ais_start) :
            layer_idx(alayer_idx), polyline_idx(apolyline_idx), is_start(ais_start) {}
        size_t layer_idx;
        size_t polyline_idx;
        bool   is_start;
    };
    class ExtrusionPathFragmentEndPointAccessor {
    public:
        ExtrusionPathFragmentEndPointAccessor(const std::vector<ExtrusionPathFragment> &path_fragments) : m_path_fragments(path_fragments) {}
        // Return an end point of a fragment, or nullptr if the fragment has been consumed already.
        const Point* operator()(const ExtrusionPathFragmentEnd &fragment_end) const {
            const Polyline &polyline = m_path_fragments[fragment_end.layer_idx].polylines[fragment_end.polyline_idx];
            return polyline.points.empty() ? nullptr :
                (fragment_end.is_start ? &polyline.points.front() : &polyline.points.back());
        }
    private:
        ExtrusionPathFragmentEndPointAccessor& operator=(const ExtrusionPathFragmentEndPointAccessor&) {
            return *this;
        }

        const std::vector<ExtrusionPathFragment> &m_path_fragments;
    };
    const coord_t search_radius = 7;
    ClosestPointInRadiusLookup<ExtrusionPathFragmentEnd, ExtrusionPathFragmentEndPointAccessor> map_fragment_starts(
        search_radius, ExtrusionPathFragmentEndPointAccessor(path_fragments));
    for (size_t i_overlapping_layer = 0; i_overlapping_layer <= n_overlapping_layers; ++ i_overlapping_layer) {
        const Polylines &polylines = path_fragments[i_overlapping_layer].polylines;
        for (size_t i_polyline = 0; i_polyline < polylines.size(); ++ i_polyline) {
            // Map a starting point of a polyline to a pair of <layer, polyline>
            if (polylines[i_polyline].points.size() >= 2) {
                map_fragment_starts.insert(ExtrusionPathFragmentEnd(i_overlapping_layer, i_polyline, true));
                map_fragment_starts.insert(ExtrusionPathFragmentEnd(i_overlapping_layer, i_polyline, false));
            }
        }
    }

    // For each source path:
    for (size_t i_path = 0; i_path < path_ends.size(); ++ i_path) {
        const Point &pt_start = path_ends[i_path].first;
        const Point &pt_end   = path_ends[i_path].second;
        Point pt_current = pt_start;
        // Find a chain of fragments with the original / reduced print height.
        ExtrusionMultiPath multipath;
        for (;;) {
            // Find a closest end point to pt_current.
            std::pair<const ExtrusionPathFragmentEnd*, coordf_t> end_and_dist2 = map_fragment_starts.find(pt_current);
            // There may be a bug in Clipper flipping the order of two last points in a fragment?
            // assert(end_and_dist2.first != nullptr);
            assert(end_and_dist2.first == nullptr || end_and_dist2.second < search_radius * search_radius);
            if (end_and_dist2.first == nullptr) {
                // New fragment connecting to pt_current was not found.
                // Verify that the last point found is close to the original end point of the unfragmented path.
                //const double d2 = (pt_end - pt_current).cast<double>.squaredNorm();
                //assert(d2 < coordf_t(search_radius * search_radius));
                // End of the path.
                break;
            }
            const ExtrusionPathFragmentEnd &fragment_end_min = *end_and_dist2.first;
            // Fragment to consume.
            ExtrusionPathFragment &frag = path_fragments[fragment_end_min.layer_idx];
            Polyline              &frag_polyline = frag.polylines[fragment_end_min.polyline_idx];
            // Path to append the fragment to.
            ExtrusionPath         *path = multipath.paths.empty() ? nullptr : &multipath.paths.back();
            if (path != nullptr) {
                // Verify whether the path is compatible with the current fragment.
                assert(this_layer.layer_type == sltBottomContact || path->height != frag.height || path->mm3_per_mm != frag.mm3_per_mm);
                if (path->height != frag.height || path->mm3_per_mm != frag.mm3_per_mm) {
                    path = nullptr;
                }
                // Merging with the previous path. This can only happen if the current layer was reduced by a base layer, which was split into a base and interface layer.
            }
            if (path == nullptr) {
                // Allocate a new path.
                multipath.paths.push_back(ExtrusionPath(extrusion_role, frag.mm3_per_mm, frag.width, frag.height));
                path = &multipath.paths.back();
            }
            // The Clipper library may flip the order of the clipped polylines arbitrarily.
            // Reverse the source polyline, if connecting to the end.
            if (! fragment_end_min.is_start)
                frag_polyline.reverse();
            // Enforce exact overlap of the end points of successive fragments.
            assert(frag_polyline.points.front() == pt_current);
            frag_polyline.points.front() = pt_current;
            // Don't repeat the first point.
            if (! path->polyline.points.empty())
                path->polyline.points.pop_back();
            // Consume the fragment's polyline, remove it from the input fragments, so it will be ignored the next time.
            path->polyline.append(std::move(frag_polyline));
            frag_polyline.points.clear();
            pt_current = path->polyline.points.back();
            if (pt_current == pt_end) {
                // End of the path.
                break;
            }
        }
        if (!multipath.paths.empty()) {
            if (multipath.paths.size() == 1) {
                // This path was not fragmented.
                extrusions_in_out.push_back(new ExtrusionPath(std::move(multipath.paths.front())));
            } else {
                // This path was fragmented. Copy the collection as a whole object, so the order inside the collection will not be changed
                // during the chaining of extrusions_in_out.
                extrusions_in_out.push_back(new ExtrusionMultiPath(std::move(multipath)));
            }
        }
    }
    // If there are any non-consumed fragments, add them separately.
    //FIXME this shall not happen, if the Clipper works as expected and all paths split to fragments could be re-connected.
    for (auto it_fragment = path_fragments.begin(); it_fragment != path_fragments.end(); ++ it_fragment)
        extrusion_entities_append_paths(extrusions_in_out, std::move(it_fragment->polylines), extrusion_role, it_fragment->mm3_per_mm, it_fragment->width, it_fragment->height);
}

static void modulate_extrusion_by_overlapping_layers(
    ExtrusionEntitiesPtr                  &extrusions_in_out,
    const SupportGeneratorLayer           &this_layer,
    const SupportGeneratorLayersPtr       &overlapping_layers)
{
    if (overlapping_layers.empty() || extrusions_in_out.empty())
        return;

    // Tree support double walls are grouped in no-sort collections. Preserve that hierarchy while
    // applying the existing path modulation to each run of paths inside the collection.
    ExtrusionEntitiesPtr modulated;
    ExtrusionEntitiesPtr paths;
    auto flush_paths = [&]() {
        if (paths.empty())
            return;
        modulate_extrusion_paths_by_overlapping_layers(paths, this_layer, overlapping_layers);
        std::move(paths.begin(), paths.end(), std::back_inserter(modulated));
        paths.clear();
    };

    for (ExtrusionEntity *extrusion : extrusions_in_out) {
        if (dynamic_cast<ExtrusionPath *>(extrusion) != nullptr) {
            paths.emplace_back(extrusion);
        } else if (auto *collection = dynamic_cast<ExtrusionEntityCollection *>(extrusion)) {
            flush_paths();
            modulate_extrusion_by_overlapping_layers(collection->entities, this_layer, overlapping_layers);
            if (collection->empty())
                delete collection;
            else
                modulated.emplace_back(collection);
        } else {
            // Support path generators are expected to produce paths or collections of paths here.
            // Keep an unexpected entity intact rather than dereferencing an invalid downcast.
            flush_paths();
            assert(dynamic_cast<ExtrusionMultiPath *>(extrusion) != nullptr);
            modulated.emplace_back(extrusion);
        }
    }
    flush_paths();

    extrusions_in_out.clear();
    extrusions_in_out.swap(modulated);
}

// Support layer that is covered by some form of dense interface.
static constexpr const std::initializer_list<SupporLayerType> support_types_interface{
    SupporLayerType::sltRaftInterface, SupporLayerType::sltBottomContact, SupporLayerType::sltBottomInterface, SupporLayerType::sltTopContact, SupporLayerType::sltTopInterface
};

// BBS multi-diameter: the mergeable layer arrays, in the order merge_support_layers_multi_diameter() indexes them.
enum SupportMergeArray { smaBottomContact = 0, smaTopContact, smaIntermediate, smaInterface, smaBaseInterface, smaCount };
// Everything but the sparse base faces the object and follows the interface rule.
static constexpr const bool support_merge_array_is_interface[smaCount] { true, true, false, true, true };

namespace {

// One print_z carrying support areas, together with the layer of every array that shares that print_z.
// Ordering the levels by print_z reproduces the support layer ids generate_support_layers() will hand out,
// because it installs exactly one support layer per non-empty print_z.
struct SupportMergeLevel
{
    coordf_t print_z { 0. };
    // Below the raft contact / the first object layer, where the adhesion geometry must not be touched.
    bool     frozen { false };
    // A raft-only level has no entry in arrays[], but still installs a support layer.
    bool     has_raft { false };
    // Index into the corresponding array, or -1.
    int      layer_idx[smaCount] { -1, -1, -1, -1, -1 };
};

} // namespace

Polygons merged_band_footprint(const Layer &layer, size_t layer_idx)
{
    Polygons band;
    for (const LayerRegion *layerm : layer.regions())
        for (const AdaptiveMergePatch &patch : layerm->adaptive_merge_patches)
            if (patch.bottom_idx <= layer_idx && layer_idx <= patch.top_idx)
                polygons_append(band, to_polygons(patch.area()));
    return band;
}

void merge_support_layers_multi_diameter(
    const PrintObject                               &object,
    const SupportParameters                         &support_params,
    const SlicingParameters                         &slicing_params,
    const SupportGeneratorLayersPtr                 &raft_layers,
    SupportGeneratorLayersPtr                       &bottom_contacts,
    SupportGeneratorLayersPtr                       &top_contacts,
    SupportGeneratorLayersPtr                       &intermediate_layers,
    SupportGeneratorLayersPtr                       &interface_layers,
    SupportGeneratorLayersPtr                       &base_interface_layers,
    std::map<coordf_t, SupportMergePlan::Candidate> &planned_filaments,
    std::map<coordf_t, SupportMergePlan::Candidate> &planned_interface_filaments,
    std::map<coordf_t, coordf_t>                     &planned_base_heights,
    std::map<coordf_t, coordf_t>                     &planned_interface_heights,
    const std::vector<coordf_t>                     *per_layer_area_print_z,
    std::vector<ExPolygons>                         *cooldown_areas,
    std::vector<ExPolygons>                         *lightning_infill_areas,
    coordf_t                                         top_gap)
{
    const SupportMergePlan plan = make_support_merge_plan(object);
    if (top_gap < 0.)
        top_gap = slicing_params.gap_support_object;

    SupportGeneratorLayersPtr *arrays[smaCount] {
        &bottom_contacts, &top_contacts, &intermediate_layers, &interface_layers, &base_interface_layers };

    // Folding areas together leaves holes in the arrays, and generate_support_layers() cannot cope with those.
    auto compact_arrays = [&arrays]() {
        for (int array_id = 0; array_id < smaCount; ++ array_id) {
            SupportGeneratorLayersPtr &layers = *arrays[array_id];
            layers.erase(std::remove(layers.begin(), layers.end(), nullptr), layers.end());
        }
    };

    // Keep raft and bridging-contact geometry fixed, but let a support first layer join the first band.
    // Its merged extrusion still reaches the bed (or the raft) and retains the first-layer footprint.
    const coordf_t z_frozen_max = slicing_params.raft_contact_top_z + EPSILON;

    // Collect the print_z levels that carry support areas. The raft is only listed so the level indices match
    // the support layer ids; its layers are never merged.
    std::vector<SupportMergeLevel> levels;
    // print_z -> levels 下标，避免逐个扫描；命中多个时取最早创建的，与原线性查找一致。
    std::multimap<coordf_t, size_t> level_index;
    auto level_at = [&levels, &level_index](coordf_t print_z) -> SupportMergeLevel & {
        size_t found = std::numeric_limits<size_t>::max();
        for (auto it = level_index.upper_bound(print_z - EPSILON); it != level_index.end() && it->first < print_z + EPSILON; ++ it)
            if (std::abs(it->first - print_z) < EPSILON)
                found = std::min(found, it->second);
        if (found == std::numeric_limits<size_t>::max()) {
            found = levels.size();
            levels.emplace_back();
            levels.back().print_z = print_z;
            level_index.emplace(print_z, found);
        }
        return levels[found];
    };

    for (const SupportGeneratorLayer *layer : raft_layers)
        if (layer != nullptr && ! layer->polygons.empty()) {
            SupportMergeLevel &level = level_at(layer->print_z);
            level.frozen   = true;
            level.has_raft = true;
        }

    for (int array_id = 0; array_id < smaCount; ++ array_id)
        for (int i = 0; i < int(arrays[array_id]->size()); ++ i) {
            SupportGeneratorLayer *layer = (*arrays[array_id])[i];
            if (layer == nullptr || layer->polygons.empty())
                continue;
            SupportMergeLevel &level = level_at(layer->print_z);
            // A bridging layer hangs at a fixed gap above the object, so print_z - height is where its
            // extrusion starts rather than where the layer starts. Growing it would close that gap.
            level.frozen |= layer->print_z < z_frozen_max || layer->bridging;
            if (level.layer_idx[array_id] >= 0) {
                if (! plan.enabled)
                    continue;
                SupportGeneratorLayer *kept = (*arrays[array_id])[level.layer_idx[array_id]];
                // generate_interface_layers() reuses the organic top interface layer object as the layer it
                // writes into (SupportGeneratorLayer &layer_new = top_interface_layer ? *top_interface_layer
                // : ...), so interface_layers and top_interface_layers end up holding the same pointer and
                // std::merge() lists it twice. Merging a layer into itself ends in rhs.reset() and wipes its
                // polygons, which is the whole interface stack. Drop the duplicate slot instead.
                if (kept == layer) {
                    (*arrays[array_id])[i] = nullptr;
                    continue;
                }
                // Downstream the arrays are looked up by print_z, so two areas of the same kind at the same
                // height cannot be told apart. Fold them together right away.
                kept->merge(std::move(*layer));
                (*arrays[array_id])[i] = nullptr;
                continue;
            }
            level.layer_idx[array_id] = i;
        }

    std::sort(levels.begin(), levels.end(),
        [](const SupportMergeLevel &lhs, const SupportMergeLevel &rhs) { return lhs.print_z < rhs.print_z; });
    if (! plan.enabled)
        for (SupportMergeLevel &level : levels)
            level.frozen = true;

    const coord_t gap_xy_scaled = scaled<coord_t>(support_params.gap_xy);

    auto layer_of = [&arrays](const SupportMergeLevel &level, int array_id) -> SupportGeneratorLayer * {
        const int idx = level.layer_idx[array_id];
        return idx < 0 ? nullptr : (*arrays[array_id])[idx];
    };

    // The object slices grown by the XY gap, per object layer. Every slab asks for the same layers again.
    const ConstLayerPtrsAdaptor object_layers = object.layers();
    std::vector<std::optional<Polygons>> object_keep_out_cache(object_layers.size());
    auto object_keep_out = [&object_layers, &object_keep_out_cache, gap_xy_scaled](size_t object_layer_idx) -> const Polygons & {
        std::optional<Polygons> &keep_out = object_keep_out_cache[object_layer_idx];
        if (! keep_out)
            keep_out = offset(object_layers[object_layer_idx]->lslices, float(gap_xy_scaled));
        return *keep_out;
    };
    std::vector<std::optional<Polygons>> band_keep_out_cache(object_layers.size());
    auto band_keep_out = [&object_layers, &band_keep_out_cache, gap_xy_scaled](size_t object_layer_idx) -> const Polygons & {
        std::optional<Polygons> &keep_out = band_keep_out_cache[object_layer_idx];
        if (! keep_out) {
            const Polygons band = merged_band_footprint(*object_layers[object_layer_idx], object_layer_idx);
            keep_out = band.empty() ? Polygons() : offset(union_(band), float(gap_xy_scaled));
        }
        return *keep_out;
    };
    auto first_object_layer_above = [&object_layers](coordf_t z) {
        return size_t(std::lower_bound(object_layers.begin(), object_layers.end(), z + EPSILON,
            [](const Layer *layer, coordf_t print_z) { return layer->print_z < print_z; }) - object_layers.begin());
    };

    // A support area is only kept clear of the object at its own level. A slab is a single extrusion over
    // its whole Z span, so it covers the XY the object occupies further down as well - a shape widening
    // towards the bed is the canonical case - and has to be cut back to the same clearance there.
    auto object_clearance = [&object_layers, &object_keep_out, &first_object_layer_above](coordf_t bottom_z, coordf_t top_z) {
        Polygons trimming;
        for (size_t idx = first_object_layer_above(bottom_z); idx < object_layers.size(); ++ idx) {
            const Layer *object_layer = object_layers[idx];
            if (object_layer->print_z - object_layer->height > top_z - EPSILON)
                break;
            polygons_append(trimming, object_keep_out(idx));
        }
        return trimming.empty() ? Polygons() : union_(trimming);
    };
    // The XY of the object starting within the top gap above top_z. An area lifted to top_z from below was
    // placed against an object surface higher up; where that surface starts in here, lifting it eats into
    // the gap, down to none when the lift equals the gap. object_clearance() only covers what starts lower.
    auto top_gap_zone = [&object_layers, &object_keep_out, &band_keep_out, &first_object_layer_above, top_gap](coordf_t top_z) {
        Polygons zone;
        if (top_gap <= EPSILON)
            return zone;
        for (size_t idx = first_object_layer_above(top_z); idx < object_layers.size(); ++ idx) {
            const Layer   *object_layer   = object_layers[idx];
            const coordf_t layer_bottom_z = object_layer->print_z - object_layer->height;
            if (layer_bottom_z > top_z + top_gap - EPSILON)
                break;
            if (layer_bottom_z < top_z - EPSILON)
                continue;
            polygons_append(zone, object_keep_out(idx));
            polygons_append(zone, band_keep_out(idx));
        }
        return zone.empty() ? Polygons() : union_(zone);
    };

    // Resolve the top contact stack from the model downwards. A thin level stays untouched when that does not
    // strand an unprintable contact directly below it. Otherwise adjacent contacts and, if necessary, sparse
    // base are folded into a continuous slab printable by the largest available interface nozzle.
    // Base-interface is a support-transition region printed with the support-body extruder, not with the
    // contact extruder. Keep it out of contact slabs so nozzle planning and extrusion roles stay consistent.
    static constexpr int top_interface_arrays[] { smaTopContact, smaInterface };
    auto first_top_interface = [&layer_of](const SupportMergeLevel &level) -> SupportGeneratorLayer * {
        for (int array_id : top_interface_arrays)
            if (SupportGeneratorLayer *layer = layer_of(level, array_id); layer != nullptr && ! layer->polygons.empty())
                return layer;
        return nullptr;
    };
    auto interface_height_range = [&layer_of](const SupportMergeLevel &level) {
        std::pair<coordf_t, coordf_t> range {
            std::numeric_limits<coordf_t>::max(), 0. };
        for (int array_id : top_interface_arrays)
            if (const SupportGeneratorLayer *layer = layer_of(level, array_id);
                layer != nullptr && ! layer->polygons.empty()) {
                range.first  = std::min(range.first, layer->height);
                range.second = std::max(range.second, layer->height);
            }
        return range;
    };
    auto candidate_fits_bottom_contact =
        [&layer_of](const SupportMergeLevel &level,
                    const SupportMergePlan::Candidate &candidate) {
        const SupportGeneratorLayer *bottom_contact =
            layer_of(level, smaBottomContact);
        return bottom_contact == nullptr || bottom_contact->polygons.empty() ||
               candidate.window.contains(bottom_contact->height);
    };
    auto level_bottom_z = [&layer_of](const SupportMergeLevel &level) {
        coordf_t bottom_z = level.print_z;
        bool found = false;
        for (int array_id : top_interface_arrays)
            if (const SupportGeneratorLayer *layer = layer_of(level, array_id);
                layer != nullptr && ! layer->polygons.empty()) {
                bottom_z = std::min(bottom_z, layer->print_z - layer->height);
                found = true;
            }
        if (const SupportGeneratorLayer *base = layer_of(level, smaIntermediate);
            base != nullptr && ! base->polygons.empty()) {
            bottom_z = std::min(bottom_z, base->print_z - base->height);
            found = true;
        }
        return found ? bottom_z : level.print_z;
    };
    // Keep interface candidate validation in one place. Interface and sparse base may use different layer
    // ladders; their overlap is resolved spatially while the sparse base slabs are built below.
    auto interface_candidate = [&plan, &candidate_fits_bottom_contact](
                                   const SupportMergeLevel &level, coordf_t top_z, coordf_t height) {
        SupportMergePlan::Candidate candidate = plan.finest_interface_for_slab(top_z, height);
        if (candidate.valid() && ! candidate_fits_bottom_contact(level, candidate))
            candidate = {};
        return candidate;
    };
    std::vector<Polygons> interface_borrowed_areas(levels.size());

    // Returns false when the slab was left alone, in which case the areas keep their own levels.
    auto merge_interface_slab =
        [&levels, &arrays, &layer_of, &first_top_interface, &level_bottom_z, &object_clearance, &top_gap_zone,
         &interface_borrowed_areas, &planned_interface_filaments,
         &planned_interface_heights](int owner_idx, int bottom_idx,
                                     const SupportMergePlan::Candidate &candidate) {
        SupportGeneratorLayer *interface_top = first_top_interface(levels[owner_idx]);
        if (interface_top == nullptr)
            return false;

        const coordf_t top_z       = levels[owner_idx].print_z;
        const coordf_t bottom_z    = level_bottom_z(levels[bottom_idx]);
        const coordf_t slab_height = top_z - bottom_z;
        // What a lower level holds under an object surface starting within the gap above top_z stays at its
        // own level: lifted, it would print into that gap.
        const Polygons gap_zone = top_gap_zone(top_z);
        // Refuse a slab the object leaves no room for at all rather than merge the areas away: printing
        // them at their own level is unprintably thin, but dropping them removes the support entirely.
        const Polygons clearance = object_clearance(bottom_z, top_z);
        if (! clearance.empty()) {
            Polygons slab;
            for (int i = owner_idx; i >= bottom_idx; -- i)
                for (int array_id : top_interface_arrays)
                    if (const SupportGeneratorLayer *source = layer_of(levels[i], array_id); source != nullptr)
                        polygons_append(slab, i < owner_idx && ! gap_zone.empty() ? diff(source->polygons, gap_zone) : source->polygons);
            if (diff(union_(slab), clearance).empty())
                return false;
        }

        std::vector<std::pair<int, int>> kept_sources;
        for (int i = owner_idx; i >= bottom_idx; -- i)
            for (int array_id : top_interface_arrays) {
                SupportGeneratorLayer *source = layer_of(levels[i], array_id);
                if (source == nullptr || source == interface_top)
                    continue;
                if (i < owner_idx && ! gap_zone.empty()) {
                    if (Polygons kept = intersection(source->polygons, gap_zone); ! kept.empty()) {
                        interface_top->polygons = union_(interface_top->polygons, diff(source->polygons, gap_zone));
                        source->polygons = std::move(kept);
                        kept_sources.emplace_back(i, array_id);
                        continue;
                    }
                }
                interface_top->merge(std::move(*source));
                (*arrays[array_id])[levels[i].layer_idx[array_id]] = nullptr;
                levels[i].layer_idx[array_id] = -1;
            }

        interface_top->print_z  = top_z;
        interface_top->height   = slab_height;
        interface_top->bottom_z = bottom_z;
        if (! clearance.empty())
            interface_top->polygons = diff(interface_top->polygons, clearance);

        const Polygons borrowed_area = union_(interface_top->polygons);
        // The slab now spans the levels its kept areas stay on, so they must not overlap it.
        for (const auto &[i, array_id] : kept_sources) {
            SupportGeneratorLayer *source = layer_of(levels[i], array_id);
            source->polygons = diff(source->polygons, borrowed_area);
            if (source->polygons.empty()) {
                (*arrays[array_id])[levels[i].layer_idx[array_id]] = nullptr;
                levels[i].layer_idx[array_id] = -1;
            }
        }
        for (int i = bottom_idx; i <= owner_idx; ++ i) {
            polygons_append(interface_borrowed_areas[size_t(i)], borrowed_area);
            if (SupportGeneratorLayer *base = layer_of(levels[i], smaIntermediate);
                base != nullptr && ! base->polygons.empty())
                base->polygons = diff(base->polygons, borrowed_area);

            auto filament_it = planned_interface_filaments.lower_bound(
                levels[i].print_z - EPSILON);
            if (filament_it != planned_interface_filaments.end() &&
                filament_it->first <= levels[i].print_z + EPSILON)
                planned_interface_filaments.erase(filament_it);
            auto height_it = planned_interface_heights.lower_bound(
                levels[i].print_z - EPSILON);
            if (height_it != planned_interface_heights.end() &&
                height_it->first <= levels[i].print_z + EPSILON)
                planned_interface_heights.erase(height_it);
        }

        planned_interface_filaments[top_z] = candidate;
        planned_interface_heights[top_z]   = slab_height;
        return true;
    };

    for (int top = int(levels.size()) - 1; top >= 0; -- top) {
        SupportGeneratorLayer *interface_top = first_top_interface(levels[top]);
        if (interface_top == nullptr || levels[top].frozen)
            continue;

        const coordf_t top_z = levels[top].print_z;
        const std::pair<coordf_t, coordf_t> height_range = interface_height_range(levels[top]);
        SupportMergePlan::Candidate candidate =
            interface_candidate(levels[top], top_z, height_range.first);
        if (candidate.valid() && ! candidate.window.contains(height_range.second))
            candidate = {};
        if (candidate.valid()) {
            planned_interface_filaments[top_z] = candidate;
            continue;
        }

        int      bottom      = top;
        coordf_t bottom_z    = level_bottom_z(levels[top]);
        coordf_t slab_height = top_z - bottom_z;
        candidate = interface_candidate(levels[top], top_z, slab_height);
        while (! candidate.valid() && bottom > 0) {
            const SupportMergeLevel &below = levels[bottom - 1];
            if (below.frozen || below.print_z < bottom_z - EPSILON)
                break;
            const bool has_interface = first_top_interface(below) != nullptr;
            const SupportGeneratorLayer *base = layer_of(below, smaIntermediate);
            if (! has_interface && (base == nullptr || base->polygons.empty()))
                break;
            // Stop once the slab outgrows the tallest band any interface nozzle ends here with, instead of
            // walking the whole stack down for a slab that can no longer match.
            const coordf_t grown_height = top_z - level_bottom_z(levels[bottom - 1]);
            if (grown_height > plan.max_interface_height(top_z, grown_height) + EPSILON)
                break;
            -- bottom;
            bottom_z    = level_bottom_z(levels[bottom]);
            slab_height = top_z - bottom_z;
            candidate   = interface_candidate(levels[top], top_z, slab_height);
        }
        if (candidate.valid() && merge_interface_slab(top, bottom, candidate)) {
            // Resume at the highest level still holding an area kept out of the top gap, so it gets planned
            // at its own height or merged with what lies below it.
            int resume = bottom;
            for (int i = top - 1; i >= bottom; -- i)
                if (first_top_interface(levels[i]) != nullptr) {
                    resume = i + 1;
                    break;
                }
            top = resume;
            continue;
        }

        // A printable upper contact may still absorb this thin level. This closes a gap such as a printable
        // 0.2 mm layer above an unprintable 0.1 mm layer by turning both into one continuous 0.3 mm slab.
        int upper = top + 1;
        while (upper < int(levels.size()) && first_top_interface(levels[upper]) == nullptr)
            ++ upper;
        if (upper < int(levels.size()) && ! levels[upper].frozen) {
            const SupportGeneratorLayer *upper_interface = first_top_interface(levels[upper]);
            const coordf_t upper_bottom_z =
                upper_interface->print_z - upper_interface->height;
            if (upper_bottom_z <= top_z + EPSILON) {
                const coordf_t combined_height =
                    levels[upper].print_z - level_bottom_z(levels[top]);
                candidate = interface_candidate(
                    levels[upper], levels[upper].print_z, combined_height);
                if (candidate.valid())
                    merge_interface_slab(upper, top, candidate);
            }
        }
    }

    // Only the sparse base is merged. An interface area is extruded straight against the object, so it keeps
    // its own print_z and its own height and is cut out of the slab growing past it. Base and interface can
    // differ in height at the same print_z because generate_support_toolpaths() builds their flows from the
    // height of their own SupportGeneratorLayer, not from the height of the installed SupportLayer.
    //
    // slab_top_of_level[i] is the level that swallowed level i's base, so the per support layer areas of a
    // level the merge emptied can follow it.
    std::vector<int> slab_top_of_level(levels.size());
    for (size_t i = 0; i < levels.size(); ++ i)
        slab_top_of_level[i] = int(i);

    for (int top = int(levels.size()) - 1; top >= 0; -- top) {
        SupportGeneratorLayer *base_top = layer_of(levels[top], smaIntermediate);
        if (base_top == nullptr || base_top->polygons.empty() || levels[top].frozen)
            continue;

        const coordf_t              slab_top_z  = levels[top].print_z;
        int                         bottom      = top;
        coordf_t                    slab_height = base_top->height;
        SupportMergePlan::Candidate pinned      = plan.coarsest_for_slab(slab_top_z, slab_height);
        int                         probe       = top;
        coordf_t                    probe_height = slab_height;

        // Probe down to the next fixed-grid boundary. Intermediate heights may be unprintable by every
        // candidate (for example 0.2 and 0.3 on a 0.8 / 0.1 ladder) but still lead to its 0.4 band.
        while (probe > 0 && ! levels[probe - 1].frozen) {
            const SupportGeneratorLayer *base_below = layer_of(levels[probe - 1], smaIntermediate);
            if (base_below == nullptr || base_below->polygons.empty())
                break;
            // levels only holds the heights that carry support areas, so the level below may sit under a Z
            // gap. The slab is a single extrusion and would hang over that void, so it stops here instead.
            if (base_below->print_z < slab_top_z - probe_height - EPSILON)
                break;
            const coordf_t height = slab_top_z - (base_below->print_z - base_below->height);
            if (height > plan.max_height_for_slab(slab_top_z, height) + EPSILON)
                break;
            const SupportMergePlan::Candidate candidate = plan.coarsest_for_slab(slab_top_z, height);
            -- probe;
            probe_height = height;
            if (candidate.valid()) {
                bottom      = probe;
                slab_height = height;
                pinned      = candidate;
            }
        }

        if (bottom < top) {
            const bool includes_bed = level_bottom_z(levels[bottom]) <= EPSILON;
            const SupportGeneratorLayer *representative =
                layer_of(levels[includes_bed ? bottom : top], smaIntermediate);
            assert(representative != nullptr);
            // A merged extrusion cannot follow every fine-layer contour through Z. Keep the band face used
            // by the model ladder instead: top for an ordinary band, bottom for the first bed-adhesion band.
            // Unioning all contours creates a swept wedge when a tree branch moves laterally between layers.
            Polygons representative_polygons = representative->polygons;

            // Interface extrusions may be thinner than this sparse-base slab while sharing its print_z.
            // Removing their footprint from the complete slab would open a column down to slab_bottom_z.
            // Keep the original fine base layers in that footprint instead, and merge only the remainder.
            Polygons interface_columns;
            for (int i = bottom; i <= top; ++ i)
                for (int array_id = 0; array_id < smaCount; ++ array_id)
                    if (support_merge_array_is_interface[array_id])
                        if (const SupportGeneratorLayer *layer = layer_of(levels[i], array_id);
                            layer != nullptr && ! layer->polygons.empty())
                            polygons_append(interface_columns, layer->polygons);
            interface_columns  = union_(interface_columns);
            if (! interface_columns.empty())
                representative_polygons = diff(representative_polygons, interface_columns);

            for (int i = top - 1; i >= bottom; -- i) {
                const int idx = levels[i].layer_idx[smaIntermediate];
                SupportGeneratorLayer *base = (*arrays[smaIntermediate])[idx];
                Polygons landing_columns;
                for (int j = bottom; j <= top; ++ j)
                    for (int array_id = 0; array_id < smaCount; ++ array_id)
                        if (support_merge_array_is_interface[array_id])
                            if (const SupportGeneratorLayer *layer = layer_of(levels[j], array_id);
                                layer != nullptr && ! layer->polygons.empty() &&
                                layer->print_z - layer->height >= base->print_z - EPSILON)
                                polygons_append(landing_columns, layer->polygons);
                Polygons preserved = landing_columns.empty() ?
                    Polygons() : intersection(base->polygons, union_(landing_columns));
                if (preserved.empty()) {
                    (*arrays[smaIntermediate])[idx]      = nullptr;
                    levels[i].layer_idx[smaIntermediate] = -1;
                    slab_top_of_level[i]                 = top;
                } else {
                    base->polygons = std::move(preserved);
                }
            }
            base_top->polygons = std::move(representative_polygons);
            // Levels holding a bridging layer are frozen, so a slab never grows over one of them.
            assert(! base_top->bridging);
            base_top->print_z  = slab_top_z;
            base_top->height   = slab_height;
            base_top->bottom_z = slab_top_z - slab_height;

            // The merged part of the base fills the whole slab. The transition columns were already removed
            // above, while the object still has to be kept clear over the complete slab height.
            Polygons trimming = object_clearance(base_top->bottom_z, slab_top_z);
            if (! trimming.empty() && ! base_top->polygons.empty())
                base_top->polygons = diff(base_top->polygons, union_(trimming));
            if (! base_top->polygons.empty())
                planned_base_heights[slab_top_z] = slab_height;
        }

        // Pin the slab to its filament only once it got thicker than the object layers: a slab of base height
        // can be extruded by any nozzle, and leaving it alone lets the exporter minimize tool changes.
        if (pinned.valid() && slab_height > plan.base_height_at(slab_top_z) + EPSILON)
            planned_filaments[slab_top_z] = pinned;

        // The for loop's decrement then moves below the slab.
        top = bottom;
    }

    // A merged base slab is one extrusion, so cutting an interface column out of it drops the support under
    // that column over the slab's whole Z span. Grow the interface down to where support actually resumes
    // rather than leave it hanging above the void the cut created.
    static constexpr int rescued_arrays[] { smaTopContact, smaInterface, smaBaseInterface };
    for (int i = int(levels.size()) - 1; i >= 0; -- i) {
        if (levels[i].frozen)
            continue;
        for (int rescued_id : rescued_arrays) {
            SupportGeneratorLayer *interface_top = layer_of(levels[i], rescued_id);
            if (interface_top == nullptr || interface_top->polygons.empty())
                continue;

            // A support transition area is extruded with the support body filament, not with the contact
            // filament, so its reach and its nozzle come from the base plan.
            const bool     with_base_filament = rescued_id == smaBaseInterface;
            const coordf_t bottom_z = interface_top->print_z - interface_top->height;
            // No filament could extrude a taller slab, so nothing below that reach can be a landing.
            const coordf_t max_reach = with_base_filament ?
                plan.max_height_for_slab(interface_top->print_z, interface_top->height) :
                plan.max_interface_height(interface_top->print_z, interface_top->height);
            if (max_reach <= interface_top->height + EPSILON)
                continue;

            // Land on the highest support surface the column reaches, so the grown slab never runs into
            // material another column of the same interface still rests on.
            coordf_t landing_z = 0.;
            bool     landed    = false;
            bool     blocked   = false;
            for (int j = i - 1; j >= 0 && ! blocked; -- j) {
                if (interface_top->print_z - levels[j].print_z > max_reach + EPSILON)
                    break;
                for (int array_id = 0; array_id < smaCount; ++ array_id) {
                    const SupportGeneratorLayer *below = layer_of(levels[j], array_id);
                    if (below == nullptr || below->polygons.empty())
                        continue;
                    if (intersection(below->polygons, interface_top->polygons).empty())
                        continue;
                    if (below->print_z > bottom_z + EPSILON) {
                        // Support already reaches into the range the slab would grow through.
                        blocked = true;
                        break;
                    }
                    landing_z = std::max(landing_z, below->print_z);
                    landed    = true;
                }
            }
            if (blocked || ! landed || landing_z > bottom_z - EPSILON)
                continue;

            // The base slab merge keeps its distance to the object, and so must an interface growing down.
            bool hits_object = false;
            for (size_t idx = first_object_layer_above(landing_z); idx < object_layers.size() && ! hits_object; ++ idx) {
                const Layer *object_layer = object_layers[idx];
                if (object_layer->print_z - object_layer->height > bottom_z - EPSILON)
                    break;
                hits_object = ! intersection(object_keep_out(idx), interface_top->polygons).empty();
            }
            if (hits_object)
                continue;

            const coordf_t              height = interface_top->print_z - landing_z;
            SupportMergePlan::Candidate candidate;
            if (with_base_filament) {
                // When the base slab merge already pinned this height, that is the filament which will
                // extrude the transition area. Otherwise pick one, which also pins the sparse base of this
                // level and therefore has to be able to print it at its own height.
                auto pin = planned_filaments.lower_bound(interface_top->print_z - EPSILON);
                if (pin != planned_filaments.end() && pin->first <= interface_top->print_z + EPSILON) {
                    if (pin->second.window.contains(height))
                        candidate = pin->second;
                } else {
                    candidate = plan.coarsest_for_slab(interface_top->print_z, height);
                    if (candidate.valid())
                        if (const SupportGeneratorLayer *base = layer_of(levels[i], smaIntermediate);
                            base != nullptr && ! base->polygons.empty() && ! candidate.window.contains(base->height))
                            candidate = {};
                }
            } else {
                candidate = plan.finest_interface_for_slab(interface_top->print_z, height);
                if (candidate.valid() && ! candidate_fits_bottom_contact(levels[i], candidate))
                    candidate = {};
            }
            if (! candidate.valid()) {
                BOOST_LOG_TRIVIAL(warning)
                    << "Support interface at z=" << interface_top->print_z << " stays "
                    << (bottom_z - landing_z) << "mm above the support below it: no filament can extrude a "
                    << height << "mm slab there";
                continue;
            }
            interface_top->height   = height;
            interface_top->bottom_z = landing_z;
            if (with_base_filament) {
                planned_filaments[interface_top->print_z] = candidate;
                continue;
            }
            planned_interface_filaments[interface_top->print_z] = candidate;
            coordf_t &planned_height = planned_interface_heights[interface_top->print_z];
            planned_height = std::max(planned_height, height);
        }
    }

    // Rebuild the areas generate_support_toolpaths() indexes by support layer id: a level the merge left
    // without any area no longer gets a support layer, so everything below it shifts down by one.
    auto level_has_areas = [&layer_of](const SupportMergeLevel &level) {
        if (level.has_raft)
            return true;
        for (int array_id = 0; array_id < smaCount; ++ array_id)
            if (const SupportGeneratorLayer *layer = layer_of(level, array_id); layer != nullptr && ! layer->polygons.empty())
                return true;
        return false;
    };
    std::vector<int> support_layer_id_of_level(levels.size(), -1);
    int              num_support_layers = 0;
    for (size_t i = 0; i < levels.size(); ++ i)
        if (level_has_areas(levels[i]))
            support_layer_id_of_level[i] = num_support_layers ++;
    // A level the merge emptied hands its areas over to the slab that swallowed its base.
    for (size_t i = 0; i < levels.size(); ++ i)
        if (support_layer_id_of_level[i] < 0)
            support_layer_id_of_level[i] = support_layer_id_of_level[size_t(slab_top_of_level[i])];

    auto remap_per_layer_areas = [&levels, &slab_top_of_level, &support_layer_id_of_level,
                                  &interface_borrowed_areas, num_support_layers,
                                  per_layer_area_print_z](std::vector<ExPolygons> *areas) {
        if (areas == nullptr || areas->empty())
            return;
        std::vector<ExPolygons> merged(static_cast<size_t>(num_support_layers));
        for (size_t i = 0; i < areas->size(); ++ i) {
            if ((*areas)[i].empty())
                continue;
            size_t source_level = i;
            if (per_layer_area_print_z != nullptr) {
                if (i >= per_layer_area_print_z->size())
                    continue;
                const coordf_t print_z = (*per_layer_area_print_z)[i];
                auto level_it = std::lower_bound(levels.begin(), levels.end(), print_z - EPSILON,
                    [](const SupportMergeLevel &level, coordf_t z) { return level.print_z < z; });
                if (level_it == levels.end() || level_it->print_z > print_z + EPSILON)
                    continue;
                source_level = size_t(level_it - levels.begin());
            }
            if (source_level >= slab_top_of_level.size())
                continue;
            ExPolygons remaining = interface_borrowed_areas[source_level].empty() ?
                std::move((*areas)[i]) :
                diff_ex((*areas)[i], union_(interface_borrowed_areas[source_level]));
            if (remaining.empty())
                continue;
            const size_t target_level = size_t(slab_top_of_level[source_level]);
            if (target_level >= support_layer_id_of_level.size())
                continue;
            if (const int id = support_layer_id_of_level[target_level]; id >= 0)
                append(merged[size_t(id)], std::move(remaining));
        }
        for (ExPolygons &expolygons : merged)
            if (expolygons.size() > 1)
                expolygons = union_ex(expolygons);
        *areas = std::move(merged);
    };
    remap_per_layer_areas(cooldown_areas);
    remap_per_layer_areas(lightning_infill_areas);

    compact_arrays();
}

SupportGeneratorLayersPtr generate_support_layers(
    PrintObject                         &object,
    const SupportGeneratorLayersPtr     &raft_layers,
    const SupportGeneratorLayersPtr     &bottom_contacts,
    const SupportGeneratorLayersPtr     &top_contacts,
    const SupportGeneratorLayersPtr     &intermediate_layers,
    const SupportGeneratorLayersPtr     &interface_layers,
    const SupportGeneratorLayersPtr     &base_interface_layers)
{
    // Install support layers into the object.
    // A support layer installed on a PrintObject has a unique print_z.
    SupportGeneratorLayersPtr layers_sorted;
    layers_sorted.reserve(raft_layers.size() + bottom_contacts.size() + top_contacts.size() + intermediate_layers.size() + interface_layers.size() + base_interface_layers.size());
    append(layers_sorted, raft_layers);
    append(layers_sorted, bottom_contacts);
    append(layers_sorted, top_contacts);
    append(layers_sorted, intermediate_layers);
    append(layers_sorted, interface_layers);
    append(layers_sorted, base_interface_layers);
    // remove dupliated layers
    std::sort(layers_sorted.begin(), layers_sorted.end());
    layers_sorted.erase(std::unique(layers_sorted.begin(), layers_sorted.end()), layers_sorted.end());

    // Sort the layers lexicographically by a raising print_z and a decreasing height.
    std::sort(layers_sorted.begin(), layers_sorted.end(), [](auto *l1, auto *l2) { return *l1 < *l2; });
    int layer_id = 0;
    int layer_id_interface = 0;
    assert(object.support_layers().empty());
    for (size_t i = 0; i < layers_sorted.size();) {
        // Find the last layer with roughly the same print_z, find the minimum layer height of all.
        // Due to the floating point inaccuracies, the print_z may not be the same even if in theory they should.
        size_t j = i + 1;
        coordf_t zmax = layers_sorted[i]->print_z + EPSILON;
        for (; j < layers_sorted.size() && layers_sorted[j]->print_z <= zmax; ++j) ;
        // Assign an average print_z to the set of layers with nearly equal print_z.
        coordf_t zavg = 0.5 * (layers_sorted[i]->print_z + layers_sorted[j - 1]->print_z);
        coordf_t height_min = layers_sorted[i]->height;
        bool     empty = true;
        // For snug supports, layers where the direction of the support interface shall change are accounted for.
        size_t   num_interfaces = 0;
        size_t   num_top_contacts = 0;
        double   top_contact_bottom_z = 0;
        for (size_t u = i; u < j; ++u) {
            SupportGeneratorLayer &layer = *layers_sorted[u];
            if (! layer.polygons.empty()) {
                empty             = false;
                num_interfaces   += one_of(layer.layer_type, support_types_interface);
                if (layer.layer_type == SupporLayerType::sltTopContact) {
                    ++ num_top_contacts;
                    assert(num_top_contacts <= 1);
                    // All top contact layers sharing this print_z shall also share bottom_z.
                    //assert(num_top_contacts == 1 || (top_contact_bottom_z - layer.bottom_z) < EPSILON);
                    top_contact_bottom_z = layer.bottom_z;
                }
            }
            layer.print_z = zavg;
            height_min = std::min(height_min, layer.height);
        }
        if (! empty) {
            // Here the upper_layer and lower_layer pointers are left to null at the support layers,
            // as they are never used. These pointers are candidates for removal.
            bool   this_layer_contacts_only = num_top_contacts > 0 && num_top_contacts == num_interfaces;
            size_t this_layer_id_interface  = layer_id_interface;
            if (this_layer_contacts_only) {
                // Find a supporting layer for its interface ID.
                for (auto it = object.support_layers().rbegin(); it != object.support_layers().rend(); ++ it)
                    if (const SupportLayer &other_layer = **it; std::abs(other_layer.print_z - top_contact_bottom_z) < EPSILON) {
                        // other_layer supports this top contact layer. Assign a different support interface direction to this layer
                        // from the layer that supports it.
                        this_layer_id_interface = other_layer.interface_id() + 1;
                    }
            }
            object.add_support_layer(layer_id ++, this_layer_id_interface, height_min, zavg);
            if (num_interfaces && ! this_layer_contacts_only)
                ++ layer_id_interface;
        }
        i = j;
    }
    return layers_sorted;
}

void generate_support_toolpaths(
    SupportLayerPtrs                    &support_layers,
    const PrintObjectConfig             &config,
    const SupportParameters             &support_params,
    const SlicingParameters             &slicing_params,
    const SupportGeneratorLayersPtr     &raft_layers,
    const SupportGeneratorLayersPtr     &bottom_contacts,
    const SupportGeneratorLayersPtr     &top_contacts,
    const SupportGeneratorLayersPtr     &intermediate_layers,
    const SupportGeneratorLayersPtr     &interface_layers,
    const SupportGeneratorLayersPtr     &base_interface_layers,
    const std::vector<ExPolygons>       &cooldown_areas,
    std::vector<Polylines>              *lightning_infill_lines,
    std::function<void()>                throw_on_cancel)
{
    std::vector<float>      angles { support_params.base_angle };
    if (config.support_base_pattern == smpRectilinearGrid)
        angles.push_back(support_params.interface_angle);

    BoundingBox bbox_object(Point(-scale_(1.), -scale_(1.0)), Point(scale_(1.), scale_(1.)));

//    const coordf_t link_max_length_factor = 3.;
    const coordf_t link_max_length_factor = 0.;

    // Insert the raft base layers.
    auto n_raft_layers = std::min<size_t>(support_layers.size(), std::max(0, int(slicing_params.raft_layers()) - 1));

    tbb::parallel_for(tbb::blocked_range<size_t>(0, n_raft_layers),
        [&support_layers, &raft_layers, &intermediate_layers, &config, &support_params, &slicing_params,
            &bbox_object, &throw_on_cancel, link_max_length_factor]
            (const tbb::blocked_range<size_t>& range) {
        for (size_t support_layer_id = range.begin(); support_layer_id < range.end(); ++ support_layer_id)
        {
            if (throw_on_cancel)
                throw_on_cancel();
            assert(support_layer_id < raft_layers.size());
            SupportLayer               &support_layer = *support_layers[support_layer_id];
            assert(support_layer.support_fills.entities.empty());
            SupportGeneratorLayer      &raft_layer    = *raft_layers[support_layer_id];

            std::unique_ptr<Fill> filler_interface = std::unique_ptr<Fill>(Fill::new_from_type(support_params.raft_interface_fill_pattern));
            std::unique_ptr<Fill> filler_support   = std::unique_ptr<Fill>(Fill::new_from_type(support_params.base_fill_pattern));
            filler_interface->set_bounding_box(bbox_object);
            filler_support->set_bounding_box(bbox_object);

            // Print the tree supports cutting through the raft with the exception of the 1st layer, where a full support layer will be printed below
            // both the raft and the trees.
            // Trim the raft layers with the tree polygons.
            const Polygons &tree_polygons =
                support_layer_id > 0 && support_layer_id < intermediate_layers.size() && is_approx(intermediate_layers[support_layer_id]->print_z, support_layer.print_z) ?
                intermediate_layers[support_layer_id]->polygons : Polygons();

            // Print the support base below the support columns, or the support base for the support columns plus the contacts.
            if (support_layer_id > 0) {
                const Polygons &to_infill_polygons = (support_layer_id < slicing_params.base_raft_layers) ?
                    raft_layer.polygons :
                    //FIXME misusing contact_polygons for support columns.
                    ((raft_layer.contact_polygons == nullptr) ? Polygons() : *raft_layer.contact_polygons);
                // Trees may cut through the raft layers down to a print bed.
                Flow flow(float(support_params.support_material_flow.width()), float(raft_layer.height), support_params.support_material_flow.nozzle_diameter());
                assert(!raft_layer.bridging);
                if (! to_infill_polygons.empty()) {
                    Fill *filler = filler_support.get();
                    filler->angle = support_params.raft_angle_base;
                    filler->spacing = support_params.support_material_flow.spacing();
                    filler->link_max_length = coord_t(scale_(filler->spacing * link_max_length_factor / support_params.support_density));
                    fill_expolygons_with_sheath_generate_paths(
                        // Destination
                        support_layer.support_fills.entities,
                        // Regions to fill
                        tree_polygons.empty() ? to_infill_polygons : diff(to_infill_polygons, tree_polygons),
                        // Filler and its parameters
                        filler, float(support_params.support_density),
                        // Extrusion parameters
                        ExtrusionRole::erSupportMaterial, flow,
                        support_params, support_params.with_sheath, false);
                }
                if (! tree_polygons.empty())
                    tree_supports_generate_paths(support_layer.support_fills.entities, tree_polygons, flow, support_params);
            }

            Fill *filler = filler_interface.get();
            Flow  flow = support_params.first_layer_flow;
            float density = 0.f;
            if (support_layer_id == 0) {
                // Base flange.
                filler->angle = support_params.raft_angle_1st_layer;
                filler->spacing = support_params.first_layer_flow.spacing();
                density       = float(config.raft_first_layer_density.value * 0.01);
            } else if (support_layer_id >= slicing_params.base_raft_layers) {
                filler->angle = support_params.raft_interface_angle(support_layer.interface_id());
                // We don't use $base_flow->spacing because we need a constant spacing
                // value that guarantees that all layers are correctly aligned.
                filler->spacing = support_params.support_material_flow.spacing();
                assert(! raft_layer.bridging);
                flow          = Flow(float(support_params.raft_interface_flow.width()), float(raft_layer.height), support_params.raft_interface_flow.nozzle_diameter());
                density       = float(support_params.raft_interface_density);
            } else
                continue;
            filler->link_max_length = coord_t(scale_(filler->spacing * link_max_length_factor / density));
            fill_expolygons_with_sheath_generate_paths(
                // Destination
                support_layer.support_fills.entities,
                // Regions to fill
                tree_polygons.empty() ? raft_layer.polygons : diff(raft_layer.polygons, tree_polygons),
                // Filler and its parameters
                filler, density,
                // Extrusion parameters
                (support_layer_id < slicing_params.base_raft_layers) ? ExtrusionRole::erSupportMaterial : ExtrusionRole::erSupportMaterialInterface, flow,
                // sheath at first layer
                support_params, support_layer_id == 0, support_layer_id == 0);
        }
    });

    struct LayerCacheItem {
        LayerCacheItem(SupportGeneratorLayerExtruded *layer_extruded = nullptr) : layer_extruded(layer_extruded) {}
        SupportGeneratorLayerExtruded         *layer_extruded;
        std::vector<SupportGeneratorLayer*>    overlapping;
    };
    struct LayerCache {
        SupportGeneratorLayerExtruded                                     bottom_contact_layer;
        SupportGeneratorLayerExtruded                                     top_contact_layer;
        SupportGeneratorLayerExtruded                                     base_layer;
        SupportGeneratorLayerExtruded                                     interface_layer;
        SupportGeneratorLayerExtruded                                     base_interface_layer;
        boost::container::static_vector<LayerCacheItem, 5>  nonempty;
        // support interface ironing related
        Polygons                                                          polys_to_iron;

        void add_nonempty_and_sort() {
            for (SupportGeneratorLayerExtruded *item : { &bottom_contact_layer, &top_contact_layer, &interface_layer, &base_interface_layer, &base_layer })
                if (! item->empty())
                    this->nonempty.emplace_back(item);
            // Sort the layers with the same print_z coordinate by their heights, thickest first.
            std::stable_sort(this->nonempty.begin(), this->nonempty.end(), [](const LayerCacheItem &lc1, const LayerCacheItem &lc2) { return lc1.layer_extruded->layer->height > lc2.layer_extruded->layer->height; });
        }
    };
    std::vector<LayerCache>             layer_caches(support_layers.size());

    tbb::parallel_for(tbb::blocked_range<size_t>(n_raft_layers, support_layers.size()),
        [&config, &slicing_params, &support_params, &support_layers, &bottom_contacts, &top_contacts, &intermediate_layers, &interface_layers, &base_interface_layers, &cooldown_areas, &lightning_infill_lines, &layer_caches, &throw_on_cancel,
            &bbox_object, &angles, n_raft_layers, link_max_length_factor]
            (const tbb::blocked_range<size_t>& range) {
        // Indices of the 1st layer in their respective container at the support layer height.
        size_t idx_layer_bottom_contact   = size_t(-1);
        size_t idx_layer_top_contact      = size_t(-1);
        size_t idx_layer_intermediate     = size_t(-1);
        size_t idx_layer_interface        = size_t(-1);
        size_t idx_layer_base_interface   = size_t(-1);
        const auto fill_type_first_layer  = ipRectilinear;
        auto filler_interface       = std::unique_ptr<Fill>(Fill::new_from_type(support_params.contact_fill_pattern));
        // Filler for the 1st layer interface, if different from filler_interface.
        auto filler_first_layer_ptr = std::unique_ptr<Fill>(range.begin() == 0 && support_params.contact_fill_pattern != fill_type_first_layer ? Fill::new_from_type(fill_type_first_layer) : nullptr);
        // Pointer to the 1st layer interface filler.
        auto filler_first_layer     = filler_first_layer_ptr ? filler_first_layer_ptr.get() : filler_interface.get();
        // Filler for the 1st layer interface, if different from filler_interface.
        auto filler_raft_contact_ptr = std::unique_ptr<Fill>(range.begin() == n_raft_layers && config.support_interface_top_layers.value == 0 ?
            Fill::new_from_type(support_params.raft_interface_fill_pattern) : nullptr);
        // Pointer to the 1st layer interface filler.
        auto filler_raft_contact     = filler_raft_contact_ptr ? filler_raft_contact_ptr.get() : filler_interface.get();
        // Filler for the base interface (to be used for soluble interface / non soluble base, to produce non soluble interface layer below soluble interface layer).
        auto filler_base_interface  = std::unique_ptr<Fill>(base_interface_layers.empty() ? nullptr :
            Fill::new_from_type(support_params.interface_density > 0.95 || support_params.with_sheath ? ipRectilinear : ipSupportBase));
        auto filler_support         = std::unique_ptr<Fill>(Fill::new_from_type(support_params.base_fill_pattern));
        filler_interface->set_bounding_box(bbox_object);
        if (filler_first_layer_ptr)
            filler_first_layer_ptr->set_bounding_box(bbox_object);
        if (filler_raft_contact_ptr)
            filler_raft_contact_ptr->set_bounding_box(bbox_object);
        if (filler_base_interface)
            filler_base_interface->set_bounding_box(bbox_object);
        filler_support->set_bounding_box(bbox_object);
        for (size_t support_layer_id = range.begin(); support_layer_id < range.end(); ++ support_layer_id)
        {
            if (throw_on_cancel)
                throw_on_cancel();
            SupportLayer &support_layer = *support_layers[support_layer_id];
            LayerCache   &layer_cache   = layer_caches[support_layer_id];
            const float support_interface_angle = support_params.support_interface_angle(support_layer.interface_id(), config.support_interface_pattern);
            // Find polygons with the same print_z.
            SupportGeneratorLayerExtruded &bottom_contact_layer = layer_cache.bottom_contact_layer;
            SupportGeneratorLayerExtruded &top_contact_layer    = layer_cache.top_contact_layer;
            SupportGeneratorLayerExtruded &base_layer           = layer_cache.base_layer;
            SupportGeneratorLayerExtruded &interface_layer      = layer_cache.interface_layer;
            SupportGeneratorLayerExtruded &base_interface_layer = layer_cache.base_interface_layer;
            // Increment the layer indices to find a layer at support_layer.print_z.
            {
                auto fun = [&support_layer](const SupportGeneratorLayer *l){ return l->print_z >= support_layer.print_z - EPSILON; };
                idx_layer_bottom_contact  = idx_higher_or_equal(bottom_contacts,     idx_layer_bottom_contact,  fun);
                idx_layer_top_contact     = idx_higher_or_equal(top_contacts,        idx_layer_top_contact,     fun);
                idx_layer_intermediate    = idx_higher_or_equal(intermediate_layers, idx_layer_intermediate,    fun);
                idx_layer_interface       = idx_higher_or_equal(interface_layers,    idx_layer_interface,       fun);
                idx_layer_base_interface  = idx_higher_or_equal(base_interface_layers, idx_layer_base_interface,fun);
            }
            // Copy polygons from the layers.
            if (idx_layer_bottom_contact < bottom_contacts.size() && bottom_contacts[idx_layer_bottom_contact]->print_z < support_layer.print_z + EPSILON)
                bottom_contact_layer.layer = bottom_contacts[idx_layer_bottom_contact];
            if (idx_layer_top_contact < top_contacts.size() && top_contacts[idx_layer_top_contact]->print_z < support_layer.print_z + EPSILON)
                top_contact_layer.layer = top_contacts[idx_layer_top_contact];
            if (idx_layer_interface < interface_layers.size() && interface_layers[idx_layer_interface]->print_z < support_layer.print_z + EPSILON)
                interface_layer.layer = interface_layers[idx_layer_interface];
            if (idx_layer_base_interface < base_interface_layers.size() && base_interface_layers[idx_layer_base_interface]->print_z < support_layer.print_z + EPSILON)
                base_interface_layer.layer = base_interface_layers[idx_layer_base_interface];
            if (idx_layer_intermediate < intermediate_layers.size() && intermediate_layers[idx_layer_intermediate]->print_z < support_layer.print_z + EPSILON)
                base_layer.layer = intermediate_layers[idx_layer_intermediate];

            // This layer is a raft contact layer. Any contact polygons at this layer are raft contacts.
            bool raft_layer = slicing_params.interface_raft_layers && top_contact_layer.layer && is_approx(top_contact_layer.layer->print_z, slicing_params.raft_contact_top_z);
            if (config.support_interface_top_layers == 0) {
                // If no top interface layers were requested, we treat the contact layer exactly as a generic base layer.
                // Don't merge the raft contact layer though.
                if (support_params.can_merge_support_regions && ! raft_layer) {
                    if (base_layer.could_merge(top_contact_layer))
                        base_layer.merge(std::move(top_contact_layer));
                    else if (base_layer.empty())
                        base_layer = std::move(top_contact_layer);
                }
            } else {
                // save the polygons to extrude in topo contact layer into polys to iron
                if (support_params.enable_support_ironing && !top_contact_layer.empty())
                    layer_cache.polys_to_iron = top_contact_layer.polygons_to_extrude();
                if (! top_contact_layer.empty()) {
                    const Flow contact_loop_flow =
                        support_layer.planned_interface_nozzle_diameter > EPSILON ?
                        support_params.support_material_interface_flow_for_nozzle(
                            support_layer.planned_interface_nozzle_diameter,
                            float(top_contact_layer.layer->height)) :
                        support_params.support_material_interface_flow;
                    LoopInterfaceProcessor loop_interface_processor(
                        1.5 * contact_loop_flow.scaled_width());
                    loop_interface_processor.n_contact_loops =
                        config.support_interface_loop_pattern.value ? 1 : 0;
                    loop_interface_processor.generate(top_contact_layer, contact_loop_flow);
                }
                // If no loops are allowed, we treat the contact layer exactly as a generic interface layer.
                // Merge interface_layer into top_contact_layer, as the top_contact_layer is not synchronized and therefore it will be used
                // to trim other layers.
                if (top_contact_layer.could_merge(interface_layer) && ! raft_layer)
                    top_contact_layer.merge(std::move(interface_layer));
            }
            if ((config.support_interface_top_layers == 0 || config.support_interface_bottom_layers == 0) && support_params.can_merge_support_regions) {
                if (base_layer.could_merge(bottom_contact_layer))
                    base_layer.merge(std::move(bottom_contact_layer));
                else if (base_layer.empty() && ! bottom_contact_layer.empty() && ! bottom_contact_layer.layer->bridging)
                    base_layer = std::move(bottom_contact_layer);
            } else if (bottom_contact_layer.could_merge(top_contact_layer) && ! raft_layer)
                top_contact_layer.merge(std::move(bottom_contact_layer));
            else if (bottom_contact_layer.could_merge(interface_layer))
                bottom_contact_layer.merge(std::move(interface_layer));

#if 0
            if ( ! interface_layer.empty() && ! base_layer.empty()) {
                // turn base support into interface when it's contained in our holes
                // (this way we get wider interface anchoring)
                //FIXME The intention of the code below is unclear. One likely wanted to just merge small islands of base layers filling in the holes
                // inside interface layers, but the code below fills just too much, see GH #4570
                Polygons islands = top_level_islands(interface_layer.layer->polygons);
                polygons_append(interface_layer.layer->polygons, intersection(base_layer.layer->polygons, islands));
                base_layer.layer->polygons = diff(base_layer.layer->polygons, islands);
            }
#endif

            // Top and bottom contacts, interface layers.
            enum class InterfaceLayerType { TopContact, BottomContact, RaftContact, Interface, InterfaceAsBase };
            auto extrude_interface = [&](SupportGeneratorLayerExtruded &layer_ex, InterfaceLayerType interface_layer_type) {
                if (! layer_ex.empty() && ! layer_ex.polygons_to_extrude().empty()) {
                    bool interface_as_base = interface_layer_type == InterfaceLayerType::InterfaceAsBase;
                    bool raft_contact      = interface_layer_type == InterfaceLayerType::RaftContact;
                    //FIXME Bottom interfaces are extruded with the briding flow. Some bridging layers have its height slightly reduced, therefore
                    // the bridging flow does not quite apply. Reduce the flow to area of an ellipse? (A = pi * a * b)
                    auto *filler = raft_contact ? filler_raft_contact : filler_interface.get();
                    Flow interface_flow_src = raft_contact ? support_params.raft_interface_flow :
                        interface_as_base ? support_params.support_material_flow : support_params.support_material_interface_flow;
                    if (! raft_contact && support_layer.planned_interface_nozzle_diameter > EPSILON)
                        interface_flow_src = interface_as_base ?
                            support_params.support_material_flow_for_nozzle(
                                support_layer.planned_interface_nozzle_diameter, float(layer_ex.layer->height)) :
                            support_params.support_material_interface_flow_for_nozzle(
                                support_layer.planned_interface_nozzle_diameter, float(layer_ex.layer->height));
                    const double bridge_nozzle = support_layer.planned_interface_nozzle_diameter > EPSILON ?
                        support_layer.planned_interface_nozzle_diameter :
                        support_params.support_material_bottom_interface_flow.nozzle_diameter();
                    auto interface_flow = layer_ex.layer->bridging ?
                        Flow::bridging_flow(layer_ex.layer->height, bridge_nozzle) :
                        interface_flow_src.with_height(float(layer_ex.layer->height));
                    filler->angle  = interface_as_base ?
                                         // If zero interface layers are configured, use the same angle as for the base layers.
                                        angles[support_layer_id % angles.size()] :
                                         // Use interface angle for the interface layers.
                                        raft_contact ? support_params.raft_interface_angle(support_layer.interface_id()) : support_interface_angle;
                    double density = raft_contact ? support_params.raft_interface_density :
                        interface_as_base ? support_params.support_density :
                        std::min(1., interface_flow_src.spacing() /
                            (config.support_interface_spacing.value + interface_flow_src.spacing()));
                    filler->spacing = interface_flow_src.spacing();
                    filler->link_max_length = coord_t(scale_(filler->spacing * link_max_length_factor / density));
                    // replace the old extrusion, avoiding empty infill generation which will cause the floating extrusion
                    auto expolygons = union_safety_offset_ex(layer_ex.polygons_to_extrude());
                    FillParams fill_params;
                    fill_params.density     = density;
                    fill_params.dont_adjust = true;
                    for (ExPolygon& expoly : expolygons)
                    {
                        auto oldSize = layer_ex.extrusions.size();
                        // Keep the polygon for the outline fallback when a coarse nozzle cannot fit
                        // the configured interface pattern into a narrow contact region.
                        fill_expolygon_generate_paths(layer_ex.extrusions, ExPolygon(expoly), filler, fill_params, density, ExtrusionRole::erSupportMaterialInterface,
                                                    interface_flow);
                        // no any infill extrusions generated
                        if (oldSize != layer_ex.extrusions.size())
                        {
                            continue;
                        }
                        // using the outline path
                        const double spacing     = interface_flow.scaled_spacing();
                        const double clip_length = spacing * 0.5;
                        ExPolygons fallback_regions = offset_ex(expoly, -clip_length);
                        for (auto &shrinkExpoly : fallback_regions) {
                            extrusion_entities_append_paths(layer_ex.extrusions, draw_perimeters(shrinkExpoly, clip_length), ExtrusionRole::erSupportMaterialInterface,
                                                            interface_flow.mm3_per_mm(), interface_flow.width(), interface_flow.height(),
                                                            // Disable reversal of the path, always start with the anchor, always print CCW.
                                                            false);
                        }
                    }
                }
            };
            const bool top_interfaces = config.support_interface_top_layers.value != 0;
            const bool bottom_interfaces = top_interfaces && config.support_interface_bottom_layers != 0;
            extrude_interface(top_contact_layer,    raft_layer ? InterfaceLayerType::RaftContact : top_interfaces ? InterfaceLayerType::TopContact : InterfaceLayerType::InterfaceAsBase);
            extrude_interface(bottom_contact_layer, bottom_interfaces ? InterfaceLayerType::BottomContact : InterfaceLayerType::InterfaceAsBase);
            extrude_interface(interface_layer,      top_interfaces ? InterfaceLayerType::Interface : InterfaceLayerType::InterfaceAsBase);

            // Base interface layers under soluble interfaces
            if ( ! base_interface_layer.empty() && ! base_interface_layer.polygons_to_extrude().empty()) {
                Fill *filler = filler_base_interface.get();
                //FIXME Bottom interfaces are extruded with the briding flow. Some bridging layers have its height slightly reduced, therefore
                // the bridging flow does not quite apply. Reduce the flow to area of an ellipse? (A = pi * a * b)
                assert(! base_interface_layer.layer->bridging);
                Flow interface_flow = support_layer.planned_nozzle_diameter > EPSILON ?
                    support_params.support_material_flow_for_nozzle(
                        support_layer.planned_nozzle_diameter,
                        float(base_interface_layer.layer->height)) :
                    support_params.support_material_flow.with_height(float(base_interface_layer.layer->height));
                filler->angle           = support_interface_angle;
                filler->spacing = interface_flow.spacing();
                const double density = std::min(1., interface_flow.spacing() /
                    (config.support_interface_spacing.value + interface_flow.spacing()));
                filler->link_max_length = coord_t(scale_(filler->spacing * link_max_length_factor / density));
                fill_expolygons_generate_paths(
                    // Destination
                    base_interface_layer.extrusions,
                    //base_layer_interface.extrusions,
                    // Regions to fill
                    union_safety_offset_ex(base_interface_layer.polygons_to_extrude()),
                    // Filler and its parameters
                    filler, float(density),
                    // Extrusion parameters
                    ExtrusionRole::erSupportTransition, interface_flow);
            }

            // Base support or flange.
            if (! base_layer.empty() && ! base_layer.polygons_to_extrude().empty()) {
                Fill             *filler          = filler_support.get();
                filler->angle = angles[support_layer_id % angles.size()];
                // We don't use $base_flow->spacing because we need a constant spacing
                // value that guarantees that all layers are correctly aligned.
                assert(! base_layer.layer->bridging);
                // BBS multi-diameter: a merged slab is extruded by the filament the support merge plan reserved
                // for it, so its line width has to follow that nozzle instead of the support_filament default.
                const Flow base_flow_src = support_layer.planned_nozzle_diameter > EPSILON ?
                    support_params.support_material_flow_for_nozzle(support_layer.planned_nozzle_diameter, float(support_params.support_material_flow.height())) :
                    support_params.support_material_flow;
                auto flow = base_flow_src.with_height(float(base_layer.layer->height));
                filler->spacing = base_flow_src.spacing();
                filler->link_max_length = coord_t(scale_(filler->spacing * link_max_length_factor / support_params.support_density));
                float density = float(support_params.support_density);
                bool  sheath  = support_params.with_sheath;
                bool  no_sort = false;
                bool  done    = false;
                if (base_layer.layer->bottom_z < EPSILON) {
                    // A merged base flange keeps the first-layer pattern and width, while its flow height
                    // spans the complete slab down to the bed.
                    filler = filler_first_layer;
                    filler->angle = Geometry::deg2rad(float(config.support_angle.value + 90.));
                    density = float(config.raft_first_layer_density.value * 0.01);
                    flow = support_layer.planned_nozzle_diameter > EPSILON ?
                        support_params.support_material_first_layer_flow_for_nozzle(
                            support_layer.planned_nozzle_diameter, float(base_layer.layer->height)) :
                        support_params.first_layer_flow.with_height(float(base_layer.layer->height));
                    // use the proper spacing for first layer as we don't need to align
                    // its pattern to the other layers
                    //FIXME When paralellizing, each thread shall have its own copy of the fillers.
                    filler->spacing = flow.spacing();
                    filler->link_max_length = coord_t(scale_(filler->spacing * link_max_length_factor / density));
                    sheath  = true;
                    no_sort = true;
                } else if (support_params.support_style == SupportMaterialStyle::smsTreeOrganic) {
                    // if the tree supports are too tall, use double wall to make it stronger
                    SupportParameters support_params2 = support_params;
                    if (support_layer.print_z > 100.0)
                        support_params2.tree_branch_diameter_double_wall_area_scaled = 0.1;
                    tree_supports_generate_paths(base_layer.extrusions, base_layer.polygons_to_extrude(), flow, support_params2);
                    // Ground internal floating voids. The organic tree base above is printed sheath-only
                    // (hollow), so material printed over an internal hole gets no support. The polylines
                    // were already clipped to this layer's filled footprint minus the model by
                    // organic_lightning_infill. They are indexed like the compacted intermediate_layers
                    // (print_z), not like support_layers (which also holds raft and contact-only rows).
                    // Emit only for the intermediate already bound as this support layer's base.
                    if (lightning_infill_lines &&
                        idx_layer_intermediate < lightning_infill_lines->size() &&
                        idx_layer_intermediate < intermediate_layers.size() &&
                        base_layer.layer == intermediate_layers[idx_layer_intermediate] &&
                        ! (*lightning_infill_lines)[idx_layer_intermediate].empty()) {
                        // The lines were planned before these toolpaths existed. Keep them 0.85 w off the
                        // sheath centrelines (enough overlap left to bond) and off the contact / interface areas.
                        Polylines &lightning = (*lightning_infill_lines)[idx_layer_intermediate];
                        const BoundingBox bbox = get_extents(lightning).inflated(flow.scaled_width());
                        Polylines sheath;
                        for (const ExtrusionEntity *entity : base_layer.extrusions)
                            entity->collect_polylines(sheath);
                        sheath.erase(std::remove_if(sheath.begin(), sheath.end(),
                            [&bbox](const Polyline &pl) { return ! bbox.overlap(get_extents(pl)); }), sheath.end());
                        // 外墙远长于闪电线，只偏移闪电线附近的一段外墙，结果不变。
                        const float sheath_offset = 0.85f * float(flow.scaled_width());
                        keep_polylines_near_bbox(sheath,
                            get_extents(lightning).inflated(coord_t(1.5f * sheath_offset) + SCALED_EPSILON));
                        Polygons blocked = offset(sheath, sheath_offset);
                        for (const SupportGeneratorLayerExtruded *other :
                             { &bottom_contact_layer, &top_contact_layer, &interface_layer, &base_interface_layer })
                            if (! other->empty())
                                append(blocked, offset(ClipperUtils::clip_clipper_polygons_with_subject_bbox(
                                    other->polygons_to_extrude(), bbox), 0.5f * float(flow.scaled_width())));
                        lightning = diff_pl(lightning, blocked);
                        lightning.erase(std::remove_if(lightning.begin(), lightning.end(),
                            [](const Polyline &pl) { return pl.length() < scaled<double>(1.); }), lightning.end());
                        extrusion_entities_append_paths(base_layer.extrusions, std::move(lightning),
                            ExtrusionRole::erSupportMaterial, float(flow.mm3_per_mm()), float(flow.width()), float(flow.height()));
                    }
                    done = true;
                }
                if (! done)
                    fill_expolygons_with_sheath_generate_paths(
                        // Destination
                        base_layer.extrusions,
                        // Regions to fill
                        base_layer.polygons_to_extrude(),
                        // Filler and its parameters
                        filler, density,
                        // Extrusion parameters
                        ExtrusionRole::erSupportMaterial, flow,
                        support_params, sheath, no_sort);
            }

            if (support_layer_id < cooldown_areas.size() && !cooldown_areas[support_layer_id].empty()) {
                std::function<void(const ExtrusionEntity *, Polygons &)> polygonsCoveredByWidth =
                    [&](const ExtrusionEntity *entity, Polygons &polygons) {
                        if (throw_on_cancel)
                            throw_on_cancel();
                        if (const ExtrusionEntityCollection *collection = dynamic_cast<const ExtrusionEntityCollection *>(entity)) {
                            for (const ExtrusionEntity *child : collection->entities)
                                polygonsCoveredByWidth(child, polygons);
                        } else {
                            entity->polygons_covered_by_width(polygons, 0.f);
                        }
                    };
                std::function<void(ExtrusionEntitiesPtr &)> setOverhangDegreeImpl = [&](ExtrusionEntitiesPtr &entities) {
                    for (auto entityPtr : entities) {
                        Polygons polygons;
                        polygonsCoveredByWidth(entityPtr, polygons);
                        if (overlaps(to_expolygons(std::move(polygons)), cooldown_areas[support_layer_id])) {
                            if (ExtrusionEntityCollection *collection = dynamic_cast<ExtrusionEntityCollection *>(entityPtr)) {
                                setOverhangDegreeImpl(collection->entities);
                            } else if (ExtrusionPath *path = dynamic_cast<ExtrusionPath *>(entityPtr)) {
                                path->set_overhang_degree(10);
                            } else if (ExtrusionMultiPath *multipath = dynamic_cast<ExtrusionMultiPath *>(entityPtr)) {
                                for (ExtrusionPath &path : multipath->paths) { path.set_overhang_degree(10); }
                            } else if (ExtrusionLoop *loop = dynamic_cast<ExtrusionLoop *>(entityPtr)) {
                                for (ExtrusionPath &path : loop->paths) { path.set_overhang_degree(10); }
                            }
                        }
                    }
                };
                setOverhangDegreeImpl(base_layer.extrusions);
                setOverhangDegreeImpl(top_contact_layer.extrusions);
                setOverhangDegreeImpl(bottom_contact_layer.extrusions);
                setOverhangDegreeImpl(interface_layer.extrusions);
                setOverhangDegreeImpl(base_interface_layer.extrusions);
                if (throw_on_cancel)
                    throw_on_cancel();
            }

            // Merge base_interface_layers to base_layers to avoid unneccessary retractions
            if (! base_layer.empty() && ! base_interface_layer.empty() && ! base_layer.polygons_to_extrude().empty() && ! base_interface_layer.polygons_to_extrude().empty() &&
                base_layer.could_merge(base_interface_layer))
                base_layer.merge(std::move(base_interface_layer));

            layer_cache.add_nonempty_and_sort();

            // Collect the support areas with this print_z into islands, as there is no need
            // for retraction over these islands.
            Polygons polys;
            // Collect the extrusions, sorted by the bottom extrusion height.
            for (LayerCacheItem &layer_cache_item : layer_cache.nonempty) {
                // Collect islands to polys.
                layer_cache_item.layer_extruded->polygons_append(polys);
                // The print_z of the top contact surfaces and bottom_z of the bottom contact surfaces are "free"
                // in a sense that they are not synchronized with other support layers. As the top and bottom contact surfaces
                // are inflated to achieve a better anchoring, it may happen, that these surfaces will at least partially
                // overlap in Z with another support layers, leading to over-extrusion.
                // Mitigate the over-extrusion by modulating the extrusion rate over these regions.
                // The print head will follow the same print_z, but the layer thickness will be reduced
                // where it overlaps with another support layer.
                //FIXME When printing a briging path, what is an equivalent height of the squished extrudate of the same width?
                // Collect overlapping top/bottom surfaces.
                layer_cache_item.overlapping.reserve(20);
                coordf_t bottom_z = layer_cache_item.layer_extruded->layer->bottom_print_z() + EPSILON;
                auto add_overlapping = [&layer_cache_item, bottom_z](const SupportGeneratorLayersPtr &layers, size_t idx_top) {
                    for (int i = int(idx_top) - 1; i >= 0 && layers[i]->print_z > bottom_z; -- i)
                        layer_cache_item.overlapping.push_back(layers[i]);
                };
                add_overlapping(top_contacts, idx_layer_top_contact);
                if (layer_cache_item.layer_extruded->layer->layer_type == SupporLayerType::sltBottomContact) {
                    // Bottom contact layer may overlap with a base layer, which may be changed to interface layer.
                    add_overlapping(intermediate_layers,   idx_layer_intermediate);
                    add_overlapping(interface_layers,      idx_layer_interface);
                    add_overlapping(base_interface_layers, idx_layer_base_interface);
                }
                // Order the layers by lexicographically by an increasing print_z and a decreasing layer height.
                std::stable_sort(layer_cache_item.overlapping.begin(), layer_cache_item.overlapping.end(), [](auto *l1, auto *l2) { return *l1 < *l2; });
            }
            assert(support_layer.support_islands.empty());
            if (! polys.empty()) {
                support_layer.support_islands = union_ex(polys);
                // support_layer.support_islands_bboxes.reserve(support_layer.support_islands.size());
                // for (const ExPolygon &expoly : support_layer.support_islands)
                //     support_layer.support_islands_bboxes.emplace_back(get_extents(expoly).inflated(SCALED_EPSILON));
            }
        } // for each support_layer_id
    });

    // Now modulate the support layer height in parallel.
    tbb::parallel_for(tbb::blocked_range<size_t>(n_raft_layers, support_layers.size()),
        [&support_layers, &layer_caches, &support_params, &bbox_object, &throw_on_cancel]
            (const tbb::blocked_range<size_t>& range) {
        for (size_t support_layer_id = range.begin(); support_layer_id < range.end(); ++ support_layer_id) {
            if (throw_on_cancel)
                throw_on_cancel();
            SupportLayer &support_layer = *support_layers[support_layer_id];
            LayerCache   &layer_cache   = layer_caches[support_layer_id];
            // For all extrusion types at this print_z, ordered by decreasing layer height:
            for (LayerCacheItem &layer_cache_item : layer_cache.nonempty) {
                // Trim the extrusion height from the bottom by the overlapping layers.
                modulate_extrusion_by_overlapping_layers(layer_cache_item.layer_extruded->extrusions, *layer_cache_item.layer_extruded->layer, layer_cache_item.overlapping);
                support_layer.support_fills.append(std::move(layer_cache_item.layer_extruded->extrusions));
            }

            // support interface ironing related generation from Orca: Generate iron toolpath for contact layer
            if (!layer_cache.polys_to_iron.empty() && support_params.enable_support_ironing) {
                auto ironing_fill = std::unique_ptr<Fill>(Fill::new_from_type(support_params.ironing_pattern));
                ironing_fill->set_bounding_box(bbox_object);
                ironing_fill->layer_id = support_layer.id();
                ironing_fill->z        = support_layer.print_z;
                ironing_fill->overlap  = 0;
                ironing_fill->angle    = support_params.ironing_angle;
                ironing_fill->spacing  = support_params.ironing_line_spacing;
                ironing_fill->link_max_length = (coord_t) scale_(3. * ironing_fill->spacing);

                ExPolygons polys_to_iron = union_safety_offset_ex(layer_cache.polys_to_iron);
                layer_cache.polys_to_iron.clear();

                // Find the layer above that directly overlaps current layer, clip the overlapped part
                if (support_layer_id < support_layers.size() - 1) {
                    const auto &upper_layer = support_layers[support_layer_id + 1];
                    if (!upper_layer->support_islands.empty() && upper_layer->bottom_z() <= support_layer.print_z + EPSILON) {

                            polys_to_iron = diff_ex(polys_to_iron, upper_layer->support_islands);
                    }
                }
                if (support_params.ironing_inset > EPSILON)
                    polys_to_iron = union_ex(offset(polys_to_iron, -scale_(support_params.ironing_inset)));

                const coordf_t interface_height = support_layer.interface_slab_height > EPSILON ?
                    support_layer.interface_slab_height : support_layer.height;
                const Flow interface_flow = support_layer.planned_interface_nozzle_diameter > EPSILON ?
                    support_params.support_material_interface_flow_for_nozzle(
                        support_layer.planned_interface_nozzle_diameter,
                        float(interface_height)) :
                    support_params.support_material_interface_flow.with_height(float(interface_height));
                Flow support_ironing_flow(interface_flow.width(),
                                          interface_flow.height() * support_params.ironing_flow_percent * 0.01,
                                          interface_flow.nozzle_diameter());
                fill_expolygons_generate_paths(
                    // Destination
                    support_layer.support_fills.entities,
                    // Regions to fill
                    std::move(polys_to_iron),
                    // Filler and its parameters
                    ironing_fill.get(), 1.f,
                    // Extrusion parameters
                    ExtrusionRole::erSupportIroning, support_ironing_flow);
            }
        }
    });

#ifndef NDEBUG
    struct Test {
        static bool verify_nonempty(const ExtrusionEntityCollection *collection) {
            for (const ExtrusionEntity *ee : collection->entities) {
                if (const ExtrusionPath *path = dynamic_cast<const ExtrusionPath*>(ee))
                    assert(! path->empty());
                else if (const ExtrusionMultiPath *multipath = dynamic_cast<const ExtrusionMultiPath*>(ee))
                    assert(! multipath->empty());
                else if (const ExtrusionEntityCollection *eecol = dynamic_cast<const ExtrusionEntityCollection*>(ee)) {
                    assert(! eecol->empty());
                    return verify_nonempty(eecol);
                } else
                    assert(false);
            }
            return true;
        }
    };
    for (const SupportLayer *support_layer : support_layers)
        assert(Test::verify_nonempty(&support_layer->support_fills));
#endif // NDEBUG
}

/*
void PrintObjectSupportMaterial::clip_by_pillars(
    const PrintObject   &object,
    LayersPtr           &bottom_contacts,
    LayersPtr           &top_contacts,
    LayersPtr           &intermediate_contacts);

{
    // this prevents supplying an empty point set to BoundingBox constructor
    if (top_contacts.empty())
        return;

    coord_t pillar_size    = scale_(PILLAR_SIZE);
    coord_t pillar_spacing = scale_(PILLAR_SPACING);

    // A regular grid of pillars, filling the 2D bounding box.
    Polygons grid;
    {
        // Rectangle with a side of 2.5x2.5mm.
        Polygon pillar;
        pillar.points.push_back(Point(0, 0));
        pillar.points.push_back(Point(pillar_size, 0));
        pillar.points.push_back(Point(pillar_size, pillar_size));
        pillar.points.push_back(Point(0, pillar_size));

        // 2D bounding box of the projection of all contact polygons.
        BoundingBox bbox;
        for (LayersPtr::const_iterator it = top_contacts.begin(); it != top_contacts.end(); ++ it)
            bbox.merge(get_extents((*it)->polygons));
        grid.reserve(size_t(ceil(bb.size()(0) / pillar_spacing)) * size_t(ceil(bb.size()(1) / pillar_spacing)));
        for (coord_t x = bb.min(0); x <= bb.max(0) - pillar_size; x += pillar_spacing) {
            for (coord_t y = bb.min(1); y <= bb.max(1) - pillar_size; y += pillar_spacing) {
                grid.push_back(pillar);
                for (size_t i = 0; i < pillar.points.size(); ++ i)
                    grid.back().points[i].translate(Point(x, y));
            }
        }
    }

    // add pillars to every layer
    for my $i (0..n_support_z) {
        $shape->[$i] = [ @$grid ];
    }

    // build capitals
    for my $i (0..n_support_z) {
        my $z = $support_z->[$i];

        my $capitals = intersection(
            $grid,
            $contact->{$z} // [],
        );

        // work on one pillar at time (if any) to prevent the capitals from being merged
        // but store the contact area supported by the capital because we need to make
        // sure nothing is left
        my $contact_supported_by_capitals = [];
        foreach my $capital (@$capitals) {
            // enlarge capital tops
            $capital = offset([$capital], +($pillar_spacing - $pillar_size)/2);
            push @$contact_supported_by_capitals, @$capital;

            for (my $j = $i-1; $j >= 0; $j--) {
                my $jz = $support_z->[$j];
                $capital = offset($capital, -$self->interface_flow->scaled_width/2);
                last if !@$capitals;
                push @{ $shape->[$j] }, @$capital;
            }
        }

        // Capitals will not generally cover the whole contact area because there will be
        // remainders. For now we handle this situation by projecting such unsupported
        // areas to the ground, just like we would do with a normal support.
        my $contact_not_supported_by_capitals = diff(
            $contact->{$z} // [],
            $contact_supported_by_capitals,
        );
        if (@$contact_not_supported_by_capitals) {
            for (my $j = $i-1; $j >= 0; $j--) {
                push @{ $shape->[$j] }, @$contact_not_supported_by_capitals;
            }
        }
    }
}

sub clip_with_shape {
    my ($self, $support, $shape) = @_;

    foreach my $i (keys %$support) {
        // don't clip bottom layer with shape so that we
        // can generate a continuous base flange
        // also don't clip raft layers
        next if $i == 0;
        next if $i < $self->object_config->raft_layers;
        $support->{$i} = intersection(
            $support->{$i},
            $shape->[$i],
        );
    }
}
*/

/*!
 * \brief Unions two Polygons. Ensures that if the input is non empty that the output also will be non empty.
 * \param first[in] The first Polygon.
 * \param second[in] The second Polygon.
 * \return The union of both Polygons
 */
[[nodiscard]] Polygons safe_union(const Polygons first, const Polygons second)
{
    // unionPolygons can slowly remove Polygons under certain circumstances, because of rounding issues (Polygons that have a thin area).
    // This does not cause a problem when actually using it on large areas, but as influence areas (representing centerpoints) can be very thin, this does occur so this ugly
    // workaround is needed Here is an example of a Polygons object that will loose vertices when unioning, and will be gone after a few times unionPolygons was called:
    /*
    Polygons example;
    Polygon exampleInner;
    exampleInner.add(Point(120410,83599));//A
    exampleInner.add(Point(120384,83643));//B
    exampleInner.add(Point(120399,83618));//C
    exampleInner.add(Point(120414,83591));//D
    exampleInner.add(Point(120423,83570));//E
    exampleInner.add(Point(120419,83580));//F
    example.add(exampleInner);
    for(int i=0;i<10;i++){
         log("Iteration %d Example area: %f\n",i,area(example));
         example=example.unionPolygons();
    }
*/

    Polygons result;
    if (!first.empty() || !second.empty()) {
        result = union_(first, second);
        if (result.empty()) {
            BOOST_LOG_TRIVIAL(debug) << "Caught an area destroying union, enlarging areas a bit.";
            // just take the few lines we have, and offset them a tiny bit. Needs to be offsetPolylines, as offset may aleady have problems with the area.
            result = union_(offset(to_polylines(first), scaled<float>(0.002), jtMiter, 1.2), offset(to_polylines(second), scaled<float>(0.002), jtMiter, 1.2));
        }
    }

    return result;
}
[[nodiscard]] ExPolygons safe_union(const ExPolygons first, const ExPolygons second)
{
    ExPolygons result;
    if (!first.empty() || !second.empty()) {
        result = union_ex(first, second);
        if (result.empty()) {
            BOOST_LOG_TRIVIAL(debug) << "Caught an area destroying union, enlarging areas a bit.";
            // just take the few lines we have, and offset them a tiny bit. Needs to be offsetPolylines, as offset may aleady have problems with the area.
            Polygons result_polys = union_(offset(to_polylines(first), scaled<float>(0.002), jtMiter, 1.2), offset(to_polylines(second), scaled<float>(0.002), jtMiter, 1.2));
            for (auto &poly : result_polys) result.emplace_back(ExPolygon(poly));
        }
    }

    return result;
}

/*!
 * \brief Offsets (increases the area of) a polygons object in multiple steps to ensure that it does not lag through over a given obstacle.
 * \param me[in] Polygons object that has to be offset.
 * \param distance[in] The distance by which me should be offset. Expects values >=0.
 * \param collision[in] The area representing obstacles.
 * \param last_step_offset_without_check[in] The most it is allowed to offset in one step.
 * \param min_amount_offset[in] How many steps have to be done at least. As this uses round offset this increases the amount of vertices, which may be required if Polygons get
 * very small. Required as arcTolerance is not exposed in offset, which should result with a similar result. \return The resulting Polygons object.
 */
[[nodiscard]] Polygons safe_offset_inc(
    const Polygons &me, coord_t distance, const Polygons &collision, coord_t safe_step_size, coord_t last_step_offset_without_check, size_t min_amount_offset)
{
    bool     do_final_difference = last_step_offset_without_check == 0;
    Polygons ret                 = safe_union(me); // ensure sane input

    // Trim the collision polygons with the region of interest for diff() efficiency.
    Polygons collision_trimmed_buffer;
    auto     collision_trimmed = [&collision_trimmed_buffer, &collision, &ret, distance]() -> const Polygons     &{
        if (collision_trimmed_buffer.empty() && !collision.empty())
            collision_trimmed_buffer = ClipperUtils::clip_clipper_polygons_with_subject_bbox(collision, get_extents(ret).inflated(std::max(0, distance) + SCALED_EPSILON));
        return collision_trimmed_buffer;
    };

    if (distance == 0) return do_final_difference ? diff(ret, collision_trimmed()) : union_(ret);
    if (safe_step_size < 0 || last_step_offset_without_check < 0) {
        BOOST_LOG_TRIVIAL(error) << "Offset increase got invalid parameter!";
        return do_final_difference ? diff(ret, collision_trimmed()) : union_(ret);
    }

    coord_t step_size = safe_step_size;
    int     steps     = distance > last_step_offset_without_check ? (distance - last_step_offset_without_check) / step_size : 0;
    if (distance - steps * step_size > last_step_offset_without_check) {
        if ((steps + 1) * step_size <= distance)
            // This will be the case when last_step_offset_without_check >= safe_step_size
            ++steps;
        else
            do_final_difference = true;
    }
    if (steps + (distance < last_step_offset_without_check || (distance % step_size) != 0) < int(min_amount_offset) && min_amount_offset > 1) {
        // yes one can add a bool as the standard specifies that a result from compare operators has to be 0 or 1
        // reduce the stepsize to ensure it is offset the required amount of times
        step_size = distance / min_amount_offset;
        if (step_size >= safe_step_size) {
            // effectivly reduce last_step_offset_without_check
            step_size = safe_step_size;
            steps     = min_amount_offset;
        } else
            steps = distance / step_size;
    }
    // offset in steps
    for (int i = 0; i < steps; ++i) {
        ret = diff(offset(ret, step_size, ClipperLib::jtRound, scaled<float>(0.01)), collision_trimmed());
        // ensure that if many offsets are done the performance does not suffer extremely by the new vertices of jtRound.
        if (i % 10 == 7) ret = polygons_simplify(ret, scaled<double>(0.015));
    }
    // offset the remainder
    float last_offset = distance - steps * step_size;
    if (last_offset > SCALED_EPSILON) ret = offset(ret, distance - steps * step_size, ClipperLib::jtRound, scaled<float>(0.01));
    ret = polygons_simplify(ret, scaled<double>(0.015));

    if (do_final_difference) ret = diff(ret, collision_trimmed());
    return union_(ret);
}

} // namespace Slic3r
