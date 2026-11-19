#include "NozzleSetupTelemetry.hpp"

#include <set>
#include <string>

#include "nlohmann/json.hpp"

#include "libslic3r/Log.hpp"
#include "libslic3r/NozzleInventory.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "slic3r/GUI/ExtruderHelpers.hpp"
#include "slic3r/GUI/GUI.hpp" // into_u8
#include "slic3r/GUI/GUI_App.hpp"

namespace Slic3r::GUI {

namespace {

// Confirmed edits not yet reported. Not persisted: edits made before a restart and never reported are dropped.
// Unguarded on purpose: the edit views and the slice event all use it on the UI thread only.
NozzleSetupEdits s_unreported_edits;

// {4-slot, 1-slot} AMS counts summed over all extruders.
std::pair<int, int> current_ams_counts()
{
    PresetBundle *bundle = wxGetApp().preset_bundle;
    if (bundle == nullptr)
        return {0, 0};
    std::pair<int, int> total{0, 0};
    for (int extruder = 0; extruder < int(bundle->extruder_ams_counts.size()); ++extruder) {
        const std::pair<int, int> counts = bundle->get_ams_count(extruder);
        total.first += counts.first;
        total.second += counts.second;
    }
    return total;
}

// Split a signed count change into the add / remove counters it represents.
void count_delta(int delta, int &add_ops, int &remove_ops)
{
    if (delta > 0)
        add_ops += delta;
    else
        remove_ops -= delta;
}

// One pick, in a form both nozzle types convert to; an empty diameter is an empty slot.
void count_pick(NozzleSetupEdits &edits, const std::string &from_diameter, NozzleVolumeType from_volume,
                const std::string &to_diameter, NozzleVolumeType to_volume)
{
    if (from_diameter != to_diameter)
        ++edits.nozzle_diameter_ops;
    // An empty slot has no flow type, so filling or clearing a slot is a diameter edit only.
    if (!from_diameter.empty() && !to_diameter.empty() && from_volume != to_volume)
        ++edits.nozzle_volume_ops;
}

// Distinct diameters of the nozzles that print, in ascending order.
nlohmann::json printing_diameters(const NozzleInventory &inventory)
{
    const std::set<std::string> diameters = inventory.printing_diameters();
    if (diameters.empty())
        BOOST_LOG_TRIVIAL(warning) << "no nozzle prints, nozzle_diameters is empty";
    return nlohmann::json(diameters);
}

nlohmann::json to_json(const NozzleSetupEdits &edits)
{
    return {
        {"nozzle_diameter_ops", edits.nozzle_diameter_ops},
        {"nozzle_volume_ops", edits.nozzle_volume_ops},
        {"ams4_add_ops", edits.ams4_add_ops},
        {"ams4_remove_ops", edits.ams4_remove_ops},
        {"ams1_add_ops", edits.ams1_add_ops},
        {"ams1_remove_ops", edits.ams1_remove_ops},
    };
}

// Subtract the counts of a sent "dialog_edits" object; a missing field subtracts nothing.
void subtract_sent(NozzleSetupEdits &edits, const nlohmann::json &sent)
{
    edits.nozzle_diameter_ops -= sent.value("nozzle_diameter_ops", 0);
    edits.nozzle_volume_ops -= sent.value("nozzle_volume_ops", 0);
    edits.ams4_add_ops -= sent.value("ams4_add_ops", 0);
    edits.ams4_remove_ops -= sent.value("ams4_remove_ops", 0);
    edits.ams1_add_ops -= sent.value("ams1_add_ops", 0);
    edits.ams1_remove_ops -= sent.value("ams1_remove_ops", 0);
}

} // namespace

NozzleSetupEditTally::NozzleSetupEditTally() : m_ams_counts(current_ams_counts()) {}

void NozzleSetupEditTally::record_nozzle_pick(const NozzleConfig &from, const NozzleConfig &to)
{
    count_pick(m_edits, into_u8(from.diameter), from.volume, into_u8(to.diameter), to.volume);
}

void NozzleSetupEditTally::record_nozzle_pick(const NozzleSlot &from, const NozzleSlot &to)
{
    count_pick(m_edits, from.empty() ? std::string() : from.diameter, from.volume_type,
               to.empty() ? std::string() : to.diameter, to.volume_type);
}

void NozzleSetupEditTally::observe_ams_counts()
{
    const std::pair<int, int> counts = current_ams_counts();
    count_delta(counts.first - m_ams_counts.first, m_edits.ams4_add_ops, m_edits.ams4_remove_ops);
    count_delta(counts.second - m_ams_counts.second, m_edits.ams1_add_ops, m_edits.ams1_remove_ops);
    m_ams_counts = counts;
}

void NozzleSetupEditTally::rebaseline_ams_counts() { m_ams_counts = current_ams_counts(); }

void NozzleSetupEditTally::commit()
{
    s_unreported_edits.nozzle_diameter_ops += m_edits.nozzle_diameter_ops;
    s_unreported_edits.nozzle_volume_ops += m_edits.nozzle_volume_ops;
    s_unreported_edits.ams4_add_ops += m_edits.ams4_add_ops;
    s_unreported_edits.ams4_remove_ops += m_edits.ams4_remove_ops;
    s_unreported_edits.ams1_add_ops += m_edits.ams1_add_ops;
    s_unreported_edits.ams1_remove_ops += m_edits.ams1_remove_ops;
    m_edits = {};
}

nlohmann::json nozzle_setup_telemetry(PresetBundle &preset_bundle)
{
    Preset &printer = preset_bundle.printers.get_edited_preset();
    if (!printer.is_bbl_vendor_preset(&preset_bundle) || !supports_multi_nozzle_diameter_mode(printer.config))
        return nullptr;

    nlohmann::json payload;
    payload["diameter_mode"]    = multi_nozzle_diameter_mode_enabled(preset_bundle.project_config) ? "multi" : "single";
    payload["nozzle_diameters"] = printing_diameters(preset_bundle.nozzle_inventory);
    payload["dialog_edits"]     = to_json(s_unreported_edits);
    return payload;
}

void on_nozzle_setup_telemetry_sent(const nlohmann::json &sent_payload)
{
    if (sent_payload.is_object() && sent_payload.contains("dialog_edits"))
        subtract_sent(s_unreported_edits, sent_payload["dialog_edits"]);
}

} // namespace Slic3r::GUI
