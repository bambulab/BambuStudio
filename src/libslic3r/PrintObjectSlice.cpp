#include "ElephantFootCompensation.hpp"
#include "I18N.hpp"
#include "Layer.hpp"
#include "MultiMaterialSegmentation.hpp"
#include "Print.hpp"
#include "ClipperUtils.hpp"
//BBS
#include "ShortestPath.hpp"

#include <boost/log/trivial.hpp>

#include <tbb/parallel_for.h>

//! macro used to mark string used at localization, return same string
#define L(s) Slic3r::I18N::translate(s)

namespace Slic3r {

bool PrintObject::clip_multipart_objects = true;
bool PrintObject::infill_only_where_needed = false;

LayerPtrs new_layers(
    PrintObject                 *print_object,
    // Object layers (pairs of bottom/top Z coordinate), without the raft.
    const std::vector<coordf_t> &object_layers)
{
    LayerPtrs out;
    out.reserve(object_layers.size());
    auto     id   = int(print_object->slicing_parameters().raft_layers());
    coordf_t zmin = print_object->slicing_parameters().object_print_z_min;
    Layer   *prev = nullptr;
    for (size_t i_layer = 0; i_layer < object_layers.size(); i_layer += 2) {
        coordf_t lo = object_layers[i_layer];
        coordf_t hi = object_layers[i_layer + 1];
        coordf_t slice_z = 0.5 * (lo + hi);
        Layer *layer = new Layer(id ++, print_object, hi - lo, hi + zmin, slice_z);
        out.emplace_back(layer);
        if (prev != nullptr) {
            prev->upper_layer = layer;
            layer->lower_layer = prev;
        }
        prev = layer;
    }
    return out;
}

// Slice single triangle mesh.
static std::vector<ExPolygons> slice_volume(
    const ModelVolume             &volume,
    const std::vector<float>      &zs,
    const MeshSlicingParamsEx     &params,
    const std::function<void()>   &throw_on_cancel_callback)
{
    std::vector<ExPolygons> layers;
    if (! zs.empty()) {
        indexed_triangle_set its = volume.mesh().its;
        if (its.indices.size() > 0) {
            MeshSlicingParamsEx params2 { params };
            params2.trafo = params2.trafo * volume.get_matrix();
            if (params2.trafo.rotation().determinant() < 0.)
                its_flip_triangles(its);
            layers = slice_mesh_ex(its, zs, params2, throw_on_cancel_callback);
            throw_on_cancel_callback();
        }
    }
    return layers;
}

// Slice single triangle mesh.
// Filter the zs not inside the ranges. The ranges are closed at the bottom and open at the top, they are sorted lexicographically and non overlapping.
static std::vector<ExPolygons> slice_volume(
    const ModelVolume                           &volume,
    const std::vector<float>                    &z,
    const std::vector<t_layer_height_range>     &ranges,
    const MeshSlicingParamsEx                   &params,
    const std::function<void()>                 &throw_on_cancel_callback)
{
    std::vector<ExPolygons> out;
    if (! z.empty() && ! ranges.empty()) {
        if (ranges.size() == 1 && z.front() >= ranges.front().first && z.back() < ranges.front().second) {
            // All layers fit into a single range.
            out = slice_volume(volume, z, params, throw_on_cancel_callback);
        } else {
            std::vector<float>                     z_filtered;
            std::vector<std::pair<size_t, size_t>> n_filtered;
            z_filtered.reserve(z.size());
            n_filtered.reserve(2 * ranges.size());
            size_t i = 0;
            for (const t_layer_height_range &range : ranges) {
                for (; i < z.size() && z[i] < range.first; ++ i) ;
                size_t first = i;
                for (; i < z.size() && z[i] < range.second; ++ i)
                    z_filtered.emplace_back(z[i]);
                if (i > first)
                    n_filtered.emplace_back(std::make_pair(first, i));
            }
            if (! n_filtered.empty()) {
                std::vector<ExPolygons> layers = slice_volume(volume, z_filtered, params, throw_on_cancel_callback);
                out.assign(z.size(), ExPolygons());
                i = 0;
                for (const std::pair<size_t, size_t> &span : n_filtered)
                    for (size_t j = span.first; j < span.second; ++ j)
                        out[j] = std::move(layers[i ++]);
            }
        }
    }
    return out;
}
static inline bool model_volume_needs_slicing(const ModelVolume &mv)
{
    ModelVolumeType type = mv.type();
    return type == ModelVolumeType::MODEL_PART || type == ModelVolumeType::NEGATIVE_VOLUME || type == ModelVolumeType::PARAMETER_MODIFIER;
}

// Slice printable volumes, negative volumes and modifier volumes, sorted by ModelVolume::id().
// Apply closing radius.
// Apply positive XY compensation to ModelVolumeType::MODEL_PART and ModelVolumeType::PARAMETER_MODIFIER, not to ModelVolumeType::NEGATIVE_VOLUME.
// Apply contour simplification.
static std::vector<VolumeSlices> slice_volumes_inner(
    const PrintConfig                                        &print_config,
    const PrintObjectConfig                                  &print_object_config,
    const Transform3d                                        &object_trafo,
    ModelVolumePtrs                                           model_volumes,
    const std::vector<PrintObjectRegions::LayerRangeRegions> &layer_ranges,
    const std::vector<float>                                 &zs,
    const std::function<void()>                              &throw_on_cancel_callback)
{
    model_volumes_sort_by_id(model_volumes);

    std::vector<VolumeSlices> out;
    out.reserve(model_volumes.size());

    std::vector<t_layer_height_range> slicing_ranges;
    if (layer_ranges.size() > 1)
        slicing_ranges.reserve(layer_ranges.size());

    MeshSlicingParamsEx params_base;
    params_base.closing_radius = print_object_config.slice_closing_radius.value;
    params_base.extra_offset   = 0;
    params_base.trafo          = object_trafo;
    //BBS: 0.0025mm is safe enough to simplify the data to speed slicing up for high-resolution model.
    //Also has on influence on arc fitting which has default resolution 0.0125mm.
    params_base.resolution     = 0.0025;
    switch (print_object_config.slicing_mode.value) {
    case SlicingMode::Regular:    params_base.mode = MeshSlicingParams::SlicingMode::Regular; break;
    case SlicingMode::EvenOdd:    params_base.mode = MeshSlicingParams::SlicingMode::EvenOdd; break;
    case SlicingMode::CloseHoles: params_base.mode = MeshSlicingParams::SlicingMode::Positive; break;
    }

    params_base.mode_below     = params_base.mode;

    // BBS
    const size_t num_extruders = print_config.filament_diameter.size();
    const bool   is_mm_painted = num_extruders > 1 && std::any_of(model_volumes.cbegin(), model_volumes.cend(), [](const ModelVolume *mv) { return mv->is_mm_painted(); });
    // BBS: don't do size compensation when slice volume.
    // Will handle contour and hole size compensation seperately later.
    //const auto   extra_offset  = is_mm_painted ? 0.f : std::max(0.f, float(print_object_config.xy_contour_compensation.value));
    const auto   extra_offset = 0.f;

    for (const ModelVolume *model_volume : model_volumes)
        if (model_volume_needs_slicing(*model_volume)) {
            MeshSlicingParamsEx params { params_base };
            if (! model_volume->is_negative_volume())
                params.extra_offset = extra_offset;
            if (layer_ranges.size() == 1) {
                if (const PrintObjectRegions::LayerRangeRegions &layer_range = layer_ranges.front(); layer_range.has_volume(model_volume->id())) {
                    if (model_volume->is_model_part() && print_config.spiral_mode) {
                        auto it = std::find_if(layer_range.volume_regions.begin(), layer_range.volume_regions.end(),
                            [model_volume](const auto &slice){ return model_volume == slice.model_volume; });
                        params.mode = MeshSlicingParams::SlicingMode::PositiveLargestContour;
                        // Slice the bottom layers with SlicingMode::Regular.
                        // This needs to be in sync with LayerRegion::make_perimeters() spiral_mode!
                        const PrintRegionConfig &region_config = it->region->config();
                        params.slicing_mode_normal_below_layer = size_t(region_config.bottom_shell_layers.value);
                        for (; params.slicing_mode_normal_below_layer < zs.size() && zs[params.slicing_mode_normal_below_layer] < region_config.bottom_shell_thickness - EPSILON;
                            ++ params.slicing_mode_normal_below_layer);
                    }
                    out.push_back({
                        model_volume->id(),
                        slice_volume(*model_volume, zs, params, throw_on_cancel_callback)
                    });
                }
            } else {
                assert(! print_config.spiral_mode);
                slicing_ranges.clear();
                for (const PrintObjectRegions::LayerRangeRegions &layer_range : layer_ranges)
                    if (layer_range.has_volume(model_volume->id()))
                        slicing_ranges.emplace_back(layer_range.layer_height_range);
                if (! slicing_ranges.empty())
                    out.push_back({
                        model_volume->id(),
                        slice_volume(*model_volume, zs, slicing_ranges, params, throw_on_cancel_callback)
                    });
            }
            if (! out.empty() && out.back().slices.empty())
                out.pop_back();
        }

    return out;
}

static inline VolumeSlices& volume_slices_find_by_id(std::vector<VolumeSlices> &volume_slices, const ObjectID id)
{
    auto it = lower_bound_by_predicate(volume_slices.begin(), volume_slices.end(), [id](const VolumeSlices &vs) { return vs.volume_id < id; });
    assert(it != volume_slices.end() && it->volume_id == id);
    return *it;
}

static inline bool overlap_in_xy(const PrintObjectRegions::BoundingBox &l, const PrintObjectRegions::BoundingBox &r)
{
    return ! (l.max().x() < r.min().x() || l.min().x() > r.max().x() ||
              l.max().y() < r.min().y() || l.min().y() > r.max().y());
}

static std::vector<PrintObjectRegions::LayerRangeRegions>::const_iterator layer_range_first(const std::vector<PrintObjectRegions::LayerRangeRegions> &layer_ranges, double z)
{
    auto  it = lower_bound_by_predicate(layer_ranges.begin(), layer_ranges.end(),
        [z](const PrintObjectRegions::LayerRangeRegions &lr) { return lr.layer_height_range.second < z; });
    assert(it != layer_ranges.end() && it->layer_height_range.first <= z && z <= it->layer_height_range.second);
    if (z == it->layer_height_range.second)
        if (auto it_next = it; ++ it_next != layer_ranges.end() && it_next->layer_height_range.first == z)
            it = it_next;
    assert(it != layer_ranges.end() && it->layer_height_range.first <= z && z <= it->layer_height_range.second);
    return it;
}

static std::vector<PrintObjectRegions::LayerRangeRegions>::const_iterator layer_range_next(
    const std::vector<PrintObjectRegions::LayerRangeRegions>            &layer_ranges,
    std::vector<PrintObjectRegions::LayerRangeRegions>::const_iterator   it,
    double                                                               z)
{
    for (; it->layer_height_range.second <= z; ++ it)
        assert(it != layer_ranges.end());
    assert(it != layer_ranges.end() && it->layer_height_range.first <= z && z < it->layer_height_range.second);
    return it;
}

static std::vector<std::vector<ExPolygons>> slices_to_regions(
    ModelVolumePtrs                                           model_volumes,
    const PrintObjectRegions                                 &print_object_regions,
    const std::vector<float>                                 &zs,
    std::vector<VolumeSlices>                               &&volume_slices,
    // If clipping is disabled, then ExPolygons produced by different volumes will never be merged, thus they will be allowed to overlap.
    // It is up to the model designer to handle these overlaps.
    const bool                                                clip_multipart_objects,
    const std::function<void()>                              &throw_on_cancel_callback)
{
    model_volumes_sort_by_id(model_volumes);

    std::vector<std::vector<ExPolygons>> slices_by_region(print_object_regions.all_regions.size(), std::vector<ExPolygons>(zs.size(), ExPolygons()));

    // First shuffle slices into regions if there is no overlap with another region possible, collect zs of the complex cases.
    std::vector<std::pair<size_t, float>> zs_complex;
    {
        size_t z_idx = 0;
        for (const PrintObjectRegions::LayerRangeRegions &layer_range : print_object_regions.layer_ranges) {
            for (; z_idx < zs.size() && zs[z_idx] < layer_range.layer_height_range.first; ++ z_idx) ;
            if (layer_range.volume_regions.empty()) {
            } else if (layer_range.volume_regions.size() == 1) {
                const ModelVolume *model_volume = layer_range.volume_regions.front().model_volume;
                assert(model_volume != nullptr);
                if (model_volume->is_model_part()) {
                    VolumeSlices &slices_src = volume_slices_find_by_id(volume_slices, model_volume->id());
                    auto         &slices_dst = slices_by_region[layer_range.volume_regions.front().region->print_object_region_id()];
                    for (; z_idx < zs.size() && zs[z_idx] < layer_range.layer_height_range.second; ++ z_idx)
                        slices_dst[z_idx] = std::move(slices_src.slices[z_idx]);
                }
            } else {
                zs_complex.reserve(zs.size());
                for (; z_idx < zs.size() && zs[z_idx] < layer_range.layer_height_range.second; ++ z_idx) {
                    float z                          = zs[z_idx];
                    int   idx_first_printable_region = -1;
                    bool  complex                    = false;
                    for (int idx_region = 0; idx_region < int(layer_range.volume_regions.size()); ++ idx_region) {
                        const PrintObjectRegions::VolumeRegion &region = layer_range.volume_regions[idx_region];
                        if (region.bbox->min().z() <= z && region.bbox->max().z() >= z) {
                            if (idx_first_printable_region == -1 && region.model_volume->is_model_part())
                                idx_first_printable_region = idx_region;
                            else if (idx_first_printable_region != -1) {
                                // Test for overlap with some other region.
                                for (int idx_region2 = idx_first_printable_region; idx_region2 < idx_region; ++ idx_region2) {
                                    const PrintObjectRegions::VolumeRegion &region2 = layer_range.volume_regions[idx_region2];
                                    if (region2.bbox->min().z() <= z && region2.bbox->max().z() >= z && overlap_in_xy(*region.bbox, *region2.bbox)) {
                                        complex = true;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                    if (complex)
                        zs_complex.push_back({ z_idx, z });
                    else if (idx_first_printable_region >= 0) {
                        const PrintObjectRegions::VolumeRegion &region = layer_range.volume_regions[idx_first_printable_region];
                        slices_by_region[region.region->print_object_region_id()][z_idx] = std::move(volume_slices_find_by_id(volume_slices, region.model_volume->id()).slices[z_idx]);
                    }
                }
            }
            throw_on_cancel_callback();
        }
    }

    // Second perform region clipping and assignment in parallel.
    if (! zs_complex.empty()) {
        std::vector<std::vector<VolumeSlices*>> layer_ranges_regions_to_slices(print_object_regions.layer_ranges.size(), std::vector<VolumeSlices*>());
        for (const PrintObjectRegions::LayerRangeRegions &layer_range : print_object_regions.layer_ranges) {
            std::vector<VolumeSlices*> &layer_range_regions_to_slices = layer_ranges_regions_to_slices[&layer_range - print_object_regions.layer_ranges.data()];
            layer_range_regions_to_slices.reserve(layer_range.volume_regions.size());
            for (const PrintObjectRegions::VolumeRegion &region : layer_range.volume_regions)
                layer_range_regions_to_slices.push_back(&volume_slices_find_by_id(volume_slices, region.model_volume->id()));
        }
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, zs_complex.size()),
            [&slices_by_region, &print_object_regions, &zs_complex, &layer_ranges_regions_to_slices, clip_multipart_objects, &throw_on_cancel_callback]
                (const tbb::blocked_range<size_t> &range) {
                float z              = zs_complex[range.begin()].second;
                auto  it_layer_range = layer_range_first(print_object_regions.layer_ranges, z);
                // Per volume_regions slices at this Z height.
                struct RegionSlice {
                    ExPolygons  expolygons;
                    // Identifier of this region in PrintObjectRegions::all_regions
                    int         region_id;
                    ObjectID    volume_id;
                    bool operator<(const RegionSlice &rhs) const {
                        bool this_empty = this->region_id < 0 || this->expolygons.empty();
                        bool rhs_empty  = rhs.region_id < 0 || rhs.expolygons.empty();
                        // Sort the empty items to the end of the list.
                        // Sort by region_id & volume_id lexicographically.
                        return ! this_empty && (rhs_empty || (this->region_id < rhs.region_id || (this->region_id == rhs.region_id && volume_id < volume_id)));
                    }
                };

                // BBS
                auto trim_overlap = [](ExPolygons& expolys_a, ExPolygons& expolys_b) {
                    ExPolygons trimming_a;
                    ExPolygons trimming_b;

                    for (ExPolygon& expoly_a : expolys_a) {
                        BoundingBox bbox_a = get_extents(expoly_a);
                        ExPolygons expolys_new;
                        for (ExPolygon& expoly_b : expolys_b) {
                            BoundingBox bbox_b = get_extents(expoly_b);
                            if (!bbox_a.overlap(bbox_b))
                                continue;

                            if (intersection_ex(expoly_a, expoly_b).empty())
                                continue;

                            ExPolygons temp = intersection_ex(expoly_b, expoly_a);
                            if (expoly_a.area() > expoly_b.area())
                                trimming_a.insert(trimming_a.end(), temp.begin(), temp.end());
                            else
                                trimming_b.insert(trimming_b.end(), temp.begin(), temp.end());
                        }
                    }

                    expolys_a = diff_ex(expolys_a, trimming_a);
                    expolys_b = diff_ex(expolys_b, trimming_b);
                };

                std::vector<RegionSlice> temp_slices;
                for (size_t zs_complex_idx = range.begin(); zs_complex_idx < range.end(); ++ zs_complex_idx) {
                    auto [z_idx, z] = zs_complex[zs_complex_idx];
                    it_layer_range = layer_range_next(print_object_regions.layer_ranges, it_layer_range, z);
                    const PrintObjectRegions::LayerRangeRegions &layer_range = *it_layer_range;
                    {
                        std::vector<VolumeSlices*> &layer_range_regions_to_slices = layer_ranges_regions_to_slices[it_layer_range - print_object_regions.layer_ranges.begin()];
                        // Per volume_regions slices at thiz Z height.
                        temp_slices.clear();
                        temp_slices.reserve(layer_range.volume_regions.size());
                        for (VolumeSlices* &slices : layer_range_regions_to_slices) {
                            const PrintObjectRegions::VolumeRegion &volume_region = layer_range.volume_regions[&slices - layer_range_regions_to_slices.data()];
                            temp_slices.push_back({ std::move(slices->slices[z_idx]), volume_region.region ? volume_region.region->print_object_region_id() : -1, volume_region.model_volume->id() });
                        }
                    }
                    for (int idx_region = 0; idx_region < int(layer_range.volume_regions.size()); ++ idx_region)
                        if (! temp_slices[idx_region].expolygons.empty()) {
                            const PrintObjectRegions::VolumeRegion &region = layer_range.volume_regions[idx_region];
                            if (region.model_volume->is_modifier()) {
                                assert(region.parent > -1);
                                bool next_region_same_modifier = idx_region + 1 < int(temp_slices.size()) && layer_range.volume_regions[idx_region + 1].model_volume == region.model_volume;
                                RegionSlice &parent_slice = temp_slices[region.parent];
                                RegionSlice &this_slice   = temp_slices[idx_region];
                                ExPolygons   source       = std::move(this_slice.expolygons);
                                if (parent_slice.expolygons.empty()) {
                                    this_slice  .expolygons.clear();
                                } else {
                                    this_slice  .expolygons = intersection_ex(parent_slice.expolygons, source);
                                    parent_slice.expolygons = diff_ex        (parent_slice.expolygons, source);
                                }
                                if (next_region_same_modifier)
                                    // To be used in the following iteration.
                                    temp_slices[idx_region + 1].expolygons = std::move(source);
                            } else if ((region.model_volume->is_model_part() && clip_multipart_objects) || region.model_volume->is_negative_volume()) {
                                // Clip every non-zero region preceding it.
                                for (int idx_region2 = 0; idx_region2 < idx_region; ++ idx_region2)
                                    if (! temp_slices[idx_region2].expolygons.empty()) {
                                        const PrintObjectRegions::VolumeRegion& region2 = layer_range.volume_regions[idx_region2];
                                        if (!region2.model_volume->is_negative_volume() && overlap_in_xy(*region.bbox, *region2.bbox))
                                            //BBS: handle negative_volume seperately, always minus the negative volume and don't need to trim overlap
                                            if (!region.model_volume->is_negative_volume())
                                                trim_overlap(temp_slices[idx_region2].expolygons, temp_slices[idx_region].expolygons);
                                            else
                                                temp_slices[idx_region2].expolygons = diff_ex(temp_slices[idx_region2].expolygons, temp_slices[idx_region].expolygons);
                                    }
                            }
                        }
                    // Sort by region_id, push empty slices to the end.
                    std::sort(temp_slices.begin(), temp_slices.end());
                    // Remove the empty slices.
                    temp_slices.erase(std::find_if(temp_slices.begin(), temp_slices.end(), [](const auto &slice) { return slice.region_id == -1 || slice.expolygons.empty(); }), temp_slices.end());
                    // Merge slices and store them to the output.
                    for (int i = 0; i < int(temp_slices.size());) {
                        // Find a range of temp_slices with the same region_id.
                        int j = i;
                        bool merged = false;
                        ExPolygons &expolygons = temp_slices[i].expolygons;
                        for (++ j; j < int(temp_slices.size()) && temp_slices[i].region_id == temp_slices[j].region_id; ++ j)
                            if (ExPolygons &expolygons2 = temp_slices[j].expolygons; ! expolygons2.empty()) {
                                if (expolygons.empty()) {
                                    expolygons = std::move(expolygons2);
                                } else {
                                    append(expolygons, std::move(expolygons2));
                                    merged = true;
                                }
                            }
                        // Don't unite the regions if ! clip_multipart_objects. In that case it is user's responsibility
                        // to handle region overlaps. Indeed, one may intentionally let the regions overlap to produce crossing perimeters
                        // for example.
                        if (merged && clip_multipart_objects)
                            expolygons = closing_ex(expolygons, float(scale_(EPSILON)));
                        slices_by_region[temp_slices[i].region_id][z_idx] = std::move(expolygons);
                        i = j;
                    }
                    throw_on_cancel_callback();
                }
            });
    }

    return slices_by_region;
}

std::string fix_slicing_errors(PrintObject* object, LayerPtrs &layers, const std::function<void()> &throw_if_canceled)
{
    std::string error_msg;//BBS

    if (layers.size() == 0) return error_msg;

    // Collect layers with slicing errors.
    // These layers will be fixed in parallel.
    std::vector<size_t> buggy_layers;
    buggy_layers.reserve(layers.size());
    // BBS: get largest external perimenter width of all layers
    auto get_ext_peri_width = [](Layer* layer) {return layer->m_regions.empty() ? 0 : layer->m_regions[0]->flow(frExternalPerimeter).scaled_width(); };
    auto it = std::max_element(layers.begin(), layers.end(), [get_ext_peri_width](auto& a, auto& b) {return get_ext_peri_width(a) < get_ext_peri_width(b); });
    coord_t thresh = get_ext_peri_width(*it) * 0.5;// half of external perimeter width  // 0.5 * scale_(this->config().line_width);
    for (size_t idx_layer = 0; idx_layer < layers.size(); ++idx_layer) {
        // BBS: detect empty layers (layers with very small regions) and mark them as problematic, then these layers will copy the nearest good layer
        auto layer = layers[idx_layer];
        ExPolygons lslices;
        for (size_t region_id = 0; region_id < layer->m_regions.size(); ++region_id) {
            LayerRegion* layerm = layer->m_regions[region_id];
            for (auto& surface : layerm->slices.surfaces) {
                auto expoly = offset_ex(surface.expolygon, -thresh);
                lslices.insert(lslices.begin(), expoly.begin(), expoly.end());
            }
        }
        if (lslices.empty()) {
            layer->slicing_errors = true;
        }

        if (layers[idx_layer]->slicing_errors) {
            buggy_layers.push_back(idx_layer);
        }
        else
            break; // only detect empty layers near bed
    }

    BOOST_LOG_TRIVIAL(debug) << "Slicing objects - fixing slicing errors in parallel - begin";
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, buggy_layers.size()),
        [&layers, &throw_if_canceled, &buggy_layers, &error_msg](const tbb::blocked_range<size_t>& range) {
            for (size_t buggy_layer_idx = range.begin(); buggy_layer_idx < range.end(); ++ buggy_layer_idx) {
                throw_if_canceled();
                size_t idx_layer = buggy_layers[buggy_layer_idx];
                // BBS: only replace empty layers lower than 1mm
                const coordf_t thresh_empty_layer_height = 1;
                Layer* layer = layers[idx_layer];
                if (layer->print_z>= thresh_empty_layer_height)
                    continue;
                assert(layer->slicing_errors);
                // Try to repair the layer surfaces by merging all contours and all holes from neighbor layers.
                // BOOST_LOG_TRIVIAL(trace) << "Attempting to repair layer" << idx_layer;
                for (size_t region_id = 0; region_id < layer->region_count(); ++ region_id) {
                    LayerRegion *layerm = layer->get_region(region_id);
                    // Find the first valid layer below / above the current layer.
                    const Surfaces *upper_surfaces = nullptr;
                    const Surfaces *lower_surfaces = nullptr;
                    //BBS: only repair empty layers lowers than 1mm
                    for (size_t j = idx_layer + 1; j < layers.size(); ++j) {
                        if (!layers[j]->slicing_errors) {
                            upper_surfaces = &layers[j]->regions()[region_id]->slices.surfaces;
                            break;
                        }
                        if (layers[j]->print_z >= thresh_empty_layer_height) break;
                    }
                    for (int j = int(idx_layer) - 1; j >= 0; --j) {
                        if (layers[j]->print_z >= thresh_empty_layer_height) continue;
                        if (!layers[j]->slicing_errors) {
                            lower_surfaces = &layers[j]->regions()[region_id]->slices.surfaces;
                            break;
                        }
                    }
                    // Collect outer contours and holes from the valid layers above & below.
                    ExPolygons expolys;
                    expolys.reserve(
                        ((upper_surfaces == nullptr) ? 0 : upper_surfaces->size()) +
                        ((lower_surfaces == nullptr) ? 0 : lower_surfaces->size()));
                    if (upper_surfaces)
                        for (const auto &surface : *upper_surfaces) {
                            expolys.emplace_back(surface.expolygon);
                        }
                    if (lower_surfaces)
                        for (const auto &surface : *lower_surfaces) {
                            expolys.emplace_back(surface.expolygon);
                        }
                    if (!expolys.empty()) {
                        //BBS
                        error_msg = L("Empty layers around bottom are replaced by nearest normal layers.");
                        layerm->slices.set(union_ex(expolys), stInternal);
                    }
                }
                // Update layer slices after repairing the single regions.
                layer->make_slices();
            }
        });
    throw_if_canceled();
    BOOST_LOG_TRIVIAL(debug) << "Slicing objects - fixing slicing errors in parallel - end";

    // remove empty layers from bottom
    while (! layers.empty() && (layers.front()->lslices.empty() || layers.front()->empty())) {
        delete layers.front();
        layers.erase(layers.begin());
        layers.front()->lower_layer = nullptr;
        for (size_t i = 0; i < layers.size(); ++ i)
            layers[i]->set_id(layers[i]->id() - 1);
    }

    //BBS
    if(error_msg.empty() && !buggy_layers.empty())
        error_msg = L("The model has too many empty layers.");
    return error_msg;
}

// Called by make_perimeters()
// 1) Decides Z positions of the layers,
// 2) Initializes layers and their regions
// 3) Slices the object meshes
// 4) Slices the modifier meshes and reclassifies the slices of the object meshes by the slices of the modifier meshes
// 5) Applies size compensation (offsets the slices in XY plane)
// 6) Replaces bad slices by the slices reconstructed from the upper/lower layer
// Resulting expolygons of layer regions are marked as Internal.
void PrintObject::slice()
{
    if (! this->set_started(posSlice))
        return;
    //BBS: add flag to reload scene for shell rendering
    m_print->set_status(5, L("Slicing mesh"), PrintBase::SlicingStatus::RELOAD_SCENE);
    std::vector<coordf_t> layer_height_profile;
    this->update_layer_height_profile(*this->model_object(), m_slicing_params, layer_height_profile);
    m_print->throw_if_canceled();
    m_typed_slices = false;
    this->clear_layers();
    m_layers = new_layers(this, generate_object_layers(m_slicing_params, layer_height_profile));
    this->slice_volumes();
    m_print->throw_if_canceled();
    // Fix the model.
    //FIXME is this the right place to do? It is done repeateadly at the UI and now here at the backend.
    std::string warning = fix_slicing_errors(this, m_layers, [this](){ m_print->throw_if_canceled(); });
    m_print->throw_if_canceled();
    //BBS: send warning message to slicing callback
    if (!warning.empty()) {
        BOOST_LOG_TRIVIAL(info) << warning;
        this->active_step_add_warning(PrintStateBase::WarningLevel::CRITICAL, warning, PrintStateBase::SlicingReplaceInitEmptyLayers);
    }
    // Update bounding boxes, back up raw slices of complex models.
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, m_layers.size()),
        [this](const tbb::blocked_range<size_t>& range) {
            for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
                m_print->throw_if_canceled();
                Layer &layer = *m_layers[layer_idx];
                layer.lslices_bboxes.clear();
                layer.lslices_bboxes.reserve(layer.lslices.size());
                for (const ExPolygon &expoly : layer.lslices)
                	layer.lslices_bboxes.emplace_back(get_extents(expoly));
                layer.backup_untyped_slices();
            }
        });
    if (m_layers.empty())
        throw Slic3r::SlicingError("No layers were detected. You might want to repair your STL file(s) or check their size or thickness and retry.\n");

    // BBS
    this->set_done(posSlice);
}

