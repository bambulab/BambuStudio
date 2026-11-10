// Behavior spec for the Filament Manager CSV import/export round-trip
// (issue bambulab/BambuStudio#10453). Ported from the original webview test
// (device_page/tests/filamentImportExport.test.ts) when the feature moved into
// the C++ layer so it could live in the application's File > Import/Export menus.
//
// The workflow this encodes (David's step list on #10453):
//   export empty -> see schema; import rows with no id -> create; export ->
//   rows carry ids; edit + mark one deleted + add new -> import reconciles to
//   update / delete / create. Partial-apply: bad rows are reported, good rows
//   still planned. Deletes come only from an explicit marker, never absence.
//
// The schema is restricted to fields that actually round-trip through the
// cloud (Update endpoint + pull). Fields the backend silently drops are not
// columns at all, so an import never implies it can change them.

#include <catch2/catch.hpp>

#include <string>
#include <vector>

#include "slic3r/GUI/fila_manager/wgtFilaManagerCsv.h"
#include "slic3r/GUI/fila_manager/wgtFilaManagerStore.h"

using namespace Slic3r::GUI;

namespace {

// `note` is the persistable free-text field; tests use it both to make spools
// distinct and to assert that an edit round-trips.
FilamentSpool make_spool(const std::string& spool_id, const std::string& note = "")
{
    FilamentSpool s;
    s.spool_id       = spool_id;
    s.brand          = "eSUN";
    s.material_type  = "PLA+";
    s.series         = "";
    s.color_code     = "#FF0000";
    s.color_name     = "Red";       // not a CSV column; set to prove it is ignored
    s.diameter       = 1.75f;
    s.initial_weight = 1000;
    s.spool_weight   = 250;
    s.net_weight     = 1000;
    s.remain_percent = 100;
    s.status         = "active";
    s.favorite       = false;
    s.entry_method   = "manual";
    s.note           = note;
    return s;
}

std::string header_line()
{
    std::string h;
    for (size_t i = 0; i < filament_csv_columns().size(); ++i) {
        if (i) h += ",";
        h += filament_csv_columns()[i];
    }
    return h;
}

ImportPlan plan_for(const std::string& csv, const std::vector<FilamentSpool>& existing)
{
    return reconcile_import(parse_csv(csv), existing);
}

} // namespace

// --- schema ------------------------------------------------------------------

TEST_CASE("CSV columns are exactly the fields that persist to the backend", "[fila_manager][csv]")
{
    // Only fields the cloud Update endpoint accepts and the pull reads back
    // belong here; a column we can't actually persist (color_name, diameter,
    // weights other than net, status, favorite) would falsely imply it's
    // editable. `id`/`deleted` are structural.
    const std::vector<std::string> expected = {
        "id", "brand", "material_type", "series", "color_code",
        "net_weight", "note", "setting_id", "deleted",
    };
    REQUIRE(filament_csv_columns() == expected);
}

TEST_CASE("Export omits non-persistable fields like color_name", "[fila_manager][csv]")
{
    // make_spool sets color_name = "Red"; it must not leak into the file.
    const std::string csv = export_spools_to_csv({make_spool("s-1")});
    REQUIRE(csv.find("color_name") == std::string::npos);
    REQUIRE(csv.find("Red") == std::string::npos);
}

TEST_CASE("All-fields export appends read-only columns with a suffix", "[fila_manager][csv]")
{
    std::vector<FilamentSpool> inv = {make_spool("s-1")};
    inv[0].color_name = "Sunset";
    const std::string csv = export_spools_to_csv(inv, /*include_readonly=*/true, " (export only)");
    REQUIRE(csv.find("net_weight") != std::string::npos);                 // importable header plain
    REQUIRE(csv.find("color_name (export only)") != std::string::npos); // read-only header suffixed
    REQUIRE(csv.find("Sunset") != std::string::npos);                     // read-only value present
}

TEST_CASE("Re-importing an all-fields export ignores read-only columns", "[fila_manager][csv]")
{
    std::vector<FilamentSpool> inv = {make_spool("s-1")};
    inv[0].color_name = "Sunset";
    std::string csv = export_spools_to_csv(inv, /*include_readonly=*/true, " (export only)");
    const size_t pos = csv.find("Sunset"); // edit a read-only value (same length)
    REQUIRE(pos != std::string::npos);
    csv.replace(pos, 6, "Teal!!");
    const ImportPlan p = plan_for(csv, inv);
    REQUIRE(p.errors.empty());
    REQUIRE(p.updates.empty()); // importable fields unchanged -> no update
}

