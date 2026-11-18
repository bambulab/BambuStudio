#include <catch2/catch.hpp>

#include "sequential_wipe_tower_test_utils.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"

#include <sstream>

using namespace Slic3r;
using namespace Slic3r::Test;
namespace ST = Slic3r::Test::SequentialTower;

namespace {

void require_separate_object_towers(ST::Printer printer = ST::Printer::H2D)
{
    INFO("Printer fixture: " << (printer == ST::Printer::H2D ? "H2D" : "P1S"));
    const auto c = ST::config(PrintSequence::ByObject, printer);
    Model model;
    ST::add_object(model, true, Vec3d(70., 70., 0.));
    ST::add_object(model, true, Vec3d(220., 220., 0.));
    Print print;
    ST::apply(print, model, c);
    REQUIRE(print.config().nozzle_diameter.size() == (printer == ST::Printer::H2D ? 2 : 1));
    print.process();

    REQUIRE(print.has_object_wipe_towers());
    REQUIRE(print.has_wipe_tower());
    const auto &placements = print.object_wipe_tower_placements();
    REQUIRE(placements.size() == 2);
    for (const auto &entry : placements) {
        REQUIRE(intersection(entry.second.footprint, print.sequential_instance_footprint(*entry.first)).empty());
        // H2D: common nozzle area X=25..325 on the 350x320 bed; P1S: 256x256.
        REQUIRE(diff(Polygons{entry.second.footprint}, print.get_extruder_shared_printable_polygon()).empty());
        const auto *data = print.object_wipe_tower_data(entry.first->print_object);
        REQUIRE(data != nullptr);
        REQUIRE_FALSE(data->tool_changes.empty());
        REQUIRE(entry.second.height > 0.);
    }
    REQUIRE(print.object_wipe_tower_placement_warnings().empty());
}

} // namespace

TEST_CASE("By-object multicolour instances get separate towers inside the bed", "[SequentialWipeTower]")
{
    require_separate_object_towers();
}

TEST_CASE("P1S by-object multicolour instances get separate towers inside the bed", "[SequentialWipeTower][P1S]")
{
    require_separate_object_towers(ST::Printer::P1S);
}

TEST_CASE("A monochrome neighbour does not acquire a sequential tower", "[SequentialWipeTower]")
{
    Model model;
    ST::add_object(model, true, Vec3d(70., 70., 0.));
    ST::add_object(model, false, Vec3d(220., 220., 0.));
    Print print;
    ST::apply(print, model, ST::config());
    print.process();

    REQUIRE(print.has_object_wipe_towers());
    REQUIRE(print.object_wipe_tower_placements().size() == 1);
    const PrintInstance *instance = print.object_wipe_tower_placements().begin()->first;
    const ModelObject *model_object = instance->print_object->model_object();
    REQUIRE(model_object != nullptr);
    REQUIRE(model_object->name == "object_1");
    for (const PrintObject *object : print.objects())
        if (object != instance->print_object)
            REQUIRE(print.object_wipe_tower_data(object) == nullptr);
}

TEST_CASE("A blocked edge instance slices without a tower and names the fallback", "[SequentialWipeTower]")
{
    auto c = ST::config();
    // Leave only two 24x14 mm pockets, too small for the exported H2D tower.
    // The first pocket must also be inside the common nozzle area X=25..325.
    for (const Polygon &p : {ST::rectangle(64., 0., 218., 320.), ST::rectangle(0., 32., 64., 320.),
                             ST::rectangle(0., 0., 64., 18.), ST::rectangle(0., 18., 40., 32.),
                             ST::rectangle(218., 0., 350., 218.), ST::rectangle(242., 218., 350., 320.),
                             ST::rectangle(218., 232., 242., 320.)})
        ST::exclude(c, p);
    Model model;
    ST::add_object(model, true, Vec3d(42., 20., 0.));
    ST::add_object(model, false, Vec3d(220., 220., 0.));
    Print print;
    ST::apply(print, model, c);
    REQUIRE_NOTHROW(print.process());

    REQUIRE(print.has_object_wipe_towers());
    REQUIRE(print.object_wipe_tower_placements().empty());
    REQUIRE_FALSE(print.has_wipe_tower());
    const auto &warnings = print.object_wipe_tower_placement_warnings();
    REQUIRE(warnings.size() == 1);
    REQUIRE(warnings.front().is_warning);
    REQUIRE(warnings.front().string.find("No space for a prime tower beside object \"object_1\"") != std::string::npos);
    REQUIRE(warnings.front().object != nullptr);
    const auto *model_object = dynamic_cast<const ModelObject *>(warnings.front().object);
    REQUIRE(model_object != nullptr);
    REQUIRE(model_object->name == "object_1");
}

