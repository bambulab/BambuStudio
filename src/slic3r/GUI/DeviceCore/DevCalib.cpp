#include <algorithm>
#include <boost/log/trivial.hpp>
#include <cmath>
#include <iterator>
#include <set>
#include "slic3r/GUI/GUI_App.hpp"

#include "slic3r/GUI/UserNotification.hpp"
#include "slic3r/Utils/CalibUtils.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <wx/dir.h>
#include "fast_float/fast_float.h"

#include "DevCalib.h"
#include "DevDefs.h"
#include "DevFilaSystem.h"
#include "DevConfig.h"
#include "DevExtruderSystem.h"
#include "DevNozzleSystem.h"
#include "slic3r/GUI/DeviceManager.hpp"

namespace Slic3r {

PaHistoryFilter::PaHistoryFilter(const std::vector<PACalibResult>& history)
    : m_data(history)
{
}

void PaHistoryFilter::reset(const std::vector<PACalibResult>& history)
{
    m_data = history;
    clear_filters();
}

PaHistoryFilter& PaHistoryFilter::set_filament_id(const std::string& filament_id)
{
    m_filament_id = filament_id;
    return *this;
}

PaHistoryFilter& PaHistoryFilter::set_nozzle_volume_type(NozzleVolumeType volume_type)
{
    m_nozzle_volume_type = volume_type;
    return *this;
}

PaHistoryFilter& PaHistoryFilter::set_nozzle_volume_type(std::optional<NozzleVolumeType> volume_type)
{
    m_nozzle_volume_type = volume_type;
    return *this;
}

PaHistoryFilter& PaHistoryFilter::set_nozzle_diameter(NozzleDiameterType diameter)
{
    if (diameter == NozzleDiameterType::NONE_DIAMETER_TYPE)
        m_nozzle_diameter.reset();
    else
        m_nozzle_diameter = diameter;
    return *this;
}

PaHistoryFilter& PaHistoryFilter::set_nozzle_diameter(std::optional<NozzleDiameterType> diameter)
{
    if (!diameter || diameter.value() == NozzleDiameterType::NONE_DIAMETER_TYPE)
        m_nozzle_diameter.reset();
    else
        m_nozzle_diameter = diameter;
    return *this;
}

PaHistoryFilter& PaHistoryFilter::set_extruder_id(std::optional<int> extruder_id)
{
    if (extruder_id && extruder_id.value() >= 0)
        m_extruder_id = extruder_id;
    else
        m_extruder_id.reset();
    return *this;
}

PaHistoryFilter& PaHistoryFilter::set_pa_profile_name(const std::string& pa_profile_name)
{
    m_pa_profile_name = pa_profile_name;
    return *this;
}

PaHistoryFilter& PaHistoryFilter::set_cali_idx(int cali_idx)
{
    m_cali_idx = cali_idx;
    return *this;
}

PaHistoryFilter& PaHistoryFilter::set_custom_filter(std::function<bool(const PACalibResult&)> predicate)
{
    m_custom_filter = std::move(predicate);
    return *this;
}

PaHistoryFilter& PaHistoryFilter::set_nozzle_pos_id(std::optional<int> nozzle_pos_id)
{
    if (nozzle_pos_id && nozzle_pos_id.value() >= 0)
        m_nozzle_pos_id = nozzle_pos_id;
    else
        m_nozzle_pos_id.reset();
    return *this;
}

void PaHistoryFilter::clear_filters()
{
    m_filament_id.clear();
    m_nozzle_volume_type.reset();
    m_nozzle_diameter.reset();
    m_extruder_id.reset();
    m_pa_profile_name.clear();
    m_cali_idx.reset();
    m_nozzle_pos_id.reset();
    m_custom_filter = nullptr;
}

size_t PaHistoryFilter::count() const
{
    return std::count_if(m_data.begin(), m_data.end(), [this](const PACalibResult& result) { return matches(result); });
}

bool PaHistoryFilter::empty() const
{
    return std::none_of(m_data.begin(), m_data.end(), [this](const PACalibResult& result) { return matches(result); });
}

std::vector<PACalibResult> PaHistoryFilter::get() const
{
    std::vector<PACalibResult> matched;
    std::copy_if(m_data.begin(), m_data.end(), std::back_inserter(matched), [this](const PACalibResult& result) { return matches(result); });

    return matched;
}

const PACalibResult* PaHistoryFilter::find_by_cali_idx(int cali_idx) const
{
    auto it = std::find_if(m_data.begin(), m_data.end(), [this, cali_idx](const PACalibResult& result) {
                               return result.cali_idx == cali_idx && matches(result);
                           });

    return it == m_data.end() ? nullptr : &(*it);
}

bool PaHistoryFilter::matches(const PACalibResult& result) const
{
    if (m_extruder_id && result.extruder_id != m_extruder_id.value()) return false;
    if (m_nozzle_volume_type && result.nozzle_volume_type != m_nozzle_volume_type.value()) return false;
    if (m_cali_idx && result.cali_idx != m_cali_idx.value()) return false;
    if (!m_filament_id.empty() && result.filament_id != m_filament_id) return false;
    if (!m_pa_profile_name.empty() && result.name != m_pa_profile_name) return false;
    if (m_nozzle_diameter && DevNozzle::ToNozzleDiameterType(result.nozzle_diameter) != m_nozzle_diameter.value())
        return false;
    if (m_nozzle_pos_id && result.nozzle_pos_id != m_nozzle_pos_id.value()) return false;
    if (m_custom_filter && !m_custom_filter(result)) return false;

    return true;
}

/* no diameter / extruder / volume filters → whole table; otherwise only the sent slice */
static bool row_matches_cover(const PACalibExtruderInfo &req, const PACalibResult &row)
{
    if (!req.use_nozzle_diameter && !req.use_extruder_id && !req.use_nozzle_volume_type)
        return true;
    if (req.use_nozzle_diameter && std::abs(row.nozzle_diameter - req.nozzle_diameter) > 1e-3f) return false;
    if (req.use_extruder_id && row.extruder_id != req.extruder_id) return false;
    if (req.use_nozzle_volume_type && row.nozzle_volume_type != req.nozzle_volume_type) return false;
    if (req.nozzle_pos_id >= 0 && row.nozzle_pos_id != req.nozzle_pos_id) return false;
    if (!req.nozzle_sn.empty() && row.nozzle_sn != req.nozzle_sn) return false;
    return true;
}

static float string_to_float(const std::string &str_value)
{
    float value = 0.0;
    fast_float::from_chars(str_value.c_str(), str_value.c_str() + str_value.size(), value);
    return value;
}

static NozzleVolumeType convert_to_nozzle_type(const std::string &str)
{
    if (str.size() < 8) {
        assert(false && "invalid nozzle info");
        return NozzleVolumeType::nvtStandard;
    }

    if (str[1] == 'S')
        return NozzleVolumeType::nvtStandard;
    else if (str[1] == 'H')
        return NozzleVolumeType::nvtHighFlow;
    else if (str[1] == 'U')
        return NozzleVolumeType::nvtTPUHighFlow;
    else if (str[1] == 'B')
        return NozzleVolumeType::nvtE3DHighFlow;
    else
        return NozzleVolumeType::nvtStandard;
}

static float get_number_flexible(const json& j, const std::string& key, float def = 0.0f)
{
    if (!j.contains(key)) return def;

    const auto& v = j[key];

    if (v.is_number_float())    return v.get<float>();
    if (v.is_number_integer())  return static_cast<float>(v.get<int>());
    if (v.is_string())          return string_to_float(v.get<std::string>());

    return def;
}

void from_json(const json& j, PACalibResult& cali) {
    cali.extruder_id        = j.value("extruder_id", 0);
    cali.nozzle_volume_type = convert_to_nozzle_type(j.value("nozzle_id", "HS00-0.4"));
    cali.tray_id            = j.value("tray_id",0);
    cali.ams_id             = j.value("ams_id",0);
    cali.slot_id            = j.value("slot_id",0);
    cali.cali_idx           = j.value("cali_idx",-1);
    cali.nozzle_pos_id      = j.value("nozzle_pos",-1);
    cali.nozzle_diameter    = get_number_flexible(j, "nozzle_diameter", 0.4f);
    cali.nozzle_sn          = j.value("nozzle_sn","");
    cali.filament_id        = j.value("filament_id","");
    cali.setting_id         = j.value("setting_id","");
    cali.name               = j.value("name","");
    cali.k_value            = get_number_flexible(j, "k_value", 0.0f);
    cali.n_coef             = get_number_flexible(j, "n_coef", 0.0f);
    cali.confidence         = j.value("confidence", 0);
}

void from_json(const json& j, FlowRatioCalibResult& cali)
{
    cali.tray_id         = j.value("tray_id", 0);
    cali.nozzle_diameter = string_to_float(j.value("nozzle_diameter", ""));
    cali.filament_id     = j.value("filament_id", "");
    cali.setting_id      = j.value("setting_id", "");
    cali.flow_ratio      = string_to_float(j.value("flow_ratio", ""));
    cali.confidence      = j.value("confidence", 0);
}

void DevCalib::ParseCalibVersion(const json& j, DevCalib* system)
{
    if(system) system->m_calib_version = j.value("cali_version", -1);
}

bool DevCalib::IsVersionExpired() const
{
    if (m_last_calib_version.has_value())
        return m_last_calib_version.value() != m_calib_version;
    else
        return true;
}

bool DevCalib::IsPAHistoryReady() const
{
    // ready = version already synced && nothing in flight && queue drained
    return !IsVersionExpired() && IsFetchIdle() && IsFetchQueueEmpty();
}

void DevCalib::ParseSupportNewAutoCalib(int flag, DevCalib* system)
{
    if(system) system->m_support_new_auto_cali = flag;
}

void DevCalib::RequestPAResult()
{
    m_pa_results_status = CalibStatus::REQUEST;
}

void DevCalib::ResetPAResult()
{
    m_pa_calib_results.clear();
    m_pa_results_status = CalibStatus::IDLE;
}

int DevCalib::RequestPAHistory(const PACalibExtruderInfo &calib_info)
{
    m_fetch_queue.push_back(calib_info);
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__
                           << " enqueue dia=" << calib_info.nozzle_diameter
                           << " use_dia=" << calib_info.use_nozzle_diameter
                           << " ext=" << calib_info.extruder_id
                           << " use_ext=" << calib_info.use_extruder_id
                           << " queue=" << m_fetch_queue.size();
    SendNextFetch();
    return 0;
}

bool DevCalib::PrepareFetchQueue()
{
    if (!m_fetch_queue.empty()) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " skip, queue already size=" << m_fetch_queue.size();
        return true;
    }

