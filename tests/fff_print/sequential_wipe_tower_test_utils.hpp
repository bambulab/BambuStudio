#ifndef SLIC3R_SEQUENTIAL_WIPE_TOWER_TEST_UTILS_HPP
#define SLIC3R_SEQUENTIAL_WIPE_TOWER_TEST_UTILS_HPP

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <boost/filesystem.hpp>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>

namespace Slic3r { namespace Test { namespace SequentialTower {

inline std::string gcode(Print &print, GCodeProcessorResult *result = nullptr)
{
    // test_data::gcode() passes a bare filename. Bambu's exporter requires
    // a nonempty parent path, even when the current directory is writable.
    struct ExportDirectory {
        boost::filesystem::path path = boost::filesystem::temp_directory_path() /
            boost::filesystem::unique_path("sequential-wipe-tower-%%%%-%%%%-%%%%");

        ExportDirectory() { boost::filesystem::create_directory(path); }
        ~ExportDirectory()
        {
            boost::system::error_code ignored;
            boost::filesystem::remove_all(path, ignored);
        }
    } directory;

    // do_export() changes this process-wide flag. Restore it even if export
    // throws, so exporting one fixture cannot change the next fixture's setup.
    struct ProcessorState {
        bool is_bbl_printer = GCodeProcessor::s_IsBBLPrinter;
        ~ProcessorState() { GCodeProcessor::s_IsBBLPrinter = is_bbl_printer; }
    } processor_state;

    print.set_status_silent();
    print.process();
    const auto groups = print.get_layered_nozzle_group_result();
    if (!groups)
        throw std::runtime_error("Sliced test print has no layered nozzle group result");
    for (unsigned int filament : print.extruders())
        if (!groups->get_first_nozzle_for_filament(filament))
            throw std::runtime_error("Sliced test print has no nozzle for filament " + std::to_string(filament));

    // Bambu's do_export() writes label_object_enabled and
    // support_material_on_wipe_tower after its guarded result extraction.
    // Even a caller interested only in text must supply a result object.
    GCodeProcessorResult local_result;
    const std::string path = print.export_gcode((directory.path / "test.gcode").string(),
                                               result ? result : &local_result, nullptr);
    std::ifstream file(path);
    if (!file.is_open())
        throw std::runtime_error("Cannot read exported test G-code: " + path);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

inline Polygon rectangle(double x0, double y0, double x1, double y1)
{
    return Polygon{Points{Point(scale_(x0), scale_(y0)), Point(scale_(x1), scale_(y0)),
                          Point(scale_(x1), scale_(y1)), Point(scale_(x0), scale_(y1))}};
}

enum class Printer { H2D, P1S };

inline DynamicPrintConfig config(PrintSequence sequence = PrintSequence::ByObject, Printer printer = Printer::H2D)
{
    DynamicPrintConfig c;
    const std::string path = std::string(TEST_DATA_DIR) + "/sequential_wipe_tower/" +
        (printer == Printer::H2D ? "h2d_by_object.json" : "p1s_by_object.json");
    ConfigSubstitutionContext substitutions(ForwardCompatibilitySubstitutionRule::Enable);
    std::map<std::string, std::string> metadata;
    std::string reason;
    // Use the same status-returning loader as the Bambu 3MF project importer.
    if (c.load_from_json(path, substitutions, true, metadata, reason) != 0)
        throw std::runtime_error("Cannot load sequential tower fixture " + path + ": " + reason);

    // Supply defaults only for settings absent from the 02.09.01.70 exports.
    // Both JSON loading and option(key, true) create missing options through
    // ConfigOptionDef::create_default_option(), including enum keys_map for
    // nullable enums. No manual enum dictionary repair is necessary.
    // No filament, extruder or variant vectors from the export are resized.
    for (const std::string &key : FullPrintConfig::defaults().keys())
        c.option(key, true);

    c.set_key_value("print_sequence", new ConfigOptionEnum<PrintSequence>(sequence));
    return c;
}

inline ModelObject *add_object(Model &model, bool multicolour, const Vec3d &position)
{
    ModelObject *o = model.add_object();
    o->name = "object_" + std::to_string(model.objects.size());
    // Adjacent 10 x 10 x 2 mm volumes force both filaments on every layer.
    TriangleMesh cube = make_cube(10., 10., 2.);
    o->add_volume(cube);
    o->volumes.back()->config.set_key_value("extruder", new ConfigOptionInt(1));
    if (multicolour) {
        cube.translate(10.f, 0.f, 0.f);
        o->add_volume(cube);
        o->volumes.back()->config.set_key_value("extruder", new ConfigOptionInt(2));
    }
    o->config.set_key_value("extruder", new ConfigOptionInt(1));
    o->add_instance()->set_offset(position);
    o->instances.back()->arrange_order = static_cast<int>(model.objects.size());
    return o;
}

inline void apply(Print &print, Model &model, const DynamicPrintConfig &c)
{
    // Print() leaves its Eigen plate origin uninitialized. PartPlate sets it
    // in the app; these standalone fixtures must establish the same contract.
    print.set_plate_index(0);
    print.set_plate_origin(Vec3d::Zero());
    for (ModelObject *o : model.objects) {
        o->ensure_on_bed();
        print.auto_assign_extruders(o);
    }
    print.apply(model, c);
    print.set_status_silent();
    print.set_no_check_flag(true);
}

inline void exclude(DynamicPrintConfig &c, const Polygon &p)
{
    auto *areas = c.option<ConfigOptionPoints>("bed_exclude_area");
    for (const Point &pt : p.points)
        areas->values.emplace_back(unscale(pt));
}

} } } // namespace Slic3r::Test::SequentialTower
#endif
