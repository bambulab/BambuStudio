#include "Print.hpp"
#include "I18N.hpp"
#include "Utils.hpp"

#include <boost/filesystem/path.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>

#include "nlohmann/json.hpp"

#include <fstream>
#include <iomanip>
#include <unordered_map>
#include <unordered_set>

using namespace nlohmann;

// Mark string for localization and translate.
#define L(s) Slic3r::I18N::translate(s)

namespace Slic3r {

FilamentCompatibilityType Print::check_multi_filaments_compatibility(const std::vector<std::string>& filament_types)
{
    bool has_high_temperature_filament = false;
    bool has_low_temperature_filament = false;
    bool has_mid_temperature_filament = false;

    for (const auto& type : filament_types) {
        if (get_filament_temp_type(type) == FilamentTempType::HighTemp)
            has_high_temperature_filament = true;
        else if (get_filament_temp_type(type) == FilamentTempType::LowTemp)
            has_low_temperature_filament = true;
        else if (get_filament_temp_type(type) == FilamentTempType::HighLowCompatible)
            has_mid_temperature_filament = true;
    }

    if (has_high_temperature_filament && has_low_temperature_filament)
        return FilamentCompatibilityType::HighLowMixed;
    else if (has_high_temperature_filament && has_mid_temperature_filament)
        return FilamentCompatibilityType::HighMidMixed;
    else if (has_low_temperature_filament && has_mid_temperature_filament)
        return FilamentCompatibilityType::LowMidMixed;
    else
        return FilamentCompatibilityType::Compatible;
}

bool Print::is_filaments_compatible(const std::vector<int>& filament_types)
{
    bool has_high_temperature_filament = false;
    bool has_low_temperature_filament = false;

    for (const auto& type : filament_types) {
        if (type == FilamentTempType::HighTemp)
            has_high_temperature_filament = true;
        else if (type == FilamentTempType::LowTemp)
            has_low_temperature_filament = true;
    }

    if (has_high_temperature_filament && has_low_temperature_filament)
        return false;

    return true;
}

int Print::get_compatible_filament_type(const std::set<int>& filament_types)
{
    bool has_high_temperature_filament = false;
    bool has_low_temperature_filament = false;

    for (const auto& type : filament_types) {
        if (type == FilamentTempType::HighTemp)
            has_high_temperature_filament = true;
        else if (type == FilamentTempType::LowTemp)
            has_low_temperature_filament = true;
    }

    if (has_high_temperature_filament && has_low_temperature_filament)
        return HighLowCompatible;
    else if (has_high_temperature_filament)
        return HighTemp;
    else if (has_low_temperature_filament)
        return LowTemp;
    return HighLowCompatible;
}

StringObjectException Print::check_multi_filament_valid(const Print& print)
{
    auto print_config = print.config();
    if (print_config.print_sequence == PrintSequence::ByObject) {
        std::set<FilamentCompatibilityType> compatibility_each_obj;
        bool enable_mix_printing = !print.need_check_multi_filaments_compatibility();

        for (const auto &objectID_t : print.print_object_ids()) {
            std::set<unsigned int> obj_used_extruder_ids;
            auto print_object = print.get_object(objectID_t);
            if (print_object) {
                auto object_extruders_t = print_object->object_extruders();
                for (unsigned int extruder : object_extruders_t)
                    obj_used_extruder_ids.insert(extruder);
            }

            if (print_object->has_support_material()) {
                auto num_extruders = (unsigned int) print_config.filament_diameter.size();
                assert(print_object->config().support_filament >= 0);
                if (print_object->config().support_filament >= 1 && (unsigned int) print_object->config().support_filament < num_extruders + 1)
                    obj_used_extruder_ids.insert((unsigned int) print_object->config().support_filament - 1);
                assert(print_object->config().support_interface_filament >= 0);
                if (print_object->config().support_interface_filament >= 1 && (unsigned int) print_object->config().support_interface_filament < num_extruders + 1)
                    obj_used_extruder_ids.insert((unsigned int) print_object->config().support_interface_filament - 1);
            }

            std::vector<std::string> filament_types;
            filament_types.reserve(obj_used_extruder_ids.size());
            for (const auto &extruder_idx : obj_used_extruder_ids)
                filament_types.push_back(print_config.filament_type.get_at(extruder_idx));

            auto compatibility = check_multi_filaments_compatibility(filament_types);
            compatibility_each_obj.insert(compatibility);
        }

        StringObjectException ret;
        std::string hypertext = "filament_mix_print";
        if (compatibility_each_obj.count(FilamentCompatibilityType::HighLowMixed)) {
            if (enable_mix_printing) {
                ret.string     = L("Printing high-temp and low-temp filaments together may cause nozzle clogging or printer damage.");
                ret.is_warning = true;
                ret.hypetext   = hypertext;
            } else {
                ret.string = L("Printing high-temp and low-temp filaments together may cause nozzle clogging or printer damage. If you still want to print, you can enable the option in Preferences.");
            }
        } else if (compatibility_each_obj.count(FilamentCompatibilityType::LowMidMixed) || compatibility_each_obj.count(FilamentCompatibilityType::HighMidMixed)) {
            ret.is_warning = true;
            ret.hypetext   = hypertext;
            ret.string     = L("Printing different-temp filaments together may cause nozzle clogging or printer damage.");
        }
        return ret;
    }

    std::vector<unsigned int> extruders = print.extruders();
    std::vector<std::string> filament_types;
    filament_types.reserve(extruders.size());
    for (const auto& extruder_idx : extruders)
        filament_types.push_back(print_config.filament_type.get_at(extruder_idx));

    auto compatibility = check_multi_filaments_compatibility(filament_types);
    bool enable_mix_printing = !print.need_check_multi_filaments_compatibility();

    StringObjectException ret;
    std::string hypertext = "filament_mix_print";

    if (compatibility == FilamentCompatibilityType::HighLowMixed) {
        if (enable_mix_printing) {
            ret.string = L("Printing high-temp and low-temp filaments together may cause nozzle clogging or printer damage.");
            ret.is_warning = true;
            ret.hypetext   = hypertext;
        } else {
            ret.string = L("Printing high-temp and low-temp filaments together may cause nozzle clogging or printer damage. If you still want to print, you can enable the option in Preferences.");
        }
    } else if (compatibility == FilamentCompatibilityType::HighMidMixed) {
        ret.is_warning = true;
        ret.hypetext = hypertext;
        ret.string = L("Printing high-temp and mid-temp filaments together may cause nozzle clogging or printer damage.");
    } else if (compatibility == FilamentCompatibilityType::LowMidMixed) {
        ret.is_warning = true;
        ret.hypetext = hypertext;
        ret.string = L("Printing mid-temp and low-temp filaments together may cause nozzle clogging or printer damage.");
    }

    return ret;
}

FilamentTempType Print::get_filament_temp_type(const std::string& filament_type)
{
    const static std::string HighTempFilamentStr = "high_temp_filament";
    const static std::string LowTempFilamentStr = "low_temp_filament";
    const static std::string HighLowCompatibleFilamentStr = "high_low_compatible_filament";
    static std::unordered_map<std::string, std::unordered_set<std::string>> filament_temp_type_map;

    if (filament_temp_type_map.empty()) {
        fs::path file_path = fs::path(resources_dir()) / "info" / "filament_info.json";
        std::ifstream in(file_path.string());
        json j;
        try {
            j = json::parse(in);
            in.close();
            auto&& high_temp_filament_arr = j[HighTempFilamentStr].get<std::vector<std::string>>();
            filament_temp_type_map[HighTempFilamentStr] = std::unordered_set<std::string>(high_temp_filament_arr.begin(), high_temp_filament_arr.end());
            auto&& low_temp_filament_arr = j[LowTempFilamentStr].get<std::vector<std::string>>();
            filament_temp_type_map[LowTempFilamentStr] = std::unordered_set<std::string>(low_temp_filament_arr.begin(), low_temp_filament_arr.end());
            auto&& high_low_compatible_filament_arr = j[HighLowCompatibleFilamentStr].get<std::vector<std::string>>();
            filament_temp_type_map[HighLowCompatibleFilamentStr] = std::unordered_set<std::string>(high_low_compatible_filament_arr.begin(), high_low_compatible_filament_arr.end());
        } catch (const json::parse_error& err) {
            in.close();
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": parse " << file_path.string() << " got a nlohmann::detail::parse_error, reason = " << err.what();
            filament_temp_type_map[HighTempFilamentStr] = {"ABS","ASA","PC","PA","PA-CF","PA-GF","PA6-CF","PET-CF","PPS","PPS-CF","PPA-GF","PPA-CF","ABS-Aero","ABS-GF"};
            filament_temp_type_map[LowTempFilamentStr] = {"PLA","TPU","PLA-CF","PLA-AERO","PVA","BVOH"};
            filament_temp_type_map[HighLowCompatibleFilamentStr] = {"HIPS","PETG","PCTG","PE","PP","EVA","PE-CF","PP-CF","PP-GF","PHA"};
        }
    }

    if (filament_temp_type_map[HighLowCompatibleFilamentStr].find(filament_type) != filament_temp_type_map[HighLowCompatibleFilamentStr].end())
        return HighLowCompatible;
    if (filament_temp_type_map[HighTempFilamentStr].find(filament_type) != filament_temp_type_map[HighTempFilamentStr].end())
        return HighTemp;
    if (filament_temp_type_map[LowTempFilamentStr].find(filament_type) != filament_temp_type_map[LowTempFilamentStr].end())
        return LowTemp;
    return Undefine;
}

int Print::get_hrc_by_nozzle_type(const NozzleType&type)
{
    static std::map<std::string, int> nozzle_type_to_hrc;
    if (nozzle_type_to_hrc.empty()) {
        fs::path file_path = fs::path(resources_dir()) / "info" / "nozzle_info.json";
        boost::nowide::ifstream in(file_path.string());
        json j;
        try {
            j = json::parse(in);
            in.close();
            for (const auto& elem : j["nozzle_hrc"].items())
                nozzle_type_to_hrc[elem.key()] = elem.value();
        } catch (const json::parse_error& err) {
            in.close();
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": parse " << file_path.string() << " got a nlohmann::detail::parse_error, reason = " << err.what();
            nozzle_type_to_hrc = {
                {"hardened_steel", 55},
                {"stainless_steel", 20},
                {"tungsten_carbide", 85},
                {"brass", 2},
                {"undefine", 0}
            };
        }
    }

    auto iter = nozzle_type_to_hrc.find(NozzleTypeEumnToStr[type]);
    if (iter != nozzle_type_to_hrc.end())
        return iter->second;
    return 0;
}

std::vector<std::string> Print::get_incompatible_filaments_by_nozzle(const float nozzle_diameter, const std::optional<NozzleVolumeType> nozzle_volume_type)
{
    static std::map<std::string, std::map<std::string, std::vector<std::string>>> incompatible_filaments;
    if (incompatible_filaments.empty()) {
        fs::path file_path = fs::path(resources_dir()) / "info" / "nozzle_incompatibles.json";
        boost::nowide::ifstream in(file_path.string());
        json j;
        try {
            j = json::parse(in);
            for (auto& [volume_type, diameter_list] : j["incompatible_nozzles"].items()) {
                for (auto& [diameter, filaments] : diameter_list.items())
                    incompatible_filaments[volume_type][diameter] = filaments.get<std::vector<std::string>>();
            }
        } catch (const json::parse_error& err) {
            in.close();
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": parse " << file_path.string() << " got a nlohmann::detail::parse_error, reason = " << err.what();
            incompatible_filaments[get_nozzle_volume_type_string(NozzleVolumeType::nvtHighFlow)] = {};
            incompatible_filaments[get_nozzle_volume_type_string(NozzleVolumeType::nvtStandard)] = {};
        }
    }

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << nozzle_diameter;
    std::string diameter_str = oss.str();

    if (nozzle_volume_type.has_value())
        return incompatible_filaments[get_nozzle_volume_type_string(nozzle_volume_type.value())][diameter_str];

    std::vector<std::string> incompatible_filaments_list;
    for (auto& [volume_type, diameter_list] : incompatible_filaments) {
        auto iter = diameter_list.find(diameter_str);
        if (iter != diameter_list.end())
            append(incompatible_filaments_list, iter->second);
    }
    return incompatible_filaments_list;
}

} // namespace Slic3r