// --- export -----------------------------------------------------------------

TEST_CASE("CSV export of an empty inventory is a header-only file", "[fila_manager][csv]")
{
    const std::string csv = export_spools_to_csv({});
    REQUIRE(csv == header_line() + "\n");
}

TEST_CASE("CSV export then re-import with no edits plans nothing", "[fila_manager][csv]")
{
    // Re-importing an unmodified export must be a no-op: a row equal to the
    // spool it maps to is not an update. (David: importing after editing one
    // row should not re-push every spool.)
    std::vector<FilamentSpool> inv = {make_spool("s-1"), make_spool("s-2", "Blue")};
    const ImportPlan p = plan_for(export_spools_to_csv(inv), inv);
    REQUIRE(p.creates.empty());
    REQUIRE(p.deletes.empty());
    REQUIRE(p.errors.empty());
    REQUIRE(p.updates.empty());
}

TEST_CASE("Only rows that actually changed become updates", "[fila_manager][csv]")
{
    std::vector<FilamentSpool> inv = {make_spool("s-1", "Red"), make_spool("s-2", "Blue")};
    std::string csv = export_spools_to_csv(inv);
    const size_t pos = csv.find("Blue");  // edit exactly one field of s-2
    REQUIRE(pos != std::string::npos);
    csv.replace(pos, 4, "Teal");
    const ImportPlan p = plan_for(csv, inv);
    REQUIRE(p.errors.empty());
    REQUIRE(p.updates.size() == 1);
    REQUIRE(p.updates[0]["spool_id"] == "s-2");
    REQUIRE(p.updates[0]["note"] == "Teal");
}

TEST_CASE("CSV fields with commas/quotes survive the export->parse round-trip", "[fila_manager][csv]")
{
    std::vector<FilamentSpool> inv = {make_spool("s-1")};
    inv[0].note   = "bought 3/4, \"cheap\"";
    inv[0].series = "Matte, X1";
    const ParsedCsv parsed = parse_csv(export_spools_to_csv(inv));
    REQUIRE(parsed.rows.size() == 1);
    REQUIRE(parsed.rows[0].get("note") == "bought 3/4, \"cheap\"");
    REQUIRE(parsed.rows[0].get("series") == "Matte, X1");
}

TEST_CASE("A #-prefixed color_code survives export and re-import", "[fila_manager][csv]")
{
    // Real spools store color_code with a leading '#'; the export writes it
    // verbatim, so the importer must accept it back.
    std::vector<FilamentSpool> inv = {make_spool("s-1")};
    inv[0].color_code = "#FF911A";
    // One edited field (note) makes it a real update; the '#'-prefixed
    // color_code must parse without error and round-trip verbatim.
    const std::string csv = header_line() + "\n"
        "s-1,eSUN,PLA+,,#FF911A,1000,changed,,";
    const ImportPlan p = plan_for(csv, inv);
    REQUIRE(p.errors.empty());
    REQUIRE(p.updates.size() == 1);
    REQUIRE(p.updates[0]["color_code"] == "#FF911A");
}

// --- reconcile: create / update / delete ------------------------------------

TEST_CASE("A row with no id becomes a create", "[fila_manager][csv]")
{
    const std::string csv = header_line() + "\n"
        ",Sunlu,PETG,,00FF00,1000,,,";
    const ImportPlan p = plan_for(csv, {});
    REQUIRE(p.creates.size() == 1);
    REQUIRE(p.updates.empty());
    REQUIRE(p.creates[0]["brand"] == "Sunlu");
    REQUIRE_FALSE(p.creates[0].contains("spool_id"));
    REQUIRE(p.creates[0]["color_code"] == "#00FF00"); // bare hex normalized
}

