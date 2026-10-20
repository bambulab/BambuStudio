#include <catch2/catch.hpp>

#include "libslic3r/Print.hpp"

using namespace Slic3r;

namespace {

ModelObject *add_filename_test_object(Model &model, const std::string &name, ModelInstanceEPrintVolumeState state)
{
    ModelObject *object = model.add_object();
    object->name = name;
    object->add_volume(make_cube(10.0, 10.0, 10.0));
    object->add_instance()->print_volume_state = state;
    object->ensure_on_bed();
    return object;
}

void apply_filename_test_model(Print &print, Model &model, DynamicPrintConfig &config)
{
    for (ModelObject *object : model.objects)
        print.auto_assign_extruders(object);
    print.apply(model, config);
}

} // namespace

TEST_CASE("Print filename exposes the first printable object name", "[Print][output_filename]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set("filename_format", "[first_object_name]");

    Print print;
    Model model;
    add_filename_test_object(model, "Outside", ModelInstancePVS_Fully_Outside);
    add_filename_test_object(model, "Bracket", ModelInstancePVS_Inside);
    add_filename_test_object(model, "Later", ModelInstancePVS_Inside);
    apply_filename_test_model(print, model, config);

    REQUIRE(print.output_filename("SavedProject") == "Bracket.gcode");
}

TEST_CASE("First object name filename placeholder is defined without a printable object", "[Print][output_filename]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set("filename_format", "part_[first_object_name]");

    Print print;
    Model model;
    add_filename_test_object(model, "Outside", ModelInstancePVS_Fully_Outside);
    apply_filename_test_model(print, model, config);

    REQUIRE(print.output_filename() == "part_.gcode");
}
