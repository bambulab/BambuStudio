#include "QueuePrintController.hpp"

#include <cstdlib>
#include <cerrno>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <ios>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/nowide/fstream.hpp>
#include <expat.h>
#include <nlohmann/json.hpp>

#include "GUI_App.hpp"
#include "Plater.hpp"
#include "PrintQueueManager.hpp"
#include "QueuePrintContext.hpp"
#include "slic3r/Utils/NetworkAgent.hpp"
#include "libslic3r/Log.hpp"

namespace Slic3r { namespace GUI {

namespace {

constexpr size_t kMaxQueueConfigBytes = 20 * 1024 * 1024;
constexpr size_t kMaxLogicalFilaments = 64;
constexpr size_t kMaxFilamentIdLength = 512;
constexpr size_t kMaxFilamentChangeSequence = 100000;

// Metadata and child elements may appear in any order inside <plate>, and some
// producers emit printer_model_id before index. Every plate is staged while it is
// parsed, and only committed once </plate> reveals which plate it was.
struct PlateStaging
{
    std::optional<int>          index;
    std::shared_ptr<PlateData>  plate = std::make_shared<PlateData>();
    std::vector<bool>           filament_ids_seen;
    std::vector<double>         nozzle_diameters;
    std::vector<int>            nozzle_volume_types;
    std::optional<bool>         enable_dynamic_nozzle_map;
    std::optional<bool>         has_filament_switcher;
    bool                        failed = false;
    std::string                 error;
};

struct SliceParser
{
    SliceParser(std::shared_ptr<QueuePrintContext> queue_context, int expected)
        : expected_index(expected), context(std::move(queue_context))
    {
    }

