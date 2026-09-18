// Tests for per-object prime towers in By-Object (sequential) printing.
// See docs/sequential_wipe_tower_architecture.md and
// docs/sequential_wipe_tower_test_strategy.md.
//
// Status: baseline characterisation + the target regression for issues
// #1876 / #9399 (2+ multicolour objects, By Object, prime tower on -> one prime
// tower per object). The [!shouldfail] case flips to passing once the feature
// emits per-object tower G-code.

#include <catch2/catch.hpp>

#include "libslic3r/Print.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/GCode/ToolOrdering.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"

#include <boost/filesystem.hpp>
#include <fstream>

#include "test_data.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;

namespace {

// A two-colour object: walls on filament `a`, infill/surfaces on filament `b`,
// set through per-object region config so each sequential object carries its own
// tool schedule. Every layer has a tool change, so the object needs a tower.
ModelObject *add_multicolour_object(Model &model, const Vec3d &offset, double w, double d, double h, int a, int b)
{
    ModelObject *o = model.add_object();
    o->add_volume(make_cube(w, d, h));
    o->config.set_key_value("wall_filament", new ConfigOptionInt(a));
    o->config.set_key_value("solid_infill_filament", new ConfigOptionInt(b));
    o->config.set_key_value("sparse_infill_filament", new ConfigOptionInt(b));
    o->config.set_key_value("top_surface_filament", new ConfigOptionInt(b));
    o->config.set_key_value("bottom_surface_filament", new ConfigOptionInt(b));
    o->add_instance();
    o->instances.front()->set_offset(offset);
    return o;
}

DynamicPrintConfig make_sequential_multicolour_config(unsigned num_filaments)
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_num_filaments(num_filaments);

    std::vector<double>      diams(num_filaments, 1.75);
    std::vector<std::string> cols, types;
    for (unsigned i = 0; i < num_filaments; ++i) {
        cols.emplace_back(i == 0 ? "#FF0000" : (i == 1 ? "#00FF00" : "#0000FF"));
        types.emplace_back("PLA");
    }
    config.set_key_value("filament_diameter", new ConfigOptionFloats(diams));
    config.set_key_value("filament_colour",   new ConfigOptionStrings(cols));
    config.set_key_value("filament_type",     new ConfigOptionStrings(types));

    // Flush volume matrix must be num_filaments^2; the default preset only sizes
    // it for its own filament count.
    std::vector<double> flush_matrix(size_t(num_filaments) * num_filaments, 0.0);
    for (unsigned i = 0; i < num_filaments; ++i)
        for (unsigned j = 0; j < num_filaments; ++j)
            if (i != j) flush_matrix[i * num_filaments + j] = 280.0;
    config.set_key_value("flush_volumes_matrix", new ConfigOptionFloats(flush_matrix));
    config.set_key_value("flush_volumes_vector", new ConfigOptionFloats(std::vector<double>(num_filaments, 140.0)));

    config.set_key_value("enable_prime_tower", new ConfigOptionBool(true));
    config.set_key_value("single_extruder_multi_material", new ConfigOptionBool(true));
    // Anchor near the plate origin so per-object towers (anchor + object shift)
    // stay on the bed for the test layouts.
    config.set_key_value("wipe_tower_x", new ConfigOptionFloats(std::vector<double>{5.0}));
    config.set_key_value("wipe_tower_y", new ConfigOptionFloats(std::vector<double>{5.0}));
    config.set_key_value("print_sequence", new ConfigOptionEnum<PrintSequence>(PrintSequence::ByObject));
    // Smooth timelapse and wrapping detection are rejected for By Object.
    config.set_key_value("timelapse_type", new ConfigOptionEnum<TimelapseType>(TimelapseType::tlTraditional));
    config.set_key_value("enable_wrapping_detection", new ConfigOptionBool(false));
    // Prime tower requires relative E.
    config.set_key_value("use_relative_e_distances", new ConfigOptionBool(true));
    config.set_key_value("wipe_tower_no_sparse_layers", new ConfigOptionBool(false));
    return config;
}

