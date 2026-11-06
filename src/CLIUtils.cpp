#include "CLIUtils.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <map>
#include <sstream>

#include <boost/algorithm/string/trim.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/nowide/fstream.hpp>

#include "nlohmann/json.hpp"

#include "libslic3r/Config.hpp"
#include "libslic3r/LocalesUtils.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Log.hpp"

namespace Slic3r {

namespace {

// 比较层高的容差，与 PresetBundle.cpp 中 PreferedPrintProfileMatch 一致
const double LAYER_HEIGHT_EPSILON = 0.0005;

// 没有 print_extruder_variant 的配置（老 3mf），其值视为 1 号挤出机的这个 variant
const char *const DEFAULT_EXTRUDER_VARIANT = "Direct Drive Standard";

// 新机型没有同质量的工艺时，依次尝试的备选质量（原质量由调用方先试）
// 名称取自 BBL 各机型系统工艺的质量名，未列出的质量没有备选
const std::vector<std::string> &quality_fallbacks(const std::string &quality)
{
    static const std::map<std::string, std::vector<std::string>> fallbacks = {
        {"Balanced Quality", {"Standard"}},
        {"Optimal",          {"Standard", "High Quality"}},
        {"Fine",             {"High Quality"}},
        {"Extra Fine",       {"High Quality"}},
        {"Draft",            {"Standard"}},
        {"Extra Draft",      {"Standard"}},
        {"Strength",         {"Standard"}},
    };
    static const std::vector<std::string> empty;
    auto iter = fallbacks.find(quality);
    return iter == fallbacks.end() ? empty : iter->second;
}

std::string json_string(const nlohmann::json &j, const char *key)
{
    auto iter = j.find(key);
    return (iter != j.end() && iter->is_string()) ? iter->get<std::string>() : std::string();
}

// 读取工艺的 print_extruder_variant / print_extruder_id，保证每个 variant 都有一个 id
// id 原样返回，即使对该机型是错的（如老 H3C/P3C 3mf 的 [2, 2, 2]），由调用方通过 has_multiple_extruder_ids() 决定是否使用
// 没有 print_extruder_variant（老 3mf）时视为一个 DEFAULT_EXTRUDER_VARIANT
// 没有 print_extruder_id，或数量与 variant 不一致时，全部视为 1 号挤出机
void get_print_extruder_variants(const DynamicPrintConfig &config, std::vector<std::string> &variants, std::vector<int> &ids)
{
    const ConfigOptionStrings *variant_opt = config.option<ConfigOptionStrings>("print_extruder_variant");
    const ConfigOptionInts    *id_opt      = config.option<ConfigOptionInts>("print_extruder_id");
    if (variant_opt && !variant_opt->values.empty())
        variants = variant_opt->values;
    else
        variants = {DEFAULT_EXTRUDER_VARIANT};
    if (id_opt && id_opt->values.size() == variants.size())
        ids = id_opt->values;
    else {
        if (id_opt)
            BOOST_LOG_TRIVIAL(warning) << boost::format("print_extruder_id size %1% not equal to print_extruder_variant size %2%, regard all as extruder 1")
                % id_opt->values.size() % variants.size();
        ids.assign(variants.size(), 1);
    }
}

template<class T>
std::string join_values(const T &values)
{
    std::ostringstream ss;
    bool first = true;
    for (const auto &value : values) {
        if (!first)
            ss << ", ";
        ss << value;
        first = false;
    }
    return ss.str();
}

} // namespace

std::string extract_process_quality_name(const std::string &process_name)
{
    size_t start = process_name.find("mm ");
    if (start == std::string::npos)
        return std::string();
    start += 3;
    size_t      end     = process_name.find(" @", start);
    std::string quality = process_name.substr(start, end == std::string::npos ? std::string::npos : end - start);
    boost::algorithm::trim(quality);
    return quality;
}

ProcessMatchResult match_process_by_layer_height(const std::string &process_full_dir,
                                                 const std::string &new_printer_system_name,
                                                 double             target_layer_height,
                                                 const std::string &old_process_system_name,
                                                 const std::string &new_default_process_name)
{
    ProcessMatchResult result;
    BOOST_LOG_TRIVIAL(info) << boost::format("match process: dir %1%, new printer %2%, layer height %3%, old process %4%, new default process %5%")
        % process_full_dir % new_printer_system_name % target_layer_height % old_process_system_name % new_default_process_name;

    if (!boost::filesystem::is_directory(process_full_dir)) {
        result.reason = "no_process_full";
        BOOST_LOG_TRIVIAL(warning) << boost::format("match process: %1% is not a directory") % process_full_dir;
        return result;
    }

    size_t compatible_count = 0;
    try {
        for (const auto &entry : boost::filesystem::directory_iterator(process_full_dir)) {
            if (!boost::filesystem::is_regular_file(entry) || entry.path().extension() != ".json")
                continue;

            std::string content;
            {
                boost::nowide::ifstream ifs(entry.path().string());
                if (!ifs.is_open())
                    continue;
                content.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
            }
            // 大部分文件不是新机型的，解析前先过滤
            if (content.find(new_printer_system_name) == std::string::npos)
                continue;

            try {
                nlohmann::json j = nlohmann::json::parse(content);
                // 只要可选的系统工艺，排除 fdm_process_common 这类基础预设
                if (json_string(j, "type") != "process" || json_string(j, "from") != "system" || json_string(j, "instantiation") != "true")
                    continue;

                auto printers_iter = j.find("compatible_printers");
                if (printers_iter == j.end() || !printers_iter->is_array())
                    continue;
                bool compatible = std::any_of(printers_iter->begin(), printers_iter->end(), [&new_printer_system_name](const nlohmann::json &printer) {
                    return printer.is_string() && printer.get<std::string>() == new_printer_system_name;
                });
                if (!compatible)
                    continue;
                compatible_count++;

                std::string layer_height_str = json_string(j, "layer_height");
                if (layer_height_str.empty())
                    continue;
                double layer_height = string_to_double_decimal_point(layer_height_str);
                if (std::abs(layer_height - target_layer_height) >= LAYER_HEIGHT_EPSILON)
                    continue;

                // 调用方按 <process_full_dir>/<name>.json 加载，所以用文件名
                result.candidates.push_back(entry.path().stem().string());
            }
            catch (const std::exception &err) {
                BOOST_LOG_TRIVIAL(warning) << boost::format("match process: parse %1% failed, reason = %2%") % entry.path().string() % err.what();
            }
        }
    }
    catch (const boost::filesystem::filesystem_error &err) {
        BOOST_LOG_TRIVIAL(warning) << boost::format("match process: scan %1% failed, reason = %2%") % process_full_dir % err.what();
    }

    std::sort(result.candidates.begin(), result.candidates.end());
    BOOST_LOG_TRIVIAL(info) << boost::format("match process: %1% processes compatible with %2%, candidates with the same layer height: [%3%]")
        % compatible_count % new_printer_system_name % join_values(result.candidates);

    auto choose = [&result](const std::string &name, const char *reason) {
        result.name   = name;
        result.reason = reason;
        BOOST_LOG_TRIVIAL(info) << boost::format("match process: choose %1%, reason %2%") % name % reason;
        return result;
    };

    if (result.candidates.empty()) {
        result.reason = (compatible_count == 0) ? "no_compatible_process" : "no_layer_height_match";
        BOOST_LOG_TRIVIAL(warning) << boost::format("match process: no match, reason %1%") % result.reason;
        return result;
    }
    if (result.candidates.size() == 1)
        return choose(result.candidates[0], "unique");

    // 1. 同质量，或其备选质量
    std::string old_quality = extract_process_quality_name(old_process_system_name);
    if (!old_quality.empty()) {
        std::vector<std::string> qualities{old_quality};
        for (const std::string &quality : quality_fallbacks(old_quality))
            qualities.push_back(quality);
        for (const std::string &quality : qualities) {
            for (const std::string &candidate : result.candidates) {
                if (extract_process_quality_name(candidate) == quality)
                    return choose(candidate, "quality_match");
            }
        }
    }

    // 2. 新机型的默认工艺
    if (!new_default_process_name.empty() &&
        std::find(result.candidates.begin(), result.candidates.end(), new_default_process_name) != result.candidates.end())
        return choose(new_default_process_name, "default_profile");

    // 3. Standard 质量
    for (const std::string &candidate : result.candidates) {
        if (extract_process_quality_name(candidate) == "Standard")
            return choose(candidate, "standard");
    }

    // 4. 按名称排序的第一个
    return choose(result.candidates.front(), "alphabetical");
}

void compute_variant_index(const DynamicPrintConfig &full_config,
                           const DynamicPrintConfig &new_config,
                           const std::string        &id_name,
                           const std::string        &variant_name,
                           std::vector<int>         &new_index,
                           bool                     &count_changed)
{
    auto curr_variant_opt = dynamic_cast<const ConfigOptionStrings *>(full_config.option(variant_name));
    auto new_variant_opt  = dynamic_cast<const ConfigOptionStrings *>(new_config.option(variant_name));
    auto curr_id_opt      = dynamic_cast<const ConfigOptionInts *>(full_config.option(id_name));
    auto new_id_opt       = dynamic_cast<const ConfigOptionInts *>(new_config.option(id_name));
    if (!new_variant_opt || !new_id_opt) {
        BOOST_LOG_TRIVIAL(error) << boost::format("can not get %1% or %2% from new config") % variant_name % id_name;
        count_changed = false;
        return;
    }

    size_t                   new_variant_count = new_variant_opt->size();
    std::vector<std::string> curr_variants;
    std::vector<int>         curr_ids;
    if (curr_variant_opt && curr_id_opt) {
        if (curr_variant_opt->size() != curr_id_opt->size()) {
            BOOST_LOG_TRIVIAL(error) << boost::format("%1%'s size %2% not equal to %3%'s size %4%") % variant_name % curr_variant_opt->size() % id_name % curr_id_opt->size();
            new_index.assign(new_variant_count, -1);
            count_changed = false;
            return;
        }
        curr_variants = curr_variant_opt->values;
        curr_ids      = curr_id_opt->values;
        count_changed = (curr_variants.size() != new_variant_count);
    }
    else
        count_changed = (new_variant_count != 1);

    // 没有 variant 的老 3mf：其值属于默认 variant
    if (curr_variants.empty()) {
        curr_variants = {DEFAULT_EXTRUDER_VARIANT};
        curr_ids      = {1};
    }
    // 这条路径保持原有行为，严格按名称 + id 匹配
    // 3mf 的 id 错误时结果全是 -1，但 variant 数不变时不会用到这个下标，见 CLIUtils.hpp
    new_index = compute_variant_index_by_name(curr_variants, curr_ids, new_variant_opt->values, new_id_opt->values, true);
}

std::set<std::string> compute_process_switch_keep_keys(const std::string           &different_settings,
                                                       bool                         skip_modified_gcodes,
                                                       const std::set<std::string> &gcodes_keys)
{
    // compatible_printers / print_settings_id / inherits：预设管理用的 key，按新工艺重建
    // print_extruder_id / print_extruder_variant：variant 结构必须用新工艺的，3mf 里的值（包括老 H3C/P3C 3mf 的错误 id）不保留
    static const std::set<std::string> never_keep_keys = {"compatible_printers", "print_settings_id", "inherits", "print_extruder_id", "print_extruder_variant"};

    std::vector<std::string> different_keys;
    if (!different_settings.empty())
        unescape_strings_cstyle(different_settings, different_keys);

    std::set<std::string> keep_keys;
    for (const std::string &key : different_keys) {
        if (key.empty() || never_keep_keys.count(key) > 0)
            continue;
        if (skip_modified_gcodes && gcodes_keys.count(key) > 0) {
            BOOST_LOG_TRIVIAL(info) << boost::format("gcodes %1% modified, reset to default.") % key;
            continue;
        }
        keep_keys.insert(key);
    }
    // 擦料塔尺寸总是保留：3mf 里对象是围绕原尺寸的擦料塔摆放的，换成新工艺的尺寸（如 H3C 宽 60，X1C 宽 35）可能压到对象，切片报 G-code 冲突
    keep_keys.insert({"prime_tower_width", "prime_tower_brim_width"});
    BOOST_LOG_TRIVIAL(info) << boost::format("process switch: different_settings %1%, keep keys [%2%]") % different_settings % join_values(keep_keys);
    return keep_keys;
}

int merge_process_to_new_preset(DynamicPrintConfig          &full_config,
                                const DynamicPrintConfig    *old_system_config,
                                const DynamicPrintConfig    &new_system_config,
                                const std::set<std::string> &keep_keys,
                                std::vector<std::string>    &kept_keys,
                                std::vector<int>            &variant_index)
{
    // 预设管理用的 key，不从新系统工艺复制，与 BambuStudio.cpp 的 update_full_config 一致
    static const std::set<std::string> skip_keys = {"compatible_prints", "compatible_printers", "model_id", "inherits", "dev_model_name",
                                                    "name", "from", "type", "version", "setting_id", "instantiation"};
    kept_keys.clear();
    variant_index.clear();
    // 合并结果先放这里，全部成功后再应用到 full_config，失败时 full_config 不变
    DynamicPrintConfig merged_config;

    try {
        // 3mf（full）、新系统工艺（new）、旧系统工艺（old_system）的 variant 和 id
        // 3mf 的 id 可能是错的，是否使用由下面的 has_multiple_extruder_ids() 决定
        std::vector<std::string> full_variants, new_variants, old_system_variants;
        std::vector<int>         full_ids, new_ids, old_system_ids;
        get_print_extruder_variants(full_config, full_variants, full_ids);
        get_print_extruder_variants(new_system_config, new_variants, new_ids);
        // 3mf 打印机的主挤出机，如 H2C/H2D 为 2
        // 多挤出机切到单挤出机时同名 variant 有多个，取主挤出机的，与 GUI 切换打印机时一致（Tab::remap_variant_cache）
        const ConfigOptionInt *master_opt         = full_config.option<ConfigOptionInt>("master_extruder_id");
        int                    master_extruder_id = master_opt ? master_opt->value : 1;

        // 映射 1：新工艺的每个 variant 对应 3mf 的哪个 variant
        // 两边都是多挤出机才同时按 id 匹配，否则只按名称匹配，各种组合的结果见 CLIUtils.hpp
        // 所以老 H3C/P3C 3mf 的错误 id（[2, 2, 2]）不影响匹配
        bool match_id = has_multiple_extruder_ids(full_ids) && has_multiple_extruder_ids(new_ids);
        variant_index = compute_variant_index_by_name(full_variants, full_ids, new_variants, new_ids, match_id, master_extruder_id);

        // 映射 2：3mf 的每个 variant 对应旧系统工艺的哪个 variant，用来判断用户改了哪些 variant
        // 两者虽是同一机型，id 仍可能不同，如老 H3C/P3C 3mf 是 [2, 2, 2]，修复后的预设是 [1, 1, 1]
        // 所以规则与映射 1 相同，否则会全部匹配失败，每个 variant 都被当成改过
        // 按名称而不是按位置映射，因为 3mf 保存后系统工艺可能新增了 variant
        bool             old_match_id = false;
        std::vector<int> old_system_index;
        if (old_system_config) {
            get_print_extruder_variants(*old_system_config, old_system_variants, old_system_ids);
            old_match_id     = has_multiple_extruder_ids(old_system_ids) && has_multiple_extruder_ids(full_ids);
            old_system_index = compute_variant_index_by_name(old_system_variants, old_system_ids, full_variants, full_ids, old_match_id, master_extruder_id);
        }
        BOOST_LOG_TRIVIAL(info) << boost::format("process switch: variants [%1%] ids [%2%] -> [%3%] ids [%4%], match_id %5%, master_extruder_id %6%, variant index [%7%]; "
                                                 "old system variants [%8%] ids [%9%], match_id %10%, old system index [%11%]")
            % join_values(full_variants) % join_values(full_ids) % join_values(new_variants) % join_values(new_ids) % match_id % master_extruder_id
            % join_values(variant_index) % join_values(old_system_variants) % join_values(old_system_ids) % old_match_id % join_values(old_system_index);

        for (const t_config_option_key &opt_key : new_system_config.keys()) {
            if (skip_keys.count(opt_key) > 0)
                continue;
            const ConfigOption *new_opt  = new_system_config.option(opt_key);
            const ConfigOption *full_opt = full_config.option(opt_key);

            // 用户没改过（包括 print_extruder_id / print_extruder_variant），或 3mf 里没有：用新系统工艺的值
            if (keep_keys.count(opt_key) == 0 || full_opt == nullptr) {
                if (full_opt == nullptr && full_config.def()->get(opt_key) == nullptr) {
                    BOOST_LOG_TRIVIAL(error) << boost::format("process switch: can not create option %1%") % opt_key;
                    kept_keys.clear();
                    variant_index.clear();
                    return -1;
                }
                merged_config.set_key_value(opt_key, new_opt->clone());
                continue;
            }

            if (print_options_with_variant.count(opt_key) == 0 || !full_opt->is_vector() || !new_opt->is_vector()) {
                // 改过，没有 variant：保留 3mf 的值
                if (*full_opt != *new_opt)
                    kept_keys.push_back(opt_key);
                BOOST_LOG_TRIVIAL(info) << boost::format("process switch: keep key %1%, value %2%") % opt_key % full_opt->serialize();
                continue;
            }

            // 改过，有 variant：先取新值，再逐个 variant 恢复 3mf 的值
            auto *new_vec = static_cast<const ConfigOptionVectorBase *>(new_opt);
            // 预设有问题，值的个数与 variant 数不一致，无法对应
            if (new_vec->size() != new_variants.size()) {
                BOOST_LOG_TRIVIAL(warning) << boost::format("process switch: %1% size %2% not equal to variant count %3% of the new process, use the new value")
                    % opt_key % new_vec->size() % new_variants.size();
                merged_config.set_key_value(opt_key, new_opt->clone());
                continue;
            }

            std::vector<std::string> full_values = static_cast<const ConfigOptionVectorBase *>(full_opt)->vserialize();
            std::vector<std::string> old_system_values;
            const ConfigOption      *old_system_opt = old_system_config ? old_system_config->option(opt_key) : nullptr;
            if (old_system_opt && old_system_opt->is_vector())
                old_system_values = static_cast<const ConfigOptionVectorBase *>(old_system_opt)->vserialize();

            // 新工艺每个 variant 要恢复的 3mf 值下标，-1 表示用新值
            // different_settings 只记录到 key，所以只恢复用户改过的 variant，即 3mf 值与旧系统工艺同一 variant 的值不同
            // 映射不到 3mf 的 variant 用新值，没有旧系统工艺的值可比时视为改过
            std::vector<int> restore_index(new_variants.size(), -1);
            for (size_t i = 0; i < restore_index.size(); i++) {
                int full_index = variant_index[i];
                if (full_index < 0 || full_index >= (int) full_values.size())
                    continue;
                if (!old_system_values.empty()) {
                    int old_index = (full_index < (int) old_system_index.size()) ? old_system_index[full_index] : -1;
                    if (old_index >= 0 && old_index < (int) old_system_values.size() && old_system_values[old_index] == full_values[full_index])
                        continue;
                }
                restore_index[i] = full_index;
            }

            // set_with_restore：先取 new_vec 的值，再把 restore_index[i] >= 0 的位置恢复为 3mf 的值
            ConfigOption *merged_opt = full_opt->clone();
            merged_config.set_key_value(opt_key, merged_opt);
            static_cast<ConfigOptionVectorBase *>(merged_opt)->set_with_restore(new_vec, restore_index, 1);
            if (*merged_opt != *new_opt)
                kept_keys.push_back(opt_key);
            BOOST_LOG_TRIVIAL(info) << boost::format("process switch: keep variant key %1%, restore index [%2%], %3% -> %4%")
                % opt_key % join_values(restore_index) % full_opt->serialize() % merged_opt->serialize();
        }

        // 新工艺没有的 keep_keys：3mf 的值不动，也算保留
        for (const std::string &key : keep_keys) {
            if (!new_system_config.has(key) && full_config.has(key))
                kept_keys.push_back(key);
        }
    }
    catch (const std::exception &err) {
        BOOST_LOG_TRIVIAL(error) << boost::format("process switch: merge failed, reason = %1%") % err.what();
        kept_keys.clear();
        variant_index.clear();
        return -1;
    }

    full_config.apply(merged_config);

    std::sort(kept_keys.begin(), kept_keys.end());
    BOOST_LOG_TRIVIAL(info) << boost::format("process switch: kept keys [%1%]") % join_values(kept_keys);
    return 0;
}

} // namespace Slic3r
