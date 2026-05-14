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
// STUDIO-18340: AMS sync may refresh weight/binding fields, but user-facing
// display identity (color_code / colors / color_type) must stay local.
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

TEST_CASE("Store::update_spool_if_changed: sync weight update keeps display fields frozen",
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
    CHECK(after->net_weight == 800.0f);
    CHECK(after->remain_percent == 80);
    CHECK(after->colors.empty());
    CHECK(after->color_type == 2);
    CHECK(after->color_code == "#00AE42");
}

TEST_CASE("Store::update_spool_if_changed: colors/color_type alone do not trigger sync write",
          "[fila_manager][colors][store]")
{
    using namespace Slic3r::GUI;
    wgtFilaManagerStore store;
    auto seed = make_seed_spool_for_colors("S2", "RFID_2");
    seed.spool_id = store.add_spool(seed);

    FilamentSpool sp = *store.get_spool(seed.spool_id);
    sp.color_type    = 1;                 // multicolor (swagger value)

    REQUIRE(store.update_spool_if_changed(sp) == false);
    CHECK(store.get_spool(seed.spool_id)->color_type == 2);
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

TEST_CASE("Store::find_by_tag_uid: all-zero placeholder is not a real RFID",
          "[fila_manager][store][rfid]")
{
    using namespace Slic3r::GUI;
    wgtFilaManagerStore store;
    auto zero = make_seed_spool_for_colors("S4", "0000000000000000");
    zero.spool_id = store.add_spool(zero);
    auto real = make_seed_spool_for_colors("S5", "D5191A1000000100");
    real.spool_id = store.add_spool(real);

    CHECK(FilamentSpool::is_valid_tag_uid("") == false);
    CHECK(FilamentSpool::is_valid_tag_uid("0000000000000000") == false);
    CHECK(FilamentSpool::is_valid_tag_uid("D5191A1000000100") == true);
    CHECK(store.find_by_tag_uid("0000000000000000") == nullptr);
    REQUIRE(store.find_by_tag_uid("D5191A1000000100") != nullptr);
    CHECK(store.find_by_tag_uid("D5191A1000000100")->spool_id == real.spool_id);
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

TEST_CASE("ColorType: AMS enum maps to swagger colour type",
          "[fila_manager][colors][color_type_map]")
{
    REQUIRE(Slic3r::GUI::from_ams_color_type(static_cast<Slic3r::DevFilaColorType>(0)) == 0);
    REQUIRE(Slic3r::GUI::from_ams_color_type(static_cast<Slic3r::DevFilaColorType>(1)) == 1);
    REQUIRE(Slic3r::GUI::from_ams_color_type(static_cast<Slic3r::DevFilaColorType>(2)) == 2);
}

#else // !BBL_TEST_HAS_WX — wx headers unavailable, mirror the spec table

namespace {
// Mirror of the two enum spaces. KEEP IN SYNC WITH wgtFilaManagerColorType.h
// and with EncodedFilament.hpp:75-80 / DevFilaColorType.
//
//   FilamentColor::ColorType    : SINGLE_CLR=0, MULTI_CLR=1, GRADIENT_CLR=2
//   swagger / FilamentSpool/AMS : 0=gradient,   1=multicolor, 2=single
constexpr int kFcSingle   = 0;
constexpr int kFcMulti    = 1;
constexpr int kFcGradient = 2;
constexpr int kAmsGradient = 0;
constexpr int kAmsMulti    = 1;
constexpr int kAmsSingle   = 2;

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
constexpr int ams_to_swagger_mirror(int ams_color_type) {
    switch (ams_color_type) {
        case kAmsGradient: return 0;
        case kAmsMulti:    return 1;
        default:           return 2;
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

    REQUIRE(ams_to_swagger_mirror(kAmsGradient) == 0);
    REQUIRE(ams_to_swagger_mirror(kAmsMulti)    == 1);
    REQUIRE(ams_to_swagger_mirror(kAmsSingle)   == 2);
}

#endif // BBL_TEST_HAS_WX

// ---------------------------------------------------------------------------
// STUDIO-18355: cloud round-trip invariants for filament-spool colour fields.
//
// Why mirror tests instead of exercising the real functions:
//   `wgtFilaManagerCloudSync::spool_to_cloud_update_patch` and
//   `wgtFilaManagerCloudSync::cloud_json_to_spool` live in
//   src/slic3r/GUI/fila_manager/wgtFilaManagerCloudSync.cpp, which transitively
//   includes slic3r/Utils/NetworkAgent.hpp + slic3r/GUI/GUI_App.hpp +
//   <wx/app.h>. Pulling those into this lean test target would break the
//   binary (no wx, no NetworkAgent linkage). Same trade-off as the
//   `swagger_to_fc_mirror` block above: lock the *invariants* in the test
//   corpus and KEEP IN SYNC with the production code. Both blocks use the
//   same comment convention.
//
// Invariants under test:
//   I1 (PUT side, STUDIO-18355 main fix): cloud body containing `color` ⇒
//      `colors` is non-empty AND `colors[0] == color`. Single-element
//      `colors` disagreeing with `color` is realigned to `[color]`. Multi-
//      element `colors` is left untouched (multicolor edits opt out).
//   I2 (PULL side, STUDIO-18355 defensive): the FilamentSpool produced from
//      a cloud response satisfies `colors.empty() || color_code == colors[0]`,
//      mirroring `FilamentSpool::from_json`.
// ---------------------------------------------------------------------------

namespace {

// Mirror of the relevant slice of
// `wgtFilaManagerCloudSync::spool_to_cloud_update_patch`. Only colour fields
// are reproduced — STUDIO-18355 does not touch other branches. KEEP IN SYNC
// with that function; in particular, any change to the
// "color ⇒ colors[0] == color" rule must be reflected here.
nlohmann::json mirror_spool_to_cloud_update_patch_color_subset(const nlohmann::json& p)
{
    nlohmann::json j = nlohmann::json::object();
    if (!p.is_object()) return j;
    if (p.contains("color_code") && p.at("color_code").is_string())
        j["color"] = p.at("color_code").get<std::string>();
    if (p.contains("colors") && p.at("colors").is_array())
        j["colors"] = p.at("colors");
    if (j.contains("color") && j.at("color").is_string()) {
        const std::string primary = j["color"].get<std::string>();
        const bool empty_or_missing = !j.contains("colors")
            || !j["colors"].is_array() || j["colors"].empty();
        if (empty_or_missing) {
            j["colors"] = nlohmann::json::array({primary});
        } else if (j["colors"].size() == 1 && j["colors"][0].is_string()
                   && j["colors"][0].get<std::string>() != primary) {
            j["colors"] = nlohmann::json::array({primary});
        }
    }
    return j;
}

// Mirror of the colour-relevant slice of
// `wgtFilaManagerCloudSync::cloud_json_to_spool`. KEEP IN SYNC.
struct CloudPullColourSubset {
    std::string color_code;
    std::vector<std::string> colors;
    int color_type {2};
};

CloudPullColourSubset mirror_cloud_json_to_spool_colour_subset(const nlohmann::json& j)
{
    CloudPullColourSubset s;
    if (j.contains("color") && j["color"].is_string())
        s.color_code = j["color"].get<std::string>();
    else if (j.contains("color_code") && j["color_code"].is_string())
        s.color_code = j["color_code"].get<std::string>();
    if (j.contains("colors") && j["colors"].is_array()) {
        for (const auto& hex : j["colors"]) {
            if (hex.is_string()) s.colors.push_back(hex.get<std::string>());
        }
    }
    if (j.contains("colorType") && j["colorType"].is_number())
        s.color_type = j["colorType"].get<int>();
    if (!s.colors.empty() && s.color_code != s.colors.front())
        s.color_code = s.colors.front();
    return s;
}

} // namespace

TEST_CASE("STUDIO-18355 (PUT): empty colors[] paired with new color collapses to [color]",
          "[fila_manager][colors][cloud][studio-18355]")
{
    // Single→single edit: AddEditDialog.tsx ships {color_code: <new>, colors: []}.
    // Cloud `[]string optional` would silently keep the previously-stored
    // colours[]; the patch must promote the empty array to [primary] so the
    // PUT actually overwrites the cloud row's colours.
    nlohmann::json patch = {
        {"color_code", "#0000FF"},
        {"colors",     nlohmann::json::array()},
    };
    auto body = mirror_spool_to_cloud_update_patch_color_subset(patch);
    REQUIRE(body.contains("color"));
    REQUIRE(body.at("color") == "#0000FF");
    REQUIRE(body.contains("colors"));
    REQUIRE(body.at("colors").is_array());
    REQUIRE(body.at("colors").size() == 1);
    CHECK(body.at("colors").at(0) == "#0000FF");
}

TEST_CASE("STUDIO-18355 (PUT): single-element colors[] disagreeing with color is realigned",
          "[fila_manager][colors][cloud][studio-18355]")
{
    nlohmann::json patch = {
        {"color_code", "#0000FF"},
        {"colors",     nlohmann::json::array({"#FF0000"})}, // stale primary leaked through
    };
    auto body = mirror_spool_to_cloud_update_patch_color_subset(patch);
    REQUIRE(body.at("colors").size() == 1);
    CHECK(body.at("colors").at(0) == "#0000FF");
}

TEST_CASE("STUDIO-18355 (PUT): multi-element colors[] left untouched on user-confirmed multicolor edit",
          "[fila_manager][colors][cloud][studio-18355]")
{
    // Multicolor edits intentionally let `color` (primary preview) and
    // `colors[]` carry independent semantics; do NOT collapse them.
    nlohmann::json patch = {
        {"color_code", "#0066FF"},
        {"colors",     nlohmann::json::array({"#0066FF", "#00AA88"})},
    };
    auto body = mirror_spool_to_cloud_update_patch_color_subset(patch);
    REQUIRE(body.at("colors").size() == 2);
    CHECK(body.at("colors").at(0) == "#0066FF");
    CHECK(body.at("colors").at(1) == "#00AA88");
}

TEST_CASE("STUDIO-18355 (PUT): patch without color_code does not synthesise a colors[] entry",
          "[fila_manager][colors][cloud][studio-18355]")
{
    // Editing only material/note must not implicitly touch the colour fields.
    nlohmann::json patch = {
        {"material_type", "PLA"},
        {"note",          "spare spool"},
    };
    auto body = mirror_spool_to_cloud_update_patch_color_subset(patch);
    CHECK_FALSE(body.contains("color"));
    CHECK_FALSE(body.contains("colors"));
}

TEST_CASE("STUDIO-18355 (PULL): cloud_json_to_spool snaps color_code to colors.front() when they disagree",
          "[fila_manager][colors][cloud][studio-18355]")
{
    // Defensive invariant aligned with FilamentSpool::from_json. Even if a
    // legacy or peer-written cloud row carries inconsistent {color, colors[0]},
    // the local store must end up self-consistent so SpoolColorChip can pick
    // either field and render the same hex.
    nlohmann::json cloud_resp = {
        {"id",        "S-18355"},
        {"color",     "#0000FF"},
        {"colors",    nlohmann::json::array({"#FF0000"})},
        {"colorType", 2},
    };
    auto s = mirror_cloud_json_to_spool_colour_subset(cloud_resp);
    CHECK(s.color_code == "#FF0000");
    REQUIRE(s.colors.size() == 1);
    CHECK(s.colors.front() == "#FF0000");
    CHECK(s.color_code == s.colors.front());
}

TEST_CASE("STUDIO-18355 (PULL): cloud response with empty colors keeps top-level color intact",
          "[fila_manager][colors][cloud][studio-18355]")
{
    // When the PUT-side guard above succeeds, the server eventually returns a
    // record where colours[] was overwritten to [primary] (or stayed empty for
    // genuinely uncoloured rows). Either way the invariant holds without
    // mutating color_code.
    nlohmann::json cloud_resp = {
        {"id",        "S-18355"},
        {"color",     "#0000FF"},
        {"colors",    nlohmann::json::array()},
        {"colorType", 2},
    };
    auto s = mirror_cloud_json_to_spool_colour_subset(cloud_resp);
    CHECK(s.color_code == "#0000FF");
    CHECK(s.colors.empty());
}
