#include <nlohmann/json.hpp>

#include "DevFilaBlackList.h"
#include "DevFilaSystem.h"
#include "DevManager.h"
#include "DevConfigUtil.h"

#include "libslic3r/Utils.hpp"

#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "libslic3r/Log.hpp"

using namespace nlohmann;

namespace Slic3r {

json DevFilaBlacklist::filaments_blacklist = json::object();


bool DevFilaBlacklist::load_filaments_blacklist_config()
{
    if (!filaments_blacklist.empty())
    {
        return false;
    }

    filaments_blacklist = json::object();

    std::string config_file = Slic3r::resources_dir() + "/printers/filaments_blacklist.json";
    boost::nowide::ifstream json_file(config_file.c_str());

    try {
        if (json_file.is_open()) {
            json_file >> filaments_blacklist;
            return true;
        }
        else {
            BOOST_LOG_TRIVIAL(error) << "load filaments blacklist config failed";
        }
    }
    catch (const std::exception& e) {
#if !BBL_RELEASE_TO_PUBLIC
        BOOST_LOG_TRIVIAL(error) << "load filaments blacklist config failed e=" << e.what();
#else
        BOOST_LOG_TRIVIAL(error) << "load filaments blacklist config failed";
#endif
        return false;
    }
    return true;
}



static std::string _get_filament_name_from_ams(const std::string& dev_id, int ams_id, int slot_id)
{
    std::string name;
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) {
        return name;
    }

    MachineObject* obj = dev->get_my_machine(dev_id);
    if (obj == nullptr) {
        return name;
    }

    if (ams_id < 0 || slot_id < 0) {
        return name;
    }

    const auto tray = obj->get_tray(std::to_string(ams_id), std::to_string(slot_id));
    if (!tray.has_value()) {
        return name;
    }

    if (!tray->is_tray_info_ready()) {
        return name;
    }

    std::string filament_id = tray->setting_id;

    PresetBundle* preset_bundle = GUI::wxGetApp().preset_bundle;
    auto          option = preset_bundle->get_filament_by_filament_id(filament_id);
    name = option ? option->filament_name : "";
    return name;
}

// Resolve the AMS type(DevAmsType id) of the slot being checked.
// Caller-provided check_info.ams_type wins; otherwise look it up from the device by ams_id.
static std::optional<int> _resolve_ams_type(const DevFilaBlacklist::CheckFilamentInfo& info)
{
    if (info.ams_type.has_value()) {
        return info.ams_type;
    }

    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) {
        return std::nullopt;
    }

    MachineObject* obj = dev->get_my_machine(info.dev_id);
    if (obj == nullptr) {
        obj = dev->get_selected_machine();
    }
    if (obj == nullptr) {
        return std::nullopt;
    }

    DevAms* ams = obj->GetFilaSystem()->GetAmsById(std::to_string(info.ams_id));
    if (ams == nullptr) {
        return std::nullopt;
    }

    return (int) ams->GetAmsType();
}

static wxString _get_ams_warning_display_name(const DevFilaBlacklist::CheckFilamentInfo& info)
{
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) {
        return "AMS";
    }

    MachineObject* obj = dev->get_my_machine(info.dev_id);
    if (!obj) {
        obj = dev->get_selected_machine();
    }
    if (!obj || !obj->GetFilaSystem()) {
        return "AMS";
    }

    const DevAms* ams = obj->GetFilaSystem()->GetAmsById(std::to_string(info.ams_id));
    if (!ams) {
        return "AMS";
    }

    // Reuse the send-page slot naming (A1, B1, ...) and remove the current slot
    // ordinal so a warning identifies the AMS unit as AMS-A, AMS-B, and so on.
    int tray_index = obj->GetFilaSystem()->GetTrayIdByAmsSlotId(info.ams_id, info.slot_id);
    if (tray_index < 0) {
        tray_index = ams->GetTrayId(info.slot_id);
    }
    wxString slot_name = Slic3r::GUI::wxGetApp().transition_tridid(tray_index, std::nullopt, obj);
    const wxString slot_suffix = wxString::Format("%d", info.slot_id + 1);
    if (slot_name.EndsWith(slot_suffix)) {
        slot_name.RemoveLast(slot_suffix.size());
    }
    return slot_name.empty() ? ams->GetDisplayName() : wxString("AMS-") + slot_name;
}

