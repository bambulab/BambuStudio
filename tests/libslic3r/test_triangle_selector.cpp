#include <catch2/catch.hpp>

#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/TriangleSelector.hpp"
#include "libslic3r/FilamentPainting.hpp"

using namespace Slic3r;

static std::pair<std::vector<std::pair<int, int>>, std::vector<bool>> truncated_split_stream()
{
    // One nibble: split-sides=1 (needs children) and then no more bits.
    std::pair<std::vector<std::pair<int, int>>, std::vector<bool>> data;
    data.first.emplace_back(0, 0);
    data.second = {true, false, false, false};
    return data;
}

TEST_CASE("TriangleSelector paint stream must not OOB on truncated 3MF bits", "[TriangleSelector]")
{
    const auto data = truncated_split_stream();
    REQUIRE_NOTHROW(TriangleSelector::has_facets(data, EnforcerBlockerType::ENFORCER));
    REQUIRE(TriangleSelector::has_facets(data, EnforcerBlockerType::ENFORCER) == false);

    TriangleMesh     mesh(its_make_cube(10., 10., 10.));
    TriangleSelector selector(mesh);
    REQUIRE_NOTHROW(selector.deserialize(data, false));
}

static void append_nibble(std::vector<bool> &bits, int n)
{
    for (int i = 0; i < 4; ++i)
        bits.push_back(bool(n & (1 << i)));
}

TEST_CASE("TriangleSelector paint state must not wrap int8_t past ExtruderMax", "[TriangleSelector]")
{
    // Leaf prefix 0b1100, then nine 0b1111 and a 0 terminator: 0 + 15*9 + 3 = 138 > 127.
    std::pair<std::vector<std::pair<int, int>>, std::vector<bool>> data;
    data.first.emplace_back(0, 0);
    append_nibble(data.second, 0b1100);
    for (int i = 0; i < 9; ++i)
        append_nibble(data.second, 0b1111);
    append_nibble(data.second, 0);

    TriangleMesh     mesh(its_make_cube(10., 10., 10.));
    TriangleSelector selector(mesh);
    REQUIRE_NOTHROW(selector.deserialize(data, false));
    REQUIRE(selector.has_facets(EnforcerBlockerType::NONE));
}

TEST_CASE("All 64 paint states round trip through the existing facet stream", "[TriangleSelector][filament_capacity_integration]")
{
    TriangleMesh mesh(its_make_cube(10., 10., 10.));
    Model model;
    ModelVolume* volume = model.add_object()->add_volume(mesh);
    for (unsigned int id = 1; id <= MAXIMUM_EXTRUDER_NUMBER; ++id) {
        CAPTURE(id);
        const auto state = static_cast<EnforcerBlockerType>(id);
        TriangleSelector source(mesh);
        source.set_facet(0, state);
        const auto data = source.serialize();
        TriangleSelector restored(mesh);
        restored.deserialize(data);
        REQUIRE(restored.num_facets(state) == 1);
        REQUIRE(restored.serialize() == data);

        volume->mmu_segmentation_facets.reset();
        volume->mmu_segmentation_facets.set(source);
        REQUIRE(volume->mmu_segmentation_facets.get_triangle_as_string(0) == encode_filament_id(id));
        volume->mmu_segmentation_facets.reset();
        volume->mmu_segmentation_facets.set_triangle_from_string(0, encode_filament_id(id));
        REQUIRE(volume->mmu_segmentation_facets.get_data() == data);
    }
}

TEST_CASE("Paint state 65 is discarded and deletion remaps the 33 boundary", "[TriangleSelector][filament_capacity_integration]")
{
    TriangleMesh mesh(its_make_cube(10., 10., 10.));
    Model model;
    ModelVolume* volume = model.add_object()->add_volume(mesh);
    volume->mmu_segmentation_facets.set_triangle_from_string(0, "2FFFFC"); // state 65
    TriangleSelector restored(mesh);
    restored.deserialize(volume->mmu_segmentation_facets.get_data());
    REQUIRE(restored.num_facets(EnforcerBlockerType::NONE) == static_cast<int>(mesh.its.indices.size()));

    TriangleSelector source(mesh);
    source.set_facet(0, EnforcerBlockerType::Extruder33);
    source.set_facet(1, EnforcerBlockerType::Extruder64);
    restored.deserialize(source.serialize(), true, EnforcerBlockerType::ExtruderMax,
                         EnforcerBlockerType::Extruder33, EnforcerBlockerType::Extruder32);
    REQUIRE(restored.num_facets(EnforcerBlockerType::Extruder32) == 1);
    REQUIRE(restored.num_facets(EnforcerBlockerType::Extruder63) == 1);
}

TEST_CASE("OBJ face import preserves IDs 32 33 and 64 without narrowing callbacks", "[TriangleSelector][filament_capacity_integration]")
{
    TriangleMesh mesh(its_make_cube(10., 10., 10.));
    Model model;
    ModelVolume* volume = model.add_object()->add_volume(mesh);
    for (unsigned int id : {32u, 33u, 64u}) {
        CAPTURE(id);
        std::vector<unsigned char> ids(mesh.its.indices.size(), static_cast<unsigned char>(id));
        REQUIRE(Model::obj_import_face_color_deal(ids, std::nullopt, volume));
        TriangleSelector restored(volume->mesh());
        restored.deserialize(volume->mmu_segmentation_facets.get_data());
        REQUIRE(restored.num_facets(static_cast<EnforcerBlockerType>(id)) == static_cast<int>(mesh.its.indices.size()));
    }
    const std::vector<unsigned char> ids(mesh.its.indices.size(), 1);
    REQUIRE(Model::obj_import_face_color_deal(ids, std::nullopt, volume, [](int, int, int) { return 289; }));
    REQUIRE(volume->mmu_segmentation_facets.empty()); // 289 must not narrow to 33.
}

TEST_CASE("OBJ vertex splits retain long leaf codes and discard incomplete streams", "[TriangleSelector][filament_capacity_integration]")
{
    indexed_triangle_set its;
    its.vertices = {Vec3f(0.f, 0.f, 0.f), Vec3f(10.f, 0.f, 0.f), Vec3f(0.f, 10.f, 0.f)};
    its.indices = {Vec3i(0, 1, 2)};
    Model model;
    ModelVolume* volume = model.add_object()->add_volume(TriangleMesh(its));
    for (const auto& ids : std::vector<std::vector<unsigned char>>{{33, 33, 33}, {33, 64, 64}, {32, 33, 64}}) {
        REQUIRE(Model::obj_import_vertex_color_deal(ids, std::nullopt, volume));
        TriangleSelector restored(volume->mesh());
        restored.deserialize(volume->mmu_segmentation_facets.get_data());
        for (unsigned char id : ids)
            REQUIRE(restored.has_facets(static_cast<EnforcerBlockerType>(id)));
        REQUIRE_FALSE(restored.has_facets(EnforcerBlockerType::NONE));
        REQUIRE(restored.serialize() == volume->mmu_segmentation_facets.get_data());
    }
    const std::vector<unsigned char> invalid{33, 65, 64};
    REQUIRE(Model::obj_import_vertex_color_deal(invalid, std::nullopt, volume));
    REQUIRE(volume->mmu_segmentation_facets.empty());
}