TEST_CASE("Excluding the preferred footprint moves the sequential tower", "[SequentialWipeTower]")
{
    auto c = ST::config();
    Model model;
    ST::add_object(model, true, Vec3d(70., 70., 0.));
    ST::add_object(model, false, Vec3d(220., 220., 0.));
    Print print;
    ST::apply(print, model, c);
    print.process();
    REQUIRE(print.object_wipe_tower_placements().size() == 1);
    const auto box = print.object_wipe_tower_placements().begin()->second.footprint.bounding_box();
    const Polygon excluded = ST::rectangle(unscale_(box.min.x()) - 0.5, unscale_(box.min.y()) - 0.5,
                                           unscale_(box.max.x()) + 0.5, unscale_(box.max.y()) + 0.5);
    ST::exclude(c, excluded);
    ST::apply(print, model, c);
    REQUIRE_NOTHROW(print.process());
    REQUIRE(print.object_wipe_tower_placements().size() == 1);
    REQUIRE(intersection(print.object_wipe_tower_placements().begin()->second.footprint, excluded).empty());
}

TEST_CASE("Multicolour copies get independent sequential tower placements", "[SequentialWipeTower]")
{
    Model model;
    ModelObject *object = ST::add_object(model, true, Vec3d(70., 70., 0.));
    object->add_instance()->set_offset(Vec3d(220., 220., 0.));
    // Equal arrange orders exercise the copy ordering path.
    object->instances.back()->arrange_order = object->instances.front()->arrange_order;
    Print print;
    ST::apply(print, model, ST::config());
    print.process();
    REQUIRE(print.objects().size() == 1);
    REQUIRE(print.is_sequential_print());
    REQUIRE(print.object_wipe_tower_placements().size() == 2);
    auto it = print.object_wipe_tower_placements().begin();
    const auto &a = *it++;
    const auto &b = *it;
    REQUIRE(a.first != b.first);
    REQUIRE(a.first->print_object == b.first->print_object);
    REQUIRE(intersection(a.second.footprint, b.second.footprint).empty());
}

TEST_CASE("Dynamic nozzle grouping keeps object tower mode disabled", "[SequentialWipeTower]")
{
    auto c = ST::config();
    c.set_key_value("enable_filament_dynamic_map", new ConfigOptionBool(true));
    c.set_key_value("filament_map_mode", new ConfigOptionEnum<FilamentMapMode>(fmmAutoForFlush));
    Model model;
    ST::add_object(model, true, Vec3d(70., 70., 0.));
    ST::add_object(model, true, Vec3d(220., 220., 0.));
    Print print;
    ST::apply(print, model, c);
    REQUIRE(print.is_dynamic_group_reorder());
    REQUIRE_FALSE(print.has_object_wipe_towers());
    REQUIRE_FALSE(print.has_wipe_tower());
}

