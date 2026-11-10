// Owned-spool swatches for the colour pickers: which Filament Manager spools
// show up when the user picks a colour for a filament of a given type.

#include <catch_main.hpp>

#include <boost/filesystem.hpp>
#include <fstream>

#include "slic3r/GUI/fila_manager/wgtFilaManagerSpoolColors.h"

using namespace Slic3r::GUI;

namespace {

FilamentSpool spool(const std::string& brand, const std::string& series, const std::string& colour,
                    const std::string& colour_name)
{
    FilamentSpool s;
    s.brand      = brand;
    s.series     = series;
    s.color_code = colour;
    s.color_name = colour_name;
    return s;
}

std::vector<std::string> labels(const std::vector<SpoolSwatch>& swatches)
{
    std::vector<std::string> out;
    for (const auto& s : swatches) out.push_back(s.label);
    return out;
}

} // namespace

TEST_CASE("Spool swatches: spools whose type is the filament's type are offered, any brand",
          "[fila_manager][spool_colors]")
{
    std::vector<FilamentSpool> spools = {
        spool("Overture", "PLA Matte", "#6E8451", "Army Green"),
        spool("Polymaker", "PLA Matte", "#F2EEE3", "Bone White"),
        spool("Elegoo", "PETG Translucent", "#3355FF", "Blue"),
    };

    auto swatches = spool_swatches_for(spools, "PLA Matte", SpoolColorRules{});

    REQUIRE(labels(swatches) == std::vector<std::string>{"Overture Army Green", "Polymaker Bone White"});
    REQUIRE(swatches[0].colors == std::vector<std::string>{"#6E8451"});
}

TEST_CASE("Spool swatches: type comparison ignores case and surrounding spaces",
          "[fila_manager][spool_colors]")
{
    std::vector<FilamentSpool> spools = {spool("Jayo", " petg matte ", "#222222", "Black")};

    REQUIRE(labels(spool_swatches_for(spools, "PETG Matte", SpoolColorRules{})) ==
            std::vector<std::string>{"Jayo Black"});
}

TEST_CASE("Spool swatches: a rule lists the filament types a spool type is offered under",
          "[fila_manager][spool_colors]")
{
    std::vector<FilamentSpool> spools = {
        spool("Elegoo", "PLA+", "#000000", "Black"),
        spool("Elegoo", "PETG Rapid", "#FFFFFF", "White"),
    };
    auto rules = SpoolColorRules::from_json(nlohmann::json::parse(R"({
        "type_aliases": { "PLA+": "PLA Basic", "PETG Rapid": ["PETG HF", "PETG Basic"] }
    })"));

    REQUIRE(labels(spool_swatches_for(spools, "PLA Basic", rules)) == std::vector<std::string>{"Elegoo Black"});
    REQUIRE(labels(spool_swatches_for(spools, "PETG HF", rules)) == std::vector<std::string>{"Elegoo White"});
    REQUIRE(labels(spool_swatches_for(spools, "PETG Basic", rules)) == std::vector<std::string>{"Elegoo White"});
    // A spool type with a rule is no longer offered under its own name.
    REQUIRE(spool_swatches_for(spools, "PLA+", rules).empty());
}

TEST_CASE("Spool swatches: a 'Prints With' link offers a spool whose type doesn't match",
          "[fila_manager][spool_colors]")
{
    // Elegoo PLA+ pointed at the Bambu PLA Basic preset through the Filament
    // Manager "Prints With" field: its type is "PLA+", which no rule maps to
    // "PLA Basic", but its filament_id is that preset's, so it belongs there.
    FilamentSpool s = spool("Elegoo", "PLA+", "#6E8451", "Army Green");
    s.filament_id   = "GFL99";

    // No link target, no rule: the "PLA+" spool is not offered under "PLA Basic".
    REQUIRE(spool_swatches_for({s}, "PLA Basic", SpoolColorRules{}).empty());
    // With the preset's filament_id as the link target, it is offered.
    REQUIRE(labels(spool_swatches_for({s}, "PLA Basic", SpoolColorRules{}, "GFL99")) ==
            std::vector<std::string>{"Elegoo Army Green"});
    // A different preset's filament_id must not pull it in.
    REQUIRE(spool_swatches_for({s}, "PLA Basic", SpoolColorRules{}, "GFL00").empty());
}

TEST_CASE("Spool swatches: the note names a custom colour, minus the '(N bought)' cruft",
          "[fila_manager][spool_colors]")
{
    // The Filament Manager has no free-text colour-name field, so a custom
    // colour's name lives in the note. Use it when there is no colour name, but
    // strip the trailing "(N bought)" the importer appends.
    FilamentSpool s = spool("Overture", "PLA Matte", "#6E8451", "");
    s.note          = "Army Green (2 bought)";

    REQUIRE(labels(spool_swatches_for({s}, "PLA Matte", SpoolColorRules{})) ==
            std::vector<std::string>{"Overture Army Green"});
}

