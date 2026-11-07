#define CATCH_CONFIG_MAIN
#include "catch2/catch.hpp"
#include "slic3r/GUI/FilamentCatalog.hpp"
#include "slic3r/GUI/FilamentCatalogDialog.hpp"
#include "libslic3r/PresetBundle.hpp"

using namespace Slic3r;
using namespace Slic3r::GUI;

TEST_CASE("Color discovery searches across families, translations and product codes")
{
    FilamentColorCodes matte("GFA01", "PLA Matte"), basic("GFA00", "PLA Basic"), cf("GFA03", "PLA-CF");
    FilamentColorCode plum("11204", "R3", &matte, {}, {{"en", "Plum"}, {"de", "Pflaume"}, {"zh", wxString::FromUTF8("莓果紫")}});
    FilamentColorCode orange("11300", "A2", &matte, {}, {{"en", "Mandarin Orange"}, {"fr", "Mandarine"}});
    FilamentColorCode basic_black("10101", "N1", &basic, {}, {{"en", "Black"}});
    FilamentColorCode cf_black("14101", "N1", &cf, {}, {{"en", "Black"}});
    CHECK(filament_catalog_matches(plum, "PLuM"));
    CHECK(filament_catalog_matches(plum, "  plum  matte "));
    CHECK(filament_catalog_matches(plum, "11204"));
    CHECK(filament_catalog_matches(plum, "pflaum"));
    CHECK(filament_catalog_matches(plum, wxString::FromUTF8("莓果")));
    CHECK(filament_catalog_matches(plum, "Bambu"));
    CHECK(filament_catalog_matches(plum, ""));
    CHECK(filament_catalog_matches(orange, "mand"));
    CHECK(filament_catalog_matches(orange, "orange matte"));
    CHECK(filament_catalog_matches(basic_black, "Black"));
    CHECK(filament_catalog_matches(cf_black, "Black"));
    CHECK_FALSE(filament_catalog_matches(basic_black, "Black CF"));
    CHECK(filament_catalog_matches(cf_black, "Black CF"));
    CHECK_FALSE(filament_catalog_matches(plum, "11205"));
    CHECK_FALSE(filament_catalog_matches(plum, "Plumb")); // fuzzy matching is outside the MVP
    CHECK_FALSE(filament_catalog_matches(orange, "orange basic"));
}

TEST_CASE("Catalog colors preserve alpha, component order and gradient identity")
{
    FilamentColor gradient;
    gradient.AddColor(wxColour(0x95, 0, 0x51, 0x80));
    gradient.AddColor(wxColour(0xF9, 0x99, 0x63, 0xFF));
    gradient.EndSet(0);
    CHECK(gradient.m_color_type == FilamentColor::ColorType::GRADIENT_CLR);
    CHECK(filament_catalog_colors(gradient) == std::vector<std::string>{"#95005180", "#F99963FF"});
    gradient.EndSet(1);
    CHECK(gradient.m_color_type == FilamentColor::ColorType::MULTI_CLR);
    CHECK(filament_catalog_colors(gradient) == std::vector<std::string>{"#95005180", "#F99963FF"});
    FilamentColor clear;
    clear.AddColor(wxColour(0, 0, 0, 0));
    clear.EndSet(0);
    CHECK(filament_catalog_colors(clear) == std::vector<std::string>{"#00000000"});
    CHECK(filament_catalog_colors(FilamentColor{}).empty());
}

static Preset& add_filament(PresetBundle& bundle, const std::string& name, const std::string& id,
                            bool compatible, bool system, const std::string& printer = "")
{
    auto config = bundle.filaments.default_preset().config;
    config.set_key_value("compatible_printers", new ConfigOptionStrings({printer}));
    Preset& preset = bundle.filaments.load_preset("", name, std::move(config), false);
    preset.filament_id = id;
    preset.is_system = system;
    preset.is_compatible = compatible;
    preset.is_visible = false;
    return preset;
}

TEST_CASE("Discovery resolves exact compatible system profiles independently of AMS and enabled presets")
{
    PresetBundle bundle;
    bundle.printers.get_edited_preset().config.opt_string("printer_variant", true) = "0.4";
    add_filament(bundle, "A incompatible Matte", "GFA01", false, true);
    add_filament(bundle, "B custom Matte", "GFA01", true, false);
    add_filament(bundle, "C Basic", "GFA00", true, true);
    add_filament(bundle, "D compatible Matte", "GFA01", true, true);
    CHECK(bundle.filament_ams_list.empty());
    CHECK(find_catalog_profile(bundle, 0, "GFA01") == "D compatible Matte");
    CHECK(find_catalog_profile(bundle, 0, "GFA00") == "C Basic");
    CHECK(find_catalog_profile(bundle, 0, "unknown").empty());
    CHECK_FALSE(bundle.filaments.find_preset("D compatible Matte")->is_visible);
}

