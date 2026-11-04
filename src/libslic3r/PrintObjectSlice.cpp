#include "AdaptiveLayerMerge.hpp"
#include "ElephantFootCompensation.hpp"
#include "I18N.hpp"
#include "Layer.hpp"
#include "MultiMaterialSegmentation.hpp"
#include "Print.hpp"
#include "ClipperUtils.hpp"
#include "Interlocking/InterlockingGenerator.hpp"
#include "Time.hpp"
#include "Utils.hpp"
//BBS
#include "ShortestPath.hpp"

#include <tbb/parallel_for.h>

#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <numeric>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include "libslic3r/Log.hpp"

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
        const indexed_triangle_set *its = &volume.mesh().its;
        indexed_triangle_set its_flipped;
        if (its->indices.size() > 0) {
            MeshSlicingParamsEx params2 { params };
            params2.trafo = params2.trafo * volume.get_matrix();
            if (params2.trafo.rotation().determinant() < 0.) {
                // 仅当模型存在镜像时才复制its并进行翻转
                its_flipped = volume.mesh().its;
                its_flip_triangles(its_flipped);
                its = &its_flipped;
            }
            // Only slice Z planes that hit this volume. Lightbox-style parts are ~1mm tall
            // while the object may have many empty layers above them.
            // zs是整个object的高度范围，可以比当前volume的高度范围更大，只对当前volume高度范围内的zs进行切片可以加速
            const auto tm = params2.trafo.matrix();
            float zmin = std::numeric_limits<float>::max();
            float zmax = std::numeric_limits<float>::lowest();
            for (const stl_vertex &v : its->vertices) {
                const float z = float(tm(2, 0) * double(v.x()) + tm(2, 1) * double(v.y()) + tm(2, 2) * double(v.z()) + tm(2, 3));
                zmin = (std::min)(zmin, z);
                zmax = (std::max)(zmax, z);
            }
            std::vector<float> zs_hit;
            std::vector<size_t> zs_idx;
            zs_hit.reserve(zs.size());
            zs_idx.reserve(zs.size());
            for (size_t i = 0; i < zs.size(); ++ i)
                if (zs[i] >= zmin - float(EPSILON) && zs[i] <= zmax + float(EPSILON)) {
                    zs_hit.push_back(zs[i]);
                    zs_idx.push_back(i);
                }

            auto scatter_slices = [&](std::vector<ExPolygons> &&sliced) {
                layers.assign(zs.size(), ExPolygons());
                const size_t n = (std::min)(zs_idx.size(), sliced.size());
                for (size_t i = 0; i < n; ++ i)
                    layers[zs_idx[i]] = std::move(sliced[i]);
            };
            if (zs_hit.empty()) {
                layers.assign(zs.size(), ExPolygons());
            } else if (zs_hit.size() == zs.size()) {
                layers = slice_mesh_ex(*its, zs, params2, throw_on_cancel_callback);
            } else {
                // slicing_mode_normal_below_layer is an index into the full zs array (vase mode).
                // Remap it onto the zs_hit subset so the bottom-shell mode does not shift.
                MeshSlicingParamsEx params_hit { params2 };
                if (params_hit.slicing_mode_normal_below_layer > 0) {
                    size_t below_cnt = 0;
                    while (below_cnt < zs_idx.size() && zs_idx[below_cnt] < params2.slicing_mode_normal_below_layer)
                        ++ below_cnt;
                    params_hit.slicing_mode_normal_below_layer = below_cnt;
                }
                scatter_slices(slice_mesh_ex(*its, zs_hit, params_hit, throw_on_cancel_callback));
            }
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
    const std::function<void()>                              &throw_on_cancel_callback,
    const Print                                              *print = nullptr)
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
    params_base.resolution = print_config.resolution <= 0.001 ? 0.0f : 0.0025;
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

    struct SliceVolumeJob {
        const ModelVolume                 *model_volume { nullptr };
        MeshSlicingParamsEx                params;
        std::vector<t_layer_height_range>  slicing_ranges;
        bool                               use_ranges { false };
    };
    std::vector<SliceVolumeJob> jobs;
    jobs.reserve(model_volumes.size());

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
                    jobs.push_back({ model_volume, params, {}, false });
                }
            } else {
                assert(! print_config.spiral_mode);
                slicing_ranges.clear();
                for (const PrintObjectRegions::LayerRangeRegions &layer_range : layer_ranges)
                    if (layer_range.has_volume(model_volume->id()))
                        slicing_ranges.emplace_back(layer_range.layer_height_range);
                if (! slicing_ranges.empty())
                    jobs.push_back({ model_volume, params, slicing_ranges, true });
            }
        }

    BOOST_LOG_TRIVIAL(info) << jobs.size() << " volumes, " << zs.size() << " layers";
    std::vector<VolumeSlices> results(jobs.size());
    std::atomic<int> done_count { 0 };
    std::mutex progress_mutex;
    auto run_job = [&](size_t i) {
        const SliceVolumeJob &job = jobs[i];
        std::vector<ExPolygons> slices = job.use_ranges ?
            slice_volume(*job.model_volume, zs, job.slicing_ranges, job.params, throw_on_cancel_callback) :
            slice_volume(*job.model_volume, zs, job.params, throw_on_cancel_callback);
        results[i] = { job.model_volume->id(), std::move(slices) };
        const int done = ++ done_count;
        if (print != nullptr && (done == int(jobs.size()) || (done & 31) == 0)) {
            const int pct = 5 + 5 * done / std::max(int(jobs.size()), 1);
            std::lock_guard<std::mutex> lock(progress_mutex);
            print->set_status(std::min(pct, 10), L("Slicing mesh"));
        }
    };
    tbb::parallel_for(tbb::blocked_range<size_t>(0, jobs.size()),
        [&](const tbb::blocked_range<size_t> &range) {
            for (size_t i = range.begin(); i != range.end(); ++ i)
                run_job(i);
        });
    for (VolumeSlices &vs : results)
        if (! vs.slices.empty())
            out.emplace_back(std::move(vs));

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
        [z](const PrintObjectRegions::LayerRangeRegions &lr) {
            return lr.layer_height_range.second < z && abs(lr.layer_height_range.second - z) > EPSILON;
        });
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
    for (; it->layer_height_range.second <= z + EPSILON; ++ it)
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
    const bool                                                order_independent_overlap_carving,
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
                    std::vector<int> printable_region_ids;
                    for (int idx_region = 0; idx_region < int(layer_range.volume_regions.size()); ++ idx_region) {
                        const PrintObjectRegions::VolumeRegion &region = layer_range.volume_regions[idx_region];
                        if (region.bbox->min().z() <= z && region.bbox->max().z() >= z) {
                            if (region.model_volume->is_model_part())
                                printable_region_ids.push_back(idx_region);
                            if (idx_first_printable_region == -1 && region.model_volume->is_model_part()) {
                                idx_first_printable_region = idx_region;
                            }
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
                        for (int printable_region_id : printable_region_ids) {
                            const PrintObjectRegions::VolumeRegion &region = layer_range.volume_regions[printable_region_id];
                            append(slices_by_region[region.region->print_object_region_id()][z_idx], std::move(volume_slices_find_by_id(volume_slices, region.model_volume->id()).slices[z_idx]));
                        }
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
            [&slices_by_region, &print_object_regions, &zs_complex, &layer_ranges_regions_to_slices, clip_multipart_objects, order_independent_overlap_carving, &throw_on_cancel_callback]
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

                            ExPolygons temp = intersection_ex(expoly_b, expoly_a, ApplySafetyOffset::Yes);
                            if (temp.empty())
                                continue;

                            if (expoly_a.contour.length() > expoly_b.contour.length())
                                trimming_a.insert(trimming_a.end(), temp.begin(), temp.end());
                            else
                                trimming_b.insert(trimming_b.end(), temp.begin(), temp.end());
                        }
                    }

                    expolys_a = diff_ex(expolys_a, trimming_a);
                    expolys_b = diff_ex(expolys_b, trimming_b);
                };

                std::vector<RegionSlice> temp_slices;
                std::vector<BoundingBox> slice_bboxes;
                for (size_t zs_complex_idx = range.begin(); zs_complex_idx < range.end(); ++ zs_complex_idx) {
                    auto [z_idx, z] = zs_complex[zs_complex_idx];
                    it_layer_range = layer_range_next(print_object_regions.layer_ranges, it_layer_range, z);
                    const PrintObjectRegions::LayerRangeRegions &layer_range = *it_layer_range;
                    {
                        std::vector<VolumeSlices*> &layer_range_regions_to_slices = layer_ranges_regions_to_slices[it_layer_range - print_object_regions.layer_ranges.begin()];
                        // Per volume_regions slices at this Z height.
                        temp_slices.clear();
                        temp_slices.reserve(layer_range.volume_regions.size());
                        for (VolumeSlices* &slices : layer_range_regions_to_slices) {
                            const PrintObjectRegions::VolumeRegion &volume_region = layer_range.volume_regions[&slices - layer_range_regions_to_slices.data()];
                            temp_slices.push_back({ std::move(slices->slices[z_idx]), volume_region.region ? volume_region.region->print_object_region_id() : -1, volume_region.model_volume->id() });
                        }
                    }
                    bool has_modifier    = false;
                    int  n_model_parts   = 0;
                    int  single_region_id = -1;
                    bool single_region   = true;
                    for (int idx_region = 0; idx_region < int(layer_range.volume_regions.size()); ++ idx_region) {
                        if (temp_slices[idx_region].expolygons.empty())
                            continue;
                        const ModelVolume *mv = layer_range.volume_regions[idx_region].model_volume;
                        if (mv->is_modifier())
                            has_modifier = true;
                        else if (mv->is_model_part()) {
                            ++ n_model_parts;
                            const int region_id = temp_slices[idx_region].region_id;
                            if (single_region_id == -1)
                                single_region_id = region_id;
                            else if (single_region_id != region_id)
                                single_region = false;
                        }
                    }

                    auto bbox_volume = [](const PrintObjectRegions::BoundingBox &b) {
                        const auto sz = b.sizes();
                        return double(sz.x()) * double(sz.y()) * double(sz.z());
                    };

                    // Fast path for lithophane / lightbox style models: thousands of same-material
                    // islands. All parts share a single PrintRegion here, so clipping them against
                    // each other is a no-op once they get unioned; only the negative volumes still
                    // have to be subtracted. Restricted to one region on purpose: across regions a
                    // later volume carves the earlier ones, and merging by region would replace that
                    // per-volume order with the order the regions happen to appear in.
                    if (! has_modifier && clip_multipart_objects && n_model_parts >= 2 && single_region && single_region_id >= 0) {
                        ExPolygons parts;
                        int        n_appended = 0;
                        for (int idx_region = 0; idx_region < int(layer_range.volume_regions.size()); ++ idx_region) {
                            if (temp_slices[idx_region].expolygons.empty())
                                continue;
                            const PrintObjectRegions::VolumeRegion &region = layer_range.volume_regions[idx_region];
                            if (region.model_volume->is_negative_volume()) {
                                // A negative volume only subtracts from the volumes preceding it,
                                // matching the generic path below.
                                if (! parts.empty())
                                    parts = diff_ex(parts, temp_slices[idx_region].expolygons);
                                continue;
                            }
                            if (! region.model_volume->is_model_part())
                                continue;
                            if (parts.empty())
                                parts = std::move(temp_slices[idx_region].expolygons);
                            else
                                append(parts, std::move(temp_slices[idx_region].expolygons));
                            ++ n_appended;
                        }
                        if (n_appended > 1)
                            parts = closing_ex(parts, float(scale_(EPSILON)));
                        slices_by_region[single_region_id][z_idx] = std::move(parts);
                        throw_on_cancel_callback();
                        continue;
                    }

                    // Bounding box of what each volume actually contributes to this layer. The volume
                    // bbox spans its whole 3D extent, so it lets through many pairs whose slices at
                    // this Z cannot possibly intersect. Computed once, before any clipping shrinks the
                    // slices, so it stays a conservative filter.
                    slice_bboxes.assign(temp_slices.size(), BoundingBox());
                    for (size_t i = 0; i < temp_slices.size(); ++ i)
                        if (! temp_slices[i].expolygons.empty())
                            slice_bboxes[i] = get_extents(temp_slices[i].expolygons);

                    for (int idx_region = 0; idx_region < int(layer_range.volume_regions.size()); ++ idx_region)
                        if (! temp_slices[idx_region].expolygons.empty()) {
                            const PrintObjectRegions::VolumeRegion &region = layer_range.volume_regions[idx_region];
                            if (region.model_volume->is_modifier()) {
                                assert(region.parent > -1);
                                bool next_region_same_modifier = idx_region + 1 < int(temp_slices.size()) && layer_range.volume_regions[idx_region + 1].model_volume == region.model_volume;
                                RegionSlice &parent_slice = temp_slices[region.parent];
                                RegionSlice &this_slice   = temp_slices[idx_region];
                                ExPolygons   source       = std::move(this_slice.expolygons);
                                const bool skip_periodic = region.region
                                    && region.region->config().periodic_modifier.value
                                    && !periodic_modifier_active(region.region->config(), int(z_idx));
                                if (skip_periodic || parent_slice.expolygons.empty()) {
                                    this_slice.expolygons.clear();
                                } else {
                                    this_slice  .expolygons = intersection_ex(parent_slice.expolygons, source);
                                    parent_slice.expolygons = diff_ex        (parent_slice.expolygons, source);
                                }
                                if (next_region_same_modifier)
                                    // To be used in the following iteration.
                                    temp_slices[idx_region + 1].expolygons = std::move(source);
                            } else if ((region.model_volume->is_model_part() && clip_multipart_objects) || region.model_volume->is_negative_volume()) {
                                // Clip every non-zero region preceding it.
                                const bool current_is_negative = region.model_volume->is_negative_volume();
                                const double current_vol = current_is_negative ? 0.0 : bbox_volume(*region.bbox);
                                for (int idx_region2 = 0; idx_region2 < idx_region; ++ idx_region2)
                                    if (! temp_slices[idx_region2].expolygons.empty()) {
                                        // Nothing left to carve with, and an empty subtrahend would be a no-op anyway.
                                        if (temp_slices[idx_region].expolygons.empty())
                                            break;
                                        // Skip trim_overlap for now, because it slow down the performace so much for some special cases
#if 1
                                        if (const PrintObjectRegions::VolumeRegion& region2 = layer_range.volume_regions[idx_region2];
                                            !region2.model_volume->is_negative_volume() && overlap_in_xy(*region.bbox, *region2.bbox)) {
                                            // Two model parts sharing a PrintRegion are unioned by the merge step below,
                                            // so whichever way they carve each other the union is the same. Skipping the
                                            // clip is exact, and on multi-material lithophane style models it removes the
                                            // bulk of the Boolean operations.
                                            // Disabled when a modifier is present: the modifier reads the parent slice
                                            // before the union, so it must observe the carved parent.
                                            if (! has_modifier && ! current_is_negative && region2.model_volume->is_model_part() &&
                                                temp_slices[idx_region].region_id == temp_slices[idx_region2].region_id)
                                                continue;
                                            if (! slice_bboxes[idx_region].overlap(slice_bboxes[idx_region2]))
                                                continue;
                                            const bool current_carves = !order_independent_overlap_carving || current_is_negative ||
                                                current_vol <= bbox_volume(*region2.bbox);
                                            if (current_carves)
                                                temp_slices[idx_region2].expolygons = diff_ex(temp_slices[idx_region2].expolygons, temp_slices[idx_region].expolygons);
                                            else
                                                temp_slices[idx_region].expolygons = diff_ex(temp_slices[idx_region].expolygons, temp_slices[idx_region2].expolygons);
                                        }
#else
                                        const PrintObjectRegions::VolumeRegion& region2 = layer_range.volume_regions[idx_region2];
                                        if (!region2.model_volume->is_negative_volume() && overlap_in_xy(*region.bbox, *region2.bbox))
                                            //BBS: handle negative_volume seperately, always minus the negative volume and don't need to trim overlap
                                            if (!region.model_volume->is_negative_volume())
                                                trim_overlap(temp_slices[idx_region2].expolygons, temp_slices[idx_region].expolygons);
                                            else
                                                temp_slices[idx_region2].expolygons = diff_ex(temp_slices[idx_region2].expolygons, temp_slices[idx_region].expolygons);
#endif
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

//BBS: grouping the volumes of an object according to their connection relationship.
// Connectivity is evaluated across ALL layers (two volumes are connected when their grown
// slices overlap on any shared layer, with a +/-1 layer tolerance), which restores the
// original full-layer semantics after the first-layer-only optimization. A spatial hash grid
// built on the full (all-layer) bounding boxes keeps the broad phase near-linear so thousands
// of islands stay fast; the exact per-layer overlap test only runs for spatially near pairs.
// The grouped slices that feed brim generation are still taken from the first printed layer.
bool groupingVolumes(std::vector<VolumeSlices> objSliceByVolume, std::vector<groupedVolumeSlices>& groups, double resolution, int firstLayerReplacedBy)
{
    const int n = int(objSliceByVolume.size());
    groups.clear();
    if (n == 0)
        return true;

    const double offsetValue = 0.05 / SCALING_FACTOR;
    const int layer_idx = std::min(firstLayerReplacedBy, std::max(0, int(objSliceByVolume.front().slices.size()) - 1));

    // Grow every layer of every volume (Douglas-Peucker + 0.05mm offset) and record the full XY
    // bounding box plus the occupied layer range. Growing all layers is what preserves cross-layer
    // connectivity; historically the bottleneck was the O(n^2) pairwise test below, not this step.
    std::vector<std::vector<ExPolygons>> grown(n);
    std::vector<std::vector<BoundingBox>> lbbox(n);
    std::vector<BoundingBox> bboxes(n);
    std::vector<int> layer_lo(n, 0);
    std::vector<int> layer_hi(n, -1);
    std::vector<char> alive(n, 0);
    tbb::parallel_for(tbb::blocked_range<int>(0, n), [&](const tbb::blocked_range<int> &range) {
        for (int i = range.begin(); i != range.end(); ++ i) {
            const std::vector<ExPolygons> &src = objSliceByVolume[i].slices;
            const int nl = int(src.size());
            grown[i].resize(nl);
            lbbox[i].resize(nl);
            BoundingBox bb;
            bool bb_valid = false;
            int lo = nl, hi = -1;
            for (int k = 0; k < nl; ++ k) {
                if (src[k].empty())
                    continue;
                ExPolygons simplified = src[k];
                if (resolution > 0.) {
                    for (ExPolygon &poly_ex : simplified)
                        poly_ex.douglas_peucker(resolution);
                }
                grown[i][k] = offset_ex(simplified, offsetValue);
                if (grown[i][k].empty())
                    continue;
                const BoundingBox lbb = get_extents(grown[i][k]);
                lbbox[i][k] = lbb;
                if (! bb_valid) { bb = lbb; bb_valid = true; }
                else            { bb.merge(lbb); }
                if (k < lo) lo = k;
                if (k > hi) hi = k;
            }
            if (bb_valid) {
                bboxes[i]   = bb;
                layer_lo[i] = lo;
                layer_hi[i] = hi;
                alive[i]    = 1;
            }
        }
    });

    std::vector<int> parent(n);
    std::iota(parent.begin(), parent.end(), 0);
    auto find_root = [&](int x) {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    };
    auto unite = [&](int a, int b) {
        a = find_root(a);
        b = find_root(b);
        if (a != b)
            parent[b] = a;
    };

    // Narrow phase: exact all-layer overlap test with +/-1 layer tolerance (the original per-layer
    // overlap semantics). Restricted to the first volume's occupied layer range.
    auto volumes_connected = [&](int i, int j) -> bool {
        const std::vector<ExPolygons> &a  = grown[i];
        const std::vector<ExPolygons> &b  = grown[j];
        const std::vector<BoundingBox> &ba = lbbox[i];
        const std::vector<BoundingBox> &bb = lbbox[j];
        const int nlb = int(b.size());
        if (int(a.size()) != nlb)
            return false;
        const int lo = std::max(0, layer_lo[i]);
        const int hi = std::min(layer_hi[i], nlb - 1);
        for (int k = lo; k <= hi; ++ k) {
            if (a[k].empty())
                continue;
            const BoundingBox &bak = ba[k];
            // Per-layer bbox pre-check avoids the expensive Clipper overlaps() for the common case
            // where two volumes share an XY column but not this particular layer.
            if (! b[k].empty()     && bak.overlap(bb[k])     && overlaps(a[k], b[k]))
                return true;
            if (k + 1 < nlb && ! b[k + 1].empty() && bak.overlap(bb[k + 1]) && overlaps(a[k], b[k + 1]))
                return true;
            if (k - 1 >= 0  && ! b[k - 1].empty() && bak.overlap(bb[k - 1]) && overlaps(a[k], b[k - 1]))
                return true;
        }
        return false;
    };

    // Broad phase: spatial hash on full (all-layer) bboxes so we only narrow-test near pairs.
    const coord_t cell_size = (std::max)(coord_t(1), coord_t(scale_(5.)));
    std::unordered_map<uint64_t, std::vector<int>> grid;
    grid.reserve(size_t(n) * 2);
    auto cell_key = [](int cx, int cy) {
        return (uint64_t(uint32_t(cx)) << 32) | uint32_t(cy);
    };
    for (int i = 0; i < n; ++ i) {
        if (! alive[i])
            continue;
        const BoundingBox &bb = bboxes[i];
        const int x0 = int(bb.min.x() / cell_size);
        const int y0 = int(bb.min.y() / cell_size);
        const int x1 = int(bb.max.x() / cell_size);
        const int y1 = int(bb.max.y() / cell_size);
        for (int cx = x0; cx <= x1; ++ cx)
            for (int cy = y0; cy <= y1; ++ cy)
                grid[cell_key(cx, cy)].push_back(i);
    }

    auto pair_key = [](int a, int b) {
        if (a > b)
            std::swap(a, b);
        return (uint64_t(uint32_t(a)) << 32) | uint32_t(b);
    };

    // Collect the unique near candidate pairs (cheap bbox / layer-range filters only). The expensive
    // per-layer overlap test is deferred so it can run in parallel below.
    std::vector<std::pair<int, int>> candidates;
    {
        std::unordered_set<uint64_t> seen;
        seen.reserve(size_t(n) * 4);
        for (const auto &cell : grid) {
            const std::vector<int> &ids = cell.second;
            for (size_t a = 0; a < ids.size(); ++ a) {
                for (size_t b = a + 1; b < ids.size(); ++ b) {
                    const int i = ids[a];
                    const int j = ids[b];
                    if (! bboxes[i].overlap(bboxes[j]))
                        continue;
                    // Layer ranges must be within +/-1 to have any chance of touching.
                    if (layer_hi[i] < layer_lo[j] - 1 || layer_hi[j] < layer_lo[i] - 1)
                        continue;
                    if (seen.insert(pair_key(i, j)).second)
                        candidates.emplace_back(i, j);
                }
            }
        }
    }

    // Narrow phase: the connectivity test is read-only and independent per pair, so evaluate all
    // candidates in parallel. Candidates are sorted for deterministic union order (grid iteration
    // order is not stable); connectivity itself is order independent.
    for (auto &p : candidates)
        if (p.first > p.second)
            std::swap(p.first, p.second);
    std::sort(candidates.begin(), candidates.end());
    std::vector<char> connected(candidates.size(), 0);
    tbb::parallel_for(tbb::blocked_range<size_t>(0, candidates.size()), [&](const tbb::blocked_range<size_t> &range) {
        for (size_t c = range.begin(); c != range.end(); ++ c)
            connected[c] = volumes_connected(candidates[c].first, candidates[c].second) ? 1 : 0;
    });
    for (size_t c = 0; c < candidates.size(); ++ c)
        if (connected[c])
            unite(candidates[c].first, candidates[c].second);

    // Assign every geometry-bearing volume to its group (slices are collected below only when needed).
    // groupId is the minimum volume index in the group for stable slice-cache JSON output.
    std::unordered_map<int, int> root_to_group;
    std::vector<int> group_min;
    groups.reserve(n);
    for (int i = 0; i < n; ++ i) {
        if (! alive[i])
            continue;
        const int root = find_root(i);
        auto it = root_to_group.find(root);
        int gidx = -1;
        if (it == root_to_group.end()) {
            gidx = int(groups.size());
            root_to_group[root] = gidx;
            groupedVolumeSlices gvs;
            gvs.groupId = i;
            groups.emplace_back(std::move(gvs));
            group_min.push_back(i);
        } else {
            gidx = it->second;
            if (i < group_min[gidx]) {
                group_min[gidx] = i;
                groups[gidx].groupId = i;
            }
        }
        groups[gidx].volume_ids.push_back(objSliceByVolume[i].volume_id);
    }

    // Collect the grown first-layer slices, union them and shrink back to the real outline. Using
    // grown[.] here (then shrinking by offsetValue) reproduces the original morphological closing
    // between volumes.
    for (int i = 0; i < n; ++ i) {
        if (! alive[i])
            continue;
        const int gidx = root_to_group[find_root(i)];
        if (layer_idx < int(grown[i].size()))
            append(groups[gidx].slices, grown[i][layer_idx]);
    }
    tbb::parallel_for(tbb::blocked_range<size_t>(0, groups.size()), [&](const tbb::blocked_range<size_t> &range) {
        for (size_t gi = range.begin(); gi != range.end(); ++ gi) {
            groups[gi].slices = offset_ex(union_ex(groups[gi].slices), -offsetValue);
            for (ExPolygon &poly_ex : groups[gi].slices)
                poly_ex.douglas_peucker(resolution);
        }
    });
    return true;
}

//BBS: filter the members of "objSliceByVolume" such that only "model_part" are included
// 哈希集合的查找是 O(n)，比双重循环更快。
std::vector<VolumeSlices> findPartVolumes(const std::vector<VolumeSlices>& objSliceByVolume, ModelVolumePtrs model_volumes) {
    std::unordered_set<size_t> part_ids;
    part_ids.reserve(model_volumes.size());
    for (const auto& mv : model_volumes)
        if (mv->is_model_part())
            part_ids.insert(mv->id().id);
    std::vector<VolumeSlices> outPut;
    outPut.reserve(objSliceByVolume.size());
    for (const auto& vs : objSliceByVolume)
        if (part_ids.count(vs.volume_id.id))
            outPut.push_back(vs);
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

void reGroupingLayerPolygons(std::vector<groupedVolumeSlices>& gvss, ExPolygons &eps, double resolution)
{
    std::vector<int> epsIndex;
    epsIndex.resize(eps.size(), -1);

    auto gvssc = gvss;
    auto epsc = eps;

    for (ExPolygon& poly_ex : epsc)
        poly_ex.douglas_peucker(resolution);

    for (int i = 0; i != gvssc.size(); ++i) {
        for (ExPolygon& poly_ex : gvssc[i].slices)
            poly_ex.douglas_peucker(resolution);
    }

    tbb::parallel_for(tbb::blocked_range<int>(0, epsc.size()),
        [&epsc, &gvssc, &epsIndex](const tbb::blocked_range<int>& range) {
            for (auto ie = range.begin(); ie != range.end(); ++ie) {
                if (epsc[ie].area() <= 0)
                    continue;

                double minArea = epsc[ie].area();
                for (int iv = 0; iv != gvssc.size(); iv++) {
                    auto clipedExPolys = diff_ex(epsc[ie], gvssc[iv].slices);
                    double area = 0;
                    for (const auto& ce : clipedExPolys) {
                        area += ce.area();
                    }
                    if (area < minArea) {
                        minArea = area;
                        epsIndex[ie] = iv;
                    }
                }
            }
        });

    for (int iv = 0; iv != gvss.size(); iv++)
        gvss[iv].slices.clear();

    for (int ie = 0; ie != eps.size(); ie++) {
        if (epsIndex[ie] >= 0)
            gvss[epsIndex[ie]].slices.push_back(eps[ie]);
    }
}

std::string fix_slicing_errors(PrintObject* object, LayerPtrs &layers, const std::function<void()> &throw_if_canceled, int &firstLayerReplacedBy)
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
    std::atomic<bool> is_replaced = false;
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, buggy_layers.size()),
        [&layers, &throw_if_canceled, &buggy_layers, &is_replaced](const tbb::blocked_range<size_t>& range) {
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
                        is_replaced = true;
                        layerm->slices.set(union_ex(expolys), stInternal);
                    }
                }
                // Update layer slices after repairing the single regions.
                layer->make_slices();
            }
        });
    throw_if_canceled();
    BOOST_LOG_TRIVIAL(debug) << "Slicing objects - fixing slicing errors in parallel - end";

    if(is_replaced)
        error_msg = L("Empty layers around bottom are replaced by nearest normal layers.");

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

    // BBS: first layer slices are sorted by volume group, if the first layer is empty and replaced by the 2nd layer