    int                         expected_index;
    int                         plate_depth = 0;
    bool                        found = false;
    bool                        failed = false;
    std::string                 error;
    // Taken over from the staged plate on commit; parse_context validates these.
    std::vector<double>         nozzle_diameters;
    std::vector<int>            nozzle_volume_types;
    std::optional<bool>         enable_dynamic_nozzle_map;
    std::optional<bool>         has_filament_switcher;
    PlateStaging                staging;
    std::shared_ptr<QueuePrintContext> context;
};

std::optional<std::string> attribute(const char** attributes, const char* name)
{
    for (const char** it = attributes; it && *it; it += 2) {
        if (std::string(*it) == name)
            return it[1] ? std::optional<std::string>(it[1]) : std::optional<std::string>(std::string());
    }
    return std::nullopt;
}

bool parse_int(const std::string& value, int minimum, int maximum, int& output)
{
    const char* cursor = value.c_str();
    if (*cursor == '\0') return false;
    char* end = nullptr;
    errno = 0;
    const long number = std::strtol(cursor, &end, 10);
    if (errno == ERANGE || end == cursor || *end != '\0' || number < minimum || number > maximum)
        return false;
    output = static_cast<int>(number);
    return true;
}

bool parse_positive_int64(const std::string& value, int64_t& output)
{
    if (value.empty()) return false;
    const char* cursor = value.c_str();
    char* end = nullptr;
    errno = 0;
    const long long number = std::strtoll(cursor, &end, 10);
    if (errno == ERANGE || end == cursor || *end != '\0' || number <= 0)
        return false;
    output = static_cast<int64_t>(number);
    return true;
}

bool is_decimal_id(const std::string& value)
{
    return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return character >= '0' && character <= '9';
    });
}

bool parse_ints(const std::string& value, int minimum, int maximum, std::vector<int>& output, size_t max_count = kMaxLogicalFilaments)
{
    output.clear();
    size_t begin = 0;
    while (begin < value.size()) {
        while (begin < value.size() && (value[begin] == ' ' || value[begin] == ',')) ++begin;
        if (begin == value.size()) break;
        const size_t end = value.find_first_of(" ,", begin);
        const std::string token = value.substr(begin, end - begin);
        int number = 0;
        if (token.empty() || !parse_int(token, minimum, maximum, number))
            return false;
        output.push_back(number);
        if (output.size() > max_count)
            return false;
        if (end == std::string::npos)
            return true;
        begin = end + 1;
    }
    return !output.empty();
}

bool parse_positive_double(const std::string& value, double& output)
{
    if (value.empty()) return false;
    char* end = nullptr;
    errno = 0;
    const double number = std::strtod(value.c_str(), &end);
    if (errno == ERANGE || end == value.c_str() || *end != '\0' || !std::isfinite(number) || number <= 0.0)
        return false;
    output = number;
    return true;
}

// A malformed plate only aborts the parse when it turns out to be the plate the
// queue asked for, so the failure is staged instead of reported right away.
void fail(PlateStaging& staging, const std::string& message)
{
    if (!staging.failed) {
        staging.failed = true;
        staging.error = message;
    }
}

std::optional<NozzleVolumeType> parse_volume_type(const std::string& value)
{
    const auto values = ConfigOptionEnum<NozzleVolumeType>::get_enum_values();
    const auto it = values.find(value);
    if (it == values.end()) return std::nullopt;
    return static_cast<NozzleVolumeType>(it->second);
}

bool is_valid_volume_type(int value)
{
    const auto& values = ConfigOptionEnum<NozzleVolumeType>::get_enum_values();
    return std::any_of(values.begin(), values.end(), [value](const auto& item) {
        return item.second == value;
    });
}

void XMLCALL start_element(void* user_data, const char* name, const char** attributes)
{
    auto& parser = *static_cast<SliceParser*>(user_data);
    if (parser.failed) return;
    const std::string element(name ? name : "");
    if (element == "plate") {
        if (parser.plate_depth == 0)
            parser.staging = PlateStaging();
        ++parser.plate_depth;
        return;
    }
    if (parser.plate_depth != 1)
        return;

    PlateStaging& staging = parser.staging;
    if (staging.failed)
        return;

    if (element == "metadata") {
        const auto key_attr = attribute(attributes, "key");
        const auto value_attr = attribute(attributes, "value");
        if (!key_attr || key_attr->empty() || !value_attr) {
            fail(staging, "The queue slice metadata is malformed.");
            return;
        }
        const std::string& key = *key_attr;
        const std::string& value = *value_attr;
        if (key == "index") {
            int index = 0;
            if (!parse_int(value, 1, kMaxQueuePlateIndex, index)) {
                fail(staging, "The queue slice plate index is invalid.");
                return;
            }
            staging.index = index;
            return;
        }
        if (value.empty()) {
            fail(staging, "The selected queue slice metadata is incomplete.");
            return;
        }

        PlateData& plate = *staging.plate;
        if (key == "printer_model_id") {
            plate.printer_model_id = value;
        } else if (key == "nozzle_diameters") {
            plate.nozzle_diameters = value;
            size_t begin = 0;
            while (begin < value.size()) {
                while (begin < value.size() && (value[begin] == ' ' || value[begin] == ',')) ++begin;
                if (begin == value.size()) break;
                const size_t end = value.find_first_of(" ,", begin);
                const std::string token = value.substr(begin, end - begin);
                double diameter = 0.0;
                if (token.empty() || !parse_positive_double(token, diameter)) {
                    fail(staging, "The selected queue nozzle diameter is invalid.");
                    return;
                }
                staging.nozzle_diameters.push_back(diameter);
                if (staging.nozzle_diameters.size() > 8) {
                    fail(staging, "The selected queue has too many nozzles.");
                    return;
                }
                if (end == std::string::npos) break;
                begin = end + 1;
            }
        } else if (key == "nozzle_volume_type") {
            plate.nozzle_volume_types = value;
            if (!parse_ints(value, 0, nvtCount - 1, staging.nozzle_volume_types) ||
                !std::all_of(staging.nozzle_volume_types.begin(), staging.nozzle_volume_types.end(), is_valid_volume_type)) {
                fail(staging, "The selected queue nozzle volume type is invalid.");
                return;
            }
        } else if (key == "filament_maps") {
            if (!parse_ints(value, 0, 8, plate.filament_maps)) {
                fail(staging, "The selected queue filament mapping is invalid.");
                return;
            }
        } else if (key == "enable_filament_dynamic_map") {
            if (value != "true" && value != "false") {
                fail(staging, "The selected queue dynamic nozzle setting is invalid.");
                return;
            }
            staging.enable_dynamic_nozzle_map = value == "true";
        } else if (key == "has_filament_switcher") {
            if (value != "true" && value != "false") {
                fail(staging, "The selected queue filament switch setting is invalid.");
                return;
            }
            staging.has_filament_switcher = value == "true";
        } else if (key == "prediction") {
            plate.gcode_prediction = value;
        } else if (key == "weight") {
            plate.gcode_weight = value;
        }
        return;
    }

    if (element == "filament") {
        const auto id_attr = attribute(attributes, "id");
        const auto tray_attr = attribute(attributes, "tray_info_idx");
        const auto group_attr = attribute(attributes, "group_id");
        const auto volume_attr = attribute(attributes, "volume_type");
        if (!id_attr || !tray_attr || tray_attr->empty() || !group_attr || group_attr->empty() || !volume_attr || volume_attr->empty()) {
            fail(staging, "The selected queue filament data is incomplete.");
            return;
        }
        FilamentInfo filament;
        int xml_id = 0;
        if (!parse_int(*id_attr, 1, static_cast<int>(kMaxLogicalFilaments), xml_id)) {
            fail(staging, "The selected queue filament id is invalid.");
            return;
        }
        filament.id = xml_id - 1;
        if (staging.filament_ids_seen.empty()) staging.filament_ids_seen.resize(kMaxLogicalFilaments, false);
        if (staging.filament_ids_seen[filament.id]) {
            fail(staging, "The selected queue filament ids are duplicated.");
            return;
        }
        staging.filament_ids_seen[filament.id] = true;
        filament.type = attribute(attributes, "type").value_or(std::string());
        filament.color = attribute(attributes, "color").value_or(std::string());
        filament.filament_id = *tray_attr;
        if (filament.filament_id.size() > kMaxFilamentIdLength ||
            !parse_ints(*group_attr, 0, 255, filament.group_id)) {
            fail(staging, "The selected queue filament mapping is invalid.");
            return;
        }
        filament.nozzle_diameter = 0.0;
        if (const auto diameter_attr = attribute(attributes, "nozzle_diameter"); diameter_attr && !diameter_attr->empty()) {
            if (!parse_positive_double(*diameter_attr, filament.nozzle_diameter)) {
                fail(staging, "The selected queue filament nozzle diameter is invalid.");
                return;
            }
        }
        filament.nozzle_volume_type = *volume_attr;
        staging.plate->slice_filaments_info.push_back(std::move(filament));
    } else if (element == "nozzle") {
        const auto id_attr = attribute(attributes, "id");
        const auto extruder_attr = attribute(attributes, "extruder_id");
        const auto diameter_attr = attribute(attributes, "nozzle_diameter");
        const auto volume_attr = attribute(attributes, "volume_type");
        if (!id_attr || !extruder_attr || !diameter_attr || diameter_attr->empty() || !volume_attr || volume_attr->empty()) {
            fail(staging, "The selected queue nozzle data is incomplete.");
            return;
        }
        MultiNozzleUtils::NozzleInfo nozzle;
        int extruder = 0;
        double diameter = 0.0;
        if (!parse_int(*id_attr, 0, 255, nozzle.group_id) || !parse_int(*extruder_attr, 1, 8, extruder) ||
            !parse_positive_double(*diameter_attr, diameter)) {
            fail(staging, "The selected queue nozzle data is invalid.");
            return;
        }
        nozzle.extruder_id = extruder - 1;
        nozzle.diameter = *diameter_attr;
        const auto volume_type = parse_volume_type(*volume_attr);
        if (!volume_type) {
            fail(staging, "The selected queue nozzle volume type is invalid.");
            return;
        }
        nozzle.volume_type = *volume_type;
        staging.plate->nozzles_info.push_back(std::move(nozzle));
        if (staging.plate->nozzles_info.size() > 64)
            fail(staging, "The selected queue has too many nozzle groups.");
    }
}

void commit_plate(SliceParser& parser)
{
    PlateStaging& staging = parser.staging;
    const bool selected = !parser.found && staging.index.has_value() && *staging.index == parser.expected_index;
    BOOST_LOG_TRIVIAL(info) << "QueuePrintController: slice plate index=" << staging.index.value_or(-1)
                            << ", expected_index=" << parser.expected_index
                            << ", selected=" << selected;
    if (!selected)
        return;

    parser.found = true;
    if (staging.failed) {
        parser.failed = true;
        parser.error = staging.error;
        BOOST_LOG_TRIVIAL(warning) << "QueuePrintController: slice parse failed, expected_index="
                                  << parser.expected_index << ", reason=" << staging.error;
        return;
    }

    staging.plate->plate_index = *staging.index - 1;
    parser.context->plate_data = staging.plate;
    parser.nozzle_diameters = std::move(staging.nozzle_diameters);
    parser.nozzle_volume_types = std::move(staging.nozzle_volume_types);
    parser.enable_dynamic_nozzle_map = staging.enable_dynamic_nozzle_map;
    parser.has_filament_switcher = staging.has_filament_switcher;
}

void XMLCALL end_element(void* user_data, const char* name)
{
    auto& parser = *static_cast<SliceParser*>(user_data);
    if (parser.failed) return;
    if (std::string(name ? name : "") == "plate") {
        --parser.plate_depth;
        if (parser.plate_depth == 0)
            commit_plate(parser);
    }
}

std::shared_ptr<QueuePrintContext> parse_context(const QueuePrintRequest& request,
                                                  const std::string& slice_info,
                                                  const std::string& project_settings,
                                                  std::string& error)
{
    if (slice_info.size() > kMaxQueueConfigBytes || project_settings.size() > kMaxQueueConfigBytes ||
        request.plate_index < 1 || request.plate_index > kMaxQueuePlateIndex) {
        error = "The queue print data exceeds supported limits.";
        return nullptr;
    }

    auto context = std::make_shared<QueuePrintContext>();
    context->queue_plate_index = request.plate_index;
    context->cloud_plate_index = request.plate_index;
    context->plate_data = std::make_shared<PlateData>();
    context->plate_data->plate_index = request.plate_index - 1;

    XML_Parser xml = XML_ParserCreate(nullptr);
    if (!xml) {
        error = "Unable to create the slice metadata parser.";
        return nullptr;
    }
    SliceParser parser(context, context->cloud_plate_index);
    XML_SetUserData(xml, &parser);
    XML_SetElementHandler(xml, start_element, end_element);
    const bool parsed = slice_info.size() <= static_cast<size_t>(std::numeric_limits<int>::max()) &&
        XML_Parse(xml, slice_info.data(), static_cast<int>(slice_info.size()), XML_TRUE) != XML_STATUS_ERROR;
    XML_ParserFree(xml);
    if (!parsed || parser.failed || !parser.found || parser.plate_depth != 0) {
        error = parser.error.empty() ? "The selected queue plate has invalid slice metadata." : parser.error;
        BOOST_LOG_TRIVIAL(warning) << "QueuePrintController: slice context rejected, queue_plate_index="
                                  << request.plate_index << ", parsed=" << parsed << ", found=" << parser.found
                                  << ", plate_depth=" << parser.plate_depth << ", reason=" << error;
        return nullptr;
    }
    if (parser.enable_dynamic_nozzle_map.value_or(false) && context->plate_data->nozzles_info.empty()) {
        error = "The selected queue dynamic nozzle data is incomplete.";
        return nullptr;
    }
    if (!context->plate_data->filament_maps.empty() &&
        context->plate_data->filament_maps.size() < context->plate_data->slice_filaments_info.size()) {
        error = "The selected queue filament mapping is incomplete.";
        return nullptr;
    }
    if (parser.nozzle_diameters.size() != parser.nozzle_volume_types.size()) {
        error = "The selected queue nozzle requirements are inconsistent.";
        return nullptr;
    }

    const nlohmann::json settings = nlohmann::json::parse(project_settings, nullptr, false);
    if (settings.is_discarded()) {
        error = "The queue project settings are invalid.";
        return nullptr;
    }
    std::vector<std::string> filament_ids;
    bool has_filament_ids = false;
    if (const auto it = settings.find("filament_ids"); it != settings.end() && it->is_array()) {
        has_filament_ids = true;
        if (it->size() > kMaxLogicalFilaments) {
            error = "The queue project has too many filament ids.";
            return nullptr;
        }
        for (const auto& id : *it) {
            if (!id.is_string() || id.get_ref<const std::string&>().size() > kMaxFilamentIdLength) {
                error = "The queue project filament ids are invalid.";
                return nullptr;
            }
            filament_ids.push_back(id.get<std::string>());
        }
    }

    boost::filesystem::path config_path;
    try {
        config_path = boost::filesystem::temp_directory_path() /
                      boost::filesystem::unique_path("queue-project-%%%%%%%%.config");
    }
    catch (const std::exception&) {
        error = "Unable to prepare the queue project settings.";
        return nullptr;
    }
    const auto remove_temp_config = [&config_path]() {
        boost::system::error_code remove_error;
        boost::filesystem::remove(config_path, remove_error);
    };
    {
        try {
            boost::nowide::ofstream config_file(config_path.string(), std::ios::binary);
            if (!config_file) {
                error = "Unable to prepare the queue project settings.";
                remove_temp_config();
                return nullptr;
            }
            config_file.write(project_settings.data(), static_cast<std::streamsize>(project_settings.size()));
            if (!config_file) {
                error = "Unable to save the queue project settings.";
                remove_temp_config();
                return nullptr;
            }
        }
        catch (const std::exception&) {
            error = "Unable to save the queue project settings.";
            remove_temp_config();
            return nullptr;
        }
    }
    std::map<std::string, std::string> key_values;
    std::string reason;
    ConfigSubstitutionContext substitutions(ForwardCompatibilitySubstitutionRule::Disable);
    int load_result = -1;
    try {
        load_result = context->config.load_from_json(config_path.string(), substitutions, true, key_values, reason);
    }
    catch (const std::exception& exception) {
        reason = exception.what();
    }
    remove_temp_config();
    if (load_result != 0) {
        // The config loader may include its temporary file path in `reason`.
        // Keep that path out of both the UI message and queue diagnostics.
        error = "Unable to load the queue project settings.";
        return nullptr;
    }

    if (has_filament_ids)
        context->config.set_key_value("filament_ids", new ConfigOptionStrings(filament_ids));
    const auto loaded_filament_ids = context->config.option<ConfigOptionStrings>("filament_ids");
    if (loaded_filament_ids && loaded_filament_ids->values.size() > kMaxLogicalFilaments) {
        error = "The queue project has too many filament ids.";
        return nullptr;
    }
    if (const auto filament_map = context->config.option<ConfigOptionInts>("filament_map"); filament_map != nullptr) {
        if (filament_map->values.size() > kMaxLogicalFilaments ||
            std::any_of(filament_map->values.begin(), filament_map->values.end(), [](int value) { return value < 0 || value > 8; })) {
            error = "The queue project filament mapping is invalid.";
            return nullptr;
        }
    }
    for (FilamentInfo& filament : context->plate_data->slice_filaments_info) {
        if (filament.id >= 0 && static_cast<size_t>(filament.id) < filament_ids.size() && !filament_ids[filament.id].empty())
            filament.filament_id = filament_ids[filament.id];
    }

    context->plate_data->config = context->config;
    if (!parser.nozzle_diameters.empty()) {
        auto nozzle_diameter = new ConfigOptionFloatsNullable();
        nozzle_diameter->values = std::move(parser.nozzle_diameters);
        context->config.set_key_value("nozzle_diameter", nozzle_diameter);
        context->plate_data->config.set_key_value("nozzle_diameter", nozzle_diameter->clone());

        const ConfigOptionDef* nozzle_volume_type_def = print_config_def.get("nozzle_volume_type");
        if (!nozzle_volume_type_def || !nozzle_volume_type_def->enum_keys_map) {
            error = "The queue nozzle configuration is unavailable.";
            return nullptr;
        }
        auto nozzle_volume_type = new ConfigOptionEnumsGeneric(nozzle_volume_type_def->enum_keys_map);
        nozzle_volume_type->values = std::move(parser.nozzle_volume_types);
        context->config.set_key_value("nozzle_volume_type", nozzle_volume_type);
        context->plate_data->config.set_key_value("nozzle_volume_type", nozzle_volume_type->clone());
    }
    if (!context->plate_data->filament_maps.empty()) {
        context->config.set_key_value("filament_map", new ConfigOptionInts(context->plate_data->filament_maps));
        context->plate_data->config.set_key_value("filament_map", new ConfigOptionInts(context->plate_data->filament_maps));
    }
    if (parser.enable_dynamic_nozzle_map.has_value()) {
        context->config.set_key_value("enable_filament_dynamic_map", new ConfigOptionBool(*parser.enable_dynamic_nozzle_map));
        context->plate_data->config.set_key_value("enable_filament_dynamic_map", new ConfigOptionBool(*parser.enable_dynamic_nozzle_map));
    }
    if (parser.has_filament_switcher.has_value()) {
        context->config.set_key_value("has_filament_switcher", new ConfigOptionBool(*parser.has_filament_switcher));
        context->plate_data->config.set_key_value("has_filament_switcher", new ConfigOptionBool(*parser.has_filament_switcher));
    }
    context->queue_plate_id = request.queue_plate_id;
    context->model_id = request.model_id;
    context->profile_id = request.profile_id;
    context->device_id = request.device_id;
    context->title = request.title;
    context->cover_url = request.cover_url;
    BOOST_LOG_TRIVIAL(info) << "QueuePrintController: built queue context, queue_plate_index="
                            << context->queue_plate_index
                            << ", cloud_plate_index=" << context->cloud_plate_index
                            << ", filaments=" << context->plate_data->slice_filaments_info.size()
                            << ", nozzles=" << context->plate_data->nozzles_info.size()
                            << ", filament_change_seq=" << context->plate_data->filament_change_sequence.size()
                            << ", nozzle_change_seq=" << context->plate_data->nozzle_change_sequence.size();
    return context;
}

// Same layout as Metadata/filament_sequence.json in 3mf:
//   { "plate_N": { "filament_sequence"|"sequence": [1-based...], "nozzle_sequence": [...], ... } }
bool apply_filament_sequence_json(PlateData& plate, int plate_index_1based, const std::string& json_text)
{
    if (json_text.empty() || plate_index_1based < 1)
        return false;

    const nlohmann::json root = nlohmann::json::parse(json_text, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        BOOST_LOG_TRIVIAL(warning) << "QueuePrintController: filament_sequence.json is invalid";
        return false;
    }

    const std::string plate_key = "plate_" + std::to_string(plate_index_1based);
    const auto plate_it = root.find(plate_key);
    if (plate_it == root.end() || !plate_it->is_object()) {
        BOOST_LOG_TRIVIAL(warning) << "QueuePrintController: filament_sequence.json missing " << plate_key;
        return false;
    }

    const char* filament_key = plate_it->contains("filament_sequence") ? "filament_sequence" : "sequence";
    const auto filament_it = plate_it->find(filament_key);
    const auto nozzle_it = plate_it->find("nozzle_sequence");
    if (filament_it == plate_it->end() || !filament_it->is_array() ||
        nozzle_it == plate_it->end() || !nozzle_it->is_array()) {
        BOOST_LOG_TRIVIAL(warning) << "QueuePrintController: filament_sequence.json sequences missing for " << plate_key;
        return false;
    }
    if (filament_it->size() > kMaxFilamentChangeSequence || nozzle_it->size() > kMaxFilamentChangeSequence) {
        BOOST_LOG_TRIVIAL(warning) << "QueuePrintController: filament_sequence.json sequences too large for " << plate_key;
        return false;
    }

    std::vector<unsigned int> filament_sequence;
    filament_sequence.reserve(filament_it->size());
    for (const auto& item : *filament_it) {
        if (!item.is_number_unsigned() && !item.is_number_integer())
            return false;
        const int id = item.get<int>();
        if (id < 1 || id > static_cast<int>(kMaxLogicalFilaments))
            return false;
        filament_sequence.push_back(static_cast<unsigned int>(id - 1));
    }

    std::vector<unsigned int> nozzle_sequence;
    nozzle_sequence.reserve(nozzle_it->size());
    for (const auto& item : *nozzle_it) {
        if (!item.is_number_unsigned() && !item.is_number_integer())
            return false;
        const int id = item.get<int>();
        if (id < 0 || id > 255)
            return false;
        nozzle_sequence.push_back(static_cast<unsigned int>(id));
    }

    plate.filament_change_sequence = std::move(filament_sequence);
    plate.nozzle_change_sequence = std::move(nozzle_sequence);

    const auto optimal_it = plate_it->find("optimal_assignment");
    if (optimal_it != plate_it->end() && optimal_it->is_array()) {
        std::vector<int> optimal_assignment;
        optimal_assignment.reserve(optimal_it->size());
        for (const auto& item : *optimal_it) {
            if (!item.is_number_integer() && !item.is_number_unsigned())
                break;
            optimal_assignment.push_back(item.get<int>());
        }
        if (optimal_assignment.size() == optimal_it->size())
            plate.optimal_assignment = std::move(optimal_assignment);
    }

    BOOST_LOG_TRIVIAL(info) << "QueuePrintController: applied filament_sequence.json for " << plate_key
                            << ", filament_change_seq=" << plate.filament_change_sequence.size()
                            << ", nozzle_change_seq=" << plate.nozzle_change_sequence.size();
    return true;
}

std::string json_string(const nlohmann::json& value, const char* key)
{
    const auto it = value.find(key);
    if (it == value.end())
        return {};
    return it->is_string() ? it->get<std::string>() : std::string();
}

bool find_profile_data(const nlohmann::json& root, const QueuePrintRequest& request,
                       std::string& slice_url, std::string& settings_url,
                       std::string& sequence_url, std::string& title)
{
    auto extension = root.find("extension");
    if (extension == root.end())
        extension = root.find("extention"); // Service response keeps this legacy spelling.
    if (extension == root.end() || !extension->is_object()) return false;
    const auto model_info = extension->find("modelInfo");
    if (model_info == extension->end() || !model_info->is_object()) return false;
    const auto configs = model_info->find("configs");
    if (configs == model_info->end() || !configs->is_array()) return false;
    for (const auto& config : *configs) {
        if (!config.is_object()) continue;
        const std::string name = json_string(config, "name");
        if (name == "slice_info.config") {
            if (!slice_url.empty()) return false;
            slice_url = json_string(config, "url");
        }
        if (name == "project_settings.config") {
            if (!settings_url.empty()) return false;
            settings_url = json_string(config, "url");
        }
        // Optional: same file as 3mf Metadata/filament_sequence.json. Name may be bare or path-like.
        if (name == "filament_sequence.json" || name == "Metadata/filament_sequence.json") {
            if (sequence_url.empty())
                sequence_url = json_string(config, "url");
        }
    }
    BOOST_LOG_TRIVIAL(info) << "QueuePrintController: profile configs resolved, queue_plate_index="
                            << request.plate_index
                            << ", slice_url_present=" << !slice_url.empty()
                            << ", settings_url_present=" << !settings_url.empty()
                            << ", sequence_url_present=" << !sequence_url.empty();
    const auto plates = model_info->find("plates");
    if (plates == model_info->end() || !plates->is_array()) return false;
    const int64_t queue_plate_index = request.plate_index;
    const nlohmann::json* legacy_zero_based_plate = nullptr;
    for (const auto& plate : *plates) {
        if (!plate.is_object()) continue;
        const auto index = plate.find("index");
        if (index == plate.end()) continue;
        int64_t profile_plate_index = -1;
        if (index->is_number_integer()) {
            profile_plate_index = index->get<int64_t>();
        } else if (index->is_number_unsigned()) {
            const uint64_t value = index->get<uint64_t>();
            if (value > static_cast<uint64_t>(kMaxQueuePlateIndex + 1)) continue;
            profile_plate_index = static_cast<int64_t>(value);
        } else {
            continue;
        }
        BOOST_LOG_TRIVIAL(info) << "QueuePrintController: profile plate index=" << profile_plate_index
                                << ", queue_plate_index=" << queue_plate_index;
        if (profile_plate_index == queue_plate_index) {
            title = json_string(plate, "title");
            if (title.empty()) title = json_string(plate, "plateName");
            if (title.empty()) title = json_string(plate, "name");
            return !slice_url.empty() && !settings_url.empty();
        }
        if (profile_plate_index == queue_plate_index - 1)
            legacy_zero_based_plate = &plate;
    }
    // Older profiles may report a zero-based index, while queue records and
    // slice_info.config use the one-based task index.
    if (legacy_zero_based_plate) {
        BOOST_LOG_TRIVIAL(info) << "QueuePrintController: using legacy zero-based profile plate index, queue_plate_index="
                                << queue_plate_index;
        title = json_string(*legacy_zero_based_plate, "title");
        if (title.empty()) title = json_string(*legacy_zero_based_plate, "plateName");
        if (title.empty()) title = json_string(*legacy_zero_based_plate, "name");
        return !slice_url.empty() && !settings_url.empty();
    }
    return false;
}

} // namespace

