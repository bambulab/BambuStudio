#include "Model.hpp"

namespace Slic3r {

// Extract the current extruder ID based on this ModelVolume's config and the parent ModelObject's config.
int ModelVolume::extruder_id() const
{
    int extruder_id = -1;
    {
        const ConfigOption *opt = this->config.option("extruder");
        if ((opt == nullptr) || (opt->getInt() == 0))
            opt = this->object->config.option("extruder");
        extruder_id = (opt == nullptr) ? 1 : opt->getInt();
    }
    return extruder_id;
}

std::vector<int> ModelVolume::get_extruders() const
{
    if (m_type == ModelVolumeType::INVALID || m_type == ModelVolumeType::NEGATIVE_VOLUME || m_type == ModelVolumeType::SUPPORT_BLOCKER ||
        m_type == ModelVolumeType::SUPPORT_ENFORCER)
        return std::vector<int>();

    if (mmu_segmentation_facets.timestamp() != mmuseg_ts) {
        std::vector<indexed_triangle_set> its_per_type;
        mmuseg_extruders.clear();
        mmuseg_ts = mmu_segmentation_facets.timestamp();
        mmu_segmentation_facets.get_facets(*this, its_per_type);
        for (int idx = 1; idx < its_per_type.size(); idx++) {
            indexed_triangle_set &its = its_per_type[idx];
            if (its.indices.empty())
                continue;

            mmuseg_extruders.push_back(idx);
        }
        if (its_per_type.size() > 0 && its_per_type[0].indices.size() == 0)
            m_mmuseg_extruders_has_0_extruder = false;
        else
            m_mmuseg_extruders_has_0_extruder = true;
    }

    std::vector<int> volume_extruders   = mmuseg_extruders;
    int              volume_extruder_id = this->extruder_id();
    if (m_mmuseg_extruders_has_0_extruder && volume_extruder_id > 0)
        volume_extruders.push_back(volume_extruder_id);

    if (this->config.option("wall_filament") && this->config.option("wall_filament")->getInt() > 0)
        volume_extruders.push_back(this->config.option("wall_filament")->getInt());
    else if (this->config.option("wall_filament") == nullptr && this->get_object()->config.option("wall_filament") &&
             this->get_object()->config.option("wall_filament")->getInt() > 0)
        volume_extruders.push_back(this->get_object()->config.option("wall_filament")->getInt());

    if (this->config.option("solid_infill_filament") && this->config.option("solid_infill_filament")->getInt() > 0)
        volume_extruders.push_back(this->config.option("solid_infill_filament")->getInt());
    else if (this->config.option("solid_infill_filament") == nullptr && this->get_object()->config.option("solid_infill_filament") &&
             this->get_object()->config.option("solid_infill_filament")->getInt() > 0)
        volume_extruders.push_back(this->get_object()->config.option("solid_infill_filament")->getInt());

    if (this->config.option("sparse_infill_filament") && this->config.option("sparse_infill_filament")->getInt() > 0)
        volume_extruders.push_back(this->config.option("sparse_infill_filament")->getInt());
    else if (this->config.option("sparse_infill_filament") == nullptr && this->get_object()->config.option("sparse_infill_filament") &&
             this->get_object()->config.option("sparse_infill_filament")->getInt() > 0)
        volume_extruders.push_back(this->get_object()->config.option("sparse_infill_filament")->getInt());

    return volume_extruders;
}

} // namespace Slic3r
