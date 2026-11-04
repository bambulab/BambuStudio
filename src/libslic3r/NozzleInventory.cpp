#include "NozzleInventory.hpp"

#include "AppConfig.hpp"
#include "LocalesUtils.hpp"
#include "Log.hpp"
#include "Preset.hpp"
#include "PresetBundle.hpp"
#include "Utils.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <sstream>

#include <boost/algorithm/string.hpp>

namespace Slic3r {

namespace {

/**
 * \brief The layer that gives the effective nozzles of an extruder
 */
enum class NozzleSource { Preset, History, Project, Machine, User };

/**
 * \brief One write of Project, Machine or User
 *
 * Stamp 0 means the layer is empty.
 */
struct NozzleLayer
{
    NozzleSource               source = NozzleSource::Project;
    std::string                model;
    std::vector<ExtruderSlots> extruders;
    uint64_t                   stamp = 0;
};

/**
 * \brief What resolve needs from the selected printer, copied when it is selected
 */
struct SelectedPrinter
{
    std::string                                model;
    std::string                                history_key;
    std::string                                diameter;
    bool                                       supports_multi_diameter = false; ///< the multi-diameter mode applies
    bool                                       has_rack                = false; ///< an extruder has more than one slot
    std::vector<int>                           slot_counts;   ///< per extruder, at least 1
    std::vector<std::vector<NozzleVolumeType>> default_types; ///< per extruder, per slot

