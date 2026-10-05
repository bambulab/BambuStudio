#include "ColorDecomposeRecipe.hpp"

#include "FilamentMixer.hpp"
#include "Utils.hpp"
#include "nlohmann/json.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <unordered_map>
#include <utility>

namespace Slic3r {
namespace {

struct LabColor {
    double l{0.0};
    double a{0.0};
    double b{0.0};
};

struct StandardRecipeEntry {
    ColorDecomposeRecipeMode mode{ColorDecomposeRecipeMode::CMYW};
    std::string material;
    std::string source;
    std::vector<std::string> component_keys;
    std::vector<std::string> component_hexes;
    std::vector<int> ratios;
    std::string measured_hex;
    LabColor measured_lab;
};

static double srgb_to_linear(double v)
{
    v /= 255.0;
    return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}

static double xyz_to_lab_component(double v)
{
    constexpr double eps = 216.0 / 24389.0;
    constexpr double kappa = 24389.0 / 27.0;
    return v > eps ? std::cbrt(v) : (kappa * v + 16.0) / 116.0;
}

static LabColor rgb_to_lab(const ColorDecomposeRgb& rgb)
{
    const double r = srgb_to_linear(rgb.r);
    const double g = srgb_to_linear(rgb.g);
    const double b = srgb_to_linear(rgb.b);

    const double x = (0.4124564 * r + 0.3575761 * g + 0.1804375 * b) / 0.95047;
    const double y = (0.2126729 * r + 0.7151522 * g + 0.0721750 * b);
    const double z = (0.0193339 * r + 0.1191920 * g + 0.9503041 * b) / 1.08883;

    const double fx = xyz_to_lab_component(x);
    const double fy = xyz_to_lab_component(y);
    const double fz = xyz_to_lab_component(z);

    return {116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz)};
}

static std::string lab_to_srgb_hex(const LabColor& lab)
{
    constexpr double Xn = 0.95047, Yn = 1.0, Zn = 1.08883;

    auto f_inv = [](double t) -> double {
        constexpr double eps   = 216.0 / 24389.0;
        constexpr double kappa = 24389.0 / 27.0;
        const double t3 = t * t * t;
        return t3 > eps ? t3 : (t * 116.0 - 16.0) / kappa;
    };

    const double fy = (lab.l + 16.0) / 116.0;
    const double fx = lab.a / 500.0 + fy;
    const double fz = fy - lab.b / 200.0;

    const double X = Xn * f_inv(fx);
    const double Y = Yn * f_inv(fy);
    const double Z = Zn * f_inv(fz);

    double r =  3.2406 * X - 1.5372 * Y - 0.4986 * Z;
    double g = -0.9689 * X + 1.8758 * Y + 0.0415 * Z;
    double b =  0.0557 * X - 0.2040 * Y + 1.0570 * Z;

    auto gamma = [](double c) -> double {
        c = std::max(0.0, std::min(1.0, c));
        return c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
    };
    auto u8 = [&](double c) -> int {
        return std::max(0, std::min(255, static_cast<int>(std::lround(gamma(c) * 255.0))));
    };

    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", u8(r), u8(g), u8(b));
    return std::string(buf);
}

static double delta_e76(const LabColor& a, const LabColor& b)
{
    return std::sqrt(std::pow(a.l - b.l, 2.0) + std::pow(a.a - b.a, 2.0) + std::pow(a.b - b.b, 2.0));
}

static bool material_matches(const std::string& a, const std::string& b)
{
    if (a.empty() || b.empty())
        return false;
    return a == b || a == b + " Basic" || b == a + " Basic";
}

static std::vector<std::vector<int>> ratio_grid(size_t n)
{
    std::vector<std::vector<int>> out;
    if (n == 2) {
        for (int a = 20; a <= 80; a += 5)
            out.push_back({a, 100 - a});
    } else if (n == 3) {
        for (int a = 20; a <= 60; a += 5)
            for (int b = 20; b <= 80 - a; b += 5) {
                const int c = 100 - a - b;
                if (c >= 20)
                    out.push_back({a, b, c});
            }
    }
    return out;
}

static ColorDecomposeRecipeMode parse_mode(const std::string& s)
{
    if (s == "RYBW" || s == "RGBY")
        return ColorDecomposeRecipeMode::RYBW;
    return ColorDecomposeRecipeMode::CMYW;
}

static std::vector<StandardRecipeEntry> load_standard_entries()
{
    std::vector<StandardRecipeEntry> entries;
    const std::string path = resources_dir() + "/filament_mixing/standard_color_recipes.json";
    std::ifstream ifs(path);
    if (!ifs)
        return entries;

    nlohmann::json root = nlohmann::json::parse(ifs, nullptr, false);
    if (root.is_discarded() || !root.contains("entries") || !root["entries"].is_array())
        return entries;

    for (const auto& item : root["entries"]) {
        if (!item.is_object())
            continue;
        StandardRecipeEntry entry;
        entry.mode = parse_mode(item.value("mode", "CMYW"));
        entry.material = item.value("material", "");
        entry.source = item.value("source", "");
        entry.measured_hex = item.value("measured_rgb", "");

        if (item.contains("components") && item["components"].is_array()) {
            for (const auto& comp : item["components"]) {
                if (comp.is_object()) {
                    entry.component_keys.push_back(comp.value("key", ""));
                    entry.component_hexes.push_back(comp.value("rgb", ""));
                }
            }
        }
        if (item.contains("ratios") && item["ratios"].is_array()) {
            for (const auto& ratio : item["ratios"]) {
                if (ratio.is_number_integer())
                    entry.ratios.push_back(ratio.get<int>());
            }
        }
        if (item.contains("measured_lab") && item["measured_lab"].is_array() && item["measured_lab"].size() >= 3) {
            entry.measured_lab = {
                item["measured_lab"][0].get<double>(),
                item["measured_lab"][1].get<double>(),
                item["measured_lab"][2].get<double>()
            };
        } else {
            ColorDecomposeRgb measured_rgb;
            if (!color_decompose_hex_to_rgb(entry.measured_hex, measured_rgb))
                continue;
            entry.measured_lab = rgb_to_lab(measured_rgb);
        }

        if (entry.component_hexes.size() >= 2 && entry.component_hexes.size() == entry.ratios.size() &&
            !entry.measured_hex.empty())
            entries.push_back(std::move(entry));
    }
    return entries;
}

static const std::vector<StandardRecipeEntry>& standard_entries()
{
    static const std::vector<StandardRecipeEntry> entries = load_standard_entries();
    return entries;
}

// Canonical form of a component list: (normalized hex, ratio) pairs sorted so
// that matching is independent of the caller's component order. The key is the
// concatenated hexes, which also encodes the component count because every
// normalized hex is exactly 7 characters. Returns false on any invalid hex.
static bool canonicalize_blend_components(const std::vector<std::string>& hexes,
                                          const std::vector<int>& ratios,
                                          std::string& key_out,
                                          std::vector<int>& ratios_out)
{
    const size_t n = hexes.size();
    if (n == 0 || n != ratios.size())
        return false;

    std::vector<std::pair<std::string, int>> pairs;
    pairs.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        ColorDecomposeRgb rgb;
        if (!color_decompose_hex_to_rgb(hexes[i], rgb))
            return false;
        char buf[8];
        std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", rgb.r, rgb.g, rgb.b);
        pairs.emplace_back(buf, ratios[i]);
    }
    std::sort(pairs.begin(), pairs.end());