template<typename ThrowOnCancel>
static inline void apply_mm_segmentation(PrintObject &print_object, ThrowOnCancel throw_on_cancel)
{
    // Returns MMU segmentation based on painting in MMU segmentation gizmo
    std::vector<std::vector<ExPolygons>> segmentation = multi_material_segmentation_by_painting(print_object, throw_on_cancel);
    assert(segmentation.size() == print_object.layer_count());
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, segmentation.size(), std::max(segmentation.size() / 128, size_t(1))),
        [&print_object, &segmentation, throw_on_cancel](const tbb::blocked_range<size_t> &range) {
            const auto  &layer_ranges   = print_object.shared_regions()->layer_ranges;
            double       z              = print_object.get_layer(range.begin())->slice_z;
            auto         it_layer_range = layer_range_first(layer_ranges, z);
            // BBS
            const size_t num_extruders = print_object.print()->config().filament_diameter.size();
            struct ByExtruder {
                ExPolygons  expolygons;
                BoundingBox bbox;
            };
            std::vector<ByExtruder> by_extruder;
            struct ByRegion {
                ExPolygons  expolygons;
                bool        needs_merge { false };
            };
            std::vector<ByRegion> by_region;
            for (size_t layer_id = range.begin(); layer_id < range.end(); ++ layer_id) {
                throw_on_cancel();
                Layer *layer = print_object.get_layer(layer_id);
                it_layer_range = layer_range_next(layer_ranges, it_layer_range, layer->slice_z);
                const PrintObjectRegions::LayerRangeRegions &layer_range = *it_layer_range;
                // Gather per extruder expolygons.
                by_extruder.assign(num_extruders, ByExtruder());
                by_region.assign(layer->region_count(), ByRegion());
                bool layer_split = false;
                for (size_t extruder_id = 0; extruder_id < num_extruders; ++ extruder_id) {
                    ByExtruder &region = by_extruder[extruder_id];
                    append(region.expolygons, std::move(segmentation[layer_id][extruder_id]));
                    if (! region.expolygons.empty()) {
                        region.bbox = get_extents(region.expolygons);
                        layer_split = true;
                    }
                }
                if (! layer_split)
                    continue;
                // Split LayerRegions by by_extruder regions.
                // layer_range.painted_regions are sorted by extruder ID and parent PrintObject region ID.
                auto it_painted_region = layer_range.painted_regions.begin();
                for (int region_id = 0; region_id < int(layer->region_count()); ++ region_id)
                    if (LayerRegion &layerm = *layer->get_region(region_id); ! layerm.slices.surfaces.empty()) {
                        assert(layerm.region().print_object_region_id() == region_id);
                        const BoundingBox bbox = get_extents(layerm.slices.surfaces);
                        assert(it_painted_region < layer_range.painted_regions.end());
                        // Find the first it_painted_region which overrides this region.
                        for (; layer_range.volume_regions[it_painted_region->parent].region->print_object_region_id() < region_id; ++ it_painted_region)
                            assert(it_painted_region != layer_range.painted_regions.end());
                        assert(it_painted_region != layer_range.painted_regions.end());
                        assert(layer_range.volume_regions[it_painted_region->parent].region == &layerm.region());
                        // 1-based extruder ID
                        bool   self_trimmed = false;
                        int    self_extruder_id = -1;
                        for (int extruder_id = 1; extruder_id <= int(by_extruder.size()); ++ extruder_id)
                            if (ByExtruder &segmented = by_extruder[extruder_id - 1]; segmented.bbox.defined && bbox.overlap(segmented.bbox)) {
                                // Find the target region.
                                for (; int(it_painted_region->extruder_id) < extruder_id; ++ it_painted_region)
                                    assert(it_painted_region != layer_range.painted_regions.end());
                                assert(layer_range.volume_regions[it_painted_region->parent].region == &layerm.region() && int(it_painted_region->extruder_id) == extruder_id);
                                //FIXME Don't trim by self, it is not reliable.
                                if (&layerm.region() == it_painted_region->region) {
                                    self_extruder_id = extruder_id;
                                    continue;
                                }
                                // Steal from this region.
                                int         target_region_id = it_painted_region->region->print_object_region_id();
                                ExPolygons  stolen           = intersection_ex(layerm.slices.surfaces, segmented.expolygons);
                                if (! stolen.empty()) {
                                    ByRegion &dst = by_region[target_region_id];
                                    if (dst.expolygons.empty()) {
                                        dst.expolygons = std::move(stolen);
                                    } else {
                                        append(dst.expolygons, std::move(stolen));
                                        dst.needs_merge = true;
                                    }
                                }
#if 0
                                if (&layerm.region() == it_painted_region->region)
                                    // Slices of this LayerRegion were trimmed by a MMU region of the same PrintRegion.
                                    self_trimmed = true;
#endif
                            }
                        if (! self_trimmed) {
                            // Trim slices of this LayerRegion with all the MMU regions.
                            Polygons mine = to_polygons(std::move(layerm.slices.surfaces));
                            for (auto &segmented : by_extruder)
                                if (&segmented - by_extruder.data() + 1 != self_extruder_id && segmented.bbox.defined && bbox.overlap(segmented.bbox)) {
                                    mine = diff(mine, segmented.expolygons);
                                    if (mine.empty())
                                        break;
                                }
                            // Filter out unprintable polygons produced by subtraction multi-material painted regions from layerm.region().
                            // ExPolygon returned from multi-material segmentation does not precisely match ExPolygons in layerm.region()
                            // (because of preprocessing of the input regions in multi-material segmentation). Therefore, subtraction from
                            // layerm.region() could produce a huge number of small unprintable regions for the model's base extruder.
                            // This could, on some models, produce bulges with the model's base color (#7109).
                            if (! mine.empty())
                                mine = opening(union_ex(mine), float(scale_(5 * EPSILON)), float(scale_(5 * EPSILON)));
                            if (! mine.empty()) {
                                ByRegion &dst = by_region[layerm.region().print_object_region_id()];
                                if (dst.expolygons.empty()) {
                                    dst.expolygons = union_ex(mine);
                                } else {
                                    append(dst.expolygons, union_ex(mine));
                                    dst.needs_merge = true;
                                }
                            }
                        }
                    }
                // Re-create Surfaces of LayerRegions.
                for (size_t region_id = 0; region_id < layer->region_count(); ++ region_id) {
                    ByRegion &src = by_region[region_id];
                    if (src.needs_merge)
                        // Multiple regions were merged into one.
                        src.expolygons = closing_ex(src.expolygons, float(scale_(10 * EPSILON)));
                    layer->get_region(region_id)->slices.set(std::move(src.expolygons), stInternal);
                }
            }
        });
}

