#ifndef libslic3r_FuzzySkin_hpp_
#define libslic3r_FuzzySkin_hpp_

namespace Slic3r::Arachne {
struct ExtrusionLine;
} // namespace Slic3r::Arachne

namespace Slic3r {

void fuzzy_polyline(Points &poly, bool closed, coordf_t slice_z, const PrintRegionConfig &config);
void fuzzy_polygon(Polygon &polygon, coordf_t slice_z, const PrintRegionConfig &config);
void fuzzy_extrusion_line(Arachne::ExtrusionLine &ext_lines, coordf_t slice_z, const PrintRegionConfig &config);

bool should_fuzzify(const PrintRegionConfig &config, size_t layer_idx, size_t perimeter_idx, bool is_contour);

Polygon apply_fuzzy_skin(const Polygon &polygon, const PrintRegionConfig &base_config, const PerimeterRegions &perimeter_regions, size_t layer_idx, size_t perimeter_idx, bool is_contour, coordf_t slice_z);

Arachne::ExtrusionLine apply_fuzzy_skin(const Arachne::ExtrusionLine &extrusion, const PrintRegionConfig &base_config, const PerimeterRegions &perimeter_regions, size_t layer_idx, size_t perimeter_idx, bool is_contour, coordf_t slice_z);

} // namespace Slic3r

#endif // libslic3r_FuzzySkin_hpp_