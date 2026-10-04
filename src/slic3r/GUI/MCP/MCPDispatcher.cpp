#include "MCPDispatcher.hpp"

#include <wx/app.h>
#include <wx/thread.h>
#include <boost/log/trivial.hpp>
#include <mutex>
#include <condition_variable>
#include <chrono>

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Tab.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PrintConfig.hpp"

namespace Slic3r {
namespace GUI {

// Helper to safely execute a lambda on wxWidgets main UI thread
static nlohmann::json run_on_gui(std::function<nlohmann::json()> func, unsigned int timeout_ms = 8000)
{
    if (wxIsMainThread()) {
        return func();
    }

    struct TaskContext {
        std::mutex mtx;
        std::condition_variable cv;
        bool finished{ false };
        std::exception_ptr ex;
        nlohmann::json result;
    };
    auto ctx = std::make_shared<TaskContext>();

    wxTheApp->CallAfter([ctx, func]() {
        try {
            ctx->result = func();
        } catch (...) {
            ctx->ex = std::current_exception();
        }
        std::unique_lock<std::mutex> lock(ctx->mtx);
        ctx->finished = true;
        ctx->cv.notify_one();
    });

    std::unique_lock<std::mutex> lock(ctx->mtx);
    if (!ctx->cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [ctx] { return ctx->finished; })) {
        throw std::runtime_error("Timed out waiting for Bambu Studio UI thread (" + std::to_string(timeout_ms) + " ms)");
    }

    if (ctx->ex) {
        std::rethrow_exception(ctx->ex);
    }

