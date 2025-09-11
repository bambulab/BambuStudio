#include "DevMapping.h"

#include "DevNozzleRack.h"
#include "DevNozzleSystem.h"
#include "DevUtil.h"

#include "libslic3r/MultiNozzleUtils.hpp"
#include "libslic3r/Print.hpp"

#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/BackgroundSlicingProcess.hpp"

#include <nlohmann/json.hpp>
using namespace nlohmann;

namespace Slic3r {

static int s_get_physical_extruder_id(int total_ext_count, int logical_extruder_id)
{
    if (total_ext_count == 2) {
        return logical_extruder_id == 1 ? 1 : 0;
    } else if (total_ext_count == 1) {
        return 0;
    }

    return logical_extruder_id - 1;
}

static std::string  s_get_diameter_str(float diameter)
{
    return wxString::Format("%.2f", diameter).ToUTF8().data();
}

static std::string s_get_diameter_str(const std::string& diameter)
{
    try {
        float dia = std::stof(diameter);
        return s_get_diameter_str(dia);
    } catch (...) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << " failed to convert: " << diameter;
        return diameter;
    }
}


// get auto nozzle mapping through AP
// warnings: 
// (1) the fila_id here is 1 based index
// (2) the AP wants the diameter string with 2 decimal places
int MachineObject::ctrl_get_auto_nozzle_mapping(Slic3r::GUI::Plater* plater, const std::vector<FilamentInfo>& ams_mapping, int flow_cali_opt)
{
    m_auto_nozzle_mapping.Clear();
    if (!plater) {
        return - 1;
    }

    GCodeProcessorResult* gcode_result = plater->background_process().get_current_gcode_result();
    if (!gcode_result) {
        return -1;
    }

    const auto& result = plater->get_partplate_list().get_current_fff_print().get_nozzle_group_result();
    if (!result) {
        assert(false && "ff_print->get_nozzle_group_result() should not be nullptr");
        return -1;
    }

    if (ams_mapping.empty()) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": the ams mapping is empty";
        return -1;// the ams mapping is empty
    }

    json command_jj;
    command_jj["print"]["command"] = "get_auto_nozzle_mapping";
    m_auto_nozzle_mapping.m_sequence_id = std::to_string(m_sequence_id++);
    command_jj["print"]["sequence_id"] = m_auto_nozzle_mapping.m_sequence_id;
    command_jj["print"]["calibration"] = flow_cali_opt;

    // filament seq
    json filament_seq_jj;
    std::unordered_set<int> filaid_set;
    for (int idx = 0; idx < gcode_result->filament_change_sequence.size(); idx++) {
        int fila_id = gcode_result->filament_change_sequence[idx];
        if (filaid_set.count(fila_id) == 0) {
            filaid_set.insert(fila_id);
            filament_seq_jj[fila_id + 1] = idx;
        }
    }
    command_jj["print"]["filament_seq"] = filament_seq_jj;

    // ams mapping
    std::vector<int> ams_mapping_vec(33, 0xFFFF);/* AP ask to fill them*/
    for (int fila_id = 0; fila_id < ams_mapping.size(); fila_id++) {
        ams_mapping_vec[fila_id + 1] = ams_mapping[fila_id].tray_id;
    }
    command_jj["print"]["ams_mapping"] = ams_mapping_vec;

    // filament info
    json filament_info_jj;
    for (int fila_id = 0; fila_id < ams_mapping.size(); fila_id++) {
        const auto& fila = ams_mapping[fila_id];
        const auto& nozzle_info = result->get_nozzle_for_filament(fila.id);
        if (!nozzle_info) {
            assert(false && "nozzle_info should not be nullptr");
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "nozzle_info should not be nullptr";
            continue;
        }

        json fila_item_jj;
        fila_item_jj["id"] = (fila.id + 1);
        fila_item_jj["direction"] = (nozzle_info->extruder_id == LOGIC_L_EXTRUDER_ID ? 1 : 2);
        fila_item_jj["group"] = nozzle_info->group_id;
        fila_item_jj["nozzle_d"] = s_get_diameter_str(nozzle_info->diameter);
        fila_item_jj["nozzle_v"] = (nozzle_info->volume_type == NozzleVolumeType::nvtHighFlow) ? "High Flow" : "Standard";
        fila_item_jj["cate"] = fila.filament_id;
        fila_item_jj["color"] = fila.color;
        filament_info_jj.push_back(fila_item_jj);
    }

    command_jj["print"]["fila_info"] = filament_info_jj;

    // nozzle info
    json nozzle_info_jj;
    const auto& extruder_nozzles = m_nozzle_system->GetNozzles();
    for (const auto& nozzle : extruder_nozzles) {
        if (nozzle.second.IsNormal()) {
            json nozzle_item_jj;
            nozzle_item_jj["pos"] = nozzle.second.GetNozzleId();
            if (m_nozzle_system->GetReplaceNozzleTar().has_value() && nozzle_item_jj["pos"] == MAIN_EXTRUDER_ID) {
                nozzle_item_jj["pos"] = m_nozzle_system->GetReplaceNozzleTar().value();// special case of tar_id. see protocol definition
            }

            nozzle_item_jj["nozzle_d"] = s_get_diameter_str(nozzle.second.GetNozzleDiameter());
            nozzle_item_jj["nozzle_v"] = (nozzle.second.GetNozzleFlowType() == NozzleFlowType::H_FLOW) ? "High Flow" : "Standard";
            nozzle_item_jj["wear"] = nozzle.second.GetNozzleWear();
            nozzle_item_jj["cate"] = nozzle.second.GetFilamentId();
            nozzle_item_jj["color"] = nozzle.second.GetFilamentColor();
            nozzle_info_jj.push_back(nozzle_item_jj);
        }
    }

    const auto& rack_nozzles = m_nozzle_system->GetNozzleRack()->GetRackNozzles();
    for (const auto& nozzle : rack_nozzles) {
        if (nozzle.second.IsNormal()) {
            json nozzle_item_jj;
            nozzle_item_jj["pos"] = (nozzle.second.GetNozzleId() + 0x10);
            if (m_nozzle_system->GetReplaceNozzleTar().has_value() && nozzle_item_jj["pos"] == MAIN_EXTRUDER_ID) {
                nozzle_item_jj["pos"] = m_nozzle_system->GetReplaceNozzleTar().value();// special case of tar_id. see protocol definition
            }

            nozzle_item_jj["nozzle_d"] = s_get_diameter_str(nozzle.second.GetNozzleDiameter());
            nozzle_item_jj["nozzle_v"] = (nozzle.second.GetNozzleFlowType() == NozzleFlowType::H_FLOW) ? "High Flow" : "Standard";
            nozzle_item_jj["wear"] = nozzle.second.GetNozzleWear();
            nozzle_item_jj["cate"] = nozzle.second.GetFilamentId();
            nozzle_item_jj["color"] = nozzle.second.GetFilamentColor();
            nozzle_info_jj.push_back(nozzle_item_jj);
        }
    }

    command_jj["print"]["nozzle_info"] = nozzle_info_jj;

    const auto& val = command_jj.dump();
    return publish_json(command_jj);
}


