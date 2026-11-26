/**
* @file  DevSliceContext.cpp
* @brief Deep-copied slicing snapshot for nozzle mapping.
*/

#include "DevSliceContext.hpp"

#include "DevUtilBackend.h"
#include "DevExtruderSystem.h"
#include "DevNozzleSystem.h"
#include "DevUtil.h"

#include "libslic3r/LocalesUtils.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"

#include "slic3r/GUI/BackgroundSlicingProcess.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/QueuePrintContext.hpp"
#include "libslic3r/Log.hpp"

#include <set>

namespace Slic3r {

namespace {

std::vector<std::vector<std::vector<float>>> build_flush_matrix_from_config(const DynamicPrintConfig& config)
{
    std::vector<std::vector<std::vector<float>>> matrix;
    const auto* nozzle_diameter = config.option<ConfigOptionFloatsNullable>("nozzle_diameter");
    const auto* flush_volumes = config.option<ConfigOptionFloats>("flush_volumes_matrix");
    const auto* filament_type = config.option<ConfigOptionStrings>("filament_type");
    if (!nozzle_diameter || !flush_volumes || !filament_type ||
        nozzle_diameter->values.empty() || filament_type->values.empty()) {
        return matrix;
    }

    const size_t extruder_nums = nozzle_diameter->values.size();
    const size_t filament_nums = filament_type->values.size();
    for (size_t extruder_id = 0; extruder_id < extruder_nums; ++extruder_id) {
        std::vector<float> flush_matrix(cast<float>(get_flush_volumes_matrix(flush_volumes->values, extruder_id, extruder_nums)));
        std::vector<std::vector<float>> wipe_volumes;
        for (size_t i = 0; i < filament_nums; ++i) {
            if ((i + 1) * filament_nums > flush_matrix.size())
                break;
            wipe_volumes.emplace_back(flush_matrix.begin() + i * filament_nums,
                                      flush_matrix.begin() + (i + 1) * filament_nums);
        }
        matrix.emplace_back(std::move(wipe_volumes));
    }

    const auto* flush_multiplier = config.option<ConfigOptionFloats>("flush_multiplier");
    if (flush_multiplier && !flush_multiplier->values.empty()) {
        std::vector<double> multipliers = flush_multiplier->values;
        multipliers.resize(extruder_nums, 1.0);
        for (size_t extruder_id = 0; extruder_id < matrix.size(); ++extruder_id) {
            for (auto& row : matrix[extruder_id]) {
                for (auto& v : row)
                    v *= static_cast<float>(multipliers[extruder_id]);
            }
        }
    }

    return matrix;
}

} // namespace

void DevSliceContext::Clear()
{
    print_type = GUI::PrintFromType::FROM_NORMAL;
    nozzles_by_filament.clear();
    used_logic_nozzles.clear();
    used_filaments.clear();
    filament_change_sequence.clear();
    nozzle_change_sequence.clear();
    flush_matrix.clear();
    support_dynamic_nozzle_map = false;
    multi_nozzle_diameter_mode = 0;
}

bool DevSliceContext::Valid() const
{
    return !used_logic_nozzles.empty() || !nozzles_by_filament.empty();
}

bool DevSliceContext::ExceedsFlushVolumeLimit(float limit) const
{
    std::vector<int> filament_seq;
    filament_seq.reserve(filament_change_sequence.size());
    for (unsigned int fid : filament_change_sequence)
        filament_seq.push_back(static_cast<int>(fid));

    std::vector<int> nozzle_seq;
    nozzle_seq.reserve(nozzle_change_sequence.size());
    for (unsigned int nid : nozzle_change_sequence)
        nozzle_seq.push_back(static_cast<int>(nid));

    return MultiNozzleUtils::flush_volume_exceeds_limit(
        filament_seq, nozzle_seq, used_logic_nozzles, flush_matrix, limit);
}

bool DevSliceContext::HasMultipleLogicNozzleDiameters() const
{
    return multi_nozzle_diameter_mode != 0;
}

// for example:
// {0, {0.4, S_FLOW}}, {1, {0.8, H_FLOW}}
// {0, {0.4, S_FLOW}}, {0, {0.4, H_FLOW}}
std::unordered_multimap<int, NozzleDef> DevSliceContext::GetSliceNozzles(int total_ext_count) const
{
    std::unordered_multimap<int, NozzleDef> slice_nozzles;
    for (const auto& nozzle : used_logic_nozzles) {
        NozzleDef nozzle_data;
        nozzle_data.nozzle_diameter = s_get_diameter(nozzle.diameter);
        nozzle_data.nozzle_flow_type = DevNozzle::ToNozzleFlowType(nozzle.volume_type);
        const int physical_extruder = DevExtder::to_physical_extruder_id(total_ext_count, nozzle.extruder_id);
        if (physical_extruder != INVALID_EXTRUDER_ID) {
            slice_nozzles.insert({ physical_extruder, nozzle_data });
        }
    }

    return slice_nozzles;
}

std::shared_ptr<DevSliceContext> DevSliceContext::CreateFromNormal(GUI::PrintFromType print_type, Slic3r::GUI::Plater* plater)
{
    auto ctx = std::make_shared<DevSliceContext>();
    ctx->print_type = print_type;
    if (print_type != GUI::PrintFromType::FROM_NORMAL) {
        return ctx;
    };

    if (!plater) {
        BOOST_LOG_TRIVIAL(error) << "plater is nullptr";
        return ctx;
    }

    GCodeProcessorResult* gcode_result = plater->background_process().get_current_gcode_result();
    if (!gcode_result) {
        BOOST_LOG_TRIVIAL(error) << "gcode_result is nullptr";
        return ctx;
    }

    auto nozzle_group_res = DevUtilBackend::GetNozzleGroupResult(plater);
    if (!nozzle_group_res) {
        BOOST_LOG_TRIVIAL(error) << "nozzle_group_result is nullptr";
        return ctx;
    }

    for (unsigned int fid : nozzle_group_res->get_used_filaments()) {
        ctx->nozzles_by_filament[static_cast<int>(fid)] =
            nozzle_group_res->get_nozzles_for_filament(static_cast<int>(fid));
    }
    ctx->used_filaments = nozzle_group_res->get_used_filaments();
    ctx->used_logic_nozzles = nozzle_group_res->get_used_nozzles_in_extruder();
    ctx->support_dynamic_nozzle_map = nozzle_group_res->is_support_dynamic_nozzle_map();
    if (Slic3r::GUI::wxGetApp().preset_bundle) {
        if (const auto* multi_diameter_mode = Slic3r::GUI::wxGetApp().preset_bundle->project_config.option<ConfigOptionBool>("multi_nozzle_diameter_mode")) {
            ctx->multi_nozzle_diameter_mode = multi_diameter_mode->value ? 1 : 0;
        }
    }
    ctx->filament_change_sequence = gcode_result->filament_change_sequence;
    ctx->nozzle_change_sequence = gcode_result->nozzle_change_sequence;

    if (Slic3r::GUI::wxGetApp().preset_bundle) {
        ctx->flush_matrix = Slic3r::GUI::wxGetApp().preset_bundle->get_full_flush_matrix();
    } else {
        BOOST_LOG_TRIVIAL(error) << "preset_bundle is nullptr";
    }

    return ctx;
}

std::shared_ptr<DevSliceContext> DevSliceContext::CreateFromSDCard(GUI::PrintFromType print_type,
                                                                   const PlateData& plate_data,
                                                                   const DynamicPrintConfig& print_config)
{
    auto ctx = std::make_shared<DevSliceContext>();
    ctx->print_type = print_type;
    ctx->used_logic_nozzles = plate_data.nozzles_info;
    ctx->filament_change_sequence = plate_data.filament_change_sequence;
    ctx->nozzle_change_sequence = plate_data.nozzle_change_sequence;

    if (const auto* dynamic_map = print_config.option<ConfigOptionBool>("enable_filament_dynamic_map")) {
        ctx->support_dynamic_nozzle_map = dynamic_map->value;
    } else if (const auto* plate_dynamic_map = plate_data.config.option<ConfigOptionBool>("enable_filament_dynamic_map")) {
        ctx->support_dynamic_nozzle_map = plate_dynamic_map->value;
    }
    if (const auto* multi_diameter_mode = print_config.option<ConfigOptionBool>("multi_nozzle_diameter_mode")) {
        ctx->multi_nozzle_diameter_mode = multi_diameter_mode->value ? 1 : 0;
    } else if (const auto* plate_multi_diameter_mode = plate_data.config.option<ConfigOptionBool>("multi_nozzle_diameter_mode")) {
        ctx->multi_nozzle_diameter_mode = plate_multi_diameter_mode->value ? 1 : 0;
    }

    for (const auto& filament : plate_data.slice_filaments_info) {
        std::vector<MultiNozzleUtils::NozzleInfo> nozzles;
        for (int group_id : filament.group_id) {
            for (const auto& nozzle : plate_data.nozzles_info) {
                if (nozzle.group_id == group_id)
                    nozzles.push_back(nozzle);
            }
        }
        ctx->nozzles_by_filament[filament.id] = std::move(nozzles);
        ctx->used_filaments.push_back(static_cast<unsigned int>(filament.id));
    }

    ctx->flush_matrix = build_flush_matrix_from_config(print_config);
    if (ctx->flush_matrix.empty()) {
        ctx->flush_matrix = build_flush_matrix_from_config(plate_data.config);
    }
    if (ctx->flush_matrix.empty()) {
        BOOST_LOG_TRIVIAL(error) << "failed to build flush_matrix from plate/print config";
    }

    return ctx;
}

std::shared_ptr<DevSliceContext> DevSliceContext::CreateFromQueue(GUI::PrintFromType print_type,
                                                                  const Slic3r::GUI::QueuePrintContext& queue_ctx)
{
    auto ctx = std::make_shared<DevSliceContext>();
    ctx->print_type = print_type;
    if (print_type != GUI::PrintFromType::FROM_PRINT_QUEUE) {
        return ctx;
    };

    if (!queue_ctx.plate_data) {
        BOOST_LOG_TRIVIAL(error) << "plate_data is nullptr";
        return ctx;
    }

    return CreateFromSDCard(print_type, *queue_ctx.plate_data, queue_ctx.config);
}

}
