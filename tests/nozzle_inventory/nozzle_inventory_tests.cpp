/**
 * \file nozzle_inventory_tests.cpp
 * \brief Unit tests of NozzleInventory: layer order, history, rack diameter filter and mirror keys
 *
 * Each test builds a printer preset collection, an in-memory history and a mirror config, so no
 * file or GUI is used. A new inventory on the same history acts as an application restart.
 */

#include <catch_main.hpp>

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/NozzleInventory.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"

using namespace Slic3r;

namespace {

/**
 * \brief The printer keys that NozzleInventory reads from a preset
 */
struct PrinterSpec
{
    std::string                   name;          ///< preset name; also the history key, as the preset has no parent
    std::string                   model;         ///< printer_model
    std::string                   variant;       ///< printer_variant, the selected diameter
    std::vector<double>           diameters;     ///< nozzle_diameter, one per extruder
    std::vector<int>              max_counts;    ///< extruder_max_nozzle_count, one per extruder
    std::vector<NozzleVolumeType> default_types; ///< default_nozzle_volume_type, one per extruder
};

const PrinterSpec DUAL_04  {"Dual 0.4 nozzle", "Dual", "0.4", {0.4, 0.4}, {1, 1}, {nvtHighFlow, nvtHighFlow}};
const PrinterSpec DUAL_02  {"Dual 0.2 nozzle", "Dual", "0.2", {0.2, 0.2}, {1, 1}, {nvtStandard, nvtStandard}};
const PrinterSpec SINGLE_04{"Single 0.4 nozzle", "Single", "0.4", {0.4}, {1}, {nvtStandard}};
const PrinterSpec RACK_04  {"Rack 0.4 nozzle", "Rack", "0.4", {0.4}, {4}, {nvtStandard}};
const PrinterSpec RACK_06  {"Rack 0.6 nozzle", "Rack", "0.6", {0.6}, {4}, {nvtStandard}};
const PrinterSpec RACK_08  {"Rack 0.8 nozzle", "Rack", "0.8", {0.8}, {4}, {nvtStandard}};

NozzleSlot nozzle(const std::string &diameter, NozzleVolumeType type) { return {diameter, type, NozzleSlotStatus::Normal}; }

/**
 * \brief The inventory with its inputs, owned in the order the inventory needs them
 */
struct Rig
{
    DynamicPrintConfig      mirror;
    AppConfig               history;
    PrinterPresetCollection printers{Preset::TYPE_PRINTER, Preset::printer_options(),
                                     static_cast<const PrintRegionConfig &>(FullPrintConfig::defaults()), "Default Printer"};
    NozzleInventory         inventory{mirror};

    Rig() { inventory.set_history(&history); }

    /**
     * \brief Load \p spec as a preset, select it and tell the inventory
     */
    void select(const PrinterSpec &spec)
    {
        DynamicPrintConfig config = printers.default_preset().config;
        config.opt_string("printer_model", true)   = spec.model;
        config.opt_string("printer_variant", true) = spec.variant;
        config.option<ConfigOptionFloatsNullable>("nozzle_diameter", true)->values        = spec.diameters;
        config.option<ConfigOptionIntsNullable>("extruder_max_nozzle_count", true)->values = spec.max_counts;
        std::vector<int> &types = config.option<ConfigOptionEnumsGeneric>("default_nozzle_volume_type", true)->values;
        types.clear();
        for (NozzleVolumeType type : spec.default_types)
            types.push_back(int(type));
        // The rvalue overload keeps keys that the default preset lacks, such as printer_variant.
        printers.load_preset("", spec.name, std::move(config), true);
        inventory.select_printer(printers);
    }

    /**
     * \brief Open a project whose file holds only the per-extruder volume types
     */
    void open_project(const std::vector<NozzleVolumeType> &file_types)
    {
        DynamicPrintConfig file;
        // A created option holds its default value; replace it, or the extruder count is wrong.
        std::vector<int> &values = file.option<ConfigOptionEnumsGeneric>("nozzle_volume_type", true)->values;
        values.clear();
        for (NozzleVolumeType type : file_types)
            values.push_back(int(type));
        inventory.open_project(printers, file);
    }

    std::vector<int> mirror_types() const { return mirror.option<ConfigOptionEnumsGeneric>("nozzle_volume_type")->values; }
};

} // namespace

