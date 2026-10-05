#ifndef slic3r_FillConformalChart_hpp_
#define slic3r_FillConformalChart_hpp_

#include "../ExPolygon.hpp"
#include "../Polyline.hpp"

#include <memory>
#include <vector>

struct indexed_triangle_set;

namespace Slic3r {
namespace FillConformal {

// One layer's 2D generator: an open spine (sole) or a closed mid-loop (collar).
struct LayerGenerator
{
    double   z { 0. };
    bool     closed { false };
    Polyline polyline;
    double   length { 0. }; // scaled
};

// 3D SDF-ridge sheet (when available), plus per-layer curves G = sheet ∩ layer
// (or 2D MAT fallback). u is tracked across height after alignment.
class MedialChart
{
public:
    MedialChart();
    ~MedialChart();
    MedialChart(MedialChart &&) noexcept;
    MedialChart &operator=(MedialChart &&) noexcept;
    MedialChart(const MedialChart &) = delete;
    MedialChart &operator=(const MedialChart &) = delete;

    static std::unique_ptr<MedialChart> build(const std::vector<ExPolygon> &islands,
                                              const std::vector<double>    &zs,
                                              double                        spacing);

    const LayerGenerator *layer(size_t layer_id) const;
    size_t                layer_count() const { return m_layers.size(); }
    bool                  empty() const { return m_layers.empty(); }
    const ::indexed_triangle_set *sheet_mesh() const;

private:
    std::vector<LayerGenerator> m_layers;
    std::unique_ptr<::indexed_triangle_set> m_sheet;
};

// 2D medial-axis / inset generator for a single island (also used without a loft).
bool extract_generator(const ExPolygon &expoly, double spacing, LayerGenerator &out);

} // namespace FillConformal
} // namespace Slic3r

#endif // slic3r_FillConformalChart_hpp_