// A blacklist item may carry one wiki for every machine, or a per-model map.
// Per-model entries win. Machines missing from the map keep an empty link.
static std::string _get_blacklist_wiki_url(const json& filament_item, const std::string& model_id)
{
    if (filament_item.contains("wiki_by_model") && filament_item["wiki_by_model"].is_object()) {
        const json& wiki_map = filament_item["wiki_by_model"];
        const auto it = wiki_map.find(model_id);
        if (it != wiki_map.end() && it->is_string()) {
            return it->get<std::string>();
        }
    }

    if (filament_item.contains("wiki") && filament_item["wiki"].is_string()) {
        return filament_item["wiki"].get<std::string>();
    }

    return "";
}

// hide_in lists scenes that must not show this item. Missing means show everywhere.
static std::vector<std::string> _get_blacklist_hide_in(const json& filament_item)
{
    std::vector<std::string> hide_in;
    if (!filament_item.contains("hide_in") || !filament_item["hide_in"].is_array()) {
        return hide_in;
    }

    for (const auto& scene : filament_item["hide_in"]) {
        if (scene.is_string()) {
            hide_in.push_back(scene.get<std::string>());
        }
    }
    return hide_in;
}

// moved from tao.wang and zhimin.zeng
void check_filaments(const DevFilaBlacklist::CheckFilamentInfo& check_info, DevFilaBlacklist::CheckResult &result)
{
    std::string  tag_type = check_info.fila_type;
    std::string  tag_name = check_info.fila_name;
    std::string  tag_vendor = check_info.fila_vendor;
    std::string  tag_calib_mode = check_info.calib_mode;
    std::transform(tag_vendor.begin(), tag_vendor.end(), tag_vendor.begin(), ::tolower);
    std::transform(tag_type.begin(), tag_type.end(), tag_type.begin(), ::tolower);
    std::transform(tag_name.begin(), tag_name.end(), tag_name.begin(), ::tolower);
    std::transform(tag_calib_mode.begin(), tag_calib_mode.end(), tag_calib_mode.begin(), ::tolower);

    const std::optional<int> tag_ams_type = _resolve_ams_type(check_info);// DevAmsType id of the slot's AMS

    DevFilaBlacklist::load_filaments_blacklist_config();
    if (DevFilaBlacklist::filaments_blacklist.contains("blacklist"))
    {
        int item_count = DevFilaBlacklist::filaments_blacklist["blacklist"].size();
        BOOST_LOG_TRIVIAL(info) << "filaments_blacklist item count=" << item_count;
        for (auto filament_item : DevFilaBlacklist::filaments_blacklist["blacklist"])
        {
            std::string action = filament_item.contains("action") ? filament_item["action"].get<std::string>() : "";
            std::string description = filament_item.contains("description") ? filament_item["description"].get<std::string>() : "";
            if (action.empty() || description.empty()) {
                assert(false);
                BOOST_LOG_TRIVIAL(error) << "ERROR: filaments_blacklist item missing action or description";
            }

            // blacklist items
            std::string vendor = filament_item.contains("vendor") ? filament_item["vendor"].get<std::string>() : "";
            std::string type = filament_item.contains("type") ? filament_item["type"].get<std::string>() : "";
            std::vector<std::string> types = filament_item.contains("types") ? filament_item["types"].get<std::vector<std::string>>() : std::vector<std::string>();
            std::string type_suffix = filament_item.contains("type_suffix") ? filament_item["type_suffix"].get<std::string>() : "";
            std::string name_suffix = filament_item.contains("name_suffix") ? filament_item["name_suffix"].get<std::string>() : "";
            std::string name = filament_item.contains("name") ? filament_item["name"].get<std::string>() : "";
            std::vector<std::string> filament_id_vec = filament_item.contains("filament_id_vec") ? filament_item["filament_id_vec"].get<std::vector<std::string>>() : std::vector<std::string>();
            std::string slot = filament_item.contains("slot") ? filament_item["slot"].get<std::string>() : "";
            std::optional<bool> used_for_print_support = filament_item.contains("used_for_print_support") ? filament_item["used_for_print_support"].get<bool>() : std::optional<bool>();
            std::optional<bool> used_for_print_object = filament_item.contains("used_for_print_object") ? filament_item["used_for_print_object"].get<bool>() : std::optional<bool>();
            std::vector<std::string> model_ids = filament_item.contains("model_id") ? filament_item["model_id"].get<std::vector<std::string>>() : std::vector<std::string>();
            std::vector<int> extruder_ids = filament_item.contains("extruder_id") ? filament_item["extruder_id"].get<std::vector<int>>() : std::vector<int>();
            std::vector<std::string> nozzle_flows = filament_item.contains("nozzle_flows") ? filament_item["nozzle_flows"].get<std::vector<std::string>>() : std::vector<std::string>();
            std::string calib_mode = filament_item.contains("calib_mode") ? filament_item["calib_mode"].get<std::string>() : "";

            // check model id
            if (!model_ids.empty() && std::find(model_ids.begin(), model_ids.end(), check_info.model_id) == model_ids.end()) { continue; }

            // check nozzle flows
            if (!nozzle_flows.empty() && std::find(nozzle_flows.begin(), nozzle_flows.end(), check_info.nozzle_flow.value_or("")) == nozzle_flows.end()) { continue; }

            // check nozzle diameter
            // A diameter scoped item cannot be judged when the slot's nozzle is unknown, so skip it
            // instead of letting it match every diameter.
            const std::vector<float> nozzle_diameters = filament_item.contains("nozzle_diameters") ? filament_item["nozzle_diameters"].get<std::vector<float>>() : std::vector<float>();
            if (!nozzle_diameters.empty() &&
                (!check_info.nozzle_diameter.has_value() ||
                 std::find(nozzle_diameters.begin(), nozzle_diameters.end(), check_info.nozzle_diameter.value()) == nozzle_diameters.end())) {
                continue;
            }

            // check ams type id (DevAmsType: 0 ext, 1 ams, 2 ams-lite, 3 n3f/ams2pro, 4 n3s/ams-ht, ...)
            const std::vector<int> ams_type_ids = filament_item.contains("ams_type_id") ? filament_item["ams_type_id"].get<std::vector<int>>() : std::vector<int>();
            if (!ams_type_ids.empty() && tag_ams_type.has_value() &&
                std::find(ams_type_ids.begin(), ams_type_ids.end(), tag_ams_type.value()) == ams_type_ids.end()) {
                continue;
            }

            // check vendor
            std::transform(vendor.begin(), vendor.end(), vendor.begin(), ::tolower);
            if (!vendor.empty())
            {
                if(!((vendor == "bambu lab"   && tag_vendor == "bambu lab") ||
                     (vendor == "third party" && tag_vendor != "bambu lab") ))
                {
                    continue;
                }
            }

            // check calib mode
            std::transform(calib_mode.begin(), calib_mode.end(), calib_mode.begin(), ::tolower);
            if (!calib_mode.empty() && calib_mode != tag_calib_mode) { continue;}

            // check type
            std::transform(type.begin(), type.end(), type.begin(), ::tolower);
            if (!type.empty() && (type != tag_type)) {
                continue;
            }

            if (!types.empty()) {
                auto it = std::find_if(types.begin(), types.end(), [&tag_type](auto ttype) {
                    std::transform(ttype.begin(), ttype.end(), ttype.begin(), ::tolower);
                    return ttype == tag_type;
                });

                if (it == types.end()) {
                    continue;// the type is not in types
                }
            }

            // check type suffix
            std::transform(type_suffix.begin(), type_suffix.end(), type_suffix.begin(), ::tolower);
            if (!type_suffix.empty())
            {
                if (tag_type.length() < type_suffix.length()) { continue; }
                if ((tag_type.substr(tag_type.length() - type_suffix.length()) != type_suffix)) { continue; }
            }

            // check name
            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
            if (!name.empty() && (name != tag_name)) { continue; }

            // check name suffix
            std::transform(name_suffix.begin(), name_suffix.end(), name_suffix.begin(), ::tolower);
            if (!name_suffix.empty()) {
                if (tag_name.length() < name_suffix.length()) { continue; }
                if ((tag_name.substr(tag_name.length() - name_suffix.length()) != name_suffix)) { continue; }
            }

            // check filament id: lets one item cover a group of filaments that share the same message
            if (!filament_id_vec.empty() && std::find(filament_id_vec.begin(), filament_id_vec.end(), check_info.fila_id) == filament_id_vec.end()) {
                continue;
            }

            // check filament used for print support
            if (used_for_print_support.has_value() && used_for_print_support != check_info.used_for_print_support) {
                continue;
            }

            // check filament used for print object
            if (used_for_print_object.has_value() && used_for_print_object != check_info.used_for_print_object) {
                continue;
            }

            // check filament switch
            if (filament_item.contains("has_filament_switch")) {
                bool has_filament_switch = DevJsonValParser::GetVal<bool>(filament_item, "has_filament_switch", false);
                if (check_info.has_filament_switch != has_filament_switch) {
                    continue;
                }
            }

            // check loc
            if (!slot.empty()) {
                bool is_ext_spool = false;
                DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
                if (dev) {
                    MachineObject* obj = dev->get_selected_machine();
                    if (obj) {
                        is_ext_spool = obj->GetFilaSystem()->IsVirtualTrayId(std::to_string(check_info.ams_id));
                    }
                }
                bool check_virtual_slot = (slot == "ext");
                bool check_ams_slot = (slot == "ams");
                if (is_ext_spool && !check_virtual_slot) {
                    continue;
                } else if (!is_ext_spool && !check_ams_slot) {
                    continue;
                }
            }

            // check extruder id
            if(!extruder_ids.empty() && check_info.extruder_id.has_value() && std::find(extruder_ids.begin(), extruder_ids.end(), check_info.extruder_id.value()) == extruder_ids.end()){
                continue;
            }

            // white items
            {
                // contains match
                std::set<std::string> white_names = filament_item.contains("white_names") ? filament_item["white_names"].get<std::set<std::string>>() : std::set<std::string>();
                if (!white_names.empty() && !tag_name.empty()) {
                    auto it = std::find_if(white_names.begin(), white_names.end(), [&tag_name](auto white_name) {
                        std::transform(white_name.begin(), white_name.end(), white_name.begin(), ::tolower);
                        return tag_name.find(white_name) != std::string::npos;
                    });
                    // the filament name is contains in the white names, skip this blacklist item
                    if (it != white_names.end()) {
                        continue;
                    }
                }
            }

            {
                // equal match
                std::set<std::string> white_fila_ids = filament_item.contains("white_fila_ids") ? filament_item["white_fila_ids"].get<std::set<std::string>>() : std::set<std::string>();
                if (!white_fila_ids.empty() && !check_info.fila_id.empty()) {
                    auto it = std::find_if(white_fila_ids.begin(), white_fila_ids.end(), [&tag_name, check_info](auto white_fila_id) {
                        return white_fila_id == check_info.fila_id;
                    });

                    if (it != white_fila_ids.end()) {
                        continue;
                    }
                }
            }

            // the item is matched
            std::string group = filament_item.contains("group") ? filament_item["group"].get<std::string>() : "";
            int priority = filament_item.contains("priority") ? filament_item["priority"].get<int>() : 0;
            if (!group.empty()) {
                bool skip_by_group = false;
                for (auto &action_pair : result.action_items) {
                    auto &items = action_pair.second;
                    for (auto it = items.begin(); it != items.end();) {
                        if (it->group != group) {
                            ++it;
                            continue;
                        }
                        if (it->priority >= priority) {
                            skip_by_group = true;
                            break;
                        }
                        it = items.erase(it);
                    }
                    if (skip_by_group) {
                        break;
                    }
                }
                if (skip_by_group) {
                    continue;
                }
            }

            DevFilaBlacklist::CheckResultItem result_item;
            result_item.action = action;
            result_item.wiki_url = _get_blacklist_wiki_url(filament_item, check_info.model_id);
            result_item.group = group;
            result_item.priority = priority;
            result_item.hide_in = _get_blacklist_hide_in(filament_item);

            if (description == "When using %s on the right extruder, it can only be used as support material.") {
                if (!name_suffix.empty()) {
                    std::transform(name_suffix.begin(), name_suffix.end(), name_suffix.begin(), ::toupper);
                    result_item.info_msg = wxString::Format(_L(description), name_suffix);
                } else {
                    result_item.info_msg = wxString::Format(_L(description), check_info.fila_name);
                }
            } else if (description == "%s does not support %s printing, as filament may break in the bent PTFE tube above the toolhead.") {
                result_item.info_msg = wxString::Format(_L(description), wxString::FromUTF8(check_info.fila_type), _get_ams_warning_display_name(check_info));
            } else if (description == "%s has a risk of nozzle clogging when using 0.4mm high-flow nozzles. Use with caution.") {
                result_item.info_msg = wxString::Format(_L(description), check_info.fila_name);
            } else if (description == "%s filaments are hard and brittle and could break in AMS, and there is also a risk of nozzle clogging when using 0.4mm high-flow nozzles. Use with caution.") {
                result_item.info_msg = wxString::Format(_L(description), check_info.fila_type);
            } else if (description == "%s has a risk of nozzle clogging when using 0.4, 0.6, 0.8mm high-flow nozzles. Use with caution.") {
                result_item.info_msg = wxString::Format(_L(description), check_info.fila_name);
            } else if (description == "Use %s in %s with caution, as it may break in the bent PTFE tube above the toolhead.") {
                result_item.info_msg = wxString::Format(_L(description), wxString::FromUTF8(check_info.fila_type), _get_ams_warning_display_name(check_info));
            } else if (description == "PLA Glow may wear the %s first stage feeder. Use an external spool instead.") {
                result_item.info_msg = wxString::Format(_L(description), _get_ams_warning_display_name(check_info));
            } else if (description == "%s filament may wear the %s feed inlet, has high feeding resistance. We recommend an external spool.") {
                const std::string& fila_desc = check_info.fila_name.empty() ? check_info.fila_type : check_info.fila_name;
                result_item.info_msg = wxString::Format(_L(description), wxString::FromUTF8(fila_desc), _get_ams_warning_display_name(check_info));
            } else if (description == "%s filament is brittle and has high feeding resistance. Use caution when loading it through %s.") {
                result_item.info_msg = wxString::Format(_L(description), wxString::FromUTF8(check_info.fila_type), _get_ams_warning_display_name(check_info));
            } else if (description == "PPA-CF filament does not support %s feeding. Use an external spool instead.") {
                result_item.info_msg = wxString::Format(_L(description), _get_ams_warning_display_name(check_info));
            } else if (description == "%s filament is brittle. Use %s feeding with caution.") {
                const std::string& fila_desc = check_info.fila_name.empty() ? check_info.fila_type : check_info.fila_name;
                result_item.info_msg = wxString::Format(_L(description), fila_desc, _get_ams_warning_display_name(check_info));
            } else if (description == "%s filament has a higher risk of clogging when printed with a 0.2mm hotend. Use another compatible hotend before printing.") {
                result_item.info_msg = wxString::Format(_L(description), check_info.fila_name);
            } else if (description == "%s may fail to load or unload due to the Filament Track Switch.") {
                result_item.info_msg = wxString::Format(_L(description), check_info.fila_name);
            } else if (const size_t first_pos = description.find("%s"); first_pos != std::string::npos) {
                // Generic case: the message takes the filament name and, with a second placeholder,
                // the nozzle diameter. Data driven messages need no dedicated branch here.
                const std::string& fila_desc = check_info.fila_name.empty() ? check_info.fila_type : check_info.fila_name;
                const wxString& fila_str = wxString::FromUTF8(fila_desc);
                if (description.find("%s", first_pos + 2) != std::string::npos) {
                    result_item.info_msg = wxString::Format(_L(description), fila_str,
                                                            wxString::Format("%gmm", check_info.nozzle_diameter.value_or(0.f)));
                } else {
                    result_item.info_msg = wxString::Format(_L(description), fila_str);
                }
            } else {
                result_item.info_msg = _L(description);
            }

            result.action_items[result_item.action].push_back(result_item);
            continue;

            // Error in description
            L("TPU is not supported by AMS.");
            L("AMS does not support 'Bambu Lab PET-CF'.");
            L("The current filament doesn't support the E3D high-flow nozzle and can't be used.");
            L("When using %s on the right extruder, it can only be used as support material.")
            L("The current filament doesn't support the TPU high-flow nozzle and can't be used.");
            L("Auto dynamic flow calibration is not supported for TPU filament.");
            L("Bambu TPU 85A is not supported for printing with 0.4 mm Standard or High Flow nozzles.");
            L("When using ABS/ASA/PETG HF on the right extruder, it can only be used as support material.");
            L("%s is not supported by the current %s hotend. Please confirm the filament type and try again.");
            L("%s is not supported by the current 2.85 mm TPU hotend. Please confirm the filament type and try again.");
            L("PPA-CF does not support AMS feeding. Use an external spool instead.");
            L("%s does not support %s printing, as filament may break in the bent PTFE tube above the toolhead.");

            // Warning in description
            L("Special attention should be paid to the feeding method of TPU.");
            L(" Please ");
            L("refer to the Wiki User Guide");
            L(" before continuing.");
            L("How to feed TPU filament on X2D.");
            L("Using non-bambu filament may have printing quality issues.");
            L("Please cold pull before printing TPU to avoid clogging. You may use cold pull maintenance on the printer.");
            L("Damp PVA will become flexible and get stuck inside AMS,please take care to dry it before use.");
            L("Damp PVA is flexible and may get stuck in extruder. Dry it before use.");
            L("PPS-CF is brittle and could break in bended PTFE tube above Toolhead.");
            L("PPA-CF is brittle and could break in bended PTFE tube above Toolhead.");
            L("%s filament is brittle. Use %s feeding with caution.");
            L("%s filament has a higher risk of clogging when printed with a 0.2mm hotend. Use another compatible hotend before printing.");
            L("CF/GF filaments are hard and brittle, It's easy to break or get stuck in AMS, please use with caution.");
            L("Default settings may affect print quality. Adjust as needed for best results.");
            L("Using non-bambu filament may have printing quality issues.");
            L("When using %s on the right extruder, it can only be used as support material.");
            L("%s has a risk of nozzle clogging when using 0.4mm high-flow nozzles. Use with caution.");
            L("%s filaments are hard and brittle and could break in AMS, and there is also a risk of nozzle clogging when using 0.4mm high-flow nozzles. Use with caution.");
            L("%s has a risk of nozzle clogging when using 0.4, 0.6, 0.8mm high-flow nozzles. Use with caution.");
            L("Use %s in %s with caution, as it may break in the bent PTFE tube above the toolhead.");
            L("PLA Glow may wear the %s first stage feeder. Use an external spool instead.");
            L("%s filament may wear the %s feed inlet, has high feeding resistance. We recommend an external spool.");
            L("%s filament is brittle and has high feeding resistance. Use caution when loading it through %s.");
            L("PPA-CF filament does not support %s feeding. Use an external spool instead.");
            L("%s may fail to load or unload due to the Filament Track Switch.");

            // Error in description, X2D filament compatibility
            L("Affected by the Filament Track Switch, %s cannot be used. Use an external spool instead.");
            L("%s is not supported with a standard %s nozzle.");
            L("%s is only supported for printing with the main extruder.");

            // Critical warning in description, X2D filament compatibility
            L("%s is used with the auxiliary extruder and carries a higher risk, which may result in print failure or equipment malfunction.");
            L("Affected by the Filament Track Switch, %s may break inside the filament tube.");

            // Warning in description, X2D filament compatibility
            L("%s is used with the auxiliary extruder and may impact print quality.");
            L("%s used with a high-flow %s nozzle may affect print quality.");
            L("%s used with a high-flow %s nozzle may cause a clog.");
            L("%s used with a high-flow %s nozzle may cause oozing.");
            L("%s used with a high-flow %s nozzle may lead to difficult extrusion.");
            L("%s used with a high-flow %s nozzle may result in poor quality in overhang and bridge areas of the model.");
            L("%s used with a standard %s nozzle may affect print quality.");
            L("%s used with a standard %s nozzle may cause oozing.");
            L("%s used with a standard %s nozzle may lead to difficult extrusion.");
            L("Affected by the Filament Track Switch, %s may fail to load or unload.");
            L("When using an auxiliary extruder, printing %s with a high-flow %s nozzle may affect model quality.");
            L("When using an auxiliary extruder, printing %s with a high-flow %s nozzle may cause a clog.");
            L("When using an auxiliary extruder, printing %s with a standard %s nozzle may affect model quality.");
        };
    }
}