TEST_CASE("A model switch keeps the machine nozzles of the other model", "[nozzle_inventory]")
{
    Rig rig;
    rig.select(DUAL_04);
    rig.inventory.sync_machine("Dual", {{nozzle("0.4", nvtStandard)}, {nozzle("0.4", nvtTPUHighFlow)}});
    REQUIRE(rig.inventory.volume_type(0) == nvtStandard);
    REQUIRE(rig.inventory.volume_type(1) == nvtTPUHighFlow);

    SECTION("the other model reads its own defaults")
    {
        rig.select(SINGLE_04);
        CHECK(rig.inventory.extruder_count() == 1);
        CHECK(rig.inventory.volume_type(0) == nvtStandard);
        CHECK(rig.mirror_types() == std::vector<int>{int(nvtStandard)});
    }
    SECTION("a switch back restores the sync")
    {
        rig.select(SINGLE_04);
        rig.select(DUAL_04);
        CHECK(rig.inventory.volume_type(0) == nvtStandard);
        CHECK(rig.inventory.volume_type(1) == nvtTPUHighFlow);
    }
    SECTION("a restart restores the sync from history")
    {
        Rig restarted;
        restarted.history = rig.history;
        restarted.select(DUAL_04);
        CHECK(restarted.inventory.volume_type(0) == nvtStandard);
        CHECK(restarted.inventory.volume_type(1) == nvtTPUHighFlow);
    }
}

TEST_CASE("The newer of project and machine wins", "[nozzle_inventory]")
{
    Rig rig;
    rig.select(DUAL_04);

    SECTION("a sync after a project open")
    {
        rig.open_project({nvtStandard, nvtStandard});
        REQUIRE(rig.mirror_types() == std::vector<int>{int(nvtStandard), int(nvtStandard)});
        rig.inventory.sync_machine("Dual", {{nozzle("0.4", nvtHighFlow)}, {nozzle("0.4", nvtTPUHighFlow)}});
        CHECK(rig.mirror_types() == std::vector<int>{int(nvtHighFlow), int(nvtTPUHighFlow)});
    }
    SECTION("a project open after a sync")
    {
        rig.inventory.sync_machine("Dual", {{nozzle("0.4", nvtHighFlow)}, {nozzle("0.4", nvtTPUHighFlow)}});
        rig.open_project({nvtStandard, nvtStandard});
        CHECK(rig.mirror_types() == std::vector<int>{int(nvtStandard), int(nvtStandard)});
        rig.inventory.close_project();
        CHECK(rig.mirror_types() == std::vector<int>{int(nvtHighFlow), int(nvtTPUHighFlow)});
    }
    SECTION("a project open is not saved as history")
    {
        rig.open_project({nvtStandard, nvtStandard});
        CHECK(rig.history.get_nozzle_volume_types_from_config(DUAL_04.name).empty());
    }
}

TEST_CASE("A diameter switch filters the rack, not the slots", "[nozzle_inventory]")
{
    Rig rig;
    rig.select(RACK_04);
    rig.inventory.sync_machine("Rack", {{nozzle("0.40", nvtStandard), nozzle("0.6", nvtHighFlow), NozzleSlot(), nozzle("0.4", nvtHighFlow)}});
    const ExtruderSlots synced = rig.inventory.slots(0);
    REQUIRE(synced.size() == 4);
    REQUIRE(synced[0].diameter == "0.4");
    REQUIRE(rig.inventory.volume_type(0) == nvtHybrid);
    REQUIRE(rig.inventory.count(0, nvtStandard) == 1);
    REQUIRE(rig.inventory.count(0, nvtHighFlow) == 1);

    SECTION("a diameter the rack holds counts only its nozzles")
    {
        rig.select(RACK_06);
        CHECK(rig.inventory.slots(0).size() == 4);
        CHECK(rig.inventory.slots(0)[1].diameter == "0.6");
        CHECK(rig.inventory.counts(0) == std::map<NozzleVolumeType, int>{{nvtHighFlow, 1}});
        CHECK(rig.inventory.volume_type(0) == nvtHighFlow);
    }
    SECTION("a diameter the rack lacks counts all installed nozzles")
    {
        rig.select(RACK_08);
        CHECK(rig.inventory.slots(0)[2].empty());
        CHECK(rig.inventory.count(0) == 3);
        CHECK(rig.inventory.volume_type(0) == nvtHybrid);
    }
}

TEST_CASE("A diameter switch without a rack drops the other diameter", "[nozzle_inventory]")
{
    Rig rig;
    rig.select(DUAL_04);
    rig.inventory.sync_machine("Dual", {{nozzle("0.4", nvtHighFlow)}, {nozzle("0.4", nvtHighFlow)}});
    rig.select(DUAL_02);
    CHECK(rig.inventory.slots(0).front().diameter == "0.2");
    CHECK(rig.inventory.volume_type(0) == nvtStandard);
    rig.select(DUAL_04);
    CHECK(rig.inventory.volume_type(0) == nvtHighFlow);
}