    MachineObject *obj = GetOwner();
    if (!obj || !obj->is_info_ready()) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " machine info not ready, keep expired";
        return false; // keep version expired so the next tick retries
    }

    if (obj->supports_full_pa_calib_table()) {
        PACalibExtruderInfo full;
        full.use_nozzle_diameter    = false;
        full.use_extruder_id        = false;
        full.use_nozzle_volume_type = false;
        m_fetch_queue.push_back(full);
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " enqueue full table fetch";
        return true;
    }

    /* Model supported diameters, not the installed ones: cali_version does not change on a nozzle swap. */
    std::set<NozzleDiameterType> diameter_types;
    for (const std::string &diameter : GUI::CalibUtils::get_supported_nozzle_diameters_by_model(obj)) {
        const NozzleDiameterType dia_type = DevNozzle::ToNozzleDiameterType(diameter);
        if (dia_type != NozzleDiameterType::NONE_DIAMETER_TYPE) {
            diameter_types.insert(dia_type);
        }
    }

    if (diameter_types.empty()) {
        diameter_types.insert(NozzleDiameterType::NOZZLE_DIAMETER_0_4);
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << " no model supported nozzle, fallback to 0.4";
    }

    for (NozzleDiameterType dia_type : diameter_types) {
        PACalibExtruderInfo item;
        item.nozzle_diameter        = DevNozzle::ToNozzleDiameterFloat(dia_type);
        item.use_nozzle_diameter    = true;
        item.use_extruder_id        = false;
        item.use_nozzle_volume_type = false;
        m_fetch_queue.push_back(item);
    }
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " prepared diameter slices=" << m_fetch_queue.size();
    return !m_fetch_queue.empty();
}

