#include <nlohmann/json.hpp>

#include "DevFilaBlackList.h"
#include "DevFilaSystem.h"
#include "DevManager.h"

#include "libslic3r/Utils.hpp"

#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/GUI_App.hpp"

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

// moved from tao.wang and zhimin.zeng
DevFilaBlacklist::CheckResult check_filaments(const DevFilaBlacklist::CheckFilamentInfo& check_info)
{
    DevFilaBlacklist::CheckResult result;

    std::string  tag_type = check_info.fila_type;
    std::string  tag_name = check_info.fila_name;
    std::string  tag_vendor = check_info.fila_vendor;
    std::string  tag_calib_mode = check_info.calib_mode;
    std::transform(tag_vendor.begin(), tag_vendor.end(), tag_vendor.begin(), ::tolower);
    std::transform(tag_type.begin(), tag_type.end(), tag_type.begin(), ::tolower);
    std::transform(tag_name.begin(), tag_name.end(), tag_name.begin(), ::tolower);
    std::transform(tag_calib_mode.begin(), tag_calib_mode.end(), tag_calib_mode.begin(), ::tolower);

    DevFilaBlacklist::load_filaments_blacklist_config();
    if (DevFilaBlacklist::filaments_blacklist.contains("blacklist"))
    {
        for (auto filament_item : DevFilaBlacklist::filaments_blacklist["blacklist"])
        {
            std::string action = filament_item.contains("action") ? filament_item["action"].get<std::string>() : "";
            std::string description = filament_item.contains("description") ? filament_item["description"].get<std::string>() : "";
            if (action.empty() || description.empty()) {
                assert(false);
                BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": ERROR: filaments_blacklist item missing action or description";
            }

            // blacklist items
            std::string vendor = filament_item.contains("vendor") ? filament_item["vendor"].get<std::string>() : "";
            std::string type = filament_item.contains("type") ? filament_item["type"].get<std::string>() : "";
            std::vector<std::string> types = filament_item.contains("types") ? filament_item["types"].get<std::vector<std::string>>() : std::vector<std::string>();
            std::string type_suffix = filament_item.contains("type_suffix") ? filament_item["type_suffix"].get<std::string>() : "";
            std::string name_suffix = filament_item.contains("name_suffix") ? filament_item["name_suffix"].get<std::string>() : "";
            std::string name = filament_item.contains("name") ? filament_item["name"].get<std::string>() : "";
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
            const std::vector<float> nozzle_diameters = filament_item.contains("nozzle_diameters") ? filament_item["nozzle_diameters"].get<std::vector<float>>() : std::vector<float>();
            if (!nozzle_diameters.empty() && check_info.nozzle_diameter.has_value() &&
                std::find(nozzle_diameters.begin(), nozzle_diameters.end(), check_info.nozzle_diameter.value()) == nozzle_diameters.end()) {
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

            // check filament used for print support
            if (used_for_print_support.has_value() && used_for_print_support != check_info.used_for_print_support) {
                continue;
            }

            // check filament used for print object
            if (used_for_print_object.has_value() && used_for_print_object != check_info.used_for_print_object) {
                continue;
            }

            // check loc
            if (!slot.empty()) {
                bool is_virtual_slot = devPrinterUtil::IsVirtualSlot(check_info.ams_id);
                bool check_virtual_slot = (slot == "ext");
                bool check_ams_slot = (slot == "ams");
                if (is_virtual_slot && !check_virtual_slot) {
                    continue;
                } else if (!is_virtual_slot && !check_ams_slot) {
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
            DevFilaBlacklist::CheckResultItem result_item;
            result_item.action = action;
            result_item.wiki_url = filament_item.contains("wiki") ? filament_item["wiki"].get<std::string>() : "";

            if (description == "When using %s on the right extruder, it can only be used as support material.") {
                if (!name_suffix.empty()) {
                    std::transform(name_suffix.begin(), name_suffix.end(), name_suffix.begin(), ::toupper);
                    result_item.info_msg = wxString::Format(_L(description), name_suffix);
                } else {
                    result_item.info_msg = wxString::Format(_L(description), check_info.fila_name);
                }
            } else if (description == "%s has a risk of nozzle clogging when using 0.4mm high-flow nozzles. Use with caution.") {
                result_item.info_msg = wxString::Format(_L(description), check_info.fila_name);
            } else if (description == "%s filaments are hard and brittle and could break in AMS, and there is also a risk of nozzle clogging when using 0.4mm high-flow nozzles. Use with caution.") {
                result_item.info_msg = wxString::Format(_L(description), check_info.fila_type);
            } else if (description == "%s has a risk of nozzle clogging when using 0.4, 0.6, 0.8mm high-flow nozzles. Use with caution.") {
                result_item.info_msg = wxString::Format(_L(description), check_info.fila_name);
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
            L("Dynamic flow calibration is not supported for TPU 85A filament.");

            // Warning in description
            L("How to feed TPU filament.");
            L("Using non-bambu filament may have printing quality issues.");
            L("Please cold pull before printing TPU to avoid clogging. You may use cold pull maintenance on the printer.");
            L("Damp PVA will become flexible and get stuck inside AMS,please take care to dry it before use.");
            L("Damp PVA is flexible and may get stuck in extruder. Dry it before use.");
            L("PPS-CF is brittle and could break in bended PTFE tube above Toolhead.");
            L("PPA-CF is brittle and could break in bended PTFE tube above Toolhead.");
            L("PLA Glow may wear the AMS first stage feeder. Use an external spool instead.");
            L("Default settings may affect print quality. Adjust as needed for best results.");
            L("%s has a risk of nozzle clogging when using 0.4mm high-flow nozzles. Use with caution.");
            L("%s filaments are hard and brittle and could break in AMS, and there is also a risk of nozzle clogging when using 0.4mm high-flow nozzles. Use with caution.");
            L("%s has a risk of nozzle clogging when using 0.4, 0.6, 0.8mm high-flow nozzles. Use with caution.");
        };
    }

    return result;
}


bool check_filaments_printable(const std::string &dev_id,
                               const std::string &tag_vendor,
                               const std::string &filament_name,
                               const std::string &tag_type,
                               const std::string &filament_id,
                               int                ams_id,
                               bool              &in_blacklist,
                               std::string       &ac,
                               wxString          &info)
{
    DeviceManager *dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) {
        return true;
    }

    MachineObject* obj = dev->get_my_machine(dev_id);
    if (obj == nullptr || !obj->is_multi_extruders()) {
        return true;
    }

    Preset *printer_preset = GUI::get_printer_preset(obj);
    if (!printer_preset)
        return true;

    ConfigOptionInts *physical_extruder_map_op = dynamic_cast<ConfigOptionInts *>(printer_preset->config.option("physical_extruder_map"));
    if (!physical_extruder_map_op)
        return true;
    std::vector<int> physical_extruder_maps = physical_extruder_map_op->values;
    int obj_extruder_id = obj->get_extruder_id_by_ams_id(std::to_string(ams_id));
    int extruder_idx = obj_extruder_id;
    for (int index = 0; index < physical_extruder_maps.size(); ++index) {
        if (physical_extruder_maps[index] == obj_extruder_id) {
            extruder_idx = index;
            break;
        }
    }

    PresetBundle *preset_bundle = GUI::wxGetApp().preset_bundle;
    std::optional<FilamentBaseInfo> filament_info = preset_bundle->get_filament_by_filament_id(filament_id, printer_preset->name);
    if (filament_info.has_value() && !(filament_info->filament_printable >> extruder_idx & 1)) {
        wxString extruder_name = extruder_idx == 0 ? _L("left") : _L("right");
        std::string fila_name     = filament_name.empty() ? tag_type : filament_name;
        ac                     = "prohibition";
        info                   = wxString::Format(_L("%s is not supported by %s extruder."), fila_name, extruder_name);
        in_blacklist           = true;
        return false;
    }

    return true;
};

DevFilaBlacklist::CheckResult DevFilaBlacklist::check_filaments_in_blacklist(const CheckFilamentInfo& info)
{
    CheckResult result;
    if (info.ams_id < 0 || info.slot_id < 0) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": ERROR: check_filaments_in_blacklist invalid ams_id or slot_id";
        return result;
    }

    bool in_blacklist = false;
    CheckResultItem blacklist_item;
    if (!check_filaments_printable(info.dev_id, info.fila_vendor, info.fila_name, info.fila_type,info.fila_id, info.ams_id, in_blacklist, blacklist_item.action, blacklist_item.info_msg)) {
        result.action_items[blacklist_item.action].push_back(blacklist_item);
        return result;
    }

    return check_filaments(info);
};

}