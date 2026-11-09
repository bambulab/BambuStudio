#include "FilamentMapPanel.hpp"
#include "Widgets/MultiNozzleSync.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "DeviceCore/DevConfigUtil.h"
#include "DeviceCore/DevNozzleSystem.h"
#include <cassert>
#include <wx/dcbuffer.h>
#include <wx/scrolwin.h>
#include "wx/graphics.h"
#include <map>
#include <set>
#include <algorithm>
#include <cstdlib>
#include <optional>
#include <functional>
#include <sstream>
#include "libslic3r/Log.hpp"

namespace Slic3r::GUI {

std::vector<int> get_fixed_nozzle_indices(int total_count, int ams_index)
{
    std::vector<int> result;
    for (int i = 0; i < total_count; ++i)
        if (i != ams_index) result.push_back(i);
    return result;
}

wxString get_ams_nozzle_label(int ams_index_0based)
{
    return wxString::Format(_L("Nozzle %s"), wxString::Format("%d", ams_index_0based + 1));
}

wxString get_fixed_nozzle_label(int total_count, int ams_index_0based)
{
    auto indices = get_fixed_nozzle_indices(total_count, ams_index_0based);
    if (indices.empty()) return _L("Nozzle");

    wxString indices_label;
    bool first_range = true;
    size_t i = 0;
    while (i < indices.size()) {
        int start = indices[i] + 1;
        int end = start;
        while (i + 1 < indices.size() && indices[i + 1] == indices[i] + 1) {
            ++i;
            end = indices[i] + 1;
        }
        if (!first_range) indices_label += "/";
        if (start == end)
            indices_label += wxString::Format("%d", start);
        else
            indices_label += wxString::Format("%d-%d", start, end);
        first_range = false;
        ++i;
    }
    return wxString::Format(_L("Nozzle %s"), indices_label);
}

int get_effective_ams_nozzle_idx_for_active_diameter(const ConfigBase& config)
{
    int ams_idx = get_effective_ams_nozzle_idx(config);
    if (ams_idx < 0)
        return -1;

    const auto* nd = dynamic_cast<const ConfigOptionFloatsNullable*>(config.option("nozzle_diameter"));
    const std::string active_diameter = (nd && !nd->values.empty()) ? format_diameter_to_str(nd->values.front()) : std::string();
    const ExtruderSlots &slots = wxGetApp().preset_bundle->nozzle_inventory.slots(0);
    if (ams_idx >= (int) slots.size())
        return -1;

    const std::string &ams_diam = slots[ams_idx].diameter;
    // In multi-diameter mode nozzle_diameter is only the conservative slicing baseline (normally
    // 0.2), not a filter on which physical nozzles may participate. An AMS attached to a 0.6 nozzle
    // must therefore remain the AMS group even while that baseline is 0.2; otherwise the manual
    // grouping UI collapses the AMS nozzle and all fixed nozzles into one group.
    const bool multi_diameter = multi_nozzle_diameter_mode_enabled(config);
    const bool usable = !slots[ams_idx].empty() &&
        (multi_diameter || active_diameter.empty() || ams_diam.empty() ||
         format_diameter_to_str(std::atof(ams_diam.c_str())) == active_diameter);
    return usable ? ams_idx : -1;
}

bool has_groupable_tpu285_nozzle(const ConfigBase& config)
{
    const auto* nd = dynamic_cast<const ConfigOptionFloatsNullable*>(config.option("nozzle_diameter"));
    const std::string active_diameter = (nd && !nd->values.empty()) ? format_diameter_to_str(nd->values.front()) : std::string();
    const bool multi_diameter = multi_nozzle_diameter_mode_enabled(config);
    for (const NozzleSlot &slot : wxGetApp().preset_bundle->nozzle_inventory.slots(0)) {
        if (slot.empty() || slot.volume_type != nvtTPU285)
            continue;
        if (multi_diameter || active_diameter.empty() || slot.diameter.empty() ||
            format_diameter_to_str(std::atof(slot.diameter.c_str())) == active_diameter)
            return true;
    }
    return false;
}

bool is_multi_diameter_without_ams(const ConfigBase& config)
{
    return is_single_ext_multi_nozzle(config) &&
           multi_nozzle_diameter_mode_enabled(config) &&
           get_effective_ams_nozzle_idx(config) < 0;
}

std::vector<FilamentMapMode> get_single_ext_multi_nozzle_auto_modes(const ConfigBase& config)
{
    // Without an AMS every nozzle holds a single filament, so there is no flush to save.
    // With one, filament saving applies in both single- and multi-diameter mode.
    std::vector<FilamentMapMode> modes = {fmmAutoForMatch};
    if (get_effective_ams_nozzle_idx(config) >= 0)
        modes.insert(modes.begin(), fmmAutoForFlush);
    return modes;
}

static std::map<NozzleVolumeType, int> get_fixed_nozzle_counts_from_full_stat(int ams_nozzle_idx, bool has_ams, const std::string& active_diameter, bool& has_full_stat)
{
    std::map<NozzleVolumeType, int> ret;
    auto preset_bundle = wxGetApp().preset_bundle;
    const ExtruderSlots &slots = preset_bundle->nozzle_inventory.slots(0);
    has_full_stat = !slots.empty();

    for (const NozzleSlot &slot : slots) {
        if (slot.empty()) continue;
        if (!active_diameter.empty() && format_diameter_to_str(std::atof(slot.diameter.c_str())) != active_diameter)
            continue;
        ret[slot.volume_type]++;
    }

    if (has_ams && !ret.empty() && ams_nozzle_idx >= 0 && ams_nozzle_idx < (int) slots.size()) {
        const NozzleSlot &ams_slot = slots[ams_nozzle_idx];
        const bool ams_counted = !ams_slot.empty() &&
            (active_diameter.empty() || format_diameter_to_str(std::atof(ams_slot.diameter.c_str())) == active_diameter);
        if (ams_counted) {
            NozzleVolumeType ams_vt = ams_slot.volume_type;
            auto it = ret.find(ams_vt);
            if (it != ret.end() && --(it->second) <= 0)
                ret.erase(it);
        }
    }
    return ret;
}

static std::optional<NozzleVolumeType> get_single_fixed_nozzle_volume_from_full_stat(int ams_nozzle_idx, bool has_ams, const std::string& active_diameter)
{
    bool has_full_stat = false;
    std::map<NozzleVolumeType, int> fixed_counts = get_fixed_nozzle_counts_from_full_stat(ams_nozzle_idx, has_ams, active_diameter, has_full_stat);
    if (!has_full_stat || fixed_counts.size() != 1)
        return std::nullopt;
    return fixed_counts.begin()->first;
}

bool should_skip_filament_grouping(const ConfigBase& config)
{
    if (!is_single_ext_multi_nozzle(config) || get_effective_ams_nozzle_idx(config) >= 0)
        return false;
    if (multi_nozzle_diameter_mode_enabled(config))
        return true;

    const auto* nd = dynamic_cast<const ConfigOptionFloatsNullable*>(config.option("nozzle_diameter"));
    const std::string active_diameter = (nd && !nd->values.empty()) ? format_diameter_to_str(nd->values.front()) : std::string();
    return get_single_fixed_nozzle_volume_from_full_stat(-1, false, active_diameter).has_value();
}

bool can_customize_filament_grouping(const ConfigBase& config)
{
    if (!is_single_ext_multi_nozzle(config))
        return true;

    const int ams_nozzle_idx = get_effective_ams_nozzle_idx_for_active_diameter(config);
    const ExtruderSlots &slots = wxGetApp().preset_bundle->nozzle_inventory.slots(0);
    if (multi_nozzle_diameter_mode_enabled(config)) {
        if (ams_nozzle_idx < 0 || ams_nozzle_idx >= int(slots.size()) || slots[ams_nozzle_idx].empty())
            return false;

        const NozzleSlot &ams_slot = slots[ams_nozzle_idx];
        return std::any_of(slots.begin(), slots.end(), [&](const NozzleSlot &slot) {
            return &slot != &ams_slot && !slot.empty() &&
                   format_diameter_to_str(string_to_double_decimal_point(slot.diameter)) ==
                       format_diameter_to_str(string_to_double_decimal_point(ams_slot.diameter)) &&
                   slot.volume_type == ams_slot.volume_type;
        });
    }

    const auto* nd = dynamic_cast<const ConfigOptionFloatsNullable*>(config.option("nozzle_diameter"));
    const std::string active_diameter = (nd && !nd->values.empty()) ? format_diameter_to_str(nd->values.front()) : std::string();
    bool has_full_stat = false;
    const auto fixed_counts = get_fixed_nozzle_counts_from_full_stat(
        ams_nozzle_idx, ams_nozzle_idx >= 0, active_diameter, has_full_stat);
    const size_t group_count = fixed_counts.size() + (ams_nozzle_idx >= 0 ? 1 : 0);
    return has_full_stat && group_count > 1;
}

static NozzleVolumeType get_ams_nozzle_volume_from_full_stat(int ams_nozzle_idx)
{
    auto preset_bundle = wxGetApp().preset_bundle;
    const ExtruderSlots &slots = preset_bundle->nozzle_inventory.slots(0);
    if (ams_nozzle_idx >= 0 && ams_nozzle_idx < (int) slots.size() && !slots[ams_nozzle_idx].empty())
        return slots[ams_nozzle_idx].volume_type;
    auto vals = preset_bundle->project_config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type")->values;
    return vals.empty() ? nvtStandard : NozzleVolumeType(vals[0]);
}

// ==================== Multi-diameter grouping restriction (GUI side) ====================
// Explicit per-filament selections take precedence over the preset's native compatible diameters.
// Without an explicit selection, preserve the existing native-support restriction.

static std::set<std::string> split_diameter_set(const std::string &joined)
{
    std::set<std::string> out;
    std::string           token;
    std::stringstream     ss(joined);
    while (std::getline(ss, token, ';'))
        if (!token.empty())
            out.insert(token);
    return out;
}

static std::vector<std::set<std::string>> get_filament_allowed_diameters(size_t filament_count)
{
    std::vector<std::set<std::string>> result(filament_count);
    const PresetBundle *preset_bundle = wxGetApp().preset_bundle;
    const auto joined = preset_bundle->compute_filament_supported_nozzle_diameters(filament_count);
    for (size_t i = 0; i < filament_count && i < joined.size(); ++i)
        result[i] = split_diameter_set(joined[i]);
    const bool supports_multi_diameter = is_single_ext_multi_nozzle(
        preset_bundle->printers.get_edited_preset().config);
    if (supports_multi_diameter && multi_nozzle_diameter_mode_enabled(preset_bundle->project_config)) {
        const auto *selected = preset_bundle->project_config.option<ConfigOptionStrings>("filament_diameter_map");
        if (selected == nullptr)
            return result;
        for (size_t i = 0; i < filament_count && i < selected->values.size(); ++i) {
            if (!selected->values[i].empty())
                result[i] = {format_diameter_to_str(string_to_double_decimal_point(selected->values[i]))};
        }
    }
    return result;
}

static std::vector<std::set<std::string>> get_backend_extruder_diameters()
{
    const PresetBundle *preset_bundle = wxGetApp().preset_bundle;
    const auto *fallback = preset_bundle->printers.get_edited_preset().config.option<ConfigOptionFloatsNullable>("nozzle_diameter");
    const NozzleInventory &nozzles = preset_bundle->nozzle_inventory;
    const size_t extruder_count = std::max<size_t>(nozzles.extruder_count(), fallback ? fallback->values.size() : 0);
    std::vector<std::set<std::string>> result(extruder_count);
    for (size_t e = 0; e < extruder_count; ++e) {
        for (const NozzleSlot &slot : nozzles.slots(int(e))) {
            const double diameter = string_to_double_decimal_point(slot.diameter);
            if (diameter > 0.0)
                result[e].insert(format_diameter_to_str(diameter));
        }
        if (result[e].empty() && fallback != nullptr && e < fallback->values.size() && fallback->values[e] > 0.0)
            result[e].insert(format_diameter_to_str(fallback->values[e]));
    }
    return result;
}

/**
 * \brief Diameter of the nozzles an extruder can slice with, or none if every installed nozzle can
 *
 * Without the multi-diameter mode, slicing disables the nozzles whose diameter differs from the extruder's.
 *
 * \param extruder_id  0-based extruder
 */
static std::optional<std::string> usable_nozzle_diameter(int extruder_id)
{
    const PresetBundle *preset_bundle = wxGetApp().preset_bundle;
    if (multi_nozzle_diameter_mode_enabled(preset_bundle->project_config))
        return std::nullopt;
    const auto *diameters = preset_bundle->printers.get_edited_preset().config.option<ConfigOptionFloatsNullable>("nozzle_diameter");
    if (diameters == nullptr || extruder_id < 0 || extruder_id >= int(diameters->values.size()))
        return std::nullopt;
    return format_diameter_to_str(diameters->values[extruder_id]);
}

/**
 * \brief Number of nozzles an extruder can slice with
 *
 * \param extruder_id  0-based extruder
 * \param type         volume type to count; none or Hybrid gives the total
 */
static int usable_nozzle_count(int extruder_id, std::optional<NozzleVolumeType> type = std::nullopt)
{
    return wxGetApp().preset_bundle->nozzle_inventory.count(extruder_id, type, usable_nozzle_diameter(extruder_id));
}

// Build a drag-drop accept predicate for a panel whose working diameter is target_diameter. Filaments
// with unknown native diameters stay unrestricted so nothing is accidentally blocked.
static std::function<bool(int)> make_diameter_accept_predicate(const std::vector<std::set<std::string>> &native_diameters,
                                                               const std::string                        &target_diameter)
{
    return [native_diameters, target_diameter](int filament_id) -> bool {
        const int idx = filament_id - 1;
        if (idx < 0 || idx >= static_cast<int>(native_diameters.size()) || native_diameters[idx].empty())
            return true;
        return native_diameters[idx].count(target_diameter) > 0;
    };
}

static std::function<bool(int)> make_diameter_accept_predicate(const std::vector<std::set<std::string>> &native_diameters,
                                                               const std::set<std::string>              &target_diameters)
{
    return [native_diameters, target_diameters](int filament_id) -> bool {
        const int idx = filament_id - 1;
        if (idx < 0 || idx >= static_cast<int>(native_diameters.size()) || native_diameters[idx].empty())
            return true;
        return std::any_of(target_diameters.begin(), target_diameters.end(),
                           [&native_diameters, idx](const std::string &diameter) {
                               return native_diameters[idx].count(diameter) > 0;
                           });
    };
}

static std::function<bool(int, const std::string &)> make_diameter_group_accept_predicate(
    const std::vector<std::set<std::string>> &native_diameters)
{
    return [native_diameters](int filament_id, const std::string &target_diameter) {
        const int idx = filament_id - 1;
        return idx < 0 || idx >= static_cast<int>(native_diameters.size()) || native_diameters[idx].empty() ||
               native_diameters[idx].count(target_diameter) > 0;
    };
}

static const wxColour BgNormalColor  = wxColour("#FFFFFF");
static const wxColour BgSelectColor  = wxColour("#EBF9F0");
static const wxColour BgDisableColor = wxColour("#CECECE");

static const wxColour BorderNormalColor   = wxColour("#CECECE");
static const wxColour BorderSelectedColor = wxColour("#00AE42");
static const wxColour BorderDisableColor  = wxColour("#EEEEEE");

static const wxColour TextNormalBlackColor = wxColour("#262E30");
static const wxColour TextNormalGreyColor = wxColour("#6B6B6B");
static const wxColour TextDisableColor = wxColour("#CECECE");
static const wxColour TextErrorColor = wxColour("#E14747");

wxDEFINE_EVENT(wxEVT_INVALID_MANUAL_MAP, wxCommandEvent);

void FilamentMapManualPanel::OnTimer(wxTimerEvent &)
{
    bool valid = true;
    int  invalid_eid = -1;
    NozzleVolumeType invalid_nozzle = NozzleVolumeType::nvtStandard;
    auto preset_bundle = wxGetApp().preset_bundle;
    auto proj_config = preset_bundle->project_config;
    auto nozzle_volume_values = proj_config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type")->values;
    std::vector<int> filament_map = GetFilamentMaps();
    std::vector<int> filament_volume_map = GetFilamentVolumeMaps();
    std::vector<std::string> selected_diameters;
    if (const auto *selected = preset_bundle->project_config.option<ConfigOptionStrings>("filament_diameter_map"))
        selected_diameters = selected->values;
    const std::string printer_model = preset_bundle->printers.get_edited_preset().config.opt_string("printer_model");
    const std::string printer_preset = preset_bundle->printers.get_edited_preset().name;
    bool restrictions_changed = false;
    if (selected_diameters != m_cached_selected_diameters || preset_bundle->filament_presets != m_cached_filament_presets ||
        printer_model != m_cached_printer_model || printer_preset != m_cached_printer_preset) {
        m_allowed_diameters = get_filament_allowed_diameters(m_filament_map.size());
        m_cached_selected_diameters = std::move(selected_diameters);
        m_cached_filament_presets = preset_bundle->filament_presets;
        m_cached_printer_model = printer_model;
        m_cached_printer_preset = printer_preset;
        restrictions_changed = true;
    }
    std::vector<std::set<std::string>> extruder_diameters = get_backend_extruder_diameters();
    if (extruder_diameters != m_extruder_diameters) {
        m_extruder_diameters = std::move(extruder_diameters);
        restrictions_changed = true;
    }
    if (restrictions_changed) {
        const std::set<std::string> left_diameters =
            m_extruder_diameters.size() > 0 ? m_extruder_diameters[0] : std::set<std::string>{};
        const std::set<std::string> right_diameters =
            m_extruder_diameters.size() > 1 ? m_extruder_diameters[1] : std::set<std::string>{};
        m_left_panel->set_accept_filament_predicate(left_diameters.empty() ? std::function<bool(int)>{} :
            make_diameter_accept_predicate(m_allowed_diameters, left_diameters));
        m_right_panel->set_accept_filament_predicate(right_diameters.empty() ? std::function<bool(int)>{} :
            make_diameter_accept_predicate(m_allowed_diameters, right_diameters));
    }

    // Multi-diameter safety net: flag any filament placed on an extruder whose working diameter its
    // preset does not support. The drag-drop restriction prevents this interactively; this also catches
    // a stale map loaded from a project whose diameters changed.
    wxString diameter_error;
    for (int fid : m_filament_list) {
        const int idx = fid - 1;
        if (idx < 0 || idx >= static_cast<int>(m_allowed_diameters.size()) || m_allowed_diameters[idx].empty())
            continue;
        const int eid = filament_map[idx] - 1;
        if (eid < 0 || eid >= static_cast<int>(m_extruder_diameters.size()))
            continue;
        const std::set<std::string> &extruder_diameters = m_extruder_diameters[eid];
        const bool has_compatible_diameter = std::any_of(
            extruder_diameters.begin(), extruder_diameters.end(),
            [this, idx](const std::string &diameter) { return m_allowed_diameters[idx].count(diameter) > 0; });
        if (!extruder_diameters.empty() && !has_compatible_diameter) {
            diameter_error = _L("Error: a filament is assigned to a nozzle of incompatible diameter. "
                                "Please regroup so each filament stays on a nozzle whose diameter it supports.");
            break;
        }
    }

    std::vector<int>nozzle_count(nozzle_volume_values.size());
    for (size_t eid = 0; eid < nozzle_volume_values.size(); ++eid) {
        NozzleVolumeType extruder_volume_type = NozzleVolumeType(nozzle_volume_values[eid]);
        bool extruder_used = std::find_if(m_filament_list.begin(), m_filament_list.end(),
            [this, eid, filament_map](int fid) {
                return (filament_map[fid - 1] - 1) == eid;
            }) != m_filament_list.end();

        if (!extruder_used) {
            continue;
        }

        if (extruder_volume_type == nvtHybrid) {
            int standard_count = preset_bundle->nozzle_inventory.count(eid, NozzleVolumeType::nvtStandard);
            int highflow_count = preset_bundle->nozzle_inventory.count(eid, NozzleVolumeType::nvtHighFlow);

            auto has_material_of_type = [this, eid, &filament_map, &filament_volume_map](NozzleVolumeType volume_type) {
                return std::find_if(m_filament_list.begin(), m_filament_list.end(),
                    [this, eid, &filament_map, &filament_volume_map, volume_type](int fid) {
                        return (filament_map[fid - 1] - 1) == eid &&
                               fid - 1 < filament_volume_map.size() &&
                               filament_volume_map[fid - 1] == static_cast<int>(volume_type);
                    }) != m_filament_list.end();
            };

            bool has_standard = has_material_of_type(NozzleVolumeType::nvtStandard);
            bool has_highflow = has_material_of_type(NozzleVolumeType::nvtHighFlow);

            if ((has_standard && standard_count == 0) ||
                (has_highflow && highflow_count == 0)) {
                auto nozzle_opt = preset_bundle->printers.get_edited_preset()
                                      .config.option<ConfigOptionFloatsNullable>("nozzle_diameter");
                bool has_configured_nozzle = nozzle_opt && eid < nozzle_opt->size()
                                             && nozzle_opt->get_at(eid) > 0;
                if (!has_configured_nozzle) {
                    valid = false;
                    invalid_eid = eid;
                    invalid_nozzle = (has_standard && standard_count == 0) ? NozzleVolumeType::nvtStandard : NozzleVolumeType::nvtHighFlow;
                    break;
                }
            }
        } else {
            int count = preset_bundle->nozzle_inventory.count(eid, extruder_volume_type);
            if (count == 0) {
                // When mixing nozzle diameters, an extruder may have 0 nozzles of the
                // "selected" diameter but still have a usable nozzle installed (just a
                // different size).  Check the configured nozzle_diameter — if it is
                // positive the extruder is physically present and should be allowed.
                auto nozzle_opt = preset_bundle->printers.get_edited_preset()
                                      .config.option<ConfigOptionFloatsNullable>("nozzle_diameter");
                bool has_configured_nozzle = nozzle_opt && eid < nozzle_opt->size()
                                             && nozzle_opt->get_at(eid) > 0;
                if (!has_configured_nozzle) {
                    valid = false;
                    invalid_eid = eid;
                    invalid_nozzle = extruder_volume_type;
                    break;
                }
            }
        }
    }

    // A diameter mismatch invalidates the grouping regardless of nozzle availability.
    if (!diameter_error.empty())
        valid = false;

    bool update_ui  = (m_invalid_id != invalid_eid) || (m_diameter_error != diameter_error);
    bool send_event = update_ui || m_force_validation;

    m_invalid_id     = invalid_eid;
    m_diameter_error = diameter_error;

    if (update_ui) {
        if (valid) {
            m_errors->Hide();
            m_suggestion_panel->Hide();
        } else if (!diameter_error.empty()) {
            // Diameter mismatch: the "set nozzle count" suggestion does not apply, so show only the error.
            m_errors->SetLabel(diameter_error);
            m_errors->Wrap(FromDIP(520));
            m_errors->Show();
            m_suggestion_panel->Hide();
        } else {
            m_errors->SetLabel(wxString::Format(_L("Error: %s extruder has no available %s nozzle, current group result is invalid."),
                                                invalid_eid == 0 ? _L("Left") : _L("Right"), invalid_nozzle == NozzleVolumeType::nvtStandard ? _L("Standard") : _L("High Flow")));
            m_errors->Wrap(FromDIP(520));
            m_errors->Show();
            m_suggestion_panel->Show();
        }
        m_left_panel->Freeze();
        m_right_panel->Freeze();
        m_tips->Freeze();
        m_description->Freeze();
        Layout();
        Fit();
        this->GetParent()->Layout();
        this->GetParent()->Fit();
        m_left_panel->Thaw();
        m_right_panel->Thaw();
        m_tips->Thaw();
        m_description->Thaw();
    }

    if (send_event) {
        wxCommandEvent event(wxEVT_INVALID_MANUAL_MAP);
        event.SetInt(valid);
        ProcessEvent(event);
        m_force_validation = false;
    }
}

void FilamentMapManualPanel::OnSuggestionClicked(wxCommandEvent &event)
{
    wxWindow *current = this;
    while (current && !wxDynamicCast(current, wxDialog)) {
        current = current->GetParent();
    }

    if (current) {
        wxDialog *dlg = wxDynamicCast(current, wxDialog);
        if (dlg) {
            int invalid_eid = m_invalid_id;
            dlg->EndModal(wxID_CANCEL);

            if (invalid_eid == 0) {
                manuallySetNozzleCount(0);
            } else if (invalid_eid == 1) {
                manuallySetNozzleCount(1);
            }
            wxGetApp().plater()->update();
        }
    }
}

std::vector<int> FilamentMapManualPanel::GetFilamentMaps() const
{
    std::vector<int> new_filament_map = m_filament_map;
    std::vector<int> left_filaments  = this->GetLeftFilaments();
    std::vector<int> right_filaments = this->GetRightFilaments();

        for (int i = 0; i < new_filament_map.size(); ++i) {
            if (std::find(left_filaments.begin(), left_filaments.end(), i + 1) != left_filaments.end()) {
                new_filament_map[i] = 1;
            } else if (std::find(right_filaments.begin(), right_filaments.end(), i + 1) != right_filaments.end()) {
                new_filament_map[i] = 2;
            }
        }
    return new_filament_map;
}

std::vector<int> FilamentMapManualPanel::GetFilamentVolumeMaps() const
{
    std::vector<int> volume_map(m_filament_map.size(), 0);

    std::vector<int> left_filaments = this->GetLeftFilaments();
    std::vector<int> right_high_flow_filaments = this->GetRightHighFlowFilaments();
    std::vector<int> right_standard_filaments = this->GetRightStandardFilaments();
    std::vector<int> right_tpu_high_flow_filaments = this->GetRightTPUHighFlowFilaments();

    auto preset_bundle = wxGetApp().preset_bundle;
    auto proj_config = preset_bundle->project_config;
    auto nozzle_volume_values = proj_config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type")->values;
    const int right_extruder_id = 1;
    const bool right_e3d_high_flow = nozzle_volume_values.size() > right_extruder_id &&
        nozzle_volume_values[right_extruder_id] == static_cast<int>(NozzleVolumeType::nvtE3DHighFlow);
    const int right_high_flow_volume = static_cast<int>(right_e3d_high_flow ?
        NozzleVolumeType::nvtE3DHighFlow : NozzleVolumeType::nvtHighFlow);

    for (int i = 0; i < volume_map.size(); ++i) {
        int filament_id = i + 1;

        if (std::find(left_filaments.begin(), left_filaments.end(), filament_id) != left_filaments.end()) {
            if (nozzle_volume_values.size() > 0) {
                volume_map[i] = nozzle_volume_values[0];
            }
        }
        else if (std::find(right_high_flow_filaments.begin(), right_high_flow_filaments.end(), filament_id) != right_high_flow_filaments.end()) {
            volume_map[i] = right_high_flow_volume;
        }
        else if (std::find(right_standard_filaments.begin(), right_standard_filaments.end(), filament_id) != right_standard_filaments.end()) {
            volume_map[i] = static_cast<int>(NozzleVolumeType::nvtStandard);
        }
        else if (std::find(right_tpu_high_flow_filaments.begin(), right_tpu_high_flow_filaments.end(), filament_id) != right_tpu_high_flow_filaments.end()) {
            volume_map[i] = static_cast<int>(NozzleVolumeType::nvtTPUHighFlow);
        }
    }

    return volume_map;
}

void FilamentMapManualPanel::SyncPanelHeights()
{
    if (!m_left_panel || !m_right_panel) return;

    auto curr_left = m_left_panel->GetMinSize();
    auto curr_right = m_right_panel->GetMinSize();

    m_left_panel->SetMinSize(wxSize(FromDIP(260), FromDIP(110)));
    m_right_panel->SetMinSize(wxSize(FromDIP(260), FromDIP(110)));

    m_left_panel->Layout();
    m_left_panel->Fit();
    m_right_panel->Layout();
    m_right_panel->Fit();

    wxSize left_best_size  = m_left_panel->GetBestSize();
    wxSize right_best_size = m_right_panel->GetBestSize();

    int max_height = std::max(left_best_size.GetHeight(), right_best_size.GetHeight());
    bool height_changed = curr_left.GetHeight() != max_height || curr_right.GetHeight() != max_height;
    if (!height_changed) {
        if (curr_left.GetHeight() > 0)
            m_left_panel->SetMinSize(curr_left);
        if (curr_right.GetHeight() > 0)
            m_right_panel->SetMinSize(curr_right);
        if (GetParent()) {
            GetParent()->Layout();
            GetParent()->Fit();
        }
        return;
    }

    m_left_panel->SetMinSize(wxSize(FromDIP(260), max_height));
    m_right_panel->SetMinSize(wxSize(FromDIP(260), max_height));

    Layout();
    Fit();

    if (GetParent()) {
        GetParent()->Layout();
        GetParent()->Fit();
    }
}

void FilamentMapManualPanel::OnDragDropCompleted(wxCommandEvent& event)
{
    SyncPanelHeights();
    event.Skip();
}

FilamentMapManualPanel::FilamentMapManualPanel(wxWindow                       *parent,
                                               const std::vector<std::string> &type,
                                               const std::vector<int>         &filament_list,
                                               const std::vector<int>         &filament_map,
                                               const std::vector<int>         &filament_volume_map)
    : FilamentMapPanel(parent)
    , m_filament_map(filament_map)
    , m_filament_volume_map(filament_volume_map)
    , m_filament_list(filament_list)
    , m_filament_type(type)
{
    m_allowed_diameters  = get_filament_allowed_diameters(m_filament_map.size());
    m_extruder_diameters = get_backend_extruder_diameters();
    if (const auto *selected = wxGetApp().preset_bundle->project_config.option<ConfigOptionStrings>(
            "filament_diameter_map"))
        m_cached_selected_diameters = selected->values;
    m_cached_filament_presets = wxGetApp().preset_bundle->filament_presets;
    m_cached_printer_model = wxGetApp().preset_bundle->printers.get_edited_preset().config.opt_string("printer_model");
    m_cached_printer_preset = wxGetApp().preset_bundle->printers.get_edited_preset().name;

    SetName(wxT("FilamentMapManualPanel"));
    SetBackgroundColour(BgNormalColor);

    auto top_sizer = new wxBoxSizer(wxVERTICAL);

    m_description = new Label(this, _L("We will slice according to this grouping method:"));
    top_sizer->Add(m_description, 0, wxALIGN_LEFT | wxLEFT, FromDIP(15));
    m_description->Wrap(FromDIP(520));
    top_sizer->AddSpacer(FromDIP(8));

    auto drag_sizer = new wxBoxSizer(wxHORIZONTAL);

    std::string pt_fmp = wxGetApp().preset_bundle->printers.get_edited_preset().get_printer_type(wxGetApp().preset_bundle);
    m_left_panel  = new DragDropPanel(this, _L(DevPrinterConfigUtil::get_toolhead_display_name(pt_fmp, DEPUTY_EXTRUDER_ID, ToolHeadComponent::Extruder, ToolHeadNameCase::TitleCase)), false);
    m_right_panel = new SeparatedDragDropPanel(this, _L(DevPrinterConfigUtil::get_toolhead_display_name(pt_fmp, MAIN_EXTRUDER_ID, ToolHeadComponent::Extruder, ToolHeadNameCase::TitleCase)), false);
    m_switch_btn  = new ScalableButton(this, wxID_ANY, "switch_filament_maps");

    UpdateNozzleVolumeType();

    for (size_t idx = 0; idx < m_filament_map.size(); ++idx) {
        auto iter = std::find(m_filament_list.begin(), m_filament_list.end(), idx + 1);
        if (iter == m_filament_list.end()) continue;
        std::string type = m_filament_type[idx];
        if (m_filament_map[idx] == 1) {
            m_left_panel->AddColorBlock(type, idx + 1);
        } else {
            assert(m_filament_map[idx] == 2);
            NozzleVolumeType vol_type = (idx < m_filament_volume_map.size()) ? NozzleVolumeType(m_filament_volume_map[idx]) : nvtStandard;
            m_right_panel->AddColorBlock(type, idx + 1, vol_type);
        }
    }

    // Multi-diameter restriction: an extruder accepts a filament when any installed physical nozzle
    // supports its selected diameter. left_panel = extruder 0 (deputy), right_panel = extruder 1 (main).
    const std::set<std::string> left_diameters =
        m_extruder_diameters.size() > 0 ? m_extruder_diameters[0] : std::set<std::string>{};
    const std::set<std::string> right_diameters =
        m_extruder_diameters.size() > 1 ? m_extruder_diameters[1] : std::set<std::string>{};
    if (!left_diameters.empty())
        m_left_panel->set_accept_filament_predicate(make_diameter_accept_predicate(m_allowed_diameters, left_diameters));
    if (!right_diameters.empty())
        m_right_panel->set_accept_filament_predicate(make_diameter_accept_predicate(m_allowed_diameters, right_diameters));

    m_left_panel->SetMinSize({FromDIP(260), FromDIP(110)});
    m_right_panel->SetMinSize({FromDIP(260), FromDIP(110)});

    drag_sizer->Add(m_left_panel, 1, wxALIGN_CENTER | wxEXPAND);
    drag_sizer->Add(m_switch_btn, 0, wxALIGN_CENTER | wxLEFT | wxRIGHT, FromDIP(8));
    drag_sizer->Add(m_right_panel, 1, wxALIGN_CENTER | wxEXPAND);

    top_sizer->Add(drag_sizer, 0, wxALIGN_CENTER | wxEXPAND);

    m_tips = new Label(this, _L("Tips: You can drag the filaments to reassign them to different nozzles."));
    m_tips->SetFont(Label::Body_13);
    m_tips->SetForegroundColour(TextNormalGreyColor);
    m_tips->Wrap(FromDIP(520));
    top_sizer->AddSpacer(FromDIP(20));
    top_sizer->Add(m_tips, 0, wxALIGN_LEFT | wxLEFT, FromDIP(15));

    m_errors = new Label(this, "");
    m_errors->SetFont(Label::Body_13);
    m_errors->SetForegroundColour(TextErrorColor);
    m_errors->Wrap(FromDIP(520));
    top_sizer->AddSpacer(FromDIP(10));
    top_sizer->Add(m_errors, 0, wxALIGN_LEFT | wxLEFT, FromDIP(15));

    m_errors->Hide();

    m_suggestion_panel = new wxPanel(this, wxID_ANY);
    m_suggestion_panel->SetBackgroundColour(*wxWHITE);
    auto suggestion_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto suggestion_text  = new Label(m_suggestion_panel, _L("Please adjust your grouping or click "));
    suggestion_text->SetFont(Label::Body_13);
    suggestion_text->SetForegroundColour(TextErrorColor);
    suggestion_text->SetBackgroundColour(*wxWHITE);
    auto suggestion_btn   = new ScalableButton(m_suggestion_panel, wxID_ANY, "edit", wxEmptyString, wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER, true, 14);
    suggestion_btn->SetBackgroundColour(*wxWHITE);
    auto suggestion_text2 = new Label(m_suggestion_panel, _L(" to set nozzle count"));
    suggestion_text2->SetFont(Label::Body_13);
    suggestion_text2->SetForegroundColour(TextErrorColor);
    suggestion_text2->SetBackgroundColour(*wxWHITE);
    suggestion_sizer->Add(suggestion_text, 0, wxALIGN_CENTER_VERTICAL);
    suggestion_sizer->Add(suggestion_btn, 0, wxALIGN_CENTER_VERTICAL);
    suggestion_sizer->Add(suggestion_text2, 0, wxALIGN_CENTER_VERTICAL);
    m_suggestion_panel->SetSizer(suggestion_sizer);
    top_sizer->Add(m_suggestion_panel, 0, wxALIGN_LEFT | wxLEFT, FromDIP(15));
    m_suggestion_panel->Hide();
    suggestion_btn->Bind(wxEVT_BUTTON, &FilamentMapManualPanel::OnSuggestionClicked, this);

    m_timer = new wxTimer(this);
    Bind(wxEVT_TIMER, &FilamentMapManualPanel::OnTimer, this);
    Bind(wxEVT_DRAG_DROP_COMPLETED, &FilamentMapManualPanel::OnDragDropCompleted, this);

    m_switch_btn->Bind(wxEVT_BUTTON, &FilamentMapManualPanel::OnSwitchFilament, this);

    SetSizer(top_sizer);
    SetMinSize(wxSize(FromDIP(580), -1));
    Layout();
    Fit();
    GUI::wxGetApp().UpdateDarkUIWin(this);
}

void FilamentMapManualPanel::UpdateNozzleVolumeType()
{
    auto check_separation = []() {
        auto preset_bundle = wxGetApp().preset_bundle;
        auto nozzle_volume_values = preset_bundle->project_config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type")->values;
        if (nozzle_volume_values.size() <= 1)
            return false;

        return nozzle_volume_values[1] == static_cast<int>(NozzleVolumeType::nvtHybrid);
    };
    bool should_separate = check_separation();
    m_right_panel->SetUseSeparation(should_separate);

    UpdateNozzleCountDisplay();

    Layout();
    Fit();
}

void FilamentMapManualPanel::UpdateNozzleCountDisplay()
{
    auto preset_bundle = wxGetApp().preset_bundle;

    std::string fmp_up_pt = preset_bundle->printers.get_edited_preset().get_printer_type(preset_bundle);
    wxString dep_ext_name = _L(DevPrinterConfigUtil::get_toolhead_display_name(fmp_up_pt, DEPUTY_EXTRUDER_ID, ToolHeadComponent::Extruder, ToolHeadNameCase::TitleCase));
    wxString main_ext_name = _L(DevPrinterConfigUtil::get_toolhead_display_name(fmp_up_pt, MAIN_EXTRUDER_ID, ToolHeadComponent::Extruder, ToolHeadNameCase::TitleCase));

    int left_count = usable_nozzle_count(0);
    wxString left_title = wxString::Format(dep_ext_name + "(%d)", left_count);
    m_left_panel->UpdateLabel(left_title);

    if (m_right_panel->IsUseSeparation()) {
        int standard_count = usable_nozzle_count(1, NozzleVolumeType::nvtStandard);
        int highflow_count = usable_nozzle_count(1, NozzleVolumeType::nvtHighFlow);
        int tpu_count = usable_nozzle_count(1, NozzleVolumeType::nvtTPUHighFlow) + usable_nozzle_count(1, NozzleVolumeType::nvtTPU285);
        wxString right_title;
        if (tpu_count > 0)
            right_title = wxString::Format(main_ext_name + "(Std: %d, HF: %d, TPU285: %d)", standard_count, highflow_count, tpu_count);
        else
            right_title = wxString::Format(main_ext_name + "(Std: %d, HF: %d)", standard_count, highflow_count);
        m_right_panel->UpdateLabel(right_title);
    } else {
        int right_count = usable_nozzle_count(1);
        wxString right_title = wxString::Format(main_ext_name + "(%d)", right_count);
        m_right_panel->UpdateLabel(right_title);
    }
}

FilamentMapManualPanel::~FilamentMapManualPanel()
{
    m_timer->Stop();
}

void FilamentMapManualPanel::OnSwitchFilament(wxCommandEvent &)
{
    auto left_blocks  = m_left_panel->get_filament_blocks();
    auto right_blocks = m_right_panel->get_filament_blocks();

    for (auto &block : left_blocks) {
        m_right_panel->AddColorBlock(block->GetType(), block->GetFilamentId(), nvtStandard, false);
        m_left_panel->RemoveColorBlock(block, false);
    }

    for (auto &block : right_blocks) {
        m_left_panel->AddColorBlock(block->GetType(), block->GetFilamentId(), false);
        m_right_panel->RemoveColorBlock(block, false);
    }
    this->GetParent()->Layout();
    this->GetParent()->Fit();

    if (m_right_panel->IsUseSeparation()) {
        m_left_panel->Layout();
        m_left_panel->Fit();
        m_right_panel->Layout();
        m_right_panel->Fit();
        SyncPanelHeights();
    }
}

bool FilamentMapManualPanel::Show(bool show)
{
    m_force_validation = show;
    if (show) {
        m_timer->Start(500);
        SyncPanelHeights();
    } else {
        m_timer->Stop();
    }

    const bool ret = FilamentMapPanel::Show(show);
    // Validate at once rather than on the first tick: until then OK stays enabled for an invalid grouping.
    if (show) {
        wxTimerEvent evt;
        OnTimer(evt);
    }
    return ret;
}

// ===================== FilamentMapMultiNozzlePanel =====================

static constexpr int AMS_PANEL_MIN_WIDTH   = 160;
static constexpr int FIXED_PANEL_MIN_WIDTH = 360;
// 列宽由盒宽均分，默认 6 列在 AMS 盒宽度下小于图标宽度，图标之间就没有间隙。
static constexpr int AMS_PANEL_GRID_COLS   = 4;
static constexpr int PANEL_MIN_WIDTH       = 580;
static constexpr int DRAG_BOX_MARGIN       = 15;

static wxString get_volume_type_label(NozzleVolumeType type)
{
    switch (type) {
    case nvtStandard:    return _L("Standard");
    case nvtHighFlow:    return _L("High Flow");
    case nvtTPUHighFlow: return _L("TPU285");
    case nvtTPU285:      return _L("TPU285");
    default:             return _L("Standard");
    }
}

FilamentMapMultiNozzlePanel::FilamentMapMultiNozzlePanel(wxWindow                       *parent,
                                         const std::vector<std::string> &type,
                                         const std::vector<int>         &filament_list,
                                         const std::vector<int>         &filament_map,
                                         const std::vector<int>         &filament_volume_map)
    : FilamentMapPanel(parent)
    , m_filament_map(filament_map)
    , m_filament_volume_map(filament_volume_map)
    , m_filament_list(filament_list)
    , m_filament_type(type)
{
    SetName(wxT("FilamentMapMultiNozzlePanel"));
    SetBackgroundColour(BgNormalColor);

    auto preset_bundle = wxGetApp().preset_bundle;
    m_allowed_diameters = get_filament_allowed_diameters(m_filament_map.size());
    if (const auto *selected = preset_bundle->project_config.option<ConfigOptionStrings>("filament_diameter_map"))
        m_cached_selected_diameters = selected->values;
    m_cached_filament_presets = preset_bundle->filament_presets;
    m_cached_printer_model = preset_bundle->printers.get_edited_preset().config.opt_string("printer_model");
    m_cached_printer_preset = preset_bundle->printers.get_edited_preset().name;
    auto full_cfg = preset_bundle->full_config();
    auto cnt_opt = full_cfg.option<ConfigOptionIntsNullable>("extruder_max_nozzle_count");
    m_total_nozzle_count = (cnt_opt && !cnt_opt->values.empty()) ? cnt_opt->values[0] : 1;
    const bool multi_diameter_mode = multi_nozzle_diameter_mode_enabled(preset_bundle->project_config);
    m_show_drag_unsupported_tip = is_single_ext_multi_nozzle(full_cfg) && multi_diameter_mode;
    m_ams_nozzle_idx = get_effective_ams_nozzle_idx_for_active_diameter(full_cfg);
    bool has_ams = (m_ams_nozzle_idx >= 0);

    auto top_sizer = new wxBoxSizer(wxVERTICAL);

    m_description = new Label(this, _L("We will slice according to this arrangement:"));
    top_sizer->Add(m_description, 0, wxALIGN_LEFT | wxLEFT, FromDIP(15));
    m_description->Wrap(FromDIP(520));
    top_sizer->AddSpacer(FromDIP(8));

    auto drag_sizer = new wxBoxSizer(wxHORIZONTAL);

    if (has_ams) {
        m_left_panel  = new SeparatedDragDropPanel(this, get_ams_nozzle_label(m_ams_nozzle_idx), false,
                                                   true, true, true);
        m_right_panel = new SeparatedDragDropPanel(
            this, get_fixed_nozzle_label(m_total_nozzle_count, m_ams_nozzle_idx), false, false, false, true);
    } else {
        m_left_panel  = nullptr;
        wxString panel_label = wxString::Format(_L("Nozzle (%d)"), m_total_nozzle_count);
        m_right_panel = new SeparatedDragDropPanel(this, panel_label, false, false, false, true);
    }

    std::optional<std::pair<std::string, NozzleVolumeType>> ams_drag_group;
    if (multi_diameter_mode) {
        std::vector<SeparatedDragDropPanel::DiameterVolumeGroup> groups;
        std::set<std::pair<std::string, NozzleVolumeType>> seen;
        const ExtruderSlots &slots = preset_bundle->nozzle_inventory.slots(0);
        for (size_t idx = 0; idx < slots.size(); ++idx) {
            if (static_cast<int>(idx) == m_ams_nozzle_idx || slots[idx].empty())
                continue;
            const double diameter_value = string_to_double_decimal_point(slots[idx].diameter);
            if (diameter_value <= 0.0)
                continue;
            const std::string diameter = format_diameter_to_str(diameter_value);
            const auto volume_type = slots[idx].volume_type;
            if (!seen.emplace(diameter, volume_type).second)
                continue;
            groups.push_back({diameter, volume_type,
                              wxString::FromUTF8(diameter) + get_volume_type_label(volume_type)});
        }
        if (!groups.empty())
            m_right_panel->SetDiameterVolumeGroups(groups);

        if (m_left_panel && m_ams_nozzle_idx >= 0 && m_ams_nozzle_idx < static_cast<int>(slots.size())) {
            const NozzleSlot &ams_slot = slots[m_ams_nozzle_idx];
            const double diameter_value = string_to_double_decimal_point(ams_slot.diameter);
            if (diameter_value > 0.0 && !ams_slot.empty()) {
                const std::string diameter = format_diameter_to_str(diameter_value);
                const auto volume_type = ams_slot.volume_type;
                m_left_panel->UpdateUnifiedLabel(
                    wxString::FromUTF8(diameter) + get_volume_type_label(volume_type));
                ams_drag_group = std::make_pair(diameter, volume_type);
            }
        }
    }

    UpdateNozzleVolumeType();

    for (size_t idx = 0; idx < m_filament_map.size(); ++idx) {
        auto iter = std::find(m_filament_list.begin(), m_filament_list.end(), idx + 1);
        if (iter == m_filament_list.end()) continue;
        std::string tp = m_filament_type[idx];
        NozzleVolumeType vol_type = (idx < m_filament_volume_map.size()) ? NozzleVolumeType(m_filament_volume_map[idx]) : nvtStandard;
        const std::string diameter = idx < m_cached_selected_diameters.size() && !m_cached_selected_diameters[idx].empty()
            ? format_diameter_to_str(string_to_double_decimal_point(m_cached_selected_diameters[idx]))
            : std::string();
        bool belongs_to_ams = has_ams && m_filament_map[idx] == 1;
        if (belongs_to_ams && multi_diameter_mode && ams_drag_group)
            belongs_to_ams = diameter == ams_drag_group->first &&
                             vol_type == ams_drag_group->second;
        if (belongs_to_ams) {
            m_left_panel->AddColorBlock(tp, idx + 1, vol_type, true, diameter);
        } else {
            m_right_panel->AddColorBlock(tp, idx + 1, vol_type, true, diameter);
        }
    }

    if (multi_diameter_mode) {
        // Multi-diameter restriction (N1X): the AMS panel represents its physical nozzle, while the fixed
        // panel may contain several configured nozzle diameters.
        const ExtruderSlots &slots = preset_bundle->nozzle_inventory.slots(0);
        std::set<std::string> ams_diameters;
        std::set<std::string> fixed_diameters;
        for (size_t i = 0; i < slots.size(); ++i) {
            const double diameter = string_to_double_decimal_point(slots[i].diameter);
            if (diameter <= 0.0)
                continue;
            auto &target = static_cast<int>(i) == m_ams_nozzle_idx ? ams_diameters : fixed_diameters;
            target.insert(format_diameter_to_str(diameter));
        }
        if (m_left_panel && m_left_panel->IsUseDiameterVolumeGroups())
            m_left_panel->set_diameter_group_accept_filament_predicate(
                make_diameter_group_accept_predicate(m_allowed_diameters));
        else if (m_left_panel && !ams_diameters.empty())
            m_left_panel->set_accept_filament_predicate(make_diameter_accept_predicate(m_allowed_diameters, ams_diameters));
        if (m_right_panel->IsUseDiameterVolumeGroups())
            m_right_panel->set_diameter_group_accept_filament_predicate(
                make_diameter_group_accept_predicate(m_allowed_diameters));
        else if (!fixed_diameters.empty())
            m_right_panel->set_accept_filament_predicate(make_diameter_accept_predicate(m_allowed_diameters, fixed_diameters));
    }

    if (has_ams) {
        m_left_panel->SetUnifiedGridColumns(AMS_PANEL_GRID_COLS);
        if (multi_diameter_mode)
            m_left_panel->SetUnifiedGridHorizontalPadding(FromDIP(8), FromDIP(4));
        m_left_panel->SetMinSize({FromDIP(AMS_PANEL_MIN_WIDTH), -1});
        m_right_panel->SetMinSize({FromDIP(FIXED_PANEL_MIN_WIDTH), -1});
        drag_sizer->AddSpacer(FromDIP(DRAG_BOX_MARGIN));
        drag_sizer->Add(m_left_panel, 3, wxALIGN_CENTER | wxEXPAND);
        drag_sizer->AddSpacer(FromDIP(12));
        drag_sizer->Add(m_right_panel, 7, wxALIGN_CENTER | wxEXPAND);
        drag_sizer->AddSpacer(FromDIP(DRAG_BOX_MARGIN));
    } else {
        m_right_panel->SetMinSize({FromDIP(460), -1});
        m_right_panel->SetUnifiedGridWidth(FromDIP(PANEL_MIN_WIDTH - 2 * DRAG_BOX_MARGIN));
        drag_sizer->Add(m_right_panel, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(DRAG_BOX_MARGIN));
    }

    top_sizer->Add(drag_sizer, 0, wxEXPAND);

    const bool single_ext_multi_nozzle = is_single_ext_multi_nozzle(full_cfg);
    wxString tips = _L("Tips: You can drag the filaments to reassign them to different nozzles.");
    if (single_ext_multi_nozzle && !has_ams && !multi_diameter_mode && !m_right_panel->IsUseSeparation()) {
        const auto *nozzle_diameters = full_cfg.option<ConfigOptionFloatsNullable>("nozzle_diameter");
        if (nozzle_diameters && !nozzle_diameters->values.empty()) {
            const wxString diameter = wxString::FromUTF8(
                format_diameter_to_str(nozzle_diameters->values.front()));
            tips = wxString::Format(
                _L("Tips: Under a %smm nozzle diameter, there are no mixed nozzle types, so no custom settings are needed."),
                diameter);
        }
    }
    m_tips = new Label(this, tips);
    m_tips->SetFont(Label::Body_13);
    m_tips->SetForegroundColour(TextNormalGreyColor);
    m_tips->Wrap(FromDIP(520));
    top_sizer->AddSpacer(FromDIP(20));
    top_sizer->Add(m_tips, 0, wxALIGN_LEFT | wxLEFT, FromDIP(15));

    m_errors = new Label(this, "");
    m_errors->SetFont(Label::Body_13);
    m_errors->SetForegroundColour(TextErrorColor);
    m_errors->Wrap(FromDIP(520));
    top_sizer->AddSpacer(FromDIP(10));
    top_sizer->Add(m_errors, 0, wxALIGN_LEFT | wxLEFT, FromDIP(15));
    m_errors->Hide();

    m_timer = new wxTimer(this);
    Bind(wxEVT_TIMER, &FilamentMapMultiNozzlePanel::OnTimer, this);
    Bind(wxEVT_DRAG_DROP_COMPLETED, &FilamentMapMultiNozzlePanel::OnDragDropCompleted, this);
    Bind(wxEVT_FILAMENT_DRAG_HOVER, &FilamentMapMultiNozzlePanel::OnDragHover, this);

    SetSizer(top_sizer);
    SetMinSize(wxSize(FromDIP(PANEL_MIN_WIDTH), -1));
    SetMaxSize(wxSize(FromDIP(PANEL_MIN_WIDTH), -1));
    Layout();
    Fit();
    GUI::wxGetApp().UpdateDarkUIWin(this);
    if (ams_drag_group)
        m_right_panel->SetEnabledDiameterVolumeGroup(ams_drag_group->first, ams_drag_group->second);
    if (m_right_panel->IsUseDiameterVolumeGroups()) {
        m_right_panel->UpdateDiameterVolumeLayout();
        SyncPanelHeights();
    }
}

FilamentMapMultiNozzlePanel::~FilamentMapMultiNozzlePanel()
{
    m_timer->Stop();
}

std::vector<int> FilamentMapMultiNozzlePanel::GetFilamentMaps() const
{
    std::vector<int> new_filament_map = m_filament_map;

    if (!m_left_panel) {
        // No AMS: single extruder, all filaments on extruder 1
        std::fill(new_filament_map.begin(), new_filament_map.end(), 1);
        return new_filament_map;
    }

    std::vector<int> left_filaments  = this->GetLeftFilaments();
    std::vector<int> right_filaments = this->GetRightFilaments();

    for (int i = 0; i < (int)new_filament_map.size(); ++i) {
        if (std::find(left_filaments.begin(), left_filaments.end(), i + 1) != left_filaments.end()) {
            new_filament_map[i] = 1;
        } else if (std::find(right_filaments.begin(), right_filaments.end(), i + 1) != right_filaments.end()) {
            new_filament_map[i] = 2;
        }
    }
    return new_filament_map;
}

std::vector<int> FilamentMapMultiNozzlePanel::GetFilamentVolumeMaps() const
{
    // In multi-diameter mode, dragging only changes the AMS/fixed-nozzle group. The per-filament
    // flow type is selected outside this dialog and must remain unchanged.
    if (multi_nozzle_diameter_mode_enabled(wxGetApp().preset_bundle->project_config))
        return m_filament_volume_map;

    std::vector<int> volume_map(m_filament_map.size(), 0);

    auto find_in = [](const std::vector<int> &v, int id) {
        return std::find(v.begin(), v.end(), id) != v.end();
    };

    std::vector<int> right_hf  = this->GetRightHighFlowFilaments();
    std::vector<int> right_std = this->GetRightStandardFilaments();
    std::vector<int> right_tpu = this->GetRightTPUHighFlowFilaments();
    std::optional<NozzleVolumeType> single_fixed_volume_type;
    if (!m_right_panel->IsUseSeparation()) {
        auto full_cfg = wxGetApp().preset_bundle->full_config();
        auto nozzle_diameters = full_cfg.option<ConfigOptionFloatsNullable>("nozzle_diameter");
        const std::string active_diameter = (nozzle_diameters && !nozzle_diameters->values.empty())
            ? format_diameter_to_str(nozzle_diameters->values.front()) : std::string();
        single_fixed_volume_type = get_single_fixed_nozzle_volume_from_full_stat(m_ams_nozzle_idx, m_left_panel != nullptr, active_diameter);
    }

    if (m_left_panel) {
        std::vector<int> left_filaments = this->GetLeftFilaments();
        // AMS filaments use the AMS nozzle's real volume type (its physical slot ams_nozzle_idx),
        // consistent with the displayed AMS nozzle and the back-end physical nozzle list.
        const int ams_vol = static_cast<int>(get_ams_nozzle_volume_from_full_stat(m_ams_nozzle_idx));

        for (int i = 0; i < (int)volume_map.size(); ++i) {
            int fid = i + 1;
            if (find_in(left_filaments, fid)) {
                volume_map[i] = ams_vol;
            }
            else if (single_fixed_volume_type)
                volume_map[i] = static_cast<int>(*single_fixed_volume_type);
            else if (find_in(right_hf, fid))
                volume_map[i] = static_cast<int>(NozzleVolumeType::nvtHighFlow);
            else if (find_in(right_std, fid))
                volume_map[i] = static_cast<int>(NozzleVolumeType::nvtStandard);
            else if (find_in(right_tpu, fid))
                volume_map[i] = static_cast<int>(NozzleVolumeType::nvtTPU285);
        }
    } else {
        for (int i = 0; i < (int)volume_map.size(); ++i) {
            int fid = i + 1;
            if (single_fixed_volume_type)
                volume_map[i] = static_cast<int>(*single_fixed_volume_type);
            else if (find_in(right_hf, fid))
                volume_map[i] = static_cast<int>(NozzleVolumeType::nvtHighFlow);
            else if (find_in(right_std, fid))
                volume_map[i] = static_cast<int>(NozzleVolumeType::nvtStandard);
            else if (find_in(right_tpu, fid))
                volume_map[i] = static_cast<int>(NozzleVolumeType::nvtTPU285);
        }
    }

    return volume_map;
}

void FilamentMapMultiNozzlePanel::SyncPanelHeights()
{
    if (!m_right_panel) return;

    if (m_left_panel) {
        m_left_panel->SetMinSize(wxSize(FromDIP(AMS_PANEL_MIN_WIDTH), -1));
        m_right_panel->SetMinSize(wxSize(FromDIP(FIXED_PANEL_MIN_WIDTH), -1));

        m_left_panel->Layout();
        m_left_panel->Fit();
        m_right_panel->Layout();
        m_right_panel->Fit();

        wxSize left_best_size  = m_left_panel->GetBestSize();
        wxSize right_best_size = m_right_panel->GetBestSize();

        int max_height = std::max(left_best_size.GetHeight(), right_best_size.GetHeight());

        m_left_panel->SetMinSize(wxSize(FromDIP(AMS_PANEL_MIN_WIDTH), max_height));
        m_right_panel->SetMinSize(wxSize(FromDIP(FIXED_PANEL_MIN_WIDTH), max_height));
    } else {
        m_right_panel->Layout();
        m_right_panel->Fit();
    }

    Layout();
    Fit();

    if (GetParent()) {
        GetParent()->Layout();
        if (auto *scroll = dynamic_cast<wxScrolledWindow *>(GetParent()))
            scroll->FitInside();
    }
}

void FilamentMapMultiNozzlePanel::OnDragDropCompleted(wxCommandEvent& event)
{
    if (!m_pending_height_sync) {
        m_pending_height_sync = true;
        CallAfter([this]() {
            m_pending_height_sync = false;
            if (m_left_panel)
                m_left_panel->UpdateDiameterVolumeLayout();
            if (m_right_panel)
                m_right_panel->UpdateDiameterVolumeLayout();
            SyncPanelHeights();
        });
    }
    event.Skip();
}

void FilamentMapMultiNozzlePanel::OnDragHover(wxCommandEvent &event)
{
    if (!m_show_drag_unsupported_tip)
        return;

    const bool show_unsupported = !event.GetInt() && m_show_drag_unsupported_tip;
    const wxString tips = show_unsupported
        ? _L("Error: The nozzle type is not supported, so the filament cannot be moved.")
        : _L("Tips: You can drag the filaments to reassign them to different nozzles.");
    const wxColour color = show_unsupported ? TextErrorColor : TextNormalGreyColor;
    if (m_tips->GetLabel() == tips && m_tips->GetForegroundColour() == color)
        return;

    m_tips->SetLabel(tips);
    m_tips->SetForegroundColour(color);
    m_tips->Wrap(FromDIP(520));
    m_tips->Refresh();
    Layout();
    Fit();
    if (GetParent()) {
        GetParent()->Layout();
        if (auto *scroll = dynamic_cast<wxScrolledWindow *>(GetParent()))
            scroll->FitInside();
    }
}

void FilamentMapMultiNozzlePanel::OnTimer(wxTimerEvent &)
{
    PresetBundle *preset_bundle = wxGetApp().preset_bundle;
    std::vector<std::string> selected_diameters;
    if (const auto *selected = preset_bundle->project_config.option<ConfigOptionStrings>("filament_diameter_map"))
        selected_diameters = selected->values;
    const std::string printer_model = preset_bundle->printers.get_edited_preset().config.opt_string("printer_model");
    const std::string printer_preset = preset_bundle->printers.get_edited_preset().name;
    if (selected_diameters != m_cached_selected_diameters || preset_bundle->filament_presets != m_cached_filament_presets ||
        printer_model != m_cached_printer_model || printer_preset != m_cached_printer_preset) {
        m_allowed_diameters = get_filament_allowed_diameters(m_filament_map.size());
        m_cached_selected_diameters = std::move(selected_diameters);
        m_cached_filament_presets = preset_bundle->filament_presets;
        m_cached_printer_model = printer_model;
        m_cached_printer_preset = printer_preset;
    }

    const DynamicPrintConfig full_config = preset_bundle->full_config();
    const bool multi_diameter_mode = multi_nozzle_diameter_mode_enabled(full_config);
    const int current_ams_nozzle_idx = get_effective_ams_nozzle_idx_for_active_diameter(full_config);
    m_topology_changed |= current_ams_nozzle_idx != m_ams_nozzle_idx;

    const ExtruderSlots &slots = preset_bundle->nozzle_inventory.slots(0);
    std::set<std::string> ams_diameters;
    std::set<std::string> fixed_diameters;
    for (size_t idx = 0; idx < slots.size(); ++idx) {
        const double diameter = string_to_double_decimal_point(slots[idx].diameter);
        if (diameter <= 0.0)
            continue;
        auto &target = int(idx) == m_ams_nozzle_idx ? ams_diameters : fixed_diameters;
        target.insert(format_diameter_to_str(diameter));
    }
    if (multi_diameter_mode) {
        if (m_left_panel && m_left_panel->IsUseDiameterVolumeGroups())
            m_left_panel->set_diameter_group_accept_filament_predicate(
                make_diameter_group_accept_predicate(m_allowed_diameters));
        else if (m_left_panel)
            m_left_panel->set_accept_filament_predicate(ams_diameters.empty() ? std::function<bool(int)>{} :
                make_diameter_accept_predicate(m_allowed_diameters, ams_diameters));
        if (m_right_panel->IsUseDiameterVolumeGroups())
            m_right_panel->set_diameter_group_accept_filament_predicate(
                make_diameter_group_accept_predicate(m_allowed_diameters));
        else
            m_right_panel->set_accept_filament_predicate(fixed_diameters.empty() ? std::function<bool(int)>{} :
                make_diameter_accept_predicate(m_allowed_diameters, fixed_diameters));
    } else {
        if (m_left_panel)
            m_left_panel->set_accept_filament_predicate({});
        m_right_panel->set_accept_filament_predicate({});
    }

    auto has_incompatible = [this](const std::vector<int> &filaments, const std::set<std::string> &targets) {
        if (targets.empty())
            return false;
        for (int filament_id : filaments) {
            const int idx = filament_id - 1;
            if (idx < 0 || idx >= int(m_allowed_diameters.size()) || m_allowed_diameters[idx].empty())
                continue;
            if (std::none_of(targets.begin(), targets.end(), [this, idx](const std::string &diameter) {
                    return m_allowed_diameters[idx].count(diameter) > 0;
                }))
                return true;
        }
        return false;
    };
    const bool invalid = m_topology_changed ||
                         (multi_diameter_mode &&
                          ((m_left_panel && has_incompatible(GetLeftFilaments(), ams_diameters)) ||
                           has_incompatible(GetRightFilaments(), fixed_diameters)));
    const wxString diameter_error = m_topology_changed ?
        _L("Error: the nozzle configuration changed. Please reopen the filament grouping dialog.") :
        (invalid ? _L("Error: a filament is assigned to a nozzle of incompatible diameter. "
                      "Please regroup so each filament stays on a nozzle whose diameter it supports.") : wxString());
    bool update_ui  = diameter_error != m_diameter_error;
    bool send_event = update_ui || m_force_validation;
    m_diameter_error = diameter_error;

    if (update_ui) {
        if (invalid) {
            m_errors->SetLabel(diameter_error);
            m_errors->Wrap(FromDIP(520));
            m_errors->Show();
        } else {
            m_errors->Hide();
        }
        if (m_left_panel) m_left_panel->Freeze();
        m_right_panel->Freeze();
        Layout();
        Fit();
        this->GetParent()->Layout();
        this->GetParent()->Fit();
        if (m_left_panel) m_left_panel->Thaw();
        m_right_panel->Thaw();
    }

    if (send_event) {
        wxCommandEvent event(wxEVT_INVALID_MANUAL_MAP);
        event.SetInt(!invalid);
        ProcessEvent(event);
        m_force_validation = false;
    }
}

void FilamentMapMultiNozzlePanel::UpdateNozzleVolumeType()
{
    if (m_right_panel->IsUseDiameterVolumeGroups()) {
        UpdateNozzleCountDisplay();
        Layout();
        Fit();
        return;
    }

    auto preset_bundle = wxGetApp().preset_bundle;
    auto full_cfg = preset_bundle->full_config();
    auto nozzle_diameters = full_cfg.option<ConfigOptionFloatsNullable>("nozzle_diameter");
    const std::string active_diameter = (nozzle_diameters && !nozzle_diameters->values.empty())
        ? format_diameter_to_str(nozzle_diameters->values.front()) : std::string();

    std::set<NozzleVolumeType> types;
    const ExtruderSlots &slots = preset_bundle->nozzle_inventory.slots(0);
    for (size_t idx = 0; idx < slots.size(); ++idx) {
        if (m_ams_nozzle_idx >= 0 && static_cast<int>(idx) == m_ams_nozzle_idx)
            continue;
        const NozzleSlot &slot = slots[idx];
        if (slot.empty()) continue;
        if (!active_diameter.empty() && format_diameter_to_str(std::atof(slot.diameter.c_str())) != active_diameter)
            continue;
        types.insert(slot.volume_type);
    }
    bool should_separate = types.size() > 1;

    m_right_panel->SetUseSeparation(should_separate);

    UpdateNozzleCountDisplay();

    Layout();
    Fit();
}

void FilamentMapMultiNozzlePanel::UpdateNozzleCountDisplay()
{
    auto preset_bundle = wxGetApp().preset_bundle;
    auto full_cfg = preset_bundle->full_config();
    bool has_ams = (m_left_panel != nullptr);

    auto nozzle_diameters = full_cfg.option<ConfigOptionFloatsNullable>("nozzle_diameter");
    const std::string active_diameter = (nozzle_diameters && !nozzle_diameters->values.empty())
        ? format_diameter_to_str(nozzle_diameters->values.front()) : std::string();
    bool has_full_stat = false;
    std::map<NozzleVolumeType, int> fixed_counts = get_fixed_nozzle_counts_from_full_stat(m_ams_nozzle_idx, has_ams, active_diameter, has_full_stat);
    auto fixed_count_of = [&fixed_counts](NozzleVolumeType vt) {
        auto it = fixed_counts.find(vt);
        return it != fixed_counts.end() ? it->second : 0;
    };

    const NozzleInventory &nozzles = preset_bundle->nozzle_inventory;
    int total = nozzles.count(0);
    wxString         ams_label, fixed_label;
    NozzleVolumeType ams_type = nvtStandard;
    if (has_ams) {
        ams_label   = get_ams_nozzle_label(m_ams_nozzle_idx);
        fixed_label = get_fixed_nozzle_label(m_total_nozzle_count, m_ams_nozzle_idx);

        const ExtruderSlots &slots = nozzles.slots(0);
        if (m_ams_nozzle_idx >= 0 && (int) slots.size() > m_ams_nozzle_idx && !slots[m_ams_nozzle_idx].empty())
            ams_type = slots[m_ams_nozzle_idx].volume_type;

        m_left_panel->UpdateLabel(ams_label);
        if (!m_left_panel->IsUseDiameterVolumeGroups()) {
            if (multi_nozzle_diameter_mode_enabled(full_cfg) &&
                m_ams_nozzle_idx >= 0 && m_ams_nozzle_idx < static_cast<int>(slots.size())) {
                const std::string diameter = format_diameter_to_str(
                    string_to_double_decimal_point(slots[m_ams_nozzle_idx].diameter));
                m_left_panel->UpdateUnifiedLabel(
                    wxString::FromUTF8(diameter) + get_volume_type_label(ams_type));
            } else {
                m_left_panel->UpdateUnifiedLabel(get_volume_type_label(ams_type));
            }
        }
    }

    if (m_right_panel->IsUseDiameterVolumeGroups()) {
        m_right_panel->UpdateLabel(has_ams ? fixed_label : wxString::Format(_L("Nozzle (%d)"), m_total_nozzle_count));
    } else if (m_right_panel->IsUseSeparation()) {
        int standard_count = nozzles.count(0, NozzleVolumeType::nvtStandard);
        int highflow_count = nozzles.count(0, NozzleVolumeType::nvtHighFlow);
        int tpu_count = nozzles.count(0, NozzleVolumeType::nvtTPUHighFlow) + nozzles.count(0, NozzleVolumeType::nvtTPU285);

        int right_std = has_full_stat ? fixed_count_of(nvtStandard) : standard_count;
        int right_hf  = has_full_stat ? fixed_count_of(nvtHighFlow) : highflow_count;
        int right_tpu = has_full_stat
            ? fixed_count_of(nvtTPUHighFlow) + fixed_count_of(nvtTPU285)
            : tpu_count;
        if (has_ams && !has_full_stat) {
            // 固定组的数量要扣掉被 AMS 占用的那个喷嘴
            if (ams_type == nvtStandard)
                right_std = std::max(0, right_std - 1);
            else if (ams_type == nvtHighFlow)
                right_hf = std::max(0, right_hf - 1);
            else if (ams_type == nvtTPUHighFlow || ams_type == nvtTPU285)
                right_tpu = std::max(0, right_tpu - 1);
        }

        // 主标题只保留喷嘴编号，各流量类型的数量挂到次级标题上
        m_right_panel->UpdateLabel(has_ams ? fixed_label : wxString::Format(_L("Nozzle (%d)"), m_total_nozzle_count));
        m_right_panel->UpdateVolumeTypeCounts(right_std, right_hf, right_tpu);
    } else {
        if (has_ams) {
            int right_count = 0;
            if (has_full_stat)
                for (const auto &kv : fixed_counts) right_count += kv.second;
            else
                right_count = std::max(0, total - 1);
            wxString right_title = wxString::Format(fixed_label + "(%d)", right_count);
            m_right_panel->UpdateLabel(right_title);
        } else {
            // 单挤出机多喷嘴且只有一种流量类型时：统一喷嘴组用机型 AMS
            // 名称标注，而非喷嘴数量/流量类型。
            std::string model_id = is_single_ext_multi_nozzle(full_cfg)
                                       ? preset_bundle->printers.get_edited_preset().get_printer_type(preset_bundle)
                                       : std::string();
            wxString title;
            if (!model_id.empty()) {
                title = wxString::Format("%sAMS", model_id);
            } else {
                wxString type_label;
                if (has_full_stat) {
                    for (const auto &kv : fixed_counts) {
                        if (kv.second > 0) { type_label = get_volume_type_label(kv.first); break; }
                    }
                }
                title = type_label.empty()
                    ? wxString::Format(_L("Nozzle (%d)"), m_total_nozzle_count)
                    : wxString::Format(_L("Nozzle (%d) - %s"), m_total_nozzle_count, type_label);
            }
            m_right_panel->UpdateLabel(title);
        }
    }
}

bool FilamentMapMultiNozzlePanel::Show(bool show)
{
    m_force_validation = show;
    if (show)
        m_timer->Start(500);
    else
        m_timer->Stop();

    const bool ret = FilamentMapPanel::Show(show);
    // Validate at once rather than on the first tick: until then OK stays enabled for an invalid grouping.
    if (show) {
        wxTimerEvent evt;
        OnTimer(evt);
    }
    return ret;
}

// ===================== FilamentMapBtnPanel =====================

class FilamentMapBtnPanel : public wxPanel
{
public:
    FilamentMapBtnPanel(wxWindow *parent, const wxString &label, const wxString &detail, const std::string &icon_path);
    void Select(bool selected);
    bool Enable(bool enable) override;
    bool IsEnabled() const { return m_enabled; }

protected:
    void OnPaint(wxPaintEvent &event);

private:
    void OnEnterWindow(wxMouseEvent &event);
    void OnLeaveWindow(wxMouseEvent &evnet);
    void OnSize(wxSizeEvent &event);