void DevCalib::SendNextFetch()
{
    if (!IsFetchIdle() || m_fetch_queue.empty())
        return;

    const PACalibExtruderInfo next = m_fetch_queue.front();
    m_pa_table_status = CalibStatus::REQUEST;
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__
                           << " send dia=" << next.nozzle_diameter
                           << " use_dia=" << next.use_nozzle_diameter
                           << " ext=" << next.extruder_id
                           << " use_ext=" << next.use_extruder_id
                           << " remain=" << m_fetch_queue.size();

    if (0 != GetOwner()->command_get_pa_calibration_tab(next)) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << " publish failed, skip item remain=" << m_fetch_queue.size() - 1;
        m_fetch_queue.erase(m_fetch_queue.begin());
        m_pa_table_status = CalibStatus::IDLE;
    }
}

void DevCalib::ResetPAHistory()
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " clear tab=" << m_pa_calib_tab.size() << " queue=" << m_fetch_queue.size();
    m_pa_calib_tab.clear();
    m_fetch_queue.clear();
    m_pa_table_status = CalibStatus::IDLE;
}

void DevCalib::RequestFlowRateResult()
{
    m_flow_results_status = CalibStatus::REQUEST;
}

void DevCalib::ResetFlowRateResult()
{
    m_flow_ratio_results.clear();
    m_flow_results_status = CalibStatus::IDLE;
}