    int extruder_count() const { return int(slot_counts.size()); }
};

const char *source_name(NozzleSource source)
{
    static constexpr std::array<const char *, 5> names = {"Preset", "History", "Project", "Machine", "User"};
    return names[size_t(source)];
}

int installed_count(const ExtruderSlots &slots)
{
    return int(std::count_if(slots.begin(), slots.end(), [](const NozzleSlot &slot) { return !slot.empty(); }));
}

/**
 * \brief Check if the extruder holds a nozzle the selected variant can print with
 */
bool holds_selected_diameter(const ExtruderSlots &slots, const SelectedPrinter &printer)
{
    // On a rack the selected diameter only filters the slots; there any installed nozzle keeps the layer.
    // The multi-diameter mode applies only to a rack, so it never changes the result here.
    if (printer.has_rack)
        return installed_count(slots) > 0;
    return std::any_of(slots.begin(), slots.end(), [&printer](const NozzleSlot &slot) { return slot.diameter == printer.diameter; });
}

/**
 * \brief Cut or pad the slots to the slot count of the extruder; a padded slot is empty
 */
ExtruderSlots fit_slot_count(ExtruderSlots slots, int slot_count)
{
    slots.resize(size_t(slot_count));
    return slots;
}

/**
 * \brief Installed slots of one diameter, one slot per volume type in \p types
 */
ExtruderSlots uniform_slots(const std::string &diameter, const std::vector<NozzleVolumeType> &types)
{
    ExtruderSlots slots;
    slots.reserve(types.size());
    for (NozzleVolumeType type : types)
        slots.push_back({diameter, type, NozzleSlotStatus::Normal});
    return slots;
}

std::string slots_text(const ExtruderSlots &slots)
{
    std::string out;
    for (const NozzleSlot &slot : slots) {
        if (!out.empty())
            out += '|';
        out += slot.empty() ? std::string("-") : slot.diameter + " " + GetNozzleVolumeTypeStr(slot.volume_type);
    }
    return out;
}

/**
 * \brief Text of a layer for logs
 *
 * \param name           layer name in the text
 * \param layer_or_null  the layer; null reads as an empty layer
 */
std::string layer_text(const char *name, const NozzleLayer *layer_or_null)
{
    static const NozzleLayer none;
    const NozzleLayer       &layer = layer_or_null ? *layer_or_null : none;
    if (layer.stamp == 0)
        return std::string(name) + "=none";
    std::string out = std::string(name) + "#" + std::to_string(layer.stamp) + "(" + layer.model + ")=";
    for (size_t eid = 0; eid < layer.extruders.size(); ++eid)
        out += (eid ? "," : "") + slots_text(layer.extruders[eid]);
    return out;
}

SelectedPrinter read_selected_printer(const PresetCollection &printers)
{
    SelectedPrinter printer;
    const Preset   &selected = printers.get_selected_preset();
    const Preset   *base     = printers.get_preset_base(selected);
    const DynamicPrintConfig &config = printers.get_edited_preset().config;
    printer.model                   = config.opt_string("printer_model");
    printer.history_key             = base ? base->name : selected.name;
    printer.diameter                = format_diameter_to_str(string_to_double_decimal_point(config.opt_string("printer_variant")));
    printer.supports_multi_diameter = is_single_ext_multi_nozzle(config);

    const auto  *nozzle_diameters = config.option<ConfigOptionFloatsNullable>("nozzle_diameter");
    const auto  *max_counts       = config.option<ConfigOptionIntsNullable>("extruder_max_nozzle_count");
    const size_t extruder_count   = nozzle_diameters ? nozzle_diameters->values.size() : 1;
    for (size_t eid = 0; eid < extruder_count; ++eid) {
        int slot_count = 1;
        if (max_counts && eid < max_counts->values.size() && !max_counts->is_nil(eid) && max_counts->values[eid] > 0)
            slot_count = max_counts->values[eid];
        std::vector<NozzleVolumeType> types;
        for (int slot = 0; slot < slot_count; ++slot)
            types.push_back(get_default_nozzle_volume_type(config, int(eid), slot));
        printer.slot_counts.push_back(slot_count);
        printer.has_rack = printer.has_rack || slot_count > 1;
        printer.default_types.push_back(std::move(types));
    }
    return printer;
}

/**
 * \brief The project's per-slot keys as slots
 *
 * \return nothing if the keys are missing or do not fit the printer
 */
std::optional<std::vector<ExtruderSlots>> read_per_slot_keys(const DynamicPrintConfig &project, const SelectedPrinter &printer)
{
    if (!per_slot_nozzle_options_consistent(project))
        return std::nullopt;
    const std::vector<int>    &counts = project.option<ConfigOptionInts>("extruder_nozzle_count")->values;
    const std::vector<int>    &types  = project.option<ConfigOptionEnumsGeneric>("extruder_nozzle_volume_type")->values;
    const std::vector<double> &diams  = project.option<ConfigOptionFloats>("extruder_nozzle_diameter")->values;
    const std::vector<int>    &ids    = project.option<ConfigOptionInts>("extruder_nozzle_slot")->values;
    if (int(counts.size()) != printer.extruder_count())
        return std::nullopt;

    std::vector<ExtruderSlots> extruders(counts.size());
    int flat        = 0; // index into the flat per-nozzle arrays
    int global_base = 0; // slot ids count across all extruders
    for (size_t eid = 0; eid < counts.size(); ++eid) {
        const int slot_count = std::max(counts[eid], printer.slot_counts[eid]);
        extruders[eid].resize(size_t(slot_count));
        for (int k = 0; k < counts[eid]; ++k, ++flat) {
            int local = ids[flat] - global_base;
            if (local < 0 || local >= slot_count)
                local = k;
            extruders[eid][local] = {format_diameter_to_str(diams[flat]), NozzleVolumeType(types[flat]), NozzleSlotStatus::Normal};
        }
        global_base += slot_count;
    }
    return extruders;
}

/**
 * \brief The legacy counts option \p key, or null if it is missing, empty or has an empty entry
 */
const ConfigOptionStrings *valid_legacy_counts(const DynamicPrintConfig &project, const char *key)
{
    const auto *opt = project.option<ConfigOptionStrings>(key);
    if (!opt || opt->values.empty())
        return nullptr;
    const bool has_empty = std::any_of(opt->values.begin(), opt->values.end(), [](const std::string &value) { return value.empty(); });
    return has_empty ? nullptr : opt;
}

/**
 * \brief Slots from the legacy per-extruder counts
 *
 * The nozzles take the selected diameter.
 */
std::optional<std::vector<ExtruderSlots>> read_legacy_counts(const DynamicPrintConfig &project, const SelectedPrinter &printer)
{
    // The new key keeps the real type names; the old one may be downgraded on export.
    const ConfigOptionStrings *opt = valid_legacy_counts(project, "extruder_nozzle_stats_new");
    if (!opt)
        opt = valid_legacy_counts(project, "extruder_nozzle_stats");
    if (!opt || int(opt->values.size()) != printer.extruder_count())
        return std::nullopt;

    const std::vector<std::map<NozzleVolumeType, int>> counts = get_extruder_nozzle_stats(opt->values);
    std::vector<ExtruderSlots>                        extruders(counts.size());
    for (size_t eid = 0; eid < counts.size(); ++eid) {
        for (const auto &[type, count] : counts[eid])
            for (int i = 0; i < count && int(extruders[eid].size()) < printer.slot_counts[eid]; ++i)
                extruders[eid].push_back({printer.diameter, type, NozzleSlotStatus::Normal});
        extruders[eid].resize(size_t(printer.slot_counts[eid]));
    }
    return extruders;
}

/**
 * \brief Slots from the per-extruder volume type alone
 *
 * All slots are installed.
 */
std::optional<std::vector<ExtruderSlots>> read_volume_types(const DynamicPrintConfig &project, const SelectedPrinter &printer)
{
    const auto *opt = project.option<ConfigOptionEnumsGeneric>("nozzle_volume_type");
    if (!opt || int(opt->values.size()) != printer.extruder_count())
        return std::nullopt;
    std::vector<ExtruderSlots> extruders;
    for (int eid = 0; eid < printer.extruder_count(); ++eid) {
        const NozzleVolumeType type = NozzleVolumeType(opt->values[eid]);
        // Hybrid only says the slots differ; the preset gives the mix.
        const std::vector<NozzleVolumeType> types = type == nvtHybrid ? printer.default_types[eid] :
                                                                        std::vector<NozzleVolumeType>(size_t(printer.slot_counts[eid]), type);
        extruders.push_back(uniform_slots(printer.diameter, types));
    }
    return extruders;
}

/**
 * \brief Replace with Standard the volume types that no nozzle of the slot's diameter is made with
 *
 * Printer presets still list such combinations, and a machine or an old project can report them,
 * so every source goes through nozzle_variant_exists().
 *
 * \param slots        slots of one extruder; changed in place
 * \param extruder_id  for the log only
 */
void drop_unmade_volume_types(ExtruderSlots &slots, int extruder_id)
{
    for (NozzleSlot &slot : slots) {
        if (slot.empty() || nozzle_variant_exists(slot.diameter, slot.volume_type))
            continue;
        BOOST_LOG_TRIVIAL(warning) << KV(log_key::extruder_id, extruder_id) << slot.diameter << " nozzle "
                                   << GetNozzleVolumeTypeStr(slot.volume_type) << " replaced by Standard";
        slot.volume_type = nvtStandard;
    }
}

/**
 * \brief One extruder of a history value: a volume type per slot, nothing for an empty slot
 */
using HistoryTypes = std::vector<std::optional<NozzleVolumeType>>;

/**
 * \brief Encode slots as a history value of BambuStudio.conf
 *
 * Extruders are joined by ',' and slots by '|'. A slot is its volume type name, or nothing if it is
 * empty; an older version reads that empty token as Standard.
 */
std::string encode_history(const std::vector<ExtruderSlots> &extruders)
{
    std::string out;
    for (size_t eid = 0; eid < extruders.size(); ++eid) {
        if (eid > 0)
            out += ',';
        for (size_t slot = 0; slot < extruders[eid].size(); ++slot) {
            if (slot > 0)
                out += '|';
            if (!extruders[eid][slot].empty())
                out += GetNozzleVolumeTypeStr(extruders[eid][slot].volume_type);
        }
    }
    return out;
}

/**
 * \brief The volume type named \p token
 *
 * \return nothing for a name no volume type has; reading it as Standard would invent a nozzle
 */
std::optional<NozzleVolumeType> parse_volume_type(const std::string &token)
{
    const NozzleVolumeType type = nozzle_volume_type_from_string(token);
    if (GetNozzleVolumeTypeStr(type) != token)
        return std::nullopt;
    return type;
}

/**
 * \brief Decode a history value
 *
 * An old value has one token per extruder.
 *
 * \return empty if a token names no volume type, so the next source is used
 */
std::vector<HistoryTypes> decode_history(const std::string &value)
{
    std::vector<HistoryTypes> extruders;
    if (value.empty())
        return extruders;
    std::vector<std::string> extruder_tokens;
    boost::split(extruder_tokens, value, boost::is_any_of(","));
    for (const std::string &extruder_token : extruder_tokens) {
        std::vector<std::string> slot_tokens;
        boost::split(slot_tokens, extruder_token, boost::is_any_of("|"));
        HistoryTypes types;
        for (std::string &token : slot_tokens) {
            boost::trim(token);
            if (token.empty()) {
                types.push_back(std::nullopt);
                continue;
            }
            const std::optional<NozzleVolumeType> type = parse_volume_type(token);
            if (!type) {
                BOOST_LOG_TRIVIAL(warning) << "nozzle history ignored, unknown volume type " << token;
                return {};
            }
            types.push_back(type);
        }
        extruders.push_back(std::move(types));
    }
    return extruders;
}

/**
 * \brief BambuStudio.conf section with the full slots of each printer model, diameters included
 *
 * A separate section, because older versions reject a nozzle_volume_types value with a diameter in it.
 */
constexpr const char *SLOT_HISTORY_SECTION = "nozzle_slots";

/**
 * \brief Encode slots as a value of the section nozzle_slots
 *
 * Extruders are joined by ',' and slots by '|'. A slot is "<diameter> <volume type>", or nothing if it
 * is empty. The status is not kept: a restored slot is Normal.
 */
std::string encode_slot_history(const std::vector<ExtruderSlots> &extruders)
{
    std::string out;
    for (size_t eid = 0; eid < extruders.size(); ++eid) {
        if (eid > 0)
            out += ',';
        for (size_t slot = 0; slot < extruders[eid].size(); ++slot) {
            if (slot > 0)
                out += '|';
            const NozzleSlot &nozzle = extruders[eid][slot];
            if (!nozzle.empty())
                out += nozzle.diameter + " " + GetNozzleVolumeTypeStr(nozzle.volume_type);
        }
    }
    return out;
}

/**
 * \brief Decode a value of the section nozzle_slots
 *
 * A token without a diameter is an empty slot.
 *
 * \return empty if a token names no volume type, so the next source is used
 */
std::vector<ExtruderSlots> decode_slot_history(const std::string &value)
{
    std::vector<ExtruderSlots> extruders;
    if (value.empty())
        return extruders;
    std::vector<std::string> extruder_tokens;
    boost::split(extruder_tokens, value, boost::is_any_of(","));
    for (const std::string &extruder_token : extruder_tokens) {
        std::vector<std::string> slot_tokens;
        boost::split(slot_tokens, extruder_token, boost::is_any_of("|"));
        ExtruderSlots slots;
        for (std::string &token : slot_tokens) {
            boost::trim(token);
            const size_t space    = token.find(' ');
            const double diameter = space == std::string::npos ? 0.0 : string_to_double_decimal_point(token.substr(0, space));
            if (diameter <= 0.0) {
                slots.push_back(NozzleSlot());
                continue;
            }
            const std::optional<NozzleVolumeType> type = parse_volume_type(token.substr(space + 1));
            if (!type) {
                BOOST_LOG_TRIVIAL(warning) << "nozzle slot history ignored, unknown volume type " << token;
                return {};
            }
            slots.push_back({format_diameter_to_str(diameter), *type, NozzleSlotStatus::Normal});
        }
        extruders.push_back(std::move(slots));
    }
    return extruders;
}

/**
 * \brief Number of slots per volume type
 *
 * Counts the slots of the selected diameter, or all installed slots if none has it.
 *
 * \param multi_diameter  the multi-diameter mode is on; then all installed slots count
 */
std::map<NozzleVolumeType, int> count_slots(const ExtruderSlots &slots, const SelectedPrinter &printer, bool multi_diameter)
{
    std::map<NozzleVolumeType, int> counts;
    if (!multi_diameter)
        for (const NozzleSlot &slot : slots)
            if (slot.diameter == printer.diameter)
                ++counts[slot.volume_type];
    // An extruder that holds only other diameters is still installed; it must stay in grouping.
    if (counts.empty())
        for (const NozzleSlot &slot : slots)
            if (!slot.empty())
                ++counts[slot.volume_type];
    return counts;
}

/**
 * \brief The single volume type of \p counts; Hybrid if they differ, Standard if there is none
 */
NozzleVolumeType summary_volume_type(const std::map<NozzleVolumeType, int> &counts)
{
    if (counts.empty())
        return nvtStandard;
    return counts.size() == 1 ? counts.begin()->first : nvtHybrid;
}

} // namespace

struct NozzleInventory::Impl
{
    DynamicPrintConfig &mirror;
    AppConfig          *history = nullptr; ///< BambuStudio.conf sections nozzle_volume_types and nozzle_slots; null in scratch copies
    SelectedPrinter     printer;           ///< what resolve() reads from the selected printer preset
    NozzleLayer         project, machine, user;
    uint64_t            last_stamp = 0;

