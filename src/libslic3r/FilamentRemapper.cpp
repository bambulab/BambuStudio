#include "FilamentRemapper.hpp"

#include <algorithm>
#include <boost/algorithm/string.hpp>
#include "libslic3r/Log.hpp"
#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>

#include "CustomGCode.hpp"
#include "FilamentMixer.hpp"
#include "FlushVolCalc.hpp"
#include "Model.hpp"
#include "Preset.hpp"
#include "TriangleSelector.hpp"

namespace Slic3r {

namespace {

static const char *FILAMENT_INT_KEYS[] = {
    "extruder",
    "support_filament",
    "support_interface_filament",
    "sparse_infill_filament",
    "solid_infill_filament",
    "wall_filament",
};

int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool remap_int_option(ModelConfig &cfg, const char *key, const std::vector<int> &old_to_new_1based)
{
    if (!cfg.has(key))
        return false;
    int old_v = cfg.opt_int(key);
    if (old_v <= 0)
        return false;
    if (old_v > (int) old_to_new_1based.size()) {
        cfg.erase(key);
        return true;
    }
    int new_v = old_to_new_1based[old_v - 1];
    if (new_v <= 0)
        cfg.erase(key);
    else
        cfg.set_key_value(key, new ConfigOptionInt(new_v));
    return true;
}

void remap_model_config(ModelConfig &cfg, const std::vector<int> &old_to_new_1based)
{
    for (const char *key : FILAMENT_INT_KEYS)
        remap_int_option(cfg, key, old_to_new_1based);
}

void remap_custom_gcodes(CustomGCode::Info &info, const std::vector<int> &old_to_new_1based)
{
    for (CustomGCode::Item &item : info.gcodes) {
        if (item.type != CustomGCode::ToolChange && item.type != CustomGCode::ColorChange)
            continue;
        if (item.extruder <= 0)
            continue;
        if (item.extruder > (int) old_to_new_1based.size()) {
            item.extruder = 1;
            continue;
        }
        int mapped = old_to_new_1based[item.extruder - 1];
        item.extruder = mapped > 0 ? mapped : 1;
    }
}

void ensure_string_array(DynamicPrintConfig &config, const char *key, size_t n, const std::string &fill = {})
{
    auto *opt = config.option<ConfigOptionStrings>(key, true);
    opt->values.assign(n, fill);
}

void ensure_bool_array(DynamicPrintConfig &config, const char *key, size_t n, bool fill = false)
{
    auto *opt = config.option<ConfigOptionBools>(key, true);
    opt->values.assign(n, fill ? (unsigned char) 1 : (unsigned char) 0);
}

void ensure_int_array(DynamicPrintConfig &config, const char *key, size_t n, int fill)
{
    auto *opt = config.option<ConfigOptionInts>(key, true);
    opt->values.assign(n, fill);
}

void apply_filament_preset_to_config(DynamicPrintConfig       &dest,
                                     const DynamicPrintConfig &preset,
                                     size_t                    filament_idx_0based,
                                     size_t                    variant_write_offset,
                                     size_t                    variant_count)
{
    for (const t_config_option_key &opt_key : preset.keys()) {
        const ConfigOption *source_opt = preset.option(opt_key);
        if (source_opt == nullptr)
            continue;

        if (opt_key == "compatible_prints" || opt_key == "compatible_printers" || opt_key == "model_id" ||
            opt_key == "dev_model_name" || opt_key == "filament_settings_id" || opt_key == "name" ||
            opt_key == "from" || opt_key == "type" || opt_key == "version" || opt_key == "setting_id" ||
            opt_key == "instantiation" || opt_key == "inherits" || opt_key == "filament_id")
            continue;

        if (source_opt->is_scalar()) {
            if (opt_key == "compatible_printers_condition") {
                // Layout matches CLI load_filaments: index 0 unused/process-side, filaments at 1..N.
                auto *opt_vec = dest.option<ConfigOptionStrings>("compatible_machine_expression_group", true);
                if (opt_vec->values.size() < filament_idx_0based + 2)
                    opt_vec->values.resize(filament_idx_0based + 2);
                opt_vec->set_at(source_opt, filament_idx_0based + 1, 0);
            } else if (opt_key == "compatible_prints_condition") {
                auto *opt_vec = dest.option<ConfigOptionStrings>("compatible_process_expression_group", true);
                if (opt_vec->values.size() <= filament_idx_0based)
                    opt_vec->values.resize(filament_idx_0based + 1);
                opt_vec->set_at(source_opt, filament_idx_0based, 0);
            }
            continue;
        }

        ConfigOption *opt = dest.option(opt_key, true);
        if (opt == nullptr)
            continue;
        auto *opt_vec_dst = static_cast<ConfigOptionVectorBase *>(opt);
        const auto *opt_vec_src = static_cast<const ConfigOptionVectorBase *>(source_opt);

        if (opt_vec_src->size() == 0)
            continue;

        if (filament_options_with_variant.count(opt_key)) {
            // Write each variant value into the contiguous variant strip for this filament.
            for (size_t v = 0; v < variant_count; ++v) {
                size_t dst_i = variant_write_offset + v;
                size_t src_i = std::min(v, opt_vec_src->size() - 1);
                opt_vec_dst->set_at(opt_vec_src, dst_i, src_i);
            }
        } else {
            opt_vec_dst->set_at(opt_vec_src, filament_idx_0based, 0);
        }
    }
}

void rebuild_flush_matrix(DynamicPrintConfig &config, size_t n_filaments)
{
    size_t nozzle_nums = 1;
    if (auto *fm = config.option<ConfigOptionFloats>("flush_multiplier")) {
        if (!fm->values.empty())
            nozzle_nums = fm->values.size();
    } else if (auto *nd = config.option<ConfigOptionFloatsNullable>("nozzle_diameter")) {
        if (!nd->values.empty())
            nozzle_nums = nd->values.size();
    }

    auto *flush_multi = config.option<ConfigOptionFloats>("flush_multiplier", true);
    flush_multi->values.resize(nozzle_nums, 1.f);
    auto *flush_multi_fast = config.option<ConfigOptionFloats>("flush_multiplier_fast", true);
    flush_multi_fast->values.resize(nozzle_nums, 1.2f);

    auto *flush_vec = config.option<ConfigOptionFloats>("flush_volumes_vector", true);
    while (flush_vec->values.size() < 2 * n_filaments) {
        flush_vec->values.push_back(flush_vec->values.size() > 1 ? flush_vec->values[0] : 140.);
        flush_vec->values.push_back(flush_vec->values.size() > 1 ? flush_vec->values[1] : 140.);
    }
    while (flush_vec->values.size() > 2 * n_filaments) {
        flush_vec->values.pop_back();
        flush_vec->values.pop_back();
    }

    const auto *colors_opt = config.option<ConfigOptionStrings>("filament_colour");
    const auto *ids_opt    = config.option<ConfigOptionStrings>("filament_ids");
    auto *is_support_opt   = config.option<ConfigOptionBools>("filament_is_support", true);
    if (is_support_opt->values.size() < n_filaments)
        is_support_opt->values.resize(n_filaments, 0);

    std::vector<int> nozzle_flush_dataset(nozzle_nums, 0);
    if (auto *opt = config.option<ConfigOptionIntsNullable>("nozzle_flush_dataset")) {
        for (size_t i = 0; i < nozzle_nums && i < opt->values.size(); ++i)
            nozzle_flush_dataset[i] = opt->values[i];
    }

    int min_flush = 0;
    if (auto *nv = config.option<ConfigOptionFloatsNullable>("nozzle_volume"); nv && !nv->values.empty())
        min_flush = (int) nv->values.front();

    std::vector<double> matrix(n_filaments * n_filaments * nozzle_nums, 0.0);
    for (size_t nozzle_id = 0; nozzle_id < nozzle_nums; ++nozzle_id) {
        std::vector<double> mtx(n_filaments * n_filaments, 0.0);
        FlushVolCalculator calculator(min_flush, g_max_flush_volume, nozzle_flush_dataset[nozzle_id]);
        for (size_t from = 0; from < n_filaments; ++from) {
            unsigned char from_rgba[4] = {255, 255, 255, 255};
            if (colors_opt && from < colors_opt->values.size()) {
                if (!parse_filament_color_rgba(colors_opt->values[from], from_rgba)) {
                    BOOST_LOG_TRIVIAL(warning) << "filament remap: failed to parse from_color '" << colors_opt->values[from] << "', defaulting to white";
                }
            }
            bool is_from_support = is_support_opt->get_at(from);
            std::string from_id = (ids_opt && from < ids_opt->values.size()) ? ids_opt->values[from] : std::string();

            for (size_t to = 0; to < n_filaments; ++to) {
                if (from == to) {
                    mtx[n_filaments * from + to] = 0.0;
                    continue;
                }
                bool is_to_support = is_support_opt->get_at(to);
                int  flushing_volume = 0;
                if (is_to_support) {
                    flushing_volume = g_flush_volume_to_support;
                } else {
                    unsigned char to_rgba[4] = {255, 255, 255, 255};
                    if (colors_opt && to < colors_opt->values.size()) {
                        if (!parse_filament_color_rgba(colors_opt->values[to], to_rgba)) {
                            BOOST_LOG_TRIVIAL(warning) << "filament remap: failed to parse to_color '" << colors_opt->values[to] << "', defaulting to white";
                        }
                    }
                    std::string to_id = (ids_opt && to < ids_opt->values.size()) ? ids_opt->values[to] : std::string();
                    flushing_volume = calculator.calc_flush_vol(from_id, to_id,
                                                                from_rgba[3], from_rgba[0], from_rgba[1], from_rgba[2],
                                                                to_rgba[3], to_rgba[0], to_rgba[1], to_rgba[2]);
                    if (is_from_support)
                        flushing_volume = std::max(g_min_flush_volume_from_support, flushing_volume);
                }
                // Fallback when calculator returns tiny values: use vector defaults.
                if (flushing_volume <= 0 && from < n_filaments && to < n_filaments)
                    flushing_volume = (int) (flush_vec->values[2 * from] + flush_vec->values[2 * to + 1]);
                mtx[n_filaments * from + to] = flushing_volume;
            }
        }
        set_flush_volumes_matrix(matrix, mtx, nozzle_id, nozzle_nums);
    }
    config.option<ConfigOptionFloats>("flush_volumes_matrix", true)->values = std::move(matrix);
}

} // namespace

bool parse_filament_color_rgba(const std::string &color, unsigned char rgba[4])
{
    rgba[0] = rgba[1] = rgba[2] = 255;
    rgba[3] = 255;
    std::string s = boost::trim_copy(color);
    if (s.empty())
        return false;
    if (s[0] == '#')
        s = s.substr(1);
    if (s.size() != 6 && s.size() != 8)
        return false;
    auto byte_at = [&](size_t i) -> int {
        int hi = hex_nibble(s[i]);
        int lo = hex_nibble(s[i + 1]);
        if (hi < 0 || lo < 0) return -1;
        return (hi << 4) | lo;
    };
    if (s.size() == 6) {
        int r = byte_at(0), g = byte_at(2), b = byte_at(4);
        if (r < 0 || g < 0 || b < 0) return false;
        rgba[0] = (unsigned char) r;
        rgba[1] = (unsigned char) g;
        rgba[2] = (unsigned char) b;
        rgba[3] = 255;
        return true;
    }
    int r = byte_at(0), g = byte_at(2), b = byte_at(4), a = byte_at(6);
    if (r < 0 || g < 0 || b < 0 || a < 0) return false;
    // Parse 8-character hex as #RRGGBBAA (RGBA format)
    rgba[0] = (unsigned char) r;
    rgba[1] = (unsigned char) g;
    rgba[2] = (unsigned char) b;
    rgba[3] = (unsigned char) a;
    return true;
}

FilamentRemapResult validate_filament_remap_input(const FilamentRemapInput &input, size_t n_old)
{
    FilamentRemapResult r;
    const size_t n_new = input.types.size();
    if (n_new == 0) {
        r.error = "new filament list is empty";
        return r;
    }
    if (input.colors.size() != n_new || input.is_mixed.size() != n_new ||
        input.mixed_components.size() != n_new || input.mixed_ratios.size() != n_new ||
        input.mixed_gradient.size() != n_new) {
        r.error = "all --new_filament_* arrays must have the same length";
        return r;
    }
    if (input.old_to_new_map.size() != n_old) {
        r.error = "old_to_new_filament_map length must equal old filament count";
        return r;
    }
    if (input.filament_presets.size() != n_new || input.filament_preset_names.size() != n_new ||
        input.filament_ids.size() != n_new) {
        r.error = "resolved filament presets size mismatch";
        return r;
    }

    for (size_t i = 0; i < n_old; ++i) {
        int v = input.old_to_new_map[i];
        if (v < 1 || (size_t) v > n_new) {
            r.error = "old_to_new_filament_map contains out-of-range new id " + std::to_string(v);
            return r;
        }
    }

    // Existing mixed-filament integrity checks assume all physical slots occupy
    // 1..N_physical contiguously, with virtual mixed slots appended after them.
    bool seen_mixed = false;
    size_t num_physical = 0;
    for (size_t i = 0; i < n_new; ++i) {
        if (input.types[i].empty()) {
            r.error = "filament type for new slot " + std::to_string(i + 1) + " is empty";
            return r;
        }
        if (!input.is_mixed[i]) {
            if (seen_mixed) {
                r.error = "physical filaments must occupy contiguous leading slots before mixed filaments";
                return r;
            }
            ++num_physical;
            if (!input.mixed_components[i].empty() || !input.mixed_ratios[i].empty()) {
                r.error = "physical filament " + std::to_string(i + 1) + " must not have mixed components/ratios";
                return r;
            }
            if (input.mixed_gradient[i]) {
                r.error = "physical filament " + std::to_string(i + 1) + " cannot enable mixed gradient";
                return r;
            }
            continue;
        }
        seen_mixed = true;
        auto comps = parse_mixed_components(input.mixed_components[i]);
        if (comps.size() < 2) {
            r.error = "mixed filament " + std::to_string(i + 1) + " needs at least 2 components";
            return r;
        }
        for (unsigned int c : comps) {
            if (c < 1 || c > num_physical) {
                r.error = "mixed filament " + std::to_string(i + 1) + " has invalid component " + std::to_string(c) +
                          " (physical range is 1.." + std::to_string(num_physical) + ")";
                return r;
            }
            if (input.is_mixed[c - 1]) {
                r.error = "mixed filament " + std::to_string(i + 1) + " component " + std::to_string(c) +
                          " must be a physical filament";
                return r;
            }
        }
        const std::string &base_type = input.types[comps[0] - 1];
        for (unsigned int c : comps) {
            if (!boost::iequals(input.types[c - 1], base_type)) {
                r.error = "mixed filament " + std::to_string(i + 1) + " components have inconsistent filament_type";
                return r;
            }
        }
        auto ratios = parse_mixed_ratios(input.mixed_ratios[i], comps.size());
        if (ratios.size() != comps.size()) {
            r.error = "mixed filament " + std::to_string(i + 1) + " ratios count mismatch";
            return r;
        }
    }

    // Prepare gradient ranges for validator (default linear range when gradient enabled).
    std::vector<std::string> grad_ranges(n_new);
    for (size_t i = 0; i < n_new; ++i)
        if (input.is_mixed[i] && input.mixed_gradient[i])
            grad_ranges[i] = "0.10,0.90";
    auto integrity = validate_mixed_filament_params(input.is_mixed, input.mixed_components, input.mixed_ratios,
                                                    input.mixed_gradient, grad_ranges,
                                                    std::vector<std::string>(n_new));
    if (!integrity.empty()) {
        r.error = integrity.begin()->second;
        return r;
    }

    r.success = true;
    return r;
}

FilamentRemapResult remap_project_filaments(Model &model, DynamicPrintConfig &config, const FilamentRemapInput &input)
{
    size_t n_old = 0;
    if (auto *opt = config.option<ConfigOptionStrings>("filament_colour"))
        n_old = opt->values.size();
    if (n_old == 0) {
        if (auto *opt = config.option<ConfigOptionStrings>("filament_type"))
            n_old = opt->values.size();
    }

    FilamentRemapResult vr = validate_filament_remap_input(input, n_old);
    if (!vr.success)
        return vr;

    const size_t n_new = input.types.size();
    BOOST_LOG_TRIVIAL(info) << "remap_project_filaments: N_old=" << n_old << " N_new=" << n_new;

    // 1) Remap model references (1-based)
    for (ModelObject *mo : model.objects) {
        if (!mo) continue;
        remap_model_config(mo->config, input.old_to_new_map);
        for (ModelVolume *mv : mo->volumes) {
            if (!mv) continue;
            remap_model_config(mv->config, input.old_to_new_map);
            if (!mv->mmu_segmentation_facets.empty()) {
                mv->mmu_segmentation_facets.remap_filament_ids(*mv, (EnforcerBlockerType) n_new, input.old_to_new_map);
                mv->mmuseg_extruders.clear();
                mv->mmuseg_ts = 0;
            }
        }
        // Layer ranges may carry extruder overrides in config.
        for (auto &range : mo->layer_config_ranges)
            remap_model_config(range.second, input.old_to_new_map);
    }

    for (auto &kv : model.plates_custom_gcodes)
        remap_custom_gcodes(kv.second, input.old_to_new_map);

    // Support filament keys in print config (global)
    for (const char *key : {"support_filament", "support_interface_filament"}) {
        if (!config.has(key)) continue;
        int old_v = config.opt_int(key);
        if (old_v <= 0) continue;
        if (old_v > (int) input.old_to_new_map.size()) {
            config.erase(key);
        } else {
            int new_v = input.old_to_new_map[old_v - 1];
            if (new_v <= 0)
                config.erase(key);
            else
                config.set_key_value(key, new ConfigOptionInt(new_v));
        }
    }

    // 2) Rebuild filament arrays from presets
    std::vector<size_t> variant_offsets(n_new, 0);
    std::vector<size_t> variant_counts(n_new, 1);
    size_t cursor = 0;
    for (size_t i = 0; i < n_new; ++i) {
        variant_offsets[i] = cursor;
        const auto *vopt = input.filament_presets[i].option<ConfigOptionStrings>("filament_extruder_variant");
        size_t nvar = (vopt && !vopt->values.empty()) ? vopt->values.size() : 1;
        variant_counts[i] = nvar;
        cursor += nvar;
    }

    // Seed variant option vectors with a single placeholder so set_at can grow them.
    // (ConfigOptionVector::set_at requires a non-empty values array.)
    {
        const auto &defaults = FullPrintConfig::defaults();
        for (const std::string &key : filament_options_with_variant) {
            ConfigOption *opt = config.option(key, true);
            if (opt == nullptr || !opt->is_vector())
                continue;
            auto *vec = static_cast<ConfigOptionVectorBase *>(opt);
            vec->clear();
            const ConfigOption *src = nullptr;
            for (size_t i = 0; i < n_new && src == nullptr; ++i) {
                const ConfigOption *cand = input.filament_presets[i].option(key);
                if (cand && cand->is_vector() && static_cast<const ConfigOptionVectorBase *>(cand)->size() > 0)
                    src = cand;
            }
            if (src == nullptr)
                src = defaults.option(key);
            if (src)
                vec->resize(1, src);
        }
    }

    // Resize non-variant filament colour/type metadata first
    ensure_string_array(config, "filament_colour", n_new, "#FFFFFF");
    ensure_string_array(config, "filament_multi_colour", n_new, "#FFFFFF");
    ensure_string_array(config, "filament_colour_type", n_new, "2");
    ensure_string_array(config, "filament_type", n_new, "PLA");
    ensure_string_array(config, "filament_ids", n_new, "");
    ensure_string_array(config, "filament_settings_id", n_new, "");
    ensure_bool_array(config, "filament_is_mixed", n_new, false);
    ensure_string_array(config, "filament_mixed_components", n_new, "");
    ensure_string_array(config, "filament_mixed_sublayer_ratios", n_new, "");
    ensure_bool_array(config, "filament_mixed_gradient", n_new, false);
    ensure_string_array(config, "filament_mixed_gradient_range", n_new, "");
    ensure_string_array(config, "filament_mixed_gradient_curve", n_new, "");
    ensure_bool_array(config, "filament_mixed_gradient_per_part", n_new, false);
    ensure_int_array(config, "filament_map", n_new, 1);
    ensure_int_array(config, "filament_map_2", n_new, 1);
    ensure_int_array(config, "filament_nozzle_map", n_new, 0);
    ensure_int_array(config, "filament_volume_map", n_new, (int) NozzleVolumeType::nvtStandard);

    for (size_t i = 0; i < n_new; ++i) {
        apply_filament_preset_to_config(config, input.filament_presets[i], i, variant_offsets[i], variant_counts[i]);

        config.option<ConfigOptionStrings>("filament_colour")->values[i]       = input.colors[i];
        config.option<ConfigOptionStrings>("filament_multi_colour")->values[i] = input.colors[i];
        config.option<ConfigOptionStrings>("filament_colour_type")->values[i]  = "2";
        config.option<ConfigOptionStrings>("filament_type")->values[i]         = input.types[i];
        config.option<ConfigOptionStrings>("filament_ids")->values[i]          = input.filament_ids[i];
        config.option<ConfigOptionStrings>("filament_settings_id")->values[i]  = input.filament_preset_names[i];

        config.option<ConfigOptionBools>("filament_is_mixed")->values[i] = input.is_mixed[i] ? 1 : 0;
        if (input.is_mixed[i]) {
            auto comps  = parse_mixed_components(input.mixed_components[i]);
            auto ratios = parse_mixed_ratios(input.mixed_ratios[i], comps.size());
            std::ostringstream comp_ss, ratio_ss;
            for (size_t k = 0; k < comps.size(); ++k) {
                if (k) { comp_ss << ","; ratio_ss << ","; }
                comp_ss << comps[k];
                ratio_ss << std::fixed << std::setprecision(4) << ratios[k];
            }
            config.option<ConfigOptionStrings>("filament_mixed_components")->values[i]      = comp_ss.str();
            config.option<ConfigOptionStrings>("filament_mixed_sublayer_ratios")->values[i] = ratio_ss.str();
            config.option<ConfigOptionBools>("filament_mixed_gradient")->values[i]         = input.mixed_gradient[i] ? 1 : 0;
            if (input.mixed_gradient[i])
                config.option<ConfigOptionStrings>("filament_mixed_gradient_range")->values[i] = "0.10,0.90";
            else
                config.option<ConfigOptionStrings>("filament_mixed_gradient_range")->values[i] = "";
            config.option<ConfigOptionStrings>("filament_mixed_gradient_curve")->values[i] = "";
            config.option<ConfigOptionBools>("filament_mixed_gradient_per_part")->values[i] = 0;
        } else {
            config.option<ConfigOptionStrings>("filament_mixed_components")->values[i].clear();
            config.option<ConfigOptionStrings>("filament_mixed_sublayer_ratios")->values[i].clear();
            config.option<ConfigOptionBools>("filament_mixed_gradient")->values[i] = 0;
            config.option<ConfigOptionStrings>("filament_mixed_gradient_range")->values[i].clear();
            config.option<ConfigOptionStrings>("filament_mixed_gradient_curve")->values[i].clear();
            config.option<ConfigOptionBools>("filament_mixed_gradient_per_part")->values[i] = 0;
        }
    }

    // N_new < N_old shrink safety: apply_filament_preset_to_config above writes indices [0, n_new)
    // through set_at, which grows a vector but never shrinks it. The colour/type/map/mixed arrays are
    // rebuilt to n_new by the ensure_*_array calls, but every other per-filament array (nozzle
    // temperature, flow ratio, retraction, ...) keeps the original N_old length carried in from the
    // 3MF. That leaves the config with filament arrays of mixed sizes (some n_new, some n_old), so a
    // later slicing step that iterates one array and indexes another reads out of bounds and crashes
    // (this is why shrinking maps crash while growing maps do not). Resize every non-variant
    // per-filament option to exactly n_new, mirroring Preset::normalize().
    {
        const auto &defaults = FullPrintConfig::defaults();
        for (const std::string &key : Preset::filament_options()) {
            if (key == "compatible_prints" || key == "compatible_printers")
                continue;
            if (filament_options_with_variant.count(key))
                continue;
            if (filament_dev_options.count(key))
                continue;
            ConfigOption *opt = config.option(key, false);
            if (opt != nullptr && opt->is_vector())
                static_cast<ConfigOptionVectorBase *>(opt)->resize(n_new, defaults.option(key));
        }
    }

    // Same shrink hazard for the accumulator arrays that apply_filament_preset_to_config only grows
    // (never shrinks): compatible_machine_expression_group is laid out [process, filament 1..N] and
    // compatible_process_expression_group as [filament 0..N-1]. When N_new < N_old they keep a stale
    // tail describing dropped filaments. Truncate that tail so their length tracks the new count.
    // Truncate-only: the apply step already guaranteed the needed entries exist, so we never grow here.
    if (auto *opt = config.option<ConfigOptionStrings>("compatible_machine_expression_group")) {
        if (opt->values.size() > n_new + 1)
            opt->values.resize(n_new + 1);
    }
    if (auto *opt = config.option<ConfigOptionStrings>("compatible_process_expression_group")) {
        if (opt->values.size() > n_new)
            opt->values.resize(n_new);
    }

    // Rebuild filament_self_index aligned with variants
    {
        auto *self_opt = config.option<ConfigOptionInts>("filament_self_index", true);
        self_opt->values.clear();
        auto *var_opt = config.option<ConfigOptionStrings>("filament_extruder_variant", true);
        // Ensure variant array length matches concatenated presets
        size_t expected = cursor;
        if (var_opt->values.size() < expected) {
            // Fill missing from presets
            var_opt->values.clear();
            for (size_t i = 0; i < n_new; ++i) {
                const auto *src = input.filament_presets[i].option<ConfigOptionStrings>("filament_extruder_variant");
                if (src && !src->values.empty()) {
                    for (const auto &v : src->values)
                        var_opt->values.push_back(v);
                } else {
                    var_opt->values.push_back("Direct Drive Standard");
                }
            }
        }
        self_opt->values.resize(var_opt->values.size(), 1);
        size_t pos = 0;
        for (size_t i = 0; i < n_new; ++i) {
            for (size_t v = 0; v < variant_counts[i] && pos < self_opt->values.size(); ++v, ++pos)
                self_opt->values[pos] = (int) (i + 1);
        }
    }

    // Final mixed validation against written config
    {
        auto *is_mixed = config.option<ConfigOptionBools>("filament_is_mixed");
        auto *comps    = config.option<ConfigOptionStrings>("filament_mixed_components");
        auto *ratios   = config.option<ConfigOptionStrings>("filament_mixed_sublayer_ratios");
        auto *grads    = config.option<ConfigOptionBools>("filament_mixed_gradient");
        auto *ranges   = config.option<ConfigOptionStrings>("filament_mixed_gradient_range");
        auto *curves   = config.option<ConfigOptionStrings>("filament_mixed_gradient_curve");
        auto errs = validate_mixed_filament_params(is_mixed->values, comps->values, ratios->values,
                                                   grads->values, ranges->values, curves->values);
        if (!errs.empty()) {
            FilamentRemapResult r;
            r.error = errs.begin()->second;
            return r;
        }
        auto type_errs = check_mixed_filament_type_consistency(is_mixed->values, comps->values,
                                                              config.option<ConfigOptionStrings>("filament_type")->values);
        if (!type_errs.empty()) {
            FilamentRemapResult r;
            r.error = "mixed filament type inconsistency at slot " + std::to_string(type_errs.front() + 1);
            return r;
        }
    }

    rebuild_flush_matrix(config, n_new);
    FilamentRemapResult ok;
    ok.success = true;
    BOOST_LOG_TRIVIAL(info) << "remap_project_filaments: completed successfully";
    return ok;
}

} // namespace Slic3r