void calib_fail_message(MachineObject* obj, std::string cali_mode, std::string reason){
    wxString    info;
    if (reason == "invalid nozzle_diameter" || reason == "nozzle_diameter is not supported") {
        info = _L("This calibration does not support the currently selected nozzle diameter");
    } else if (reason == "invalid handle_flowrate_cali param") {
        info = _L("Current flowrate cali param is invalid");
    } else if (reason == "nozzle_diameter is not matched") {
        info = _L("Selected diameter and machine diameter do not match");
    } else if (reason == "generate auto filament cali gcode failure") {
        info = _L("Failed to generate cali gcode");
    } else {
        info = wxString(reason);
    }

    GUI::wxGetApp().push_notification(obj, info, _L("Calibration error"), UserNotificationStyle::UNS_WARNING_CONFIRM);
    BOOST_LOG_TRIVIAL(info) << cali_mode << " result fail, reason = " << reason;
}

void DevCalib::ExtrusionCalibSetParse(const json & jj){
    int tray_id = jj.value("tray_id", -1);

    auto tray_ams_slot_map = GetOwner()->GetFilaSystem()->GetTrayIndexMap();
    int ams_id  = tray_ams_slot_map.find(tray_id) != tray_ams_slot_map.end() ? tray_ams_slot_map[tray_id].first : -1;
    int slot_id = tray_ams_slot_map.find(tray_id) != tray_ams_slot_map.end() ? tray_ams_slot_map[tray_id].second : -1;

    if(tray_id == VIRTUAL_TRAY_MAIN_ID) {
        GetOwner()->vt_slot[MAIN_EXTRUDER_ID].k = jj.value("k_value", GetOwner()->vt_slot[MAIN_EXTRUDER_ID].k);
        GetOwner()->vt_slot[MAIN_EXTRUDER_ID].n = jj.value("n_value", GetOwner()->vt_slot[MAIN_EXTRUDER_ID].n);
    }else{
        auto tray_item = GetOwner()->GetFilaSystem()->GetAmsTray(std::to_string(ams_id), std::to_string(slot_id));
        if (tray_item) {
            tray_item->k = jj.value("k_value", tray_item->k);
            tray_item->n = jj.value("n_coef", tray_item->n);
        }
    }

    GetOwner()->extrusion_cali_set_tray_id = tray_id;
    GetOwner()->extrusion_cali_set_hold_start = std::chrono::system_clock::now();
}

