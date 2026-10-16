#include <catch2/catch.hpp>

#include "libslic3r/Model.hpp"
#include "libslic3r/Format/3mf.hpp"
#include "libslic3r/Format/STL.hpp"
#include "libslic3r/Format/SlicedFilamentColours.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include <boost/filesystem/operations.hpp>

using namespace Slic3r;

SCENARIO("Reading 3mf file", "[3mf]") {
    GIVEN("umlauts in the path of the file") {
        Model model;
        WHEN("3mf model is read") {
        	std::string path = std::string(TEST_DATA_DIR) + "/test_3mf/Geräte/Büchse.3mf";
        	DynamicPrintConfig config;
            ConfigSubstitutionContext ctxt{ ForwardCompatibilitySubstitutionRule::Disable };
            bool ret = load_3mf(path.c_str(), config, ctxt, &model, false);
            THEN("load should succeed") {
                REQUIRE(ret);
            }
        }
    }
}

SCENARIO("Export+Import geometry to/from 3mf file cycle", "[3mf]") {
    GIVEN("world vertices coordinates before save") {
        // load a model from stl file
        Model src_model;
        std::string src_file = std::string(TEST_DATA_DIR) + "/test_3mf/Prusa.stl";
        load_stl(src_file.c_str(), &src_model);
        src_model.add_default_instances();

        ModelObject* src_object = src_model.objects.front();

        // apply generic transformation to the 1st volume
        Geometry::Transformation src_volume_transform;
        src_volume_transform.set_offset({ 10.0, 20.0, 0.0 });
        src_volume_transform.set_rotation({ Geometry::deg2rad(25.0), Geometry::deg2rad(35.0), Geometry::deg2rad(45.0) });
        src_volume_transform.set_scaling_factor({ 1.1, 1.2, 1.3 });
        src_volume_transform.set_mirror({ -1.0, 1.0, -1.0 });
        src_object->volumes.front()->set_transformation(src_volume_transform);

        // apply generic transformation to the 1st instance
        Geometry::Transformation src_instance_transform;
        src_instance_transform.set_offset({ 5.0, 10.0, 0.0 });
        src_instance_transform.set_rotation({ Geometry::deg2rad(12.0), Geometry::deg2rad(13.0), Geometry::deg2rad(14.0) });
        src_instance_transform.set_scaling_factor({ 0.9, 0.8, 0.7 });
        src_instance_transform.set_mirror({ 1.0, -1.0, -1.0 });
        src_object->instances.front()->set_transformation(src_instance_transform);

        WHEN("model is saved+loaded to/from 3mf file") {
            // save the model to 3mf file
            std::string test_file = std::string(TEST_DATA_DIR) + "/test_3mf/prusa.3mf";
            store_3mf(test_file.c_str(), &src_model, nullptr, false);

            // load back the model from the 3mf file
            Model dst_model;
            DynamicPrintConfig dst_config;
            {
                ConfigSubstitutionContext ctxt{ ForwardCompatibilitySubstitutionRule::Disable };
                load_3mf(test_file.c_str(), dst_config, ctxt, &dst_model, false);
            }
            boost::filesystem::remove(test_file);

            // compare meshes
            TriangleMesh src_mesh = src_model.mesh();
            TriangleMesh dst_mesh = dst_model.mesh();

            bool res = src_mesh.its.vertices.size() == dst_mesh.its.vertices.size();
            if (res) {
                for (size_t i = 0; i < dst_mesh.its.vertices.size(); ++i) {
                    res &= dst_mesh.its.vertices[i].isApprox(src_mesh.its.vertices[i]);
                }
            }
            THEN("world vertices coordinates after load match") {
                REQUIRE(res);
            }
        }
    }
}

