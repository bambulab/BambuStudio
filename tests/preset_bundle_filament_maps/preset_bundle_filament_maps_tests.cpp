/**
 * \file preset_bundle_filament_maps_tests.cpp
 * \brief Unit tests of the per-filament map sizes in PresetBundle (issue #13326)
 *
 * The slicer indexes filament_map, filament_nozzle_map and filament_volume_map by filament. When the
 * filament count changes, every one of them must follow it. Each test builds a PresetBundle in memory
 * from its built-in default presets, so no profile file or GUI is used. The only file read is
 * resources/info/multi_nozzle_rules.json, which the config code loads when filaments change.
 */

#include <catch_main.hpp>

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Utils.hpp"

using namespace Slic3r;

namespace {

/**
 * \brief Size of an int vector option of \p config, or -1 when the option is missing
 */
int ints_size(const DynamicPrintConfig &config, const char *key)
{
    const auto *opt = config.option<ConfigOptionInts>(key);
    return opt ? int(opt->values.size()) : -1;
}

/**
 * \brief One AMS tray, with the keys build_filament_ams_list() (CalibrationWizardPresetPage.cpp) sets
 */
DynamicPrintConfig make_tray(const std::string &filament_id, const std::string &tray_name, const std::string &ams_id,
                             const std::string &slot_id, const std::string &colour)
{
    DynamicPrintConfig tray;
    tray.set_key_value("filament_id", new ConfigOptionStrings{filament_id});
    tray.set_key_value("tag_uid", new ConfigOptionStrings{""});
    tray.set_key_value("ams_id", new ConfigOptionStrings{ams_id});
    tray.set_key_value("slot_id", new ConfigOptionStrings{slot_id});
    tray.set_key_value("filament_type", new ConfigOptionStrings{"PLA"});
    tray.set_key_value("tray_name", new ConfigOptionStrings{tray_name});
    tray.set_key_value("filament_colour", new ConfigOptionStrings{colour});
    tray.set_key_value("filament_multi_colour", new ConfigOptionStrings{colour});
    tray.set_key_value("filament_colour_type", new ConfigOptionStrings{"1"});
    tray.set_key_value("filament_exist", new ConfigOptionBools{true});
    tray.set_key_value("filament_is_support", new ConfigOptionBools{false});
    return tray;
}

/**
 * \brief A bundle with \p count filaments, set up the way the "+" button does it
 *
 * A default-constructed bundle has no filament slot yet; PresetBundle::reset() seeds the first one
 * the same way.
 */
void start_with_filaments(PresetBundle &bundle, unsigned int count)
{
    set_resources_dir(TEST_RESOURCES_DIR);
    bundle.filament_presets = {bundle.filaments.get_selected_preset_name()};
    bundle.set_num_filaments(count);
}

} // namespace

TEST_CASE("AMS sync keeps every per-filament map in step with the filament count", "[PresetBundle][FilamentMaps]")
{
    PresetBundle bundle;
    start_with_filaments(bundle, 3);

    REQUIRE(bundle.filament_presets.size() == 3);
    CHECK(ints_size(bundle.project_config, "filament_map") == 3);
    CHECK(ints_size(bundle.project_config, "filament_nozzle_map") == 3);
    CHECK(ints_size(bundle.project_config, "filament_volume_map") == 3);

    // The filament the trays report. sync_ams_list() accepts a preset that is compatible, is its
    // own base (no "inherits") and carries the tray's filament_id.
    DynamicPrintConfig filament_config = bundle.filaments.default_preset().config;
    Preset &tray_filament = bundle.filaments.load_preset("", "Test PLA", filament_config, false);
    tray_filament.filament_id = "GFTEST00";

    // P1S with one AMS (A1-A4) and the external spool, as in the report: five trays.
    bundle.filament_ams_list.clear();
    bundle.filament_ams_list.emplace(0, make_tray("GFTEST00", "A1", "0", "0", "#FF0000"));
    bundle.filament_ams_list.emplace(1, make_tray("GFTEST00", "A2", "0", "1", "#00FF00"));
    bundle.filament_ams_list.emplace(2, make_tray("GFTEST00", "A3", "0", "2", "#0000FF"));
    bundle.filament_ams_list.emplace(3, make_tray("GFTEST00", "A4", "0", "3", "#FFFF00"));
    bundle.filament_ams_list.emplace(0x10000 + 255, make_tray("GFTEST00", "Ext", "255", "0", "#FFFFFF"));

    // Overwrite sync (use_map = false, no append), the direct "Sync filaments with AMS" path.
    std::vector<std::pair<DynamicPrintConfig *, std::string>> unknowns;
    std::map<int, AMSMapInfo>                                 maps;
    MergeFilamentInfo                                         merge_info;
    const unsigned int n = bundle.sync_ams_list(unknowns, false, maps, false, merge_info, false);

    REQUIRE(n == 5);
    REQUIRE(bundle.filament_presets.size() == 5);
    CHECK(ints_size(bundle.project_config, "filament_map") == 5);
    // Without the sync_ams_list() part of the fix these two stay at 3.
    CHECK(ints_size(bundle.project_config, "filament_nozzle_map") == 5);
    CHECK(ints_size(bundle.project_config, "filament_volume_map") == 5);
}

TEST_CASE("full_config() hands out filament_volume_map with one entry per filament", "[PresetBundle][FilamentMaps]")
{
    PresetBundle bundle;
    start_with_filaments(bundle, 5);
    REQUIRE(bundle.filament_presets.size() == 5);

    // The state an AMS sync left behind before the fix: five filaments, three volume entries.
    bundle.project_config.option<ConfigOptionInts>("filament_volume_map")->values =
        std::vector<int>(3, int(NozzleVolumeType::nvtStandard));
    REQUIRE(ints_size(bundle.project_config, "filament_volume_map") == 3);

    // full_config(false) is the config a single-extruder printer slices with (Plater.cpp) and the
    // base of full_config_for_3mf(), which is saved into project_settings.config.
    const DynamicPrintConfig config = bundle.full_config(false);

    CHECK(ints_size(config, "filament_map") == 5);
    // Without the full_fff_config() part of the fix this is 3.
    CHECK(ints_size(config, "filament_volume_map") == 5);
}
