#include "Print.hpp"

#include "GCode.hpp"
#include "I18N.hpp"
#include "Time.hpp"

#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>

#define L(s) Slic3r::I18N::translate(s)

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

std::string Print::export_gcode(const std::string &path_template, GCodeProcessorResult *result, ThumbnailsGeneratorCallback thumbnail_cb)
{
    std::string path = this->output_filepath(path_template);
    std::string message;
    if (! path.empty() && result == nullptr) {
        message = L("Exporting G-code");
        message += " to ";
        message += path;
    } else
        message = L("Generating G-code");
    this->set_status(80, message);

    GCode       gcode;
    const Vec3d origin = this->get_plate_origin();
    gcode.set_gcode_offset(origin(0), origin(1));
    gcode.do_export(this, path.c_str(), result, thumbnail_cb);
    gcode.export_layer_filaments(result);
    if (result != nullptr) {
        result->conflict_result     = m_conflict_result;
        result->nozzle_group_result = this->get_layered_nozzle_group_result();
    }
    return path.c_str();
}

std::string Print::output_filename(const std::string &filename_base) const
{
    DynamicConfig config = this->finished() ? this->print_statistics().config() : this->print_statistics().placeholders();
    config.set_key_value("num_filaments", new ConfigOptionInt((int) m_config.nozzle_diameter.size()));
    config.set_key_value("plate_name", new ConfigOptionString(get_plate_name()));

    return this->PrintBase::output_filename(m_config.filename_format.value, ".gcode", filename_base, &config);
}

void Print::export_gcode_from_previous_file(const std::string &file, GCodeProcessorResult *result, ThumbnailsGeneratorCallback thumbnail_cb)
{
    try {
        GCodeProcessor processor;
        if (result && result->nozzle_group_result)
            processor.initialize_from_context(result->nozzle_group_result);
        const Vec3d origin = this->get_plate_origin();
        processor.set_xy_offset(origin(0), origin(1));
        processor.process_file(file);

        auto filament_seq_loaded = result->filament_change_sequence;
        auto nozzle_seq_loaded   = result->nozzle_change_sequence;
        *result                  = std::move(processor.extract_result());
        result->filament_change_sequence = filament_seq_loaded;
        result->nozzle_change_sequence   = nozzle_seq_loaded;
    } catch (std::exception &) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": found errors when process gcode file " << file.c_str();
        throw Slic3r::RuntimeError(std::string("Failed to process the G-code file ") + file + " from previous 3mf\n");
    }

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": process the G-code file " << file.c_str() << " successfully";
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
