#include "../Print.hpp"
#include "../ShortestPath.hpp"

#include "ClipperUtils.hpp"
#include "FillLightning.hpp"
#include "Lightning/Generator.hpp"

namespace Slic3r::FillLightning {

void Filler::_fill_surface_single(
    const FillParams              &params,
    unsigned int                   thickness_layers,
    const std::pair<float, Point> &direction,
    ExPolygon                      expolygon,
    Polylines                     &polylines_out)
{
    if (!generator) return;
    const Layer &layer      = generator->getTreesForLayer(this->layer_id);
    Polylines    fill_lines = layer.convertToLines(to_polygons(expolygon), scaled<coord_t>(0.5 * this->spacing - this->overlap));
    
    // Apply multiline offset if needed
    multiline_fill(fill_lines, params, spacing);
    Polylines all_polylines = intersection_pl(std::move(fill_lines), expolygon);
    if (params.dont_connect() || all_polylines.size() <= 1) {
        append(polylines_out, chain_polylines(std::move(all_polylines)));
    } else
        connect_infill(std::move(all_polylines), expolygon, polylines_out, this->spacing, params);
}

void GeneratorDeleter::operator()(Generator *p) {
    delete p;
}

GeneratorPtr build_generator(const PrintObject &print_object, const std::function<void()> &throw_on_cancel_callback)
{
    return GeneratorPtr(new Generator(print_object, throw_on_cancel_callback));
}

} // namespace Slic3r::FillAdaptive