TEST_CASE("Multi-diameter discovery respects the target slot instead of the main printer diameter")
{
    PresetBundle bundle;
    for (const std::string diameter : {"0.2", "0.4"}) {
        auto config = bundle.printers.default_preset().config;
        config.opt_string("printer_model", true) = "Test Printer";
        config.opt_string("printer_variant", true) = diameter;
        bundle.printers.load_preset("", "Printer " + diameter, std::move(config), true);
    }
    bundle.project_config.set_key_value("multi_nozzle_diameter_mode", new ConfigOptionBool(true));
    bundle.project_config.set_key_value("filament_diameter_map", new ConfigOptionStrings({"0.2", "0.4", "0.6"}));
    add_filament(bundle, "Matte 0.4", "GFA01", true, true, "Printer 0.4");
    add_filament(bundle, "Matte 0.2", "GFA01", true, true, "Printer 0.2");
    CHECK(find_catalog_profile(bundle, 0, "GFA01") == "Matte 0.2");
    CHECK(find_catalog_profile(bundle, 1, "GFA01") == "Matte 0.4");
    CHECK(find_catalog_profile(bundle, 2, "GFA01").empty());
}

TEST_CASE("Inventory colors preserve complete identity and reject missing or malformed values")
{
    FilamentSpool spool;
    spool.color_code = " ffffff ";
    CHECK(filament_catalog_colors(inventory_filament_color(spool)) == std::vector<std::string>{"#FFFFFFFF"});
    spool.color_code = "#00000000";
    CHECK(filament_catalog_colors(inventory_filament_color(spool)) == std::vector<std::string>{"#00000000"});
    spool.colors = {"#950051FF", "#F99963FF"};
    spool.color_type = 0;
    auto gradient = inventory_filament_color(spool);
    CHECK(gradient.m_color_type == FilamentColor::ColorType::GRADIENT_CLR);
    CHECK(filament_catalog_colors(gradient) == spool.colors);
    spool.colors[1] = "not a color";
    CHECK(inventory_filament_color(spool).GetColors().empty());
    spool.colors.clear();
    spool.color_code.clear();
    CHECK(inventory_filament_color(spool).GetColors().empty());
}

TEST_CASE("Owned spool discovery does not depend on mounting or unknown remaining weight")
{
    FilamentSpool spool;
    spool.net_weight = 0;
    spool.in_printer = false;
    CHECK(inventory_spool_available(spool));
    spool.in_printer = true;
    CHECK(inventory_spool_available(spool));
    spool.status = "low";
    CHECK(inventory_spool_available(spool));
    spool.status = "empty";
    CHECK_FALSE(inventory_spool_available(spool));
    spool.status = "archived";
    CHECK_FALSE(inventory_spool_available(spool));
}

TEST_CASE("Inventory searches include vendor, material, hex and user notes without invented names")
{
    FilamentSpool spool;
    spool.brand = "Elegoo";
    spool.material_type = "PETG";
    spool.series = "Rapid";
    CHECK(inventory_material_name(spool) == "PETG Rapid");
    spool.series = "PETG Rapid";
    CHECK(inventory_material_name(spool) == "PETG Rapid");
    spool.series = "Elegoo PETG Rapid";
    CHECK(inventory_material_name(spool) == "PETG Rapid");
    CHECK(filament_search_matches("Elegoo PETG Rapid #F72323FF red for mahjong", "elegoo red"));
    CHECK(filament_search_matches("Elegoo PETG Rapid #F72323FF", "f72323"));
    CHECK_FALSE(filament_search_matches("Elegoo PETG Rapid #F72323FF", "Bambu"));
}

