#include <catch2/catch.hpp>
#include "libslic3r/FilamentGroup.hpp"
#include <vector>

using namespace Slic3r;

namespace {
FilamentGroupContext context_with_available_nozzles()
{
    FilamentGroupContext context{};
    context.machine_info.max_group_size = {4, 2};
    context.machine_info.master_extruder_id = 0;
    context.machine_info.prefer_non_model_filament = {false, false, false};
    context.nozzle_info.nozzle_list = {
        {"0.4", NozzleVolumeType::nvtStandard, 0, 0, true},
        {"0.4", NozzleVolumeType::nvtStandard, 0, 1, false},
        {"0.6", NozzleVolumeType::nvtHighFlow, 1, 2, true}};
    context.nozzle_info.extruder_nozzle_list = {{0, {0, 1}}, {1, {2}}};
    return context;
}
}

TEST_CASE("Filament context compaction preserves unspecified capacity fallback", "[filament_group][capacity_fallback]")
{
    const auto context = context_with_available_nozzles();
    std::vector<int> full_ids;
    const auto compact = context.available_only(full_ids);
    REQUIRE(full_ids == std::vector<int>{0, 2});
    REQUIRE(compact.nozzle_info.nozzle_list.size() == 2);
    CHECK(compact.nozzle_info.nozzle_list[0].group_id == 0);
    CHECK(compact.nozzle_info.nozzle_list[1].group_id == 1);
    CHECK(compact.nozzle_info.per_nozzle_capacity.empty());
    CHECK(compact.machine_info.max_group_size == context.machine_info.max_group_size);
    CHECK(context.nozzle_info.per_nozzle_capacity.empty());
    CHECK(context.nozzle_info.nozzle_list[2].group_id == 2);
}

TEST_CASE("Filament context compaction retains explicit physical capacities", "[filament_group][capacity_fallback]")
{
    auto context = context_with_available_nozzles();
    context.nozzle_info.per_nozzle_capacity = {4, 0, 2};
    std::vector<int> full_ids;
    const auto compact = context.available_only(full_ids);
    REQUIRE(full_ids == std::vector<int>{0, 2});
    CHECK(compact.nozzle_info.per_nozzle_capacity == std::vector<int>{4, 2});
    CHECK(context.nozzle_info.per_nozzle_capacity == std::vector<int>{4, 0, 2});
}

TEST_CASE("Filament context compaction preserves explicit partial-capacity defaults", "[filament_group][capacity_fallback]")
{
    auto context = context_with_available_nozzles();
    context.nozzle_info.per_nozzle_capacity = {4};
    std::vector<int> full_ids;
    const auto compact = context.available_only(full_ids);
    CHECK(compact.nozzle_info.per_nozzle_capacity == std::vector<int>{4, 1});
}

TEST_CASE("Filament context compaction handles an empty available nozzle set", "[filament_group][capacity_fallback]")
{
    auto context = context_with_available_nozzles();
    for (auto& nozzle : context.nozzle_info.nozzle_list)
        nozzle.installed = false;
    context.nozzle_info.per_nozzle_capacity = {4, 0, 2};
    std::vector<int> full_ids;
    const auto compact = context.available_only(full_ids);
    CHECK(full_ids.empty());
    CHECK(compact.nozzle_info.nozzle_list.empty());
    CHECK(compact.nozzle_info.per_nozzle_capacity.empty());
}
