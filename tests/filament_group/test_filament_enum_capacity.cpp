#include <catch2/catch.hpp>
#include "fg_test_evaluator.hpp"

#include <algorithm>
#include <string>
#include <vector>

using namespace Slic3r;
using namespace Slic3r::FGTest;

namespace {
TestCase legacy_case(const char* name = "mode_bestfit_161")
{
    return load_test_case(std::string(FG_TEST_GOLDEN_DIR) + "/config_a/" + name + ".json");
}

void check_same_result(const FilamentGroupContext& actual, const FilamentGroupContext& reference)
{
    const auto observed = run_and_evaluate(actual);
    const auto expected = run_and_evaluate(reference);
    CHECK(observed.filament_map == expected.filament_map);
    CHECK(full_evaluate_map(actual, observed.filament_map).full_score ==
          full_evaluate_map(reference, expected.filament_map).full_score);
}

void check_unbounded_legacy_preserved(const FilamentGroupContext& context)
{
    REQUIRE(context.nozzle_info.per_nozzle_capacity.empty());
    auto reference = context;
    reference.nozzle_info.per_nozzle_capacity.assign(context.nozzle_info.nozzle_list.size(), 100);
    check_same_result(context, reference);
}
}

TEST_CASE("Legacy enum capacities retain both literal A golden optima", "[filament_group][enum_capacity_fallback]")
{
    SECTION("BestFit") {
        const auto tc = legacy_case();
        REQUIRE(tc.base_result.has_value());
        REQUIRE(tc.context.nozzle_info.per_nozzle_capacity.empty());
        const auto result = run_and_evaluate(tc.context);
        CHECK(result.constraints_ok);
        CHECK(result.filament_map == std::vector<int>{1, 1, 0, 0, 0, 1, 1});
        CHECK(full_evaluate_map(tc.context, result.filament_map).full_score == tc.base_result->full_score);
        CHECK(tc.context.nozzle_info.per_nozzle_capacity.empty());
    }
    SECTION("BestCost") {
        const auto tc = legacy_case("stress_55");
        REQUIRE(tc.base_result.has_value());
        REQUIRE(tc.context.nozzle_info.per_nozzle_capacity.empty());
        const auto result = run_and_evaluate(tc.context);
        CHECK(result.constraints_ok);
        CHECK(result.filament_map == std::vector<int>{1, 0, 1, 1, 0, 0, 1});
        CHECK(full_evaluate_map(tc.context, result.filament_map).full_score == tc.base_result->full_score);
    }
}

TEST_CASE("Legacy enum capacities preserve a deterministic feasible constrained assignment", "[filament_group][enum_capacity_fallback]")
{
    auto context = legacy_case().context;
    context.model_info.unprintable_filaments = {{0, 1, 2}, {3, 4}};
    const auto original_bans = context.model_info.unprintable_filaments;
    REQUIRE(full_evaluate_map(context, {1, 1, 1, 0, 0, 0, 1}).constraints_ok);
    const auto result = run_and_evaluate(context);
    CHECK(result.constraints_ok);
    auto reference = context;
    reference.nozzle_info.per_nozzle_capacity = {4, 4};
    check_same_result(context, reference);
    CHECK(context.model_info.unprintable_filaments == original_bans);
    CHECK(context.nozzle_info.per_nozzle_capacity.empty());
}

TEST_CASE("Enum explicit capacities take precedence and partial capacities retain defaults", "[filament_group][enum_capacity_fallback]")
{
    SECTION("Explicit precedence") {
        auto context = legacy_case().context;
        context.nozzle_info.per_nozzle_capacity = {1, 6};
        auto reference = context;
        reference.machine_info.max_group_size = {100, 100};
        check_same_result(context, reference);
        const auto result = run_and_evaluate(context);
        CHECK(std::count(result.filament_map.begin(), result.filament_map.end(), 0) <= 1);
        CHECK(std::count(result.filament_map.begin(), result.filament_map.end(), 1) <= 6);
        CHECK(context.nozzle_info.per_nozzle_capacity == std::vector<int>{1, 6});
    }
    SECTION("Partial explicit capacities") {
        auto context = legacy_case().context;
        context.nozzle_info.per_nozzle_capacity = {4};
        auto reference = context;
        reference.nozzle_info.per_nozzle_capacity = {4, 1};
        check_same_result(context, reference);
        CHECK(context.nozzle_info.per_nozzle_capacity == std::vector<int>{4});
    }
}

TEST_CASE("Legacy enum capacities retain physical association across unavailable nozzle compaction", "[filament_group][enum_capacity_fallback]")
{
    auto context = legacy_case().context;
    context.nozzle_info.nozzle_list = {
        {"0.4", NozzleVolumeType::nvtStandard, 0, 0, true},
        {"0.4", NozzleVolumeType::nvtStandard, 0, 1, false},
        {"0.4", NozzleVolumeType::nvtStandard, 1, 2, true}};
    context.nozzle_info.extruder_nozzle_list = {{0, {0, 1}}, {1, {2}}};
    context.machine_info.prefer_non_model_filament = {false, false, true};
    std::vector<int> full_ids;
    const auto compact = context.available_only(full_ids);
    REQUIRE(full_ids == std::vector<int>{0, 2});
    REQUIRE(compact.nozzle_info.nozzle_list[1].group_id == 1);
    REQUIRE(compact.nozzle_info.nozzle_list[1].extruder_id == 1);
    REQUIRE(compact.nozzle_info.per_nozzle_capacity.empty());
    const auto result = run_and_evaluate(context);
    CHECK(result.filament_map == std::vector<int>{2, 2, 0, 0, 0, 2, 2});
    CHECK(result.constraints_ok);
    auto reference = context;
    reference.nozzle_info.per_nozzle_capacity = {4, 0, 4};
    check_same_result(context, reference);
    CHECK(context.nozzle_info.nozzle_list[2].group_id == 2);
    CHECK_FALSE(context.nozzle_info.nozzle_list[1].installed);
}