void s_auto_nozzle_mapping(const nlohmann::json &print_jj, DevNozzleMappingResult& result)
{
    result.Clear();
    DevJsonValParser::ParseVal(print_jj, "result", result.m_result);
    DevJsonValParser::ParseVal(print_jj, "reason", result.m_mqtt_reason);
    DevJsonValParser::ParseVal(print_jj, "errno", result.m_errno);
    DevJsonValParser::ParseVal(print_jj, "detail", result.m_detail_json);
    DevJsonValParser::ParseVal(print_jj, "type", result.m_type);

    if (print_jj.contains("mapping")) {
        result.m_nozzle_mapping_json = print_jj["mapping"];
        const auto& mapping = print_jj["mapping"].get<std::vector<int>>();
        for (int fila_id = 0; fila_id < mapping.size(); ++fila_id) {
            result.m_nozzle_mapping[fila_id] =  mapping[fila_id];
        }
    }

    result.m_detail_msg = result.m_detail_json.dump(1);
    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": get_auto_nozzle_mapping: " << result.m_result;
};

void MachineObject::parse_auto_nozzle_mapping(const json& print_jj)
{
    if (print_jj.contains("command") && print_jj["command"].get<string>() == "get_auto_nozzle_mapping") {
        if (print_jj.contains("sequence_id") && print_jj["sequence_id"] == m_auto_nozzle_mapping.m_sequence_id) {
            s_auto_nozzle_mapping(print_jj, m_auto_nozzle_mapping);
        }
    }
}

void DevNozzleMappingResult::Clear()
{
    m_sequence_id.clear();
    m_result.clear();
    m_mqtt_reason.clear();
    m_type.clear();
    m_errno = 0;
    m_detail_msg.clear();
    m_detail_json.clear();
    m_nozzle_mapping.clear();
    m_nozzle_mapping_json.clear();
}

} // namespace Slic3r