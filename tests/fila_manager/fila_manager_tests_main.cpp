// STUDIO-17977 / openspec 20260506耗材管理器AMS同步渐变多拼色:
// Catch2 main entry for fila_manager unit tests.
//
// This target is intentionally lean: it links libslic3r + test_common only,
// and compiles wgtFilaManagerStore.cpp directly into the executable so the
// test never has to drag in libslic3r_gui / wxWidgets. Future fila_manager
// changes (e.g. AMS auto-sync throttle) can extend the source list and
// follow the existing TEST_CASE convention to add their own [tag] groups.

#include <catch_main.hpp>

#include "slic3r/GUI/fila_manager/wgtFilaManagerStore.h"

using namespace Slic3r::GUI;

// Smoke test: keep at least one TEST_CASE so the binary is non-empty even
// before STUDIO-17977 Task 1 lands its [colors] cases. Verifies that the
// existing FilamentSpool serialization round-trip is wired up correctly
// through the test target (catches CMake / link-time regressions early).
TEST_CASE("FilamentSpool: default-constructed round-trip", "[fila_manager][smoke]")
{
    FilamentSpool s;
    auto j  = s.to_json();
    auto s2 = FilamentSpool::from_json(j);

    REQUIRE(s2.spool_id       == s.spool_id);
    REQUIRE(s2.color_code     == s.color_code);
    REQUIRE(s2.diameter       == Approx(s.diameter));
    REQUIRE(s2.remain_percent == s.remain_percent);
    REQUIRE(s2.status         == s.status);
}

// ---------------------------------------------------------------------------
// STUDIO-17977 / openspec 20260506耗材管理器AMS同步渐变多拼色
// FilamentSpool colors / color_type fields: JSON round-trip + invariant
// ---------------------------------------------------------------------------

TEST_CASE("FilamentSpool: from_json missing colors/color_type falls back to single color",
          "[fila_manager][colors]")
{
    nlohmann::json j;
    j["spool_id"]   = "S1";
    j["color_code"] = "#00AE42";
    // no colors / color_type fields (legacy schema)
    auto s = FilamentSpool::from_json(j);
    REQUIRE(s.color_code == "#00AE42");
    REQUIRE(s.colors.empty());
    REQUIRE(s.color_type == 2); // 2 == single colour (DevFilaColorType::CTYPE_SINGLE)
}

TEST_CASE("FilamentSpool: from_json gradient", "[fila_manager][colors]")
{
    nlohmann::json j;
    j["spool_id"]   = "S2";
    j["color_code"] = "#0066FF";
    j["colors"]     = nlohmann::json::array({"#0066FF", "#00AA88"});
    j["color_type"] = 0; // gradient
    auto s = FilamentSpool::from_json(j);
    REQUIRE(s.colors == std::vector<std::string>{"#0066FF", "#00AA88"});
    REQUIRE(s.color_type == 0);
    // invariant: when colors is non-empty, color_code must equal colors[0]
    REQUIRE(s.color_code == "#0066FF");
}

TEST_CASE("FilamentSpool: to_json round-trip preserves colors/color_type",
          "[fila_manager][colors]")
{
    FilamentSpool s;
    s.spool_id   = "S3";
    s.color_code = "#0066FF";
    s.colors     = {"#0066FF", "#00AA88", "#88FF00"};
    s.color_type = 1; // multicolor
    auto j  = s.to_json();
    auto s2 = FilamentSpool::from_json(j);
    REQUIRE(s2.colors     == s.colors);
    REQUIRE(s2.color_type == 1);
    REQUIRE(s2.color_code == "#0066FF");
}

TEST_CASE("FilamentSpool: to_json single-color spool emits empty colors[] and color_type=2",
          "[fila_manager][colors]")
{
    FilamentSpool s;
    s.spool_id   = "S4";
    s.color_code = "#00AE42";
    // colors empty, color_type defaults to 2
    auto j = s.to_json();
    REQUIRE(j.contains("colors"));
    REQUIRE(j["colors"].is_array());
    REQUIRE(j["colors"].empty());
    REQUIRE(j["color_type"] == 2);
}

// ---------------------------------------------------------------------------
// STUDIO-17977 Task 2: sync writes colors/color_type;
//                       identity color_code stays frozen.
// Verifies the (now extended) sync-cared field set in
// update_spool_if_changed - covers colors[] / color_type as
// non-identity, sync-mutable fields.
// ---------------------------------------------------------------------------

namespace {
    // Inline seed spool helper so we don't depend on a make_seed_spool that
    // doesn't exist yet in the test corpus.
    FilamentSpool make_seed_spool_for_colors(const std::string& sid,
                                              const std::string& tag)
    {
        FilamentSpool s;
        s.spool_id     = sid;
        s.tag_uid      = tag;
        s.setting_id   = "GFB99";
        s.color_code   = "#00AE42";
        s.color_name   = "Bambu Green";
        s.material_type= "PLA";
        s.brand        = "Bambu Lab";
        s.series       = "PLA Basic";
        s.diameter     = 1.75f;
        s.initial_weight = 1000.0f;
        s.remain_percent = 100;
        s.status         = "active";
        return s;
    }
}

TEST_CASE("Store::update_spool_if_changed: colors changed -> write through, color_code frozen",
          "[fila_manager][colors][store]")
{
    using namespace Slic3r::GUI;
    wgtFilaManagerStore store;
    auto seed = make_seed_spool_for_colors("S1", "RFID_1");
    seed.spool_id = store.add_spool(seed); // store mints / accepts the id

    FilamentSpool sp = *store.get_spool(seed.spool_id);
    sp.colors        = {"#0066FF", "#00AA88"};
    sp.color_type    = 0;                 // gradient (swagger value)
    sp.net_weight    = 800.0f;            // also bump a sync-cared scalar
    sp.remain_percent = 80;

    REQUIRE(store.update_spool_if_changed(sp) == true);

    const auto* after = store.get_spool(seed.spool_id);
    REQUIRE(after != nullptr);
    CHECK(after->colors     == std::vector<std::string>{"#0066FF", "#00AA88"});
    CHECK(after->color_type == 0);
    // color_code is identity-frozen: must stay seed value, NOT change to colors[0]
    CHECK(after->color_code == "#00AE42");
}

TEST_CASE("Store::update_spool_if_changed: only colors/color_type changed still triggers write",
          "[fila_manager][colors][store]")
{
    using namespace Slic3r::GUI;
    wgtFilaManagerStore store;
    auto seed = make_seed_spool_for_colors("S2", "RFID_2");
    seed.spool_id = store.add_spool(seed);

    FilamentSpool sp = *store.get_spool(seed.spool_id);
    sp.color_type    = 1;                 // multicolor (swagger value)
    // colors stays empty intentionally - this exercises the "only color_type
    // moved" edge case, mirroring an AMS that toggles ctype on a slot whose
    // hex list hasn't been parsed yet.

    REQUIRE(store.update_spool_if_changed(sp) == true);
    CHECK(store.get_spool(seed.spool_id)->color_type == 1);
}

TEST_CASE("Store::update_spool_if_changed: no sync-cared field changed -> no write",
          "[fila_manager][colors][store]")
{
    using namespace Slic3r::GUI;
    wgtFilaManagerStore store;
    auto seed = make_seed_spool_for_colors("S3", "RFID_3");
    seed.spool_id = store.add_spool(seed);

    // Build sp from store snapshot - by definition all sync-cared fields equal cur.
    FilamentSpool sp = *store.get_spool(seed.spool_id);

    REQUIRE(store.update_spool_if_changed(sp) == false);
}