/* calib select ack parse */
void DevCalib::ExtrusionCalibSelectParse(const json &jj){
    try{
        int tray_id = jj.value("tray_id", -1);

        auto tray_ams_slot_map = GetOwner()->GetFilaSystem()->GetTrayIndexMap();
        int default_ams_id = tray_ams_slot_map.find(tray_id) != tray_ams_slot_map.end() ? tray_ams_slot_map[tray_id].first : -1;
        int default_slot_id = tray_ams_slot_map.find(tray_id) != tray_ams_slot_map.end() ? tray_ams_slot_map[tray_id].second : -1;

        int ams_id = jj.value("ams_id", default_ams_id);
        int slot_id = jj.value("slot_id", default_slot_id);

        BOOST_LOG_TRIVIAL(trace) << "extrusion_cali_sel: illegal ams_id = " << ams_id << "slot_id = " << slot_id;

        std::vector<DevAmsTray> &vt_slot = GetOwner()->vt_slot;
        if (ams_id == VIRTUAL_TRAY_MAIN_ID && vt_slot.size() > 0) {
            vt_slot[MAIN_EXTRUDER_ID].cali_idx = jj.value("cali_idx", vt_slot[MAIN_EXTRUDER_ID].cali_idx);
            vt_slot[MAIN_EXTRUDER_ID].set_hold_count();
        } else if (ams_id == VIRTUAL_TRAY_DEPUTY_ID && vt_slot.size() > 1) {
            vt_slot[DEPUTY_EXTRUDER_ID].cali_idx = jj.value("cali_idx", vt_slot[DEPUTY_EXTRUDER_ID].cali_idx);
            vt_slot[DEPUTY_EXTRUDER_ID].set_hold_count();
        } else {
            auto tray_item = GetOwner()->GetFilaSystem()->GetAmsTray(std::to_string(ams_id), std::to_string(slot_id));
            if (tray_item) {
                tray_item->cali_idx = jj.value("cali_idx", tray_item->cali_idx);
                tray_item->set_hold_count();
            }
        }
    } catch(...){

    }
}

void DevCalib::ExtrusionCalibGetTableParse(const json &jj){
    if (GetPAHistoryStatus() == CalibStatus::REQUEST ) {
        if (m_fetch_queue.empty()) {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " drop stale reply, queue empty";
            m_pa_table_status = CalibStatus::IDLE;
            return;
        }

        m_pa_table_status = CalibStatus::WAITING;

        /* request success */
        if (!(jj.contains("result") && jj.contains("reason") && jj["result"].get<std::string>() == "fail")) {
            SyncCalibVersion();
            m_pa_table_status = CalibStatus::FINISHED;
        }

        const PACalibExtruderInfo &active = m_fetch_queue.front();

        try{
            json filaments_json;
            if(jj.contains("filaments"))
            {
                /* fill item->nozzle_diameter with command->nozzle_diameter */
                filaments_json = jj["filaments"];
                for (auto &f : filaments_json) {
                    if (!f.contains("nozzle_diameter") && jj.contains("nozzle_diameter")) {
                        f["nozzle_diameter"] = jj["nozzle_diameter"];
                    }
                }
            }

            std::vector<PACalibResult> new_rows = filaments_json.get<std::vector<PACalibResult>>();

            /* filter invalid pa_calib_tab */
            new_rows.erase(std::remove_if(new_rows.begin(), new_rows.end(), [](auto &res) { return res.k_value < 0.0f || res.k_value > 10.0f; }), new_rows.end());

            const size_t before = m_pa_calib_tab.size();
            m_pa_calib_tab.erase(std::remove_if(m_pa_calib_tab.begin(), m_pa_calib_tab.end(),
                                               [&active](const PACalibResult &row) { return row_matches_cover(active, row); }),
                                m_pa_calib_tab.end());
            m_pa_calib_tab.insert(m_pa_calib_tab.end(), new_rows.begin(), new_rows.end());

            BOOST_LOG_TRIVIAL(info) << __FUNCTION__
                                   << " cover dia=" << active.nozzle_diameter
                                   << " use_dia=" << active.use_nozzle_diameter
                                   << " ext=" << active.extruder_id
                                   << " use_ext=" << active.use_extruder_id
                                   << " tab " << before << "->" << m_pa_calib_tab.size()
                                   << " new_rows=" << new_rows.size();
        } catch(...) {
            BOOST_LOG_TRIVIAL(error) << "pa calib history missing fields, current json:\n "<< jj.dump();
        }

        m_fetch_queue.erase(m_fetch_queue.begin());
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " pop queue remain=" << m_fetch_queue.size() << " status=" << static_cast<int>(GetPAHistoryStatus());
        if (GetPAHistoryStatus() == CalibStatus::WAITING)
            m_pa_table_status = CalibStatus::IDLE;
        // notify cali history to update
    }
}

