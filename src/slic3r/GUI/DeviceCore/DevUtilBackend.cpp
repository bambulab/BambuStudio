#include "DevUtilBackend.h"

#include "slic3r/GUI/BackgroundSlicingProcess.hpp"

#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GUI_App.hpp"

#include <wx/string.h>

namespace Slic3r
{


Slic3r::MultiNozzleUtils::NozzleInfo DevUtilBackend::GetNozzleInfo(const DevNozzle& dev_nozzle)
{
    MultiNozzleUtils::NozzleInfo info;
    info.diameter = dev_nozzle.GetNozzleDiameterStr().ToStdString();
    info.volume_type = (dev_nozzle.GetNozzleFlowType() == NozzleFlowType::H_FLOW ? NozzleVolumeType::nvtHighFlow : NozzleVolumeType::nvtStandard);
    info.extruder_id = dev_nozzle.GetLogicExtruderId();

    return info;
}

std::optional<Slic3r::MultiNozzleUtils::MultiNozzleGroupResult>
DevUtilBackend::GetNozzleGroupResult(Slic3r::GUI::Plater* plater)
{
    if (plater && plater->background_process().get_current_gcode_result()) {
        return plater->background_process().get_current_gcode_result()->nozzle_group_result;
    }

    return std::nullopt;
}

std::unordered_map<NozzleDef, int>
DevUtilBackend::CollectNozzleInfo(MultiNozzleUtils::MultiNozzleGroupResult* nozzle_group_res, int logic_ext_id)
{
    std::unordered_map<NozzleDef, int> need_nozzle_map;
    if (!nozzle_group_res) {
        return need_nozzle_map;
    }

    const std::vector<Slic3r::MultiNozzleUtils::NozzleInfo>& nozzle_vec = nozzle_group_res->get_nozzle_vec(logic_ext_id);
    for (auto slicing_nozzle : nozzle_vec) {
        try {
            NozzleDef data;
            data.nozzle_diameter = boost::lexical_cast<float>(slicing_nozzle.diameter);
            data.nozzle_flow_type = (slicing_nozzle.volume_type == NozzleVolumeType::nvtHighFlow ? NozzleFlowType::H_FLOW : NozzleFlowType::S_FLOW);
            need_nozzle_map[data]++;
        } catch (const std::exception& e) {
            assert(0);
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "exception: " << e.what();
        }
    }

    return need_nozzle_map;
}

};// namespace Slic3r