    key_out.clear();
    key_out.reserve(n * 7);
    ratios_out.clear();
    ratios_out.reserve(n);
    for (const auto& p : pairs) {
        key_out += p.first;
        ratios_out.push_back(p.second);
    }
    return true;
}

struct MeasuredBlendAnchor {
    std::vector<int> ratios; // reordered to match the canonical hex order
    LabColor         lab;
    std::string      hex;
};

// Measured/interpolated entries grouped by their canonical component set. Built
// once: re-normalizing and re-sorting every entry's components on each lookup
// used to dominate the decomposition search, which issues one lookup per point
// of the ratio grid for every candidate filament combination.
static const std::unordered_map<std::string, std::vector<MeasuredBlendAnchor>>& measured_blend_index()
{
    static const std::unordered_map<std::string, std::vector<MeasuredBlendAnchor>> index = [] {
        std::unordered_map<std::string, std::vector<MeasuredBlendAnchor>> out;
        for (const StandardRecipeEntry& entry : standard_entries()) {
            if (entry.source != "measured" && entry.source != "interpolated")
                continue;
            std::string         key;
            MeasuredBlendAnchor anchor;
            if (!canonicalize_blend_components(entry.component_hexes, entry.ratios, key, anchor.ratios))
                continue;
            anchor.lab = entry.measured_lab;
            anchor.hex = entry.measured_hex;
            out[key].push_back(std::move(anchor));
        }
        return out;
    }();
    return index;
}