// the later will be stored in "object->firstLayerObjGroupsMod()"
    if (!buggy_layers.empty() && buggy_layers.front() == 0 && layers.size() > 1)
        firstLayerReplacedBy = 1;

    return error_msg;
}

void groupingVolumesForBrim(PrintObject* object, LayerPtrs& layers, int firstLayerReplacedBy)
{
    const auto           scaled_resolution = scaled<double>(object->print()->config().resolution.value);
    auto partsObjSliceByVolume = findPartVolumes(object->firstLayerObjSliceMod(), object->model_object()->volumes);
    groupingVolumes(partsObjSliceByVolume, object->firstLayerObjGroupsMod(), scaled_resolution, firstLayerReplacedBy);
    applyNegtiveVolumes(object->model_object()->volumes, object->firstLayerObjSliceMod(), object->firstLayerObjGroupsMod(), scaled_resolution);

    // BBS: the actual first layer slices stored in layers are re-sorted by volume group and will be used to generate brim
    reGroupingLayerPolygons(object->firstLayerObjGroupsMod(), layers.front()->lslices, scaled_resolution);
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

    long long slice_begin_time = 0;
    long long region_split_time = 0;
    long long mm_segment_time = 0;
    if (m_print->m_slice_time)
        slice_begin_time = Slic3r::Utils::get_current_milliseconds_time_monotonic();

    //BBS: add flag to reload scene for shell rendering
    m_print->set_status(5, L("Slicing mesh"), PrintBase::SlicingStatus::RELOAD_SCENE);
    std::vector<coordf_t> layer_height_profile;
    bool nozzle_range_reset = false;
    this->update_layer_height_profile(*this->model_object(), m_slicing_params, layer_height_profile, nozzle_range_reset);
    if (nozzle_range_reset)
        this->active_step_add_warning(PrintStateBase::WarningLevel::NON_CRITICAL,
            L("The variable layer height profile has been reset because some layer heights "
              "exceed the allowed range of the current nozzle."));
    m_print->throw_if_canceled();
    m_typed_slices = false;
    this->clear_layers();
    m_layers = new_layers(this, generate_object_layers(m_slicing_params, layer_height_profile, m_config.precise_z_height.value));
    this->slice_volumes(&region_split_time, &mm_segment_time);
    m_print->throw_if_canceled();
    int firstLayerReplacedBy = 0;

#if 1
    // Fix the model.
    //FIXME is this the right place to do? It is done repeateadly at the UI and now here at the backend.
    std::string warning = fix_slicing_errors(this, m_layers, [this](){ m_print->throw_if_canceled(); }, firstLayerReplacedBy);
    m_print->throw_if_canceled();
    //BBS: send warning message to slicing callback
    // This warning is inaccurate, because the empty layers may have been replaced, or the model has supports.
    //if (!warning.empty()) {
    //    BOOST_LOG_TRIVIAL(info) << warning;
    //    this->active_step_add_warning(PrintStateBase::WarningLevel::CRITICAL, warning, PrintStateBase::SlicingReplaceInitEmptyLayers);
    //}
#endif

    // BBS: the actual first layer slices stored in layers are re-sorted by volume group and will be used to generate brim
    m_print->set_status(12, L("Slicing mesh"));
    groupingVolumesForBrim(this, m_layers, firstLayerReplacedBy);
    m_print->set_status(14, L("Slicing mesh"));

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
        throw Slic3r::SlicingError(L("No layers were detected. You might want to repair your STL file(s) or check their size or thickness and retry.\n"));

    if (m_print->m_slice_time) {
        const long long elapsed = Slic3r::Utils::get_current_milliseconds_time_monotonic() - slice_begin_time;
        (*m_print->m_slice_time)[TIME_SLICE_LAYERS] += std::max(0LL, elapsed - region_split_time - mm_segment_time);
    }

    // BBS
    this->set_done(posSlice);
}

