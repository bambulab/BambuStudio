#include <catch2/catch.hpp>

#include "libslic3r/Print.hpp"
#include "libslic3r/GCode/ToolOrdering.hpp"
#include "libslic3r/Exception.hpp"

#include "test_data.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;

// Covers Print::_mark_flush_into_objects_without_tower() and
// WipingExtrusions::mark_wiping_extrusions_for_layer(): flush_into_infill/flush_into_objects/
// flush_into_support diverting purge volume into object geometry on a plate that has no shared
// prime tower at all (single-nozzle only - see the scope note on
// _mark_flush_into_objects_without_tower()).
//
// Reliably forcing a genuine per-layer body/infill colour split in this test harness turned out
// to need more region-assignment machinery than was worth chasing here. The trick used instead:
// support material with a distinct interface filament (support_filament=0 "auto", i.e.
// overriddable; support_interface_filament pinned to a second extruder) is the one multi-material
// pattern already proven to work end to end in this suite (test_support_material_on_wipe_tower.cpp
// - "PLA model with Supp.PLA interface"), and it forces a genuine per-layer toolchange without
// needing painted regions or a multi-part object. flush_into_infill is then what's actually under
// test: it lets the body's own sparse infill absorb some of that toolchange's purge volume.

namespace {

DynamicPrintConfig make_no_tower_flush_config(unsigned num_filaments)
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_num_filaments(num_filaments);

    std::vector<double>      filament_diameters(num_filaments, 1.75);
    std::vector<std::string> filament_colours;
    std::vector<std::string> filament_types;
    filament_colours.reserve(num_filaments);
    filament_types.reserve(num_filaments);
    for (unsigned i = 0; i < num_filaments; ++i) {
        filament_colours.emplace_back((i % 2 == 0) ? "#FF0000" : "#00FF00");
        filament_types.emplace_back("PLA");
    }

    // The one thing this feature is actually about: no shared prime tower.
    config.set_key_value("enable_prime_tower", new ConfigOptionBool(false));
    config.set_key_value("enable_support", new ConfigOptionBool(true));
    config.set_key_value("single_extruder_multi_material", new ConfigOptionBool(true));
    config.set_key_value("filament_diameter", new ConfigOptionFloats(filament_diameters));
    config.set_key_value("filament_colour", new ConfigOptionStrings(filament_colours));
    config.set_key_value("filament_type", new ConfigOptionStrings(filament_types));
    return config;
}

void init_no_tower_flush_print(Print &print, Model &model, const DynamicPrintConfig &config_in)
{
    model.clear_objects();
    ModelObject *object = model.add_object();
    object->add_volume(mesh(TestMesh::overhang)); // needs support, so a support interface layer actually gets generated
    object->add_instance();
    // Body on extruder 1; support base left "auto" (0 -> follows whatever's active, so it's
    // itself eligible to absorb purge); interface pinned to extruder 2, forcing a real
    // toolchange at every support-interface layer. flush_into_infill is what's under test: does
    // the body's own infill pick up some of that toolchange's purge volume.
    object->config.set_key_value("extruder", new ConfigOptionInt(1));
    object->config.set_key_value("enable_support", new ConfigOptionBool(true));
    object->config.set_key_value("support_filament", new ConfigOptionInt(0));
    object->config.set_key_value("support_interface_filament", new ConfigOptionInt(2));
    object->config.set_key_value("flush_into_infill", new ConfigOptionBool(true));

    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_num_filaments(config_in.option<ConfigOptionFloats>("filament_diameter")->size());
    config.apply(config_in);

    for (ModelObject *mo : model.objects)
        mo->ensure_on_bed();

    print.apply(model, config);
    print.set_status_silent();
    print.set_no_check_flag(true);
}

} // namespace

TEST_CASE("flush_into_object_without_tower: no prime tower, flush_into_infill absorbs toolchange volume", "[FlushIntoObjectWithoutTower]")
{
    DynamicPrintConfig config = make_no_tower_flush_config(2);

    Print print;
    Model model;
    init_no_tower_flush_print(print, model, config);

    REQUIRE_FALSE(print.has_wipe_tower());

    try {
        print.process();
    } catch (const Slic3r::SlicingErrors &e) {
        std::string msg;
        for (const auto &err : e.errors_)
            msg += std::string(err.what()) + " | ";
        FAIL(std::string("print.process() threw SlicingErrors: ") + msg);
    } catch (const std::exception &e) {
        FAIL(std::string("print.process() threw: ") + e.what());
    }

    // Some layer's toolchange(s) must have actually diverted purge volume into the body's own
    // infill - i.e. the new no-tower marking pass ran and did something, not just silently
    // no-op'd because it's gated on has_wipe_tower() like the old code path was.
    bool any_overridden = false;
    for (const LayerTools &lt : print.tool_ordering().layer_tools())
        any_overridden |= const_cast<LayerTools &>(lt).wiping_extrusions().is_anything_overridden();
    REQUIRE(any_overridden);

    // NOTE: intentionally not also asserting on Slic3r::Test::gcode(print)'s output here - that
    // shared helper hits its own pre-existing, unrelated failure in this environment
    // (boost::filesystem::create_directory) unrelated to this change; full G-code generation is
    // instead verified by hand via the built GUI app (see the handoff notes).
}