TEST_CASE("Non-Bambu inventory resolves its own profiles and never a borrowed Bambu identity")
{
    PresetBundle bundle;
    bundle.printers.get_edited_preset().config.opt_string("printer_variant", true) = "0.4";
    auto& bambu = add_filament(bundle, "Bambu PLA Basic", "GFA00", true, true);
    bambu.config.set_key_value("filament_vendor", new ConfigOptionStrings({"Bambu Lab"}));
    auto& elegoo = add_filament(bundle, "Elegoo PETG Rapid", "ELEG001", true, false);
    elegoo.config.set_key_value("filament_vendor", new ConfigOptionStrings({"ELEGOO"}));
    FilamentSpool spool;
    spool.brand = "  elegoo  ";
    spool.filament_id = "ELEG001";
    spool.material_type = "PETG";
    spool.series = "Rapid";
    CHECK(find_inventory_profile(bundle, 0, spool) == "Elegoo PETG Rapid");
    spool.filament_id.clear();
    CHECK(find_inventory_profile(bundle, 0, spool) == "Elegoo PETG Rapid");
    spool.filament_id = "GFA00"; // stale cross-brand identity must not select a Bambu profile
    CHECK(find_inventory_profile(bundle, 0, spool) == "Elegoo PETG Rapid");
    spool.material_type = "PLA";
    spool.series = "Basic";
    CHECK(find_inventory_profile(bundle, 0, spool).empty());
    spool.brand = "Unknown vendor";
    CHECK(find_inventory_profile(bundle, 0, spool).empty());
    spool.brand = "Bambu";
    CHECK(find_inventory_profile(bundle, 0, spool) == "Bambu PLA Basic");
    bundle.filaments.find_preset("Bambu PLA Basic")->is_compatible = false;
    CHECK(find_inventory_profile(bundle, 0, spool).empty());
}

TEST_CASE("Remaining filament weight prefers measurements and estimates net material only")
{
    FilamentSpool spool;
    CHECK_FALSE(inventory_remaining_weight(spool).has_value());
    spool.initial_weight = 1250;
    spool.spool_weight = 250;
    spool.remain_percent = 50;
    REQUIRE(inventory_remaining_weight(spool).has_value());
    CHECK(*inventory_remaining_weight(spool) == Approx(500));
    spool.net_weight = 487;
    CHECK(*inventory_remaining_weight(spool) == Approx(487));
    spool.net_weight = 0;
    spool.remain_percent = 0;
    CHECK(*inventory_remaining_weight(spool) == Approx(0));
    spool.remain_percent = 101;
    CHECK_FALSE(inventory_remaining_weight(spool).has_value());
    spool.remain_percent = 100;
    spool.initial_weight = spool.spool_weight;
    CHECK_FALSE(inventory_remaining_weight(spool).has_value());
}

TEST_CASE("Combined spool weights retain unknown contributions instead of implying a complete total")
{
    InventoryRemaining remaining;
    FilamentSpool spool;
    spool.net_weight = 700;
    remaining.add(spool);
    remaining.add(spool);
    CHECK(remaining.spools == 2);
    CHECK(remaining.unknown == 0);
    CHECK(remaining.grams == Approx(1400));
    remaining.add(FilamentSpool{});
    CHECK(remaining.spools == 3);
    CHECK(remaining.unknown == 1);
    CHECK(remaining.grams == Approx(1400));
}

TEST_CASE("Remaining sorts numerically in either direction and keeps missing weights last")
{
    const InventoryRemaining light{1, 0, 50}, heavy{2, 0, 1400}, zero{1, 0, 0};
    const InventoryRemaining partial{2, 1, 20}, unknown{1, 1, 0}, unowned{};
    CHECK(compare_inventory_remaining(light, heavy, true) < 0);
    CHECK(compare_inventory_remaining(light, heavy, false) > 0);
    CHECK(compare_inventory_remaining(zero, light, true) < 0);
    for (bool ascending : {false, true}) {
        CHECK(compare_inventory_remaining(light, partial, ascending) < 0);
        CHECK(compare_inventory_remaining(heavy, unknown, ascending) < 0);
        CHECK(compare_inventory_remaining(partial, unknown, ascending) < 0);
        CHECK(compare_inventory_remaining(unknown, unowned, ascending) < 0);
        CHECK(compare_inventory_remaining(light, light, ascending) == 0);
    }
}

TEST_CASE("Merged inventory rows retain distinct notes without repeating duplicates")
{
    InventoryNotes notes;
    CHECK(notes.add("  ").empty());
    CHECK(notes.add("Orange for mahjong") == "Orange for mahjong");
    CHECK(notes.add("  Orange for mahjong  ") == "Orange for mahjong");
    CHECK(notes.add("Opened spool; dry first") == "Orange for mahjong; Opened spool; dry first");
    CHECK(notes.values.size() == 2);
    CHECK(notes.add("") == "Orange for mahjong; Opened spool; dry first");
    CHECK(filament_search_matches(notes.add(""), "mahjong dry"));
}
