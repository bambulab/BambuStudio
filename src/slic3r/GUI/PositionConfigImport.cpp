#include "PositionConfigImport.hpp"
#include "PartPlate.hpp"

#include <boost/nowide/fstream.hpp>
#include <boost/format.hpp>
#include <nlohmann/json.hpp>

namespace Slic3r { namespace GUI {

using json = nlohmann::json;

static std::string arrange_object_identity(Model& model, ModelObject* mo)
{
    if (mo && !mo->instances.empty() && mo->instances.front() && !mo->instances.front()->uuid.empty())
        return mo->instances.front()->uuid;
    return std::to_string(mo ? model.get_object_backup_id(*mo) : -1);
}

PositionConfigImportResult import_position_config_from_file(
    Model&              model,
    PartPlateList&      plate_list,
    DynamicPrintConfig& config,
    const std::string&  json_path,
    const std::string&  expected_printer_model)
{
    PositionConfigImportResult result;

    json root;
    try {
        boost::nowide::ifstream ifs(json_path.c_str());
        if (!ifs.is_open()) {
            result.error   = PositionConfigImportError::FileOpenFailed;
            result.message = "Failed to open positions file: " + json_path;
            return result;
        }
        ifs >> root;
    } catch (const std::exception& e) {
        result.error   = PositionConfigImportError::ParseFailed;
        result.message = std::string("Failed to parse the positions file: ") + e.what();
        return result;
    }

    if (!root.contains("type") || root["type"].get<std::string>() != "bbs_arrange_config"
        || !root.contains("plates") || !root["plates"].is_array()) {
        result.error   = PositionConfigImportError::InvalidFormat;
        result.message = "This file is not a valid arrange positions file.";
        return result;
    }

    const std::string json_printer_model = root.value("printer_model", std::string());
    if (!json_printer_model.empty() && !expected_printer_model.empty()
        && json_printer_model != expected_printer_model) {
        result.error   = PositionConfigImportError::PrinterModelMismatch;
        result.message = (boost::format("Printer model mismatch: positions file has '%1%', but current printer model is '%2%'.")
            % json_printer_model % expected_printer_model).str();
        return result;
    }

    const int plate_count = root.value("plate_count", 1);

    while (plate_list.get_plate_count() < plate_count && plate_list.get_plate_count() < (int) PartPlateList::MAX_PLATES_COUNT)
        plate_list.create_plate(false);

    std::map<std::string, int> uuid_to_obj;
    for (size_t i = 0; i < model.objects.size(); ++i)
        if (model.objects[i])
            uuid_to_obj[arrange_object_identity(model, model.objects[i])] = (int) i;

    int applied = 0;
    int missing = 0;
    for (const auto& jplate : root["plates"]) {
        if (!jplate.contains("objects") || !jplate["objects"].is_array())
            continue;

        for (const auto& jobj : jplate["objects"]) {
            const std::string uuid = jobj.value("uuid", std::string());
            auto              it   = uuid_to_obj.find(uuid);
            if (uuid.empty() || it == uuid_to_obj.end() || !jobj.contains("instances")) {
                missing++;
                continue;
            }

            ModelObject* mo = model.objects[it->second];
            for (const auto& jinst : jobj["instances"]) {
                const int inst_idx = jinst.value("instance_index", -1);
                if (inst_idx < 0 || inst_idx >= (int) mo->instances.size() || !mo->instances[inst_idx]) {
                    missing++;
                    continue;
                }
                ModelInstance* mi = mo->instances[inst_idx];

                Vec3d offset(jinst.value("position_x", 0.0),
                             jinst.value("position_y", 0.0),
                             mi->get_offset().z());
                mi->set_offset(offset);

                Vec3d rotation = mi->get_rotation();
                rotation.z()   = jinst.value("rotation_z", 0.0);
                mi->set_rotation(rotation);

                mi->arrange_order = jinst.value("arrange_order", 0);
                if (jinst.contains("printable"))
                    mi->printable = jinst["printable"].get<bool>();

                mo->invalidate_bounding_box();
                applied++;
            }
        }
    }

    plate_list.rebuild_plates_after_arrangement(true, false, -1);
    plate_list.select_plate(0);

    ConfigOptionFloats* tower_x_opt = config.opt<ConfigOptionFloats>("wipe_tower_x", true);
    ConfigOptionFloats* tower_y_opt = config.opt<ConfigOptionFloats>("wipe_tower_y", true);
    const int           cur_plates  = plate_list.get_plate_count();
    if (tower_x_opt)
        tower_x_opt->values.resize(cur_plates, tower_x_opt->values.empty() ? 0.0 : tower_x_opt->values.front());
    if (tower_y_opt)
        tower_y_opt->values.resize(cur_plates, tower_y_opt->values.empty() ? 0.0 : tower_y_opt->values.front());

    bool has_enabled = false;
    bool enabled     = false;
    for (const auto& jplate : root["plates"]) {
        const int p_idx = jplate.value("plate_index", -1);
        if (p_idx < 0 || p_idx >= cur_plates || !jplate.contains("prime_tower") || !jplate["prime_tower"].is_object())
            continue;

        const json& src = jplate["prime_tower"];

        if (tower_x_opt && src.contains("position_x"))
            tower_x_opt->values[p_idx] = src["position_x"].get<double>();
        if (tower_y_opt && src.contains("position_y"))
            tower_y_opt->values[p_idx] = src["position_y"].get<double>();

        if (!has_enabled && src.contains("enabled")) {
            enabled     = src["enabled"].get<bool>();
            has_enabled = true;
        }
    }
    if (has_enabled)
        config.set_key_value("enable_prime_tower", new ConfigOptionBool(enabled));

    if (tower_x_opt && tower_y_opt) {
        model.wipe_tower.positions.clear();
        model.wipe_tower.positions.resize(tower_x_opt->values.size());
        for (size_t i = 0; i < tower_x_opt->values.size(); ++i)
            model.wipe_tower.positions[i] = Vec2d(tower_x_opt->get_at(i), tower_y_opt->get_at(i));
        if (config.has("wipe_tower_rotation_angle"))
            model.wipe_tower.rotation = config.opt_float("wipe_tower_rotation_angle");
    }

    plate_list.reload_all_objects(false, -1);

    result.applied = applied;
    result.missing = missing;
    return result;
}

}} // namespace Slic3r::GUI