    std::vector<ExtruderSlots> result;
    std::vector<NozzleSource>  sources;

    explicit Impl(DynamicPrintConfig &mirror) : mirror(mirror) {}

    /**
     * \brief The newest layer of the selected model with its extruder count, or null if none
     */
    const NozzleLayer *top_layer() const
    {
        const NozzleLayer *top = nullptr;
        for (const NozzleLayer *layer : {&project, &machine, &user}) {
            if (layer->stamp == 0 || layer->model != printer.model || int(layer->extruders.size()) != printer.extruder_count())
                continue;
            if (!top || layer->stamp > top->stamp)
                top = layer;
        }
        return top;
    }

    /**
     * \brief The full slots last saved for the selected model
     *
     * \return empty if they do not fit its extruders
     */
    std::vector<ExtruderSlots> model_history() const
    {
        if (!history || printer.model.empty())
            return {};
        std::vector<ExtruderSlots> extruders = decode_slot_history(history->get(SLOT_HISTORY_SECTION, printer.model));
        if (int(extruders.size()) != printer.extruder_count())
            return {};
        return extruders;
    }

    /**
     * \brief The saved volume types of the selected variant; empty if there is none
     */
    std::vector<HistoryTypes> history_types() const
    {
        if (!history || printer.history_key.empty())
            return {};
        return decode_history(history->get_nozzle_volume_types_from_config(printer.history_key));
    }