TEST_CASE("Enum ambiguous or multiple nozzle layouts keep existing empty capacity behavior", "[filament_group][enum_capacity_fallback]")
{
    auto context = legacy_case().context;
    SECTION("Reversed compact extruder association") {
        context.nozzle_info.nozzle_list[0].extruder_id = 1;
        context.nozzle_info.nozzle_list[1].extruder_id = 0;
        context.nozzle_info.extruder_nozzle_list = {{0, {1}}, {1, {0}}};
        check_unbounded_legacy_preserved(context);
    }
    SECTION("Two nozzles on one extruder") {
        context.nozzle_info.nozzle_list[1].extruder_id = 0;
        context.nozzle_info.extruder_nozzle_list = {{0, {0, 1}}};
        check_unbounded_legacy_preserved(context);
    }
    SECTION("Multiple nozzles on the second extruder") {
        context.nozzle_info.nozzle_list.push_back({"0.4", NozzleVolumeType::nvtHighFlow, 1, 2, true});
        context.nozzle_info.extruder_nozzle_list[1].push_back(2);
        check_unbounded_legacy_preserved(context);
    }
    SECTION("Missing legacy extruder capacity") {
        context.machine_info.max_group_size = {4};
        check_unbounded_legacy_preserved(context);
    }
    SECTION("Empty legacy capacities") {
        context.machine_info.max_group_size.clear();
        check_unbounded_legacy_preserved(context);
    }
    SECTION("Unavailable left extruder leaves a nonidentity compact association") {
        context.nozzle_info.nozzle_list[0].installed = false;
        std::vector<int> full_ids;
        const auto compact = context.available_only(full_ids);
        REQUIRE(full_ids == std::vector<int>{1});
        REQUIRE(compact.nozzle_info.nozzle_list[0].group_id == 0);
        REQUIRE(compact.nozzle_info.nozzle_list[0].extruder_id == 1);
        check_unbounded_legacy_preserved(context);
        CHECK(run_and_evaluate(context).filament_map == std::vector<int>(7, 1));
    }
    SECTION("Empty available layout") {
        for (auto& nozzle : context.nozzle_info.nozzle_list)
            nozzle.installed = false;
        auto reference = context;
        reference.nozzle_info.per_nozzle_capacity = {1, 1};
        CHECK_THROWS_WITH(FilamentGroup(context).calc_filament_group(), "No valid nozzle found. Please check nozzle count.");
        CHECK_THROWS_WITH(FilamentGroup(reference).calc_filament_group(), "No valid nozzle found. Please check nozzle count.");
    }
}

TEST_CASE("Enum compatibility leaves MatchMode unchanged", "[filament_group][enum_capacity_fallback]")
{
    const auto context = legacy_case("mode_match_150").context;
    auto reference = context;
    reference.nozzle_info.per_nozzle_capacity = {3, 3};
    check_same_result(context, reference);
    CHECK(run_and_evaluate(context).filament_map == std::vector<int>{1, 1, 1, 1});
}

TEST_CASE("Legacy enum deduplication retains distinct effective capacities", "[filament_group][enum_capacity_fallback][enum_capacity_dedup]")
{
    auto context = legacy_case().context;
    context.nozzle_info.nozzle_list.push_back({"0.4", NozzleVolumeType::nvtStandard, 2, 2, true});
    context.nozzle_info.extruder_nozzle_list[2] = {2};
    context.machine_info.max_group_size = {3, 1, 3};
    context.machine_info.prefer_non_model_filament = {false, false, false};
    context.machine_info.machine_filament_info.resize(3);
    context.model_info.unprintable_filaments = {{3, 4, 5, 6}, {0, 1, 2, 4, 5, 6}, {0, 1, 2, 3}};
    context.group_info.ignore_ext_filament = true;
    REQUIRE(context.nozzle_info.per_nozzle_capacity.empty());

    auto reference = context;
    reference.nozzle_info.per_nozzle_capacity = {3, 1, 3};
    const auto expected = run_and_evaluate(reference);
    REQUIRE(expected.constraints_ok);
    CHECK(expected.filament_map == std::vector<int>{0, 0, 0, 1, 2, 2, 2});
    const auto result = run_and_evaluate(context);
    CHECK(result.constraints_ok);
    CHECK(result.filament_map == expected.filament_map);
    CHECK(context.nozzle_info.per_nozzle_capacity.empty());
    CHECK(reference.nozzle_info.per_nozzle_capacity == std::vector<int>{3, 1, 3});
}