// A nozzle coarser than the finest one on the plate does not print every layer: it covers a whole band of
// them with a single extrusion shaped like the band's top layer. Painting, however, is done in object space
// and knows nothing about that. Paint that never reaches a band top would be decimated away together with
// the layers it sits on, leaving a void that nobody fills, while paint that does reach one silently owns
// the layers underneath, where a finer nozzle would otherwise lay a second colour into the same Z.
//
// Both are settled here, before the paint is cut into regions, by making each coarse extruder's paint
// constant across its own bands: empty on the band top means the paint is dropped and the area falls back
// to whoever owned it before painting, which is what never having painted it looks like. A coarse colour
// left thinner than its nozzle can extrude is finally handed to a neighbouring colour.
//
// The bands of one nozzle never share a layer, and a band reads and writes only the layers it covers, so the
// bands of one extruder (one diameter in the base pass) are settled in parallel. Extruders still go one after
// the other: a later one trims what an earlier one has claimed.
static void normalize_mm_segmentation_to_layer_bands(PrintObject &print_object,
                                                     std::vector<std::vector<ExPolygons>> &segmentation,
                                                     const std::function<void()> &throw_on_cancel)
{
    auto area_mm2 = [](const ExPolygons &expolygons) {
        return ::Slic3r::area(expolygons) * SCALING_FACTOR * SCALING_FACTOR;
    };
    auto printable_pieces = [](ExPolygons pieces, double diameter) {
        return adaptive_printable_pieces(std::move(pieces), diameter);
    };

    const size_t layer_count = segmentation.size();
    // Enter only when the print uses the adaptive diameter ladder. A single coarse filament may pass this
    // gate, but without a second painted diameter there is no cross-diameter segmentation to normalize.
    if (layer_count < 2 || ! print_object.print()->uses_multiple_nozzle_diameters()) {
        BOOST_LOG_TRIVIAL(debug) << "[mm_band] skip: layers=" << layer_count
                                 << " multi_diameter=" << print_object.print()->uses_multiple_nozzle_diameters();
        return;
    }
    const size_t extruder_count = segmentation.front().size();
    if (extruder_count < 2) {
        BOOST_LOG_TRIVIAL(debug) << "[mm_band] skip: extruder_count=" << extruder_count;
        return;
    }
    for (const std::vector<ExPolygons> &layer : segmentation)
        if (layer.size() != extruder_count) {
            BOOST_LOG_TRIVIAL(debug) << "[mm_band] skip: ragged segmentation, expected " << extruder_count
                                     << " extruders per layer";
            return;
        }

    const PrintConfig &print_config = print_object.print()->config();
    // Only the extruders the paint actually hands area to take part. The region set carries an entry per
    // filament of the plate, and an entry this object never prints has no filament to read a diameter from,
    // so it would report a diameter the plate never uses.
    std::vector<double> extruder_nozzles(extruder_count, 0.);
    double              min_nozzle = 0.;
    double              max_nozzle = 0.;
    for (size_t extruder_id = 0; extruder_id < extruder_count; ++ extruder_id) {
        bool prints_anything = false;
        for (const std::vector<ExPolygons> &layer : segmentation)
            if (! layer[extruder_id].empty()) {
                prints_anything = true;
                break;
            }
        if (! prints_anything)
            continue;
        const double diameter = nozzle_diameter_for_filament(print_config, (unsigned int) extruder_id + 1);
        if (diameter <= EPSILON)
            continue;
        extruder_nozzles[extruder_id] = diameter;
        if (min_nozzle <= EPSILON || diameter < min_nozzle)
            min_nozzle = diameter;
        if (diameter > max_nozzle)
            max_nozzle = diameter;
    }
    {
        std::string painted;
        for (size_t extruder_id = 0; extruder_id < extruder_count; ++ extruder_id) {
            if (extruder_nozzles[extruder_id] <= EPSILON)
                continue;
            size_t layers_painted = 0;
            double total          = 0.;
            for (const std::vector<ExPolygons> &layer : segmentation)
                if (! layer[extruder_id].empty()) {
                    ++ layers_painted;
                    total += area_mm2(layer[extruder_id]);
                }
            painted += " E" + std::to_string(extruder_id) + "(d=" + std::to_string(extruder_nozzles[extruder_id])
                     + ",layers=" + std::to_string(layers_painted) + ",area=" + std::to_string(total) + ")";
        }
        BOOST_LOG_TRIVIAL(debug) << "[mm_band] painted extruders:" << (painted.empty() ? " none" : painted);
    }
    // Whatever the paint leaves alone keeps its region's own filament. That area is absent from
    // `segmentation`, yet it is extruded in bands like everything else, so its nozzle counts as in use too -
    // without it an object painted with a single colour looks like a single nozzle here and nothing below
    // is normalized at all.
    std::vector<double> base_nozzles;
    for (size_t layer_idx = 0; layer_idx < layer_count; ++ layer_idx)
        for (const LayerRegion *layerm : print_object.get_layer(int(layer_idx))->regions()) {
            if (layerm->slices.surfaces.empty())
                continue;
            const double diameter = nozzle_diameter_for_filament(print_config, layerm->region().config().wall_filament);
            if (diameter <= EPSILON)
                continue;
            if (std::none_of(base_nozzles.begin(), base_nozzles.end(),
                             [diameter](double known) { return std::abs(known - diameter) <= EPSILON; }))
                base_nozzles.emplace_back(diameter);
            if (min_nozzle <= EPSILON || diameter < min_nozzle)
                min_nozzle = diameter;
            if (diameter > max_nozzle)
                max_nozzle = diameter;
        }
    if (min_nozzle <= EPSILON) {
        BOOST_LOG_TRIVIAL(debug) << "[mm_band] skip: no usable nozzle in this object";
        return;
    }
    // Bands are counted off the grid the plate was sliced on, not off the nozzles it happens to use, so that
    // this grid is the one compute_adaptive_merge_groups() decimates on. Both the reference diameter and the
    // base height must come from the sliced layer stack: the process preset's layer height is only the
    // authored value, which SlicingParameters may have subdivided to resolve a finer nozzle.
    const double primary          = layer_grid_nozzle_diameter(print_object.print()->full_print_config());
    const double reference_nozzle = primary > EPSILON ? primary : min_nozzle;
    const double base_layer_height = print_object.slicing_parameters().layer_height;
    const auto uses_merged_ladder = [reference_nozzle, base_layer_height](double diameter) {
        return diameter > EPSILON &&
               adaptive_layer_band_size(diameter, reference_nozzle, base_layer_height) >= 2;
    };
    const bool has_merged_ladder = std::any_of(
        extruder_nozzles.begin(), extruder_nozzles.end(),
        uses_merged_ladder) ||
        std::any_of(base_nozzles.begin(), base_nozzles.end(), uses_merged_ladder);
    if (! has_merged_ladder) {
        BOOST_LOG_TRIVIAL(debug) << "[mm_band] skip: every painted nozzle prints the base grid";
        return;
    }
    {
        std::string bases;
        for (double diameter : base_nozzles)
            bases += " " + std::to_string(diameter);
        BOOST_LOG_TRIVIAL(debug) << "[mm_band] layers=" << layer_count << " extruders=" << extruder_count
                                 << " base_nozzles:" << (bases.empty() ? " none" : bases)
                                 << " min=" << min_nozzle << " max=" << max_nozzle
                                 << " primary=" << primary << " reference=" << reference_nozzle
                                 << " base_layer_height=" << base_layer_height;
    }

    std::vector<double> layer_heights(layer_count, 0.);
    for (size_t layer_idx = 0; layer_idx < layer_count; ++ layer_idx)
        layer_heights[layer_idx] = print_object.get_layer(int(layer_idx))->height;

    // Keep every nozzle on its own fixed ladder. Colour ownership is normalized to the band's anchor
    // contour below; colour starts, stops and layer-to-layer footprint changes must not split the ladder.
    const std::vector<uint8_t> band_tops;
    BOOST_LOG_TRIVIAL(debug) << "[mm_band] band_tops=none (fixed nozzle ladders)";

    // A coarse colour discarded below its band top may have overpainted another colour there. Restore
    // that colour from the immutable top-layer ownership before normalizing any extruder; otherwise the
    // vacated area is absent from segmentation altogether and falls through to the model's base filament.
    // Doing this as a pre-pass also lets a restored coarse owner pass through its own normalization below.
    const std::vector<std::vector<ExPolygons>> original_segmentation = segmentation;
    auto build_painted_bands = [&](size_t band_size, const AdaptiveLayerHeightRange &range) {
        // The ladder belongs to the nozzle diameter, not to a colour. Colours sharing one diameter must
        // use the same phase; ownership changes only at the common band's anchor layer.
        return build_adaptive_layer_bands(layer_heights, band_size, range, band_tops);
    };
    // Two ladders of incommensurate band sizes (a 0.6 and a 0.8 on the same grid) make a colour's band top
    // land on a layer the other colour owns over and over. Anchoring on that layer would release the whole
    // band, so the top is pulled down to the last layer this colour really owns - the same shortening
    // compute_adaptive_merge_groups() applies, which is what keeps the two passes describing one band.
    auto pull_painted_band = [&](const AdaptiveLayerBand &src, size_t extruder_id,
                                 const AdaptiveLayerHeightRange &range, AdaptiveLayerBand &out) {
        out = src;
        if (src.bottom_idx == 0)
            return true;
        std::vector<unsigned char> has_material(src.top_idx + 1, 0);
        for (size_t layer_idx = src.bottom_idx;
             layer_idx <= src.top_idx && layer_idx < original_segmentation.size(); ++ layer_idx)
            has_material[layer_idx] = ! original_segmentation[layer_idx][extruder_id].empty();
        return pull_adaptive_band_top_to_material(out, layer_heights, range, has_material);
    };
    // Ownership on a band's anchor layer is a real extrusion. Protect it from another nozzle whose
    // differently aligned band merely passes through that layer on the way to its own anchor.
    std::vector<std::vector<ExPolygons>> painted_anchor_claims(
        layer_count, std::vector<ExPolygons>(extruder_count));
    for (size_t extruder_id = 0; extruder_id < extruder_count; ++ extruder_id) {
        const double nozzle = extruder_nozzles[extruder_id];
        if (nozzle <= EPSILON)
            continue;
        const size_t band_size = adaptive_layer_band_size(nozzle, reference_nozzle, base_layer_height);
        if (band_size < 2) {
            // An unmerged nozzle anchors a real extrusion on every physical layer.
            for (size_t layer_idx = 0; layer_idx < layer_count; ++ layer_idx)
                painted_anchor_claims[layer_idx][extruder_id] =
                    original_segmentation[layer_idx][extruder_id];
            continue;
        }
        const AdaptiveLayerHeightRange range = adaptive_layer_height_range(nozzle);
        for (const AdaptiveLayerBand &band : build_painted_bands(band_size, range)) {
            AdaptiveLayerBand live;
            if (! pull_painted_band(band, extruder_id, range, live))
                continue;
            const size_t anchor_idx = band.bottom_idx == 0 ? 0 : live.top_idx;
            painted_anchor_claims[anchor_idx][extruder_id] =
                original_segmentation[anchor_idx][extruder_id];
        }
    }
    auto other_anchor_claims = [&](size_t layer_idx, size_t extruder_id) {
        Polygons protected_area;
        for (size_t other_id = 0; other_id < extruder_count; ++ other_id)
            if (other_id != extruder_id)
                polygons_append(protected_area, to_polygons(painted_anchor_claims[layer_idx][other_id]));
        return protected_area.empty() ? ExPolygons{} : union_ex(protected_area);
    };
    auto all_anchor_claims = [&](size_t layer_idx) {
        Polygons protected_area;
        for (size_t extruder_id = 0; extruder_id < extruder_count; ++ extruder_id)
            polygons_append(protected_area, to_polygons(painted_anchor_claims[layer_idx][extruder_id]));
        return protected_area.empty() ? ExPolygons{} : union_ex(protected_area);
    };
    for (size_t extruder_id = 0; extruder_id < extruder_count; ++ extruder_id) {
        const double nozzle = extruder_nozzles[extruder_id];
        if (nozzle <= EPSILON)
            continue;
        const size_t band_size = adaptive_layer_band_size(nozzle, reference_nozzle, base_layer_height);
        if (band_size < 2)
            continue;
        const AdaptiveLayerHeightRange range = adaptive_layer_height_range(nozzle);
        const std::vector<AdaptiveLayerBand> bands = build_painted_bands(band_size, range);
        tbb::parallel_for(tbb::blocked_range<size_t>(0, bands.size()), [&](const tbb::blocked_range<size_t> &band_range) {
            for (size_t band_idx = band_range.begin(); band_idx < band_range.end(); ++ band_idx) {
                throw_on_cancel();
                const AdaptiveLayerBand &band = bands[band_idx];
                AdaptiveLayerBand live;
                if (! pull_painted_band(band, extruder_id, range, live))
                    continue;
                const size_t anchor_idx = band.bottom_idx == 0 ? 0 : live.top_idx;
                for (size_t layer_idx = live.bottom_idx; layer_idx <= live.top_idx; ++ layer_idx) {
                    if (layer_idx == anchor_idx)
                        continue;
                    const ExPolygons vacated = diff_ex(original_segmentation[layer_idx][extruder_id],
                                                       original_segmentation[anchor_idx][extruder_id]);
                    if (vacated.empty())
                        continue;
                    for (size_t other_id = 0; other_id < extruder_count; ++ other_id) {
                        if (other_id == extruder_id || original_segmentation[anchor_idx][other_id].empty())
                            continue;
                        ExPolygons restored = intersection_ex(vacated, original_segmentation[anchor_idx][other_id]);
                        if (restored.empty())
                            continue;
                        const double before = area_mm2(segmentation[layer_idx][other_id]);
                        Polygons combined = to_polygons(segmentation[layer_idx][other_id]);
                        polygons_append(combined, to_polygons(restored));
                        segmentation[layer_idx][other_id] = union_ex(combined);
                        BOOST_LOG_TRIVIAL(debug) << "[mm_band]   restore E" << extruder_id
                                                 << " band=[" << live.bottom_idx << "," << live.top_idx << "]"
                                                 << " layer=" << layer_idx << " -> E" << other_id
                                                 << " " << before << " -> " << area_mm2(segmentation[layer_idx][other_id]);
                    }
                }
            }
        });
    }

    // Ascending extruder id, the order painted_regions are applied in, so a contested area goes to the
    // same extruder here as it would in apply_mm_segmentation() below.
    std::vector<ExPolygons> claimed(layer_count);
    for (size_t extruder_id = 0; extruder_id < extruder_count; ++ extruder_id) {
        const double nozzle = extruder_nozzles[extruder_id];
        if (nozzle <= EPSILON)
            continue;
        const size_t band_size = adaptive_layer_band_size(nozzle, reference_nozzle, base_layer_height);
        if (band_size < 2) {
            BOOST_LOG_TRIVIAL(debug) << "[mm_band] pass1 E" << extruder_id << " d=" << nozzle
                                     << " band_size=" << band_size << " -> prints every layer, untouched";
            continue;
        }
        const AdaptiveLayerHeightRange range = adaptive_layer_height_range(nozzle);
        const std::vector<AdaptiveLayerBand> bands = build_painted_bands(band_size, range);
        BOOST_LOG_TRIVIAL(debug) << "[mm_band] pass1 E" << extruder_id << " d=" << nozzle
                                 << " band_size=" << band_size << " bands=" << bands.size();
        tbb::parallel_for(tbb::blocked_range<size_t>(0, bands.size()), [&](const tbb::blocked_range<size_t> &band_range) {
            for (size_t band_idx = band_range.begin(); band_idx < band_range.end(); ++ band_idx) {
                throw_on_cancel();
                const AdaptiveLayerBand &band = bands[band_idx];
                AdaptiveLayerBand live;
                if (! pull_painted_band(band, extruder_id, range, live))
                    continue;
                // The band shows one face over its whole Z range and takes its ownership from that face: the
                // top layer normally, the object base for the band that starts at layer 0. That band is the
                // bottom of the print, so paint reaching it only down a vertical wall must not claim it.
                const bool       anchored_on_base = band.bottom_idx == 0;
                const size_t     anchor_idx       = anchored_on_base ? 0 : live.top_idx;
                const ExPolygons retained         = segmentation[anchor_idx][extruder_id];
                for (size_t layer_idx = live.bottom_idx; layer_idx <= live.top_idx; ++ layer_idx) {
                    const double before = area_mm2(segmentation[layer_idx][extruder_id]);
                    // The band lays the top contour down and nothing else, so a footprint this extruder painted
                    // further down is not extruded at all. It still belongs to this nozzle: releasing it lets the
                    // base filament print it layer by layer inside the Z range the band already owns, which comes
                    // out as a rim of the model colour tracing every band step - a colour change on every layer
                    // instead of the step the decimation implies. Only what another colour holds on the band top
                    // is given up, and the restore pass above has moved exactly that to its owner already.
                    ExPolygons own = retained;
                    // Never on a base-anchored band: what it would recover there is exactly the side paint the
                    // first band must not carry, and layer 0 has already said who owns the bottom face.
                    if (! retained.empty() && ! anchored_on_base && layer_idx != anchor_idx) {
                        ExPolygons orphaned = diff_ex(original_segmentation[layer_idx][extruder_id],
                                                      original_segmentation[anchor_idx][extruder_id]);
                        for (size_t other_id = 0; other_id < extruder_count && ! orphaned.empty(); ++ other_id)
                            if (other_id != extruder_id && ! original_segmentation[anchor_idx][other_id].empty())
                                orphaned = diff_ex(orphaned, original_segmentation[anchor_idx][other_id]);
                        if (! orphaned.empty()) {
                            const double orphaned_area = area_mm2(orphaned);
                            const double retained_area = area_mm2(retained);
                            BOOST_LOG_TRIVIAL(debug) << "[mm_band]   pass1 E" << extruder_id
                                                     << " band=[" << live.bottom_idx << "," << live.top_idx << "]"
                                                     << " layer=" << layer_idx << " keeps vacated "
                                                     << orphaned_area << " unclaimed by any other colour";
                            // What is kept here is extruded on the band top and nowhere else, so a top holding
                            // less than this layer owns means the forced-boundary pass above missed a step:
                            // the kept area has no merged extrusion to land on and no other colour prints it.
                            if (orphaned_area > retained_area)
                                BOOST_LOG_TRIVIAL(warning) << "[mm_band]   pass1 E" << extruder_id
                                                           << " band=[" << live.bottom_idx << "," << live.top_idx << "]"
                                                           << " layer=" << layer_idx << " keeps " << orphaned_area
                                                           << " the band top cannot carry (top holds "
                                                           << retained_area << ")";
                            Polygons combined = to_polygons(own);
                            polygons_append(combined, to_polygons(orphaned));
                            own = union_ex(combined);
                        }
                    }
                    const ExPolygons protected_area = other_anchor_claims(layer_idx, extruder_id);
                    if (! own.empty() && ! protected_area.empty())
                        own = diff_ex(own, protected_area);
                    ExPolygons area = own.empty() || claimed[layer_idx].empty() ?
                        std::move(own) : diff_ex(own, claimed[layer_idx]);
                    double stolen = 0.;
                    if (! area.empty()) {
                        // One extrusion fills the band's whole Z range, so no other extruder can keep a share
                        // of these layers.
                        for (size_t other_id = 0; other_id < extruder_count; ++ other_id)
                            if (other_id != extruder_id && ! segmentation[layer_idx][other_id].empty()) {
                                const double other_before = area_mm2(segmentation[layer_idx][other_id]);
                                // Preserve the colour ownership even when the remainder is narrow. Returning
                                // such a boundary strip to the base region inserts a third colour between the
                                // two painted colours; the wall generator is the right place to decide whether
                                // it becomes a variable-width wall or gap fill.
                                segmentation[layer_idx][other_id] =
                                    diff_ex(segmentation[layer_idx][other_id], area);
                                stolen += other_before - area_mm2(segmentation[layer_idx][other_id]);
                            }
                        Polygons combined = to_polygons(claimed[layer_idx]);
                        polygons_append(combined, to_polygons(area));
                        claimed[layer_idx] = union_ex(combined);
                    }
                    const double after = area_mm2(area);
                    if (std::abs(after - before) > 1e-4 || stolen > 1e-4)
                        BOOST_LOG_TRIVIAL(debug) << "[mm_band]   pass1 E" << extruder_id
                                                 << " band=[" << live.bottom_idx << "," << live.top_idx << "]"
                                                 << " layer=" << layer_idx
                                                 << (layer_idx == anchor_idx ? " (anchor)" : "")
                                                 << " own " << before << " -> " << after
                                                 << " took_from_others=" << stolen;
                    segmentation[layer_idx][extruder_id] = std::move(area);
                }
            }
        });
    }

    // The unpainted footprint prints in bands as well, and the band carrying the object's top surface has
    // to own its whole Z range. A colour painted on a side wall that keeps extruding on the layers inside
    // that band leaves the merged slab nowhere to go: compute_adaptive_merge_groups() trims the slab
    // against what the finer nozzle already laid down, so the top face comes out short of the model
    // outline by that strip and stops being the shape the user drew.
    auto base_at = [&](size_t layer_idx, double diameter) -> ExPolygons {
        Polygons base;
        for (const LayerRegion *layerm : print_object.get_layer(int(layer_idx))->regions())
            if (! layerm->slices.surfaces.empty() &&
                std::abs(nozzle_diameter_for_filament(print_config, layerm->region().config().wall_filament) - diameter) <= EPSILON)
                polygons_append(base, to_polygons(layerm->slices.surfaces));
        return base.empty() ? ExPolygons{} : union_ex(base);
    };
    auto unpainted_at = [&](size_t layer_idx, double diameter) -> ExPolygons {
        ExPolygons base = base_at(layer_idx, diameter);
        if (base.empty()) {
            BOOST_LOG_TRIVIAL(debug) << "[mm_band]   unpainted_at layer=" << layer_idx << " d=" << diameter
                                     << " no region carries this diameter -> empty";
            return {};
        }
        Polygons painted;
        for (size_t extruder_id = 0; extruder_id < extruder_count; ++ extruder_id)
            polygons_append(painted, to_polygons(segmentation[layer_idx][extruder_id]));
        if (painted.empty()) {
            BOOST_LOG_TRIVIAL(debug) << "[mm_band]   unpainted_at layer=" << layer_idx << " d=" << diameter
                                     << " slice=" << area_mm2(base) << " painted=0 -> whole slice";
            return base;
        }
        // Only what this nozzle could actually lay down may evict paint. A layer the paint covers entirely
        // still differs from its own slice by a hairline, and keeping that hairline would slit the wider
        // layers below this one in two - it falls inside their paint, not outside it - and the wall
        // generator would then follow the slit instead of the model outline.
        const ExPolygons raw  = diff_ex(base, union_ex(painted));
        const ExPolygons kept = printable_pieces(raw, diameter);
        BOOST_LOG_TRIVIAL(debug) << "[mm_band]   unpainted_at layer=" << layer_idx << " d=" << diameter
                                 << " slice=" << area_mm2(base)
                                 << " painted=" << area_mm2(union_ex(painted))
                                 << " raw_unpainted=" << area_mm2(raw) << " (" << raw.size() << " pieces)"
                                 << " printable=" << area_mm2(kept) << " (" << kept.size() << " pieces)";
        return kept;
    };
    std::vector<ExPolygons> base_claimed(layer_count);
    for (double diameter : base_nozzles) {
        const size_t band_size = adaptive_layer_band_size(diameter, reference_nozzle, base_layer_height);
        if (band_size < 2) {
            BOOST_LOG_TRIVIAL(debug) << "[mm_band] pass2 base d=" << diameter << " band_size=" << band_size
                                     << " -> prints every layer, untouched";
            continue;
        }
        const AdaptiveLayerHeightRange range = adaptive_layer_height_range(diameter);
        const std::vector<AdaptiveLayerBand> bands =
            build_adaptive_layer_bands(layer_heights, band_size, range, band_tops);
        BOOST_LOG_TRIVIAL(debug) << "[mm_band] pass2 base d=" << diameter << " band_size=" << band_size
                                 << " bands=" << bands.size();
        tbb::parallel_for(tbb::blocked_range<size_t>(0, bands.size()), [&](const tbb::blocked_range<size_t> &band_range) {
            for (size_t band_idx = band_range.begin(); band_idx < band_range.end(); ++ band_idx) {
                throw_on_cancel();
                const AdaptiveLayerBand &band = bands[band_idx];
                AdaptiveLayerBand live = band;
                // A band carries one contour over its whole Z range, and that contour is taken from the face
                // the band shows: the top layer for an ordinary band, the object base for the first one. The
                // first band is the bottom face of the print, so a colour painted on a vertical wall must not
                // decide its ownership - it starts above the band, exactly as a side colour ends below the
                // band carrying the top face.
                const bool anchored_on_base = band.bottom_idx == 0;
                if (! anchored_on_base && unpainted_at(band.top_idx, diameter).empty()) {
                    std::vector<unsigned char> has_material(band.top_idx + 1, 0);
                    for (size_t layer_idx = band.bottom_idx; layer_idx <= band.top_idx; ++ layer_idx)
                        has_material[layer_idx] = ! unpainted_at(layer_idx, diameter).empty();
                    if (! pull_adaptive_band_top_to_material(live, layer_heights, range, has_material))
                        continue;
                }
                const size_t     anchor_idx = anchored_on_base ? 0 : live.top_idx;
                const ExPolygons retained   = unpainted_at(anchor_idx, diameter);
                if (retained.empty())
                    continue;
                for (size_t layer_idx = live.bottom_idx; layer_idx <= live.top_idx; ++ layer_idx) {
                    ExPolygons area = retained;
                    const ExPolygons protected_area = all_anchor_claims(layer_idx);
                    if (! protected_area.empty())
                        area = diff_ex(area, protected_area);
                    if (area.empty())
                        continue;
                    Polygons claimed = to_polygons(base_claimed[layer_idx]);
                    polygons_append(claimed, to_polygons(area));
                    base_claimed[layer_idx] = union_ex(claimed);
                    for (size_t other_id = 0; other_id < extruder_count; ++ other_id)
                        if (! segmentation[layer_idx][other_id].empty()) {
                            const double before = area_mm2(segmentation[layer_idx][other_id]);
                            segmentation[layer_idx][other_id] = printable_pieces(
                                diff_ex(segmentation[layer_idx][other_id], area), extruder_nozzles[other_id]);
                            const double after = area_mm2(segmentation[layer_idx][other_id]);
                            if (before - after > 1e-4)
                                BOOST_LOG_TRIVIAL(debug) << "[mm_band]   pass2 d=" << diameter
                                                         << " band=[" << live.bottom_idx << "," << live.top_idx << "]"
                                                         << " layer=" << layer_idx
                                                         << (layer_idx == anchor_idx ? " (anchor)" : "")
                                                         << " E" << other_id << " " << before << " -> " << after
                                                         << " lost=" << (before - after)
                                                         << " pieces=" << segmentation[layer_idx][other_id].size();
                        }
                }
            }
        });
    }

    // The handovers above cannot be made to agree on their own. An extruder decides what to give up from
    // the paint of its own band top, while what the receiver actually keeps is decided by the receiver's
    // band top and by every extruder that takes a share of that layer after it. Where the two disagree the
    // footprint is left owned by nobody, and nobody means the region's own filament: that is the finest
    // nozzle of the plate, the one nozzle that prints every layer, so a strip too narrow for any painted
    // nozzle still reaches the plate - as a line of the model colour drawn between two painted ones.
    //
    // Rather than chase every handover, hold the invariant that matters: paint that went into this pass
    // comes out of it painted. The colour holding most of a lost piece takes it back, which is the colour
    // that was drawn there. Pieces too small for even the reference nozzle are left alone - they never
    // reach an extruder, and re-uniting the fringe of every colour boundary would cost far more than it
    // could ever change.
    //
    // Each layer reads only its own paint and writes only its own segmentation, so the layers are settled in
    // parallel. Within a layer a piece is a sliver while the paint it is matched against spans the whole
    // layer: clipping that paint to the piece's bounding box first leaves the intersection unchanged and
    // keeps Clipper from walking every edge of the layer once per piece. An owner is chosen from the paint
    // before this pass, never from what an earlier piece reclaimed, so merging all pieces of one owner in a
    // single union yields the same footprint as merging them one by one.
    const double reclaim_min_area = 0.25 * reference_nozzle * reference_nozzle;
    tbb::parallel_for(tbb::blocked_range<size_t>(0, layer_count), [&](const tbb::blocked_range<size_t> &range) {
        for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
            throw_on_cancel();
            Polygons before;
            Polygons after;
            for (size_t extruder_id = 0; extruder_id < extruder_count; ++ extruder_id) {
                polygons_append(before, to_polygons(original_segmentation[layer_idx][extruder_id]));
                polygons_append(after, to_polygons(segmentation[layer_idx][extruder_id]));
            }
            if (before.empty())
                continue;
            ExPolygons lost_areas = after.empty() ? union_ex(before) : diff_ex(union_ex(before), union_ex(after));
            if (! base_claimed[layer_idx].empty())
                lost_areas = diff_ex(lost_areas, base_claimed[layer_idx]);
            std::vector<Polygons> reclaimed(extruder_count);
            for (const ExPolygon &piece : lost_areas) {
                const ExPolygons one { piece };
                const double     lost = area_mm2(one);
                if (lost < reclaim_min_area)
                    continue;
                const BoundingBox piece_bbox = get_extents(piece).inflated(SCALED_EPSILON);
                size_t owner = extruder_count;
                double held  = 0.;
                for (size_t extruder_id = 0; extruder_id < extruder_count; ++ extruder_id) {
                    const ExPolygons &paint = original_segmentation[layer_idx][extruder_id];
                    if (paint.empty())
                        continue;
                    const Polygons clip = ClipperUtils::clip_clipper_polygons_with_subject_bbox(paint, piece_bbox);
                    if (clip.empty())
                        continue;
                    const double painted = area_mm2(intersection_ex(one, clip));
                    if (painted > held) {
                        held  = painted;
                        owner = extruder_id;
                    }
                }
                if (owner == extruder_count)
                    continue;
                polygons_append(reclaimed[owner], to_polygons(one));
                BOOST_LOG_TRIVIAL(debug) << "[mm_band]   reclaim layer=" << layer_idx << " E" << owner << " lost=" << lost;
            }
            for (size_t owner = 0; owner < extruder_count; ++ owner) {
                if (reclaimed[owner].empty())
                    continue;
                const double was = area_mm2(segmentation[layer_idx][owner]);
                polygons_append(reclaimed[owner], to_polygons(segmentation[layer_idx][owner]));
                segmentation[layer_idx][owner] = union_ex(reclaimed[owner]);
                BOOST_LOG_TRIVIAL(debug) << "[mm_band]   reclaim layer=" << layer_idx << " E" << owner << " "
                                         << was << " -> " << area_mm2(segmentation[layer_idx][owner]);
            }
        }
    });

    // A coarse colour holding a footprint for fewer layers than its nozzle's minimum height cannot extrude
    // it: the band would have to reach into the colours above and below, and a fine colour there prints every
    // layer and cannot give way. Typical cases are a 0.1 mm stroke of a 0.8 colour on its band's bottom layer,
    // or a 0.8 body that 0.2 paint above and below leaves 0.1 mm of. Left alone, the merge planner has no
    // same-colour slab to fold it into, and the step stays empty across the whole footprint.
    //
    // Such a piece takes the colour standing on it one layer up, and that colour plans it like any other
    // footprint it owns: a fine nozzle prints it as sliced, a coarse one merges it with its own slab above.
    // This is what single-diameter printing does with paint that misses a layer's sampling height - the colour
    // drawn there is not the colour printed. What no other colour stands on goes to the colour below, and what
    // has neither keeps its colour. The first band of each ladder rests on the bed and keeps its own rule.
    {
        struct Owner {
            // The unpainted footprint of the regions carrying this nozzle, or a painted extruder.
            bool                     base;
            size_t                   extruder_id;
            double                   nozzle;
            AdaptiveLayerHeightRange range;
            // Last layer of the first band; layers up to it are left alone. -1 when this nozzle prints every
            // layer and nothing of it can be thin.
            long long                first_band_top;
        };
        std::vector<Owner> owners;
        auto add_owner = [&](bool base, size_t extruder_id, double nozzle) {
            const size_t band_size = adaptive_layer_band_size(nozzle, reference_nozzle, base_layer_height);
            const AdaptiveLayerHeightRange range = adaptive_layer_height_range(nozzle);
            long long first_band_top = -1;
            if (band_size >= 2) {
                const std::vector<AdaptiveLayerBand> bands = build_adaptive_layer_bands(layer_heights, band_size, range, band_tops);
                first_band_top = bands.empty() || bands.front().bottom_idx != 0 ? 0 : (long long) bands.front().top_idx;
            }
            owners.push_back({ base, extruder_id, nozzle, range, band_size >= 2 ? first_band_top : -1 });
        };
        for (size_t extruder_id = 0; extruder_id < extruder_count; ++ extruder_id)
            if (extruder_nozzles[extruder_id] > EPSILON)
                add_owner(false, extruder_id, extruder_nozzles[extruder_id]);
        for (double diameter : base_nozzles)
            add_owner(true, 0, diameter);

        // The unpainted footprint is recomputed only for a layer whose paint changed.
        std::vector<std::vector<ExPolygons>> unpainted_cache(owners.size(), std::vector<ExPolygons>(layer_count));
        std::vector<std::vector<unsigned char>> unpainted_valid(owners.size(), std::vector<unsigned char>(layer_count, 0));
        auto owned = [&](const Owner &owner, size_t layer_idx) -> ExPolygons {
            if (! owner.base)
                return segmentation[layer_idx][owner.extruder_id];
            const size_t owner_idx = &owner - owners.data();
            if (! unpainted_valid[owner_idx][layer_idx]) {
                ExPolygons base = base_at(layer_idx, owner.nozzle);
                if (! base.empty()) {
                    Polygons painted;
                    for (size_t extruder_id = 0; extruder_id < extruder_count; ++ extruder_id)
                        polygons_append(painted, to_polygons(segmentation[layer_idx][extruder_id]));
                    if (! painted.empty())
                        base = diff_ex(base, union_ex(painted));
                }
                unpainted_cache[owner_idx][layer_idx] = std::move(base);
                unpainted_valid[owner_idx][layer_idx] = 1;
            }
            return unpainted_cache[owner_idx][layer_idx];
        };
        auto paint_changed = [&](size_t layer_idx) {
            for (std::vector<unsigned char> &valid : unpainted_valid)
                valid[layer_idx] = 0;
        };
        auto span_height = [&layer_heights](size_t bottom_idx, size_t top_idx) {
            double height = 0.;
            for (size_t layer_idx = bottom_idx; layer_idx <= top_idx; ++ layer_idx)
                height += layer_heights[layer_idx];
            return height;
        };

        // Bottom to top, so a piece given to the colour above already lengthens that colour's run when the
        // layers above are judged.
        for (size_t layer_idx = 0; layer_idx < layer_count; ++ layer_idx) {
            throw_on_cancel();
            for (const Owner &owner : owners) {
                if (owner.first_band_top < 0 || (long long) layer_idx <= owner.first_band_top)
                    continue;
                const ExPolygons here = owned(owner, layer_idx);
                if (here.empty())
                    continue;

                // Whatever this colour holds through some run of layers around this one that reaches its
                // minimum height is printable. Only the shortest such runs need checking: for each bottom, the
                // lowest top that reaches the height; from the bottom where this layer alone reaches it on, a
                // lower bottom only gives a longer run.
                Polygons printable;
                for (size_t bottom_idx = layer_idx + 1; bottom_idx-- > 0;) {
                    size_t top_idx = layer_idx;
                    while (top_idx + 1 < layer_count && span_height(bottom_idx, top_idx) < owner.range.min_height - EPSILON)
                        ++ top_idx;
                    if (span_height(bottom_idx, top_idx) >= owner.range.min_height - EPSILON) {
                        ExPolygons run = here;
                        for (size_t idx = bottom_idx; idx <= top_idx && ! run.empty(); ++ idx)
                            if (idx != layer_idx)
                                run = intersection_ex(run, owned(owner, idx));
                        polygons_append(printable, to_polygons(run));
                    }
                    if (span_height(bottom_idx, layer_idx) >= owner.range.min_height - EPSILON)
                        break;
                }
                ExPolygons thin = printable.empty() ? here : diff_ex(here, union_ex(printable));
                // Pieces even the finest nozzle could not print - hairlines between two polygonisations - are
                // left where they are.
                thin = printable_pieces(std::move(thin), reference_nozzle);
                if (thin.empty())
                    continue;

                for (size_t neighbour_idx : { layer_idx + 1, layer_idx - 1 }) {
                    if (thin.empty() || neighbour_idx >= layer_count)
                        continue;
                    for (const Owner &receiver : owners) {
                        if (thin.empty())
                            break;
                        if (&receiver == &owner || (receiver.base && owner.base))
                            continue;
                        ExPolygons taken = intersection_ex(thin, owned(receiver, neighbour_idx));
                        // Removing the paint hands the footprint to whichever region holds it on this layer, which
                        // is the receiver only where that region is there too: two parts may meet at this layer.
                        if (receiver.base && ! taken.empty())
                            taken = intersection_ex(taken, base_at(layer_idx, receiver.nozzle));
                        if (taken.empty())
                            continue;
                        // Painted area leaves its extruder; the unpainted footprint is whatever no paint covers,
                        // so it is given up by painting it and taken back by removing the paint.
                        if (! owner.base)
                            segmentation[layer_idx][owner.extruder_id] =
                                diff_ex(segmentation[layer_idx][owner.extruder_id], taken);
                        if (! receiver.base) {
                            Polygons combined = to_polygons(segmentation[layer_idx][receiver.extruder_id]);
                            polygons_append(combined, to_polygons(taken));
                            segmentation[layer_idx][receiver.extruder_id] = union_ex(combined);
                        }
                        paint_changed(layer_idx);
                        thin = diff_ex(thin, taken);
                        BOOST_LOG_TRIVIAL(debug) << "[mm_band]   thin layer=" << layer_idx
                                                 << (owner.base ? " base d=" + std::to_string(owner.nozzle) :
                                                                  " E" + std::to_string(owner.extruder_id))
                                                 << " -> " << (neighbour_idx > layer_idx ? "above" : "below")
                                                 << (receiver.base ? " base d=" + std::to_string(receiver.nozzle) :
                                                                     " E" + std::to_string(receiver.extruder_id))
                                                 << " area=" << area_mm2(taken);
                    }
                }
            }
        }
    }
}

