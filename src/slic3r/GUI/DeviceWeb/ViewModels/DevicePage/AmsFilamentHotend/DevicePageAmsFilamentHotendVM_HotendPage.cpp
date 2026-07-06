#include "DevicePageAmsFilamentHotendVM.hpp"

#include "slic3r/GUI/DeviceCore/DevDefs.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/DeviceCore/DevNozzleRack.h"
#include "slic3r/GUI/DeviceCore/DevNozzleSystem.h"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/DeviceWeb/DeviceWebHost.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/wxExtensions.hpp"

#include <algorithm>
#include <boost/format.hpp>
#include <vector>
#include <wx/sizer.h>

namespace Slic3r { namespace GUI {

namespace {

std::string normalize_hex_for_web(const std::string& raw)
{
    if (raw.empty()) return "#D9D9D9";
    std::string hex = raw;
    if (hex[0] != '#') hex = "#" + hex;
    if (hex.size() >= 7) hex = hex.substr(0, 7);
    return hex;
}

std::string color_type_for_web(DevFilaColorType color_type)
{
    switch (color_type) {
    case DevFilaColorType::CTYPE_MULTI:    return "multi";
    case DevFilaColorType::CTYPE_GRADIANT: return "gradient";
    case DevFilaColorType::CTYPE_SINGLE:   return "single";
    }
    return "single";
}

std::string nozzle_flow_short_name(NozzleFlowType flow_type)
{
    switch (flow_type) {
    case NozzleFlowType::S_FLOW: return "Std";
    case NozzleFlowType::H_FLOW: return "HF";
    case NozzleFlowType::U_FLOW: return "UHF";
    case NozzleFlowType::T_FLOW: return "TF";
    default: return "";
    }
}

bool is_pen_tool(const DevNozzleToolDefs& tool_defs)
{
    return tool_defs.tool_type == DevNozzleToolType::Cut &&
           tool_defs.cut_info.has_value() &&
           tool_defs.cut_info->type == CutType::Pen;
}

std::string tool_kind_for_web(const DevNozzleToolDefs& tool_defs)
{
    namespace SV = AmsHotendSchema::values;
    switch (tool_defs.tool_type) {
    case DevNozzleToolType::Nozzle: return SV::tool_kind::nozzle;
    case DevNozzleToolType::Cut:    return is_pen_tool(tool_defs) ? SV::tool_kind::pen : SV::tool_kind::cutter;
    case DevNozzleToolType::Laser:  return SV::tool_kind::laser;
    case DevNozzleToolType::Fan:    return SV::tool_kind::fan;
    case DevNozzleToolType::None:
    default:                        return SV::tool_kind::empty;
    }
}

std::string tool_label_for_web(const DevNozzleToolDefs& tool_defs)
{
    switch (tool_defs.tool_type) {
    case DevNozzleToolType::Cut:   return is_pen_tool(tool_defs) ? _u8L("Pen") : _u8L("Cutter");
    case DevNozzleToolType::Laser: return _u8L("Laser");
    case DevNozzleToolType::Fan:   return _u8L("Fan");
    case DevNozzleToolType::Nozzle:
    case DevNozzleToolType::None:
    default:                       return "--";
    }
}

std::string detail_tool_image_name(const DevNozzleToolDefs& tool_defs)
{
    return tool_defs.tool_type == DevNozzleToolType::Nozzle ? "hotend_detail_nozzle" : "empty";
}

std::string wx_to_utf8(const wxString& value)
{
    return value.ToUTF8().data();
}

int addr_id_to_display_idx(int addr_id) { return addr_id + 1; }

int rack_pos_id_from_addr_id(int addr_id) { return addr_id + 0x10; }

std::string rack_operation_for_job(int job, int reading_count)
{
    namespace SV = AmsHotendSchema::values;
    switch (job) {
    case 1: return SV::rack_operation::take;
    case 2: return SV::rack_operation::place;
    case 3: return SV::rack_operation::exchange;
    case 4: return SV::rack_operation::read_all;
    case 5: return SV::rack_operation::move;
    case 6: return SV::rack_operation::read_single;
    default:
        if (reading_count > 1) return SV::rack_operation::read_all;
        if (reading_count == 1) return SV::rack_operation::read_single;
        return SV::rack_operation::idle;
    }
}

int rack_target_display_index(MachineObject* machine_obj, int job, int reading_idx)
{
    if (!machine_obj || !machine_obj->GetNozzleSystem()) return reading_idx;
    const int rack_count = machine_obj->GetNozzleSystem()->GetNozzleRack()
        ? machine_obj->GetNozzleSystem()->GetNozzleRack()->GetTotalRackNozzleCount()
        : 0;
    auto id_to_display = [rack_count](int id) {
        if (id == 0xff || id < 0x10) return 0;
        const int display = id - 0x10 + 1;
        return rack_count <= 0 || display <= rack_count ? display : 0;
    };

    const auto src = machine_obj->GetNozzleSystem()->GetReplaceNozzleSrc();
    const auto tar = machine_obj->GetNozzleSystem()->GetReplaceNozzleTar();
    if (job == 1 || job == 3) {
        if (src) { const int d = id_to_display(*src); if (d > 0) return d; }
        if (tar) { const int d = id_to_display(*tar); if (d > 0) return d; }
    } else if (job == 2) {
        if (tar) { const int d = id_to_display(*tar); if (d > 0) return d; }
        if (src) { const int d = id_to_display(*src); if (d > 0) return d; }
    }
    return reading_idx;
}

std::string rack_step_label(DevNozzleRack::RackStatus step)
{
    switch (step) {
    case DevNozzleRack::RACK_STATUS_HOTEND_CENTRE:         return _u8L("Homing Induction Hotend Rack");
    case DevNozzleRack::RACK_STATUS_TOOLHEAD_CENTRE:       return _u8L("Homing Toolhead");
    case DevNozzleRack::RACK_STATUS_CALIBRATE_HOTEND_RACK: return _u8L("Calibrating Hotend Rack");
    case DevNozzleRack::RACK_STATUS_WAIT_HOTEND_HEATING:   return _u8L("Waiting for hotend to heat");
    case DevNozzleRack::RACK_STATUS_CUT_MATERIAL:          return _u8L("Cutting Filament");
    case DevNozzleRack::RACK_STATUS_UNLOCK_HOTEND:         return _u8L("Unlocking Hotend");
    case DevNozzleRack::RACK_STATUS_LIFT_HOTEND_RACK:      return _u8L("Lifting Hotend Rack");
    case DevNozzleRack::RACK_STATUS_PLACE_HOTEND:          return _u8L("Placing Hotend");
    case DevNozzleRack::RACK_STATUS_PICK_HOTEND:           return _u8L("Taking Hotend");
    case DevNozzleRack::RACK_STATUS_LOCK_HOTEND:           return _u8L("Locking Hotend");
    case DevNozzleRack::RACK_STATUS_MOVE:                  return _u8L("Moving Hotend Rack");
    case DevNozzleRack::RACK_STATUS_IDLE:                  return _u8L("Idle");
    case DevNozzleRack::RACK_STATUS_UNKNOWN:
    case DevNozzleRack::RACK_STATUS_END:
    default:                                               return _u8L("Hotend rack moving");
    }
}

nlohmann::json build_rack_steps(const std::vector<int>& raw_steps, DevNozzleRack::RackStatus current_status)
{
    namespace SK = AmsHotendSchema::keys;
    namespace SV = AmsHotendSchema::values;

    std::vector<int> steps = raw_steps;
    if (steps.empty() && current_status > DevNozzleRack::RACK_STATUS_IDLE && current_status < DevNozzleRack::RACK_STATUS_END) {
        steps.push_back(static_cast<int>(current_status));
    }

    const auto active_it = std::find(steps.begin(), steps.end(), static_cast<int>(current_status));
    const int active_index = active_it == steps.end() ? -1 : static_cast<int>(std::distance(steps.begin(), active_it));

    nlohmann::json result = nlohmann::json::array();
    for (int i = 0; i < static_cast<int>(steps.size()); ++i) {
        const int step = steps[i];
        std::string state = SV::rack_step_state::pending;
        if (active_index >= 0) {
            if (i < active_index) state = SV::rack_step_state::done;
            else if (i == active_index) state = SV::rack_step_state::active;
        }
        result.push_back({
            {SK::rack_step::id, step},
            {SK::rack_step::label, rack_step_label(static_cast<DevNozzleRack::RackStatus>(step))},
            {SK::rack_step::state, state}
        });
    }
    return result;
}

class HotendDetailDialog : public DPIDialog
{
public:
    explicit HotendDetailDialog(wxWindow* parent)
        : DPIDialog(parent, wxID_ANY, _L("Hotend Details"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
    {
        SetClientSize(wxSize(FromDIP(520), FromDIP(320)));
        SetMinClientSize(wxSize(FromDIP(520), FromDIP(320)));
    }

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override { SetSize(suggested_rect.GetSize()); }
};

void open_hotend_detail_dialog(const nlohmann::json& payload)
{
    auto* dev_mgr = wxGetApp().getDeviceManager();
    MachineObject* machine_obj = dev_mgr ? dev_mgr->get_selected_machine() : nullptr;
    if (!machine_obj) return;

    const int addr_id = payload.value("addr_id", -1);
    const int display_index = payload.value("display_index", -1);
    if (display_index <= 0) return;

    wxWindow* parent = wxGetApp().mainframe;
    HotendDetailDialog dlg(parent);
    const std::string route = (boost::format("/device_page/hotend_detail?addr_id=%1%&display_index=%2%")
        % addr_id % display_index).str();
    auto* host = new DeviceWebHost(&dlg, DeviceWebHostMode::DevicePageAmsFilamentHotend, route);
    host->SetMinSize(wxSize(dlg.FromDIP(520), dlg.FromDIP(320)));
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(host, 1, wxEXPAND);
    dlg.SetSizer(sizer);
    dlg.Layout();
    dlg.ShowModal();
}

} // namespace

void DevicePageAmsFilamentHotendVM::BuildHotendRackStatus(MachineObject* machine_obj, nlohmann::json& hotend_page) const
{
    namespace SK = AmsHotendSchema::keys;
    namespace SV = AmsHotendSchema::values;

    auto nozzle_system = machine_obj ? machine_obj->GetNozzleSystem() : nullptr;
    auto nozzle_rack = nozzle_system ? nozzle_system->GetNozzleRack() : nullptr;
    const int reading_idx = nozzle_rack ? nozzle_rack->GetReadingIdx() : 0;
    const int reading_count = nozzle_rack ? nozzle_rack->GetReadingCount() : 0;
    const int rack_job = nozzle_rack ? nozzle_rack->GetJob() : 0;
    const auto rack_status = nozzle_rack ? nozzle_rack->GetStatus() : DevNozzleRack::RACK_STATUS_UNKNOWN;
    const auto rack_operation = rack_operation_for_job(rack_job, reading_count);
    const bool rack_reading = reading_count > 0;
    const bool rack_busy = rack_job != 0 || rack_reading;
    const bool rack_failed = rack_status == DevNozzleRack::RACK_STATUS_UNKNOWN && rack_busy;
    const int target_display_index = rack_target_display_index(machine_obj, rack_job, reading_idx);

    hotend_page[SK::hotend_page::hotend_rack_status] = {
        {SK::rack_status::active, rack_busy},
        {SK::rack_status::mode,   rack_busy ? ((rack_operation == SV::rack_operation::read_all || reading_count > 1) ? SV::rack_mode::all : SV::rack_mode::single)
                                            : SV::rack_mode::idle},
        {SK::rack_status::display_index, rack_busy ? target_display_index : 0},
        {SK::rack_status::current,       rack_reading ? reading_idx : 0},
        {SK::rack_status::total,         rack_reading ? reading_count : static_cast<int>((nozzle_rack ? nozzle_rack->GetSteps().size() : 0))},
        {SK::rack_status::message, rack_reading ? (reading_count > 1
            ? (boost::format(_u8L("Reading (%1% / %2%)")) % reading_idx % reading_count).str()
            : (boost::format(_u8L("Reading hotend %1%")) % reading_idx).str()) : ""},
        {SK::rack_status::tips, rack_busy ? _u8L("Please wait...") : std::string()},
        {SK::rack_status::job, rack_job},
        {SK::rack_status::operation, rack_operation},
        {SK::rack_status::state, rack_failed ? SV::rack_state::failed : (rack_busy ? SV::rack_state::running : SV::rack_state::idle)},
        {SK::rack_status::target_display_index, rack_busy ? target_display_index : 0},
        {SK::rack_status::current_step, rack_busy ? static_cast<int>(rack_status) : 0},
        {SK::rack_status::steps, nozzle_rack ? build_rack_steps(nozzle_rack->GetSteps(), rack_status) : nlohmann::json::array()},
        {SK::rack_status::can_stop, rack_busy},
        // Retry (resume the rack job) is offered throughout an active job, not
        // only after a failure, so the button stays clickable during the
        // operation per the N1x design.
        {SK::rack_status::can_retry, rack_busy}
    };

    const bool can_read_all = nozzle_rack && nozzle_rack->CtrlCanReadAll() && !rack_busy;
    hotend_page[SK::hotend_page::actions][SK::hotend_actions::can_read_all_hotends] = can_read_all;
    if (!can_read_all) {
        hotend_page[SK::hotend_page::actions][SK::hotend_actions::read_all_disabled_reason]
            = rack_busy ? _u8L("Hotend rack is busy") : _u8L("Hotend rack cannot read all");
    }
}

nlohmann::json DevicePageAmsFilamentHotendVM::BuildSlot(MachineObject* machine_obj,
                                                        int addr_id,
                                                        const std::optional<std::pair<int, int>>& slot_id,
                                                        const Slic3r::DevAms* /*fallback_dryer_unit*/) const
{
    namespace SK = AmsHotendSchema::keys;
    namespace SV = AmsHotendSchema::values;

    auto rack_nozzle_opt = machine_obj->GetNozzleSystem()->GetNozzleOptByAddrId(addr_id);
    const int display_idx = addr_id_to_display_idx(addr_id);
    const bool is_top_tier = (display_idx % 2) == 0;

    const DevAmsTray* tray = nullptr;
    DevAmsTray tray_copy;
    std::string ams_id;
    std::string tray_id;
    if (machine_obj && slot_id.has_value()) {
        ams_id = std::to_string(slot_id->first);
        tray_id = std::to_string(slot_id->second);
        if (auto fila_system = machine_obj->GetFilaSystem(); fila_system && fila_system->GetVirtualTray(slot_id->first).has_value()) {
            tray_id = "0";
        }
        auto opt_tray = machine_obj->get_tray(slot_id->first, slot_id->second);
        if (opt_tray.has_value()) {
            tray_copy = *opt_tray;
            tray = &tray_copy;
        }
    }

    const bool has_hotend_slot = true;
    // An empty hotend slot (no nozzle/tool inserted) drives the "None" nozzle
    // visual; an inserted-but-abnormal tool drives the "Error" visual.
    const bool has_tool = rack_nozzle_opt.has_value() && rack_nozzle_opt->IsExist();
    const bool nozzle_abnormal = has_tool && rack_nozzle_opt->IsAbnormal();
    const std::string nozzle_tool_kind = has_tool ? tool_kind_for_web(rack_nozzle_opt->GetToolDefs()) : std::string(SV::tool_kind::empty);
    const bool listed_as_changing = IsSlotInFilamentChangeList(machine_obj, ams_id, tray_id);
    const bool filament_changing = IsFilamentChangeActive(machine_obj);
    const bool loaded_in_nozzle = IsSlotLoadedInNozzle(machine_obj, addr_id, ams_id, tray_id);
    const std::string card_state = ResolveCardState(machine_obj, tray, ams_id, tray_id, addr_id);
    const std::string line_state = ResolveLineState(card_state, has_hotend_slot, filament_changing, listed_as_changing, loaded_in_nozzle);
    const std::string nozzle_state = ResolveNozzleState(card_state, line_state, has_tool, nozzle_abnormal);

    nlohmann::json bind_ams = nlohmann::json::array();
    nlohmann::json ams_operation_units = nlohmann::json::array();
    int bind_ams_slot_count = 0;
    if (machine_obj && machine_obj->GetNozzleSystem() && machine_obj->GetFilaSystem()) {
        const auto& rack_nozzle = machine_obj->GetNozzleSystem()->GetNozzleOptByAddrId(addr_id);
        if (rack_nozzle.has_value()) {
            for (int bind_ams_id : rack_nozzle->GetBindAms()) {
                const std::string bind_ams_id_str = std::to_string(bind_ams_id);
                const DevAms* bind_ams_unit = machine_obj->GetFilaSystem()->GetAmsById(bind_ams_id_str);
                const int slot_count = bind_ams_unit ? bind_ams_unit->GetSlotCount() : 0;
                bind_ams_slot_count += slot_count;
                bind_ams.push_back({
                    {SK::bind_ams::ams_id, bind_ams_id_str},
                    {SK::bind_ams::ams_type, bind_ams_unit ? std::to_string((int)bind_ams_unit->GetAmsType()) : ""},
                    {SK::bind_ams::slot_count, slot_count}
                });
                if (bind_ams_unit) ams_operation_units.push_back(BuildAmsUnitJson(machine_obj, bind_ams_unit));
            }
        }
    }

    std::string filament_name;
    std::string filament_type;
    std::string color = "#D9D9D9";
    std::string color_type = "single";
    int filament_remain = 100;
    if (tray && tray->is_exists) {
        if (tray->m_fila_type == "PLA-S") filament_type = "Sup.PLA";
        else if (tray->m_fila_type == "PA-S") filament_type = "Sup.PA";
        else if (tray->m_fila_type == "ABS-S") filament_type = "Sup.ABS";
        else filament_type = tray->m_fila_type;
        filament_name = ResolveFilamentCardText(tray, filament_type);
        color = normalize_hex_for_web(tray->color);
        color_type = color_type_for_web(tray->ctype);
        if (tray->remain >= 0 && tray->remain <= 100) filament_remain = tray->remain;
    }

    nlohmann::json hotend = nlohmann::json::object();
    nlohmann::json hotend_detail_dialog;
    if (rack_nozzle_opt.has_value()) {
        const bool has_tool = has_hotend_slot && rack_nozzle_opt->IsToolConnected();
        const auto tool_defs = has_tool ? rack_nozzle_opt->GetToolDefs() : DevNozzleToolDefs();
        const bool is_nozzle_tool = tool_defs.tool_type == DevNozzleToolType::Nozzle;
        const bool is_on_extruder = IsNozzleOnExtruder(machine_obj, addr_id);
        const bool has_read_detail = has_tool && !rack_nozzle_opt->IsUnknown();
        const std::string serial_number = has_read_detail ? wx_to_utf8(rack_nozzle_opt->GetSerialNumber()) : "";
        std::optional<DevFirmwareVersionInfo> firmware;
        if (has_read_detail && !serial_number.empty()) {
            if (auto rack = machine_obj->GetNozzleSystem()->GetNozzleRack()) firmware = rack->GetNozzleFirmwareInfo(addr_id);
        }

        std::string op_state = SV::hotend_op_state::idle;
        if (has_tool && rack_nozzle_opt->IsAbnormal()) {
            op_state = SV::hotend_op_state::error;
        } else if (has_tool && rack_nozzle_opt->IsToolLostInHolder()) {
            op_state = SV::hotend_op_state::lost_in_holder;
        } else if (auto temp_status = rack_nozzle_opt->GetSetRackNozzleTempStatus()) {
            switch (*temp_status) {
            case DevNozzle::NozzleTempStatus::Heating: op_state = SV::hotend_op_state::heating; break;
            case DevNozzle::NozzleTempStatus::Cooling: op_state = SV::hotend_op_state::cooling; break;
            default: op_state = SV::hotend_op_state::idle; break;
            }
        } else if (rack_nozzle_opt->GetTargetTemp() > rack_nozzle_opt->GetCurrentTemp()) {
            op_state = SV::hotend_op_state::heating;
        } else if (rack_nozzle_opt->GetCurrentTemp() > rack_nozzle_opt->GetTargetTemp() && rack_nozzle_opt->GetTargetTemp() > 0) {
            op_state = SV::hotend_op_state::cooling;
        }
        if (auto rack = machine_obj->GetNozzleSystem()->GetNozzleRack(); rack && rack->GetReadingCount() == 1 && rack->GetReadingIdx() == display_idx) {
            op_state = SV::hotend_op_state::reading;
        } else if (auto rack = machine_obj->GetNozzleSystem()->GetNozzleRack(); rack && rack->GetJob() != 0) {
            const int target_display_index = rack_target_display_index(machine_obj, rack->GetJob(), rack->GetReadingIdx());
            if (target_display_index == display_idx) {
                if (rack->GetJob() == 1 || rack->GetJob() == 3) op_state = SV::hotend_op_state::taking;
                else if (rack->GetJob() == 2) op_state = SV::hotend_op_state::placing;
            }
        }

        hotend[SK::hotend::op_state] = has_tool ? op_state : std::string(SV::hotend_op_state::idle);
        hotend[SK::hotend::tool_kind] = has_tool ? tool_kind_for_web(tool_defs) : SV::tool_kind::empty;
        hotend[SK::hotend::nozzle_label] = is_nozzle_tool ? wx_to_utf8(rack_nozzle_opt->GetNozzleDiameterStr()) : tool_label_for_web(tool_defs);
        hotend[SK::hotend::nozzle_subtext] = is_nozzle_tool ? nozzle_flow_short_name(rack_nozzle_opt->GetNozzleFlowType()) : "";
        hotend_detail_dialog[SK::hotend_detail_dialog::filament_name] = filament_name;
        hotend_detail_dialog[SK::hotend_detail_dialog::filament_color] = color;
        hotend_detail_dialog[SK::hotend_detail_dialog::show_filament] = is_nozzle_tool;
        hotend_detail_dialog[SK::hotend_detail_dialog::tool_image_name] = detail_tool_image_name(tool_defs);
        hotend_detail_dialog[SK::hotend_detail_dialog::tool_type] = is_nozzle_tool
            ? (boost::format("%1% %2% %3%")
                % wx_to_utf8(rack_nozzle_opt->GetNozzleDiameterStr())
                % wx_to_utf8(rack_nozzle_opt->GetNozzleFlowTypeStr())
                % wx_to_utf8(rack_nozzle_opt->GetNozzleTypeStr())).str()
            : tool_label_for_web(tool_defs);
        hotend_detail_dialog[SK::hotend_detail_dialog::nozzle_type] = is_nozzle_tool
            ? (boost::format("%1% %2% %3%")
                % wx_to_utf8(rack_nozzle_opt->GetNozzleDiameterStr())
                % wx_to_utf8(rack_nozzle_opt->GetNozzleFlowTypeStr())
                % wx_to_utf8(rack_nozzle_opt->GetNozzleTypeStr())).str()
            : tool_label_for_web(tool_defs);
        hotend_detail_dialog[SK::hotend_detail_dialog::firmware_version] = firmware.has_value() && !firmware->sw_ver.empty() ? firmware->sw_ver : "--";
        hotend_detail_dialog[SK::hotend_detail_dialog::serial_number] = has_read_detail ? serial_number : "--";
        hotend_detail_dialog[SK::hotend_detail_dialog::used_time_hours] = has_read_detail
            ? nlohmann::json(std::max(0, rack_nozzle_opt->GetNozzlePrintTime()) / 60)
            : nlohmann::json(nullptr);
        hotend[SK::hotend::current_temp] = is_nozzle_tool ? nlohmann::json(rack_nozzle_opt->GetCurrentTemp()) : nlohmann::json(nullptr);
        hotend[SK::hotend::target_temp] = is_nozzle_tool ? rack_nozzle_opt->GetTargetTemp() : 0;
        hotend[SK::hotend::selected] = is_on_extruder;
    }

    const bool nozzle_has_filament = rack_nozzle_opt.has_value() && rack_nozzle_opt->HasFilamentInNozzle();
    bool is_external_spool = false;
    if (machine_obj && slot_id.has_value()) {
        if (auto fila_system = machine_obj->GetFilaSystem()) is_external_spool = fila_system->GetVirtualTray(slot_id->first).has_value();
    }

    nlohmann::json prefeed_indicator = {
        {SK::prefeed_indicator::visible, false},
        {SK::prefeed_indicator::colors, nlohmann::json::array()},
        {SK::prefeed_indicator::color_type, std::string("single")}
    };
    const bool external_spool_active = is_external_spool
        && line_state != SV::line_state::no_filament
        && line_state != SV::line_state::no_hotend;
    if (external_spool_active && rack_nozzle_opt.has_value() && machine_obj && machine_obj->GetFilaSystem()) {
        for (const auto& prefeed_slot : rack_nozzle_opt->GetPreloadBindAmsSlots()) {
            auto prefeed_tray_opt = machine_obj->get_tray(prefeed_slot.first, prefeed_slot.second);
            if (!prefeed_tray_opt.has_value() || !prefeed_tray_opt->is_exists) continue;

            std::vector<std::string> prefeed_colors = prefeed_tray_opt->cols;
            if (prefeed_colors.empty()) prefeed_colors.push_back(prefeed_tray_opt->color);
            for (auto& c : prefeed_colors) c = normalize_hex_for_web(c);

            prefeed_indicator = {
                {SK::prefeed_indicator::visible, true},
                {SK::prefeed_indicator::colors, prefeed_colors},
                {SK::prefeed_indicator::color_type, color_type_for_web(prefeed_tray_opt->ctype)}
            };
            break;
        }
    }

    const bool is_on_extruder = machine_obj && IsNozzleOnExtruder(machine_obj, addr_id);
    const auto hotend_tool_kind = hotend.value(SK::hotend::tool_kind, std::string(SV::tool_kind::empty));
    const auto hotend_op_state = hotend.value(SK::hotend::op_state, std::string(SV::hotend_op_state::idle));
    const bool hotend_busy = hotend_op_state == SV::hotend_op_state::reading ||
        hotend_op_state == SV::hotend_op_state::taking ||
        hotend_op_state == SV::hotend_op_state::placing ||
        hotend_op_state == SV::hotend_op_state::removing;
    const bool hotend_can_operate = has_hotend_slot && !hotend_busy;
    const nlohmann::json hotend_menu_actions = {
        {SK::hotend_menu_actions::can_read, hotend_can_operate && hotend_tool_kind == SV::tool_kind::nozzle},
        {SK::hotend_menu_actions::can_detail, has_hotend_slot},
        {SK::hotend_menu_actions::can_set_temperature, hotend_can_operate && hotend_tool_kind == SV::tool_kind::nozzle && hotend_op_state != SV::hotend_op_state::error},
        {SK::hotend_menu_actions::can_take_place, hotend_can_operate}
    };
    const nlohmann::json menu_actions = {
        {SK::menu_actions::can_edit, card_state != SV::card_state::empty},
        {SK::menu_actions::can_load, line_state == SV::line_state::no_filament && card_state != SV::card_state::empty},
        {SK::menu_actions::can_unload, line_state == SV::line_state::loaded},
        {SK::menu_actions::can_read, card_state != SV::card_state::reading && card_state != SV::card_state::empty},
        {SK::menu_actions::can_ams_operation, bind_ams_slot_count > 0}
    };

    const nlohmann::json card_section = {
        {SK::filament_card::state, card_state},
        {SK::filament_card::color, color},
        {SK::filament_card::color_type, color_type},
        {SK::filament_card::text, filament_name},
        {SK::filament_card::name, filament_name},
        {SK::filament_card::type, filament_type},
        {SK::filament_card::remain, filament_remain},
        {SK::filament_card::prefeed_indicator, prefeed_indicator},
        {SK::filament_card::status_text, ResolveStatusText(card_state)}
    };

    const nlohmann::json line_section = {
        {SK::filament_line::state, line_state},
        {SK::filament_line::has_filament, nozzle_has_filament},
        {SK::filament_line::filament_color, nozzle_has_filament ? color : std::string()}
    };

    const nlohmann::json nozzle_section = {
        {SK::filament_nozzle::state, nozzle_state},
        {SK::filament_nozzle::has_filament, nozzle_has_filament},
        {SK::filament_nozzle::filament_color, nozzle_has_filament ? color : std::string()},
        {SK::filament_nozzle::on_extruder, is_on_extruder},
        {SK::filament_nozzle::tool_kind, nozzle_tool_kind}
    };

    return {
        {SK::filament_slot::addr_id, addr_id},
        {SK::filament_slot::display_index, display_idx},
        {SK::filament_slot::ams_id, ams_id},
        {SK::filament_slot::slot_id, tray_id},
        {SK::filament_slot::bind_ams, bind_ams},
        {SK::filament_slot::bind_ams_slot_count, bind_ams_slot_count},
        {SK::filament_slot::enabled, has_hotend_slot},
        {SK::filament_slot::is_on_extruder, is_on_extruder},
        {SK::filament_slot::is_external_spool, is_external_spool},
        {SK::filament_slot::is_top_tier, is_top_tier},
        {SK::filament_slot::menu_actions, menu_actions},
        {SK::filament_slot::hotend_menu_actions, hotend_menu_actions},
        {SK::filament_slot::ams_operation, {
            {SK::ams_operation::available, bind_ams_slot_count > 0},
            {SK::ams_ext_operation_slot::units, ams_operation_units}
        }},
        {SK::filament_slot::filament_card, card_section},
        {SK::filament_slot::filament_line, line_section},
        {SK::filament_slot::filament_nozzle, nozzle_section},
        {SK::filament_slot::hotend, hotend},
        {SK::filament_slot::hotend_detail_dialog, hotend_detail_dialog}
    };
}

bool DevicePageAmsFilamentHotendVM::IsNozzleOnExtruder(MachineObject* machine_obj, int addr_id) const
{
    if (!machine_obj || !machine_obj->GetExtderSystem()) return false;
    for (const auto& ext : machine_obj->GetExtderSystem()->GetExtruders()) {
        if (ext.HasNozzleInstalled()) {
            auto addrIdOpt = machine_obj->GetNozzleSystem()->GetExtNozzle(ext.GetExtId()).GetAddrId();
            if (addrIdOpt.has_value() && addrIdOpt.value() == addr_id) return true;
        }
    }
    return false;
}

std::optional<nlohmann::json> DevicePageAmsFilamentHotendVM::HandleHotendAction(const std::string& action,
                                                                                const nlohmann::json& payload,
                                                                                MachineObject* machine_obj)
{
    if (action == "hotend_detail") {
        const int display_index = payload.value("display_index", 0);
        if (display_index <= 0) {
            return MakeResponse(GetModule(), "action", action, 2, _u8L("Invalid hotend display_index"), BuildState());
        }
        const nlohmann::json action_payload = payload;
        wxGetApp().CallAfter([action_payload]() { open_hotend_detail_dialog(action_payload); });
    } else if (action == "read_all_hotends") {
        wxGetApp().CallAfter([]() {
            auto* dev_mgr = wxGetApp().getDeviceManager();
            MachineObject* mo = dev_mgr ? dev_mgr->get_selected_machine() : nullptr;
            if (!mo) return;
            if (auto rack = mo->GetNozzleSystem()->GetNozzleRack()) rack->CtrlRackReadAll(true);
        });
    } else if (action == "read_hotend") {
        const int display_index = payload.value("display_index", 0);
        if (display_index <= 0) {
            return MakeResponse(GetModule(), "action", action, 2, _u8L("Invalid hotend display_index"), BuildState());
        }
        wxGetApp().CallAfter([display_index]() {
            auto* dev_mgr = wxGetApp().getDeviceManager();
            MachineObject* mo = dev_mgr ? dev_mgr->get_selected_machine() : nullptr;
            if (!mo) return;
            if (auto rack = mo->GetNozzleSystem()->GetNozzleRack()) rack->CrtlRackReadNozzle(display_index - 1);
        });
    } else if (action == "take_hotend" || action == "place_hotend") {
        const int display_index = payload.value("display_index", 0);
        if (display_index <= 0) {
            return MakeResponse(GetModule(), "action", action, 2, _u8L("Invalid hotend display_index"), BuildState());
        }
        wxGetApp().CallAfter([display_index, action]() {
            auto* dev_mgr = wxGetApp().getDeviceManager();
            MachineObject* mo = dev_mgr ? dev_mgr->get_selected_machine() : nullptr;
            if (!mo) return;
            if (auto rack = mo->GetNozzleSystem()->GetNozzleRack()) {
                const int rack_nozzle_id = 0x10 + display_index - 1;
                if (action == "place_hotend") rack->CtrlNozzleExchange(0, rack_nozzle_id);
                else rack->CtrlNozzleExchange(rack_nozzle_id, 0);
            }
        });
    } else if (action == "stop_hotend_operation" || action == "retry_hotend_operation") {
        wxGetApp().CallAfter([action]() {
            auto* dev_mgr = wxGetApp().getDeviceManager();
            MachineObject* mo = dev_mgr ? dev_mgr->get_selected_machine() : nullptr;
            if (!mo || !mo->GetNozzleSystem()) return;
            auto rack = mo->GetNozzleSystem()->GetNozzleRack();
            if (!rack) return;
            const bool is_retry = action == "retry_hotend_operation";
            if (!is_retry) rack->CtrlStopHotendRackJob();
            else rack->CtrlRetryHotendRackJob();
        });
    } else if (action == "set_hotend_temperature") {
        const int addr_id = payload.value("addr_id", -1);
        const int target_temp = payload.value("target_temp", -1);
        if (addr_id < 0 || target_temp < 0 || target_temp > 350) {
            return MakeResponse(GetModule(), "action", action, 2, _u8L("Invalid hotend temperature payload"), BuildState());
        }
        auto nozzle_system = machine_obj->GetNozzleSystem();
        auto nozzle_rack = nozzle_system ? nozzle_system->GetNozzleRack() : nullptr;
        if (!nozzle_system || !nozzle_rack) {
            return MakeResponse(GetModule(), "action", action, 2, _u8L("No hotend rack"), BuildState());
        }
        const int rack_pos_id = rack_pos_id_from_addr_id(addr_id);
        const auto rack_nozzle = nozzle_system->GetNozzleByPosId(rack_pos_id);
        if (!rack_nozzle.IsExist() || rack_nozzle.IsAbnormal()) {
            return MakeResponse(GetModule(), "action", action, 2, _u8L("Hotend cannot set temperature"), BuildState());
        }
        nozzle_rack->SetNozzleTargetTemp(rack_pos_id, target_temp);
    } else {
        return std::nullopt;
    }

    return MakeResponse(GetModule(), "action", action, 0, "", BuildState());
}

}} // namespace Slic3r::GUI