static void evaluate_candidate(const ColorDecomposeRgb& target,
                               const std::vector<std::string>& hexes,
                               const std::vector<int>& ratios,
                               const std::vector<unsigned int>& indices,
                               ColorDecomposeRecipeMode mode,
                               double& best_score,
                               ColorDecomposeRecipeResult& best)
{
    const std::string mixed = blend_color_multi(hexes, ratios);
    ColorDecomposeRgb mixed_rgb;
    if (!color_decompose_hex_to_rgb(mixed, mixed_rgb))
        return;

    const double score = delta_e76(rgb_to_lab(target), rgb_to_lab(mixed_rgb));
    if (score >= best_score)
        return;

    best_score = score;
    best.valid = true;
    best.mode = mode;
    best.matched_color_hex = mixed;
    best.components.clear();
    for (size_t i = 0; i < hexes.size(); ++i) {
        ColorDecomposeRecipeComponent comp;
        comp.color_hex = hexes[i];
        comp.ratio = ratios[i];
        comp.filament_index = i < indices.size() ? indices[i] : 0;
        best.components.push_back(comp);
    }
}

} // namespace

std::string color_decompose_rgb_to_hex(const ColorDecomposeRgb& rgb)
{
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", rgb.r, rgb.g, rgb.b);
    return std::string(buf);
}

bool color_decompose_hex_to_rgb(const std::string& hex, ColorDecomposeRgb& out)
{
    if (hex.size() < 7 || hex[0] != '#')
        return false;
    unsigned r = 0, g = 0, b = 0;
    if (std::sscanf(hex.c_str(), "#%02x%02x%02x", &r, &g, &b) != 3)
        return false;
    out = {static_cast<unsigned char>(r), static_cast<unsigned char>(g), static_cast<unsigned char>(b)};
    return true;
}

