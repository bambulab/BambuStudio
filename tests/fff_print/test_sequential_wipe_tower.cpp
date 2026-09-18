// Tests for per-object prime towers in By-Object (sequential) printing
// (issues #1876 / #9399). See docs/sequential_wipe_tower_architecture.md and
// docs/sequential_wipe_tower_test_strategy.md.

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

// An object whose walls print with filament `a` and infill/surfaces with `b`,
// through per-object region config so each sequential object carries its own
// tool schedule. With a != b every layer has a tool change and the object needs
// a tower; with a == b it is single-colour and needs none.
ModelObject *add_multicolour_object(Model &model, const Vec3d &offset, double w, double d, double h, int a, int b)
{
    ModelObject *o = model.add_object();
    o->add_volume(make_cube(w, d, h));
    o->config.set_key_value("wall_filament", new ConfigOptionInt(a));
    o->config.set_key_value("solid_infill_filament", new ConfigOptionInt(b));
    o->config.set_key_value("sparse_infill_filament", new ConfigOptionInt(b));
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
    // A roomy bed (P2S-ish) so per-object tower placement has space.
    config.set_key_value("printable_area", new ConfigOptionPoints(
        {Vec2d(0, 0), Vec2d(256, 0), Vec2d(256, 256), Vec2d(0, 256)}));
    config.set_key_value("print_sequence", new ConfigOptionEnum<PrintSequence>(PrintSequence::ByObject));
    // Smooth timelapse and wrapping detection are rejected for By Object.
    config.set_key_value("timelapse_type", new ConfigOptionEnum<TimelapseType>(TimelapseType::tlTraditional));
    config.set_key_value("enable_wrapping_detection", new ConfigOptionBool(false));
    // Prime tower requires relative E.
    config.set_key_value("use_relative_e_distances", new ConfigOptionBool(true));
    config.set_key_value("wipe_tower_no_sparse_layers", new ConfigOptionBool(false));
    return config;
}

struct ObjSpec {
    Vec3d  offset;
    int    wall = 1;    // wall filament
    int    infill = 2;  // infill/surface filament (== wall -> single colour)
    double h = 2.0;     // object height (mm)
};

void build_sequential_print(Print &print, Model &model, const DynamicPrintConfig &config_in,
                            const std::vector<ObjSpec> &objs, int n_filaments,
                            double w = 20, double d = 20)
{
    model.clear_objects();
    for (const ObjSpec &s : objs)
        add_multicolour_object(model, s.offset, w, d, s.h, s.wall, s.infill);

    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_num_filaments(n_filaments);
    config.apply(config_in);

    for (ModelObject *mo : model.objects) {
        mo->ensure_on_bed();
        print.auto_assign_extruders(mo);
    }
    print.apply(model, config);
    print.set_status_silent();
    // validate() runs sequential_print_clearance_valid(), which assigns each
    // instance an arrange_order. Without that, sort_object_instances_by_model_order()
    // collapses all instances onto the first (they all have arrange_order 0).
    print.validate();
}

