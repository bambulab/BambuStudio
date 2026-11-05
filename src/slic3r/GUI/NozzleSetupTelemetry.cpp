#include "NozzleSetupTelemetry.hpp"

#include "nlohmann/json.hpp"

#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/ExtruderHelpers.hpp"
#include "slic3r/GUI/GUI_App.hpp"

namespace Slic3r::GUI {

namespace {

// The card dialog edits the only extruder there is.
constexpr int k_extruder_index = 0;

// Confirmed edits not yet reported. Not persisted: edits made before a restart and never reported are dropped.
// Unguarded on purpose: the dialog and the slice event both use it on the UI thread only.
NozzleSetupEdits s_unreported_edits;

std::pair<int, int> current_ams_counts() { return wxGetApp().preset_bundle->get_ams_count(k_extruder_index); }

// Split a signed count change into the add / remove counters it represents.
void count_delta(int delta, int &add_ops, int &remove_ops)
{
    if (delta > 0)
        add_ops += delta;
    else
        remove_ops -= delta;
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
    if (from.diameter != to.diameter)
        ++m_edits.nozzle_diameter_ops;
    // An empty slot has no flow type, so filling or clearing a slot is a diameter edit only.
    if (!from.is_empty() && !to.is_empty() && from.volume != to.volume)
        ++m_edits.nozzle_volume_ops;
}

void NozzleSetupEditTally::observe_ams_counts()
{
    const std::pair<int, int> counts = current_ams_counts();
    count_delta(counts.first - m_ams_counts.first, m_edits.ams4_add_ops, m_edits.ams4_remove_ops);
    count_delta(counts.second - m_ams_counts.second, m_edits.ams1_add_ops, m_edits.ams1_remove_ops);
    m_ams_counts = counts;
}

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
    if (!printer.is_bbl_vendor_preset(&preset_bundle) || nozzle_layout_of(printer.config) != NozzleLayout::SingleExtruderRack)
        return nullptr;

    nlohmann::json payload;
    if (multi_nozzle_diameter_mode_enabled(preset_bundle.project_config)) {
        payload["diameter_mode"] = "multi";
    } else {
        payload["diameter_mode"] = "single";
        const auto *diameters = printer.config.option<ConfigOptionFloatsNullable>("nozzle_diameter");
        if (diameters != nullptr && !diameters->values.empty())
            payload["nozzle_diameter"] = format_diameter_to_str(diameters->get_at(0));
    }
    payload["dialog_edits"] = to_json(s_unreported_edits);
    return payload;
}

void on_nozzle_setup_telemetry_sent(const nlohmann::json &sent_payload)
{
    if (sent_payload.is_object() && sent_payload.contains("dialog_edits"))
        subtract_sent(s_unreported_edits, sent_payload["dialog_edits"]);
}

} // namespace Slic3r::GUI