ColorDecomposeRecipeResult recommend_from_physical_filaments(
    const ColorDecomposeRgb& target,
    const std::vector<ColorDecomposePhysicalFilament>& physical_filaments,
    const std::string& preferred_material_type)
{
    std::vector<ColorDecomposePhysicalFilament> candidates;
    for (const auto& filament : physical_filaments) {
        if (filament.is_mixed)
            continue;
        ColorDecomposeRgb ignored;
        if (!color_decompose_hex_to_rgb(filament.color_hex, ignored))
            continue;
        if (preferred_material_type.empty() || material_matches(filament.type, preferred_material_type))
            candidates.push_back(filament);
    }

    // Early exit: if a material-matched candidate has the exact target color,
    // return it as 100%. Downstream rejects single-component results (no mixed
    // slot created), which is correct -- the color already exists.
    const std::string target_hex = color_decompose_rgb_to_hex(target);
    for (const auto& cand : candidates) {
        ColorDecomposeRgb cand_rgb;
        if (!color_decompose_hex_to_rgb(cand.color_hex, cand_rgb))
            continue;
        if (color_decompose_rgb_to_hex(cand_rgb) == target_hex) {
            ColorDecomposeRecipeResult exact;
            exact.valid = true;
            exact.mode = ColorDecomposeRecipeMode::MaterialList;
            exact.matched_color_hex = cand.color_hex;
            ColorDecomposeRecipeComponent comp;
            comp.color_hex = cand.color_hex;
            comp.ratio = 100;
            comp.filament_index = cand.filament_index;
            exact.components.push_back(comp);
            return exact;
        }
    }

    if (candidates.size() < 2)
        candidates = physical_filaments;
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [](const auto& filament) {
        if (filament.is_mixed)
            return true;
        ColorDecomposeRgb ignored;
        return !color_decompose_hex_to_rgb(filament.color_hex, ignored);
    }), candidates.end());

    constexpr size_t kMaxCandidates = 8;
    if (candidates.size() > kMaxCandidates) {
        const LabColor target_lab = rgb_to_lab(target);
        std::sort(candidates.begin(), candidates.end(),
            [&target_lab](const ColorDecomposePhysicalFilament& a, const ColorDecomposePhysicalFilament& b) {
                ColorDecomposeRgb rgb_a, rgb_b;
                color_decompose_hex_to_rgb(a.color_hex, rgb_a);
                color_decompose_hex_to_rgb(b.color_hex, rgb_b);
                return delta_e76(target_lab, rgb_to_lab(rgb_a))
                     < delta_e76(target_lab, rgb_to_lab(rgb_b));
            });
        candidates.resize(kMaxCandidates);
    }

    ColorDecomposeRecipeResult best;
    double best_score = std::numeric_limits<double>::max();

    for (size_t i = 0; i < candidates.size(); ++i) {
        for (size_t j = i + 1; j < candidates.size(); ++j) {
            const std::vector<std::string> hexes = {candidates[i].color_hex, candidates[j].color_hex};
            const std::vector<unsigned int> indices = {candidates[i].filament_index, candidates[j].filament_index};
            for (const auto& ratios : ratio_grid(2))
                evaluate_candidate(target, hexes, ratios, indices, ColorDecomposeRecipeMode::MaterialList, best_score, best);

            for (size_t k = j + 1; k < candidates.size(); ++k) {
                const std::vector<std::string> hexes3 = {candidates[i].color_hex, candidates[j].color_hex, candidates[k].color_hex};
                const std::vector<unsigned int> indices3 = {candidates[i].filament_index, candidates[j].filament_index, candidates[k].filament_index};
                for (const auto& ratios : ratio_grid(3))
                    evaluate_candidate(target, hexes3, ratios, indices3, ColorDecomposeRecipeMode::MaterialList, best_score, best);
            }
        }
    }

    return best;
}

ColorDecomposeRecipeResult lookup_standard_recipe(
    const ColorDecomposeRgb& target,
    ColorDecomposeRecipeMode mode,
    const std::string& preferred_material_type)
{
    const LabColor target_lab = rgb_to_lab(target);
    ColorDecomposeRecipeResult best;
    double best_score = std::numeric_limits<double>::max();

    auto consider = [&](bool require_material_match) {
        for (const StandardRecipeEntry& entry : standard_entries()) {
            if (entry.mode != mode)
                continue;
            if (require_material_match && !material_matches(entry.material, preferred_material_type))
                continue;
            if (!require_material_match && !preferred_material_type.empty() && material_matches(entry.material, preferred_material_type))
                continue;

            const double score = delta_e76(target_lab, entry.measured_lab);
            if (score >= best_score)
                continue;

            best_score = score;
            best.valid = true;
            best.mode = mode;
            best.matched_color_hex = entry.measured_hex;
            best.components.clear();
            for (size_t i = 0; i < entry.component_hexes.size(); ++i) {
                ColorDecomposeRecipeComponent comp;
                comp.color_hex = entry.component_hexes[i];
                comp.base_color = i < entry.component_keys.size() ? entry.component_keys[i] : "";
                comp.ratio = entry.ratios[i];
                comp.filament_index = 0;
                best.components.push_back(comp);
            }
        }
    };

    consider(true);
    if (!best.valid)
        consider(false);
    return best;
}