template<typename ThrowOnCancel>
static inline void apply_mm_segmentation(PrintObject &print_object, ThrowOnCancel throw_on_cancel)
{
    // Returns MMU segmentation based on painting in MMU segmentation gizmo
    std::vector<std::vector<ExPolygons>> segmentation = multi_material_segmentation_by_painting(print_object, throw_on_cancel);
    assert(segmentation.size() == print_object.layer_count());
    normalize_mm_segmentation_to_layer_bands(print_object, segmentation, throw_on_cancel);
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

template<typename ThrowOnCancel> void apply_fuzzy_skin_segmentation(PrintObject &print_object, ThrowOnCancel throw_on_cancel)
{
    // Returns fuzzy skin segmentation based on painting in the fuzzy skin painting gizmo.
    std::vector<std::vector<ExPolygons>> segmentation = fuzzy_skin_segmentation_by_painting(print_object, throw_on_cancel);
    assert(segmentation.size() == print_object.layer_count());

    struct ByRegion
    {
        ExPolygons expolygons;
        bool       needs_merge{false};
    };

    tbb::parallel_for(tbb::blocked_range<size_t>(0, segmentation.size(), std::max(segmentation.size() / 128, size_t(1))),
        [&print_object, &segmentation, throw_on_cancel](const tbb::blocked_range<size_t> &range) {
        const auto &layer_ranges   = print_object.shared_regions()->layer_ranges;
        auto        it_layer_range = layer_range_first(layer_ranges, print_object.get_layer(int(range.begin()))->slice_z);

        for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++layer_idx) {
            throw_on_cancel();

            Layer &layer                                             = *print_object.get_layer(int(layer_idx));
            it_layer_range                                           = layer_range_next(layer_ranges, it_layer_range, layer.slice_z);
            const PrintObjectRegions::LayerRangeRegions &layer_range = *it_layer_range;

            //assert(segmentation[layer_idx].size() == 1);
            const ExPolygons &fuzzy_skin_segmentation      = segmentation[layer_idx][1];
            const BoundingBox fuzzy_skin_segmentation_bbox = get_extents(fuzzy_skin_segmentation);
            if (fuzzy_skin_segmentation.empty())
                continue;

            // Split LayerRegions by painted fuzzy skin regions.
            // layer_range.fuzzy_skin_painted_regions are sorted by parent PrintObject region ID.
            std::vector<ByRegion> by_region(layer.region_count());
            auto                  it_fuzzy_skin_region_begin = layer_range.fuzzy_skin_painted_regions.cbegin();
            for (int parent_layer_region_idx = 0; parent_layer_region_idx < layer.region_count(); ++parent_layer_region_idx) {
                if (it_fuzzy_skin_region_begin == layer_range.fuzzy_skin_painted_regions.cend()) continue;

                const LayerRegion &parent_layer_region = *layer.get_region(parent_layer_region_idx);
                const PrintRegion &parent_print_region = parent_layer_region.region();
                assert(parent_print_region.print_object_region_id() == parent_layer_region_idx);
                if (parent_layer_region.slices.empty())
                    continue;

                // Find the first FuzzySkinPaintedRegion, which overrides the parent PrintRegion.
                auto it_fuzzy_skin_region = std::find_if(it_fuzzy_skin_region_begin, layer_range.fuzzy_skin_painted_regions.cend(),
                                                         [&layer_range, &parent_print_region](const auto &fuzzy_skin_region) {
                                                             return fuzzy_skin_region.parent_print_object_region_id(layer_range) == parent_print_region.print_object_region_id();
                                                         });

                if (it_fuzzy_skin_region == layer_range.fuzzy_skin_painted_regions.cend()) continue; // This LayerRegion isn't overrides by any FuzzySkinPaintedRegion.

                assert(it_fuzzy_skin_region->parent_print_object_region(layer_range) == &parent_print_region);

                // Update the beginning FuzzySkinPaintedRegion iterator for the next iteration.
                it_fuzzy_skin_region_begin = std::next(it_fuzzy_skin_region);

                const BoundingBox parent_layer_region_bbox        = get_extents(parent_layer_region.slices.surfaces);
                Polygons          layer_region_remaining_polygons = to_polygons(parent_layer_region.slices.surfaces);
                // Don't trim by self, it is not reliable.
                if (parent_layer_region_bbox.overlap(fuzzy_skin_segmentation_bbox) && it_fuzzy_skin_region->region != &parent_print_region) {
                    // Steal from this region.
                    const int  target_region_id = it_fuzzy_skin_region->region->print_object_region_id();
                    ExPolygons stolen           = intersection_ex(parent_layer_region.slices.surfaces, fuzzy_skin_segmentation);
                    if (!stolen.empty()) {
                        ByRegion &dst = by_region[target_region_id];
                        if (dst.expolygons.empty()) {
                            dst.expolygons = std::move(stolen);
                        } else {
                            append(dst.expolygons, std::move(stolen));
                            dst.needs_merge = true;
                        }
                    }

                    // Trim slices of this LayerRegion by the fuzzy skin region.
                    layer_region_remaining_polygons = diff(layer_region_remaining_polygons, fuzzy_skin_segmentation);

                    // Filter out unprintable polygons. Detailed explanation is inside apply_mm_segmentation.
                    if (!layer_region_remaining_polygons.empty()) {
                        layer_region_remaining_polygons = opening(union_ex(layer_region_remaining_polygons), scaled<float>(5. * EPSILON), scaled<float>(5. * EPSILON));
                    }
                }

                if (!layer_region_remaining_polygons.empty()) {
                    ByRegion &dst = by_region[parent_print_region.print_object_region_id()];
                    if (dst.expolygons.empty()) {
                        dst.expolygons = union_ex(layer_region_remaining_polygons);
                    } else {
                        append(dst.expolygons, union_ex(layer_region_remaining_polygons));
                        dst.needs_merge = true;
                    }
                }
            }

            // Re-create Surfaces of LayerRegions.
            for (int region_id = 0; region_id < layer.region_count(); ++region_id) {
                ByRegion &src = by_region[region_id];
                if (src.needs_merge) {
                    // Multiple regions were merged into one.
                    src.expolygons = closing_ex(src.expolygons, scaled<float>(10. * EPSILON));
                }

                layer.get_region(region_id)->slices.set(std::move(src.expolygons), stInternal);
            }
        }
    }); // end of parallel_for
}