void check_filaments_printable(const DevFilaBlacklist::CheckFilamentInfo &check_info, DevFilaBlacklist::CheckResult &result)
{
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) { return; }

    MachineObject *obj = dev->get_my_machine(check_info.dev_id);
    if (obj == nullptr || !obj->is_multi_extruders()) { return; }

    Preset *printer_preset = GUI::get_printer_preset(obj);
    if (!printer_preset) { return; }

    ConfigOptionInts *physical_extruder_map_op = dynamic_cast<ConfigOptionInts *>(printer_preset->config.option("physical_extruder_map"));
    if (!physical_extruder_map_op) { return; }

    if (!check_info.extruder_id.has_value()) { return; }

    int              extruder_idx           = -1;
    std::vector<int> physical_extruder_maps = physical_extruder_map_op->values;
    for (int index = 0; index < physical_extruder_maps.size(); ++index) {
        if (physical_extruder_maps[index] == check_info.extruder_id.value()) {
            extruder_idx = index;
            break;
        }
    }

    if (extruder_idx == -1) { return; }

    PresetBundle                   *preset_bundle = GUI::wxGetApp().preset_bundle;
    std::optional<FilamentBaseInfo> filament_info = preset_bundle->get_filament_by_filament_id(check_info.fila_id, printer_preset->name);
    if (filament_info.has_value()) {
        int bl_ext_id = (extruder_idx == 0) ? DEPUTY_EXTRUDER_ID : MAIN_EXTRUDER_ID;
        wxString     extruder_name = _L(DevPrinterConfigUtil::get_toolhead_display_name(obj->printer_type, bl_ext_id, ToolHeadComponent::Extruder, ToolHeadNameCase::LowerCase, true));
        std::string  fila_name = check_info.fila_name.empty() ? check_info.fila_type : check_info.fila_name;
        if (!(filament_info->filament_printable >> extruder_idx & 1)) {
            DevFilaBlacklist::CheckResultItem item;
            item.action = DevFilaBlacklist::ACTION_PROHIBITION;
            item.info_msg = wxString::Format(_L("%s is not supported by %s extruder."), fila_name, extruder_name);
            result.action_items[item.action].push_back(item);
            return;
        }

        // Check if the extruder is bowden
        bool is_bowden_extruder = false;
        auto exrtuder_type_opt = dynamic_cast<const ConfigOptionEnumsGeneric*>(printer_preset->config.option("extruder_type"));
        if (exrtuder_type_opt && exrtuder_type_opt->values.size() > extruder_idx) {
            ExtruderType extruder_type = (ExtruderType)exrtuder_type_opt->values[extruder_idx];
            is_bowden_extruder = (extruder_type == ExtruderType::etBowden);
        }

        // Compatibility levels: 0 = printable, 1 = error, 2 = critical warning, 3 = warning (4-7 reserved)
        int compatibale_val = filament_info->get_extruder_compatibility(extruder_idx);
        if (compatibale_val == 0) {
        } else if (compatibale_val == 1) {
            DevFilaBlacklist::CheckResultItem item;
            item.action = DevFilaBlacklist::ACTION_PROHIBITION;
            item.info_msg = wxString::Format(_L("%s is not supported by %s extruder."), fila_name, extruder_name);
            result.action_items[item.action].push_back(item);
        } else if (compatibale_val == 2) {
            DevFilaBlacklist::CheckResultItem item;
            item.action = DevFilaBlacklist::ACTION_WARNING;
            if (is_bowden_extruder) {
                item.info_msg = wxString::Format(_L("There may be critical print quality issues when printing '%s' with %s Bowden extruder. Use with caution!"), fila_name, extruder_name);
            } else {
                item.info_msg = wxString::Format(_L("There may be critical print quality issues when printing '%s' with %s extruder. Use with caution!"), fila_name, extruder_name);
            }

            result.action_items[item.action].push_back(item);
        } else if (compatibale_val == 3) {
            DevFilaBlacklist::CheckResultItem item;
            item.action = DevFilaBlacklist::ACTION_WARNING;
            if (is_bowden_extruder) {
                item.info_msg = wxString::Format(_L("There may be print quality issues when printing '%s' with %s Bowden extruder. Use with caution."), fila_name, extruder_name);
            } else {
                item.info_msg = wxString::Format(_L("There may be print quality issues when printing '%s' with %s extruder. Use with caution."), fila_name, extruder_name);
            }

            result.action_items[item.action].push_back(item);
        }
    }
};

