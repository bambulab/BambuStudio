#include "Print.hpp"

#include "PlaceholderParser.hpp"
#include "Time.hpp"

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>

namespace Slic3r {

DynamicConfig PrintStatistics::config() const
{
    DynamicConfig config;
    std::string   normal_print_time = short_time(this->estimated_normal_print_time);
    std::string   silent_print_time = short_time(this->estimated_silent_print_time);
    config.set_key_value("print_time", new ConfigOptionString(normal_print_time));
    config.set_key_value("normal_print_time", new ConfigOptionString(normal_print_time));
    config.set_key_value("silent_print_time", new ConfigOptionString(silent_print_time));
    config.set_key_value("used_filament", new ConfigOptionFloat(this->total_used_filament / 1000.));
    config.set_key_value("extruded_volume", new ConfigOptionFloat(this->total_extruded_volume));
    config.set_key_value("total_cost", new ConfigOptionFloat(this->total_cost));
    config.set_key_value("total_toolchanges", new ConfigOptionInt(this->total_toolchanges));
    config.set_key_value("total_weight", new ConfigOptionFloat(this->total_weight));
    config.set_key_value("total_wipe_tower_cost", new ConfigOptionFloat(this->total_wipe_tower_cost));
    config.set_key_value("total_wipe_tower_filament", new ConfigOptionFloat(this->total_wipe_tower_filament));
    config.set_key_value("initial_tool", new ConfigOptionInt(static_cast<int>(this->initial_tool)));
    return config;
}

DynamicConfig PrintStatistics::placeholders()
{
    DynamicConfig config;
    for (const std::string &key : {
             "print_time", "normal_print_time", "silent_print_time",
             "used_filament", "extruded_volume", "total_cost", "total_weight",
             "intial_tool", "total_toolchanges", "total_wipe_tower_cost", "total_wipe_tower_filament" })
        config.set_key_value(key, new ConfigOptionString(std::string("{") + key + "}"));
    return config;
}

std::string PrintStatistics::finalize_output_path(const std::string &path_in) const
{
    std::string final_path;
    try {
        boost::filesystem::path path(path_in);
        DynamicConfig           cfg = this->config();
        PlaceholderParser       pp;
        std::string             new_stem = pp.process(path.stem().string(), 0, &cfg);
        final_path                       = (path.parent_path() / (new_stem + path.extension().string())).string();
    } catch (const std::exception &ex) {
        BOOST_LOG_TRIVIAL(error) << "Failed to apply the print statistics to the export file name: " << ex.what();
        final_path = path_in;
    }
    return final_path;
}

} // namespace Slic3r