TEST_CASE("Legacy keys still give the nozzles", "[nozzle_inventory]")
{
    Rig rig;

    SECTION("history written by an older version")
    {
        rig.history.save_nozzle_volume_types_to_config(DUAL_04.name, "Standard,TPU High Flow");
        rig.select(DUAL_04);
        CHECK(rig.inventory.volume_type(0) == nvtStandard);
        CHECK(rig.inventory.volume_type(1) == nvtTPUHighFlow);
    }
    SECTION("a project with only the legacy counts")
    {
        rig.select(DUAL_04);
        DynamicPrintConfig file;
        file.option<ConfigOptionStrings>("extruder_nozzle_stats", true)->values = {"Standard#1", "TPU High Flow#1"};
        rig.inventory.open_project(rig.printers, file);
        CHECK(rig.inventory.volume_type(0) == nvtStandard);
        CHECK(rig.inventory.volume_type(1) == nvtTPUHighFlow);
    }
    SECTION("nozzle_slots wins over nozzle_volume_types")
    {
        rig.history.set("nozzle_slots", "Dual", std::string("0.4 TPU High Flow,0.4 Standard"));
        rig.history.save_nozzle_volume_types_to_config(DUAL_04.name, "High Flow,High Flow");
        rig.select(DUAL_04);
        CHECK(rig.inventory.volume_type(0) == nvtTPUHighFlow);
        CHECK(rig.inventory.volume_type(1) == nvtStandard);
    }
    SECTION("a sync writes history that an older version can read")
    {
        rig.select(DUAL_04);
        rig.inventory.sync_machine("Dual", {{nozzle("0.4", nvtStandard)}, {nozzle("0.4", nvtTPUHighFlow)}});
        CHECK(rig.history.get_nozzle_volume_types_from_config(DUAL_04.name) == "Standard,TPU High Flow");
    }
}

TEST_CASE("An unknown volume type in history is not read as Standard", "[nozzle_inventory]")
{
    // The preset default is High Flow, so a token read as Standard shows as a wrong result.
    Rig rig;

    SECTION("in nozzle_volume_types")
    {
        rig.history.save_nozzle_volume_types_to_config(DUAL_04.name, "Future Flow,Standard");
        rig.select(DUAL_04);
        CHECK(rig.inventory.volume_type(0) == nvtHighFlow);
        CHECK(rig.inventory.volume_type(1) == nvtHighFlow);
    }
    SECTION("in nozzle_slots, which then gives way to nozzle_volume_types")
    {
        // A string literal would pick the bool overload of AppConfig::set().
        rig.history.set("nozzle_slots", "Dual", std::string("0.4 Future Flow,0.4 Standard"));
        rig.history.save_nozzle_volume_types_to_config(DUAL_04.name, "TPU High Flow,TPU High Flow");
        rig.select(DUAL_04);
        CHECK(rig.inventory.volume_type(0) == nvtTPUHighFlow);
        CHECK(rig.inventory.volume_type(1) == nvtTPUHighFlow);
    }
}

TEST_CASE("A printer switch rewrites the mirror after a bulk copy", "[nozzle_inventory]")
{
    // A project load copies the file's nozzle keys into the mirror before the inventory sees the new printer.
    Rig rig;
    rig.select(DUAL_04);
    const DynamicPrintConfig dual_keys = rig.mirror;
    rig.select(SINGLE_04);
    rig.mirror.apply(dual_keys);
    REQUIRE(rig.mirror_types().size() == 2);

    rig.inventory.select_printer(rig.printers);
    CHECK(rig.mirror_types() == std::vector<int>{int(nvtStandard)});
    CHECK(rig.mirror.option<ConfigOptionInts>("extruder_nozzle_count")->values == std::vector<int>{1});
    CHECK(rig.mirror.option<ConfigOptionStrings>("extruder_nozzle_stats")->values.size() == 1);
    CHECK(per_slot_nozzle_options_consistent(rig.mirror));
}

TEST_CASE("User edits stay in the slots of the extruder", "[nozzle_inventory]")
{
    Rig rig;
    rig.select(RACK_04);

    SECTION("counts past the slot count are dropped")
    {
        rig.inventory.user_set_counts(0, {{nvtStandard, 3}, {nvtHighFlow, 3}});
        CHECK(rig.inventory.slots(0).size() == 4);
        CHECK(rig.inventory.count(0, nvtStandard) == 3);
        CHECK(rig.inventory.count(0, nvtHighFlow) == 1);
    }
    SECTION("a single volume type is ignored on a rack")
    {
        rig.inventory.user_set_volume_type(0, nvtHighFlow);
        CHECK(rig.inventory.volume_type(0) == nvtStandard);
    }
}