TEST_CASE("Spool swatches: a colour name wins over the note", "[fila_manager][spool_colors]")
{
    FilamentSpool s = spool("Overture", "PLA Matte", "#6E8451", "Olive");
    s.note          = "Army Green (2 bought)";

    REQUIRE(labels(spool_swatches_for({s}, "PLA Matte", SpoolColorRules{})) ==
            std::vector<std::string>{"Overture Olive"});
}

TEST_CASE("Spool swatches: with neither colour name nor note the label is brand-only",
          "[fila_manager][spool_colors]")
{
    FilamentSpool s = spool("Overture", "PLA Matte", "#6E8451", "");
    REQUIRE(labels(spool_swatches_for({s}, "PLA Matte", SpoolColorRules{})) ==
            std::vector<std::string>{"Overture"});
}

TEST_CASE("Spool swatches: empty and archived spools are not offered", "[fila_manager][spool_colors]")
{
    FilamentSpool used_up  = spool("Elegoo", "PLA Matte", "#111111", "Black");
    used_up.status         = "empty";
    FilamentSpool archived = spool("Elegoo", "PLA Matte", "#222222", "Gray");
    archived.status        = "archived";
    FilamentSpool low      = spool("Elegoo", "PLA Matte", "#333333", "Slate");
    low.status             = "low";

    REQUIRE(labels(spool_swatches_for({used_up, archived, low}, "PLA Matte", SpoolColorRules{})) ==
            std::vector<std::string>{"Elegoo Slate"});
}

TEST_CASE("Spool swatches: several spools of the same colour show once", "[fila_manager][spool_colors]")
{
    std::vector<FilamentSpool> spools = {
        spool("Elegoo", "PLA Matte", "#000000", "Black"),
        spool("Elegoo", "PLA Matte", "#000000", "Black"),
    };

    REQUIRE(spool_swatches_for(spools, "PLA Matte", SpoolColorRules{}).size() == 1);
}

TEST_CASE("Spool swatches: multi-colour spools keep all colours and their colour type",
          "[fila_manager][spool_colors]")
{
    FilamentSpool s = spool("Elegoo", "PLA Silk", "#FFD700", "Gold Silver");
    s.colors        = {"#FFD700", "#C0C0C0"};
    s.color_type    = 1;

    auto swatches = spool_swatches_for({s}, "PLA Silk", SpoolColorRules{});

    REQUIRE(swatches.size() == 1);
    REQUIRE(swatches[0].colors == std::vector<std::string>{"#FFD700", "#C0C0C0"});
    REQUIRE(swatches[0].color_type == 1);
}

TEST_CASE("Spool colour rules: a missing or broken rules file means no rules",
          "[fila_manager][spool_colors]")
{
    namespace fs = boost::filesystem;
    fs::path dir = fs::temp_directory_path() / fs::unique_path("spool-rules-%%%%");
    fs::create_directories(dir);

    REQUIRE(SpoolColorRules::load((dir / "missing.json").string()).type_aliases.empty());

    std::ofstream((dir / "broken.json").string()) << "{ not json";
    REQUIRE(SpoolColorRules::load((dir / "broken.json").string()).type_aliases.empty());

    std::ofstream((dir / "good.json").string()) << R"({"type_aliases": {"PLA Rock": "PLA Marble"}})";
    REQUIRE(SpoolColorRules::load((dir / "good.json").string()).type_aliases.at("pla rock") ==
            std::vector<std::string>{"PLA Marble"});

    fs::remove_all(dir);
}

TEST_CASE("Spool preset types: a spool maps to the filament types named by its type or its rule",
          "[fila_manager][spool_colors]")
{
    auto rules = SpoolColorRules::from_json(nlohmann::json::parse(R"({
        "type_aliases": { "TPU Air 85A 270c": "TPU Air", "PETG Rapid": ["PETG HF", "PETG Basic"] }
    })"));

    REQUIRE(spool_preset_types(spool("Overture", "PLA Matte", "#6E8451", "Army Green"), rules) ==
            std::vector<std::string>{"PLA Matte"});
    REQUIRE(spool_preset_types(spool("Siraya Tech", " tpu air 85a 270c ", "#000000", "Black"), rules) ==
            std::vector<std::string>{"TPU Air"});
    REQUIRE(spool_preset_types(spool("Elegoo", "PETG Rapid", "#FFFFFF", "White"), rules) ==
            std::vector<std::string>{"PETG HF", "PETG Basic"});

    FilamentSpool no_series = spool("Elegoo", "", "#000000", "Black");
    no_series.material_type = "PETG";
    REQUIRE(spool_preset_types(no_series, rules) == std::vector<std::string>{"PETG"});
}