// Convenience: N identical two-colour objects spaced `pitch` mm along X.
std::vector<ObjSpec> row_of_multicolour(int n, double pitch = 120.0)
{
    std::vector<ObjSpec> v;
    for (int i = 0; i < n; ++i)
        v.push_back(ObjSpec{Vec3d(pitch * i, 0, 0), 1, 2, 2.0});
    return v;
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
    build_sequential_print(print, model, config, row_of_multicolour(2), 2);

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
    build_sequential_print(print, model, config, row_of_multicolour(2), 2);
    REQUIRE(print.validate().string.empty());

    std::string gcode = seq_gcode(print);
    if (const char *p = std::getenv("SEQWT_DUMP")) {
        std::ofstream o(p);
        o << gcode;
    }

    // Per-object tower plans exist, each near its object and not on top of it.
    const auto &pod = print.sequential_print_data().value();
    REQUIRE(pod.object_wipe_tower_map.size() == 2);
    auto box2d = [](const BoundingBoxf3 &b) {
        return BoundingBoxf(Vec2d(b.min.x(), b.min.y()), Vec2d(b.max.x(), b.max.y()));
    };
    auto overlaps = [](const BoundingBoxf &a, const BoundingBoxf &b) {
        return a.min.x() < b.max.x() && b.min.x() < a.max.x() &&
               a.min.y() < b.max.y() && b.min.y() < a.max.y();
    };
    std::vector<BoundingBoxf> tower_boxes;
    for (const PrintObject *o : pod.print_object_order) {
        const auto &plan = pod.object_wipe_tower_map.at(o);
        REQUIRE(plan.has_tower);
        REQUIRE_FALSE(plan.tool_changes.empty());
        BoundingBoxf obj = box2d(o->model_object()->instance_bounding_box(0));
        BoundingBoxf tw(Vec2d(plan.position.x() + plan.bbx.min.x() + plan.rib_offset.x(),
                              plan.position.y() + plan.bbx.min.y() + plan.rib_offset.y()),
                        Vec2d(plan.position.x() + plan.bbx.max.x() + plan.rib_offset.x(),
                              plan.position.y() + plan.bbx.max.y() + plan.rib_offset.y()));
        INFO("obj x[" << obj.min.x() << "," << obj.max.x() << "] y[" << obj.min.y() << "," << obj.max.y() << "]  "
             "tower x[" << tw.min.x() << "," << tw.max.x() << "] y[" << tw.min.y() << "," << tw.max.y() << "]");
        REQUIRE_FALSE(overlaps(obj, tw));                     // tower is beside, not on, its object
        REQUIRE((obj.center() - tw.center()).norm() < 90.f);  // but nearby
        REQUIRE(tw.min.x() >= 0.0);                           // on the bed
        REQUIRE(tw.min.y() >= 0.0);
        REQUIRE(tw.max.x() <= 256.0);
        REQUIRE(tw.max.y() <= 256.0);
        tower_boxes.push_back(tw);
    }
    REQUIRE_FALSE(overlaps(tower_boxes[0], tower_boxes[1]));   // towers don't overlap each other

    // G-code carries wipe-tower extrusion and plenty of tool changes.
    CHECK(gcode.find("WIPE_TOWER") != std::string::npos);
    CHECK(count_tool_changes(gcode) >= 8);

    // The two objects print one after the other: exactly one object boundary.
    std::vector<size_t> bounds = object_boundaries(gcode);
    REQUIRE(bounds.size() == 1);
    size_t split = bounds[0];

    // All wipe-tower extrusion before the boundary is in tower A's footprint;
    // after the boundary, in tower B's -- neither tower is revisited.
    auto in_x = [](std::pair<float,float> sp, const BoundingBoxf &b) {
        return sp.first >= b.min.x() - 2.f && sp.second <= b.max.x() + 2.f;
    };
    REQUIRE(in_x(wipe_tower_x_span(gcode, 0, split), tower_boxes[0]));
    REQUIRE(in_x(wipe_tower_x_span(gcode, split, gcode.size()), tower_boxes[1]));
}

TEST_CASE("SeqWT regression: By-Layer 2 multicolour objects keep one global tower", "[SequentialWipeTower]")
{
    DynamicPrintConfig config = make_sequential_multicolour_config(2);
    config.set_key_value("print_sequence", new ConfigOptionEnum<PrintSequence>(PrintSequence::ByLayer));

    Print print;
    Model model;
    build_sequential_print(print, model, config, row_of_multicolour(2), 2);

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
    build_sequential_print(print, model, config, row_of_multicolour(2), 2);

    REQUIRE(print.validate().string.empty());
    print.process();

    REQUIRE_FALSE(print.has_wipe_tower());
    REQUIRE(count_object_orderings_with_tower(print) == 0);
}

// Task doc Test 3: three objects -> three towers (guards against a 2-object
// special case).
TEST_CASE("SeqWT: three multicolour objects get three towers", "[SequentialWipeTower]")
{
    DynamicPrintConfig config = make_sequential_multicolour_config(2);

    Print print;
    Model model;
    build_sequential_print(print, model, config, row_of_multicolour(3), 2);

    REQUIRE(print.validate().string.empty());
    print.process();

    REQUIRE(print.sequential_print_data()->print_object_order.size() == 3);
    REQUIRE(count_object_orderings_with_tower(print) == 3);
    REQUIRE(print.sequential_print_data()->object_wipe_tower_map.size() == 3);

    std::string gcode = seq_gcode(print);
    REQUIRE(object_boundaries(gcode).size() == 2);   // three objects, two boundaries
}

// Task doc Test 4: a single-colour object between two multicolour ones gets no
// tower of its own.
TEST_CASE("SeqWT: single-colour object in the middle gets no tower", "[SequentialWipeTower]")
{
    DynamicPrintConfig config = make_sequential_multicolour_config(2);

    Print print;
    Model model;
    build_sequential_print(print, model, config, {
        ObjSpec{Vec3d(0, 0, 0),   1, 2, 2.0},   // A: two colours
        ObjSpec{Vec3d(120, 0, 0), 1, 1, 2.0},   // B: single colour
        ObjSpec{Vec3d(240, 0, 0), 1, 2, 2.0},   // C: two colours
    }, 2);

    REQUIRE(print.validate().string.empty());
    print.process();

    const auto &pod = print.sequential_print_data().value();
    REQUIRE(pod.print_object_order.size() == 3);
    // A and C get a tower, B does not.
    REQUIRE(count_object_orderings_with_tower(print) == 2);
    const PrintObject *b = pod.print_object_order[1];
    REQUIRE(pod.object_wipe_tower_map.count(b) == 0);
}

