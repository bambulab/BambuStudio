#pragma once
#include <nlohmann/json.hpp>
#include "slic3r/Utils/json_diff.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "DevDefs.h"
#include "libslic3r/Calib.hpp"


namespace Slic3r {

class MachineObject;


enum class CalibStatus{
    IDLE = 0,
    REQUEST,
    WAITING,
    FINISHED,
};

/* Filtering view over the merged PA history cache.
 * Unset condition = no filter. NONE_DIAMETER_TYPE = not filtering. */
class PaHistoryFilter
{
public:
    PaHistoryFilter() = default;
    explicit PaHistoryFilter(const std::vector<PACalibResult>& history);

    void reset(const std::vector<PACalibResult>& history);

    /* filter conditions, AND combined */
    PaHistoryFilter& set_filament_id(const std::string& filament_id);
    PaHistoryFilter& set_nozzle_volume_type(NozzleVolumeType volume_type);
    /* nullopt = not filtering */
    PaHistoryFilter& set_nozzle_volume_type(std::optional<NozzleVolumeType> volume_type);
    PaHistoryFilter& set_nozzle_diameter(NozzleDiameterType diameter);
    /* nullopt / NONE = not filtering */
    PaHistoryFilter& set_nozzle_diameter(std::optional<NozzleDiameterType> diameter);
    /* nullopt / negative = not filtering */
    PaHistoryFilter& set_extruder_id(std::optional<int> extruder_id);
    PaHistoryFilter& set_pa_profile_name(const std::string& pa_profile_name);
    PaHistoryFilter& set_cali_idx(int cali_idx);
    /* extra AND predicate; empty = no extra filter */
    PaHistoryFilter& set_custom_filter(std::function<bool(const PACalibResult&)> predicate);
    /* nullopt / negative = not filtering */
    PaHistoryFilter& set_nozzle_pos_id(std::optional<int> nozzle_pos_id);

    void clear_filters();

    size_t count() const;
    bool empty() const;
    std::vector<PACalibResult> get() const;
    const PACalibResult* find_by_cali_idx(int idx) const;

private:
    bool matches(const PACalibResult& result) const;

    std::vector<PACalibResult>          m_data;
    std::optional<int>                  m_extruder_id;
    std::optional<NozzleVolumeType>     m_nozzle_volume_type;
    std::optional<NozzleDiameterType>   m_nozzle_diameter;
    std::optional<int>                  m_cali_idx;
    std::optional<int>                  m_nozzle_pos_id;
    std::string                         m_filament_id;  // empty = not filtering
    std::string                         m_pa_profile_name; // empty = not filtering
    std::function<bool(const PACalibResult&)> m_custom_filter;
};

enum class ManualPaCaliMethod {
    PA_LINE = 0,
    PA_PATTERN,
};

class DevCalib
{
protected:
    bool                    m_support_new_auto_cali{false};
    int                     m_calib_version {-1};
    std::optional<int>      m_last_calib_version;

public:
    DevCalib(MachineObject *obj) : m_owner(obj){};
    MachineObject* GetOwner() const {return m_owner; };

    void RequestPAResult();
    CalibStatus GetPAResultStatus() const {return m_pa_results_status;}
    bool IsPAResultReady() const { return m_pa_results_status == CalibStatus::FINISHED;}
    void ResetPAResult();

    /* calib history */
    int RequestPAHistory(const PACalibExtruderInfo &calib_info);
    CalibStatus GetPAHistoryStatus() const {return m_pa_table_status;}
    bool IsPAHistoryReady() const;
    void ResetPAHistory();

    /* serialized history fetch queue */
    bool IsFetchQueueEmpty() const { return m_fetch_queue.empty(); }
    bool IsFetchIdle() const { return m_pa_table_status == CalibStatus::IDLE || m_pa_table_status == CalibStatus::FINISHED; }
    bool PrepareFetchQueue();
    void SendNextFetch();

    void RequestFlowRateResult();
    CalibStatus GetFlowRateResultStatus() const {return m_flow_results_status;}
    bool IsFlowRateReady() const { return m_flow_results_status == CalibStatus::FINISHED;}
    void ResetFlowRateResult();

