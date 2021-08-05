#include "../ClipperUtils.hpp"
#include "../ExPolygon.hpp"
#include "../Surface.hpp"
#include "../VariableWidth.hpp"

#include "FillConcentricWGapFill.hpp"

namespace Slic3r {

void FillConcentricWGapFill::fill_surface_extrusion(const Surface* surface, const FillParams& params, ExtrusionEntitiesPtr& out)
{
    // Perform offset.
    Slic3r::ExPolygons expp = offset_ex(surface->expolygon, double(scale_(0 - 0.5 * this->spacing)));
    // Create the infills for each of the regions.
    Polylines polylines_out;
    for (size_t i = 0; i < expp.size(); ++i) {
        ExPolygon expolygon = expp[i];

        coord_t distance = scale_(this->spacing / params.density);
        if (params.density > 0.9999f && !params.dont_adjust) {
            distance = scale_(this->spacing);
        }

        ExPolygons gaps;
        Polygons loops = (Polygons)expolygon;
        Polygons last = loops;
        bool first = true;
        while (!last.empty()) {
            Polygons next_onion = offset2(last, -double(distance + scale_(this->spacing) / 2), +double(scale_(this->spacing) / 2));
            loops.insert(loops.end(), next_onion.begin(), next_onion.end());
            append(gaps, diff_ex(
                offset(last, -0.5f * distance),
                offset(next_onion, 0.5f * distance + 10)));  // 10 is safty offset
            last = next_onion;
            if (first && !this->no_overlap_expolygons.empty()) {
                gaps = intersection_ex(gaps, this->no_overlap_expolygons);
            }
            first = false;
        }

        ExtrusionRole good_role = params.extrusion_role;
        ExtrusionEntityCollection *coll_nosort = new ExtrusionEntityCollection();
        coll_nosort->no_sort = this->no_sort(); //can be sorted inside the pass
        extrusion_entities_append_loops(
            coll_nosort->entities, std::move(loops),
            good_role,
            params.flow.mm3_per_mm(),
            params.flow.width,
            params.flow.height);

        //add gapfills
        if (!gaps.empty() && params.density >= 1) {
            double min = 0.2 * distance * (1 - INSET_OVERLAP_TOLERANCE);
            double max = 2. * distance;
            ExPolygons gaps_ex = diff_ex(
                offset2_ex(gaps, -float(min / 2), float(min / 2)),
                offset2_ex(gaps, -float(max / 2), float(max / 2)),
                true);
            ThickPolylines polylines;
            for (const ExPolygon& ex : gaps_ex) {
                // ignore too small gap
                if (ex.area() > min * max)
                    ex.medial_axis(max, min, &polylines);
            }

            if (!polylines.empty() && !is_bridge(good_role)) {
                ExtrusionEntityCollection gap_fill;
                variable_width(polylines, erGapFill, params.flow, gap_fill.entities);
                coll_nosort->append(std::move(gap_fill.entities));
            }
        }

        if (!coll_nosort->entities.empty())
            out.push_back(coll_nosort);
        else
            delete coll_nosort;
    }
}

}