// Task doc Test 7 (issue #9165): each tower follows its own object's tool order.
TEST_CASE("SeqWT: each tower follows its object's own filament order", "[SequentialWipeTower]")
{
    DynamicPrintConfig config = make_sequential_multicolour_config(3);

    Print print;
    Model model;
    build_sequential_print(print, model, config, {
        ObjSpec{Vec3d(0, 0, 0),   1, 2, 2.0},   // A: walls 1, infill 2
        ObjSpec{Vec3d(120, 0, 0), 1, 3, 2.0},   // B: walls 1, infill 3 (different schedule)
    }, 3);

    REQUIRE(print.validate().string.empty());
    print.process();

    const auto &pod = print.sequential_print_data().value();
    REQUIRE(pod.print_object_order.size() == 2);
    const ToolOrdering &to_a = pod.object_tool_ordering_map.at(pod.print_object_order[0]);
    const ToolOrdering &to_b = pod.object_tool_ordering_map.at(pod.print_object_order[1]);

    // Each object's ToolOrdering uses only its own two filaments -- no cross-talk.
    auto uses = [](const ToolOrdering &to, unsigned f) {
        const auto &e = to.all_extruders();
        return std::find(e.begin(), e.end(), f) != e.end();
    };
    REQUIRE(to_a.all_extruders().size() == 2);
    REQUIRE(to_b.all_extruders().size() == 2);
    REQUIRE(uses(to_a, 1));                 // filament 2 (id 1)
    REQUIRE_FALSE(uses(to_a, 2));           // never filament 3
    REQUIRE(uses(to_b, 2));                 // filament 3 (id 2)
    REQUIRE_FALSE(uses(to_b, 1));           // never filament 2

    // Both get a tower planned from their own ordering.
    REQUIRE(count_object_orderings_with_tower(print) == 2);
    const auto &plan_a = pod.object_wipe_tower_map.at(pod.print_object_order[0]);
    const auto &plan_b = pod.object_wipe_tower_map.at(pod.print_object_order[1]);
    REQUIRE_FALSE(plan_a.tool_changes.empty());
    REQUIRE_FALSE(plan_b.tool_changes.empty());
}

// Task doc collision A: wide separation slices without complaint.
TEST_CASE("SeqWT collision: wide separation slices", "[SequentialWipeTower]")
{
    DynamicPrintConfig config = make_sequential_multicolour_config(2);

    Print print;
    Model model;
    build_sequential_print(print, model, config, row_of_multicolour(2, 150.0), 2);

    REQUIRE(print.validate().string.empty());
    REQUIRE_NOTHROW(print.process());
    REQUIRE(count_object_orderings_with_tower(print) == 2);
}

// Task doc collision B/C/D: when there is no room on the bed for a tower clear
// of the other objects, the slice is rejected with a clear message.
TEST_CASE("SeqWT collision: no room for a tower rejects the slice", "[SequentialWipeTower]")
{
    DynamicPrintConfig config = make_sequential_multicolour_config(2);
    // Tiny bed, two objects taking up most of it -> nowhere to put a tower.
    config.set_key_value("printable_area", new ConfigOptionPoints(
        {Vec2d(0, 0), Vec2d(70, 0), Vec2d(70, 30), Vec2d(0, 30)}));

    Print print;
    Model model;
    build_sequential_print(print, model, config, {
        ObjSpec{Vec3d(0, 0, 0),  1, 2, 2.0},   // A world X[0,20]
        ObjSpec{Vec3d(45, 0, 0), 1, 2, 3.0},   // B world X[45,65] (distinct height -> distinct object)
    }, 2);

    REQUIRE_THROWS_AS(print.process(), Slic3r::SlicingError);
}

// Task doc Test 8: objects of different height keep independent towers.
TEST_CASE("SeqWT: objects of different height keep independent towers", "[SequentialWipeTower]")
{
    DynamicPrintConfig config = make_sequential_multicolour_config(2);

    Print print;
    Model model;
    build_sequential_print(print, model, config, {
        ObjSpec{Vec3d(0, 0, 0),   1, 2, 2.0},
        ObjSpec{Vec3d(120, 0, 0), 1, 2, 4.0},   // twice as tall
    }, 2);

    REQUIRE(print.validate().string.empty());

    std::string gcode = seq_gcode(print);
    std::vector<size_t> bounds = object_boundaries(gcode);
    REQUIRE(bounds.size() == 1);

    // Tower B has more planned layers than tower A (B is taller).
    const auto &pod = print.sequential_print_data().value();
    const auto &plan_a = pod.object_wipe_tower_map.at(pod.print_object_order[0]);
    const auto &plan_b = pod.object_wipe_tower_map.at(pod.print_object_order[1]);
    REQUIRE(plan_b.tool_changes.size() > plan_a.tool_changes.size());
}
