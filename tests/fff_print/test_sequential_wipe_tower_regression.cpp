#include <catch2/catch.hpp>

#include "sequential_wipe_tower_test_utils.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;
namespace ST = Slic3r::Test::SequentialTower;

namespace {

void require_global_tower_and_statistics(ST::Printer printer = ST::Printer::H2D)
{
    INFO("Printer fixture: " << (printer == ST::Printer::H2D ? "H2D" : "P1S"));
    auto c = ST::config(PrintSequence::ByLayer, printer);
    Model model;
    ST::add_object(model, true, Vec3d(70., 70., 0.));
    ST::add_object(model, true, Vec3d(220., 220., 0.));
    Print print;
    ST::apply(print, model, c);
    REQUIRE(print.config().nozzle_diameter.size() == (printer == ST::Printer::H2D ? 2 : 1));
    print.process();

    REQUIRE_FALSE(print.has_object_wipe_towers());
    REQUIRE(print.object_wipe_tower_placements().empty());
    REQUIRE(print.has_wipe_tower());
    const WipeTowerData &tower = print.wipe_tower_data();
    REQUIRE_FALSE(tower.tool_changes.empty());
    REQUIRE(&print.wipe_tower_statistics_data() == &tower);
    const Points corners = print.first_layer_wipe_tower_corners();
    REQUIRE(corners.size() == 4);
    const double tower_x = c.option<ConfigOptionFloats>("wipe_tower_x")->get_at(0);
    const double tower_y = c.option<ConfigOptionFloats>("wipe_tower_y")->get_at(0);
    REQUIRE(unscale_(corners.front().x()) == Approx(tower_x + tower.bbx.min.x() + tower.rib_offset.x()).margin(1e-5));
    REQUIRE(unscale_(corners.front().y()) == Approx(tower_y + tower.bbx.min.y() + tower.rib_offset.y()).margin(1e-5));
    int planned_changes = 0;
    for (const auto &layer : tower.tool_changes)
        for (const auto &change : layer)
            if (change.is_tool_change && change.initial_tool != change.new_tool)
                ++planned_changes;
    REQUIRE(planned_changes > 0);
    REQUIRE(tower.number_of_toolchanges == planned_changes);
    // Upstream fills this vector during process(), but its writer records only
    // ramming lengths, not ordinary wall/purge extrusion. This fixture has no
    // ramming, so zero is expected; the exported processor statistics below
    // must still prove that the global tower actually extrudes material.
    REQUIRE(tower.used_filament == std::vector<float>(c.option<ConfigOptionFloats>("filament_diameter")->size(), 0.f));
    const auto used_before_export = tower.used_filament;
    GCodeProcessorResult result;
    const std::string output = ST::gcode(print, &result);
    REQUIRE_FALSE(output.empty());
    REQUIRE(&print.wipe_tower_statistics_data() == &print.wipe_tower_data());
    REQUIRE(print.wipe_tower_data().used_filament == used_before_export);
    REQUIRE(print.wipe_tower_statistics_data().number_of_toolchanges == planned_changes);
    REQUIRE(print.print_statistics().total_toolchanges == planned_changes);
    double tower_volume = 0.;
    for (const auto &entry : result.print_statistics.wipe_tower_volumes_per_extruder) {
        REQUIRE(entry.second >= 0.);
        tower_volume += entry.second;
    }
    REQUIRE(tower_volume > 0.);
}

} // namespace

TEST_CASE("By-layer multicolour objects retain one configured global tower and its statistics", "[SequentialWipeTower][Regression]")
{
    require_global_tower_and_statistics();
}

TEST_CASE("P1S by-layer multicolour objects retain one configured global tower and its statistics", "[SequentialWipeTower][Regression][P1S]")
{
    require_global_tower_and_statistics(ST::Printer::P1S);
}

TEST_CASE("A single multicolour object retains the upstream global tower for either sequence", "[SequentialWipeTower][Regression]")
{
    PrintSequence sequence = PrintSequence::ByLayer;
    SECTION("By layer") { sequence = PrintSequence::ByLayer; }
    SECTION("By object") { sequence = PrintSequence::ByObject; }
    Model model;
    ST::add_object(model, true, Vec3d(70., 70., 0.));
    Print print;
    ST::apply(print, model, ST::config(sequence));
    print.process();
    REQUIRE_FALSE(print.is_sequential_print());
    REQUIRE_FALSE(print.has_object_wipe_towers());
    REQUIRE(print.has_wipe_tower());
    REQUIRE(print.object_wipe_tower_placements().empty());
    REQUIRE_FALSE(print.wipe_tower_data().tool_changes.empty());
    REQUIRE(print.first_layer_wipe_tower_corners().size() == 4);
}