TEST_CASE("Sequential timelapse and clumping validation retain upstream errors", "[SequentialWipeTower]")
{
    auto c = ST::config();
    std::string expected;
    SECTION("Smooth timelapse") {
        c.set_key_value("timelapse_type", new ConfigOptionEnum<TimelapseType>(TimelapseType::tlSmooth));
        expected = "Smooth mode of timelapse is not supported when \"by object\" sequence is enabled.";
    }
    SECTION("Clumping detection") {
        c.set_key_value("enable_wrapping_detection", new ConfigOptionBool(true));
        expected = "Clumping detection is not supported when \"by object\" sequence is enabled.";
    }
    Model model;
    ST::add_object(model, true, Vec3d(70., 70., 0.));
    ST::add_object(model, true, Vec3d(220., 220., 0.));
    Print print;
    ST::apply(print, model, c);
    REQUIRE(print.validate().string == expected);
}

TEST_CASE("G-code emits a tower in each sequential copy block and counts actual tool changes", "[SequentialWipeTower][GCode]")
{
    auto c = ST::config();
    // Delimit whole sequential blocks: object labels only enclose model paths.
    // The Bambu writer assumes the start G-code loads the initial filament.
    // Emit that selection so the counter below excludes it, not the first change.
    c.set_key_value("machine_start_gcode", new ConfigOptionString("; TEST_BLOCK 0\n; TEST_TOTAL {total_toolchanges}\nT{initial_extruder}\n"));
    c.set_key_value("printing_by_object_gcode", new ConfigOptionString("; TEST_BLOCK {current_object_idx}\n"));
    c.set_key_value("machine_end_gcode", new ConfigOptionString("; TEST_BLOCK_END\n"));
    c.set_key_value("change_filament_gcode", new ConfigOptionString("T{next_extruder}\n"));
    Model model;
    ModelObject *object = ST::add_object(model, true, Vec3d(70., 70., 0.));
    object->add_instance()->set_offset(Vec3d(220., 220., 0.));
    object->instances.back()->arrange_order = object->instances.front()->arrange_order;
    Print print;
    ST::apply(print, model, c);
    print.process();
    // Check the sliced blocks before entering the exporter, so a missing
    // placement or tower is reported at its source rather than during export.
    REQUIRE(print.object_wipe_tower_placements().size() == 2);
    for (const auto &entry : print.object_wipe_tower_placements()) {
        const auto *data = print.object_wipe_tower_data(entry.first->print_object);
        REQUIRE(data != nullptr);
        REQUIRE_FALSE(data->tool_changes.empty());
    }
    const std::string output = ST::gcode(print);
    const size_t first = output.find("; TEST_BLOCK 0\n");
    const size_t second = output.find("; TEST_BLOCK 1\n");
    const size_t end = output.find("; TEST_BLOCK_END\n");
    REQUIRE(first != std::string::npos);
    REQUIRE(second != std::string::npos);
    REQUIRE(end != std::string::npos);
    REQUIRE(first < second);
    REQUIRE(second < end);
    const std::string tower_tag = ";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Wipe_Tower_Start);
    for (const auto &bounds : {std::make_pair(first, second), std::make_pair(second, end)}) {
        const size_t tower = output.find(tower_tag, bounds.first);
        REQUIRE(tower != std::string::npos);
        REQUIRE(tower < bounds.second);
    }
    // Count changes of active filament, excluding the initial tool selection.
    std::istringstream lines(output.substr(first, end - first));
    std::string line;
    int previous = -1;
    int changes = 0;
    while (std::getline(lines, line)) {
        if (line.size() < 2 || line[0] != 'T' || line[1] < '0' || line[1] > '9')
            continue;
        const int tool = std::stoi(line.substr(1));
        if (previous >= 0 && tool != previous)
            ++changes;
        previous = tool;
    }
    REQUIRE(changes > 0);
    REQUIRE(changes == print.wipe_tower_statistics_data().number_of_toolchanges);
    REQUIRE(output.find("; TEST_TOTAL " + std::to_string(changes) + "\n") != std::string::npos);
    REQUIRE(print.print_statistics().total_toolchanges == changes);
}