//BBS: justify whether a volume is connected to another one
bool doesVolumeIntersect(VolumeSlices& vs1, VolumeSlices& vs2)
{
    if (vs1.volume_id == vs2.volume_id) return true;
    if (vs1.slices.size() != vs2.slices.size()) return false;

    for (int i = 0; i != vs1.slices.size(); ++i) {

        if (vs1.slices[i].empty()) continue;
        if (!vs2.slices[i].empty() && !intersection_ex(vs1.slices[i], vs2.slices[i]).empty()) return true;
        if (i + 1 != vs2.slices.size() && !vs2.slices[i + 1].empty()) {
            if (!intersection_ex(vs1.slices[i], vs2.slices[i + 1]).empty()) return true;
        }
        if (i - 1 >= 0 && !vs2.slices[i - 1].empty()) {
            if (!intersection_ex(vs1.slices[i], vs2.slices[i - 1]).empty()) return true;
        }
    }
    return false;
}

//BBS: grouping the volumes of an object according to their connection relationship
bool groupingVolumes(std::vector<VolumeSlices> objSliceByVolume, std::vector<groupedVolumeSlices>& groups, double resolution)
{
    int existGroups = 0;
    std::vector<int> groupIndex(objSliceByVolume.size(), -1);

    double offsetValue = 0.15 / SCALING_FACTOR;

    for (int i = 0; i != objSliceByVolume.size(); ++i) {
        for (int j = 0; j != objSliceByVolume[i].slices.size(); ++j) {
            objSliceByVolume[i].slices[j] = offset_ex(objSliceByVolume[i].slices[j], offsetValue);
            for (ExPolygon& poly_ex : objSliceByVolume[i].slices[j])
                poly_ex.douglas_peucker(resolution);
        }
    }

    for (int i = 0; i != objSliceByVolume.size(); ++i) {
        if (groupIndex[i] < 0) {
            groupIndex[i] = i;
            ++existGroups;
        }
        for (int j = i + 1; j != objSliceByVolume.size(); ++j) {
            if (doesVolumeIntersect(objSliceByVolume[i], objSliceByVolume[j])) {
                if (groupIndex[j] < 0) groupIndex[j] = groupIndex[i];
                if (groupIndex[j] != groupIndex[i]) {
                    int retain = std::min(groupIndex[i], groupIndex[j]);
                    int cover = std::max(groupIndex[i], groupIndex[j]);
                    for (int k = 0; k != objSliceByVolume.size(); ++k) {
                        if (groupIndex[k] == cover) groupIndex[k] = retain;
                    }
                    --existGroups;
                }
            }

        }
    }

    std::vector<int> groupVector{};
    for (int gi : groupIndex) {
        bool exist = false;
        for (int gv : groupVector) {
            if (gv == gi) {
                exist = true;
                break;
            }
        }
        if (!exist) groupVector.push_back(gi);
    }

    // group volumes and their slices according to the grouping Vector
    groups.clear();

    for (int gv : groupVector) {
        groupedVolumeSlices gvs;
        gvs.groupId = gv;
        for (int i = 0; i != objSliceByVolume.size(); ++i) {
            if (groupIndex[i] == gv) {
                gvs.volume_ids.push_back(objSliceByVolume[i].volume_id);
                append(gvs.slices, objSliceByVolume[i].slices.front());
            }
        }

        // the slices of a group should be unioned
        gvs.slices = offset_ex(union_ex(gvs.slices), -offsetValue);
        for (ExPolygon& poly_ex : gvs.slices)
            poly_ex.douglas_peucker(resolution);

        groups.push_back(gvs);
    }
    return true;
}