TEST_CASE("Monochrome objects keep sequential printing without towers", "[SequentialWipeTower][Regression]")
{
    Model model;
    ST::add_object(model, false, Vec3d(70., 70., 0.));
    ST::add_object(model, false, Vec3d(220., 220., 0.));
    Print print;
    ST::apply(print, model, ST::config());
    print.process();
    REQUIRE(print.is_sequential_print());
    // Mode eligibility alone does not imply that any tower was generated.
    REQUIRE_FALSE(print.has_wipe_tower());
    REQUIRE(print.object_wipe_tower_placements().empty());
    REQUIRE(print.wipe_tower_data().tool_changes.empty());
    for (const PrintObject *object : print.objects())
        REQUIRE(print.object_wipe_tower_data(object) == nullptr);
}

TEST_CASE("Disabling prime towers prevents both global and object towers", "[SequentialWipeTower][Regression]")
{
    auto c = ST::config();
    c.set_key_value("enable_prime_tower", new ConfigOptionBool(false));
    Model model;
    ST::add_object(model, true, Vec3d(70., 70., 0.));
    ST::add_object(model, true, Vec3d(220., 220., 0.));
    Print print;
    ST::apply(print, model, c);
    print.process();
    REQUIRE(print.is_sequential_print());
    REQUIRE_FALSE(print.has_object_wipe_towers());
    REQUIRE_FALSE(print.has_wipe_tower());
    REQUIRE(print.object_wipe_tower_placements().empty());
    REQUIRE(print.wipe_tower_data().tool_changes.empty());
}

TEST_CASE("Monochrome copies retain the upstream non-sequential path without a tower", "[SequentialWipeTower][Regression]")
{
    Model model;
    ModelObject *object = ST::add_object(model, false, Vec3d(70., 70., 0.));
    object->add_instance()->set_offset(Vec3d(220., 220., 0.));
    Print print;
    ST::apply(print, model, ST::config());
    print.process();
    REQUIRE(print.objects().size() == 1);
    REQUIRE(print.num_object_instances() == 2);
    REQUIRE_FALSE(print.is_sequential_print());
    REQUIRE_FALSE(print.has_object_wipe_towers());
    REQUIRE_FALSE(print.has_wipe_tower());
    REQUIRE(print.wipe_tower_data().tool_changes.empty());
    REQUIRE(print.object_wipe_tower_placements().empty());
}

TEST_CASE("Single-filament normalization still disables the upstream global tower", "[SequentialWipeTower][Regression]")
{
    auto c = ST::config(PrintSequence::ByLayer);
    int num_objects = 1;
    SECTION("One object by layer") {}
    SECTION("Multiple objects by layer") { num_objects = 2; }
    SECTION("One object by object") {
        c.set_key_value("print_sequence", new ConfigOptionEnum<PrintSequence>(PrintSequence::ByObject));
    }
    // Keep the exported filament profiles intact; only one filament is used.
    c.normalize_fdm_2(num_objects, 1);
    REQUIRE_FALSE(c.opt_bool("enable_prime_tower"));
}

TEST_CASE("Intentional change: by-object normalization preserves the tower option for multiple objects", "[SequentialWipeTower][Regression]")
{
    auto c = ST::config();
    unsigned used_filaments = 2;
    SECTION("Multicolour objects") { used_filaments = 2; }
    SECTION("Monochrome plate preserves eligibility but generates no towers") { used_filaments = 1; }
    c.set_key_value("independent_support_layer_height", new ConfigOptionBool(true));
    c.normalize_fdm_2(2, used_filaments);
    REQUIRE(c.opt_bool("enable_prime_tower"));
    REQUIRE(c.opt_bool("independent_support_layer_height"));
}

TEST_CASE("Global tower normalization still synchronizes support layer heights", "[SequentialWipeTower][Regression]")
{
    auto c = ST::config(PrintSequence::ByLayer);
    c.set_key_value("independent_support_layer_height", new ConfigOptionBool(true));
    c.normalize_fdm_2(2, 2);
    REQUIRE(c.opt_bool("enable_prime_tower"));
    REQUIRE_FALSE(c.opt_bool("independent_support_layer_height"));
}

TEST_CASE("Sequential clearance without towers retains upstream near and far results", "[SequentialWipeTower][Regression]")
{
    double x = 220.;
    bool close = false;
    SECTION("Close objects retain the exact collision error") { x = 82.; close = true; }
    SECTION("Separated objects remain valid") { x = 220.; }
    Model model;
    ST::add_object(model, false, Vec3d(70., 70., 0.));
    ST::add_object(model, false, Vec3d(x, 70., 0.));
    Print print;
    ST::apply(print, model, ST::config());
    REQUIRE_FALSE(print.has_wipe_tower());
    const auto error = Print::sequential_print_clearance_valid(print);
    if (close) {
        REQUIRE(error.string == "object_2 is too close to others, and collisions may be caused.");
        REQUIRE(error.object != nullptr);
        const auto *model_object = dynamic_cast<const ModelObject *>(error.object);
        REQUIRE(model_object != nullptr);
        REQUIRE(model_object->name == "object_2");
    } else {
        REQUIRE(error.string.empty());
    }
}
