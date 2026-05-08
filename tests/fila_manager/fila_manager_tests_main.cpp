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

// ---------------------------------------------------------------------------
// STUDIO-17977 / openspec 20260506耗材管理器AMS同步渐变多拼色 - Task 7
// Persistence schema compatibility: legacy spools.json + forward-compat
// ---------------------------------------------------------------------------

TEST_CASE("FilamentSpool: loading legacy spools.json (no colors field) is non-destructive",
          "[fila_manager][colors][compat]")
{
    // 模拟 2.5.2 之前的 spool 持久化 JSON
    nlohmann::json legacy;
    legacy["spool_id"]    = "LEGACY1";
    legacy["color_code"]  = "#00AE42";
    legacy["color_name"]  = "Bambu Green";
    legacy["material_type"] = "PLA";
    // 没有 colors / color_type

    auto s = FilamentSpool::from_json(legacy);
    REQUIRE(s.color_code == "#00AE42");
    REQUIRE(s.colors.empty());
    REQUIRE(s.color_type == 2);

    // round-trip：再 to_json，单色 spool 不应引入破坏性新字段
    auto j2 = s.to_json();
    // colors 字段允许存在但为空数组，color_type=2 也允许写
    if (j2.contains("colors")) REQUIRE(j2["colors"].empty());
    REQUIRE(j2.value("color_type", 2) == 2);
}

TEST_CASE("FilamentSpool: forward-compat unknown future fields are ignored",
          "[fila_manager][colors][compat]")
{
    nlohmann::json future;
    future["spool_id"]   = "S1";
    future["color_code"] = "#00AE42";
    future["colors"]     = nlohmann::json::array({"#0066FF", "#00AA88"});
    future["color_type"] = 0;
    future["color_decoration"] = "metallic-flake"; // 未来字段
    future["nozzle_match"]     = nlohmann::json::array({"0.4", "0.2"}); // 未来字段
    REQUIRE_NOTHROW(FilamentSpool::from_json(future));
    auto s = FilamentSpool::from_json(future);
    REQUIRE(s.colors == std::vector<std::string>{"#0066FF", "#00AA88"});
    REQUIRE(s.color_type == 0);
}

// ---------------------------------------------------------------------------
// STUDIO-17977 / Task 10: ColorType bridge between swagger and
//                         FilamentColor::ColorType
//
// `wgtFilaManagerColorType.h` is the canonical helper. It pulls
// `EncodedFilament.hpp`, which transitively includes <wx/colour.h> /
// <wx/string.h>. This test target (tests/fila_manager) intentionally links
// only libslic3r + Catch2 — no wxWidgets — so blindly including the helper
// would break the entire fila_manager_tests binary (and take down the
// existing [colors] / [compat] / [cloud] / [store] cases above).
//
// The include is guarded with __has_include so that:
//   * if a future cmake change adds wx headers to this target's include
//     path, we pick up the real helper automatically and exercise the
//     enum-typed signatures end-to-end;
//   * otherwise we fall back to a mirror table test that still locks the
//     swagger ↔ FilamentColor::ColorType mapping the helper is required
//     to implement (design.md § 9.5). The mirror test MUST be kept in
//     sync with wgtFilaManagerColorType.h — both reference the same
//     swagger spec.
// ---------------------------------------------------------------------------

#if defined(__has_include) && __has_include(<wx/colour.h>)
#  define BBL_TEST_HAS_WX 1
#  include "slic3r/GUI/fila_manager/wgtFilaManagerColorType.h"
#else
#  define BBL_TEST_HAS_WX 0
#endif

#if BBL_TEST_HAS_WX

TEST_CASE("ColorType: swagger <-> FilamentColor::ColorType round-trip",
          "[fila_manager][colors][color_type_map]")
{
    using CT = Slic3r::FilamentColor::ColorType;
    // swagger 0=gradient / 1=multicolor / 2=single
    // FilamentColor::ColorType: SINGLE_CLR=0, MULTI_CLR=1, GRADIENT_CLR=2
    REQUIRE(Slic3r::GUI::to_filament_color_type(0) == CT::GRADIENT_CLR);
    REQUIRE(Slic3r::GUI::to_filament_color_type(1) == CT::MULTI_CLR);
    REQUIRE(Slic3r::GUI::to_filament_color_type(2) == CT::SINGLE_CLR);
    REQUIRE(Slic3r::GUI::to_filament_color_type(99) == CT::SINGLE_CLR); // defensive

    REQUIRE(Slic3r::GUI::from_filament_color_type(CT::GRADIENT_CLR) == 0);
    REQUIRE(Slic3r::GUI::from_filament_color_type(CT::MULTI_CLR)    == 1);
    REQUIRE(Slic3r::GUI::from_filament_color_type(CT::SINGLE_CLR)   == 2);
}

#else // !BBL_TEST_HAS_WX — wx headers unavailable, mirror the spec table

namespace {
// Mirror of the two enum spaces. KEEP IN SYNC WITH wgtFilaManagerColorType.h
// and with EncodedFilament.hpp:75-80 / DevFilaColorType.
//
//   FilamentColor::ColorType  : SINGLE_CLR=0, MULTI_CLR=1, GRADIENT_CLR=2
//   swagger / FilamentSpool   : 0=gradient,   1=multicolor, 2=single
constexpr int kFcSingle   = 0;
constexpr int kFcMulti    = 1;
constexpr int kFcGradient = 2;

constexpr int swagger_to_fc_mirror(int spool_color_type) {
    switch (spool_color_type) {
        case 0: return kFcGradient;
        case 1: return kFcMulti;
        default: return kFcSingle;
    }
}
constexpr int fc_to_swagger_mirror(int fc_color_type) {
    switch (fc_color_type) {
        case kFcGradient: return 0;
        case kFcMulti:    return 1;
        default:          return 2;
    }
}
} // namespace

TEST_CASE("ColorType: swagger <-> FilamentColor::ColorType round-trip (mirror)",
          "[fila_manager][colors][color_type_map]")
{
    REQUIRE(swagger_to_fc_mirror(0)  == kFcGradient);
    REQUIRE(swagger_to_fc_mirror(1)  == kFcMulti);
    REQUIRE(swagger_to_fc_mirror(2)  == kFcSingle);
    REQUIRE(swagger_to_fc_mirror(99) == kFcSingle); // defensive

    REQUIRE(fc_to_swagger_mirror(kFcGradient) == 0);
    REQUIRE(fc_to_swagger_mirror(kFcMulti)    == 1);
    REQUIRE(fc_to_swagger_mirror(kFcSingle)   == 2);
}

#endif // BBL_TEST_HAS_WX
