#include "McpTools.hpp"
#include "McpJobs.hpp"
#include "McpGcodeExport.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "GUI_ObjectList.hpp"
#include "DeviceManager.hpp"
#include "DeviceCore/DevManager.h"
#include "DeviceCore/DevStorage.h"
#include "DeviceCore/DevFilaSystem.h"
#include "DeviceCore/DevMapping.h"
#include "GLCanvas3D.hpp"
#include "Tab.hpp"
#include "BackgroundSlicingProcess.hpp"
#include "slic3r/Utils/NetworkAgent.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/TriangleSelector.hpp"
#include "libslic3r/QuadricEdgeCollapse.hpp"
#include "libslic3r/Slicing.hpp"
#include "libslic3r/GCode/ThumbnailData.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"
#include "nlohmann/json.hpp"
#include <wx/dialog.h>
#include <wx/image.h>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <future>
#include <limits>
#include <map>
#include <mutex>
#include <regex>
#include <set>
#include <stdexcept>
#include <thread>
#include <Eigen/Geometry>

namespace Slic3r { namespace GUI {
namespace {
using Json = nlohmann::json;
using Handler = std::function<Json(Plater&, const Json&)>;

struct RpcError : std::runtime_error {
    std::string code;
    RpcError(std::string code, std::string message) : std::runtime_error(std::move(message)), code(std::move(code)) {}
};

bool noninteractive_import = false;
std::atomic<bool> mcp_publication_enabled{false};
std::atomic<std::uint64_t> mcp_domain_generation{0};
McpSliceJobs slice_jobs;
struct UiJob {
    std::uint64_t run = 0;
    std::string id, kind, state = "running", error;
    int plate_index = -1;
    bool completion_observed = false, cancellation_requested = false;
};
std::deque<UiJob> ui_jobs;
std::uint64_t next_ui_run = 0;
struct ExternalJob {
    std::string id, kind;
    int plate_index = -1;
    std::atomic<bool> cancel_requested{false};
    std::mutex mutex;
    std::string status = "running", error, message;
    int progress_percent = 0;
    Json result = nullptr;
    bool terminal_recorded = false;
    std::shared_future<void> done;
    std::thread worker;
};
std::deque<std::shared_ptr<ExternalJob>> external_jobs;
std::uint64_t next_external_run = 0;
struct SliceResult {
    std::string id, fingerprint, source, file_identity;
    int plate_index = -1;
    std::uint64_t run = 0;
    std::uintptr_t native_result = 0;
};
SliceResult slice_result;
std::map<int, SliceResult> completed_slices;

const std::string& session_id() {
    static const std::string value = boost::uuids::to_string(boost::uuids::random_generator()());
    return value;
}

std::string reference_id(unsigned long long id) {
    return session_id() + ":" + std::to_string(id);
}

struct ImportScope {
    const bool previous = noninteractive_import;
    ImportScope() { noninteractive_import = true; }
    ~ImportScope() { noninteractive_import = previous; }
};


void require(bool condition, const char* message) {
    if (!condition) throw RpcError("INVALID_PARAMS", message);
}

std::string text(const Json& params, const char* key) {
    require(params.contains(key) && params.at(key).is_string(), "Required string parameter missing");
    const auto value = params.at(key).get<std::string>();
    require(!value.empty() && value.size() <= 32768 && value.find('\0') == std::string::npos, "Invalid string parameter");
    return value;
}

int index(const Json& params, const char* key, std::size_t count) {
    require(params.contains(key) && params.at(key).is_number_integer(), "Required index parameter missing");
    auto value = params.at(key).get<long long>();
    require(value >= 0 && static_cast<unsigned long long>(value) < count, "Index is outside current state; list again");
    return static_cast<int>(value);
}

bool flag(const Json& params, const char* key, bool fallback = false) {
    if (!params.contains(key)) return fallback;
    require(params.at(key).is_boolean(), "Flag must be boolean");
    return params.at(key).get<bool>();
}

Vec3d vector3(const Json& value, bool positive = false) {
    require(value.is_array() && value.size() == 3, "Vector must have three numeric values");
    Vec3d result;
    for (int i = 0; i < 3; ++i) {
        require(value[i].is_number(), "Vector values must be numeric");
        result[i] = value[i].get<double>();
        require(std::isfinite(result[i]) && std::abs(result[i]) <= 1000000 && (!positive || result[i] > 0), "Invalid vector value");
    }
    return result;
}

int object_index(Plater& plater, const Json& params, const char* key = "objectId") {
    const auto id = text(params, key);
    const auto& objects = plater.model().objects;
    for (std::size_t i = 0; i < objects.size(); ++i)
        if (reference_id(objects[i]->id().id) == id) return static_cast<int>(i);
    throw RpcError("STALE_REFERENCE", "Object no longer exists; list the model again");
}

int instance_index(const ModelObject& object, const Json& params) {
    const auto id = text(params, "instanceId");
    for (std::size_t i = 0; i < object.instances.size(); ++i)
        if (reference_id(object.instances[i]->id().id) == id) return static_cast<int>(i);
    throw RpcError("STALE_REFERENCE", "Instance no longer exists; list the model again");
}

int volume_index(const ModelObject& object, const Json& params) {
    const auto id = text(params, "volumeId");
    for (std::size_t i = 0; i < object.volumes.size(); ++i)
        if (reference_id(object.volumes[i]->id().id) == id) return static_cast<int>(i);
    throw RpcError("STALE_REFERENCE", "Volume no longer exists; inspect the object again");
}

std::map<std::string, std::string> plate_observations;
std::deque<std::string> plate_observation_order;
unsigned long long plate_observation_counter = 0;

std::string plate_fingerprint(Plater& plater, bool include_snapshot = true) {
    Json state = {{"model", std::to_string(plater.model().id().id)}};
    if (include_snapshot) state["snapshot"] = plater.get_active_snapshot_time();
    state["plates"] = Json::array();
    for (auto* plate : plater.get_partplate_list().get_plate_list()) {
        const auto volume = plate->get_build_volume();
        state["plates"].push_back({reinterpret_cast<std::uintptr_t>(plate), plate->get_plate_name(),
            plate->is_locked(), static_cast<int>(plate->get_bed_type()), static_cast<int>(plate->get_real_print_seq()),
            volume.min.x(), volume.min.y(), volume.min.z(), volume.max.x(), volume.max.y(), volume.max.z()});
    }
    state["objects"] = Json::array();
    for (const auto* object : plater.model().objects) state["objects"].push_back(std::to_string(object->id().id));
    return state.dump();
}

void check_plate_revision(Plater& plater, const Json& params) {
    const auto revision = text(params, "expectedPlateRevision");
    auto it = plate_observations.find(revision);
    if (it == plate_observations.end() || it->second != plate_fingerprint(plater))
        throw RpcError("STALE_REFERENCE", "Plate state changed; list plates and use the new revision");
}

Json vector_json(const Vec3d& value) { return Json::array({value.x(), value.y(), value.z()}); }

Json bounds_json(const BoundingBoxf3& bounds) {
    return {{"min", vector_json(bounds.min)}, {"max", vector_json(bounds.max)}, {"size", vector_json(bounds.size())}};
}

boost::filesystem::path input_path(const std::string& value) {
    require(!value.empty() && value.size() <= 32768 && value.find('\0') == std::string::npos, "Invalid input path");
    boost::filesystem::path path(value);
    require(path.is_absolute(), "File path must be absolute");
    require(boost::filesystem::is_regular_file(path), "Input file does not exist");
    return path;
}

Json model_state(Plater& plater) {
    Json objects = Json::array();
    auto& model = plater.model();
    for (std::size_t i = 0; i < model.objects.size(); ++i) {
        const auto& object = *model.objects[i];
        Json instances = Json::array(), volumes = Json::array();
        for (std::size_t j = 0; j < object.instances.size(); ++j) {
            const auto& instance = *object.instances[j];
            instances.push_back({{"index", j}, {"id", reference_id(instance.id().id)},
                {"position", vector_json(instance.get_offset())},
                {"rotation", vector_json(instance.get_rotation() * (180.0 / M_PI))},
                {"scale", vector_json(instance.get_scaling_factor())},
                {"plateIndex", plater.get_partplate_list().find_instance(static_cast<int>(i), static_cast<int>(j))}});
        }
        for (std::size_t j = 0; j < object.volumes.size(); ++j) {
            const auto& volume = *object.volumes[j];
            volumes.push_back({{"index", j}, {"id", reference_id(volume.id().id)}, {"name", volume.name}, {"type", static_cast<int>(volume.type())}});
        }
        objects.push_back({{"index", i}, {"id", reference_id(object.id().id)}, {"name", object.name},
            {"dimensions", vector_json(object.bounding_box().size())}, {"instances", instances}, {"volumes", volumes}});
    }
    return {{"objects", objects}, {"indexStability", "Re-list after deletion, reordering, undo or project replacement"}, {"units", "mm"}, {"rotationUnits", "degrees"}};
}

Json object_inspection(Plater& plater, const Json& params) {
    const auto i = object_index(plater, params);
    const auto& object = *plater.model().objects[i];
    Json volumes = Json::array(), instances = Json::array();
    for (std::size_t j = 0; j < object.volumes.size(); ++j) {
        const auto& volume = *object.volumes[j];
        volumes.push_back({{"index", j}, {"id", reference_id(volume.id().id)}, {"name", volume.name},
            {"type", static_cast<int>(volume.type())}, {"triangleCount", volume.mesh().facets_count()}});
    }
    for (std::size_t j = 0; j < object.instances.size(); ++j) {
        const auto& instance = *object.instances[j];
        instances.push_back({{"index", j}, {"id", reference_id(instance.id().id)},
            {"bounds", bounds_json(object.instance_bounding_box(j))}, {"mirror", vector_json(instance.get_mirror())},
            {"plateIndex", plater.get_partplate_list().find_instance(i, j)}});
    }
    return {{"objectId", reference_id(object.id().id)}, {"name", object.name},
        {"bounds", bounds_json(object.bounding_box())}, {"triangleCount", object.facets_count()},
        {"volumes", volumes}, {"instances", instances}, {"units", "mm"}};
}

Json printer_state(Plater& plater) {
    const auto& bundle = *wxGetApp().preset_bundle;
    const auto config = bundle.full_config_secure();
    Json machine = Json::object();
    for (const char* key : {"printer_model", "printer_variant", "nozzle_diameter", "printable_height", "bed_shape", "bed_exclude_area", "curr_bed_type"})
        if (config.has(key)) machine[key] = config.opt_serialize(key);
    Json filaments = Json::array();
    for (const auto& name : bundle.filament_presets) filaments.push_back(name);
    Json plates = Json::array();
    auto& list = plater.get_partplate_list();
    for (int i = 0; i < list.get_plate_count(); ++i) {
        auto* plate = list.get_plate(i);
        plates.push_back({{"index", i}, {"bedType", static_cast<int>(plate->get_bed_type())},
            {"printSequence", static_cast<int>(plate->get_real_print_seq())},
            {"bounds", bounds_json(plate->get_build_volume())}});
    }
    return {{"selected", {{"printer", bundle.printers.get_selected_preset_name()},
                {"process", bundle.prints.get_selected_preset_name()}, {"filaments", filaments}}},
        {"machine", machine}, {"plates", plates}, {"units", "mm"}, {"encoding", "native serialized strings"}};
}

Json preset_list(const Json& params) {
    const auto kind = text(params, "kind");
    auto& bundle = *wxGetApp().preset_bundle;
    const PresetCollection* collection = nullptr;
    if (kind == "printer") collection = &bundle.printers;
    else if (kind == "process") collection = &bundle.prints;
    else if (kind == "filament") collection = &bundle.filaments;
    else throw RpcError("INVALID_PARAMS", "kind must be printer, process or filament");
    Json profiles = Json::array();
    for (const auto& preset : collection->get_presets()) {
        if (!preset.is_visible) continue;
        profiles.push_back({{"name", preset.name}, {"compatible", preset.is_compatible},
            {"system", preset.is_system}, {"user", preset.is_user()},
            {"projectEmbedded", preset.is_project_embedded}, {"default", preset.is_default}});
    }
    Json slots = Json::array();
    if (kind == "filament") for (const auto& name : bundle.filament_presets) slots.push_back(name);
    return {{"kind", kind}, {"selected", collection->get_selected_preset_name()}, {"slots", slots}, {"profiles", profiles}};
}

Json select_preset(Plater& plater, const Json& params) {
    const auto kind = text(params, "kind");
    const auto name = text(params, "name");
    const auto expected = text(params, "expectedCurrent");
    auto& bundle = *wxGetApp().preset_bundle;
    if (plater.is_presets_dirty())
        throw RpcError("INTERACTION_REQUIRED", "Save or discard edited presets before selecting another profile");
    PresetCollection* collection = nullptr;
    Preset::Type type;
    if (kind == "printer") { collection = &bundle.printers; type = Preset::TYPE_PRINTER; }
    else if (kind == "process") { collection = &bundle.prints; type = Preset::TYPE_PRINT; }
    else if (kind == "filament") { collection = &bundle.filaments; type = Preset::TYPE_FILAMENT; }
    else throw RpcError("INVALID_PARAMS", "kind must be printer, process or filament");
    const auto* target = collection->find_preset(name, false);
    if (!target || !target->is_visible || !target->is_compatible)
        throw RpcError("INVALID_PARAMS", "Preset is missing, hidden or incompatible with the current printer");
    if (kind == "printer" && target->printer_technology() != ptFFF)
        throw RpcError("UNSUPPORTED", "Only FFF printer presets are supported");
    auto* tab = wxGetApp().get_tab(type);
    if (!tab) throw RpcError("BUSY", "Native preset tab is unavailable");
    if (kind == "filament") {
        const int slot = index(params, "slot", bundle.filament_presets.size());
        if (bundle.filament_presets[slot] != expected)
            throw RpcError("STALE_REFERENCE", "Filament slot changed; inspect current printer state");
        if (const auto* types = target->config.opt<ConfigOptionStrings>("filament_type")) {
            if (!types->values.empty() && types->values[0] == "PVA") {
                const auto config = bundle.full_config_secure();
                if (const auto* nozzle = config.opt<ConfigOptionFloatsNullable>("nozzle_diameter"))
                    if (std::find(nozzle->values.begin(), nozzle->values.end(), 0.2) != nozzle->values.end())
                        throw RpcError("INTERACTION_REQUIRED", "Native PVA and 0.2 mm nozzle selection requires an interactive warning");
            }
        }
        plater.take_snapshot("MCP select filament preset");
        const auto old = bundle.filament_presets[slot];
        bundle.set_filament_preset(slot, name);
        try {
            if (!plater.on_filament_change(slot) || (slot == 0 && !tab->select_preset(name)))
                throw RpcError("NATIVE_ERROR", "Native filament selection was rejected");
        } catch (...) {
            bundle.set_filament_preset(slot, old);
            throw;
        }
    } else {
        if (collection->get_selected_preset_name() != expected)
            throw RpcError("STALE_REFERENCE", "Selected preset changed; list presets again");
        if (kind == "printer" && !plater.model().objects.empty())
            throw RpcError("INTERACTION_REQUIRED", "Native printer switching repositions existing objects; use an empty project for semantic selection");
        if (name == expected) return printer_state(plater);
        plater.take_snapshot(kind == "printer" ? "MCP select printer preset" : "MCP select process preset");
        if (!tab->select_preset(name)) throw RpcError("NATIVE_ERROR", "Native preset selection was rejected");
    }
    plater.on_config_change(bundle.full_config_secure());
    plater.update_project_dirty_from_presets();
    bundle.export_selections(*wxGetApp().app_config);
    return printer_state(plater);
}

Json select_nozzle(Plater& plater, const Json& params) {
    const auto expected = text(params, "expectedPrinterPreset");
    auto& bundle = *wxGetApp().preset_bundle;
    if (bundle.printers.get_selected_preset_name() != expected)
        throw RpcError("STALE_REFERENCE", "Printer preset changed; inspect it again");
    require(params.contains("diameter") && params["diameter"].is_number(), "Provide nozzle diameter in millimeters");
    const double diameter = params["diameter"].get<double>();
    require(std::isfinite(diameter) && diameter >= 0.1 && diameter <= 2.0, "Nozzle diameter is outside native preset range");
    const auto& selected = bundle.printers.get_selected_preset();
    require(selected.config.has("printer_model"), "Selected printer preset has no model identity");
    const auto model = selected.config.opt_string("printer_model");
    std::vector<std::string> matching;
    for (const auto& preset : bundle.printers.get_presets()) {
        if (!preset.is_visible || !preset.is_compatible || preset.printer_technology() != ptFFF ||
            !preset.config.has("printer_model") || preset.config.opt_string("printer_model") != model) continue;
        const auto* nozzles = preset.config.opt<ConfigOptionFloatsNullable>("nozzle_diameter");
        if (nozzles && nozzles->values.size() == 1 && std::abs(nozzles->values[0] - diameter) < 1e-6)
            matching.push_back(preset.name);
    }
    if (matching.empty()) throw RpcError("INVALID_PARAMS", "No compatible printer preset matches that model and nozzle diameter");
    std::string chosen;
    if (params.contains("presetName")) {
        chosen = text(params, "presetName");
        require(std::find(matching.begin(), matching.end(), chosen) != matching.end(),
            "presetName does not match a compatible nozzle preset");
    } else {
        if (matching.size() != 1)
            throw RpcError("INVALID_PARAMS", "Several presets match this nozzle; provide presetName from preset.list");
        chosen = matching.front();
    }
    return select_preset(plater, {{"kind", "printer"}, {"name", chosen}, {"expectedCurrent", expected}});
}

Json effective_settings(Plater& plater, const Json& params) {
    auto& bundle = *wxGetApp().preset_bundle;
    auto full = bundle.full_config_secure();
    const DynamicPrintConfig* plate_config = nullptr;
    const DynamicPrintConfig* object_config = nullptr;
    const DynamicPrintConfig* volume_config = nullptr;
    Json scope = Json::object();
    if (params.contains("plateIndex")) {
        check_plate_revision(plater, params);
        const auto i = index(params, "plateIndex", plater.get_partplate_list().get_plate_count());
        plate_config = plater.get_partplate_list().get_plate(i)->config();
        scope["plateIndex"] = i;
    }
    if (params.contains("objectId")) {
        const auto i = object_index(plater, params);
        object_config = &plater.model().objects[i]->config.get();
        scope["objectId"] = reference_id(plater.model().objects[i]->id().id);
        if (params.contains("volumeId")) {
            const auto j = volume_index(*plater.model().objects[i], params);
            volume_config = &plater.model().objects[i]->volumes[j]->config.get();
            scope["volumeId"] = reference_id(plater.model().objects[i]->volumes[j]->id().id);
        }
    } else {
        require(!params.contains("volumeId"), "volumeId requires objectId");
    }
    std::set<std::string> keys;
    if (params.contains("keys")) {
        require(params["keys"].is_array() && !params["keys"].empty() && params["keys"].size() <= 100,
            "keys must contain 1 to 100 setting names");
        for (const auto& key : params["keys"]) {
            require(key.is_string(), "Setting names must be strings");
            const auto name = key.get<std::string>();
            require(!name.empty() && name.size() <= 160, "Invalid setting name");
            keys.insert(name);
        }
    } else {
        for (const auto& key : full.keys()) keys.insert(key);
        if (plate_config) for (const auto& key : plate_config->keys()) keys.insert(key);
        if (object_config) for (const auto& key : object_config->keys()) keys.insert(key);
        if (volume_config) for (const auto& key : volume_config->keys()) keys.insert(key);
    }
    Json values = Json::object();
    for (const auto& key : keys) {
        if (volume_config && volume_config->has(key)) values[key] = {{"value", volume_config->opt_serialize(key)}, {"source", "volume"}};
        else if (object_config && object_config->has(key)) values[key] = {{"value", object_config->opt_serialize(key)}, {"source", "object"}};
        else if (plate_config && plate_config->has(key)) values[key] = {{"value", plate_config->opt_serialize(key)}, {"source", "plate"}};
        else if (full.has(key)) values[key] = {{"value", full.opt_serialize(key)}, {"source", "global"}};
        else throw RpcError("INVALID_PARAMS", "Unknown setting: " + key);
    }
    return {{"scope", scope}, {"values", values}, {"encoding", "native serialized strings"}};
}

Json plate_state(Plater& plater) {
    auto& list = plater.get_partplate_list();
    Json plates = Json::array();
    for (int i = 0; i < list.get_plate_count(); ++i) {
        auto* plate = list.get_plate(i);
        plates.push_back({{"index", i}, {"name", plate->get_plate_name()}, {"locked", plate->is_locked()},
            {"selected", i == list.get_curr_plate_index()}, {"printable", plate->is_printable()}});
    }
    const auto revision = reference_id(++plate_observation_counter);
    plate_observations[revision] = plate_fingerprint(plater);
    plate_observation_order.push_back(revision);
    while (plate_observation_order.size() > 32) {
        plate_observations.erase(plate_observation_order.front());
        plate_observation_order.pop_front();
    }
    return {{"plates", plates}, {"selectedIndex", list.get_curr_plate_index()}, {"plateRevision", revision}};
}

bool modal_open() {
    for (auto* window : wxTopLevelWindows) {
        auto* dialog = dynamic_cast<wxDialog*>(window);
        if (dialog && dialog->IsModal()) return true;
    }
    return false;
}

Json app_state(Plater& plater) {
    return {{"sessionId", session_id()}, {"appVersion", SLIC3R_VERSION}, {"projectName", plater.get_project_name().ToUTF8().data()},
        {"projectPath", plater.get_project_filename().ToUTF8().data()}, {"dirty", plater.is_project_dirty()},
        {"presetsDirty", plater.is_presets_dirty()}, {"uiJobRunning", plater.is_any_job_running()},
        {"slicingRunning", plater.background_process().running()}, {"modalOpen", modal_open()},
        {"objectCount", plater.model().objects.size()}, {"plateCount", plater.get_partplate_list().get_plate_count()},
        {"selectedPlateIndex", plater.get_partplate_list().get_curr_plate_index()},
        {"platerView", plater.is_preview_shown() ? "preview" : "prepare"}};
}

UiJob* find_ui_job(const std::string& id) {
    for (auto& job : ui_jobs) if (job.id == id) return &job;
    return nullptr;
}

UiJob& begin_ui_job(Plater& plater, const char* kind) {
    while (ui_jobs.size() >= 64) ui_jobs.pop_front();
    UiJob job;
    job.run = ++next_ui_run;
    job.id = reference_id(job.run) + ":" + kind;
    job.kind = kind;
    job.plate_index = plater.get_partplate_list().get_curr_plate_index();
    ui_jobs.push_back(std::move(job));
    return ui_jobs.back();
}

Json ui_job_state(const UiJob& job) {
    return {{"jobId", job.id}, {"kind", job.kind}, {"plateIndex", job.plate_index},
        {"status", job.state}, {"completionObserved", job.completion_observed},
        {"cancelRequested", job.cancellation_requested}, {"error", job.error}};
}

std::shared_ptr<ExternalJob> find_external_job(const std::string& id) {
    for (const auto& job : external_jobs) if (job->id == id) return job;
    return {};
}

Json external_job_state(const std::shared_ptr<ExternalJob>& job) {
    const bool exited = job->done.valid() && job->done.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    std::lock_guard<std::mutex> lock(job->mutex);
    Json error = !exited || job->error.empty() ? Json(nullptr) : Json{{"code", "NATIVE_ERROR"}, {"message", job->error}};
    return {{"jobId", job->id}, {"kind", job->kind}, {"plateIndex", job->plate_index},
        {"status", exited ? job->status : job->cancel_requested ? "cancelling" : "running"},
        {"progressPercent", exited ? job->progress_percent : std::min(job->progress_percent, 99)}, {"message", job->message},
        {"cancelRequested", job->cancel_requested.load()}, {"completionObserved", exited},
        {"error", error}, {"result", exited ? job->result : Json(nullptr)}};
}

void external_progress(const std::shared_ptr<ExternalJob>& job, int percent, std::string message) {
    std::lock_guard<std::mutex> lock(job->mutex);
    if (job->terminal_recorded) return;
    job->progress_percent = std::clamp(percent, 0, 100);
    job->message = std::move(message);
}

void external_finish(const std::shared_ptr<ExternalJob>& job, std::string status, std::string error = {}, Json result = nullptr) {
    std::lock_guard<std::mutex> lock(job->mutex);
    if (job->terminal_recorded) return;
    job->terminal_recorded = true;
    job->status = std::move(status);
    job->error = std::move(error);
    job->result = std::move(result);
    if (job->status == "succeeded") job->progress_percent = 100;
}

std::shared_ptr<ExternalJob> start_external_job(std::string kind, int plate_index,
    std::function<void(const std::shared_ptr<ExternalJob>&)> runner) {
    if (external_jobs.size() >= 32) {
        auto& oldest = external_jobs.front();
        {
            std::lock_guard<std::mutex> lock(oldest->mutex);
            if (oldest->done.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                throw RpcError("BUSY", "External job capacity is full");
        }
        if (oldest->worker.joinable()) oldest->worker.join();
        external_jobs.pop_front();
    }
    auto job = std::make_shared<ExternalJob>();
    job->id = reference_id(++next_external_run) + ":external:" + kind;
    job->kind = std::move(kind);
    job->plate_index = plate_index;
    auto completed = std::make_shared<std::promise<void>>();
    job->done = completed->get_future().share();
    job->worker = std::thread([job, completed, runner = std::move(runner)] {
        try { runner(job); }
        catch (const std::exception& error) { external_finish(job, "failed", error.what()); }
        catch (...) { external_finish(job, "failed", "Native external operation failed"); }
        external_finish(job, "failed", "Native external operation returned without a terminal result");
        completed->set_value_at_thread_exit();
    });
    external_jobs.push_back(job);
    return job;
}

void refresh(Plater& plater) {
    plater.set_plater_dirty(true);
    plater.update(true, true);
    plater.object_list_changed();
}

void allow_replace(Plater& plater, const Json& params) {
    if ((plater.is_project_dirty() || plater.is_presets_dirty()) && !flag(params, "discardChanges"))
        throw RpcError("DIRTY_PROJECT", "Save the current project or explicitly set discardChanges=true");
}

template<class Config> Json config_state(const Config& config) {
    Json result = Json::object();
    for (const auto& key : config.keys()) result[key] = config.opt_serialize(key);
    return result;
}

Json matrix_state(const Transform3d& transform) {
    Json value = Json::array();
    for (int row = 0; row < 4; ++row) for (int column = 0; column < 4; ++column)
        value.push_back(transform.matrix()(row, column));
    return value;
}

std::string slice_fingerprint(Plater& plater, bool pending = false) {
    auto& bundle = *wxGetApp().preset_bundle;
    Json state = {{"plateState", plate_fingerprint(plater, false)}, {"model", model_state(plater)},
        {"config", config_state(bundle.full_config_secure())},
        {"projectConfig", config_state(bundle.project_config)},
        {"selectedProfiles", {{"printer", bundle.printers.get_selected_preset_name()},
            {"process", bundle.prints.get_selected_preset_name()}, {"filaments", bundle.filament_presets}}}};
    state["plateConfigs"] = Json::array();
    for (auto* plate : plater.get_partplate_list().get_plate_list()) {
        Json config = Json::object();
        const auto mode = plate->get_real_filament_map_mode(wxGetApp().preset_bundle->project_config);
        for (const auto& key : plate->config()->keys()) {
            if (((is_auto_filament_map_mode(mode) && (key == "filament_map" || key == "filament_volume_map")) ||
                            (mode != FilamentMapMode::fmmNozzleManual && key == "filament_nozzle_map"))) continue;
            config[key] = plate->config()->opt_serialize(key);
        }
        state["plateConfigs"].push_back(config);
    }
    state["objectConfigs"] = Json::array();
    for (const auto* object : plater.model().objects) {
        Json item = {{"config", config_state(object->config)}, {"layerProfile", object->layer_height_profile.timestamp()},
            {"printable", object->printable}, {"layerRanges", Json::array()}, {"volumes", Json::array()}, {"instances", Json::array()}};
        for (const auto* instance : object->instances)
            item["instances"].push_back({matrix_state(instance->get_transformation().get_matrix()), instance->printable});
        for (const auto& range : object->layer_config_ranges)
            item["layerRanges"].push_back({range.first.first, range.first.second, config_state(range.second)});
        for (const auto* volume : object->volumes)
            item["volumes"].push_back({{"id", volume->id().id}, {"type", static_cast<int>(volume->type())},
                {"config", config_state(volume->config)}, {"transform", matrix_state(volume->get_transformation().get_matrix())},
                {"mesh", reinterpret_cast<std::uintptr_t>(volume->mesh_ptr())}, {"triangleCount", volume->mesh().facets_count()},
                {"support", volume->supported_facets.timestamp()}, {"exterior", volume->exterior_facets.timestamp()},
                {"seam", volume->seam_facets.timestamp()}, {"material", volume->mmu_segmentation_facets.timestamp()},
                {"fuzzy", volume->fuzzy_skin_facets.timestamp()}});
        state["objectConfigs"].push_back(item);
    }
    return state.dump();
}

std::string slice_input_guard(Plater& plater) {
    return plate_fingerprint(plater) + ":selected:" + std::to_string(plater.get_partplate_list().get_curr_plate_index());
}

bool result_current(Plater& plater) {
    auto& background = plater.background_process();
    if (background.running() || background.mcp_orphans_running()) return false;
    if (auto* job = slice_jobs.active()) if (job->kind == "slice") return false;
    if (slice_result.id.empty() || slice_result.plate_index != plater.get_partplate_list().get_curr_plate_index() ||
        slice_result.fingerprint != slice_fingerprint(plater)) return false;
    auto* plate = plater.get_partplate_list().get_curr_plate();
    return !background.running() &&
        plate->is_slice_result_valid() && plate->is_slice_result_ready_for_export() &&
        plate->get_slice_result() && !plate->get_slice_result()->moves.empty() &&
        reinterpret_cast<std::uintptr_t>(plate->get_slice_result()) == slice_result.native_result &&
        plate->get_tmp_gcode_path() == slice_result.source &&
        !slice_result.file_identity.empty() && mcp_gcode_identity(slice_result.source) == slice_result.file_identity;
}

void reconcile_slices(Plater& plater) {
    const int selected = plater.get_partplate_list().get_curr_plate_index();
    if (slice_result.plate_index != selected) {
        const auto found = completed_slices.find(selected);
        slice_result = found == completed_slices.end() ? SliceResult{} : found->second;
    }
    if (auto* job = slice_jobs.active()) {
        if (job->kind == "slice" && job->input_guard != slice_input_guard(plater)) {
            slice_jobs.invalidate(job->run);
            job->cancellation_requested = true;
            plater.background_process().mcp_request_cancel(job->run);
        }
    }
    if (!slice_result.id.empty() && !result_current(plater)) {
        completed_slices.erase(selected);
        slice_result = {};
    }
}

Json job_state(const McpSliceJob& job) {
    Json error = nullptr, result = nullptr;
    if (!job.error_code.empty()) error = {{"code", job.error_code}, {"message", job.error_message}};
    if (job.state == "succeeded") {
        if (job.kind == "slice") result = {{"sliceResultId", job.result_id}, {"reused", job.reused}};
        else result = {{"sliceResultId", job.result_id}, {"path", job.path}, {"bytes", job.bytes}};
    }
    return {{"jobId", job.id}, {"kind", job.kind}, {"plateIndex", job.plate_index}, {"status", job.state},
        {"progressPercent", job.progress}, {"message", job.error_message}, {"cancelRequested", job.cancellation_requested},
        {"error", error}, {"result", result}, {"completionObserved", job.completion_observed}, {"workerPending", job.worker_pending}};
}

PartPlate& current_slice_plate(Plater& plater, const Json& params) {
    check_plate_revision(plater, params);
    const int plate = index(params, "plateIndex", plater.get_partplate_list().get_plate_count());
    require(plate == plater.get_partplate_list().get_curr_plate_index(), "Only the selected plate is supported; select it explicitly first");
    if (plater.background_process().is_export_scheduled() || plater.background_process().is_upload_scheduled())
        throw RpcError("BUSY", "A native export or upload is already scheduled");
    if (plater.printer_technology() != ptFFF) throw RpcError("UNSUPPORTED", "Only FFF slicing is implemented");
    if (plater.only_gcode_mode() || plater.is_gcode_3mf() || plater.model().objects.empty() || plater.model().calib_pa_pattern)
        throw RpcError("UNSUPPORTED", "Only ordinary geometry projects support tracked slicing");
    const auto config = wxGetApp().preset_bundle->full_config_secure();
    const auto* scripts = config.opt<ConfigOptionStrings>("post_process");
    if (scripts) for (const auto& script : scripts->values)
        if (script.find_first_not_of(" \t\r\n") != std::string::npos)
            throw RpcError("INTERACTION_REQUIRED", "Remove post-processing scripts before semantic slicing; native script consent is not accepted automatically");
    return *plater.get_partplate_list().get_curr_plate();
}

Json validate_slice(Plater& plater, const Json& params) {
    auto& plate = current_slice_plate(plater, params);
    Json errors = Json::array(), warnings = Json::array();
    if (!plate.has_printable_instances()) errors.push_back("Selected plate has no printable instances");
    if (plater.sidebar().has_broken_mixed_filament()) errors.push_back("Native filament assignment is incomplete");
    StringObjectException warning;
    const auto error = plater.mcp_validate_slice(&warning);
    if (!error.string.empty()) errors.push_back(error.string);
    if (!warning.string.empty()) warnings.push_back(warning.string);
    const auto* scripts = plater.background_process().fff_print()->full_print_config().opt<ConfigOptionStrings>("post_process");
    if (scripts) for (const auto& script : scripts->values)
        if (script.find_first_not_of(" \t\r\n") != std::string::npos)
            throw RpcError("INTERACTION_REQUIRED", "Native effective slice configuration contains post-processing scripts");
    reconcile_slices(plater);
    return {{"plateIndex", plater.get_partplate_list().get_curr_plate_index()}, {"valid", errors.empty()}, {"errors", errors}, {"warnings", warnings}};
}

Json slicing_state(Plater& plater) {
    reconcile_slices(plater);
    auto* plate = plater.get_partplate_list().get_curr_plate();
    Json job_id = nullptr, progress = nullptr, result_id = nullptr, statistics = nullptr;
    if (auto* job = slice_jobs.active()) { job_id = job->id; progress = job->progress; }
    if (result_current(plater)) {
        result_id = slice_result.id;
        progress = 100;
        const auto& stats = plate->fff_print()->print_statistics();
        statistics = {{"estimatedNormalTime", stats.estimated_normal_print_time}, {"estimatedSilentTime", stats.estimated_silent_print_time}, {"totalToolchanges", stats.total_toolchanges}};
    }
    const bool native_busy = plater.background_process().running() || plater.background_process().mcp_orphans_running() || slice_jobs.active();
    const bool native_valid = !native_busy && plate->is_slice_result_valid();
    const bool native_ready = native_valid && plate->is_slice_result_ready_for_export();
    const auto plates = plate_state(plater);
    return {{"sessionId", session_id()}, {"plateIndex", plater.get_partplate_list().get_curr_plate_index()},
        {"plateRevision", plates["plateRevision"]}, {"busy", native_busy},
        {"valid", native_valid}, {"readyForExport", native_ready},
        {"progressPercent", progress}, {"sliceResultId", result_id}, {"currentJobId", job_id}, {"statistics", statistics}};
}

Json start_slice(Plater& plater, const Json& params) {
    const auto validation = validate_slice(plater, params);
    if (!validation["valid"].get<bool>()) throw RpcError("VALIDATION_FAILED", validation["errors"].dump());
    if (result_current(plater)) {
        const auto current = slice_result;
        auto& job = slice_jobs.begin(session_id(), current.plate_index, slice_fingerprint(plater, true));
        job.reused = true;
        job.result_id = current.id;
        slice_jobs.finish(job.run, "succeeded", {}, {}, false);
        return job_state(job);
    }
    slice_result = {};
    completed_slices.erase(plater.get_partplate_list().get_curr_plate_index());
    auto& job = slice_jobs.begin(session_id(), plater.get_partplate_list().get_curr_plate_index(), slice_fingerprint(plater, true));
    job.input_guard = slice_input_guard(plater);
    try {
        if (!plater.mcp_start_slice(job.run)) {
            slice_jobs.finish(job.run, "failed", "NATIVE_ERROR", "Native slicing refused to start", false);
            throw RpcError("NATIVE_ERROR", "Native slicing refused to start; inspect current state");
        }
    } catch (...) {
        if (job.worker_pending && !plater.background_process().running())
            slice_jobs.finish(job.run, "failed", "NATIVE_ERROR", "Native slicing start failed", false);
        throw;
    }
    return job_state(job);
}

Json export_gcode(Plater& plater, const Json& params) {
    current_slice_plate(plater, params);
    reconcile_slices(plater);
    const auto token = text(params, "sliceResultId");
    if (!result_current(plater) || token != slice_result.id) throw RpcError("STALE_REFERENCE", "Slice result is no longer current; start a fresh tracked slice");
    const auto destination = boost::filesystem::path(text(params, "path"));
    require(destination.is_absolute() && destination.extension() == ".gcode", "Export requires an absolute .gcode path");
    require(boost::filesystem::is_directory(destination.parent_path()), "Export parent directory does not exist");
    require(!boost::filesystem::exists(destination) || boost::filesystem::is_regular_file(destination), "Destination must be a regular file");
    const bool overwrite = flag(params, "overwrite");
    require(overwrite || !boost::filesystem::exists(destination), "Destination exists; explicitly set overwrite=true");
    require(mcp_gcode_paths_distinct(slice_result.source, destination.string()), "Destination must not alias the native temporary G-code");
    const auto current = slice_result;
    const auto temporary = destination.parent_path() / (".bambu-mcp-" + boost::uuids::to_string(boost::uuids::random_generator()()) + ".tmp");
    auto& job = slice_jobs.begin(session_id(), current.plate_index, current.fingerprint);
    job.kind = "export_gcode";
    job.result_id = current.id;
    job.path = destination.string();
    try {
        job.bytes = mcp_copy_gcode(current.source, temporary.string(), destination.string(), overwrite,
            [&plater, token] { return result_current(plater) && slice_result.id == token; });
        slice_jobs.finish(job.run, "succeeded");
    } catch (const std::exception& error) {
        slice_jobs.finish(job.run, "failed", "NATIVE_ERROR", error.what());
    }
    return job_state(job);
}

const std::set<std::string>& writable_settings() {
    static const std::set<std::string> keys = {"layer_height", "initial_layer_print_height", "wall_loops", "top_shell_layers",
        "bottom_shell_layers", "sparse_infill_density", "sparse_infill_pattern", "enable_support", "support_type",
        "support_threshold_angle", "support_on_build_plate_only", "brim_type", "brim_width", "skirt_loops",
        "outer_wall_speed", "inner_wall_speed", "sparse_infill_speed", "travel_speed", "print_sequence", "seam_position"};
    return keys;
}

const std::set<std::string>& override_settings() {
    static const std::set<std::string> keys = {"layer_height", "initial_layer_print_height", "wall_loops",
        "top_shell_layers", "bottom_shell_layers", "sparse_infill_density", "sparse_infill_pattern",
        "enable_support", "support_type", "support_threshold_angle", "support_on_build_plate_only",
        "brim_type", "brim_width", "skirt_loops", "outer_wall_speed", "inner_wall_speed",
        "sparse_infill_speed", "travel_speed", "seam_position"};
    return keys;
}

void require_object_unlocked(Plater& plater, int object_index_value) {
    auto& plates = plater.get_partplate_list();
    const auto& object = *plater.model().objects[object_index_value];
    for (std::size_t j = 0; j < object.instances.size(); ++j) {
        const int plate = plates.find_instance(object_index_value, j);
        require(plate < 0 || !plates.get_plate(plate)->is_locked(), "Unlock every plate containing the object first");
    }
}

Json update_overrides(Plater& plater, const Json& params) {
    const auto scope = text(params, "scope");
    require(params.contains("values") && params["values"].is_object() && !params["values"].empty(), "Provide settings values");
    DynamicPrintConfig candidate;
    ModelConfig* model_config = nullptr;
    DynamicPrintConfig* plate_config = nullptr;
    int scoped_plate = -1, scoped_object = -1;
    if (scope == "plate") {
        check_plate_revision(plater, params);
        const int i = index(params, "plateIndex", plater.get_partplate_list().get_plate_count());
        scoped_plate = i;
        auto* plate = plater.get_partplate_list().get_plate(i);
        require(!plate->is_locked(), "Unlock the plate before updating its settings");
        plate_config = plate->config();
        candidate = *plate_config;
    } else if (scope == "object" || scope == "volume") {
        const int i = object_index(plater, params);
        scoped_object = i;
        require_object_unlocked(plater, i);
        auto& object = *plater.model().objects[i];
        model_config = scope == "object" ? &object.config : &object.volumes[volume_index(object, params)]->config;
        candidate = model_config->get();
    } else throw RpcError("INVALID_PARAMS", "scope must be plate, object or volume");
    auto full = wxGetApp().preset_bundle->full_config_secure();
    for (auto it = params["values"].begin(); it != params["values"].end(); ++it) {
        require(override_settings().count(it.key()) && full.has(it.key()) && it.value().is_string(),
            "Unsupported setting or non-string value");
        const auto value = it.value().get<std::string>();
        require(value.size() <= 1024, "Setting value too long");
        candidate.set_deserialize_strict(it.key(), value);
    }
    auto validate_effective = [](DynamicPrintConfig& config) {
        const auto errors = config.validate();
        if (!errors.empty()) throw RpcError("INVALID_PARAMS", "Native configuration validation failed: " + errors.begin()->first + ": " + errors.begin()->second);
    };
    if (scope == "plate") {
        auto plate_full = full;
        plate_full.apply(candidate, true);
        validate_effective(plate_full);
        for (std::size_t i = 0; i < plater.model().objects.size(); ++i) {
            const auto& object = *plater.model().objects[i];
            bool on_plate = false;
            for (std::size_t j = 0; j < object.instances.size(); ++j)
                on_plate |= plater.get_partplate_list().find_instance(i, j) == scoped_plate;
            if (!on_plate) continue;
            auto object_full = plate_full;
            object_full.apply(object.config.get(), true);
            validate_effective(object_full);
            for (const auto* volume : object.volumes) {
                auto volume_full = object_full;
                volume_full.apply(volume->config.get(), true);
                validate_effective(volume_full);
            }
        }
    } else {
        const auto& object = *plater.model().objects[scoped_object];
        std::set<int> affected_plates;
        for (std::size_t j = 0; j < object.instances.size(); ++j)
            affected_plates.insert(plater.get_partplate_list().find_instance(scoped_object, j));
        if (affected_plates.empty()) affected_plates.insert(-1);
        for (int plate_index : affected_plates) {
            auto effective = full;
            if (plate_index >= 0) effective.apply(*plater.get_partplate_list().get_plate(plate_index)->config(), true);
            effective.apply(scope == "object" ? candidate : object.config.get(), true);
            if (scope == "object") {
                validate_effective(effective);
                for (const auto* volume : object.volumes) {
                    auto volume_full = effective;
                    volume_full.apply(volume->config.get(), true);
                    validate_effective(volume_full);
                }
            } else {
                effective.apply(candidate, true);
                validate_effective(effective);
            }
        }
    }
    plater.take_snapshot("MCP update scoped settings");
    if (plate_config) *plate_config = std::move(candidate);
    else model_config->assign_config(std::move(candidate));
    refresh(plater);
    return {{"scope", scope}, {"updated", params["values"]}, {"model", model_state(plater)}, {"plates", plate_state(plater)}};
}

std::string mcp_stage_sliced_plate(Plater& plater, const Json& params, bool for_print) {
    current_slice_plate(plater, params);
    const auto requested_result = text(params, "sliceResultId");
    if (!result_current(plater) || slice_result.id != requested_result)
        throw RpcError("STALE_REFERENCE", "Selected plate no longer has the requested completed slice");
    const auto plate_index = plater.get_partplate_list().get_curr_plate_index();
    const auto source_identity = slice_result.file_identity;
    const auto staging_dir = std::filesystem::temp_directory_path() /
        ("bambu-mcp-slice-" + boost::uuids::to_string(boost::uuids::random_generator()()));
    try {
        std::filesystem::create_directory(staging_dir);
        std::filesystem::permissions(staging_dir, std::filesystem::perms::owner_all,
            std::filesystem::perm_options::replace);
        const auto output = staging_dir / "plate.gcode.3mf";
        const auto strategy = for_print
            ? SaveStrategy::Silence | SaveStrategy::SkipModel | SaveStrategy::WithGcode | SaveStrategy::SkipAuxiliary
            : SaveStrategy::Silence | SaveStrategy::SplitModel | SaveStrategy::WithGcode | SaveStrategy::SkipModel;
        if (plater.export_3mf(boost::filesystem::path(output.string()), strategy, plate_index) < 0)
            throw RpcError("NATIVE_ERROR", "Native sliced 3MF export failed");
        if (!std::filesystem::is_regular_file(output) || std::filesystem::file_size(output) == 0)
            throw RpcError("NATIVE_ERROR", "Native sliced 3MF export produced no file");
        if (!result_current(plater) || slice_result.id != requested_result || slice_result.file_identity != source_identity)
            throw RpcError("STALE_REFERENCE", "Slice result changed during 3MF packaging");
        return output.string();
    } catch (...) {
        std::filesystem::remove_all(staging_dir);
        throw;
    }
}

Json mcp_export_sliced_3mf(Plater& plater, const Json& params) {
    const auto destination = boost::filesystem::path(text(params, "path"));
    require(destination.is_absolute() && boost::algorithm::iends_with(destination.string(), ".gcode.3mf"),
        "Sliced export requires an absolute .gcode.3mf path");
    require(boost::filesystem::is_directory(destination.parent_path()), "Export parent directory does not exist");
    require(!boost::filesystem::exists(destination) || boost::filesystem::is_regular_file(destination),
        "Destination must be a regular file");
    const bool overwrite = flag(params, "overwrite");
    require(overwrite || !boost::filesystem::exists(destination), "Destination exists; explicitly set overwrite=true");
    const auto staged = mcp_stage_sliced_plate(plater, params, false);
    const auto result_id = slice_result.id;
    const auto temporary = destination.parent_path() /
        (".bambu-mcp-" + boost::uuids::to_string(boost::uuids::random_generator()()) + ".tmp");
    try {
        const auto bytes = mcp_copy_gcode(staged, temporary.string(), destination.string(), overwrite,
            [&plater, &result_id] { return result_current(plater) && slice_result.id == result_id; });
        std::filesystem::remove_all(std::filesystem::path(staged).parent_path());
        return {{"sliceResultId", result_id}, {"plateIndex", plater.get_partplate_list().get_curr_plate_index()},
            {"path", destination.string()}, {"bytes", bytes}};
    } catch (...) {
        std::filesystem::remove_all(std::filesystem::path(staged).parent_path());
        throw;
    }
}

#include "McpBatch.inc"
#include "McpAdvanced.inc"
#include "McpDevices.inc"
#include "McpPreview.inc"

const std::map<std::string, Handler>& handlers() {
    static const std::map<std::string, Handler> methods = [] {
    std::map<std::string, Handler> methods = {
        {"slice.get_state", [](Plater& p, const Json&) { return slicing_state(p); }},
        {"slice.validate", [](Plater& p, const Json& a) { return validate_slice(p, a); }},
        {"slice.start", [](Plater& p, const Json& a) { return start_slice(p, a); }},
        {"slice.start_batch", [](Plater& p, const Json& a) { return start_batch(p, a); }},
        {"job.get", [](Plater& p, const Json& a) {
            reconcile_slices(p);
            const auto id = text(a, "jobId");
            if (auto* job = slice_jobs.find(id)) return job_state(*job);
            if (auto* job = find_ui_job(id)) return ui_job_state(*job);
            if (auto job = find_external_job(id)) return external_job_state(job);
            if (auto* job = find_batch_job(id)) return batch_job_state(*job);
            throw RpcError("STALE_REFERENCE", "Job is unknown, expired, or belongs to another application session");
        }},
        {"job.cancel", [](Plater& p, const Json& a) {
            reconcile_slices(p);
            const auto id = text(a, "jobId");
            if (auto* job = slice_jobs.find(id)) {
                if (job->worker_pending && !job->cancellation_requested) {
                    job->cancellation_requested = true;
                    if (job->state == "running") job->state = "cancelling";
                    p.background_process().mcp_request_cancel(job->run);
                }
                return job_state(*job);
            }
            if (auto* job = find_ui_job(id)) {
                if (!job->completion_observed && !job->cancellation_requested) {
                    job->cancellation_requested = true;
                    job->state = "cancelling";
                    p.mcp_cancel_ui_job();
                }
                return ui_job_state(*job);
            }
            if (auto job = find_external_job(id)) {
                {
                    std::lock_guard<std::mutex> lock(job->mutex);
                    if (job->done.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                        job->cancel_requested = true;
                    }
                }
                return external_job_state(job);
            }
            if (auto* job = find_batch_job(id)) {
                if (!job->completion_observed && !job->cancel_requested) {
                    job->cancel_requested = true;
                    job->status = "cancelling";
                    if (job->current_run) if (auto* child = slice_jobs.find(job->current_run)) {
                        child->cancellation_requested = true;
                        p.background_process().mcp_request_cancel(child->run);
                    }
                }
                return batch_job_state(*job);
            }
            throw RpcError("STALE_REFERENCE", "Job is unknown, expired, or belongs to another application session");
        }},
        {"export.gcode", [](Plater& p, const Json& a) { return export_gcode(p, a); }},
        {"export.sliced_3mf", [](Plater& p, const Json& a) { return mcp_export_sliced_3mf(p, a); }},
        {"app.get_state", [](Plater& p, const Json&) { return app_state(p); }},
        {"printer.get_state", [](Plater& p, const Json&) { return printer_state(p); }},
        {"preset.list", [](Plater&, const Json& a) { return preset_list(a); }},
        {"preset.select", [](Plater& p, const Json& a) { return select_preset(p, a); }},
        {"printer.select_nozzle", [](Plater& p, const Json& a) { return select_nozzle(p, a); }},
        {"settings.effective", [](Plater& p, const Json& a) { return effective_settings(p, a); }},
        {"settings.override", [](Plater& p, const Json& a) { return update_overrides(p, a); }},
        {"model.list", [](Plater& p, const Json&) { return model_state(p); }},
        {"object.inspect", [](Plater& p, const Json& a) { return object_inspection(p, a); }},
        {"plate.list", [](Plater& p, const Json&) { return plate_state(p); }},
        {"project.new", [](Plater& p, const Json& a) {
            allow_replace(p, a);
            p.new_project(true, true);
            return app_state(p);
        }},
        {"project.open", [](Plater& p, const Json& a) {
            auto path = input_path(text(a, "path"));
            require(path.extension() == ".3mf", "Project open requires a .3mf file");
            require(path.string().find_first_of("\"'<>") == std::string::npos, "Project path contains unsupported characters");
            allow_replace(p, a);
            if (p.is_project_dirty() || p.is_presets_dirty()) p.new_project(true, true);
            ImportScope import_scope;
            auto result = p.load_project(wxString::FromUTF8(path.string()), "<silence>", true);
            if (result == wxID_CANCEL) throw RpcError("NATIVE_ERROR", "Native project load was cancelled or failed");
            return app_state(p);
        }},
        {"project.save", [](Plater& p, const Json& a) {
            boost::filesystem::path path(text(a, "path"));
            require(path.is_absolute() && path.extension() == ".3mf", "Save requires an absolute .3mf destination");
            require(boost::filesystem::is_directory(path.parent_path()), "Destination directory does not exist");
            require(!boost::filesystem::exists(path) || flag(a, "overwrite"), "Destination exists; set overwrite=true");
            if (p.export_3mf(path, SaveStrategy::Silence) != 0 || !boost::filesystem::is_regular_file(path))
                throw RpcError("NATIVE_ERROR", "Native project export failed");
            p.set_project_filename(wxString::FromUTF8(path.string()));
            p.reset_project_dirty_after_save();
            return Json{{"path", path.string()}, {"saved", true}};
        }},
        {"model.import", [](Plater& p, const Json& a) {
            require(a.contains("paths") && a["paths"].is_array() && !a["paths"].empty() && a["paths"].size() <= 100, "Provide 1 to 100 paths");
            std::vector<boost::filesystem::path> paths;
            const std::set<std::string> extensions = {".stl", ".obj", ".3mf", ".step", ".stp"};
            for (const auto& item : a["paths"]) {
                require(item.is_string(), "Paths must be strings");
                auto path = input_path(item.get<std::string>());
                auto extension = path.extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                require(extensions.count(extension) != 0, "Unsupported geometry import format");
                paths.push_back(path);
            }
            ImportScope import_scope;
            auto loaded = p.load_files(paths, LoadStrategy::LoadModel | LoadStrategy::Silence, false);
            if (loaded.empty()) throw RpcError("NATIVE_ERROR", "Native import did not load any objects");
            return Json{{"importedIndices", loaded}, {"model", model_state(p)}};
        }},
        {"object.rename", [](Plater& p, const Json& a) {
            auto i = object_index(p, a);
            auto name = text(a, "name");
            p.take_snapshot("MCP rename object");
            p.model().objects[i]->name = name;
            p.sidebar().obj_list()->update_name_in_list(i, -1);
            refresh(p);
            return model_state(p);
        }},
        {"object.select", [](Plater& p, const Json& a) {
            auto i = object_index(p, a);
            p.sidebar().obj_list()->select_item(ObjectVolumeID{p.model().objects[i], nullptr});
            return Json{{"selectedObjectIndex", i}};
        }},
        {"object.transform", [](Plater& p, const Json& a) {
            auto i = object_index(p, a);
            auto& object = *p.model().objects[i];
            auto j = instance_index(object, a);
            require(a.contains("position") || a.contains("rotation") || a.contains("scale"), "Provide a transform");
            auto* instance = object.instances[j];
            auto position = a.contains("position") ? vector3(a["position"]) : instance->get_offset();
            auto rotation = a.contains("rotation") ? vector3(a["rotation"]) * (M_PI / 180.0) : instance->get_rotation();
            auto scale = a.contains("scale") ? vector3(a["scale"], true) : instance->get_scaling_factor();
            p.take_snapshot("MCP transform instance");
            instance->set_offset(position); instance->set_rotation(rotation); instance->set_scaling_factor(scale);
            object.invalidate_bounding_box();
            p.get_partplate_list().notify_instance_update(i, j);
            refresh(p);
            return model_state(p);
        }},
        {"object.center", [](Plater& p, const Json& a) {
            auto i = object_index(p, a);
            auto& object = *p.model().objects[i];
            auto j = instance_index(object, a);
            auto& plates = p.get_partplate_list();
            auto plate_index = plates.find_instance(i, j);
            require(plate_index >= 0, "Instance is not assigned to a plate");
            auto* plate = plates.get_plate(plate_index);
            require(!plate->is_locked(), "Unlock the plate before moving its instance");
            auto bounds = object.instance_bounding_box(j);
            const auto center = plate->get_bounding_box().center();
            Vec3d shift(center.x() - bounds.center().x(), center.y() - bounds.center().y(), 0);
            bounds.translate(shift);
            require(plate->contains(bounds), "Centered instance does not fit the plate");
            p.take_snapshot("MCP center instance");
            object.instances[j]->set_offset(object.instances[j]->get_offset() + shift);
            object.invalidate_bounding_box();
            plates.notify_instance_update(i, j);
            refresh(p);
            return model_state(p);
        }},
        {"object.drop_to_bed", [](Plater& p, const Json& a) {
            auto i = object_index(p, a);
            auto& object = *p.model().objects[i];
            auto j = instance_index(object, a);
            auto& plates = p.get_partplate_list();
            auto plate_index = plates.find_instance(i, j);
            require(plate_index >= 0, "Instance is not assigned to a plate");
            auto* plate = plates.get_plate(plate_index);
            require(!plate->is_locked(), "Unlock the plate before moving its instance");
            auto bounds = object.instance_bounding_box(j);
            Vec3d shift(0, 0, plate->get_bounding_box().min.z() - bounds.min.z());
            bounds.translate(shift);
            require(plate->contains(bounds), "Instance does not fit the plate after dropping to bed");
            p.take_snapshot("MCP drop instance to bed");
            object.instances[j]->set_offset(object.instances[j]->get_offset() + shift);
            object.invalidate_bounding_box();
            plates.notify_instance_update(i, j);
            refresh(p);
            return model_state(p);
        }},
        {"object.mirror", [](Plater& p, const Json& a) {
            auto i = object_index(p, a);
            auto& object = *p.model().objects[i];
            auto j = instance_index(object, a);
            const auto axis_name = text(a, "axis");
            Axis axis;
            if (axis_name == "x") axis = X;
            else if (axis_name == "y") axis = Y;
            else if (axis_name == "z") axis = Z;
            else throw RpcError("INVALID_PARAMS", "axis must be x, y or z");
            auto& plates = p.get_partplate_list();
            auto plate_index = plates.find_instance(i, j);
            require(plate_index >= 0 && !plates.get_plate(plate_index)->is_locked(), "Unlock the assigned plate before mirroring");
            auto* instance = object.instances[j];
            p.take_snapshot("MCP mirror instance");
            instance->set_mirror(axis, -instance->get_mirror(axis));
            object.invalidate_bounding_box();
            plates.notify_instance_update(i, j);
            refresh(p);
            return model_state(p);
        }},
        {"filament.assign", [](Plater& p, const Json& a) {
            const int i = object_index(p, a);
            require_object_unlocked(p, i);
            auto& bundle = *wxGetApp().preset_bundle;
            const int slot = index(a, "filamentIndex", bundle.filament_presets.size());
            auto& object = *p.model().objects[i];
            ModelConfig* config = &object.config;
            Json target = {{"objectId", reference_id(object.id().id)}};
            if (a.contains("volumeId")) {
                const int j = volume_index(object, a);
                config = &object.volumes[j]->config;
                target["volumeId"] = reference_id(object.volumes[j]->id().id);
            }
            p.take_snapshot("MCP assign filament");
            config->set("extruder", slot + 1);
            refresh(p);
            return Json{{"target", target}, {"filamentIndex", slot}, {"preset", bundle.filament_presets[slot]},
                {"model", model_state(p)}};
        }},
        {"object.delete", [](Plater& p, const Json& a) {
            auto i = object_index(p, a);
            p.take_snapshot("MCP delete object");
            p.remove(i);
            return model_state(p);
        }},
        {"object.duplicate", [](Plater& p, const Json& a) {
            auto i = object_index(p, a);
            require(!a.contains("copies") || a["copies"].is_number_integer(), "Copies must be an integer");
            auto count = a.value("copies", 1LL);
            require(count >= 1 && count <= 100, "Copies must be between 1 and 100");
            p.sidebar().obj_list()->select_item(ObjectVolumeID{p.model().objects[i], nullptr});
            const auto before = p.model().objects.size();
            p.get_view3D_canvas3D()->get_selection().clone(static_cast<int>(count));
            if (p.model().objects.size() != before + static_cast<std::size_t>(count))
                throw RpcError("NATIVE_ERROR", "Native clone did not create the requested object copies; inspect the model");
            return model_state(p);
        }},
        {"object.reorder", [](Plater& p, const Json& a) {
            auto from = object_index(p, a, "fromObjectId");
            auto to = object_index(p, a, "toObjectId");
            require(p.get_partplate_list().find_instance(from, 0) == p.get_partplate_list().find_instance(to, 0), "Object ordering is limited to one plate");
            p.sidebar().obj_list()->mcp_reorder_object(from, to);
            refresh(p);
            return model_state(p);
        }},
        {"selection.all", [](Plater& p, const Json& a) {
            if (flag(a, "currentPlate")) p.select_curr_plate_all(); else p.select_all();
            return Json{{"selected", true}};
        }},
        {"selection.clear", [](Plater& p, const Json&) { p.deselect_all(); return Json{{"cleared", true}}; }},
        {"plate.select", [](Plater& p, const Json& a) {
            check_plate_revision(p, a);
            auto i = index(a, "plateIndex", p.get_partplate_list().get_plate_count());
            if (p.select_plate(i, false) != 0) throw RpcError("NATIVE_ERROR", "Could not select plate");
            return plate_state(p);
        }},
        {"plate.create", [](Plater& p, const Json&) {
            p.take_snapshot("MCP create plate");
            auto i = p.get_partplate_list().create_plate();
            if (i < 0) throw RpcError("NATIVE_ERROR", "Could not create plate");
            refresh(p);
            return plate_state(p);
        }},
        {"plate.delete", [](Plater& p, const Json& a) {
            check_plate_revision(p, a);
            auto& plates = p.get_partplate_list();
            auto i = index(a, "plateIndex", plates.get_plate_count());
            require(plates.get_plate_count() > 1, "Cannot delete the last plate");
            auto* source = plates.get_plate(i);
            require(!source->is_locked(), "Unlock the plate before deleting it");
            const auto instances = source->get_obj_and_inst_set();
            const auto policy = a.contains("contents") ? text(a, "contents") : std::string("require_empty");
            require(policy == "require_empty" || policy == "move" || policy == "delete",
                "contents must be require_empty, move or delete");
            require(policy != "require_empty" || instances.empty(),
                "Plate contains instances; choose contents=move or contents=delete explicitly");
            require(instances.size() <= 100, "Plate has more than 100 instances; reduce it before deleting");
            int target_index = -1;
            if (policy == "move") {
                target_index = index(a, "targetPlateIndex", plates.get_plate_count());
                require(target_index != i, "Move destination must be another plate");
                auto* target = plates.get_plate(target_index);
                require(!target->is_locked() && target->empty(), "Move destination must be unlocked and empty");
            }
            std::set<int, std::greater<int>> exclusive_objects;
            if (policy == "delete") {
                for (const auto& [object_index_value, instance_index_value] : instances) {
                    const auto& object = *p.model().objects[object_index_value];
                    for (std::size_t j = 0; j < object.instances.size(); ++j)
                        require(plates.find_instance(object_index_value, j) == i,
                            "An object also has instances outside this plate; move its contents instead");
                    exclusive_objects.insert(object_index_value);
                }
            }
            p.take_snapshot("MCP delete plate");
            if (policy == "move") {
                auto* target = plates.get_plate(target_index);
                const Vec3d shift = (target->get_build_volume().center() - source->get_build_volume().center()).cast<double>();
                for (const auto& [object_index_value, instance_index_value] : instances) {
                    auto& object = *p.model().objects[object_index_value];
                    auto* instance = object.instances[instance_index_value];
                    instance->set_offset(instance->get_offset() + shift);
                    object.invalidate_bounding_box();
                    if (source->remove_instance(object_index_value, instance_index_value) != 0 ||
                        target->add_instance(object_index_value, instance_index_value, false) != 0)
                        throw RpcError("NATIVE_ERROR", "Plate contents partially moved; inspect the model before retrying");
                }
            } else if (policy == "delete") {
                for (int object_index_value : exclusive_objects) p.remove(object_index_value);
            }
            require(source->empty(), "Native plate contents did not clear; inspect the model");
            const auto before = plates.get_plate_count();
            if (p.mcp_delete_plate(i) != 0 || plates.get_plate_count() != before - 1)
                throw RpcError("NATIVE_ERROR", "Native plate deletion did not complete");
            p.set_plater_dirty(true);
            completed_slices.clear();
            slice_result = {};
            return Json{{"contents", policy}, {"movedInstanceCount", policy == "move" ? instances.size() : 0},
                {"deletedObjectCount", policy == "delete" ? exclusive_objects.size() : 0},
                {"destinationPlateIndex", target_index < 0 ? Json(nullptr) : Json(target_index > i ? target_index - 1 : target_index)},
                {"plates", plate_state(p)}, {"model", model_state(p)}};
        }},
        {"plate.rename", [](Plater& p, const Json& a) {
            check_plate_revision(p, a);
            auto i = index(a, "plateIndex", p.get_partplate_list().get_plate_count());
            auto name = text(a, "name");
            p.take_snapshot("MCP rename plate");
            p.get_partplate_list().get_plate(i)->set_plate_name(name);
            refresh(p);
            return plate_state(p);
        }},
        {"plate.reorder", [](Plater& p, const Json& a) {
            check_plate_revision(p, a);
            auto& plates = p.get_partplate_list();
            auto from = index(a, "fromIndex", plates.get_plate_count());
            auto to = index(a, "toIndex", plates.get_plate_count());
            if (p.mcp_reorder_plate(from, to) != 0) throw RpcError("NATIVE_ERROR", "Could not reorder plates");
            p.set_plater_dirty(true);
            return plate_state(p);
        }},
        {"plate.lock", [](Plater& p, const Json& a) {
            check_plate_revision(p, a);
            auto i = index(a, "plateIndex", p.get_partplate_list().get_plate_count());
            auto locked = flag(a, "locked", true);
            p.take_snapshot("MCP lock plate");
            p.get_partplate_list().get_plate(i)->lock(locked);
            refresh(p);
            return plate_state(p);
        }},
        {"plate.move_instance", [](Plater& p, const Json& a) {
            check_plate_revision(p, a);
            auto i = object_index(p, a);
            auto& object = *p.model().objects[i];
            auto j = instance_index(object, a);
            auto& plates = p.get_partplate_list();
            auto target = index(a, "plateIndex", plates.get_plate_count());
            auto source = plates.find_instance(i, j);
            require(source >= 0, "Instance is not assigned to a plate");
            require(!plates.get_plate(source)->is_locked() && !plates.get_plate(target)->is_locked(),
                "Unlock both plates before moving the instance");
            if (source != target) {
                p.take_snapshot("MCP move instance to plate");
                if (plates.add_to_plate(i, j, target) != 0 || plates.find_instance(i, j) != target)
                    throw RpcError("NATIVE_ERROR", "Native plate transfer did not complete; inspect the model");
                refresh(p);
            }
            return Json{{"plateIndex", target}, {"model", model_state(p)}, {"plates", plate_state(p)}};
        }},
        {"plate.clone", [](Plater& p, const Json& a) {
            check_plate_revision(p, a);
            auto& plates = p.get_partplate_list();
            const int source_index = index(a, "plateIndex", plates.get_plate_count());
            auto* source = plates.get_plate(source_index);
            const auto copies = source->get_obj_and_inst_set();
            require(copies.size() <= 100, "Plate has more than 100 instances; clone in smaller groups");
            const auto source_config = *source->config();
            const auto source_name = source->get_plate_name();
            p.take_snapshot("MCP clone plate");
            const int target_index = plates.create_plate();
            if (target_index < 0) throw RpcError("NATIVE_ERROR", "Native plate creation failed");
            source = plates.get_plate(source_index);
            auto* target = plates.get_plate(target_index);
            *target->config() = source_config;
            target->set_plate_name(source_name + " copy");
            const auto shift = target->get_bounding_box().center() - source->get_bounding_box().center();
            for (const auto& entry : copies) {
                auto& object = *p.model().objects[entry.first];
                auto* instance = object.add_instance(*object.instances[entry.second]);
                const int new_index = static_cast<int>(object.instances.size()) - 1;
                instance->set_offset(instance->get_offset() + shift);
                object.invalidate_bounding_box();
                if (target->add_instance(entry.first, new_index, false) != 0)
                    throw RpcError("NATIVE_ERROR", "Native plate clone partially completed; inspect the model");
            }
            refresh(p);
            return Json{{"plateIndex", target_index}, {"model", model_state(p)}, {"plates", plate_state(p)}};
        }},
        {"plate.set_print_sequence", [](Plater& p, const Json& a) {
            check_plate_revision(p, a);
            auto i = index(a, "plateIndex", p.get_partplate_list().get_plate_count());
            auto value = text(a, "sequence");
            PrintSequence sequence;
            if (value == "default") sequence = PrintSequence::ByDefault;
            else if (value == "by_layer") sequence = PrintSequence::ByLayer;
            else if (value == "by_object") sequence = PrintSequence::ByObject;
            else throw RpcError("INVALID_PARAMS", "sequence must be default, by_layer or by_object");
            auto* plate = p.get_partplate_list().get_plate(i);
            require(!plate->is_locked(), "Unlock the plate before changing its print sequence");
            p.take_snapshot("MCP set plate print sequence");
            plate->set_print_seq(sequence);
            refresh(p);
            return plate_state(p);
        }},
        {"plate.set_bed_type", [](Plater& p, const Json& a) {
            check_plate_revision(p, a);
            const int i = index(a, "plateIndex", p.get_partplate_list().get_plate_count());
            const auto value = text(a, "bedType");
            const std::map<std::string, BedType> types = {{"default", btDefault}, {"cool_plate", btPC},
                {"engineering_plate", btEP}, {"textured_pei", btPTE}, {"high_temp", btPEI},
                {"super_tack", btSuperTack}};
            const auto match = types.find(value);
            require(match != types.end(), "Unknown bed type");
            auto* plate = p.get_partplate_list().get_plate(i);
            require(!plate->is_locked(), "Unlock the plate before changing its bed type");
            p.take_snapshot("MCP set plate bed type");
            plate->set_bed_type(match->second);
            refresh(p);
            return printer_state(p);
        }},
        {"arrange.start", [](Plater& p, const Json&) {
            auto& job = begin_ui_job(p, "arrange");
            try { p.mcp_start_arrange(job.run); }
            catch (const std::exception& error) {
                mcp_ui_job_completed(job.run, false, false, error.what());
                throw;
            }
            return ui_job_state(job);
        }},
        {"orient.start", [](Plater& p, const Json&) {
            auto& job = begin_ui_job(p, "orient");
            try { p.mcp_start_orient(job.run); }
            catch (const std::exception& error) {
                mcp_ui_job_completed(job.run, false, false, error.what());
                throw;
            }
            return ui_job_state(job);
        }},
        {"history.undo", [](Plater& p, const Json&) { p.undo(); return app_state(p); }},
        {"history.redo", [](Plater& p, const Json&) { p.redo(); return app_state(p); }},
        {"view.set", [](Plater& p, const Json& a) {
            auto direction = text(a, "direction");
            const std::set<std::string> views = {"iso", "top", "bottom", "front", "rear", "left", "right"};
            require(views.count(direction) != 0, "Unknown view direction");
            p.select_view(direction);
            return Json{{"direction", direction}};
        }},
        {"settings.get", [](Plater&, const Json&) {
            auto config = wxGetApp().preset_bundle->full_config_secure();
            Json values = Json::object();
            for (const auto& key : writable_settings()) if (config.has(key)) values[key] = config.opt_serialize(key);
            return Json{{"scope", "process"}, {"values", values}, {"encoding", "native serialized strings"}, {"writableKeys", writable_settings()}};
        }},
        {"settings.update", [](Plater& p, const Json& a) {
            require(a.contains("values") && a["values"].is_object() && !a["values"].empty(), "Provide settings values");
            auto* tab = wxGetApp().get_tab(Preset::TYPE_PRINT);
            if (!tab) throw RpcError("BUSY", "Process settings are unavailable");
            DynamicPrintConfig candidate(*tab->get_config());
            auto full = wxGetApp().preset_bundle->full_config_secure();
            for (auto it = a["values"].begin(); it != a["values"].end(); ++it) {
                require(writable_settings().count(it.key()) && candidate.has(it.key()) && it.value().is_string(), "Unsupported setting or non-string value");
                auto value = it.value().get<std::string>();
                require(value.size() <= 1024, "Setting value too long");
                candidate.set_deserialize_strict(it.key(), value);
                full.set_deserialize_strict(it.key(), value);
            }
            auto errors = full.validate();
            if (!errors.empty()) throw RpcError("INVALID_PARAMS", "Native configuration validation failed: " + errors.begin()->first + ": " + errors.begin()->second);
            p.take_snapshot("MCP update process settings");
            tab->load_config(candidate);
            p.on_config_change(wxGetApp().preset_bundle->full_config_secure());
            p.update_project_dirty_from_presets();
            return Json{{"updated", a["values"]}, {"scope", "process"}};
        }}
    };
    register_advanced_handlers(methods);
    register_devices_handlers(methods);
    register_preview_handlers(methods);
    return methods;
    }();
    return methods;
}

bool device_actions_enabled() {
    return wxGetApp().app_config->get_bool("mcp_allow_device_actions");
}

bool account_reads_enabled() {
    return wxGetApp().app_config->get_bool("mcp_allow_account_reads");
}

Json execute(Plater& plater, const std::string& method, const Json& params) {
    require(params.is_object(), "params must be an object");
    const std::map<std::string, std::set<std::string>> slice_fields = {
        {"slice.get_state", {}}, {"slice.validate", {"plateIndex", "expectedPlateRevision"}},
        {"slice.start", {"plateIndex", "expectedPlateRevision"}},
        {"slice.start_batch", {"expectedPlateRevision", "plateIndices"}},
        {"job.get", {"jobId"}}, {"job.cancel", {"jobId"}},
        {"export.gcode", {"plateIndex", "expectedPlateRevision", "sliceResultId", "path", "overwrite"}},
        {"export.sliced_3mf", {"plateIndex", "expectedPlateRevision", "sliceResultId", "path", "overwrite"}}
    };
    if (auto schema = slice_fields.find(method); schema != slice_fields.end()) {
        for (auto field = params.begin(); field != params.end(); ++field)
            require(schema->second.count(field.key()) != 0, "Unknown parameter for this method");
        for (const auto* key : {"jobId", "expectedPlateRevision", "sliceResultId"})
            if (params.contains(key)) require(text(params, key).size() <= 160, "Reference is too long");
    }
    if (method == "app.get_capabilities") {
        Json names = Json::array({"app.get_capabilities"});
        for (const auto& item : handlers()) names.push_back(item.first);
        return {{"sessionId", session_id()}, {"protocolVersion", 1}, {"appVersion", SLIC3R_VERSION}, {"methods", names},
            {"features", {{"jobCompletionTracking", true}, {"jobTrackingScope", "FFF plate slicing, sequential plate queue, UI jobs and external device jobs"}, {"parameterIndices", "zero-based"}, {"objectReferences", "stable IDs"}, {"plateReferences", "index with required expectedPlateRevision"}, {"securityPolicyVersion", 1}, {"deviceActionsEnabled", device_actions_enabled()}, {"accountReadsEnabled", account_reads_enabled()}}}};
    }
    const auto it = handlers().find(method);
    if (it == handlers().end()) throw RpcError("UNSUPPORTED", "Method is not implemented in this native build");
    if ((method == "device.pause" || method == "device.resume" || method == "device.stop" ||
         method == "calibration.start" || method == "device.upload.start" || method == "device.print.start") &&
        !device_actions_enabled())
        throw RpcError("PERMISSION_DENIED", "Printer actions are disabled; enable them in MCP Preferences");
    if ((method == "account.tasks.list" || method == "account.presets.list") && !account_reads_enabled())
        throw RpcError("PERMISSION_DENIED", "Cloud account reads are disabled; enable them in MCP Preferences");
    reconcile_slices(plater);
    const bool read = method == "slice.get_state" || method == "job.get" || method == "job.cancel" || method == "app.get_state" || method == "model.list" || method == "object.inspect" || method == "geometry.faces" || method == "geometry.analyze" || method == "layers.get" || method == "plate.list" || method == "settings.get" || method == "settings.effective" || method == "printer.get_state" || method == "preset.list" || method == "preview.get_summary" || method == "preview.get_layers" || method == "preview.get_toolpaths" || method == "device.list" || method == "device.get_state" || method == "device.upload.plan" || method == "device.print.plan" || method == "ams.inventory" || method == "calibration.get_state" || method == "account.get_state" || method == "account.tasks.list" || method == "account.presets.list" || method == "preferences.get";
    auto* canvas = plater.get_view3D_canvas3D();
    const bool interactive_edit = canvas && (canvas->is_dragging() || canvas->get_gizmos_manager().is_in_editing_mode());
    const bool ui_job_active = std::any_of(ui_jobs.begin(), ui_jobs.end(), [](const UiJob& job) { return !job.completion_observed; });
    const bool external_job_active = std::any_of(external_jobs.begin(), external_jobs.end(), [](const std::shared_ptr<ExternalJob>& job) {
        return job->done.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
    });
    if (!read && (modal_open() || interactive_edit || plater.is_loading_project() || plater.is_any_job_running() || plater.background_process().running() || plater.background_process().mcp_orphans_running() || slice_jobs.active() || batch_active() || ui_job_active || external_job_active))
        throw RpcError("BUSY", "Finish the active dialog, interactive edit or native job before changing the project");
    return it->second(plater, params);
}

}

bool mcp_noninteractive_import() { return noninteractive_import; }

bool mcp_slice_active() { return slice_jobs.active() != nullptr; }

bool mcp_accept_slice_event(std::uint64_t run) { return slice_jobs.accepts_event(run); }

void mcp_slice_progress(std::uint64_t run, int percent) { slice_jobs.progress(run, percent); }

void mcp_slice_completed(Plater& plater, std::uint64_t run, bool success, bool cancelled, bool worker_stopped, const std::string& error) {
    auto* job = slice_jobs.find(run);
    if (!job || !job->worker_pending) return;
    struct NotifyBatch {
        McpSliceJob* job;
        ~NotifyBatch() { if (job && !job->worker_pending) batch_child_completed(*job); }
    } notify_batch{job};
    try {
    if (!worker_stopped) {
        job->cancellation_requested = true;
        if (job->state == "running") job->state = "cancelling";
        return;
    }
    if (job->state == "stale" || job->fingerprint != slice_fingerprint(plater, true)) {
        slice_jobs.finish(run, "stale", "STALE_REFERENCE", "Native slice inputs changed before completion");
        return;
    }
    if (cancelled) { slice_jobs.finish(run, "cancelled"); return; }
    if (!success) { slice_jobs.finish(run, "failed", "NATIVE_ERROR", error.empty() ? "Native slicing failed" : error); return; }
    auto* plate = plater.get_partplate_list().get_curr_plate();
    if (job->plate_index != plater.get_partplate_list().get_curr_plate_index() ||
        plater.background_process().mcp_current_run() != run || !plate->is_slice_result_valid() ||
        !plate->is_slice_result_ready_for_export() || !plate->get_slice_result() || plate->get_slice_result()->moves.empty() ||
        !boost::filesystem::is_regular_file(plate->get_tmp_gcode_path()) || boost::filesystem::file_size(plate->get_tmp_gcode_path()) == 0) {
        slice_jobs.finish(run, "failed", "NATIVE_ERROR", "Native completion did not produce a valid exportable G-code result");
        return;
    }
    job->result_id = session_id() + ":result:" + std::to_string(run);
    slice_result = {job->result_id, slice_fingerprint(plater), plate->get_tmp_gcode_path(),
        mcp_gcode_identity(plate->get_tmp_gcode_path()), job->plate_index, run,
        reinterpret_cast<std::uintptr_t>(plate->get_slice_result())};
    completed_slices[job->plate_index] = slice_result;
    slice_jobs.finish(run, "succeeded");
    } catch (const std::exception& exception) {
        slice_jobs.finish(run, "failed", "NATIVE_ERROR", exception.what());
    }
}

void mcp_ui_job_completed(std::uint64_t run, bool success, bool cancelled, const std::string& error) {
    if (!run) return;
    for (auto& job : ui_jobs) {
        if (job.run != run || job.completion_observed) continue;
        job.completion_observed = true;
        job.state = cancelled ? "cancelled" : success ? "succeeded" : "failed";
        job.error = success || cancelled ? std::string() : error.empty() ? "Native UI job failed" : error;
        return;
    }
}


void mcp_require_interactive_import(const char* reason) {
    if (noninteractive_import)
        throw RpcError("INTERACTION_REQUIRED", std::string(reason) + "; open the file in Bambu Studio to resolve it. The project may have changed; inspect state before retrying.");
}

void mcp_quiesce_external_jobs() {
    for (const auto& job : external_jobs) job->cancel_requested = true;
    for (const auto& job : external_jobs) if (job->worker.joinable()) job->worker.join();
}

void McpTools::set_active(bool active) {
    const bool was_active = mcp_publication_enabled.exchange(active);
    if (active) return;
    if (!was_active) return;
    ++mcp_domain_generation;
    for (auto& batch : slice_batches)
        if (!batch.completion_observed) {
            batch.cancel_requested = true;
            if (batch.current_run) batch.status = "cancelling";
            else finish_batch(batch, "cancelled");
        }
    bool cancel_ui_job = false;
    for (auto& job : ui_jobs)
        if (!job.completion_observed) {
            job.cancellation_requested = true;
            job.state = "cancelling";
            cancel_ui_job = true;
        }
    if (auto* plater = wxGetApp().plater()) {
        if (cancel_ui_job) plater->mcp_cancel_ui_job();
        if (auto* job = slice_jobs.active()) {
            job->cancellation_requested = true;
            plater->background_process().mcp_request_cancel(job->run);
        }
    }
    slice_result = {};
    completed_slices.clear();
}

void McpTools::quiesce_external_jobs() {
    mcp_quiesce_external_jobs();
    external_jobs.clear();
}

bool McpTools::has_pending_native_work() {
    if (slice_jobs.active() || batch_active()) return true;
    for (const auto& job : ui_jobs)
        if (!job.completion_observed) return true;
    for (const auto& job : external_jobs)
        if (job->done.valid() && job->done.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return true;
    return false;
}

namespace {
constexpr const char* mcp_manifest =
#include "McpToolsManifest.inc"
;

const Json& catalog() {
    static const Json tools = Json::parse(mcp_manifest);
    return tools;
}

void validate_schema(Json& value, const Json& schema, const std::string& field, int depth = 0) {
    if (depth > 12) throw std::invalid_argument("Arguments are too deeply nested");
    if (schema.contains("type")) {
        const std::string type = schema["type"].get<std::string>();
        const bool valid = type == "object" ? value.is_object() : type == "array" ? value.is_array() :
            type == "string" ? value.is_string() : type == "boolean" ? value.is_boolean() :
            type == "integer" ? value.is_number_integer() : type == "number" ? value.is_number() : false;
        if (!valid) throw std::invalid_argument(field + " has the wrong type");
    }
    if (schema.contains("const") && value != schema["const"])
        throw std::invalid_argument(field + " has an invalid value");
    if (schema.contains("enum") && std::find(schema["enum"].begin(), schema["enum"].end(), value) == schema["enum"].end())
        throw std::invalid_argument(field + " has an invalid value");
    if (value.is_object()) {
        if (schema.contains("properties")) {
            for (auto property = schema["properties"].begin(); property != schema["properties"].end(); ++property)
                if (!value.contains(property.key()) && property.value().contains("default"))
                    value[property.key()] = property.value()["default"];
        }
        if (schema.contains("required"))
            for (const auto& key : schema["required"])
                if (!value.contains(key.get<std::string>()))
                    throw std::invalid_argument(field + " requires " + key.get<std::string>());
        for (auto member = value.begin(); member != value.end(); ++member) {
            if (schema.contains("properties") && schema["properties"].contains(member.key()))
                validate_schema(member.value(), schema["properties"][member.key()], field + "." + member.key(), depth + 1);
            else if (schema.value("additionalProperties", Json(true)) == Json(false))
                throw std::invalid_argument(field + " has an unknown field: " + member.key());
            else if (schema.contains("additionalProperties") && schema["additionalProperties"].is_object())
                validate_schema(member.value(), schema["additionalProperties"], field + "." + member.key(), depth + 1);
        }
    }
    if (value.is_array()) {
        if (schema.contains("minItems") && value.size() < schema["minItems"].get<size_t>())
            throw std::invalid_argument(field + " has too few items");
        if (schema.contains("maxItems") && value.size() > schema["maxItems"].get<size_t>())
            throw std::invalid_argument(field + " has too many items");
        if (schema.contains("items"))
            for (size_t i = 0; i < value.size(); ++i)
                validate_schema(value[i], schema["items"], field + "[" + std::to_string(i) + "]", depth + 1);
    }
    if (value.is_string()) {
        const std::string text = value.get<std::string>();
        if (schema.contains("minLength") && text.size() < schema["minLength"].get<size_t>())
            throw std::invalid_argument(field + " is too short");
        if (schema.contains("maxLength") && text.size() > schema["maxLength"].get<size_t>())
            throw std::invalid_argument(field + " is too long");
        if (schema.contains("pattern") && !std::regex_search(text, std::regex(schema["pattern"].get<std::string>())))
            throw std::invalid_argument(field + " has an invalid format");
    }
    if (value.is_number()) {
        const double number = value.get<double>();
        if (!std::isfinite(number) ||
            (schema.contains("minimum") && number < schema["minimum"].get<double>()) ||
            (schema.contains("exclusiveMinimum") && number <= schema["exclusiveMinimum"].get<double>()) ||
            (schema.contains("maximum") && number > schema["maximum"].get<double>()))
            throw std::invalid_argument(field + " is outside the allowed range");
    }
}

void validate_refinements(const std::string& method, const Json& args) {
    auto require_value = [](bool valid, const char* message) {
        if (!valid) throw std::invalid_argument(message);
    };
    auto ends_with = [](const std::string& value, const std::string& suffix) {
        return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
    };
    for (const char* key : {"path"})
        if (args.contains(key)) {
            const auto& path = args[key].get_ref<const std::string&>();
            require_value(path.find('\0') == std::string::npos && boost::filesystem::path(path).is_absolute(),
                          "path must be an absolute path without NUL");
        }
    if (args.contains("paths"))
        for (const auto& entry : args["paths"]) {
            const auto& path = entry.get_ref<const std::string&>();
            require_value(path.find('\0') == std::string::npos && boost::filesystem::path(path).is_absolute(),
                          "paths must contain absolute paths without NUL");
            if (method == "model.import_multipart")
                require_value(ends_with(path, ".stl") || ends_with(path, ".obj"),
                              "Multipart import accepts only STL or OBJ files");
        }
    if (method == "object.transform")
        require_value(args.contains("position") || args.contains("rotation") || args.contains("scale"),
                      "Provide position, rotation, or scale");
    if (method == "plate.delete")
        require_value((args.value("contents", std::string("require_empty")) == "move") == args.contains("targetPlateIndex"),
                      "targetPlateIndex is required only for contents=move");
    const std::map<std::string, std::vector<std::string>> suffixes = {
        {"view.capture", {".png"}}, {"export.geometry", {".stl", ".obj"}},
        {"export.gcode", {".gcode"}}, {"export.sliced_3mf", {".gcode.3mf"}}
    };
    if (auto it = suffixes.find(method); it != suffixes.end()) {
        bool matching = false;
        for (const auto& suffix : it->second) matching |= ends_with(args["path"].get_ref<const std::string&>(), suffix);
        require_value(matching, "Destination file extension is invalid");
    }
    if (method == "settings.effective")
        require_value((!args.contains("plateIndex") || args.contains("expectedPlateRevision")) &&
                      (!args.contains("volumeId") || args.contains("objectId")), "Missing scope revision or object ID");
    if (method == "settings.update" || method == "settings.override")
        require_value(args["values"].size() >= 1 && args["values"].size() <= 100, "Provide 1 to 100 settings");
    if (method == "settings.override") {
        const auto scope = args["scope"].get<std::string>();
        require_value(scope == "plate" ? args.contains("plateIndex") && args.contains("expectedPlateRevision") && !args.contains("objectId") && !args.contains("volumeId") :
                      scope == "object" ? args.contains("objectId") && !args.contains("plateIndex") && !args.contains("volumeId") :
                      args.contains("objectId") && args.contains("volumeId") && !args.contains("plateIndex"),
                      "Provide exactly the IDs required by the selected scope");
    }
    if (method == "object.cut") {
        bool nonzero = false;
        for (const auto& component : args["normal"]) nonzero |= component.get<double>() != 0;
        require_value(nonzero, "Plane normal must be nonzero");
    }
    for (const char* key : {"objectIds", "faceIndices", "plateIndices"})
        if (args.contains(key)) {
            std::set<Json> unique(args[key].begin(), args[key].end());
            require_value(unique.size() == args[key].size(), "Array values must be unique");
        }
    if (method == "ams.mapping.plan")
        require_value(args.contains("amsMapping") && !args["amsMapping"].empty(), "Provide AMS assignments");
    if (method == "calibration.start") {
        bool selected = false;
        for (const char* key : {"vibration", "bedLeveling", "camera", "motorNoise", "nozzle", "bedCalibration", "clumpPosition"})
            selected |= args.value(key, false);
        require_value(selected, "Select at least one calibration routine");
    }
}
}

nlohmann::json McpTools::definitions() {
    Json output = catalog();
    for (auto& tool : output) tool.erase("method");
    return output;
}

nlohmann::json McpTools::call(Plater& plater, const std::string& name, const nlohmann::json& arguments) {
    if (!arguments.is_object()) throw std::invalid_argument("Arguments must be an object");
    std::string method;
    Json normalized = arguments;
    for (const auto& entry : catalog())
        if (entry["name"] == name) {
            method = entry["method"].get<std::string>();
            validate_schema(normalized, entry["inputSchema"], "arguments");
            validate_refinements(method, normalized);
            break;
        }
    if (method.empty()) throw std::invalid_argument("Unknown tool name: " + name);
    try {
        auto data = execute(plater, method, normalized);
        return {{"content", nlohmann::json::array({{{"type", "text"}, {"text", data.dump()}}})},
                {"structuredContent", std::move(data)}};
    } catch (const RpcError& error) {
        if (error.code == "INVALID_PARAMS") throw std::invalid_argument(error.what());
        nlohmann::json data = {{"code", error.code}, {"message", error.what()}};
        return {{"content", nlohmann::json::array({{{"type", "text"}, {"text", data.dump()}}})},
                {"structuredContent", std::move(data)}, {"isError", true}};
    }
}

}}