QueuePrintController& QueuePrintController::Get()
{
    static QueuePrintController controller;
    return controller;
}

void QueuePrintController::PrepareAndShow(QueuePrintRequest request, Completion completion)
{
    std::weak_ptr<int> alive = m_alive;
    std::thread([request = std::move(request), completion = std::move(completion), alive]() mutable {
        std::string response;
        int64_t profile_id = 0;
        if (alive.expired() || !wxTheApp) return;
        BOOST_LOG_TRIVIAL(info) << "QueuePrintController: preparing queue plate, queue_plate_index="
                                << request.plate_index
                                << ", cover_url_present=" << !request.cover_url.empty();
        int result = is_decimal_id(request.queue_plate_id) && is_decimal_id(request.profile_id) && !request.model_id.empty() &&
                     !request.device_id.empty() && request.plate_index >= 1 && request.plate_index <= kMaxQueuePlateIndex &&
                     parse_positive_int64(request.profile_id, profile_id)
            ? PrintQueueManager::Get().get_model_profile(profile_id, request.model_id, response)
            : BAMBU_NETWORK_ERR_INVALID_HANDLE;
        std::string error = "Unable to load the queue project.";
        std::shared_ptr<QueuePrintContext> context;
        if (result == BAMBU_NETWORK_SUCCESS) {
            BOOST_LOG_TRIVIAL(info) << "QueuePrintController: profile request succeeded, response_size=" << response.size();
            const nlohmann::json profile = nlohmann::json::parse(response, nullptr, false);
            std::string slice_url;
            std::string settings_url;
            std::string sequence_url;
            std::string title;
            if (!profile.is_discarded() && find_profile_data(profile, request, slice_url, settings_url, sequence_url, title)) {
                NetworkAgent* agent = wxGetApp().getAgent();
                std::string slice_info;
                std::string project_settings;
                const int slice_result = agent ? agent->download_print_queue_config({slice_url}, &slice_info)
                                               : BAMBU_NETWORK_ERR_INVALID_HANDLE;
                const int settings_result = slice_result == BAMBU_NETWORK_SUCCESS && agent
                    ? agent->download_print_queue_config({settings_url}, &project_settings)
                    : BAMBU_NETWORK_ERR_INVALID_HANDLE;
                BOOST_LOG_TRIVIAL(info) << "QueuePrintController: config downloads finished, slice_result=" << slice_result
                                        << ", slice_size=" << slice_info.size()
                                        << ", settings_result=" << settings_result
                                        << ", settings_size=" << project_settings.size();
                if (slice_result == BAMBU_NETWORK_SUCCESS && settings_result == BAMBU_NETWORK_SUCCESS) {
                    if (!title.empty()) request.title = title;
                    context = parse_context(request, slice_info, project_settings, error);
                    if (context && !sequence_url.empty() && agent) {
                        std::string sequence_json;
                        const int sequence_result = agent->download_print_queue_config({sequence_url}, &sequence_json);
                        BOOST_LOG_TRIVIAL(info) << "QueuePrintController: filament_sequence download finished, result="
                                                << sequence_result << ", size=" << sequence_json.size();
                        if (sequence_result == BAMBU_NETWORK_SUCCESS) {
                            if (!apply_filament_sequence_json(*context->plate_data, context->cloud_plate_index, sequence_json)) {
                                BOOST_LOG_TRIVIAL(warning) << "QueuePrintController: failed to apply filament_sequence.json";
                            }
                        }
                    } else if (context) {
                        BOOST_LOG_TRIVIAL(warning) << "QueuePrintController: filament_sequence.json not listed in profile configs; "
                                                   << "change sequences remain empty";
                    }
                    if (context && !context->cover_url.empty()) {
                        std::string thumbnail;
                        const int thumbnail_result = agent->download_print_queue_config({context->cover_url}, &thumbnail);
                        if (thumbnail_result == BAMBU_NETWORK_SUCCESS && !thumbnail.empty()) {
                            context->plate_data->plate_thumbnail.pixels.assign(thumbnail.begin(), thumbnail.end());
                        }
                        BOOST_LOG_TRIVIAL(info) << "QueuePrintController: thumbnail download finished, result=" << thumbnail_result
                                                << ", size=" << thumbnail.size();
                    }
                } else {
                    error = "Unable to download the queue print settings.";
                }
            } else {
                error = "The queue project does not contain the selected plate configuration.";
            }
        } else {
            BOOST_LOG_TRIVIAL(warning) << "QueuePrintController: profile request failed, result=" << result
                                      << ", response_size=" << response.size();
        }
        if (alive.expired() || !wxTheApp) return;
        wxApp* app = wxTheApp;
        app->CallAfter([context = std::move(context), completion = std::move(completion), error = std::move(error), alive]() mutable {
            if (alive.expired() || !wxTheApp) return;
            if (context) {
                if (wxGetApp().plater()->show_print_queue_dialog(context, [&completion]() {
                    if (completion) completion(BAMBU_NETWORK_SUCCESS, {});
                })) {
                } else if (completion) {
                    completion(BAMBU_NETWORK_ERR_INVALID_HANDLE, "Unable to open the queue print dialog.");
                }
            } else {
                BOOST_LOG_TRIVIAL(warning) << "Queue print preparation failed: " << error;
                if (completion) completion(BAMBU_NETWORK_ERR_INVALID_RESULT, error);
            }
        });
    }).detach();
}

}} // namespace Slic3r::GUI