    int  GetCalibVersion() const {return m_calib_version;}
    void SyncCalibVersion() { if (IsVersionInited()) { m_last_calib_version = m_calib_version; } }
    void ResetCalibVersion() {m_last_calib_version.reset();}
    bool IsVersionExpired() const;
    bool IsVersionInited() const { return m_calib_version > -1;}

    bool IsSupportNewAutoCali() const {return m_support_new_auto_cali;}

public:
    void                        SetStashCalibFinished(bool finished) {m_calib_finished = finished;}
    bool                        GetStashCalibFinished() { return m_calib_finished;}

    void                        SetStashFlowRatio(float ratio) { m_flow_ratio = ratio;}
    float                       GetStashFlowRatio() const {return m_flow_ratio;}

    FlowRatioCalibrationType    GetFlowRatioCalibType() {return m_flow_ratio_calibration_type;}
    void                        SetFlowRatioCalibType(const FlowRatioCalibrationType &type) { m_flow_ratio_calibration_type = type; }

    ManualPaCaliMethod          GetManualPaCalibMethod() {return m_manual_pa_cali_method;}
    void                        SetManualPaCalibMethod(const ManualPaCaliMethod& method) { m_manual_pa_cali_method = method;}

    NozzleDiameterType          GetSelectedNozzleDiameter() {return m_selected_nozzle_diameter;}
    void                        SetSelectedNozzleDiameter(const NozzleDiameterType& diameter) { m_selected_nozzle_diameter = diameter;}

    std::vector<CaliPresetInfo> GetSelectedCalibPreset() {return m_selected_calib_preset;}
    void                        ResetSelectedCalibPreset() { m_selected_calib_preset.clear();}
    void                        SetSelectedCalibPreset(const std::vector<CaliPresetInfo>& preset) { m_selected_calib_preset = preset;}

    std::vector<PACalibResult>          GetPAHistory() const { return PaHistoryFilter(m_pa_calib_tab).get(); }
    PaHistoryFilter                     GetPaHistoryFilter() const { return PaHistoryFilter(m_pa_calib_tab); }
    std::vector<PACalibResult>          GetPAResult() const {return m_pa_calib_results; }
    std::vector<FlowRatioCalibResult>   GetFlowRatioResult() const {return m_flow_ratio_results; }

protected:
    void ExtrusionCalibSetParse(const json &jj);
    void ExtrusionCalibSelectParse(const json &jj);
    void ExtrusionCalibGetTableParse(const json &jj);
    void ExtrusionCalibGetResultParse(const json &jj);
    void FlowrateGetResultParse(const json &jj);

private:
    MachineObject* m_owner{nullptr};

    std::vector<PACalibResult>          m_pa_calib_tab;
    std::vector<PACalibResult>          m_pa_calib_results;
    std::vector<FlowRatioCalibResult>   m_flow_ratio_results;
    std::vector<PACalibExtruderInfo>    m_fetch_queue;

    CalibStatus m_pa_results_status{CalibStatus::IDLE};
    CalibStatus m_pa_table_status{CalibStatus::IDLE};
    CalibStatus m_flow_results_status{CalibStatus::IDLE};

    FlowRatioCalibrationType   m_flow_ratio_calibration_type{FlowRatioCalibrationType::COMPLETE_CALIBRATION};
    ManualPaCaliMethod         m_manual_pa_cali_method{ManualPaCaliMethod::PA_LINE};

    // calibration page selected info
    // 1: record when start calibration in preset page
    // 2: reset when start calibration in start page
    // 3: save tray_id, filament_id, setting_id, and name, nozzle_dia
    // std::vector<CaliPresetInfo> selected_cali_preset;
    NozzleDiameterType          m_selected_nozzle_diameter{NozzleDiameterType::NONE_DIAMETER_TYPE};
    std::vector<CaliPresetInfo> m_selected_calib_preset;

    // stash calibrating info
    float                       m_flow_ratio { 0.0 };
    bool                        m_calib_finished{false};

public:
    static void ParseCalibVersion(const json& j, DevCalib* system);

    static void ParseSupportNewAutoCalib(int flag, DevCalib* system);

    static void ParseV1_0(const json& print_json, DevCalib* system, bool key_field_only);
};

} // namespace Slic3r