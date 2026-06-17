#include "PrintConfig.hpp"
#include "ClipperUtils.hpp"
#include "Flow.hpp"

namespace Slic3r {

namespace {

Points to_points(const std::vector<Vec2d> &dpts)
{
    Points pts;
    pts.reserve(dpts.size());
    for (const auto &v : dpts)
        pts.emplace_back(coord_t(scale_(v.x())), coord_t(scale_(v.y())));
    return pts;
}

} // namespace

Polygon get_shared_poly(const std::vector<Pointfs> &extruder_polys)
{
    Polygon result;
    for (size_t index = 0; index < extruder_polys.size(); ++index) {
        const Pointfs &extruder_area = extruder_polys[index];
        if (index == 0) {
            result.points = to_points(extruder_area);
        } else {
            Polygon extruder_poly;
            extruder_poly.points = to_points(extruder_area);
            Polygons result_polygon = intersection(extruder_poly, result);
            result = result_polygon[0];
        }
    }
    return result;
}

Points get_bed_shape(const DynamicPrintConfig &config, bool use_share)
{
    const ConfigOptionPoints *bed_shape_opt = config.opt<ConfigOptionPoints>("printable_area");
    if (!bed_shape_opt) {
        if (auto center_opt = config.opt<ConfigOptionPoint>("center"))
            return { scaled(center_opt->value) };
        return {};
    }

    Polygon bed_poly;
    if (use_share) {
        const ConfigOptionPointsGroups *extruder_area_opt = config.opt<ConfigOptionPointsGroups>("extruder_printable_area");
        if (extruder_area_opt && extruder_area_opt->size() > 0) {
            const std::vector<Pointfs> &extruder_areas = extruder_area_opt->values;
            bed_poly = get_shared_poly(extruder_areas);
        } else {
            bed_poly.points = to_points(bed_shape_opt->values);
        }
    } else {
        bed_poly.points = to_points(bed_shape_opt->values);
    }

    return bed_poly.points;
}

Points get_bed_shape(const PrintConfig &cfg, bool use_share)
{
    Polygon bed_poly;
    if (use_share) {
        const std::vector<Pointfs> &extruder_areas = cfg.extruder_printable_area.values;
        if (!extruder_areas.empty()) {
            bed_poly = get_shared_poly(extruder_areas);
        } else {
            bed_poly.points = to_points(cfg.printable_area.values);
        }
    } else {
        bed_poly.points = to_points(cfg.printable_area.values);
    }

    return bed_poly.points;
}

Points get_bed_shape(const SLAPrinterConfig &cfg)
{
    return to_points(cfg.printable_area.values);
}

Polygon get_bed_shape_with_excluded_area(const PrintConfig &cfg, bool use_share)
{
    Polygon bed_poly;
    bed_poly.points = get_bed_shape(cfg, use_share);

    Points   exclude_area_points = to_points(cfg.bed_exclude_area.values);
    Polygons exclude_polys;
    Polygon  exclude_poly;
    for (size_t i = 0; i < exclude_area_points.size(); ++i) {
        exclude_poly.points.emplace_back(exclude_area_points[i]);
        if (i % 4 == 3) {
            exclude_polys.push_back(exclude_poly);
            exclude_poly.points.clear();
        }
    }

    if (cfg.enable_wrapping_detection.value) {
        const Pointfs &wrapping_detection_area = cfg.wrapping_exclude_area.values;
        Polygon        wrapping_poly;
        for (const auto &pt : wrapping_detection_area)
            wrapping_poly.points.push_back(Point(scale_(pt.x()), scale_(pt.y())));
        exclude_polys.push_back(wrapping_poly);
    }

    auto diff_polys = diff({ bed_poly }, exclude_polys);
    if (!diff_polys.empty())
        bed_poly = diff_polys[0];
    return bed_poly;
}

bool has_skirt(const ConfigBase &cfg)
{
    auto opt_skirt_height = cfg.option("skirt_height");
    auto opt_skirt_loops = cfg.option("skirt_loops");
    auto opt_draft_shield = cfg.option("draft_shield");
    return (opt_skirt_height && opt_skirt_height->getInt() > 0 && opt_skirt_loops && opt_skirt_loops->getInt() > 0)
        || (opt_draft_shield && opt_draft_shield->getInt() != dsDisabled);
}

float get_real_skirt_dist(const ConfigBase &cfg)
{
    auto opt_skirt_per_object = cfg.option("skirt_per_object");
    if (!opt_skirt_per_object || !opt_skirt_per_object->getBool())
        return 0.f;

    if (!has_skirt(cfg))
        return 0.f;

    auto opt_skirt_loops = cfg.option("skirt_loops");
    int  skirt_loops = opt_skirt_loops ? opt_skirt_loops->getInt() : 0;
    auto opt_draft_shield = cfg.option("draft_shield");
    if (opt_draft_shield && opt_draft_shield->getInt() != dsDisabled && skirt_loops == 0)
        skirt_loops = 1;
    if (skirt_loops <= 0)
        return 0.f;

    auto  opt_dist = cfg.option("skirt_distance");
    float skirt_distance = opt_dist ? static_cast<float>(opt_dist->getFloat()) : 0.f;
    auto  opt_nozzle = cfg.option("nozzle_diameter");
    auto  opt_nozzle_f = dynamic_cast<const ConfigOptionFloats *>(opt_nozzle);
    float nozzle_dia = opt_nozzle_f ? static_cast<float>(opt_nozzle_f->get_at(0)) : 0.4f;
    auto  opt_lh = cfg.option("initial_layer_print_height");
    float layer_height = opt_lh ? static_cast<float>(opt_lh->getFloat()) : 0.2f;

    ConfigOptionFloat width_opt;
    auto              opt_lw = cfg.option("initial_layer_line_width");
    width_opt.value = (opt_lw && opt_lw->getFloat() > 0) ? opt_lw->getFloat() : 0;
    if (width_opt.value == 0) {
        auto opt_gen_lw = cfg.option("line_width");
        width_opt.value = (opt_gen_lw && opt_gen_lw->getFloat() > 0) ? opt_gen_lw->getFloat() : 0;
    }

    Flow  flow = Flow::new_from_config_width(frPerimeter, width_opt, nozzle_dia, layer_height);
    float spacing = flow.spacing();
    float flow_width = flow.width();
    return skirt_distance + (skirt_loops - 0.5f) * spacing + 0.5f * flow_width;
}

} // namespace Slic3r