void build_sequential_print(Print &print, Model &model, const DynamicPrintConfig &config_in,
                            const std::vector<Vec3d> &offsets, int n_filaments,
                            double w = 20, double d = 20, double h = 2.0)
{
    model.clear_objects();
    for (const Vec3d &off : offsets)
        add_multicolour_object(model, off, w, d, h, 1, 2);

    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_num_filaments(n_filaments);
    config.apply(config_in);

    for (ModelObject *mo : model.objects) {
        mo->ensure_on_bed();
        print.auto_assign_extruders(mo);
    }
    print.apply(model, config);
    print.set_status_silent();
}

// Number of sequential objects that will actually get a prime tower: the plate
// must have the tower enabled AND the object's own tool ordering must need one.
size_t count_object_orderings_with_tower(const Print &print)
{
    if (!print.has_wipe_tower() || !print.sequential_print_data().has_value())
        return 0;
    const auto &pod = print.sequential_print_data().value();
    size_t n = 0;
    for (const PrintObject *o : pod.print_object_order) {
        auto it = pod.object_tool_ordering_map.find(o);
        if (it != pod.object_tool_ordering_map.end() && it->second.has_wipe_tower())
            ++n;
    }
    return n;
}

// Like Test::gcode() but writes to an absolute temp path (BambuStudio's
// export_gcode wants a real parent directory).
std::string seq_gcode(Print &print)
{
    boost::filesystem::path tmp = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("seqwt-%%%%-%%%%.gcode");
    print.set_status_silent();
    print.process();
    // export_gcode() dereferences the result pointer unconditionally near the end
    // (result->label_object_enabled = ...), so a real object is required.
    GCodeProcessorResult gcode_result;
    print.export_gcode(tmp.string(), &gcode_result, nullptr);
    std::ifstream t(tmp.string());
    std::string str((std::istreambuf_iterator<char>(t)), std::istreambuf_iterator<char>());
    boost::filesystem::remove(tmp);
    return str;
}

int count_tool_changes(const std::string &gcode)
{
    int n = 0;
    for (size_t pos = 0; (pos = gcode.find("\nT", pos)) != std::string::npos; ) {
        pos += 2;
        if (pos < gcode.size() && std::isdigit((unsigned char) gcode[pos]))
            ++n;
    }
    return n;
}

// Min/max X of extrusion moves inside every WIPE_TOWER_START..END block that
// lies within [begin,end) of the gcode text.
std::pair<float, float> wipe_tower_x_span(const std::string &gcode, size_t begin, size_t end)
{
    float lo = 1e9f, hi = -1e9f;
    size_t pos = begin;
    while (true) {
        size_t s = gcode.find("WIPE_TOWER_START", pos);
        if (s == std::string::npos || s >= end) break;
        size_t e = gcode.find("WIPE_TOWER_END", s);
        if (e == std::string::npos) break;
        std::string blk = gcode.substr(s, e - s);
        size_t bp = 0;
        while ((bp = blk.find("G1 ", bp)) != std::string::npos) {
            size_t xp = blk.find('X', bp);
            size_t nl = blk.find('\n', bp);
            if (xp != std::string::npos && xp < nl) {
                float x = std::strtof(blk.c_str() + xp + 1, nullptr);
                lo = std::min(lo, x); hi = std::max(hi, x);
            }
            bp = nl == std::string::npos ? blk.size() : nl + 1;
        }
        pos = e + 1;
    }
    return {lo, hi};
}

// Byte offsets of each "; OBJECT_ID: N" where N changes from the previous one,
// i.e. the boundaries between sequential objects.
std::vector<size_t> object_boundaries(const std::string &gcode)
{
    std::vector<size_t> out;
    std::string last_id;
    size_t pos = 0;
    const std::string tag = "; OBJECT_ID: ";
    while ((pos = gcode.find(tag, pos)) != std::string::npos) {
        size_t nl = gcode.find('\n', pos);
        std::string id = gcode.substr(pos + tag.size(), nl - pos - tag.size());
        if (id != last_id) {
            if (!last_id.empty())
                out.push_back(pos);
            last_id = id;
        }
        pos = nl == std::string::npos ? gcode.size() : nl + 1;
    }
    return out;
}

} // namespace

