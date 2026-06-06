#include "Model.hpp"

#include <algorithm>
#include <cassert>

namespace Slic3r {

bool FacetsAnnotation::equals(const FacetsAnnotation &other) const
{
    const std::pair<std::vector<std::pair<int, int>>, std::vector<bool>> &data = other.get_data();
    return m_data == data;
}

bool model_object_list_equal(const Model &model_old, const Model &model_new)
{
    if (model_old.objects.size() != model_new.objects.size())
        return false;
    for (size_t i = 0; i < model_old.objects.size(); ++i)
        if (model_old.objects[i]->id() != model_new.objects[i]->id())
            return false;
    return true;
}

bool model_object_list_extended(const Model &model_old, const Model &model_new)
{
    if (model_old.objects.size() >= model_new.objects.size())
        return false;
    for (size_t i = 0; i < model_old.objects.size(); ++i)
        if (model_old.objects[i]->id() != model_new.objects[i]->id())
            return false;
    return true;
}

template <typename TypeFilterFn>
bool model_volume_list_changed_impl(const ModelObject &model_object_old, const ModelObject &model_object_new, TypeFilterFn type_filter)
{
    size_t i_old, i_new;
    for (i_old = 0, i_new = 0; i_old < model_object_old.volumes.size() && i_new < model_object_new.volumes.size();) {
        const ModelVolume &mv_old = *model_object_old.volumes[i_old];
        const ModelVolume &mv_new = *model_object_new.volumes[i_new];
        if (! type_filter(mv_old.type())) {
            ++i_old;
            continue;
        }
        if (! type_filter(mv_new.type())) {
            ++i_new;
            continue;
        }
        if (mv_old.type() != mv_new.type() || mv_old.id() != mv_new.id())
            return true;
        if (! mv_old.get_matrix().isApprox(mv_new.get_matrix()))
            return true;
        ++i_old;
        ++i_new;
    }
    for (; i_old < model_object_old.volumes.size(); ++i_old) {
        const ModelVolume &mv_old = *model_object_old.volumes[i_old];
        if (type_filter(mv_old.type()))
            return true;
    }
    for (; i_new < model_object_new.volumes.size(); ++i_new) {
        const ModelVolume &mv_new = *model_object_new.volumes[i_new];
        if (type_filter(mv_new.type()))
            return true;
    }
    return false;
}

bool model_volume_list_changed(const ModelObject &model_object_old, const ModelObject &model_object_new, const ModelVolumeType type)
{
    return model_volume_list_changed_impl(model_object_old, model_object_new, [type](const ModelVolumeType t) { return t == type; });
}

bool model_volume_list_changed(const ModelObject &model_object_old, const ModelObject &model_object_new, const std::initializer_list<ModelVolumeType> &types)
{
    return model_volume_list_changed_impl(model_object_old, model_object_new, [&types](const ModelVolumeType t) {
        return std::find(types.begin(), types.end(), t) != types.end();
    });
}

template <typename TypeFilterFn, typename CompareFn>
bool model_property_changed(const ModelObject &model_object_old, const ModelObject &model_object_new, TypeFilterFn type_filter, CompareFn compare)
{
    assert(! model_volume_list_changed_impl(model_object_old, model_object_new, type_filter));
    size_t i_old, i_new;
    for (i_old = 0, i_new = 0; i_old < model_object_old.volumes.size() && i_new < model_object_new.volumes.size();) {
        const ModelVolume &mv_old = *model_object_old.volumes[i_old];
        const ModelVolume &mv_new = *model_object_new.volumes[i_new];
        if (! type_filter(mv_old.type())) {
            ++i_old;
            continue;
        }
        if (! type_filter(mv_new.type())) {
            ++i_new;
            continue;
        }
        assert(mv_old.type() == mv_new.type() && mv_old.id() == mv_new.id());
        if (! compare(mv_old, mv_new))
            return true;
        ++i_old;
        ++i_new;
    }
    return false;
}

bool model_custom_supports_data_changed(const ModelObject &mo, const ModelObject &mo_new)
{
    return model_property_changed(
        mo, mo_new, [](const ModelVolumeType t) { return t == ModelVolumeType::MODEL_PART; },
        [](const ModelVolume &mv_old, const ModelVolume &mv_new) { return mv_old.supported_facets.timestamp_matches(mv_new.supported_facets); });
}

bool model_custom_fuzzy_skin_data_changed(const ModelObject &mo, const ModelObject &mo_new)
{
    return model_property_changed(
        mo, mo_new, [](const ModelVolumeType t) { return t == ModelVolumeType::MODEL_PART; },
        [](const ModelVolume &mv_old, const ModelVolume &mv_new) { return mv_old.fuzzy_skin_facets.timestamp_matches(mv_new.fuzzy_skin_facets); });
}

bool model_custom_seam_data_changed(const ModelObject &mo, const ModelObject &mo_new)
{
    return model_property_changed(
        mo, mo_new, [](const ModelVolumeType t) { return t == ModelVolumeType::MODEL_PART; },
        [](const ModelVolume &mv_old, const ModelVolume &mv_new) { return mv_old.seam_facets.timestamp_matches(mv_new.seam_facets); });
}

bool model_mmu_segmentation_data_changed(const ModelObject &mo, const ModelObject &mo_new)
{
    return model_property_changed(
        mo, mo_new, [](const ModelVolumeType t) { return t == ModelVolumeType::MODEL_PART; },
        [](const ModelVolume &mv_old, const ModelVolume &mv_new) { return mv_old.mmu_segmentation_facets.timestamp_matches(mv_new.mmu_segmentation_facets); });
}

bool model_brim_points_data_changed(const ModelObject &mo, const ModelObject &mo_new)
{
    if (mo.brim_points.size() != mo_new.brim_points.size())
        return true;
    for (size_t i = 0; i < mo.brim_points.size(); ++i) {
        if (mo.brim_points[i] != mo_new.brim_points[i])
            return true;
    }
    return false;
}

} // namespace Slic3r