std::string lookup_measured_blend_color(const std::vector<std::string>& component_hexes,
                                       const std::vector<int>& ratios)
{
    if (component_hexes.size() < 2 || component_hexes.size() != ratios.size())
        return {};

    // Stage 1: canonicalize input by sorting (hex, ratio) pairs so matching
    // is independent of the caller's component order.
    const size_t n = component_hexes.size();
    std::string in_key;
    std::vector<int> in_ratios;
    if (!canonicalize_blend_components(component_hexes, ratios, in_key, in_ratios))
        return {};

    // Normalize ratios to sum=100 (callers may pass arbitrary weights,
    // e.g. MixedFilamentDialog uses ratio*10000).
    {
        int sum = 0;
        for (int r : in_ratios) sum += r;
        if (sum > 0 && sum != 100) {
            int new_sum = 0;
            for (size_t i = 0; i < in_ratios.size(); ++i) {
                in_ratios[i] = static_cast<int>(std::lround(
                    static_cast<double>(in_ratios[i]) * 100.0 / static_cast<double>(sum)));
                new_sum += in_ratios[i];
            }
            if (new_sum != 100) {
                auto it = std::max_element(in_ratios.begin(), in_ratios.end());
                *it += (100 - new_sum);
            }
        }
    }

    // Fall back to polynomial model for ratios outside the measured range.
    {
        bool out_of_range = false;
        if (n == 2) {
            for (int r : in_ratios)
                if (r < 20 || r > 80) { out_of_range = true; break; }
        } else {
            for (int r : in_ratios)
                if (r < 20) { out_of_range = true; break; }
        }
        if (out_of_range)
            return {};
    }

    // Stage 2: anchors sharing the component set; try exact ratio match.
    const auto& index = measured_blend_index();
    const auto  it    = index.find(in_key);
    if (it == index.end())
        return {};
    const std::vector<MeasuredBlendAnchor>& anchors = it->second;

    for (const MeasuredBlendAnchor& a : anchors)
        if (a.ratios == in_ratios)
            return a.hex;

    if (anchors.size() < 2)
        return {};

    // Stage 3: interpolation in Lab space.
    if (n == 2) {
        // 1D linear interpolation along ratio[0].
        std::vector<const MeasuredBlendAnchor*> by_ratio;
        by_ratio.reserve(anchors.size());
        for (const MeasuredBlendAnchor& a : anchors)
            by_ratio.push_back(&a);
        std::sort(by_ratio.begin(), by_ratio.end(),
                  [](const MeasuredBlendAnchor* a, const MeasuredBlendAnchor* b) { return a->ratios[0] < b->ratios[0]; });
        const double x = static_cast<double>(in_ratios[0]);
        size_t lo = 0;
        while (lo + 2 < by_ratio.size() && static_cast<double>(by_ratio[lo + 1]->ratios[0]) <= x)
            ++lo;
        const MeasuredBlendAnchor& a0 = *by_ratio[lo];
        const MeasuredBlendAnchor& a1 = *by_ratio[lo + 1];
        const double span = static_cast<double>(a1.ratios[0] - a0.ratios[0]);
        const double t = span > 0.0 ? (x - static_cast<double>(a0.ratios[0])) / span : 0.0;
        return lab_to_srgb_hex({a0.lab.l + t * (a1.lab.l - a0.lab.l),
                                a0.lab.a + t * (a1.lab.a - a0.lab.a),
                                a0.lab.b + t * (a1.lab.b - a0.lab.b)});
    }

    // 3+ color: IDW (p=2) with 3 nearest anchors in the (ratio[0], ratio[1]) plane.
    const double ra = static_cast<double>(in_ratios[0]);
    const double rb = static_cast<double>(in_ratios[1]);
    std::vector<std::pair<double, const MeasuredBlendAnchor*>> dists;
    dists.reserve(anchors.size());
    for (const MeasuredBlendAnchor& a : anchors) {
        const double d = std::sqrt(std::pow(ra - static_cast<double>(a.ratios[0]), 2.0) +
                                   std::pow(rb - static_cast<double>(a.ratios[1]), 2.0));
        if (d == 0.0)
            return a.hex;
        dists.emplace_back(d, &a);
    }
    const size_t k = std::min(static_cast<size_t>(3), dists.size());
    std::partial_sort(dists.begin(), dists.begin() + k, dists.end(),
                      [](const auto& a, const auto& b) { return a.first < b.first; });
    double num_l = 0.0, num_a = 0.0, num_b = 0.0, den = 0.0;
    for (size_t j = 0; j < k; ++j) {
        const double w = 1.0 / (dists[j].first * dists[j].first);
        num_l += w * dists[j].second->lab.l;
        num_a += w * dists[j].second->lab.a;
        num_b += w * dists[j].second->lab.b;
        den += w;
    }
    return lab_to_srgb_hex({num_l / den, num_a / den, num_b / den});
}

} // namespace Slic3r
