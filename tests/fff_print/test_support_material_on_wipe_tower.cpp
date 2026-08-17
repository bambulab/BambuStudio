#include <catch2/catch.hpp>

#include "libslic3r/Print.hpp"

#include "test_data.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;

namespace {

DynamicPrintConfig make_wipe_tower_support_config(unsigned num_filaments)
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_num_filaments(num_filaments);

    std::vector<double> filament_diameters(num_filaments, 1.75);
    std::vector<std::string> filament_colours;
    std::vector<std::string> filament_types;
    filament_colours.reserve(num_filaments);
    filament_types.reserve(num_filaments);
    for (unsigned i = 0; i < num_filaments; ++i) {
        filament_colours.emplace_back((i == 0) ? "#FF0000" : "#00FF00");
        filament_types.emplace_back("PLA");
    }

    config.set_key_value("enable_prime_tower", new ConfigOptionBool(true));
    config.set_key_value("enable_support", new ConfigOptionBool(true));
    config.set_key_value("timelapse_type", new ConfigOptionEnum<TimelapseType>(TimelapseType::tlSmooth));
    config.set_key_value("single_extruder_multi_material", new ConfigOptionBool(true));
    config.set_key_value("filament_diameter", new ConfigOptionFloats(filament_diameters));
    config.set_key_value("filament_colour", new ConfigOptionStrings(filament_colours));
    config.set_key_value("filament_type", new ConfigOptionStrings(filament_types));
    return config;
}

void add_cube_object(Model &model, double offset_x = 0.0)
{
    ModelObject *object = model.add_object();
    object->add_volume(mesh(TestMesh::cube_20x20x20));
    object->add_instance();
    object->instances.front()->set_offset(Vec3d(offset_x, 0.0, 0.0));
}

void set_model_object_support(ModelObject &object, int extruder, int support_filament, int support_interface_filament)
{
    object.config.set_key_value("extruder", new ConfigOptionInt(extruder));
    object.config.set_key_value("enable_support", new ConfigOptionBool(true));
    object.config.set_key_value("support_filament", new ConfigOptionInt(support_filament));
    object.config.set_key_value("support_interface_filament", new ConfigOptionInt(support_interface_filament));
}

void init_support_on_wipe_tower_print(
    Print &print,
    Model &model,
    const DynamicPrintConfig &config_in,
    unsigned num_objects,
    const std::function<void(Model &)> &configure_model = nullptr)
{
    model.clear_objects();
    for (unsigned i = 0; i < num_objects; ++i)
        add_cube_object(model, 30.0 * i);

    if (configure_model)
        configure_model(model);

    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_num_filaments(config_in.option<ConfigOptionFloats>("filament_diameter")->size());
    config.apply(config_in);

    for (ModelObject *mo : model.objects) {
        mo->ensure_on_bed();
        print.auto_assign_extruders(mo);
    }

    print.apply(model, config);
    print.set_status_silent();
    print.set_no_check_flag(true);
}

void set_filament_is_support(DynamicPrintConfig &config, unsigned filament_idx, bool is_support)
{
    auto *opt = config.option<ConfigOptionBools>("filament_is_support", true);
    opt->get_at(filament_idx) = is_support;
}

} // namespace

TEST_CASE("support_material_on_wipe_tower: PLA model with Supp.PLA interface", "[SupportMaterialOnWipeTower]")
{
    DynamicPrintConfig config = make_wipe_tower_support_config(2);
    config.option<ConfigOptionStrings>("filament_type")->values[0] = "PLA";
    config.option<ConfigOptionStrings>("filament_type")->values[1] = "PLA";
    set_filament_is_support(config, 1, true);

    Print print;
    Model model;
    init_support_on_wipe_tower_print(print, model, config, 1, [](Model &m) {
        // support_filament=0 means support body follows model PLA; interface uses Supp.PLA.
        set_model_object_support(*m.objects.front(), 1, 0, 2);
    });

    REQUIRE(print.config().filament_diameter.size() == 2);
    REQUIRE(print.has_wipe_tower());
    REQUIRE(print.support_material_on_wipe_tower());
}

TEST_CASE("support_material_on_wipe_tower: TPU model with PLA support", "[SupportMaterialOnWipeTower]")
{
    DynamicPrintConfig config = make_wipe_tower_support_config(2);
    config.option<ConfigOptionStrings>("filament_type")->values[0] = "TPU";
    config.option<ConfigOptionStrings>("filament_type")->values[1] = "PLA";

    Print print;
    Model model;
    init_support_on_wipe_tower_print(print, model, config, 1, [](Model &m) {
        set_model_object_support(*m.objects.front(), 1, 2, 2);
    });

    REQUIRE(print.config().filament_diameter.size() == 2);
    REQUIRE(print.support_material_on_wipe_tower());
}

TEST_CASE("support_material_on_wipe_tower: PLA/PETG cross support interfaces", "[SupportMaterialOnWipeTower]")
{
    DynamicPrintConfig config = make_wipe_tower_support_config(2);
    config.option<ConfigOptionStrings>("filament_type")->values[0] = "PLA";
    config.option<ConfigOptionStrings>("filament_type")->values[1] = "PETG";

    Print print;
    Model model;
    init_support_on_wipe_tower_print(print, model, config, 2, [](Model &m) {
        set_model_object_support(*m.objects[0], 1, 0, 2);
        set_model_object_support(*m.objects[1], 2, 0, 1);
    });

    REQUIRE(print.support_material_on_wipe_tower());
}

TEST_CASE("support_material_on_wipe_tower: default support filament", "[SupportMaterialOnWipeTower]")
{
    DynamicPrintConfig config = make_wipe_tower_support_config(2);
    config.option<ConfigOptionStrings>("filament_type")->values[0] = "PLA";
    config.option<ConfigOptionStrings>("filament_type")->values[1] = "PETG";

    Print print;
    Model model;
    init_support_on_wipe_tower_print(print, model, config, 1, [](Model &m) {
        set_model_object_support(*m.objects.front(), 1, 0, 0);
    });

    REQUIRE_FALSE(print.support_material_on_wipe_tower());
}

TEST_CASE("support_material_on_wipe_tower: support same as model body", "[SupportMaterialOnWipeTower]")
{
    DynamicPrintConfig config = make_wipe_tower_support_config(2);
    config.option<ConfigOptionStrings>("filament_type")->values[0] = "PLA";
    config.option<ConfigOptionStrings>("filament_type")->values[1] = "PETG";

    Print print;
    Model model;
    init_support_on_wipe_tower_print(print, model, config, 1, [](Model &m) {
        set_model_object_support(*m.objects.front(), 1, 1, 1);
    });

    REQUIRE_FALSE(print.support_material_on_wipe_tower());
}
