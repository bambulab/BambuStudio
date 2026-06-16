#pragma once

#include "../Slicing.hpp"
#include "SupportLayer.hpp"

#include <deque>

namespace Slic3r {

inline double layer_z(const SlicingParameters& slicing_params, const size_t layer_idx)
{
    return slicing_params.object_print_z_min + slicing_params.first_object_layer_height + layer_idx * slicing_params.layer_height;
}

inline SupportGeneratorLayer& layer_initialize(
    SupportGeneratorLayer& layer_new,
    const SupporLayerType    layer_type,
    const SlicingParameters& slicing_params,
    const size_t             layer_idx)
{
    layer_new.layer_type = layer_type;
    layer_new.print_z = layer_z(slicing_params, layer_idx);
    layer_new.height = layer_idx == 0 ? slicing_params.first_object_layer_height : slicing_params.layer_height;
    layer_new.bottom_z = layer_idx == 0 ? slicing_params.object_print_z_min : layer_new.print_z - layer_new.height;
    return layer_new;
}

// Using the std::deque as an allocator.
inline SupportGeneratorLayer& layer_allocate(
    std::deque<SupportGeneratorLayer>& layer_storage,
    SupporLayerType                    layer_type,
    const SlicingParameters&           slicing_params,
    size_t                             layer_idx)
{
    //FIXME take raft into account.
    layer_storage.push_back(SupportGeneratorLayer());
    return layer_initialize(layer_storage.back(), layer_type, slicing_params, layer_idx);
}

} // namespace Slic3r