//BBS: filter the members of "objSliceByVolume" such that only "model_part" are included
std::vector<VolumeSlices> findPartVolumes(const std::vector<VolumeSlices>& objSliceByVolume, ModelVolumePtrs model_volumes) {
    std::vector<VolumeSlices> outPut;
    for (const auto& vs : objSliceByVolume) {
        for (const auto& mv : model_volumes) {
            if (vs.volume_id == mv->id() && mv->is_model_part()) outPut.push_back(vs);
        }
    }
    return outPut;
}

void applyNegtiveVolumes(ModelVolumePtrs model_volumes, const std::vector<VolumeSlices>& objSliceByVolume, std::vector<groupedVolumeSlices>& groups, double resolution) {
    ExPolygons negTotal;
    for (const auto& vs : objSliceByVolume) {
        for (const auto& mv : model_volumes) {
            if (vs.volume_id == mv->id() && mv->is_negative_volume()) {
                if (vs.slices.size() > 0) {
                    append(negTotal, vs.slices.front());
                }
            }
        }
    }

    for (auto& g : groups) {
        g.slices = diff_ex(g.slices, negTotal);
        for (ExPolygon& poly_ex : g.slices)
            poly_ex.douglas_peucker(resolution);
    }
}
// 1) Decides Z positions of the layers,
// 2) Initializes layers and their regions
// 3) Slices the object meshes
// 4) Slices the modifier meshes and reclassifies the slices of the object meshes by the slices of the modifier meshes
// 5) Applies size compensation (offsets the slices in XY plane)
// 6) Replaces bad slices by the slices reconstructed from the upper/lower layer
// Resulting expolygons of layer regions are marked as Internal.
//
// this should be idempotent
void PrintObject::slice_volumes()
{
    BOOST_LOG_TRIVIAL(info) << "Slicing volumes..." << log_memory_info();
    const Print *print                      = this->print();
    const auto   throw_on_cancel_callback   = std::function<void()>([print](){ print->throw_if_canceled(); });

    // Clear old LayerRegions, allocate for new PrintRegions.
    for (Layer* layer : m_layers) {
        //BBS: should delete all LayerRegionPtr to avoid memory leak
        while (!layer->m_regions.empty()) {
            if (layer->m_regions.back())
                delete layer->m_regions.back();
            layer->m_regions.pop_back();
        }
        layer->m_regions.reserve(m_shared_regions->all_regions.size());
        for (const std::unique_ptr<PrintRegion> &pr : m_shared_regions->all_regions)
            layer->m_regions.emplace_back(new LayerRegion(layer, pr.get()));
    }
    // BBS: first layer slices are sorted by volume
    std::vector<float>                   slice_zs      = zs_from_layers(m_layers);
    if (!slice_zs.empty()) {
        firstLayerObjSliceByVolume = slice_volumes_inner(
            print->config(), this->config(), this->trafo_centered(),
            this->model_object()->volumes, m_shared_regions->layer_ranges, {slice_zs.front()}, throw_on_cancel_callback);
    }

    std::vector<VolumeSlices> objSliceByVolume;
    if (!slice_zs.empty()) {
        objSliceByVolume = slice_volumes_inner(
            print->config(), this->config(), this->trafo_centered(),
            this->model_object()->volumes, m_shared_regions->layer_ranges, slice_zs, throw_on_cancel_callback);
    }

    //BBS: "model_part" volumes are grouded according to their connections
    const auto           scaled_resolution = scaled<double>(print->config().resolution.value);
    std::vector<VolumeSlices> objSliceByVolumeParts = findPartVolumes(objSliceByVolume, this->model_object()->volumes);
    groupingVolumes(objSliceByVolumeParts, firstLayerObjSliceByGroups, scaled_resolution);
    applyNegtiveVolumes(this->model_object()->volumes, objSliceByVolume, firstLayerObjSliceByGroups, scaled_resolution);

    std::vector<std::vector<ExPolygons>> region_slices = slices_to_regions(this->model_object()->volumes, *m_shared_regions, slice_zs,
        std::move(objSliceByVolume),
        PrintObject::clip_multipart_objects,
        throw_on_cancel_callback);

    for (size_t region_id = 0; region_id < region_slices.size(); ++ region_id) {
        std::vector<ExPolygons> &by_layer = region_slices[region_id];
        for (size_t layer_id = 0; layer_id < by_layer.size(); ++ layer_id)
            m_layers[layer_id]->regions()[region_id]->slices.append(std::move(by_layer[layer_id]), stInternal);
    }
    region_slices.clear();

    BOOST_LOG_TRIVIAL(debug) << "Slicing volumes - removing top empty layers";
    while (! m_layers.empty()) {
        const Layer *layer = m_layers.back();
        if (! layer->empty())
            break;
        delete layer;
        m_layers.pop_back();
    }
    if (! m_layers.empty())
        m_layers.back()->upper_layer = nullptr;
    m_print->throw_if_canceled();

    // Is any ModelVolume MMU painted?
    if (const auto& volumes = this->model_object()->volumes;
        m_print->config().filament_diameter.size() > 1 && // BBS
        std::find_if(volumes.begin(), volumes.end(), [](const ModelVolume* v) { return !v->mmu_segmentation_facets.empty(); }) != volumes.end()) {

        // If XY Size compensation is also enabled, notify the user that XY Size compensation
        // would not be used because the object is multi-material painted.
        if (m_config.xy_hole_compensation.value != 0.f || m_config.xy_contour_compensation.value != 0.f) {
            //this->active_step_add_warning(
            //    PrintStateBase::WarningLevel::CRITICAL,
            //    L("An object has enabled XY Size compensation which will not be used because it is also multi-material painted.\nXY Size "
            //      "compensation cannot be combined with multi-material painting."));
            BOOST_LOG_TRIVIAL(info) << "xy compensation will not work for object " << this->model_object()->name << " for multi filament.";
        }

        BOOST_LOG_TRIVIAL(debug) << "Slicing volumes - MMU segmentation";
        apply_mm_segmentation(*this, [print]() { print->throw_if_canceled(); });
    }


    BOOST_LOG_TRIVIAL(debug) << "Slicing volumes - make_slices in parallel - begin";
    {
        // Compensation value, scaled. Only applying the negative scaling here, as the positive scaling has already been applied during slicing.
        const size_t num_extruders = print->config().filament_diameter.size();
        const auto   xy_hole_scaled = (num_extruders > 1 && this->is_mm_painted()) ? scaled<float>(0.f) : scaled<float>(m_config.xy_hole_compensation.value);
        const auto   xy_contour_scaled            = (num_extruders > 1 && this->is_mm_painted()) ? scaled<float>(0.f) : scaled<float>(m_config.xy_contour_compensation.value);
        const float  elephant_foot_compensation_scaled = (m_config.raft_layers == 0) ?
        	// Only enable Elephant foot compensation if printing directly on the print bed.
            float(scale_(m_config.elefant_foot_compensation.value)) :
        	0.f;
        // Uncompensated slices for the first layer in case the Elephant foot compensation is applied.
	    ExPolygons  lslices_1st_layer;
        //BBS: this part has been changed a lot to support seperated contour and hole size compensation
	    tbb::parallel_for(
	        tbb::blocked_range<size_t>(0, m_layers.size()),
			[this, xy_hole_scaled, xy_contour_scaled, elephant_foot_compensation_scaled, &lslices_1st_layer](const tbb::blocked_range<size_t>& range) {
	            for (size_t layer_id = range.begin(); layer_id < range.end(); ++ layer_id) {
	                m_print->throw_if_canceled();
	                Layer *layer = m_layers[layer_id];
	                // Apply size compensation and perform clipping of multi-part objects.
	                float elfoot = (layer_id == 0) ? elephant_foot_compensation_scaled : 0.f;
	                if (layer->m_regions.size() == 1) {
	                    // Optimized version for a single region layer.
	                    // Single region, growing or shrinking.
	                    LayerRegion *layerm = layer->m_regions.front();
                        if (elfoot > 0) {
		                    // Apply the elephant foot compensation and store the 1st layer slices without the Elephant foot compensation applied.
		                    lslices_1st_layer = to_expolygons(std::move(layerm->slices.surfaces));
                            if (xy_contour_scaled > 0 || xy_hole_scaled > 0) {
                                lslices_1st_layer = _shrink_contour_holes(std::max(0.f, xy_contour_scaled),
                                                                   std::max(0.f, xy_hole_scaled),
                                                                   lslices_1st_layer);
                            }
                            if (xy_contour_scaled < 0 || xy_hole_scaled < 0) {
                                lslices_1st_layer = _shrink_contour_holes(std::min(0.f, xy_contour_scaled),
                                                                   std::min(0.f, xy_hole_scaled),
                                                                   lslices_1st_layer);
                            }
							layerm->slices.set(
								union_ex(
									Slic3r::elephant_foot_compensation(lslices_1st_layer,
	                            		layerm->flow(frExternalPerimeter), unscale<double>(elfoot))),
								stInternal);
	                    } else {
	                        // Apply the XY contour and hole size compensation.
                            if (xy_contour_scaled != 0.0f || xy_hole_scaled != 0.0f) {
                                ExPolygons expolygons = to_expolygons(std::move(layerm->slices.surfaces));
                                if (xy_contour_scaled > 0 || xy_hole_scaled > 0) {
                                    expolygons = _shrink_contour_holes(std::max(0.f, xy_contour_scaled),
                                                                       std::max(0.f, xy_hole_scaled),
                                                                       expolygons);
                                }
                                if (xy_contour_scaled < 0 || xy_hole_scaled < 0) {
                                    expolygons = _shrink_contour_holes(std::min(0.f, xy_contour_scaled),
                                                                       std::min(0.f, xy_hole_scaled),
                                                                       expolygons);
                                }
                                layerm->slices.set(std::move(expolygons), stInternal);
                            }
	                    }
	                } else {
                        float max_growth = std::max(xy_hole_scaled, xy_contour_scaled);
                        float min_growth = std::min(xy_hole_scaled, xy_contour_scaled);
                        ExPolygons merged_poly_for_holes_growing;
                        if (max_growth > 0) {
                            //BBS: merge polygons because region can cut "holes".
                            //Then, cut them to give them again later to their region
                            merged_poly_for_holes_growing = layer->merged(float(SCALED_EPSILON));
                            merged_poly_for_holes_growing = _shrink_contour_holes(std::max(0.f, xy_contour_scaled),
                                                                                  std::max(0.f, xy_hole_scaled),
                                                                                  union_ex(merged_poly_for_holes_growing));

                            // BBS: clipping regions, priority is given to the first regions.
                            Polygons processed;
                            for (size_t region_id = 0; region_id < layer->regions().size(); ++region_id) {
                                ExPolygons slices = to_expolygons(std::move(layer->m_regions[region_id]->slices.surfaces));
                                if (max_growth > 0.f) {
                                    slices = intersection_ex(offset_ex(slices, max_growth), merged_poly_for_holes_growing);
                                }

                                //BBS: Trim by the slices of already processed regions.
                                if (region_id > 0)
                                    slices = diff_ex(to_polygons(std::move(slices)), processed);
                                if (region_id + 1 < layer->regions().size())
                                    // Collect the already processed regions to trim the to be processed regions.
                                    polygons_append(processed, slices);
                                layer->m_regions[region_id]->slices.set(std::move(slices), stInternal);
                            }
                        }
                        if (min_growth < 0.f || elfoot > 0.f) {
                            // Apply the negative XY compensation. (the ones that is <0)
                            ExPolygons trimming;
                            static const float eps = float(scale_(m_config.slice_closing_radius.value) * 1.5);
                            if (elfoot > 0.f) {
                                lslices_1st_layer = offset_ex(layer->merged(eps), -eps);
                                trimming = Slic3r::elephant_foot_compensation(lslices_1st_layer,
                                    layer->m_regions.front()->flow(frExternalPerimeter), unscale<double>(elfoot));
                            } else {
                                trimming = layer->merged(float(SCALED_EPSILON));
                            }
                            if (min_growth < 0.0f)
                                trimming = _shrink_contour_holes(std::min(0.f, xy_contour_scaled),
                                                                 std::min(0.f, xy_hole_scaled),
                                                                 trimming);
                            //BBS: trim surfaces
                            for (size_t region_id = 0; region_id < layer->regions().size(); ++region_id)
                                layer->regions()[region_id]->trim_surfaces(to_polygons(trimming));
                        }
	                }
	                // Merge all regions' slices to get islands, chain them by a shortest path.
	                layer->make_slices();
	            }
	        });
	    if (elephant_foot_compensation_scaled > 0.f && ! m_layers.empty()) {
	    	// The Elephant foot has been compensated, therefore the 1st layer's lslices are shrank with the Elephant foot compensation value.
	    	// Store the uncompensated value there.
	    	assert(m_layers.front()->id() == 0);
            //BBS: sort the lslices_1st_layer according to shortest path before saving
            //Otherwise the travel of first layer would be mess.
            Points ordering_points;
            ordering_points.reserve(lslices_1st_layer.size());
            for (const ExPolygon& ex : lslices_1st_layer)
                ordering_points.push_back(ex.contour.first_point());
            std::vector<Points::size_type> order = chain_points(ordering_points);
            ExPolygons lslices_1st_layer_sorted;
            lslices_1st_layer_sorted.reserve(lslices_1st_layer.size());
            for (size_t i : order)
                lslices_1st_layer_sorted.emplace_back(std::move(lslices_1st_layer[i]));

            m_layers.front()->lslices = std::move(lslices_1st_layer_sorted);
		}
	}

    m_print->throw_if_canceled();
    BOOST_LOG_TRIVIAL(debug) << "Slicing volumes - make_slices in parallel - end";
}

