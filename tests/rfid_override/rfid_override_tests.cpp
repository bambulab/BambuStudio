#define CATCH_CONFIG_MAIN
#include "catch2/catch.hpp"
#include "slic3r/GUI/fila_manager/RfidSpoolOverride.hpp"
#include <boost/filesystem.hpp>
#include <fstream>
using namespace Slic3r;
using namespace Slic3r::GUI;
namespace {
const std::string tag_a = "0123456789ABCDEF0123456789ABCDEF";
const std::string tag_b = "1123456789ABCDEF0123456789ABCDEF";
struct Temporary {
    boost::filesystem::path dir = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("rfid-%%%%-%%%%");
    std::string file() const { return (dir / "overrides.json").string(); }
    ~Temporary() { boost::system::error_code ec; boost::filesystem::remove_all(dir, ec); }
};
Preset& add_profile(PresetBundle& bundle, const std::string& name, const std::string& type) {
    auto cfg = bundle.filaments.default_preset().config;
    cfg.set_key_value("filament_type", new ConfigOptionStrings{type});
    auto& preset = bundle.filaments.load_preset("", name, std::move(cfg), false);
    preset.is_compatible = true;
    preset.filament_id = "custom-pla";
    return preset;
}
}
TEST_CASE("RFID assignments survive restart and distinguish otherwise identical spools") {
    Temporary tmp;
    RfidSpoolOverride a{"PLA", "#FF6600", {{"H2C 0.4", "Third party PLA"}}};
    RfidSpoolOverride b{"PLA", "#0000FF", {{"H2C 0.4", "Other PLA"}}};
    std::string error;
    {
        RfidSpoolOverrides store(tmp.file());
        REQUIRE(store.set(tag_a, a, error));
        REQUIRE(store.set(tag_b, b, error));
    }
    RfidSpoolOverrides reopened(tmp.file());
    REQUIRE(reopened.find(tag_a));
    CHECK(reopened.find("0123456789abcdef0123456789abcdef") != nullptr);
    REQUIRE(reopened.find(tag_b));
    CHECK(reopened.find(tag_a)->color == "#FF6600");
    CHECK(reopened.find(tag_b)->profiles.at("H2C 0.4") == "Other PLA");
    a.profiles["X1C 0.4"] = "Third party PLA X1C";
    REQUIRE(reopened.set(tag_a, a, error));
    CHECK(reopened.find(tag_a)->profiles.size() == 2);
    REQUIRE(reopened.remove(tag_a, error));
    RfidSpoolOverrides removed(tmp.file());
    CHECK(removed.find(tag_a) == nullptr);
    CHECK(removed.find(tag_b) != nullptr);
}
TEST_CASE("Malformed identifiers and colors cannot create assignments") {
    Temporary tmp;
    RfidSpoolOverrides store(tmp.file());
    std::string error;
    RfidSpoolOverride value{"PLA", "#123456", {{"printer", "profile"}}};
    for (const std::string tag : {"", "0", "0000000000000000", "../../anything", "G123456789ABCDEF"})
        CHECK_FALSE(store.set(tag, value, error));
    value.color = "orange";
    CHECK_FALSE(store.set(tag_a, value, error));
    CHECK(store.find(tag_a) == nullptr);
}
TEST_CASE("Unreadable or corrupted persistence does not overwrite existing data") {
    Temporary tmp;
    boost::filesystem::create_directories(tmp.dir);
    std::ofstream(tmp.file()) << "broken-json";
    RfidSpoolOverrides store(tmp.file());
    REQUIRE_FALSE(store.load_error().empty());
    std::string error;
    CHECK_FALSE(store.set(tag_a, {"PLA", "#123456", {{"printer", "profile"}}}, error));
    std::ifstream file(tmp.file());
    std::string data;
    std::getline(file, data);
    CHECK(data == "broken-json");
}
TEST_CASE("Failed saves do not activate an in-memory override") {
    Temporary tmp;
    boost::filesystem::create_directories(tmp.dir);
    std::ofstream(tmp.file()) << "not a directory";
    RfidSpoolOverrides store(tmp.file() + "/nested.json");
    std::string error;
    CHECK_FALSE(store.set(tag_a, {"PLA", "#123456", {{"printer", "profile"}}}, error));
    CHECK(store.find(tag_a) == nullptr);
}
TEST_CASE("Resolve exact custom profile, reject missing or incompatible assignments") {
    PresetBundle bundle;
    auto& printer = bundle.printers.get_edited_preset();
    printer.config.opt_string("printer_variant", true) = "0.4";
    const std::string printer_name = printer.name;
    add_profile(bundle, "Original PLA", "PLA");
    add_profile(bundle, "My PLA", "PLA");
    RfidSpoolOverride value{"PLA", "#FF6600", {{printer_name, "My PLA"}}};
    std::string error;
    REQUIRE(resolve_rfid_override(value, bundle, "PLA", 0.4, error));
    CHECK(resolve_rfid_override(value, bundle, "PLA", 0.4, error)->name == "My PLA");
    bundle.project_config.set_key_value("multi_nozzle_diameter_mode", new ConfigOptionBool(true));
    CHECK_FALSE(resolve_rfid_override(value, bundle, "PLA", 0.4, error));
    bundle.project_config.set_key_value("multi_nozzle_diameter_mode", new ConfigOptionBool(false));
    CHECK_FALSE(resolve_rfid_override(value, bundle, "PETG", 0.4, error));
    CHECK_FALSE(resolve_rfid_override(value, bundle, "PLA", 0.2, error));
    bundle.filaments.find_preset("My PLA", false)->is_compatible = false;
    CHECK_FALSE(resolve_rfid_override(value, bundle, "PLA", 0.4, error));
    value.profiles[printer_name] = "Deleted profile";
    CHECK_FALSE(resolve_rfid_override(value, bundle, "PLA", 0.4, error));
    value.profiles.clear();
    CHECK_FALSE(resolve_rfid_override(value, bundle, "PLA", 0.4, error));
}
TEST_CASE("Sync honors exact override instead of the shared base filament ID") {
    PresetBundle bundle;
    add_profile(bundle, "Base PLA", "PLA");
    add_profile(bundle, "My calibrated PLA", "PLA");
    DynamicPrintConfig tray;
    tray.set_key_value("filament_id", new ConfigOptionStrings{"custom-pla"});
    tray.set_key_value("filament_type", new ConfigOptionStrings{"PLA"});
    tray.set_key_value("filament_colour", new ConfigOptionStrings{"#FF6600"});
    tray.set_key_value("filament_multi_colour", new ConfigOptionStrings{"#FF6600"});
    tray.set_key_value("filament_colour_type", new ConfigOptionStrings{"2"});
    tray.set_key_value("tray_name", new ConfigOptionStrings{"A1"});
    tray.set_key_value("ams_id", new ConfigOptionStrings{"0"});
    tray.set_key_value("slot_id", new ConfigOptionStrings{"0"});
    tray.set_key_value("rfid_override_profile", new ConfigOptionStrings{"My calibrated PLA"});
    bundle.filament_ams_list[0] = tray;
    std::vector<std::pair<DynamicPrintConfig*, std::string>> unknowns;
    std::map<int, AMSMapInfo> maps;
    MergeFilamentInfo merge;
    SECTION("Exact profile wins over shared base ID") {
        bundle.sync_ams_list(unknowns, false, maps, false, merge, false);
        REQUIRE(bundle.filament_presets.size() == 1);
        CHECK(bundle.filament_presets[0] == "My calibrated PLA");
        CHECK(bundle.project_config.opt_string("filament_colour", 0u) == "#FF6600");
        CHECK(unknowns.empty());
    }
    SECTION("Missing profile leaves the project unchanged and reports the tray") {
        bundle.filament_ams_list[0].set_key_value("rfid_override_profile", new ConfigOptionStrings{"Deleted profile"});
        const auto before = bundle.filament_presets;
        CHECK(bundle.sync_ams_list(unknowns, false, maps, false, merge, false) == 0);
        CHECK(bundle.filament_presets == before);
        REQUIRE(unknowns.size() == 1);
        CHECK(unknowns.front().first == &bundle.filament_ams_list[0]);
    }
    SECTION("Compatibility errors cannot fall through to the original tag profile") {
        bundle.filament_ams_list[0].set_key_value("rfid_override_error", new ConfigOptionStrings{"No matching nozzle"});
        const auto before = bundle.filament_presets;
        CHECK(bundle.sync_ams_list(unknowns, false, maps, false, merge, false) == 0);
        CHECK(bundle.filament_presets == before);
        REQUIRE(unknowns.size() == 1);
        CHECK(unknowns.front().second == "No matching nozzle");
    }
}