TEST_CASE("A row whose id matches an existing spool becomes an update", "[fila_manager][csv]")
{
    std::vector<FilamentSpool> inv = {make_spool("s-1")};
    const std::string csv = header_line() + "\n"
        "s-1,eSUN,PLA+,,#FF0000,800,changed,,";
    const ImportPlan p = plan_for(csv, inv);
    REQUIRE(p.updates.size() == 1);
    REQUIRE(p.creates.empty());
    REQUIRE(p.updates[0]["spool_id"] == "s-1");
    REQUIRE(p.updates[0]["note"] == "changed");
}

TEST_CASE("A row with an id that matches nothing is an error, not a create", "[fila_manager][csv]")
{
    const std::string csv = header_line() + "\n"
        "ghost-99,eSUN,PLA+,,FF0000,1000,,,";
    const ImportPlan p = plan_for(csv, {make_spool("s-1")});
    REQUIRE(p.creates.empty());
    REQUIRE(p.updates.empty());
    REQUIRE(p.errors.size() == 1);
    REQUIRE(p.errors[0].row == 2); // 1-based source line
}

TEST_CASE("A row marked deleted with a valid id becomes a delete", "[fila_manager][csv]")
{
    std::vector<FilamentSpool> inv = {make_spool("s-1")};
    const std::string csv = header_line() + "\n"
        "s-1,eSUN,PLA+,,FF0000,1000,,,true";
    const ImportPlan p = plan_for(csv, inv);
    REQUIRE(p.deletes.size() == 1);
    REQUIRE(p.updates.empty());
    REQUIRE(p.deletes[0].spool_id == "s-1");
    REQUIRE(p.deletes[0].label.find("eSUN") != std::string::npos);
}

TEST_CASE("Delete label identifies the spool by brand, type, series, color and note", "[fila_manager][csv]")
{
    // The confirm dialog shows only this label, so it must carry enough to tell
    // one spool from another: brand + type alone (e.g. "Elegoo PLA") is too
    // vague when several share them. (David, reviewing the delete warning.)
    FilamentSpool s   = make_spool("s-1", "opened 11/9");
    s.brand           = "Elegoo";
    s.material_type   = "PLA";
    s.series          = "PLA+";
    s.color_name      = "Galaxy Black";
    const std::string csv = header_line() + "\n"
        "s-1,Elegoo,PLA,PLA+,#000000,1000,opened 11/9,,true";
    const ImportPlan p = plan_for(csv, {s});
    REQUIRE(p.deletes.size() == 1);
    const std::string& label = p.deletes[0].label;
    REQUIRE(label.find("Elegoo")       != std::string::npos); // brand
    REQUIRE(label.find("PLA+")         != std::string::npos); // series
    REQUIRE(label.find("Galaxy Black") != std::string::npos); // color
    REQUIRE(label.find("opened 11/9")  != std::string::npos); // note
    // Series supersedes the generic material_type; showing both ("PLA PLA+")
    // is redundant. (David: "Both PLA and PLA+ is silly.")
    REQUIRE(label.find("PLA PLA+")     == std::string::npos);
}

TEST_CASE("A row marked deleted with no/unknown id is an error", "[fila_manager][csv]")
{
    const std::string csv = header_line() + "\n"
        ",eSUN,PLA+,,FF0000,1000,,,true";
    const ImportPlan p = plan_for(csv, {});
    REQUIRE(p.deletes.empty());
    REQUIRE(p.errors.size() == 1);
}

// --- safety + partial apply -------------------------------------------------

TEST_CASE("A spool absent from the file is never deleted", "[fila_manager][csv]")
{
    std::vector<FilamentSpool> inv = {make_spool("s-1"), make_spool("s-2")};
    const std::string csv = header_line() + "\n"
        "s-1,eSUN,PLA+,,#FF0000,800,,,"; // s-1 edited (net weight); s-2 absent
    const ImportPlan p = plan_for(csv, inv);
    REQUIRE(p.deletes.empty());
    REQUIRE(p.updates.size() == 1);
}

TEST_CASE("Invalid rows are reported while valid rows are still planned", "[fila_manager][csv]")
{
    const std::string csv = header_line() + "\n"
        ",,PLA+,,FF0000,1000,,,\n"       // missing brand -> error
        ",Sunlu,PETG,,00FF00,1000,,,";   // good -> create
    const ImportPlan p = plan_for(csv, {});
    REQUIRE(p.creates.size() == 1);
    REQUIRE(p.errors.size() == 1);
    REQUIRE(p.errors[0].row == 2);
}