//BBS: this function is used to offset contour and holes of expolygons seperately by different value
ExPolygons PrintObject::_shrink_contour_holes(double contour_delta, double hole_delta, const ExPolygons& polys) const
{
    ExPolygons new_ex_polys;
    for (const ExPolygon& ex_poly : polys) {
        Polygons contours;
        Polygons holes;
        //BBS: modify hole
        for (const Polygon& hole : ex_poly.holes) {
            if (hole_delta != 0) {
                for (Polygon& newHole : offset(hole, -hole_delta)) {
                    newHole.make_counter_clockwise();
                    holes.emplace_back(std::move(newHole));
                }
            } else {
                holes.push_back(hole);
                holes.back().make_counter_clockwise();
            }
        }
        //BBS: modify contour
        if (contour_delta != 0) {
            Polygons new_contours = offset(ex_poly.contour, contour_delta);
            if (new_contours.size() == 0)
                continue;
            contours.insert(contours.end(), std::make_move_iterator(new_contours.begin()), std::make_move_iterator(new_contours.end()));
        } else {
            contours.push_back(ex_poly.contour);
        }
        ExPolygons temp = diff_ex(union_(contours), union_(holes));
        new_ex_polys.insert(new_ex_polys.end(), std::make_move_iterator(temp.begin()), std::make_move_iterator(temp.end()));
    }
    return union_ex(new_ex_polys);
}