void DevCalib::ExtrusionCalibGetResultParse(const json &jj)
{
    m_pa_results_status = CalibStatus::WAITING;

    if (!(jj.contains("result") && jj.contains("reason") && jj["result"].get<std::string>() == "fail" && jj.contains("err_code"))) {
        m_pa_results_status = CalibStatus::FINISHED;
    }

    try {
        json filaments_json;
        if(jj.contains("filaments"))
        {
            /* fill item->nozzle_diameter with command->nozzle_diameter */
            filaments_json = jj["filaments"];
            for (auto &f : filaments_json) {
                if (!f.contains("nozzle_diameter") && jj.contains("nozzle_diameter") ) {
                    f["nozzle_diameter"] = jj["nozzle_diameter"];
                }

                if (IsSupportNewAutoCali()) {
                    auto ams_id = f.value("ams_id", 0);
                    auto slot_id = f.value("slot_id", 0);
                    if(f.contains("tray_id")){
                        f["tray_id"] = GetOwner()->GetFilaSystem()->GetTrayIdByAmsSlotId(ams_id, slot_id);
                    }
                }
            }
        }

        m_pa_calib_results = filaments_json.get<std::vector<PACalibResult>>();

        m_pa_calib_results.erase(std::remove_if(m_pa_calib_results.begin(), m_pa_calib_results.end(), [](auto &res) { return res.k_value < 0.0f || res.k_value > 10.0f; }), m_pa_calib_results.end());

        if (m_pa_calib_results.empty()) { BOOST_LOG_TRIVIAL(info) << "empty pa calib result"; }
    } catch (...) {
        m_pa_calib_results.clear();
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "pa calibration results missing fileds, current json: \n"<<jj.dump();
    }
}

void DevCalib::FlowrateGetResultParse(const json &jj){
    m_flow_results_status = CalibStatus::FINISHED;
    m_flow_ratio_results.clear();

    if(!jj.contains("filaments")) return;

    try {
        m_flow_ratio_results = jj["filaments"].get<std::vector<FlowRatioCalibResult>>();
    } catch (...) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "flow ratio calibration results missing fileds, current json:\n"<<jj.dump();
    }
}

void DevCalib::ParseV1_0(const json &jj, DevCalib *system, bool key_field_only)
{
    if(!jj.contains("command")) return;

    if (jj["command"].get<std::string>() == "extrusion_cali" || jj["command"].get<std::string>() == "flowrate_cali") {
        if (jj.contains("result")) {
            if (jj["result"].get<std::string>() == "success") {
            } else if (jj["result"].get<std::string>() == "fail") {
                std::string cali_mode = jj["command"].get<std::string>();
                std::string reason    = jj["reason"].get<std::string>();
                calib_fail_message(system->GetOwner(), cali_mode, reason);
            }
        }
    } else if (jj["command"].get<std::string>() == "extrusion_cali_set") {
        system->ExtrusionCalibSetParse(jj);
    } else if (jj["command"].get<std::string>() == "extrusion_cali_sel") {
        system->ExtrusionCalibSelectParse(jj);
    } else if (jj["command"].get<std::string>() == "extrusion_cali_get") {
        system->ExtrusionCalibGetTableParse(jj);
    } else if (jj["command"].get<std::string>() == "extrusion_cali_get_result") {
        system->ExtrusionCalibGetResultParse(jj);
    } else if (jj["command"].get<std::string>() == "flowrate_get_result" && !key_field_only) {
        system->FlowrateGetResultParse(jj);
    }
}

} // namespace Slic3r