    /**
     * \brief History of the selected variant for one extruder
     *
     * \param grid  saved volume types of the variant
     * \param eid   0-based extruder
     * \return      nothing if the extruder has no entry
     */
    std::optional<ExtruderSlots> history_slots(const std::vector<HistoryTypes> &grid, int eid) const
    {
        if (eid >= int(grid.size()) || grid[eid].empty())
            return std::nullopt;
        ExtruderSlots slots;
        // A short entry repeats its last type over the remaining slots.
        for (int slot = 0; slot < printer.slot_counts[eid]; ++slot) {
            const std::optional<NozzleVolumeType> &type = slot < int(grid[eid].size()) ? grid[eid][slot] : grid[eid].back();
            slots.push_back(type ? NozzleSlot{printer.diameter, *type, NozzleSlotStatus::Normal} : NozzleSlot());
        }
        return slots;
    }

    /**
     * \brief Compute the effective slots and their source for every extruder
     */
    void resolve()
    {
        const NozzleLayer *top    = top_layer();
        const auto         saved  = model_history();
        const auto         grid   = history_types();
        result.assign(size_t(printer.extruder_count()), {});
        sources.assign(size_t(printer.extruder_count()), NozzleSource::Preset);
        for (int eid = 0; eid < printer.extruder_count(); ++eid) {
            if (top && holds_selected_diameter(top->extruders[eid], printer)) {
                result[eid]  = fit_slot_count(top->extruders[eid], printer.slot_counts[eid]);
                sources[eid] = top->source;
            } else if (!saved.empty() && holds_selected_diameter(saved[eid], printer)) {
                result[eid]  = fit_slot_count(saved[eid], printer.slot_counts[eid]);
                sources[eid] = NozzleSource::History;
            } else if (auto slots = history_slots(grid, eid)) {
                result[eid]  = std::move(*slots);
                sources[eid] = NozzleSource::History;
            } else {
                result[eid] = uniform_slots(printer.diameter, printer.default_types[eid]);
            }
            drop_unmade_volume_types(result[eid], eid);
        }
    }