std::vector<Polygons> PrintObject::slice_support_volumes(const ModelVolumeType model_volume_type) const
{
    auto it_volume     = this->model_object()->volumes.begin();
    auto it_volume_end = this->model_object()->volumes.end();
    for (; it_volume != it_volume_end && (*it_volume)->type() != model_volume_type; ++ it_volume) ;
    std::vector<Polygons> slices;
    if (it_volume != it_volume_end) {
        // Found at least a single support volume of model_volume_type.
        std::vector<float> zs = zs_from_layers(this->layers());
        std::vector<char>  merge_layers;
        bool               merge = false;
        const Print       *print = this->print();
        auto               throw_on_cancel_callback = std::function<void()>([print](){ print->throw_if_canceled(); });
        MeshSlicingParamsEx params;
        params.trafo = this->trafo_centered();
        for (; it_volume != it_volume_end; ++ it_volume)
            if ((*it_volume)->type() == model_volume_type) {
                std::vector<ExPolygons> slices2 = slice_volume(*(*it_volume), zs, params, throw_on_cancel_callback);
                if (slices.empty()) {
                    slices.reserve(slices2.size());
                    for (ExPolygons &src : slices2)
                        slices.emplace_back(to_polygons(std::move(src)));
                } else if (!slices2.empty()) {
                    if (merge_layers.empty())
                        merge_layers.assign(zs.size(), false);
                    for (size_t i = 0; i < zs.size(); ++ i) {
                        if (slices[i].empty())
                            slices[i] = to_polygons(std::move(slices2[i]));
                        else if (! slices2[i].empty()) {
                            append(slices[i], to_polygons(std::move(slices2[i])));
                            merge_layers[i] = true;
                            merge = true;
                        }
                    }
                }
            }
        if (merge) {
            std::vector<Polygons*> to_merge;
            to_merge.reserve(zs.size());
            for (size_t i = 0; i < zs.size(); ++ i)
                if (merge_layers[i])
                    to_merge.emplace_back(&slices[i]);
            tbb::parallel_for(
                tbb::blocked_range<size_t>(0, to_merge.size()),
                [&to_merge](const tbb::blocked_range<size_t> &range) {
                    for (size_t i = range.begin(); i < range.end(); ++ i)
                        *to_merge[i] = union_(*to_merge[i]);
            });
        }
    }
    return slices;
}

} // namespace Slic3r