// MMU cannot apply filament shrinkage compensation.
static bool should_skip_filament_shrink(const PrintObject &object)
{
    const Print *print = object.print();
    if (print == nullptr || print->config().filament_diameter.size() <= 1 || ! object.is_mm_painted())
        return false;

    const std::vector<double> &filament_shrink = print->config().filament_shrink.values;
    const std::vector<unsigned int> object_filaments = object.object_extruders();
    return std::any_of(object_filaments.begin(), object_filaments.end(), [&filament_shrink](unsigned int extruder) {
        return extruder < filament_shrink.size() && filament_shrink[extruder] != 0. && filament_shrink[extruder] != 100.;
    });
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
void PrintObject::slice_volumes(long long *region_split_ms_out, long long *mm_segment_ms_out)
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

    std::vector<float>                   slice_zs      = zs_from_layers(m_layers);
    std::vector<VolumeSlices> objSliceByVolume;
    if (!slice_zs.empty()) {
        objSliceByVolume = slice_volumes_inner(
            print->config(), this->config(), this->trafo_centered(),
            this->model_object()->volumes, m_shared_regions->layer_ranges, slice_zs, throw_on_cancel_callback, print);
    }

    //BBS: "model_part" volumes are grouded according to their connections
    //const auto           scaled_resolution = scaled<double>(print->config().resolution.value);
    //firstLayerObjSliceByVolume = findPartVolumes(objSliceByVolume, this->model_object()->volumes);
    //groupingVolumes(objSliceByVolumeParts, firstLayerObjSliceByGroups, scaled_resolution);
    //applyNegtiveVolumes(this->model_object()->volumes, objSliceByVolume, firstLayerObjSliceByGroups, scaled_resolution);
    firstLayerObjSliceByVolume = objSliceByVolume;

    print->set_status(10, L("Slicing mesh"));
    const long long region_split_begin_time = Slic3r::Utils::get_current_milliseconds_time_monotonic();
    std::vector<std::vector<ExPolygons>> region_slices = slices_to_regions(this->model_object()->volumes, *m_shared_regions, slice_zs,
        std::move(objSliceByVolume),
        PrintObject::clip_multipart_objects,
        print->config().enable_order_independent_overlap_carving.value,
        throw_on_cancel_callback);

    for (size_t region_id = 0; region_id < region_slices.size(); ++ region_id) {
        std::vector<ExPolygons> &by_layer = region_slices[region_id];
        for (size_t layer_id = 0; layer_id < by_layer.size(); ++ layer_id)
            m_layers[layer_id]->regions()[region_id]->slices.append(std::move(by_layer[layer_id]), stInternal);
    }
    region_slices.clear();
    const long long region_split_time =
        Slic3r::Utils::get_current_milliseconds_time_monotonic() - region_split_begin_time;
    if (region_split_ms_out)
        *region_split_ms_out += region_split_time;
    if (m_print->m_slice_time)
        (*m_print->m_slice_time)[TIME_REGION_SPLIT] += region_split_time;

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

    const bool skip_filament_shrink = should_skip_filament_shrink(*this);

    if (skip_filament_shrink) {
        this->active_step_add_warning(
            PrintStateBase::WarningLevel::CRITICAL,
            L("Shrinkage compensation does not take effect on color-painted models."));
        BOOST_LOG_TRIVIAL(info) << "filament shrink compensation will not work for object " << this->model_object()->name << " for multi filament.";
    }

    // Is any ModelVolume MMU painted?
    if (const auto& volumes = this->model_object()->volumes;
        m_print->config().filament_diameter.size() > 1 && // BBS
        std::find_if(volumes.begin(), volumes.end(), [](const ModelVolume *v) { return v->is_model_part() && !v->mmu_segmentation_facets.empty(); }) != volumes.end()) {

        // If XY Size compensation is also enabled, notify the user that XY Size compensation
        // would not be used because the object is multi-material painted.
        if (m_config.xy_hole_compensation.value != 0.f || m_config.xy_contour_compensation.value != 0.f) {
            this->active_step_add_warning(
                PrintStateBase::WarningLevel::CRITICAL,
                L("An object's XY size compensation will not be used because it is also color-painted.\nXY Size "
                  "compensation can not be combined with color-painting."));
            BOOST_LOG_TRIVIAL(info) << "xy compensation will not work for object " << this->model_object()->name << " for multi filament.";
        }

        BOOST_LOG_TRIVIAL(debug) << "Slicing volumes - MMU segmentation";
        const long long mm_segment_begin_time = Slic3r::Utils::get_current_milliseconds_time_monotonic();
        apply_mm_segmentation(*this, [print]() { print->throw_if_canceled(); });
        const long long mm_segment_elapsed =
            Slic3r::Utils::get_current_milliseconds_time_monotonic() - mm_segment_begin_time;
        if (mm_segment_ms_out)
            *mm_segment_ms_out += mm_segment_elapsed;
        if (m_print->m_slice_time)
            (*m_print->m_slice_time)[TIME_MM_SEGMENT_2D] += mm_segment_elapsed;
    }

     // Is any ModelVolume fuzzy skin painted?
    if (this->model_object()->is_fuzzy_skin_painted()) {
        // If XY Size compensation is also enabled, notify the user that XY Size compensation
        // would not be used because the object has custom fuzzy skin painted.
        if (m_config.xy_hole_compensation.value != 0.f || m_config.xy_contour_compensation.value != 0.f) {
            this->active_step_add_warning(
                PrintStateBase::WarningLevel::CRITICAL,
                _u8L("An object has enabled XY Size compensation which will not be used because it is also fuzzy skin painted.\nXY Size "
                     "compensation cannot be combined with fuzzy skin painting.") +
                    "\n" + (_u8L("Object name")) + ": " + this->model_object()->name);
        }

        BOOST_LOG_TRIVIAL(debug) << "Slicing volumes - Fuzzy skin segmentation";
        apply_fuzzy_skin_segmentation(*this, [print]() { print->throw_if_canceled(); });
    }

    InterlockingGenerator::generate_interlocking_structure(this);
    m_print->throw_if_canceled();

    InterlockingGenerator::generate_embedding_wall(this);
    m_print->throw_if_canceled();

    // TODO(filament-shrink): refactor — apply per role/region in perimeter & infill stages instead of baking
    //   into posSlice geometry. Current impl scales whole region by the wall_filament's shrink, which:
    //     (1) forces posSlice invalidation whenever wall_filament changes (see PrintObject::invalidate_state_by_config_options),
    //     (2) ignores per-role shrink (infill/solid_infill filament may differ from wall_filament).
    // SuperSlicer: filament shrink. Color-painted objects use the same multi-material gate as
    // XY compensation and skip scaling when any filament used by this object enables shrinkage.
    if (!skip_filament_shrink) {
        for (Layer *layer : m_layers) {
            for (size_t i = 0; i < layer->region_count(); ++i) {
                LayerRegion *region = layer->get_region(i);
                ExPolygons ex_polys = to_expolygons(region->slices.surfaces);
                // extruder() returns 0 for a region that inherits the object's filament, and the
                // per-filament vector can be shorter than the number of filaments the object uses,
                // so both ends have to be range checked (same guard as support_shrinkage_scale()).
                const std::vector<double> &shrink      = print->config().filament_shrink.values;
                const int                  filament_id = region->region().extruder(FlowRole::frPerimeter) - 1;
                double                     scale       = (filament_id < 0 || filament_id >= int(shrink.size())) ?
                                                             1. : shrink[filament_id] * 0.01;
                if (scale != 1 && scale != 0) {
                    scale = 1 / scale;
                    for (ExPolygon &poly : ex_polys)
                        poly.scale(scale);
                }

                region->slices.set(std::move(ex_polys), stInternal);
            }
        }
        // firstLayerObjSlice is captured from raw volume slices before this compensation.
        // Brim grouping matches those slices against compensated lslices; scale them by the
        // same factor or thin-walled rings can stop overlapping (GitHub #12186, feishu issue-7112906345).
        if (const double shrink_scale = this->support_shrinkage_scale(); shrink_scale != 1.) {
            for (VolumeSlices &vs : firstLayerObjSliceByVolume)
                for (ExPolygons &expolys : vs.slices)
                    for (ExPolygon &poly : expolys)
                        poly.scale(shrink_scale);
        }
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
                        float max_growth = std::max(-xy_hole_scaled, xy_contour_scaled);
                        float min_growth = std::min(-xy_hole_scaled, xy_contour_scaled);
                        // for hole, positive xy_hole means increase hole and shrink expolygon
                        ExPolygons merged_poly_for_holes_growing;
                        if (max_growth > 0) {
                            //BBS: merge polygons because region can cut "holes".
                            //Then, cut them to give them again later to their region
                            merged_poly_for_holes_growing = layer->merged(float(SCALED_EPSILON));
                            merged_poly_for_holes_growing = _shrink_contour_holes(std::max(0.f, xy_contour_scaled),
                                                                                  std::min(0.f, xy_hole_scaled),// negetative offset expand expolygons
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
                                                                 std::max(0.f, xy_hole_scaled),//positive xy_hole means shrink
                                                                 trimming);
                            //BBS: trim surfaces
                            for (size_t region_id = 0; region_id < layer->regions().size(); ++region_id) {
                                // BBS: split trimming result by region
                                ExPolygons contour_exp = to_expolygons(std::move(layer->regions()[region_id]->slices.surfaces));

                                layer->regions()[region_id]->slices.set(intersection_ex(contour_exp, to_polygons(trimming)), stInternal);
                            }
                        }
	                }
	                // Merge all regions' slices to get islands, chain them by a shortest path.
                    if (this->config().enable_circle_compensation)
                        layer->apply_auto_circle_compensation();
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

double PrintObject::support_shrinkage_scale() const
{
    const Print *print = this->print();
    if (print == nullptr || this->num_printing_regions() == 0)
        return 1.;
    // Keep supports aligned with object contours whenever object shrinkage is skipped.
    if (should_skip_filament_shrink(*this))
        return 1.;
    const std::vector<double> &shrink = print->config().filament_shrink.values;
    if (shrink.empty())
        return 1.;

    double shrink_percent = 0.;
    bool   found          = false;
    for (size_t i = 0; i < this->num_printing_regions(); ++i) {
        const int filament_id = this->printing_region(i).extruder(FlowRole::frPerimeter) - 1;
        if (filament_id < 0 || filament_id >= int(shrink.size()))
            continue;
        if (!found) {
            shrink_percent = shrink[filament_id];
            found          = true;
        } else if (shrink[filament_id] != shrink_percent) {
            break;
        }
    }
    if (!found || shrink_percent == 0. || shrink_percent == 100.)
        return 1.;
    return 100. / shrink_percent; // == 1 / (shrink_percent * 0.01)
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

        // Align the support-volume slices with the filament_shrink-compensated object contours.
        if (const double shrink_scale = this->support_shrinkage_scale(); shrink_scale != 1.) {
            for (Polygons &polys : slices)
                for (Polygon &poly : polys)
                    poly.scale(shrink_scale);
        }
    }
    return slices;
}

} // namespace Slic3r
