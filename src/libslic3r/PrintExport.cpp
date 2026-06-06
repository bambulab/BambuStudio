#include "Print.hpp"

#include "GCode.hpp"
#include "I18N.hpp"

#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>

#define L(s) Slic3r::I18N::translate(s)

namespace Slic3r {

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

} // namespace Slic3r