TEST_CASE("SeqWT: By-Object 2 multicolour objects get a tower each (planning)", "[SequentialWipeTower]")
{
    DynamicPrintConfig config = make_sequential_multicolour_config(2);

    Print print;
    Model model;
    build_sequential_print(print, model, config, {Vec3d(0, 0, 0), Vec3d(120, 0, 0)}, 2);

    REQUIRE(print.validate().string.empty());
    print.process();

    REQUIRE(print.is_sequential_print());
    REQUIRE(print.has_wipe_tower());
    REQUIRE(print.sequential_print_data().has_value());
    REQUIRE(print.sequential_print_data()->print_object_order.size() == 2);
    // Each object independently needs a tower (both are 2-colour).
    REQUIRE(count_object_orderings_with_tower(print) == 2);
}

// #1876 / #9399: the emitter must produce one prime tower per object.
TEST_CASE("SeqWT: By-Object 2 multicolour objects emit two prime towers (G-code)", "[SequentialWipeTower]")
{
    DynamicPrintConfig config = make_sequential_multicolour_config(2);

    Print print;
    Model model;
    build_sequential_print(print, model, config, {Vec3d(0, 0, 0), Vec3d(120, 0, 0)}, 2);
    REQUIRE(print.validate().string.empty());

    std::string gcode = seq_gcode(print);
    if (const char *p = std::getenv("SEQWT_DUMP")) {
        std::ofstream o(p);
        o << gcode;
    }

    // Per-object tower plans exist and are at two distinct positions.
    const auto &pod = print.sequential_print_data().value();
    REQUIRE(pod.object_wipe_tower_map.size() == 2);
    std::vector<Vec2f> positions;
    for (const auto &kv : pod.object_wipe_tower_map) {
        REQUIRE(kv.second.has_tower);
        REQUIRE_FALSE(kv.second.tool_changes.empty());
        positions.push_back(kv.second.position);
    }
    REQUIRE((positions[0] - positions[1]).norm() > 50.f);

    // G-code carries wipe-tower extrusion and plenty of tool changes.
    CHECK(gcode.find("WIPE_TOWER") != std::string::npos);
    CHECK(count_tool_changes(gcode) >= 8);

    // The two objects are printed one after the other: exactly one object
    // boundary in the G-code.
    std::vector<size_t> bounds = object_boundaries(gcode);
    REQUIRE(bounds.size() == 1);
    size_t split = bounds[0];

    // Each object's tower activity stays in its own X band, and object 2's tower
    // region is never touched while object 1 prints (and vice versa).
    auto span_a = wipe_tower_x_span(gcode, 0, split);
    auto span_b = wipe_tower_x_span(gcode, split, gcode.size());
    INFO("tower A x-span [" << span_a.first << "," << span_a.second << "]  "
         << "tower B x-span [" << span_b.first << "," << span_b.second << "]");
    REQUIRE(span_a.second < span_a.first + 60.f);          // A tower is compact
    REQUIRE(span_b.first > span_a.second + 40.f);          // B tower is well to the right of A
}

TEST_CASE("SeqWT regression: By-Layer 2 multicolour objects keep one global tower", "[SequentialWipeTower]")
{
    DynamicPrintConfig config = make_sequential_multicolour_config(2);
    config.set_key_value("print_sequence", new ConfigOptionEnum<PrintSequence>(PrintSequence::ByLayer));

    Print print;
    Model model;
    build_sequential_print(print, model, config, {Vec3d(0, 0, 0), Vec3d(120, 0, 0)}, 2);

    REQUIRE(print.validate().string.empty());
    print.process();

    REQUIRE_FALSE(print.is_sequential_print());
    REQUIRE(print.has_wipe_tower());
    REQUIRE(print.sequential_print_data() == std::nullopt);
    REQUIRE_FALSE(print.wipe_tower_data().tool_changes.empty());
}

TEST_CASE("SeqWT regression: prime tower disabled -> no tower", "[SequentialWipeTower]")
{
    DynamicPrintConfig config = make_sequential_multicolour_config(2);
    config.set_key_value("enable_prime_tower", new ConfigOptionBool(false));

    Print print;
    Model model;
    build_sequential_print(print, model, config, {Vec3d(0, 0, 0), Vec3d(120, 0, 0)}, 2);

    REQUIRE(print.validate().string.empty());
    print.process();

    REQUIRE_FALSE(print.has_wipe_tower());
    REQUIRE(count_object_orderings_with_tower(print) == 0);
}
