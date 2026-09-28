/**
* @file  DevSliceContext.hpp
* @brief Deep-copied slicing snapshot for nozzle mapping (independent of live Plater/gcode).
*/

#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include "DevDefs.h"
#include "libslic3r/MultiNozzleUtils.hpp"

namespace Slic3r
{

class DynamicPrintConfig;
struct PlateData;

namespace GUI
{
class Plater;
struct QueuePrintContext;
}

struct DevSliceContext
{
    GUI::PrintFromType print_type = GUI::PrintFromType::FROM_NORMAL;

    std::unordered_map<int, std::vector<MultiNozzleUtils::NozzleInfo>> nozzles_by_filament;
    std::vector<MultiNozzleUtils::NozzleInfo> used_logic_nozzles;
    std::vector<unsigned int> used_filaments;

    std::vector<unsigned int> filament_change_sequence;
    std::vector<unsigned int> nozzle_change_sequence;
    std::vector<std::vector<std::vector<float>>> flush_matrix;
    bool support_dynamic_nozzle_map = false;

    void Clear();
    bool Valid() const;

public:
    DevSliceContext() = default;
    static std::shared_ptr<DevSliceContext> CreateFromNormal(GUI::PrintFromType print_type,
                                                            Slic3r::GUI::Plater* plater);
    static std::shared_ptr<DevSliceContext> CreateFromSDCard(GUI::PrintFromType print_type,
                                                            const PlateData& plate_data,
                                                            const DynamicPrintConfig& print_config);
    static std::shared_ptr<DevSliceContext> CreateFromQueue(GUI::PrintFromType print_type,
                                                            const Slic3r::GUI::QueuePrintContext& queue_ctx);
};

}