    void UpdateStatus();

    wxBitmap icon_enabled;
    wxBitmap icon_disabled;

    wxBitmapButton *m_btn;
    wxStaticText   *m_label;
    Label          *m_disable_tip;
    Label          *m_detail;
    std::string     m_icon_path;
    bool            m_enabled{true}; //  override system enable state, to get consistent UI
    bool            m_hover{false};
    bool            m_selected{false};
};

GUI::FilamentMapBtnPanel::FilamentMapBtnPanel(wxWindow *parent, const wxString &label, const wxString &detail, const std::string &icon) : wxPanel(parent)
{
    SetBackgroundColour(*wxWHITE);
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    m_hover = false;

    auto sizer = new wxBoxSizer(wxVERTICAL);

    icon_enabled = create_scaled_bitmap(icon, nullptr, 20);
    icon_disabled = create_scaled_bitmap(icon + "_disabled", nullptr, 20);

    m_btn    = new wxBitmapButton(this, wxID_ANY, icon_enabled, wxDefaultPosition, wxDefaultSize, wxNO_BORDER);
    m_btn->SetBackgroundStyle(wxBG_STYLE_PAINT);

    m_label = new wxStaticText(this, wxID_ANY, label);
    m_label->SetFont(Label::Head_14);
    m_label->SetForegroundColour(TextNormalBlackColor);

    auto label_sizer = new wxBoxSizer(wxHORIZONTAL);
    label_sizer->AddStretchSpacer();
    label_sizer->Add(m_btn, 0, wxALIGN_CENTER | wxEXPAND | wxLEFT, FromDIP(1));
    label_sizer->Add(m_label, 0, wxALIGN_CENTER | wxEXPAND| wxALL, FromDIP(3));
    label_sizer->AddStretchSpacer();

    auto label_width = label_sizer->GetMinSize().GetWidth();
    auto min_width = label_width + FromDIP(24) * 2;
    min_width = std::max(FromDIP(160), min_width);

    m_disable_tip = new Label(this, _L("(Sync with printer)"));

    sizer->AddSpacer(FromDIP(20));
    sizer->Add(label_sizer, 0, wxALIGN_CENTER | wxEXPAND);
    sizer->Add(m_disable_tip, 0, wxALIGN_CENTER);
    sizer->AddSpacer(FromDIP(3));

    m_detail = new Label(this, detail);
    m_detail->SetFont(Label::Body_12);
    m_detail->SetForegroundColour(TextNormalGreyColor);

    sizer->Add(m_detail, 1, wxALIGN_CENTER | wxEXPAND);
    sizer->AddSpacer(FromDIP(10));

    SetSizer(sizer);
    SetMinSize(wxSize(min_width, -1));
    Layout();
    Fit();

    GUI::wxGetApp().UpdateDarkUIWin(this);

    auto forward_click_to_parent = [this](wxMouseEvent &event) {
        wxCommandEvent click_event(wxEVT_LEFT_DOWN, GetId());
        click_event.SetEventObject(this);
        this->ProcessEvent(click_event);
    };

    m_btn->Bind(wxEVT_LEFT_DOWN, forward_click_to_parent);
    m_label->Bind(wxEVT_LEFT_DOWN, forward_click_to_parent);
    m_detail->Bind(wxEVT_LEFT_DOWN, forward_click_to_parent);

    Bind(wxEVT_PAINT, &FilamentMapBtnPanel::OnPaint, this);
    Bind(wxEVT_ENTER_WINDOW, &FilamentMapBtnPanel::OnEnterWindow, this);
    Bind(wxEVT_LEAVE_WINDOW, &FilamentMapBtnPanel::OnLeaveWindow, this);
    Bind(wxEVT_SIZE, &FilamentMapBtnPanel::OnSize, this);
}

void FilamentMapBtnPanel::OnPaint(wxPaintEvent &event)
{
    wxAutoBufferedPaintDC dc(this);
    wxGraphicsContext    *gc = wxGraphicsContext::Create(dc);

    if (gc) {
        dc.Clear();
        wxRect rect = GetClientRect();
        gc->SetBrush(wxTransparentColour);
        gc->DrawRoundedRectangle(0, 0, rect.width, rect.height, 0);
        wxColour bg_color = m_selected ? BgSelectColor : BgNormalColor;

        wxColour border_color = m_hover || m_selected ? BorderSelectedColor : BorderNormalColor;

        bg_color     = StateColor::darkModeColorFor(bg_color);
        border_color = StateColor::darkModeColorFor(border_color);
        gc->SetBrush(wxBrush(bg_color));
        gc->SetPen(wxPen(border_color, 1));
        gc->DrawRoundedRectangle(1, 1, rect.width - 2, rect.height - 2, 8);
        delete gc;
    }
}

void FilamentMapBtnPanel::UpdateStatus()
{
    if (m_selected) {
        m_btn->SetBackgroundColour(BgSelectColor);
        m_label->SetBackgroundColour(BgSelectColor);
        m_detail->SetBackgroundColour(BgSelectColor);
        m_disable_tip->SetBackgroundColour(BgSelectColor);
    }
    else {
        m_btn->SetBackgroundColour(BgNormalColor);
        m_label->SetBackgroundColour(BgNormalColor);
        m_detail->SetBackgroundColour(BgNormalColor);
        m_disable_tip->SetBackgroundColour(BgNormalColor);
    }
    if (!m_enabled) {
        m_disable_tip->SetLabel(_L("(Sync with printer)"));
        m_disable_tip->SetForegroundColour(TextDisableColor);
        m_btn->SetBitmap(icon_disabled);
        m_btn->SetForegroundColour(BgDisableColor);
        m_label->SetForegroundColour(TextDisableColor);
        m_detail->SetForegroundColour(TextDisableColor);
    } else {
        m_disable_tip->SetLabel("");
        m_disable_tip->SetForegroundColour(TextNormalBlackColor);
        m_btn->SetBitmap(icon_enabled);
        m_btn->SetForegroundColour(BgNormalColor);
        m_label->SetForegroundColour(TextNormalBlackColor);
        m_detail->SetForegroundColour(TextNormalGreyColor);
    }
    GUI::wxGetApp().UpdateDarkUIWin(this);
    Refresh();
}

void FilamentMapBtnPanel::OnEnterWindow(wxMouseEvent &event)
{
    if (!m_hover && m_enabled) {
        m_hover = true;
        UpdateStatus();
        event.Skip();
    }
}

void FilamentMapBtnPanel::OnLeaveWindow(wxMouseEvent &event)
{
    if (m_hover) {
        wxPoint pos = this->ScreenToClient(wxGetMousePosition());
        if (this->GetClientRect().Contains(pos)) return;
        m_hover = false;
        UpdateStatus();
        event.Skip();
    }
}

void FilamentMapBtnPanel::OnSize(wxSizeEvent &event)
{
    event.Skip();
    const int horizontal_margin = FromDIP(12);
    int text_width = event.GetSize().GetWidth() - horizontal_margin * 2;
    if (text_width > 0) {
        m_detail->SetMaxSize(wxSize(text_width, -1));
        m_detail->Wrap(text_width);
    }
}

bool FilamentMapBtnPanel::Enable(bool enable)
{
    m_enabled = enable;
    UpdateStatus();
    return true;
}

void FilamentMapBtnPanel::Select(bool selected)
{
    m_selected = selected;
    UpdateStatus();
}

FilamentMapAutoPanel::FilamentMapAutoPanel(wxWindow *parent, FilamentMapMode mode, bool machine_synced, const std::vector<FilamentMapMode> &available_modes)
    : FilamentMapPanel(parent)
{
    std::string pt_auto = wxGetApp().preset_bundle->printers.get_edited_preset().get_printer_type(wxGetApp().preset_bundle);
    wxString main_nz_auto   = _L(DevPrinterConfigUtil::get_toolhead_display_name(pt_auto, MAIN_EXTRUDER_ID, ToolHeadComponent::Nozzle, ToolHeadNameCase::LowerCase));
    wxString deputy_nz_auto = _L(DevPrinterConfigUtil::get_toolhead_display_name(pt_auto, DEPUTY_EXTRUDER_ID, ToolHeadComponent::Nozzle, ToolHeadNameCase::LowerCase));

    const wxString AutoForFlushDetail = wxString::Format(_L("Generates filament grouping for the %s and %s based on the most filament-saving principles to minimize waste"), deputy_nz_auto, main_nz_auto);

    const wxString AutoForMatchDetail = wxString::Format(_L("Generates filament grouping for the %s and %s based on the printer's actual filament status, reducing the need for manual filament adjustment"), deputy_nz_auto, main_nz_auto);

    const wxString AutoForQualityDetail = wxString::Format(_L("Generates filament grouping for the %s and %s based on the quality of prints, prioritizing print quality over filament saving"), deputy_nz_auto, main_nz_auto);

    m_machine_synced = machine_synced;
    m_available_modes = available_modes;
    m_mode = mode;

    // Create mode panels based on available modes
    auto sizer = new wxBoxSizer(wxHORIZONTAL);

    // Define all possible modes with their labels and details
    std::map<FilamentMapMode, std::pair<wxString, wxString>> mode_info = {
        {fmmAutoForFlush, {_L("Filament-Saving Mode"), AutoForFlushDetail}},
        {fmmAutoForMatch, {_L("Convenience Mode"), AutoForMatchDetail}},
        {fmmAutoForQuality, {_L("Quality Mode"), AutoForQualityDetail}}
    };

    // Create panels for available modes
    for (const auto& available_mode : m_available_modes) {
        auto it = mode_info.find(available_mode);
        if (it == mode_info.end()) {
            assert(false && "invalid auto mode");
            BOOST_LOG_TRIVIAL(warning) << "invalid auto mode skipped: " << mode;
            continue;
        }

        const auto &label  = it->second.first;
        const auto &detail = it->second.second;
        const auto &icon   = GetIconForMode(available_mode);

        auto panel = new FilamentMapBtnPanel(this, label, detail, icon);

        // Disable match mode if not machine synced
        if (available_mode == fmmAutoForMatch && !m_machine_synced) { panel->Enable(false); }

        m_mode_panels.push_back(panel);

        // Bind click event
        panel->Bind(wxEVT_LEFT_DOWN, [this, available_mode, panel](auto &event) {
            if (panel->IsEnabled()) { this->OnModeSwitch(available_mode); }
        });
    }

    int spacer_width = FromDIP(20);
    sizer->AddSpacer(spacer_width);
    for (size_t i = 0; i < m_mode_panels.size(); ++i) {
        if (i > 0)
            sizer->AddSpacer(spacer_width);
        sizer->Add(m_mode_panels[i], 1, wxEXPAND);
    }
    sizer->AddSpacer(spacer_width);

    UpdateStatus();

    SetSizer(sizer);
    // First layout assigns equal widths, triggering OnSize which re-wraps
    // detail text. Second Fit/Layout picks up the updated heights.
    Layout();
    Fit();
    Layout();

    GUI::wxGetApp().UpdateDarkUIWin(this);
}

void FilamentMapAutoPanel::UpdateStatus()
{
    assert(m_mode_panels.size() == m_available_modes.size());
    for (size_t i = 0; i < m_mode_panels.size(); ++i) {
        bool selected = (m_mode == m_available_modes[i]);
        m_mode_panels[i]->Select(selected);
    }
}

void FilamentMapAutoPanel::OnModeSwitch(FilamentMapMode mode)
{
    m_mode = mode;
    UpdateStatus();
}

std::string FilamentMapAutoPanel::GetIconForMode(FilamentMapMode mode)
{
    switch (mode) {
    case fmmAutoForMatch: return "match_mode_panel_icon";
    case fmmAutoForFlush: return "flush_mode_panel_icon";
    case fmmAutoForQuality: return "quality_mode_panel_icon";
    default:
        BOOST_LOG_TRIVIAL(warning) << "invalid mode: " << mode;
        return {};
    }
}

FilamentMapSavingPanel::FilamentMapSavingPanel(wxWindow *parent, const wxString &description, FilamentMapMode mode)
    : FilamentMapPanel(parent)
    , m_mode(mode)
{
    SetBackgroundColour(*wxWHITE);

    const bool custom_text = !description.empty();

    auto saving_sizer = new wxBoxSizer(wxVERTICAL);
    saving_sizer->AddSpacer(FromDIP(32));

    // 空文件夹带放大镜的占位图标。仅保存面板与 默认占位（custom text）都显示。
    {
        auto icon_bitmap = create_scaled_bitmap("search_file", nullptr, 80);
        auto icon_btn    = new wxStaticBitmap(this, wxID_ANY, icon_bitmap);
        saving_sizer->Add(icon_btn, 0, wxALIGN_CENTER);
        saving_sizer->AddSpacer(FromDIP(16));
    }

    wxString desc_text = description;
    if (desc_text.empty()) {
        std::string pt_saving = wxGetApp().preset_bundle->printers.get_edited_preset().get_printer_type(wxGetApp().preset_bundle);
        wxString main_nz_saving   = _L(DevPrinterConfigUtil::get_toolhead_display_name(pt_saving, MAIN_EXTRUDER_ID, ToolHeadComponent::Nozzle, ToolHeadNameCase::LowerCase));
        wxString deputy_nz_saving = _L(DevPrinterConfigUtil::get_toolhead_display_name(pt_saving, DEPUTY_EXTRUDER_ID, ToolHeadComponent::Nozzle, ToolHeadNameCase::LowerCase));
        desc_text = wxString::Format(_L("Generates filament grouping for the %s and %s based on the most filament-saving principles to minimize waste"), deputy_nz_saving, main_nz_saving);
    }

    if (custom_text) {
        // 换行而非省略号：长文案 / 本地化文案保持完整可读，面板高度随行数增长。
        auto desc_label = new Label(this, desc_text, wxALIGN_CENTER_HORIZONTAL);
        desc_label->SetFont(Label::Body_14);
        desc_label->SetForegroundColour(wxColour("#6B6B6B"));
        desc_label->Wrap(FromDIP(460));
        saving_sizer->Add(desc_label, 0, wxALIGN_CENTER | wxLEFT | wxRIGHT, FromDIP(50));
    } else {
        auto desc_label = new Label(this, desc_text);
        desc_label->SetFont(Label::Body_12);
        desc_label->SetForegroundColour(wxColour("#6B6B6B"));
        saving_sizer->Add(desc_label, 0, wxALIGN_CENTER | wxLEFT | wxRIGHT, FromDIP(50));
    }
    saving_sizer->AddSpacer(FromDIP(32));

    SetSizer(saving_sizer);

    if (custom_text)
        SetMinSize(wxSize(-1, FromDIP(180)));
}

} // namespace Slic3r::GUI