SCENARIO("2D convex hull of sinking object", "[3mf]") {
    GIVEN("model") {
        // load a model
        Model model;
        std::string src_file = std::string(TEST_DATA_DIR) + "/test_3mf/Prusa.stl";
        load_stl(src_file.c_str(), &model);
        model.add_default_instances();

        WHEN("model is rotated, scaled and set as sinking") {
            ModelObject* object = model.objects.front();
            object->center_around_origin(false);

            // set instance's attitude so that it is rotated, scaled and sinking
            ModelInstance* instance = object->instances.front();
            instance->set_rotation(X, -M_PI / 4.0);
            instance->set_offset(Vec3d::Zero());
            instance->set_scaling_factor({ 2.0, 2.0, 2.0 });

            // calculate 2D convex hull
            Polygon hull_2d = object->convex_hull_2d(instance->get_transformation().get_matrix());

            // verify result
            Points result = {
                { -91501496, -15914144 },
                { 91501496, -15914144 },
                { 91501496, 4243 },
                { 78229680, 4246883 },
                { 56898100, 4246883 },
                { -85501496, 4242641 },
                { -91501496, 4243 }
            };

            // Allow 1um error due to floating point rounding.
            bool res = hull_2d.points.size() == result.size();
            if (res)
                for (size_t i = 0; i < result.size(); ++ i) {
                    const Point &p1 = result[i];
                    const Point &p2 = hull_2d.points[i];
                    if (std::abs(p1.x() - p2.x()) > 1 || std::abs(p1.y() - p2.y()) > 1) {
                        res = false;
                        break;
                    }
                }

            THEN("2D convex hull should match with reference") {
                REQUIRE(res);
            }
        }
    }
}

TEST_CASE("Sliced colour export preserves the editable config and mixed recipes",
          "[filament_capacity_integration][sliced_filament_colour]")
{
    DynamicPrintConfig source;
    auto* colours = new ConfigOptionStrings();
    for (unsigned int i = 0; i < 64; ++i)
        colours->values.push_back("colour_" + std::to_string(i + 1));
    source.set_key_value("filament_colour", colours);
    auto* mixed = new ConfigOptionBools();
    mixed->values.assign(64, false);
    std::fill(mixed->values.begin() + 8, mixed->values.end(), true);
    source.set_key_value("filament_is_mixed", mixed);
    auto* recipes = new ConfigOptionStrings();
    recipes->values.assign(64, "");
    std::fill(recipes->values.begin() + 8, recipes->values.end(), "1,2");
    source.set_key_value("filament_mixed_components", recipes);

    SECTION("Editable project save keeps all 64 colours") {
        REQUIRE(sliced_filament_colour_count(colours->values.size(), mixed->values, false) == 64);
    }
    SECTION("Transport export changes only the colour list on its copy") {
        DynamicPrintConfig exported = source;
        auto* output_colours = exported.option<ConfigOptionStrings>("filament_colour");
        const auto* output_mixed = exported.option<ConfigOptionBools>("filament_is_mixed");
        output_colours->values.resize(sliced_filament_colour_count(
            output_colours->values.size(), output_mixed->values, true));
        REQUIRE(output_colours->values.size() == 8);
        for (std::size_t i = 0; i < 8; ++i)
            REQUIRE(output_colours->values[i] == colours->values[i]);
        REQUIRE(source.option<ConfigOptionStrings>("filament_colour")->values.size() == 64);
        REQUIRE(exported.option<ConfigOptionBools>("filament_is_mixed")->values == mixed->values);
        REQUIRE(exported.option<ConfigOptionStrings>("filament_mixed_components")->values == recipes->values);
    }
}

TEST_CASE("Sliced colour export retains physical indices and legacy metadata",
          "[filament_capacity_integration][sliced_filament_colour]")
{
    for (std::size_t count : {8u, 32u, 33u, 64u}) {
        CAPTURE(count);
        std::vector<unsigned char> mixed(count, 1);
        std::fill(mixed.begin(), mixed.begin() + 8, 0);
        REQUIRE(sliced_filament_colour_count(count, mixed, false) == count);
        REQUIRE(sliced_filament_colour_count(count, mixed, true) == (count <= 32 ? count : 8));
    }
    std::vector<unsigned char> mixed(64, 1);
    mixed[0] = 0;
    mixed[32] = 0;
    // Keep physical ID 33 at index 32; never compact it to index 1.
    REQUIRE(sliced_filament_colour_count(64, mixed, true) == 33);
    mixed.back() = 0;
    REQUIRE(sliced_filament_colour_count(64, mixed, true) == 64);
    std::fill(mixed.begin(), mixed.end(), 1);
    REQUIRE(sliced_filament_colour_count(64, mixed, true) == 64);
    mixed.resize(63);
    REQUIRE(sliced_filament_colour_count(64, mixed, true) == 64);
    mixed.clear();
    REQUIRE(sliced_filament_colour_count(64, mixed, true) == 64);
}