    /**
     * \brief Save the result as history
     *
     * History keeps the last state of the printer, so only machine and user results go there.
     */
    void save_history() const
    {
        const NozzleLayer *top = top_layer();
        if (!history || !top || top->source == NozzleSource::Project || printer.history_key.empty())
            return;
        if (std::find(sources.begin(), sources.end(), top->source) == sources.end())
            return;
        const std::string encoded = encode_history(result);
        if (encoded != history->get_nozzle_volume_types_from_config(printer.history_key))
            history->save_nozzle_volume_types_to_config(printer.history_key, encoded);
        const std::string slots = encode_slot_history(result);
        if (!printer.model.empty() && slots != history->get(SLOT_HISTORY_SECTION, printer.model))
            history->set(SLOT_HISTORY_SECTION, printer.model, slots);
    }

    /**
     * \brief Replace one layer and stamp it as the newest
     *
     * Snaps each diameter to the preset spelling, such as "0.4"; an empty slot keeps its empty diameter.
     */
    void write(NozzleLayer &layer, NozzleSource source, const std::string &model, std::vector<ExtruderSlots> extruders)
    {
        // Diameters are compared as text; one spelling here frees the writers from matching the preset.
        for (ExtruderSlots &slots : extruders)
            for (NozzleSlot &slot : slots)
                if (!slot.diameter.empty())
                    slot.diameter = format_diameter_to_str(string_to_double_decimal_point(slot.diameter));
        layer.source    = source;
        layer.model     = model;
        layer.extruders = std::move(extruders);
        layer.stamp     = ++last_stamp;
    }

