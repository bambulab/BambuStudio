#include <catch2/catch.hpp>

#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/PresetIncludeDiffCache.hpp"

#include <stdexcept>
#include <deque>

using namespace Slic3r;

namespace {
DynamicPrintConfig include_config(double height, int walls)
{
    DynamicPrintConfig config;
    config.set_key_value("layer_height", new ConfigOptionFloat(height));
    config.set_key_value("wall_loops", new ConfigOptionInt(walls));
    return config;
}
}

TEST_CASE("Preset include differences are computed lazily once per object pair", "[Config][PresetIncludeDiffCache]")
{
    const auto defaults = include_config(0.2, 2);
    const auto included = include_config(0.3, 3);
    PresetIncludeDiffCache cache;
    int calls = 0;
    auto compute = [&] { ++calls; return included.diff(defaults); };
    const auto expected = included.diff(defaults);
    CHECK(cache.get(included, defaults, compute) == expected);
    CHECK(cache.get(included, defaults, compute) == expected);
    CHECK(calls == 1);
}

TEST_CASE("Preset include differences distinguish both config identities", "[Config][PresetIncludeDiffCache]")
{
    const auto defaults = include_config(0.2, 2);
    const auto other_defaults = include_config(0.3, 2);
    const auto included = include_config(0.3, 3);
    const auto same_name_other_object = include_config(0.4, 2);
    PresetIncludeDiffCache cache;
    int calls = 0;
    auto get = [&](const DynamicPrintConfig &config, const DynamicPrintConfig &base) {
        return cache.get(config, base, [&] { ++calls; return config.diff(base); });
    };
    CHECK(get(included, defaults) == included.diff(defaults));
    CHECK(get(included, other_defaults) == included.diff(other_defaults));
    CHECK(get(same_name_other_object, defaults) == same_name_other_object.diff(defaults));
    CHECK(get(included, defaults) == included.diff(defaults));
    CHECK(calls == 3);
}

TEST_CASE("Preset category reset discards differences before address reuse", "[Config][PresetIncludeDiffCache]")
{
    auto defaults = include_config(0.2, 2);
    auto included = include_config(0.3, 3);
    PresetIncludeDiffCache cache;
    int calls = 0;
    auto compute = [&] { ++calls; return included.diff(defaults); };
    CHECK(cache.get(included, defaults, compute) == included.diff(defaults));
    cache.clear();
    included = include_config(0.2, 4);
    defaults = include_config(0.2, 4);
    CHECK(cache.get(included, defaults, compute).empty());
    CHECK(calls == 2);
}

TEST_CASE("Empty preset include differences are cached", "[Config][PresetIncludeDiffCache]")
{
    const auto defaults = include_config(0.2, 2);
    const auto included = defaults;
    PresetIncludeDiffCache cache;
    int calls = 0;
    auto compute = [&] { ++calls; return included.diff(defaults); };
    CHECK(cache.get(included, defaults, compute).empty());
    CHECK(cache.get(included, defaults, compute).empty());
    CHECK(calls == 1);
}

TEST_CASE("Failed preset include computations are retried", "[Config][PresetIncludeDiffCache]")
{
    const auto defaults = include_config(0.2, 2);
    const auto included = include_config(0.3, 3);
    PresetIncludeDiffCache cache;
    int calls = 0;
    auto compute = [&] {
        if (++calls == 1) throw std::runtime_error("diff computation failed");
        return included.diff(defaults);
    };
    CHECK_THROWS_AS(cache.get(included, defaults, compute), std::runtime_error);
    CHECK(cache.get(included, defaults, compute) == included.diff(defaults));
    CHECK(cache.get(included, defaults, compute) == included.diff(defaults));
    CHECK(calls == 2);
}

TEST_CASE("Cached includes preserve application order and exact resulting config", "[Config][PresetIncludeDiffCache]")
{
    const auto defaults = include_config(0.2, 2);
    const auto first = include_config(0.3, 3);
    const auto second = include_config(0.4, 4);
    PresetIncludeDiffCache cache;
    int calls = 0;
    auto expected = defaults;
    auto actual = defaults;
    for (const auto *included : {&first, &second, &first}) {
        expected.apply_only(*included, included->diff(defaults));
        actual.apply_only(*included, cache.get(*included, defaults, [&] {
            ++calls;
            return included->diff(defaults);
        }));
    }
    CHECK(actual == expected);
    CHECK(actual.opt_float("layer_height") == 0.3);
    CHECK(actual.opt_int("wall_loops") == 3);
    CHECK(calls == 2);
}

TEST_CASE("Default snapshots survive deque movement", "[Config][PresetIncludeDiffCache]")
{
    PresetIncludeDiffCache cache;
    std::deque<DynamicPrintConfig> defaults;
    defaults.push_back(include_config(0.2, 2));
    defaults.push_back(include_config(0.3, 3));
    const auto expected = defaults[0];
    const auto *snapshot = &cache.default_snapshot(0, defaults[0]);
    REQUIRE(snapshot != &defaults[0]);
    defaults.insert(defaults.begin() + 1, include_config(0.4, 4));
    CHECK(*snapshot == expected);
    CHECK(&cache.default_snapshot(0, defaults[0]) == snapshot);
}

TEST_CASE("Default snapshots distinguish reused source addresses and reset with categories", "[Config][PresetIncludeDiffCache]")
{
    PresetIncludeDiffCache cache;
    auto reused_source = include_config(0.5, 5);
    const auto &first = cache.default_snapshot(1, reused_source);
    reused_source = include_config(0.6, 6);
    const auto &second = cache.default_snapshot(2, reused_source);
    CHECK(first.opt_float("layer_height") == 0.5);
    CHECK(second.opt_float("layer_height") == 0.6);
    CHECK(&first != &second);

    const auto included = include_config(0.5, 5);
    CHECK(cache.get(included, first, [&] { return included.diff(first); }).empty());
    CHECK_FALSE(cache.get(included, second, [&] { return included.diff(second); }).empty());
    cache.clear();
    const auto &next_category = cache.default_snapshot(1, reused_source);
    CHECK(next_category == reused_source);
    CHECK_FALSE(cache.get(included, next_category, [&] { return included.diff(next_category); }).empty());
}