    return ctx->result;
}

nlohmann::json MCPDispatcher::make_response(const nlohmann::json& id, const nlohmann::json& result)
{
    nlohmann::json res;
    res["jsonrpc"] = "2.0";
    res["id"] = id;
    res["result"] = result;
    return res;
}

nlohmann::json MCPDispatcher::make_error(const nlohmann::json& id, int code, const std::string& message)
{
    nlohmann::json res;
    res["jsonrpc"] = "2.0";
    res["id"] = id;
    res["error"] = {
        {"code", code},
        {"message", message}
    };
    return res;
}

nlohmann::json MCPDispatcher::make_tool_result(const std::string& text, bool is_error)
{
    nlohmann::json res;
    res["content"] = nlohmann::json::array({
        {
            {"type", "text"},
            {"text", text}
        }
    });
    res["isError"] = is_error;
    return res;
}

nlohmann::json MCPDispatcher::dispatch(const nlohmann::json& request)
{
    if (!request.is_object()) {
        return make_error(nullptr, -32600, "Invalid Request: expected JSON object");
    }

    nlohmann::json id = request.value("id", nlohmann::json(nullptr));
    std::string method = request.value("method", "");
    nlohmann::json params = request.value("params", nlohmann::json::object());

    // Notifications (no id)
    if (id.is_null() && (method == "notifications/initialized" || method == "initialized")) {
        BOOST_LOG_TRIVIAL(info) << "[MCP] Client initialized successfully";
        return nlohmann::json();
    }

    if (method == "initialize") {
        return handle_initialize(params, id);
    } else if (method == "ping") {
        return make_response(id, nlohmann::json::object());
    } else if (method == "tools/list") {
        return handle_tools_list(id);
    } else if (method == "tools/call") {
        return handle_tools_call(params, id);
    } else {
        return make_error(id, -32601, "Method not found: " + method);
    }
}

nlohmann::json MCPDispatcher::handle_initialize(const nlohmann::json& /*params*/, const nlohmann::json& id)
{
    nlohmann::json result;
    result["protocolVersion"] = "2024-11-05";
    result["capabilities"] = {
        {"tools", nlohmann::json::object()}
    };
    result["serverInfo"] = {
        {"name", "bambu-studio-mcp"},
        {"version", "1.0.0"}
    };
    return make_response(id, result);
}

nlohmann::json MCPDispatcher::handle_tools_list(const nlohmann::json& id)
{
    nlohmann::json tools = nlohmann::json::array();

    // 1. get_profile_summary
    tools.push_back({
        {"name", "get_profile_summary"},
        {"description", "Returns a concise summary of the active Bambu Studio print setup, including printer model, active print preset, loaded filament presets/colors, infill, speeds, critical fit tolerance compensations (xy_contour_compensation, xy_hole_compensation, elefant_foot_compensation), and objects on the build plate."},
        {"inputSchema", {
            {"type", "object"},
            {"properties", nlohmann::json::object()}
        }}
    });

    // 2. get_slicing_params
    tools.push_back({
        {"name", "get_slicing_params"},
        {"description", "Retrieves specific slicing parameters by key list or category group (e.g. 'precision', 'quality', 'strength', 'speed', 'support', 'thermal') to avoid context flooding."},
        {"inputSchema", {
            {"type", "object"},
            {"properties", {
                {"category", {
                    {"type", "string"},
                    {"description", "Profile category: 'process' (default), 'filament', or 'printer'."},
                    {"default", "process"}
                }},
                {"keys", {
                    {"type", "array"},
                    {"items", {{"type", "string"}}},
                    {"description", "Specific parameter keys to retrieve (e.g. ['xy_contour_compensation', 'xy_hole_compensation'])."}
                }},
                {"group", {
                    {"type", "string"},
                    {"description", "Named parameter group: 'precision', 'quality', 'strength', 'speed', 'support', 'thermal'."}
                }}
            }}
        }}
    });

    // 3. set_slicing_params
    tools.push_back({
        {"name", "set_slicing_params"},
        {"description", "Modifies slicing parameters directly inside the active Bambu Studio project and presets in real time. Refreshes the GUI tabs and triggers background slicing update."},
        {"inputSchema", {
            {"type", "object"},
            {"properties", {
                {"params", {
                    {"type", "object"},
                    {"description", "Key-value dictionary of parameters to set (e.g. {'xy_contour_compensation': -0.10, 'xy_hole_compensation': 0.10, 'elefant_foot_compensation': 0.20})."}
                }}
            },
            {"required", nlohmann::json::array({"params"})}
        }}
    });

    // 4. get_plater_info
    tools.push_back({
        {"name", "get_plater_info"},
        {"description", "Returns information about the loaded 3D models on the build plate: object names, dimensions (X, Y, Z in mm), volumes, part names, and assigned filament extruders."},
        {"inputSchema", {
            {"type", "object"},
            {"properties", nlohmann::json::object()}
        }}
    });

    // 5. paint_part
    tools.push_back({
        {"name", "paint_part"},
        {"description", "Assigns an AMS filament slot / extruder to a specific 3D model or volume (1-indexed for AMS slot 1..N, or 0 for default). Updates 3D canvas and plater."},
        {"inputSchema", {
            {"type", "object"},
            {"properties", {
                {"object_index", {
                    {"type", "integer"},
                    {"description", "Index of the object on the plate (0-indexed)."}
                }},
                {"object_name", {
                    {"type", "string"},
                    {"description", "Alternative: name of the object to color."}
                }},
                {"volume_index", {
                    {"type", "integer"},
                    {"description", "Optional volume index within the object for multi-part models."}
                }},
                {"extruder_id", {
                    {"type", "integer"},
                    {"description", "Filament extruder slot (1 for Slot 1, 2 for Slot 2, etc.)."}
                }}
            },
            {"required", nlohmann::json::array({"extruder_id"})}
        }}
    });

    // 6. slice_project
    tools.push_back({
        {"name", "slice_project"},
        {"description", "Initiates slicing for the active build plate in the Bambu Studio GUI."},
        {"inputSchema", {
            {"type", "object"},
            {"properties", nlohmann::json::object()}
        }}
    });

    // 7. get_slice_status
    tools.push_back({
        {"name", "get_slice_status"},
        {"description", "Returns whether slicing is currently in progress in Bambu Studio and whether slice toolpaths are ready."},
        {"inputSchema", {
            {"type", "object"},
            {"properties", nlohmann::json::object()}
        }}
    });

    nlohmann::json result;
    result["tools"] = tools;
    return make_response(id, result);
}

nlohmann::json MCPDispatcher::handle_tools_call(const nlohmann::json& params, const nlohmann::json& id)
{
    std::string tool_name = params.value("name", "");
    nlohmann::json args = params.value("arguments", nlohmann::json::object());

    try {
        nlohmann::json tool_res;
        if (tool_name == "get_profile_summary") {
            tool_res = tool_get_profile_summary(args);
        } else if (tool_name == "get_slicing_params") {
            tool_res = tool_get_slicing_params(args);
        } else if (tool_name == "set_slicing_params") {
            tool_res = tool_set_slicing_params(args);
        } else if (tool_name == "get_plater_info") {
            tool_res = tool_get_plater_info(args);
        } else if (tool_name == "paint_part") {
            tool_res = tool_paint_part(args);
        } else if (tool_name == "slice_project") {
            tool_res = tool_slice_project(args);
        } else if (tool_name == "get_slice_status") {
            tool_res = tool_get_slice_status(args);
        } else {
            return make_error(id, -32601, "Unknown tool: " + tool_name);
        }

        std::string text_output = tool_res.dump(2);
        bool is_error = tool_res.contains("error");
        return make_response(id, make_tool_result(text_output, is_error));
    } catch (const std::exception& e) {
        return make_response(id, make_tool_result("Error executing tool " + tool_name + ": " + e.what(), true));
    }
}

nlohmann::json MCPDispatcher::tool_get_profile_summary(const nlohmann::json& /*args*/)
{
    return run_on_gui([]() -> nlohmann::json {
        nlohmann::json j;
        PresetBundle* bundle = wxGetApp().preset_bundle;
        if (!bundle) {
            j["error"] = "Preset bundle not initialized";
            return j;
        }

        // Printer preset
        const Preset& printer = bundle->printers.get_edited_preset();
        j["printer"] = {
            {"name", printer.name},
            {"model", printer.get_printer_type(bundle)},
            {"technology", printer.printer_technology() == ptFFF ? "FFF" : "SLA"}
        };

        // Print process preset
        const Preset& print = bundle->prints.get_edited_preset();
        j["process_preset"] = print.name;

        const DynamicPrintConfig& cfg = print.config;
        auto get_opt = [&cfg](const std::string& key) -> std::string {
            return cfg.has(key) ? cfg.opt_serialize(key) : "";
        };

        j["tolerances"] = {
            {"xy_contour_compensation", get_opt("xy_contour_compensation")},
            {"xy_hole_compensation", get_opt("xy_hole_compensation")},
            {"elefant_foot_compensation", get_opt("elefant_foot_compensation")},
            {"precise_outer_wall", get_opt("precise_outer_wall")},
            {"slice_closing_radius", get_opt("slice_closing_radius")}
        };

        j["quality"] = {
            {"layer_height", get_opt("layer_height")},
            {"initial_layer_print_height", get_opt("initial_layer_print_height")},
            {"wall_loops", get_opt("wall_loops")},
            {"seam_position", get_opt("seam_position")}
        };

        j["infill"] = {
            {"sparse_infill_density", get_opt("sparse_infill_density")},
            {"sparse_infill_pattern", get_opt("sparse_infill_pattern")}
        };

        j["speeds"] = {
            {"outer_wall_speed", get_opt("outer_wall_speed")},
            {"inner_wall_speed", get_opt("inner_wall_speed")},
            {"sparse_infill_speed", get_opt("sparse_infill_speed")},
            {"travel_speed", get_opt("travel_speed")}
        };

        // Active filament presets
        nlohmann::json fila_arr = nlohmann::json::array();
        for (size_t i = 0; i < bundle->filament_presets.size(); ++i) {
            fila_arr.push_back({
                {"slot", i + 1},
                {"preset_name", bundle->filament_presets[i]}
            });
        }
        j["filaments"] = fila_arr;

        // Plater summary
        Plater* plater = wxGetApp().plater();
        if (plater) {
            const Model& model = plater->model();
            nlohmann::json plater_info;
            plater_info["is_slicing"] = plater->is_background_process_slicing();
            plater_info["objects_count"] = model.objects.size();

            nlohmann::json obj_arr = nlohmann::json::array();
            for (size_t i = 0; i < model.objects.size(); ++i) {
                const ModelObject* obj = model.objects[i];
                if (!obj) continue;
                const BoundingBoxf3& bb = obj->raw_bounding_box();
                Vec3d sz = bb.size();
                obj_arr.push_back({
                    {"index", i},
                    {"name", obj->name},
                    {"volumes_count", obj->volumes.size()},
                    {"dimensions_mm", {{"x", sz.x()}, {"y", sz.y()}, {"z", sz.z()}}}
                });
            }
            plater_info["objects"] = obj_arr;
            j["plater"] = plater_info;
        }

        return j;
    });
}

nlohmann::json MCPDispatcher::tool_get_slicing_params(const nlohmann::json& args)
{
    return run_on_gui([&args]() -> nlohmann::json {
        PresetBundle* bundle = wxGetApp().preset_bundle;
        if (!bundle) return {{"error", "Preset bundle not available"}};

        std::string category = args.value("category", "process");
        std::string group = args.value("group", "");
        std::vector<std::string> keys;

        if (args.contains("keys") && args["keys"].is_array()) {
            for (const auto& k : args["keys"]) {
                if (k.is_string()) keys.push_back(k.get<std::string>());
            }
        }

        const DynamicPrintConfig* cfg = nullptr;
        if (category == "printer" || category == "machine") {
            cfg = &bundle->printers.get_edited_preset().config;
        } else if (category == "filament") {
            cfg = &bundle->filaments.get_edited_preset().config;
        } else {
            cfg = &bundle->prints.get_edited_preset().config;
        }

        if (keys.empty() && !group.empty()) {
            if (group == "precision") {
                keys = {"xy_contour_compensation", "xy_hole_compensation", "elefant_foot_compensation", "precise_outer_wall", "slice_closing_radius", "resolution"};
            } else if (group == "quality") {
                keys = {"layer_height", "initial_layer_print_height", "wall_loops", "seam_position", "ironing_type"};
            } else if (group == "strength") {
                keys = {"sparse_infill_density", "sparse_infill_pattern", "top_shell_layers", "bottom_shell_layers", "wall_loops"};
            } else if (group == "speed") {
                keys = {"outer_wall_speed", "inner_wall_speed", "sparse_infill_speed", "internal_solid_infill_speed", "top_surface_speed", "travel_speed"};
            } else if (group == "support") {
                keys = {"enable_support", "support_type", "support_style", "support_top_z_distance", "support_bottom_z_distance"};
            } else if (group == "thermal") {
                keys = {"nozzle_temperature", "nozzle_temperature_initial_layer", "bed_temperature", "bed_temperature_initial_layer"};
            }
        }

        if (keys.empty()) {
            return {
                {"error", "Please specify 'keys' (array of parameter names) or 'group' ('precision', 'quality', 'strength', 'speed', 'support', 'thermal') to avoid context flooding."}
            };
        }

        nlohmann::json res = nlohmann::json::object();
        for (const auto& k : keys) {
            if (cfg->has(k)) {
                res[k] = cfg->opt_serialize(k);
            } else {
                res[k] = nullptr;
            }
        }
        return res;
    });
}

nlohmann::json MCPDispatcher::tool_set_slicing_params(const nlohmann::json& args)
{
    return run_on_gui([&args]() -> nlohmann::json {
        PresetBundle* bundle = wxGetApp().preset_bundle;
        if (!bundle) return {{"error", "Preset bundle not available"}};

        if (!args.contains("params") || !args["params"].is_object()) {
            return {{"error", "Missing required 'params' object with key-value pairs"}};
        }

        Preset& print_preset = bundle->prints.get_edited_preset();
        DynamicPrintConfig& cfg = print_preset.config;

        nlohmann::json applied = nlohmann::json::object();
        nlohmann::json errors = nlohmann::json::object();

        for (auto it = args["params"].begin(); it != args["params"].end(); ++it) {
            std::string key = it.key();
            std::string val_str;

            if (it.value().is_string()) {
                val_str = it.value().get<std::string>();
            } else if (it.value().is_number_float()) {
                val_str = std::to_string(it.value().get<double>());
            } else if (it.value().is_number_integer()) {
                val_str = std::to_string(it.value().get<int64_t>());
            } else if (it.value().is_boolean()) {
                val_str = it.value().get<bool>() ? "1" : "0";
            } else {
                val_str = it.value().dump();
            }

            try {
                cfg.set_deserialize_strict(key, val_str);
                applied[key] = cfg.opt_serialize(key);
            } catch (const std::exception& e) {
                errors[key] = e.what();
            }
        }

        // Refresh UI Tab so controls immediately update
        Tab* print_tab = wxGetApp().get_tab(Preset::TYPE_PRINT);
        if (print_tab) {
            print_tab->update_dirty();
            print_tab->reload_config();
            print_tab->update();
        }

        // Schedule background processing
        Plater* plater = wxGetApp().plater();
        if (plater) {
            plater->schedule_background_process();
        }

        nlohmann::json res;
        res["success"] = errors.empty();
        res["applied_parameters"] = applied;
        if (!errors.empty()) {
            res["errors"] = errors;
        }
        return res;
    });
}

nlohmann::json MCPDispatcher::tool_get_plater_info(const nlohmann::json& /*args*/)
{
    return run_on_gui([]() -> nlohmann::json {
        Plater* plater = wxGetApp().plater();
        if (!plater) return {{"error", "Plater not initialized"}};

        const Model& model = plater->model();
        nlohmann::json res;
        res["is_slicing"] = plater->is_background_process_slicing();
        res["printer_technology"] = plater->printer_technology() == ptFFF ? "FFF" : "SLA";

        nlohmann::json obj_list = nlohmann::json::array();
        for (size_t obj_idx = 0; obj_idx < model.objects.size(); ++obj_idx) {
            const ModelObject* obj = model.objects[obj_idx];
            if (!obj) continue;

            const BoundingBoxf3& bb = obj->raw_bounding_box();
            Vec3d size = bb.size();

            nlohmann::json obj_json;
            obj_json["index"] = obj_idx;
            obj_json["id"] = obj->id().id;
            obj_json["name"] = obj->name;
            obj_json["input_file"] = obj->input_file;
            obj_json["printable"] = obj->printable;
            obj_json["dimensions_mm"] = {
                {"x", size.x()},
                {"y", size.y()},
                {"z", size.z()}
            };
            obj_json["instances_count"] = obj->instances.size();

            if (obj->config.has("extruder")) {
                obj_json["assigned_extruder"] = obj->config.opt_int("extruder");
            } else {
                obj_json["assigned_extruder"] = 0;
            }

            nlohmann::json vol_list = nlohmann::json::array();
            for (size_t v_idx = 0; v_idx < obj->volumes.size(); ++v_idx) {
                const ModelVolume* vol = obj->volumes[v_idx];
                if (!vol) continue;
                nlohmann::json v_json;
                v_json["volume_index"] = v_idx;
                v_json["name"] = vol->name;
                v_json["type"] = static_cast<int>(vol->type());
                v_json["facets_count"] = vol->mesh().facets_count();
                if (vol->config.has("extruder")) {
                    v_json["assigned_extruder"] = vol->config.opt_int("extruder");
                } else {
                    v_json["assigned_extruder"] = 0;
                }
                vol_list.push_back(v_json);
            }
            obj_json["volumes"] = vol_list;
            obj_list.push_back(obj_json);
        }
        res["objects"] = obj_list;
        return res;
    });
}

nlohmann::json MCPDispatcher::tool_paint_part(const nlohmann::json& args)
{
    return run_on_gui([&args]() -> nlohmann::json {
        Plater* plater = wxGetApp().plater();
        if (!plater) return {{"error", "Plater not initialized"}};

        Model& model = plater->model();
        if (model.objects.empty()) {
            return {{"error", "No objects loaded on plater"}};
        }

        int obj_idx = -1;
        if (args.contains("object_index") && args["object_index"].is_number_integer()) {
            obj_idx = args["object_index"].get<int>();
        } else if (args.contains("object_name") && args["object_name"].is_string()) {
            std::string name = args["object_name"].get<std::string>();
            for (size_t i = 0; i < model.objects.size(); ++i) {
                if (model.objects[i]->name == name) {
                    obj_idx = static_cast<int>(i);
                    break;
                }
            }
        }

        if (obj_idx < 0 || obj_idx >= static_cast<int>(model.objects.size())) {
            return {{"error", "Object index/name not found on plater"}};
        }

        int extruder_id = args.value("extruder_id", 1);
        int volume_index = args.value("volume_index", -1);

        ModelObject* obj = model.objects[obj_idx];
        if (volume_index >= 0 && volume_index < static_cast<int>(obj->volumes.size())) {
            ModelVolume* vol = obj->volumes[volume_index];
            vol->config.set_key_value("extruder", new ConfigOptionInt(extruder_id));
        } else {
            obj->config.set_key_value("extruder", new ConfigOptionInt(extruder_id));
        }

        plater->changed_object(obj_idx);

        nlohmann::json res;
        res["success"] = true;
        res["object_index"] = obj_idx;
        res["object_name"] = obj->name;
        res["extruder_id"] = extruder_id;
        if (volume_index >= 0) res["volume_index"] = volume_index;
        return res;
    });
}

nlohmann::json MCPDispatcher::tool_slice_project(const nlohmann::json& /*args*/)
{
    return run_on_gui([]() -> nlohmann::json {
        Plater* plater = wxGetApp().plater();
        if (!plater) return {{"error", "Plater not initialized"}};

        if (plater->model().objects.empty()) {
            return {{"error", "Cannot slice: no objects loaded on plater"}};
        }

        if (plater->is_background_process_slicing()) {
            return {{"status", "already_slicing"}, {"message", "A slicing process is already running"}};
        }

        plater->reslice();

        return {
            {"status", "started"},
            {"message", "Slicing triggered successfully in Bambu Studio GUI"}
        };
    });
}

nlohmann::json MCPDispatcher::tool_get_slice_status(const nlohmann::json& /*args*/)
{
    return run_on_gui([]() -> nlohmann::json {
        Plater* plater = wxGetApp().plater();
        if (!plater) return {{"error", "Plater not initialized"}};

        nlohmann::json res;
        res["is_slicing"] = plater->is_background_process_slicing();
        res["has_toolpaths"] = plater->has_toolpaths_to_export();
        return res;
    });
}

} // namespace GUI
} // namespace Slic3r