    /**
     * \brief Recompute, mirror, persist and log after any intent
     *
     * \param intent  name of the change, for the log
     * \param self    the owner, whose export_to() fills the mirror
     */
    void commit(const char *intent, NozzleInventory &self)
    {
        resolve();
        self.export_to(mirror);
        save_history();
        BOOST_LOG_TRIVIAL(info) << "nozzle inventory " << intent << ": " << describe();
    }

    /**
     * \brief All layers and the result, for logs
     */
    std::string describe() const
    {
        std::string out = "printer=" + printer.history_key + " diameter=" + printer.diameter +
                          (multi_diameter() ? " multi-diameter" : "") + (printer.has_rack ? " rack" : "") + " " +
                          layer_text("project", &project) + " " + layer_text("machine", &machine) + " " +
                          layer_text("user", &user) + " result=";
        for (size_t eid = 0; eid < result.size(); ++eid)
            out += std::string(eid ? "," : "") + source_name(sources[eid]) + ":" + slots_text(result[eid]);
        return out;
    }

    /**
     * \brief Check if the multi-diameter mode is on for the selected printer
     *
     * Reads the mode from the project on each call, so no caller has to report a mode change. A
     * printer without multi-diameter support reads off, even while the project still has the mode on.
     */
    bool multi_diameter() const { return printer.supports_multi_diameter && multi_nozzle_diameter_mode_enabled(mirror); }

