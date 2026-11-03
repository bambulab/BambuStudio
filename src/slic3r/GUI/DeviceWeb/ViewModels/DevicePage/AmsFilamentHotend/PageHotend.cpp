#include "ViewModel.hpp"
#include "ResolverDeviceState.hpp"
#include "ResolverSlotState.hpp"
#include "ActionHandlerHotend.hpp"
#include "BuilderHotendRackProgressStatus.hpp"

#include "slic3r/GUI/DeviceCore/DevConfigUtil.h"
#include "slic3r/GUI/DeviceCore/DevDefs.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/DeviceCore/DevNozzleRack.h"
#include "slic3r/GUI/DeviceCore/DevNozzleSystem.h"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/DeviceWeb/DeviceWebHost.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/fila_manager/wgtFilaManagerSync.h"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/wxExtensions.hpp"

#include <algorithm>
#include <boost/format.hpp>
#include <iomanip>
#include <sstream>
#include <vector>
#include <wx/sizer.h>

namespace Slic3r { namespace GUI {

namespace {

std::string normalize_hex_for_web(const std::string& raw)
{
    if (raw.empty()) return "#D9D9D9";
    std::string hex = raw;
    if (hex[0] != '#') hex = "#" + hex;
    if (hex.size() > 9) hex = hex.substr(0, 9);
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
    case NozzleFlowType::T_FLOW: return "TPU 285";
    default: return "";
    }
}

// Bare diameter for the Filament tab caption
std::string nozzle_diameter_short_str(float diameter)
{
    return (boost::format("%.1f") % diameter).str();
}

std::string format_nozzle_used_time(int print_time_seconds)
{
    const int safe_seconds = std::max(0, print_time_seconds);
    // Match device UX: show exact minutes below 1h instead of collapsing to "<1h".
    if (safe_seconds < 3600) return std::to_string(safe_seconds / 60) + " min";

    const double hours = safe_seconds / 3600.0;
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << hours << "h";
    return oss.str();
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

constexpr int kDefaultHotendMaxTemp = 350;

int hotend_max_temp(const MachineObject* machine_obj)
{
    if (machine_obj && machine_obj->nozzle_temp_range.size() >= 2 && machine_obj->nozzle_temp_range[1] > 0)
        return machine_obj->nozzle_temp_range[1];
    return kDefaultHotendMaxTemp;
}

class HotendDetailDialog : public DPIDialog
{
public:
    explicit HotendDetailDialog(wxWindow* parent)
        : DPIDialog(parent, wxID_ANY, _L("Hotend Details"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
    {
        SetClientSize(wxSize(FromDIP(520), FromDIP(354)));
        SetMinClientSize(wxSize(FromDIP(520), FromDIP(354)));
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
    host->SetMinSize(wxSize(dlg.FromDIP(520), dlg.FromDIP(354)));
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(host, 1, wxEXPAND);
    dlg.SetSizer(sizer);
    dlg.Layout();
    dlg.ShowModal();
}

// Filament load / unload needs a healthy, idle tool.
bool is_hotend_ready_for_filament(const std::string& op_state)
{
    namespace SV = AmsHotendSchema::values;
    return op_state != SV::hotend_op_state::reading &&
           op_state != SV::hotend_op_state::taking &&
           op_state != SV::hotend_op_state::placing &&
           op_state != SV::hotend_op_state::removing &&
           op_state != SV::hotend_op_state::error &&
           op_state != SV::hotend_op_state::lost_in_holder &&
           op_state != SV::hotend_op_state::unknown;
}

nlohmann::json build_hotend_menu_actions(const std::string& op_state,
                                         const std::string& tool_kind,
                                         bool has_hotend_slot,
                                         bool is_on_extruder)
{
    namespace SK = AmsHotendSchema::keys;
    namespace SV = AmsHotendSchema::values;

    const bool busy = op_state == SV::hotend_op_state::reading ||
                      op_state == SV::hotend_op_state::taking ||
                      op_state == SV::hotend_op_state::placing ||
                      op_state == SV::hotend_op_state::removing;
    const bool error = op_state == SV::hotend_op_state::error;
    const bool lost = op_state == SV::hotend_op_state::lost_in_holder;
    const bool is_nozzle = tool_kind == SV::tool_kind::nozzle;

    // Lost keeps temperature control only; a comm error blocks every action.
    const bool can_operate = has_hotend_slot && !busy && !error;
    const bool can_move = can_operate && !lost;

    const bool can_read = can_operate;
    const bool can_detail = has_hotend_slot && !error;
    const bool can_set_temp = can_operate && is_nozzle && !is_on_extruder;
    const bool can_take_place = can_move;

    std::string disabled_reason;
    if (error) {
        disabled_reason = _u8L("PrintTool communication error. Please check the PrintTool cable and try again.");
    } else if (lost) {
        disabled_reason = _u8L("PrintTool detached. Cannot load.");
    }

    return {
        {SK::hotend_menu_actions::can_read, can_read},
        {SK::hotend_menu_actions::can_detail, can_detail},
        {SK::hotend_menu_actions::can_set_temperature, can_set_temp},
        {SK::hotend_menu_actions::can_take_place, can_take_place},
        {SK::hotend_menu_actions::disabled_reason, disabled_reason}
    };
}

std::string nozzle_state_tooltip(const std::string& nozzle_state)
{
    namespace SV = AmsHotendSchema::values;
    if (nozzle_state == SV::nozzle_state::lost) return _u8L("PrintTool detached. Cannot load.");
    if (nozzle_state == SV::nozzle_state::error) return _u8L("PrintTool communication error. Cannot operate.");
    if (nozzle_state == SV::nozzle_state::feed_suspect) return _u8L("The current filament may not be fully loaded into the PrintTool");
    if (nozzle_state == SV::nozzle_state::unknown) return _u8L("Hotend type unknown");
    return std::string();
}

void show_nozzle_hint_dialog(int addr_id)
{
    namespace SV = AmsHotendSchema::values;
    auto* dev_mgr = wxGetApp().getDeviceManager();
    MachineObject* machine_obj = dev_mgr ? dev_mgr->get_selected_machine() : nullptr;
    if (!machine_obj || !machine_obj->GetNozzleSystem()) return;

    // Re-resolve from live data: the state the web side clicked on may be stale.
    // Card / line inputs are left empty so only nozzle-owned faults pick a dialog.
    const std::string nozzle_state = DevicePageFilamentSlotStateResolver::ResolveNozzleState(
        machine_obj, machine_obj->GetNozzleSystem()->GetNozzleOptByAddrId(addr_id), std::string());

    wxString message;
    std::string wiki_url;
    if (nozzle_state == SV::nozzle_state::error) {
        message = _L("PrintTool communication error. Please check the PrintTool cable and try again.");
        wiki_url = DevPrinterConfigUtil::get_nozzle_comm_error_wiki(machine_obj->printer_type);
    } else if (nozzle_state == SV::nozzle_state::feed_suspect) {
        message = _L("The current filament may not be fully loaded into the PrintTool, which may cause printing issues. Please pull the filament out of the PrintTool, then click \"Load\" to complete loading.");
        wiki_url = DevPrinterConfigUtil::get_nozzle_feed_suspect_wiki(machine_obj->printer_type);
    } else if (nozzle_state == SV::nozzle_state::unknown) {
        message = _L("Hotend type unknown. Please edit the hotend type on the printer and try again.");
    } else {
        // Lost only shows a hover tooltip on the web side; other states have nothing to report.
        return;
    }

    const bool with_wiki = !wiki_url.empty();
    MessageDialog dlg(wxGetApp().mainframe, message, _L("Tip"), wxOK);
    if (with_wiki) dlg.SetButtonLabel(wxID_OK, _L("View details"));
    if (dlg.ShowModal() == wxID_OK && with_wiki) wxLaunchDefaultBrowser(wxString::FromUTF8(wiki_url));
}

} // namespace

void DevicePageAmsFilamentHotendVM::BuildHotendRackStatus(MachineObject* machine_obj, nlohmann::json& hotend_page) const
{
    namespace SK = AmsHotendSchema::keys;

    auto nozzle_system = machine_obj ? machine_obj->GetNozzleSystem() : nullptr;
    auto nozzle_rack = nozzle_system ? nozzle_system->GetNozzleRack() : nullptr;
    hotend_page[SK::hotend_page::hotend_rack_status] = DevicePageHotendRackProgressStatusBuilder::Build(machine_obj);

    const bool rack_busy = hotend_page[SK::hotend_page::hotend_rack_status].value(SK::rack_status::active, false);
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

    auto fila_sys = machine_obj->GetFilaSystem();
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
    const std::string nozzle_tool_kind = has_tool ? tool_kind_for_web(rack_nozzle_opt->GetToolDefs()) : std::string(SV::tool_kind::empty);
    const bool current_filament_changing = IsCurrentFilamentChangingSlot(machine_obj, ams_id, tray_id);
    const bool filament_changing = IsFilamentChangeActive(machine_obj);
    const bool loaded_in_nozzle = DevicePageFilamentSlotStateResolver::IsTraySlotLoadedInNozzle(machine_obj, tray);
    const bool tray_reading = tray && tray->is_reading(machine_obj);
    const std::string card_state = DevicePageFilamentSlotStateResolver::ResolveCardState(machine_obj, tray, rack_nozzle_opt, filament_changing, current_filament_changing, tray_reading);
    const std::string line_state = DevicePageFilamentSlotStateResolver::ResolveLineState(card_state, has_hotend_slot, filament_changing, current_filament_changing, loaded_in_nozzle);
    const std::string nozzle_state = DevicePageFilamentSlotStateResolver::ResolveNozzleState(machine_obj, rack_nozzle_opt, line_state);

    nlohmann::json bind_ams = nlohmann::json::array();
    nlohmann::json ams_operation_units = nlohmann::json::array();
    if (machine_obj && machine_obj->GetNozzleSystem() && machine_obj->GetFilaSystem()) {
        const auto& rack_nozzle = machine_obj->GetNozzleSystem()->GetNozzleOptByAddrId(addr_id);
        if (rack_nozzle.has_value()) {
            for (int bind_ams_id : rack_nozzle->GetBindAms()) {
                const std::string bind_ams_id_str = std::to_string(bind_ams_id);
                const DevAms* bind_ams_unit = machine_obj->GetFilaSystem()->GetAmsById(bind_ams_id_str);
                const int slot_count = bind_ams_unit ? bind_ams_unit->GetSlotCount() : 0;
                bind_ams.push_back({
                    {SK::bind_ams::ams_id, bind_ams_id_str},
                    {SK::bind_ams::ams_type, bind_ams_unit ? std::to_string((int)bind_ams_unit->GetAmsType()) : ""},
                    {SK::bind_ams::slot_count, slot_count}
                                   });
                if (bind_ams_unit) ams_operation_units.push_back(BuildAmsUnitJson(machine_obj, bind_ams_unit));
            }
        }
    }
    // Number of AMS units bound to this nozzle, not their combined slot count.
    const int bind_ams_count = static_cast<int>(bind_ams.size());

    std::string slot_filament_name;
    std::string slot_filament_type;
    std::string slot_color = "#D9D9D9";
    std::string slot_color_type = "single";
    std::vector<std::string> slot_colors;
    int filament_remain = 100;
    if (tray && tray->is_exists) {
        if (tray->m_fila_type == "PLA-S") slot_filament_type = "Sup.PLA";
        else if (tray->m_fila_type == "PA-S") slot_filament_type = "Sup.PA";
        else if (tray->m_fila_type == "ABS-S") slot_filament_type = "Sup.ABS";
        else slot_filament_type = tray->m_fila_type;
        slot_filament_name = ResolveFilamentCardText(tray, slot_filament_type);
        slot_color = normalize_hex_for_web(tray->color);
        slot_color_type = color_type_for_web(tray->ctype);
        slot_colors = tray->cols;
        if (slot_colors.empty() && !tray->color.empty()) slot_colors.push_back(tray->color);
        for (auto& c : slot_colors) c = normalize_hex_for_web(c);
        filament_remain = DevicePageFilamentSlotStateResolver::ResolveDisplayRemain(tray);
    } else if (card_state == SV::card_state::awaiting_tpu_read) {
        slot_filament_name = DevicePageFilamentSlotStateResolver::ResolveStatusText(card_state);
    }

    nlohmann::json hotend = nlohmann::json::object();
    nlohmann::json hotend_detail_dialog;
    std::string filament_nozzle_label;
    std::string filament_nozzle_subtext;
    if (rack_nozzle_opt.has_value()) {
        const bool is_on_extruder = IsNozzleOnExtruder(machine_obj, addr_id);
        // Holder connection clears once the nozzle is mounted on the extruder.
        // Diameter and flow still live on that nozzle object, so a taken slot
        // can show "0.4 mm" / "Std" instead of an empty tool.
        const bool has_tool = has_hotend_slot && rack_nozzle_opt->IsToolConnected();
        const auto tool_defs = (has_tool || is_on_extruder)
            ? rack_nozzle_opt->GetToolDefs()
            : DevNozzleToolDefs();
        const bool is_nozzle_tool = tool_defs.tool_type == DevNozzleToolType::Nozzle;
        const bool has_read_detail = has_tool && !rack_nozzle_opt->IsUnknown();
        const std::string serial_number = has_read_detail ? wx_to_utf8(rack_nozzle_opt->GetSerialNumber()) : "";
        std::optional<DevFirmwareVersionInfo> firmware;
        if (has_read_detail && !serial_number.empty()) {
            if (auto rack = machine_obj->GetNozzleSystem()->GetNozzleRack()) firmware = rack->GetNozzleFirmwareInfo(addr_id);
        }

        const std::string op_state = DevicePageHotendSlotStateResolver::ResolveOpState(machine_obj, rack_nozzle_opt, line_state, display_idx);

        hotend[SK::hotend::op_state] = op_state;
        hotend[SK::hotend::temp_trend] = DevicePageHotendSlotStateResolver::ResolveTempTrend(rack_nozzle_opt);
        // tool_defs already falls back to the on-extruder tool, so a slot whose nozzle is
        // mounted (or in flight during a take/place) must not report an empty tool kind.
        hotend[SK::hotend::tool_kind] = (has_tool || is_on_extruder) ? tool_kind_for_web(tool_defs) : SV::tool_kind::empty;
        std::string hotend_tooltip;
        if (op_state == SV::hotend_op_state::error) {
            hotend_tooltip = _u8L("PrintTool communication error. Please check the PrintTool cable and try again.");
        } else if (op_state == SV::hotend_op_state::lost_in_holder) {
            hotend_tooltip = _u8L("PrintTool detached. Cannot load.");
        } else if (has_tool && tool_defs.tool_type == DevNozzleToolType::Cut) {
            hotend_tooltip = _u8L("Settings for CutTool and DrawTool are not supported yet.");
        }
        hotend[SK::hotend::tooltip] = hotend_tooltip;
        hotend[SK::hotend::nozzle_label] = is_nozzle_tool ? wx_to_utf8(rack_nozzle_opt->GetNozzleDiameterStr()) : tool_label_for_web(tool_defs);
        hotend[SK::hotend::nozzle_subtext] = is_nozzle_tool ? nozzle_flow_short_name(rack_nozzle_opt->GetNozzleFlowType()) : "";
        if (has_tool || is_on_extruder) {
            filament_nozzle_label = is_nozzle_tool ? nozzle_diameter_short_str(rack_nozzle_opt->GetNozzleDiameter())
                                                   : tool_label_for_web(tool_defs);
            filament_nozzle_subtext = is_nozzle_tool ? nozzle_flow_short_name(rack_nozzle_opt->GetNozzleFlowType()) : "";
        }
        hotend_detail_dialog[SK::hotend_detail_dialog::filament_name] = ResolveFilamentCardText(rack_nozzle_opt->GetFilamentId(), std::string());
        hotend_detail_dialog[SK::hotend_detail_dialog::filament_color] = normalize_hex_for_web(rack_nozzle_opt->GetFilamentColor());
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
            ? nlohmann::json(format_nozzle_used_time(rack_nozzle_opt->GetNozzlePrintTime()))
            : nlohmann::json(nullptr);
        const auto nozzle_switch_count = rack_nozzle_opt->GetNozzleSwitchCount();
        hotend_detail_dialog[SK::hotend_detail_dialog::switch_count] = (has_read_detail && nozzle_switch_count.has_value())
            ? nlohmann::json(*nozzle_switch_count)
            : nlohmann::json(nullptr);
        hotend[SK::hotend::current_temp] = is_nozzle_tool ? nlohmann::json(rack_nozzle_opt->GetCurrentTemp()) : nlohmann::json(nullptr);
        hotend[SK::hotend::target_temp] = is_nozzle_tool ? rack_nozzle_opt->GetTargetTemp() : 0;
        hotend[SK::hotend::max_temp] = hotend_max_temp(machine_obj);
        hotend[SK::hotend::selected] = is_on_extruder;
    }

    bool is_external_spool = false;
    if (machine_obj && slot_id.has_value()) {
        if (auto fila_system = machine_obj->GetFilaSystem()) is_external_spool = fila_system->GetVirtualTray(slot_id->first).has_value();
    }

    nlohmann::json prefeed_indicator = {
        {SK::prefeed_indicator::visible, false},
        {SK::prefeed_indicator::colors, nlohmann::json::array()},
        {SK::prefeed_indicator::color_type, std::string("single")},
        {SK::prefeed_indicator::prefeed_fila_type, std::string()}
    };
    const bool external_spool_active = is_external_spool
        && line_state != SV::line_state::no_filament
        && line_state != SV::line_state::no_hotend;
    if (external_spool_active && rack_nozzle_opt.has_value() && machine_obj && machine_obj->GetFilaSystem()) {
        for (const auto& prefeed_slot : rack_nozzle_opt->GetPreloadBindAmsSlots()) {
            auto prefeed_tray_opt = machine_obj->get_tray(prefeed_slot.first, prefeed_slot.second);
            if (!prefeed_tray_opt.has_value() || !prefeed_tray_opt->is_exists) continue;

            // Same unknown rule as ResolveCardState: empty setting_id or type.
            const bool prefeed_unknown =
                prefeed_tray_opt->setting_id.empty() || prefeed_tray_opt->m_fila_type.empty();
            std::vector<std::string> prefeed_colors = prefeed_tray_opt->cols;
            if (prefeed_colors.empty()) prefeed_colors.push_back(prefeed_tray_opt->color);
            for (auto& c : prefeed_colors) c = normalize_hex_for_web(c);

            prefeed_indicator = {
                {SK::prefeed_indicator::visible, true},
                {SK::prefeed_indicator::colors, prefeed_colors},
                {SK::prefeed_indicator::color_type, color_type_for_web(prefeed_tray_opt->ctype)},
                {SK::prefeed_indicator::prefeed_fila_type, prefeed_unknown ? std::string() : prefeed_tray_opt->m_fila_type}
            };
            break;
        }
    }

    const bool is_on_extruder = machine_obj && IsNozzleOnExtruder(machine_obj, addr_id);
    const auto hotend_tool_kind = hotend.value(SK::hotend::tool_kind, std::string(SV::tool_kind::empty));
    const auto hotend_op_state = hotend.value(SK::hotend::op_state, std::string(SV::hotend_op_state::none));
    const bool hotend_ready_for_filament = is_hotend_ready_for_filament(hotend_op_state);
    const nlohmann::json hotend_menu_actions = build_hotend_menu_actions(hotend_op_state, hotend_tool_kind, has_hotend_slot, is_on_extruder);

    bool can_edit_filament = card_state != SV::card_state::empty;
    if (filament_changing && slot_id.has_value() && fila_sys->IsFilamentChangingSlot(slot_id->first, slot_id->second)) {
        can_edit_filament = false;
    }

    const char* filament_read_label = (card_state == SV::card_state::unknown) ? SV::read_label::read : SV::read_label::reread;// Unknown filament type shows "Read"; known type shows "Reread".
    bool can_read_filament = card_state != SV::card_state::reading && card_state != SV::card_state::empty && machine_obj && !machine_obj->GetFilaSystem()->GetAmsList().empty();
    bool can_external_read_rfid_tpu = false;
    if (filament_changing && slot_id.has_value() && fila_sys->IsFilamentChangingSlot(slot_id->first, slot_id->second)) {
        can_read_filament = false;
    } else if (is_external_spool) {
        auto tray_opt = fila_sys->GetVirtualTray(slot_id->first);
        if (tray_opt && tray_opt->CanReadExternalRFID_TPU(machine_obj)) {
            can_read_filament = true;
            can_external_read_rfid_tpu = true;
        } else {
            can_read_filament = false;
        }
    }

    bool can_load_filament = !filament_changing && line_state == SV::line_state::no_filament &&
                             card_state != SV::card_state::empty && card_state != SV::card_state::awaiting_tpu_read &&
                             hotend_ready_for_filament;
    bool can_unload_filament = !filament_changing && line_state == SV::line_state::loaded && hotend_ready_for_filament;
    bool can_ams_operation = (bind_ams_count > 0 || is_external_spool || can_external_read_rfid_tpu) && (hotend_tool_kind == SV::tool_kind::nozzle) && machine_obj && !machine_obj->GetFilaSystem()->GetAmsList().empty();
    const nlohmann::json menu_actions = {
        {SK::menu_actions::can_edit, can_edit_filament},
        {SK::menu_actions::can_load, can_load_filament },
        {SK::menu_actions::can_unload, can_unload_filament},
        {SK::menu_actions::can_read, can_read_filament },
        {SK::menu_actions::read_label, filament_read_label},
        {SK::menu_actions::can_ams_operation, can_ams_operation }
    };

    bool new_filament_hint = false;
    if (machine_obj && !machine_obj->is_lan_mode_printer() && slot_id.has_value()) {
        if (auto* sync = wxGetApp().fila_manager_sync())
            new_filament_hint = sync->has_pending_badge(machine_obj->get_dev_id(), ams_id, tray_id);
    }

    const nlohmann::json card_section = {
        {SK::filament_card::state, card_state},
        {SK::filament_card::color, slot_color},
        {SK::filament_card::color_type, slot_color_type},
        {SK::filament_card::colors, slot_colors},
        {SK::filament_card::text, slot_filament_name},
        {SK::filament_card::name, slot_filament_name},
        {SK::filament_card::type, slot_filament_type},
        {SK::filament_card::remain, filament_remain},
        {SK::filament_card::prefeed_indicator, prefeed_indicator},
        {SK::filament_card::status_text, DevicePageFilamentSlotStateResolver::ResolveStatusText(card_state)},
        {SK::filament_card::new_filament_hint, new_filament_hint}
    };

    bool filament_line_has_filament = false;
    std::string filament_line_color;
    if (rack_nozzle_opt.has_value()) {
        auto slot_now_opt = rack_nozzle_opt->GetSlotNow();
        if (slot_now_opt.has_value() && rack_nozzle_opt->HasFilamentAtNozzleInlet()) {
            filament_line_color = normalize_hex_for_web(slot_now_opt->color);
            filament_line_has_filament = !filament_line_color.empty();
        }
    }

    const nlohmann::json line_section = {
        {SK::filament_line::state, line_state},
        {SK::filament_line::has_filament, filament_line_has_filament},
        {SK::filament_line::filament_color, filament_line_color}
    };

    // filament_in_nozzle differs from filament_in_slot_spool.
    std::string nozzle_filament_color;
    if (rack_nozzle_opt.has_value()) {
        nozzle_filament_color = normalize_hex_for_web(rack_nozzle_opt->GetFilamentColor());
    }

    const nlohmann::json nozzle_section = {
        {SK::filament_nozzle::state, nozzle_state},
        {SK::filament_nozzle::has_filament, !nozzle_filament_color.empty()},
        {SK::filament_nozzle::filament_color, nozzle_filament_color},
        {SK::filament_nozzle::on_extruder, is_on_extruder},
        {SK::filament_nozzle::tool_kind, nozzle_tool_kind},
        {SK::filament_nozzle::tooltip, nozzle_state_tooltip(nozzle_state)},
        {SK::filament_nozzle::nozzle_label, filament_nozzle_label},
        {SK::filament_nozzle::nozzle_subtext, filament_nozzle_subtext}
    };

    return {
        {SK::filament_slot::addr_id, addr_id},
        {SK::filament_slot::display_index, display_idx},
        {SK::filament_slot::ams_id, ams_id},
        {SK::filament_slot::slot_id, tray_id},
        {SK::filament_slot::bind_ams, bind_ams},
        {SK::filament_slot::bind_ams_count, bind_ams_count},
        {SK::filament_slot::enabled, has_hotend_slot},
        {SK::filament_slot::is_on_extruder, is_on_extruder},
        {SK::filament_slot::is_external_spool, is_external_spool},
        {SK::filament_slot::is_top_tier, is_top_tier},
        {SK::filament_slot::menu_actions, menu_actions},
        {SK::filament_slot::hotend_menu_actions, hotend_menu_actions},
        {SK::filament_slot::ams_operation, {
            {SK::ams_operation::available, bind_ams_count > 0},
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

std::optional<nlohmann::json> DevicePageHotendActionHandler::TryHandle(DevicePageAmsFilamentHotendVM& vm,
                                                                       const std::string& action,
                                                                       const nlohmann::json& payload,
                                                                       MachineObject* machine_obj)
{
    if (action == "hotend_detail") {
        const int display_index = payload.value("display_index", 0);
        if (display_index <= 0) {
            return vm.MakeResponse(vm.GetModule(), "action", action, 2, _u8L("Invalid hotend display_index"), vm.BuildState());
        }
        const nlohmann::json action_payload = payload;
        wxGetApp().CallAfter([action_payload]() { open_hotend_detail_dialog(action_payload); });
    } else if (action == "read_all_hotends") {
        if (DevicePageDeviceStateResolver::BlockIfBusy(machine_obj)) {
            return vm.MakeResponse(vm.GetModule(), "action", action, 3,
                _u8L("The printer is busy on other job"), vm.BuildState());
        }
        wxGetApp().CallAfter([]() {
            auto* dev_mgr = wxGetApp().getDeviceManager();
            MachineObject* mo = dev_mgr ? dev_mgr->get_selected_machine() : nullptr;
            if (!mo) return;
            if (auto rack = mo->GetNozzleSystem()->GetNozzleRack()) rack->CtrlRackReadAll(true);
        });
    } else if (action == "read_hotend") {
        const int display_index = payload.value("display_index", 0);
        if (display_index <= 0) {
            return vm.MakeResponse(vm.GetModule(), "action", action, 2, _u8L("Invalid hotend display_index"), vm.BuildState());
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
            return vm.MakeResponse(vm.GetModule(), "action", action, 2, _u8L("Invalid hotend display_index"), vm.BuildState());
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
        int target_temp = payload.value("target_temp", -1);
        if (addr_id < 0 || target_temp < 0) {
            return vm.MakeResponse(vm.GetModule(), "action", action, 2, _u8L("Invalid hotend temperature payload"), vm.BuildState());
        }
        auto nozzle_system = machine_obj->GetNozzleSystem();
        auto nozzle_rack = nozzle_system ? nozzle_system->GetNozzleRack() : nullptr;
        if (!nozzle_system || !nozzle_rack) {
            return vm.MakeResponse(vm.GetModule(), "action", action, 2, _u8L("No hotend rack"), vm.BuildState());
        }
        const int rack_pos_id = rack_pos_id_from_addr_id(addr_id);
        const auto rack_nozzle = nozzle_system->GetNozzleByPosId(rack_pos_id);
        if (!rack_nozzle.IsExist() || rack_nozzle.IsAbnormal()) {
            return vm.MakeResponse(vm.GetModule(), "action", action, 2, _u8L("Hotend cannot set temperature"), vm.BuildState());
        }
        target_temp = std::min(target_temp, hotend_max_temp(machine_obj));
        nozzle_rack->SetNozzleTargetTemp(rack_pos_id, target_temp);
    } else if (action == "nozzle_hint") {
        const int addr_id = payload.value("addr_id", -1);
        if (addr_id < 0) {
            return vm.MakeResponse(vm.GetModule(), "action", action, 2, _u8L("Invalid hotend addr_id"), vm.BuildState());
        }
        wxGetApp().CallAfter([addr_id]() { show_nozzle_hint_dialog(addr_id); });
    } else {
        return std::nullopt;
    }

    return vm.MakeResponse(vm.GetModule(), "action", action, 0, "", vm.BuildState());
}

}} // namespace Slic3r::GUI
