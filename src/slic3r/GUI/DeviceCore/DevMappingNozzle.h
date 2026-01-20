/**
* @file  DevMappingNozzle.h
* @brief Support nozzle mapping for hotend rack
*/

#pragma once
#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>

#include <wx/string.h>

namespace Slic3r
{

namespace GUI
{
class Plater;
}

class MachineObject;
class DevNozzleMappingCtrl
{
    friend class MachineObject;
public:
    enum class NozzleMappingVersion : int {
        Version_V0 = 0, // V0 means support logical fila_id -> physical nozzle_id mapping
        Version_V1 = 1, // V1 means support logical nozzle_id -> physical nozzle_id mapping
    };

public:
    DevNozzleMappingCtrl(MachineObject* obj) : m_obj(obj) {};

public:
    void Clear();

    bool HasResult() const { return !m_result.empty(); }
    std::string GetResultStr() const { return m_result; }

    // mqtt error info
    std::string GetMqttReason() const { return m_mqtt_reason; }

    // command error info
    int GetErrno() const { return m_errno; }
    std::string GetDetailMsg() const { return m_detail_msg; }

    // nozzle mapping
    std::unordered_map<int, int> GetNozzleMappingMap() const { return m_nozzle_mapping; }
    nlohmann::json GetNozzleMappingJson() const { return m_nozzle_mapping_json; }
    void SetManualNozzleMappingByFila(int fila_id, int nozzle_pos_id);

    std::vector<int> GetMappedNozzlePosVecByFilaId(int fila_id) const;// return empy if not mapped
    wxString GetMappedNozzlePosStrByFilaId(int fila_id, const wxString& default_str = "?") const;

    // flush weight
    float  GetFlushWeightBase() const { return m_flush_weight_base; }
    float  GetFlushWeightCurrent() const { return m_flush_weight_current; }

public:
    void ParseAutoNozzleMapping(const nlohmann::json& print_jj);
    int CtrlGetAutoNozzleMapping(Slic3r::GUI::Plater* plater, const std::vector<FilamentInfo>& ams_mapping, int flow_cali_opt, int pa_value);

private:
    float  GetFlushWeight(Slic3r::MachineObject* obj) const;

private:
    MachineObject* m_obj;
    Slic3r::GUI::Plater* m_plater = nullptr;

    std::string m_sequence_id;

    std::string m_result;
    std::string m_mqtt_reason;
    std::string m_type; // auto or manual

    int         m_errno;
    std::string m_detail_msg;
    nlohmann::json m_detail_json;

    nlohmann::json m_nozzle_mapping_json;
    std::unordered_map<int, int> m_nozzle_mapping;  // v0: fila_id -> physical_nozzle_id, v1: logical_nozzle_id -> physical_nozzle_id

    float m_flush_weight_base = -1;// the base weight for flush
    float m_flush_weight_current = -1;// the weight current

    const NozzleMappingVersion m_req_version = NozzleMappingVersion::Version_V1;
    NozzleMappingVersion m_ack_version = NozzleMappingVersion::Version_V0;
};

}