    bool valid_extruder(int extruder_id) const { return extruder_id >= 0 && extruder_id < int(result.size()); }
};

NozzleInventory::NozzleInventory(DynamicPrintConfig &mirror) : m_impl(std::make_unique<Impl>(mirror)) {}

NozzleInventory::~NozzleInventory() = default;

NozzleInventory &NozzleInventory::operator=(const NozzleInventory &rhs)
{
    if (this == &rhs)
        return *this;
    m_impl->printer    = rhs.m_impl->printer;
    m_impl->project    = rhs.m_impl->project;
    m_impl->machine    = rhs.m_impl->machine;
    m_impl->user       = rhs.m_impl->user;
    m_impl->last_stamp = rhs.m_impl->last_stamp;
    m_impl->result     = rhs.m_impl->result;
    m_impl->sources    = rhs.m_impl->sources;
    return *this;
}

void NozzleInventory::set_history(AppConfig *history) { m_impl->history = history; }

void NozzleInventory::select_printer(const PresetCollection &printers)
{
    m_impl->printer = read_selected_printer(printers);
    m_impl->commit("select printer", *this);
}

void NozzleInventory::open_project(const PresetCollection &printers, const DynamicPrintConfig &file_keys)
{
    m_impl->printer = read_selected_printer(printers);
    auto extruders = read_per_slot_keys(file_keys, m_impl->printer);
    if (!extruders)
        extruders = read_legacy_counts(file_keys, m_impl->printer);
    if (!extruders && file_keys.has("nozzle_volume_type"))
        extruders = read_volume_types(file_keys, m_impl->printer);
    if (extruders)
        m_impl->write(m_impl->project, NozzleSource::Project, m_impl->printer.model, std::move(*extruders));
    else
        m_impl->project = NozzleLayer();
    if (!extruders)
        BOOST_LOG_TRIVIAL(warning) << "project has no usable nozzle keys, nozzles come from the printer";
    m_impl->commit("open project", *this);
}

void NozzleInventory::close_project()
{
    m_impl->project = NozzleLayer();
    m_impl->commit("close project", *this);
}

void NozzleInventory::sync_machine(const std::string &printer_model, std::vector<ExtruderSlots> layout)
{
    m_impl->write(m_impl->machine, NozzleSource::Machine, printer_model, std::move(layout));
    m_impl->commit("sync machine", *this);
}

void NozzleInventory::user_set_slots(int extruder_id, ExtruderSlots slots)
{
    if (!m_impl->valid_extruder(extruder_id)) {
        BOOST_LOG_TRIVIAL(warning) << KV(log_key::extruder_id, extruder_id) << "user set slots ignored, no such extruder";
        return;
    }
    std::vector<ExtruderSlots> extruders = m_impl->result;
    extruders[extruder_id]               = fit_slot_count(std::move(slots), m_impl->printer.slot_counts[extruder_id]);
    m_impl->write(m_impl->user, NozzleSource::User, m_impl->printer.model, std::move(extruders));
    m_impl->commit("user set slots", *this);
}

void NozzleInventory::user_set_counts(int extruder_id, const std::map<NozzleVolumeType, int> &counts)
{
    if (!m_impl->valid_extruder(extruder_id)) {
        BOOST_LOG_TRIVIAL(warning) << KV(log_key::extruder_id, extruder_id) << "user set counts ignored, no such extruder";
        return;
    }
    // The counts describe the nozzles of the selected diameter; other diameters keep their slots.
    ExtruderSlots slots = m_impl->result[extruder_id];
    for (NozzleSlot &slot : slots)
        if (slot.diameter == m_impl->printer.diameter || m_impl->multi_diameter())
            slot = NozzleSlot();
    auto next_free = slots.begin();
    int  dropped   = 0;
    for (const auto &[type, count] : counts) {
        for (int i = 0; i < count; ++i) {
            next_free = std::find_if(next_free, slots.end(), [](const NozzleSlot &slot) { return slot.empty(); });
            if (next_free == slots.end()) {
                dropped += count - i;
                break;
            }
            *next_free = {m_impl->printer.diameter, type, NozzleSlotStatus::Normal};
        }
    }
    if (dropped > 0)
        BOOST_LOG_TRIVIAL(warning) << KV(log_key::extruder_id, extruder_id) << "user set counts: " << dropped
                                   << " nozzles dropped, no free slot";
    user_set_slots(extruder_id, std::move(slots));
}

void NozzleInventory::user_set_volume_type(int extruder_id, NozzleVolumeType type)
{
    if (!m_impl->valid_extruder(extruder_id) || type == nvtHybrid || m_impl->printer.slot_counts[extruder_id] != 1) {
        BOOST_LOG_TRIVIAL(warning) << KV(log_key::extruder_id, extruder_id) << "user set volume type ignored, extruder has no single nozzle";
        return;
    }
    user_set_slots(extruder_id, {{m_impl->printer.diameter, type, NozzleSlotStatus::Normal}});
}

void NozzleInventory::user_unify_diameter(const std::string &diameter)
{
    // The newest layer, not the result: the result already replaced a nozzle of another diameter.
    const NozzleLayer *top = m_impl->top_layer();
    if (!top) {
        BOOST_LOG_TRIVIAL(warning) << "user unify diameter ignored, no nozzles of the selected printer";
        return;
    }
    std::vector<ExtruderSlots> extruders = top->extruders;
    for (ExtruderSlots &slots : extruders)
        for (NozzleSlot &slot : slots)
            if (!slot.empty())
                slot.diameter = diameter;
    m_impl->write(m_impl->user, NozzleSource::User, m_impl->printer.model, std::move(extruders));
    m_impl->commit("user unify diameter", *this);
}

int NozzleInventory::extruder_count() const { return int(m_impl->result.size()); }

const ExtruderSlots &NozzleInventory::slots(int extruder_id) const
{
    static const ExtruderSlots none;
    return m_impl->valid_extruder(extruder_id) ? m_impl->result[extruder_id] : none;
}

std::map<NozzleVolumeType, int> NozzleInventory::counts(int extruder_id, const std::optional<std::string> &diameter) const
{
    if (!diameter)
        return count_slots(slots(extruder_id), m_impl->printer, m_impl->multi_diameter());

    const std::string wanted = format_diameter_to_str(string_to_double_decimal_point(*diameter));
    std::map<NozzleVolumeType, int> counts;
    for (const NozzleSlot &slot : slots(extruder_id))
        if (!slot.empty() && slot.diameter == wanted)
            ++counts[slot.volume_type];
    return counts;
}

int NozzleInventory::count(int extruder_id, std::optional<NozzleVolumeType> type, const std::optional<std::string> &diameter) const
{
    const std::map<NozzleVolumeType, int> all = counts(extruder_id, diameter);
    if (!type || *type == nvtHybrid) {
        int total = 0;
        for (const auto &entry : all)
            total += entry.second;
        return total;
    }
    const auto it = all.find(*type);
    return it == all.end() ? 0 : it->second;
}

NozzleVolumeType NozzleInventory::volume_type(int extruder_id) const
{
    return summary_volume_type(counts(extruder_id));
}

void NozzleInventory::refresh_mirror() { export_to(m_impl->mirror); }

void NozzleInventory::export_to(DynamicPrintConfig &config) const
{
    // Before the first select_printer() there are no extruders; empty keys would break readers of values[0].
    if (extruder_count() == 0)
        return;
    std::vector<int>                             volume_types;
    std::vector<std::map<NozzleVolumeType, int>> all_counts;
    ExtruderFullStat                             full_stat;
    full_stat.resize(size_t(extruder_count()));
    for (int eid = 0; eid < extruder_count(); ++eid) {
        all_counts.push_back(counts(eid));
        volume_types.push_back(int(summary_volume_type(all_counts.back())));
        std::vector<std::pair<std::string, std::string>> stats;
        for (const NozzleSlot &slot : slots(eid))
            stats.emplace_back(slot.diameter, slot.empty() ? std::string() : GetNozzleVolumeTypeStr(slot.volume_type));
        full_stat.set_extruder_stats(eid, stats);
    }
    config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type", true)->values = volume_types;
    const std::vector<std::string> legacy = save_extruder_nozzle_stats_to_string(all_counts);
    config.option<ConfigOptionStrings>("extruder_nozzle_stats", true)->values     = legacy;
    config.option<ConfigOptionStrings>("extruder_nozzle_stats_new", true)->values = legacy;
    sync_per_slot_nozzle_options_from_full_stat(config, full_stat);
}

} // namespace Slic3r