DevFilaBlacklist::CheckResult DevFilaBlacklist::check_filaments_in_blacklist(const CheckFilamentInfo &info)
{
    CheckResult result;
    if (info.ams_id < 0 || info.slot_id < 0) {
        BOOST_LOG_TRIVIAL(error) << "ERROR: check_filaments_in_blacklist invalid ams_id or slot_id";
        return result;
    }

    // The preset and the blacklist file can both judge the same filament. Keep the stricter verdict,
    // and prefer the blacklist when both are equally strict, so that the config file can refine what
    // the preset says without having to edit every preset.
    CheckResult preset_result;
    check_filaments_printable(info, preset_result);

    CheckResult blacklist_result;
    check_filaments(info, blacklist_result);

    if (blacklist_result.empty()) {
        result = preset_result;
    } else if (preset_result.empty() || blacklist_result.GetMaxSeverity() >= preset_result.GetMaxSeverity()) {
        result = blacklist_result;
    } else {
        result = preset_result;
    }

    // if skip_ams_blacklist_check is true, change prohibition to critical warning so that the print
    // is no longer blocked while the message keeps the highest non-blocking severity
    if (GUI::wxGetApp().app_config->get("skip_ams_blacklist_check") == "true") {
        const auto& prohibit_items = result.get_prohibition_items();
        if (!prohibit_items.empty()) {
            for (auto prohibit_item : prohibit_items) {
                prohibit_item.action = ACTION_CRITICAL_WARNING;
                result.action_items[ACTION_CRITICAL_WARNING].push_back(prohibit_item);
            }

            result.action_items.erase(ACTION_PROHIBITION);
        }
    }

    return result;
};

}