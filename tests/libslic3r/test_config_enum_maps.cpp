#include <catch2/catch.hpp>

#include "libslic3r/PrintConfig.hpp"

#include <cstddef>
#include <memory>
#include <vector>

using namespace Slic3r;

namespace {
const t_config_enum_values *enum_map(const ConfigOption &option)
{
    return option.nullable()
        ? dynamic_cast<const ConfigOptionEnumsGenericNullable &>(option).keys_map
        : dynamic_cast<const ConfigOptionEnumsGeneric &>(option).keys_map;
}

void check_enum_defaults(const ConfigBase &config)
{
    size_t checked = 0;
    for (const auto &key : config.keys()) {
        const ConfigOption &option = *config.option(key);
        if (option.type() != coEnums)
            continue;
        INFO(key);
        const ConfigOptionDef &definition = *config.def()->get(key);
        REQUIRE(definition.enum_keys_map != nullptr);
        const bool bound = enum_map(option) == definition.enum_keys_map;
        CHECK(bound);
        // Report an invalid map safely, without dereferencing it in serialize().
        if (bound) {
            std::unique_ptr<ConfigOption> expected(definition.create_default_option());
            CHECK(option.serialize() == expected->serialize());
        }
        ++checked;
    }
    CHECK(checked == 9);
}

}

TEST_CASE("Enum definition factories bind their serialization maps", "[Config][EnumMaps]")
{
    size_t checked = 0;
    for (const auto &entry : print_config_def.options) {
        const auto &definition = entry.second;
        if (definition.type != coEnums)
            continue;
        INFO(entry.first);
        REQUIRE(definition.enum_keys_map != nullptr);
        std::unique_ptr<ConfigOption> option(definition.create_default_option());
        REQUIRE(enum_map(*option) == definition.enum_keys_map);
        CHECK_FALSE(option->serialize().empty());
        ++checked;
    }
    CHECK(checked == 11);
    std::unique_ptr<ConfigOption> cooling(print_config_def.get("cooling_slowdown_logic")->create_default_option());
    CHECK(cooling->serialize() == "uniform_cooling");
}

TEST_CASE("Static and dynamic full defaults serialize enum vectors", "[Config][EnumMaps]")
{
    SECTION("Static cache") { check_enum_defaults(FullPrintConfig::defaults()); }
    SECTION("Static instance") { const FullPrintConfig config; check_enum_defaults(config); }
    SECTION("Static copy") {
        const FullPrintConfig original;
        const FullPrintConfig copy(original);
        check_enum_defaults(copy);
    }
    SECTION("Static assignment") {
        FullPrintConfig copy;
        copy = FullPrintConfig::defaults();
        check_enum_defaults(copy);
    }
    SECTION("Dynamic full defaults") { check_enum_defaults(DynamicPrintConfig::full_print_config()); }
    SECTION("Dynamic copy") {
        const auto original = DynamicPrintConfig::full_print_config();
        const DynamicPrintConfig copy(original);
        check_enum_defaults(copy);
    }
    SECTION("Dynamic assignment") {
        DynamicPrintConfig copy;
        copy = DynamicPrintConfig::full_print_config();
        check_enum_defaults(copy);
    }
}

TEST_CASE("Resetting static defaults restores definition-owned enum maps", "[Config][EnumMaps]")
{
    struct ResettableFullPrintConfig : FullPrintConfig {
        void reset() { PrintRegionConfig::set_defaults(); }
    } config;
    config.cooling_slowdown_logic.keys_map = nullptr;
    config.cooling_slowdown_logic.values.clear();
    config.z_hop_types.keys_map = nullptr;
    config.z_hop_types.values.clear();
    config.reset();
    check_enum_defaults(config);
}

TEST_CASE("Initializer-list enum vectors start with an unbound map", "[Config][EnumMaps]")
{
    const ConfigOptionEnumsGeneric plain({0, 1});
    const ConfigOptionEnumsGenericNullable nullable({0, 1});
    CHECK(plain.keys_map == nullptr);
    CHECK(nullable.keys_map == nullptr);
    CHECK(plain.values == std::vector<int>{0, 1});
    CHECK(nullable.values == std::vector<int>{0, 1});
}

