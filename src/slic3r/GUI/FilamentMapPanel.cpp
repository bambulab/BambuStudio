#include "FilamentMapPanel.hpp"
#include "Widgets/MultiNozzleSync.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "DeviceCore/DevConfigUtil.h"
#include "DeviceCore/DevNozzleSystem.h"
#include <boost/log/trivial.hpp>
#include <cassert>
#include <wx/dcbuffer.h>
#include "wx/graphics.h"
#include <map>
#include <set>
#include <algorithm>
#include <cstdlib>
#include <optional>

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
    return wxString::Format(_L("Nozzle %d"), ams_index_0based + 1);
}

wxString get_fixed_nozzle_label(int total_count, int ams_index_0based)
{
    auto indices = get_fixed_nozzle_indices(total_count, ams_index_0based);
    if (indices.empty()) return _L("Nozzle");

    wxString label = _L("Nozzle ");
    bool first_range = true;
    size_t i = 0;
    while (i < indices.size()) {
        int start = indices[i] + 1;
        int end = start;
        while (i + 1 < indices.size() && indices[i + 1] == indices[i] + 1) {
            ++i;
            end = indices[i] + 1;
        }
        if (!first_range) label += "/";
        if (start == end)
            label += wxString::Format("%d", start);
        else
            label += wxString::Format("%d-%d", start, end);
        first_range = false;
        ++i;
    }
    return label;
}
bool is_multi_nozzle_single_extruder(const ConfigBase &config)
{
    auto       opt_hotend                = config.option<ConfigOptionBool>("handle_hotend_as_extruder");
    auto       nozzle_diameters          = config.option<ConfigOptionFloatsNullable>("nozzle_diameter");
    auto       extruder_max_nozzle_count = config.option<ConfigOptionIntsNullable>("extruder_max_nozzle_count");
    const bool support_multi_nozzle      = extruder_max_nozzle_count &&
                                      std::any_of(extruder_max_nozzle_count->values.begin(), extruder_max_nozzle_count->values.end(), [](int val) { return val > 1; });
    return opt_hotend && opt_hotend->value && nozzle_diameters && nozzle_diameters->size() == 1 && support_multi_nozzle;
}

int get_effective_ams_nozzle_idx_for_active_diameter(const ConfigBase& config)
{
    int ams_idx = get_effective_ams_nozzle_idx(config);
    if (ams_idx < 0)
        return -1;

    const auto* nd = dynamic_cast<const ConfigOptionFloatsNullable*>(config.option("nozzle_diameter"));
    const std::string active_diameter = (nd && !nd->values.empty()) ? format_diameter_to_str(nd->values.front()) : std::string();
    auto full_stats = wxGetApp().preset_bundle->extruder_full_stat.get_extruder_stats(0);
    if (ams_idx >= (int) full_stats.size())
        return -1;

    const std::string& ams_diam = full_stats[ams_idx].first;
    const std::string& ams_vol  = full_stats[ams_idx].second;
    const bool usable = !ams_vol.empty() &&
        (active_diameter.empty() || ams_diam.empty() || format_diameter_to_str(std::atof(ams_diam.c_str())) == active_diameter);
    return usable ? ams_idx : -1;
}

static std::map<NozzleVolumeType, int> get_fixed_nozzle_counts_from_full_stat(int ams_nozzle_idx, bool has_ams, const std::string& active_diameter, bool& has_full_stat)
{
    std::map<NozzleVolumeType, int> ret;
    auto preset_bundle = wxGetApp().preset_bundle;
    auto full_stats = preset_bundle->extruder_full_stat.get_extruder_stats(0);
    has_full_stat = !full_stats.empty();

    for (const auto &slot : full_stats) {
        const std::string &diameter = slot.first;
        const std::string &vol = slot.second;
        if (vol.empty()) continue;
        if (!active_diameter.empty() && !diameter.empty() && format_diameter_to_str(std::atof(diameter.c_str())) != active_diameter)
            continue;
        ret[DevNozzle::ToNozzleVolumeType(vol)]++;
    }

    if (has_ams && !ret.empty() && ams_nozzle_idx >= 0 && ams_nozzle_idx < (int) full_stats.size()) {
        const std::string& ams_diam = full_stats[ams_nozzle_idx].first;
        const std::string& ams_vol  = full_stats[ams_nozzle_idx].second;
        const bool ams_counted = !ams_vol.empty() &&
            (active_diameter.empty() || ams_diam.empty() || format_diameter_to_str(std::atof(ams_diam.c_str())) == active_diameter);
        if (ams_counted) {
            NozzleVolumeType ams_vt = DevNozzle::ToNozzleVolumeType(ams_vol);
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

static int get_ams_slot_count_from_config(const ConfigBase& config)
{
    const auto* ams_count_opt = dynamic_cast<const ConfigOptionStrings*>(config.option("extruder_ams_count"));
    if (!ams_count_opt)
        return 1;
    const auto counts = get_extruder_ams_count(ams_count_opt->values);
    int slots = 0;
    for (const auto& ext : counts)
        for (const auto& kv : ext)
            slots += kv.first * kv.second;
    return std::max(1, slots);
}

static NozzleVolumeType get_ams_nozzle_volume_from_full_stat(int ams_nozzle_idx)
{
    auto preset_bundle = wxGetApp().preset_bundle;
    auto full_stats = preset_bundle->extruder_full_stat.get_extruder_stats(0);
    if (ams_nozzle_idx >= 0 && ams_nozzle_idx < (int) full_stats.size() && !full_stats[ams_nozzle_idx].second.empty())
        return DevNozzle::ToNozzleVolumeType(full_stats[ams_nozzle_idx].second);
    auto vals = preset_bundle->project_config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type")->values;
    return vals.empty() ? nvtStandard : NozzleVolumeType(vals[0]);
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
            int standard_count = preset_bundle->extruder_nozzle_stat.get_extruder_nozzle_count(eid, NozzleVolumeType::nvtStandard);
            int highflow_count = preset_bundle->extruder_nozzle_stat.get_extruder_nozzle_count(eid, NozzleVolumeType::nvtHighFlow);

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
                valid = false;
                invalid_eid = eid;
                invalid_nozzle = (has_standard && standard_count == 0) ? NozzleVolumeType::nvtStandard : NozzleVolumeType::nvtHighFlow;
                break;
            }
        } else {
            int count = preset_bundle->extruder_nozzle_stat.get_extruder_nozzle_count(eid, extruder_volume_type);
            if (count == 0) {
                valid = false;
                invalid_eid = eid;
                invalid_nozzle = extruder_volume_type;
                break;
            }
        }
    }

    bool update_ui  = m_invalid_id != invalid_eid;
    bool send_event = update_ui || m_force_validation;

    m_invalid_id = invalid_eid;

    if (update_ui) {
        if (valid) {
            m_errors->Hide();
            m_suggestion_panel->Hide();
        } else {
            m_errors->SetLabel(wxString::Format(_L("Error: %s extruder has no available %s nozzle, current group result is invalid."),
                                                invalid_eid == 0 ? _L("Left") : _L("Right"), invalid_nozzle == NozzleVolumeType::nvtStandard ? _L("Standard") : _L("High Flow")));
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
            m_right_panel->AddColorBlock(color, type, idx + 1, vol_type);
        }
    }
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

    int left_count = preset_bundle->extruder_nozzle_stat.get_extruder_nozzle_count(0);
    wxString left_title = wxString::Format(dep_ext_name + "(%d)", left_count);
    m_left_panel->UpdateLabel(left_title);

    if (m_right_panel->IsUseSeparation()) {
        int standard_count = preset_bundle->extruder_nozzle_stat.get_extruder_nozzle_count(1, NozzleVolumeType::nvtStandard);
        int highflow_count = preset_bundle->extruder_nozzle_stat.get_extruder_nozzle_count(1, NozzleVolumeType::nvtHighFlow);
        int tpu_count = preset_bundle->extruder_nozzle_stat.get_extruder_nozzle_count(1, NozzleVolumeType::nvtTPUHighFlow)
                      + preset_bundle->extruder_nozzle_stat.get_extruder_nozzle_count(1, NozzleVolumeType::nvtTPU285);
        wxString right_title;
        if (tpu_count > 0)
            right_title = wxString::Format(main_ext_name + "(Std: %d, HF: %d, TPU285: %d)", standard_count, highflow_count, tpu_count);
        else
            right_title = wxString::Format(main_ext_name + "(Std: %d, HF: %d)", standard_count, highflow_count);
        m_right_panel->UpdateLabel(right_title);
    } else {
        int right_count = preset_bundle->extruder_nozzle_stat.get_extruder_nozzle_count(1);
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
        m_right_panel->AddColorBlock(block->GetColor(), block->GetType(), block->GetFilamentId(), nvtStandard, false);
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

    return FilamentMapPanel::Show(show);
}

// ===================== FilamentMapMultiNozzlePanel =====================

FilamentMapMultiNozzlePanel::FilamentMapMultiNozzlePanel(wxWindow                       *parent,
                                         const std::vector<std::string> &color,
                                         const std::vector<std::string> &type,
                                         const std::vector<int>         &filament_list,
                                         const std::vector<int>         &filament_map,
                                         const std::vector<int>         &filament_volume_map)
    : FilamentMapPanel(parent)
    , m_filament_map(filament_map)
    , m_filament_volume_map(filament_volume_map)
    , m_filament_list(filament_list)
    , m_filament_color(color)
    , m_filament_type(type)
{
    SetName(wxT("FilamentMapMultiNozzlePanel"));
    SetBackgroundColour(BgNormalColor);

    auto preset_bundle = wxGetApp().preset_bundle;
    auto full_cfg = preset_bundle->full_config();
    auto cnt_opt = full_cfg.option<ConfigOptionIntsNullable>("extruder_max_nozzle_count");
    m_total_nozzle_count = (cnt_opt && !cnt_opt->values.empty()) ? cnt_opt->values[0] : 1;
    m_ams_nozzle_idx = get_effective_ams_nozzle_idx_for_active_diameter(full_cfg);
    bool has_ams = (m_ams_nozzle_idx >= 0);
    m_ams_slot_count = has_ams ? get_ams_slot_count_from_config(full_cfg) : 1;

    auto top_sizer = new wxBoxSizer(wxVERTICAL);

    m_description = new Label(this, _L("We will slice according to this arrangement:"));
    top_sizer->Add(m_description, 0, wxALIGN_LEFT | wxLEFT, FromDIP(15));
    m_description->Wrap(FromDIP(520));
    top_sizer->AddSpacer(FromDIP(8));

    auto drag_sizer = new wxBoxSizer(wxHORIZONTAL);

    if (has_ams) {
        m_left_panel  = new SeparatedDragDropPanel(this, get_ams_nozzle_label(m_ams_nozzle_idx), false);
        m_right_panel = new SeparatedDragDropPanel(this, get_fixed_nozzle_label(m_total_nozzle_count, m_ams_nozzle_idx), false);
    } else {
        m_left_panel  = nullptr;
        wxString panel_label = wxString::Format(_L("Nozzle (%d)"), m_total_nozzle_count);
        m_right_panel = new SeparatedDragDropPanel(this, panel_label, false);
    }

    UpdateNozzleVolumeType();

    for (size_t idx = 0; idx < m_filament_map.size(); ++idx) {
        auto iter = std::find(m_filament_list.begin(), m_filament_list.end(), idx + 1);
        if (iter == m_filament_list.end()) continue;
        wxColor clr = Hex2Color(m_filament_color[idx]);
        std::string tp = m_filament_type[idx];
        NozzleVolumeType vol_type = (idx < m_filament_volume_map.size()) ? NozzleVolumeType(m_filament_volume_map[idx]) : nvtStandard;
        if (has_ams && m_filament_map[idx] == 1) {
            m_left_panel->AddColorBlock(clr, tp, idx + 1, vol_type);
        } else {
            m_right_panel->AddColorBlock(clr, tp, idx + 1, vol_type);
        }
    }

    if (has_ams) {
        m_left_panel->SetMinSize({FromDIP(160), -1});
        m_right_panel->SetMinSize({FromDIP(360), -1});
        drag_sizer->Add(m_left_panel, 3, wxALIGN_CENTER | wxEXPAND);
        drag_sizer->AddSpacer(FromDIP(12));
        drag_sizer->Add(m_right_panel, 7, wxALIGN_CENTER | wxEXPAND);
    } else {
        m_right_panel->SetMinSize({FromDIP(550), -1});
        drag_sizer->AddStretchSpacer(1);
        drag_sizer->Add(m_right_panel, 0, wxALIGN_CENTER);
        drag_sizer->AddStretchSpacer(1);
    }

    top_sizer->Add(drag_sizer, 0, wxEXPAND);

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

    m_timer = new wxTimer(this);
    Bind(wxEVT_TIMER, &FilamentMapMultiNozzlePanel::OnTimer, this);
    Bind(wxEVT_DRAG_DROP_COMPLETED, &FilamentMapMultiNozzlePanel::OnDragDropCompleted, this);

    SetSizer(top_sizer);
    SetMinSize(wxSize(FromDIP(580), -1));
    Layout();
    Fit();
    GUI::wxGetApp().UpdateDarkUIWin(this);
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
        m_left_panel->SetMinSize(wxSize(FromDIP(160), -1));
        m_right_panel->SetMinSize(wxSize(FromDIP(360), -1));

        m_left_panel->Layout();
        m_left_panel->Fit();
        m_right_panel->Layout();
        m_right_panel->Fit();

        wxSize left_best_size  = m_left_panel->GetBestSize();
        wxSize right_best_size = m_right_panel->GetBestSize();

        int max_height = std::max(left_best_size.GetHeight(), right_best_size.GetHeight());

        m_left_panel->SetMinSize(wxSize(FromDIP(160), max_height));
        m_right_panel->SetMinSize(wxSize(FromDIP(360), max_height));
    } else {
        m_right_panel->Layout();
        m_right_panel->Fit();
    }

    Layout();
    Fit();

    if (GetParent()) {
        GetParent()->Layout();
        GetParent()->Fit();
    }
}

void FilamentMapMultiNozzlePanel::OnDragDropCompleted(wxCommandEvent& event)
{
    if (!m_pending_height_sync) {
        m_pending_height_sync = true;
        CallAfter([this]() {
            m_pending_height_sync = false;
            SyncPanelHeights();
        });
    }
    event.Skip();
}

void FilamentMapMultiNozzlePanel::OnTimer(wxTimerEvent &)
{
    bool update_ui  = m_invalid_id != -1;
    bool send_event = update_ui || m_force_validation;

    m_invalid_id = -1;

    if (update_ui) {
        m_errors->Hide();
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
        event.SetInt(1);
        ProcessEvent(event);
        m_force_validation = false;
    }
}

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

void FilamentMapMultiNozzlePanel::UpdateNozzleVolumeType()
{
    auto preset_bundle = wxGetApp().preset_bundle;
    auto full_cfg = preset_bundle->full_config();
    auto nozzle_diameters = full_cfg.option<ConfigOptionFloatsNullable>("nozzle_diameter");
    const std::string active_diameter = (nozzle_diameters && !nozzle_diameters->values.empty())
        ? format_diameter_to_str(nozzle_diameters->values.front()) : std::string();

    std::set<NozzleVolumeType> types;
    auto full_stats = preset_bundle->extruder_full_stat.get_extruder_stats(0);
    for (size_t idx = 0; idx < full_stats.size(); ++idx) {
        if (m_ams_nozzle_idx >= 0 && static_cast<int>(idx) == m_ams_nozzle_idx)
            continue;
        const auto& slot = full_stats[idx];
        const std::string& diameter = slot.first;
        const std::string& vol = slot.second;
        if (vol.empty()) continue;
        if (!active_diameter.empty() && !diameter.empty() && format_diameter_to_str(std::atof(diameter.c_str())) != active_diameter)
            continue;
        types.insert(DevNozzle::ToNozzleVolumeType(vol));
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

    int total = preset_bundle->extruder_nozzle_stat.get_extruder_nozzle_count(0);

    if (m_right_panel->IsUseSeparation()) {
        int standard_count = preset_bundle->extruder_nozzle_stat.get_extruder_nozzle_count(0, NozzleVolumeType::nvtStandard);
        int highflow_count = preset_bundle->extruder_nozzle_stat.get_extruder_nozzle_count(0, NozzleVolumeType::nvtHighFlow);
        int tpu_count = preset_bundle->extruder_nozzle_stat.get_extruder_nozzle_count(0, NozzleVolumeType::nvtTPUHighFlow)
                      + preset_bundle->extruder_nozzle_stat.get_extruder_nozzle_count(0, NozzleVolumeType::nvtTPU285);

        if (has_ams) {
            NozzleVolumeType ams_type = nvtStandard;
            auto full_stats = preset_bundle->extruder_full_stat.get_extruder_stats(0);
            if ((int)full_stats.size() > m_ams_nozzle_idx && !full_stats[m_ams_nozzle_idx].second.empty()) {
                ams_type = DevNozzle::ToNozzleVolumeType(full_stats[m_ams_nozzle_idx].second);
            }

            wxString ams_label   = get_ams_nozzle_label(m_ams_nozzle_idx);
            wxString fixed_label = get_fixed_nozzle_label(m_total_nozzle_count, m_ams_nozzle_idx);

            int left_count = m_ams_slot_count;
            wxString left_title = wxString::Format(ams_label + "(%d) - %s", left_count, get_volume_type_label(ams_type));
            m_left_panel->UpdateLabel(left_title);

            int right_std = has_full_stat ? fixed_count_of(nvtStandard)
                                          : ((ams_type == nvtStandard) ? std::max(0, standard_count - 1) : standard_count);
            int right_hf  = has_full_stat ? fixed_count_of(nvtHighFlow)
                                          : ((ams_type == nvtHighFlow) ? std::max(0, highflow_count - 1) : highflow_count);
            int right_tpu = has_full_stat ? fixed_count_of(nvtTPU285)
                                          : ((ams_type == nvtTPUHighFlow || ams_type == nvtTPU285) ? std::max(0, tpu_count - 1) : tpu_count);

            wxString right_title;
            if (tpu_count > 0)
                right_title = wxString::Format(fixed_label + "(Std: %d, HF: %d, TPU285: %d)", right_std, right_hf, right_tpu);
            else
                right_title = wxString::Format(fixed_label + "(Std: %d, HF: %d)", right_std, right_hf);
            m_right_panel->UpdateLabel(right_title);
        } else {
            wxString title;
            if (tpu_count > 0)
                title = wxString::Format(_L("Nozzle (%d) - Std: %d, HF: %d, TPU285: %d"), m_total_nozzle_count, standard_count, highflow_count, tpu_count);
            else
                title = wxString::Format(_L("Nozzle (%d) - Std: %d, HF: %d"), m_total_nozzle_count, standard_count, highflow_count);
            m_right_panel->UpdateLabel(title);
        }
    } else {
        if (has_ams) {
            wxString ams_label   = get_ams_nozzle_label(m_ams_nozzle_idx);
            wxString fixed_label = get_fixed_nozzle_label(m_total_nozzle_count, m_ams_nozzle_idx);

            int left_count = m_ams_slot_count;
            wxString left_title = wxString::Format(ams_label + "(%d)", left_count);
            m_left_panel->UpdateLabel(left_title);

            int right_count = 0;
            if (has_full_stat)
                for (const auto &kv : fixed_counts) right_count += kv.second;
            else
                right_count = std::max(0, total - 1);
            wxString right_title = wxString::Format(fixed_label + "(%d)", right_count);
            m_right_panel->UpdateLabel(right_title);
        } else {
            wxString type_label;
            if (has_full_stat) {
                for (const auto &kv : fixed_counts) {
                    if (kv.second > 0) { type_label = get_volume_type_label(kv.first); break; }
                }
            }
            wxString title = type_label.empty()
                ? wxString::Format(_L("Nozzle (%d)"), m_total_nozzle_count)
                : wxString::Format(_L("Nozzle (%d) - %s"), m_total_nozzle_count, type_label);
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

    return FilamentMapPanel::Show(show);
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

FilamentMapSavingPanel::FilamentMapSavingPanel(wxWindow *parent, const wxString &description) : FilamentMapPanel(parent)
{
    SetBackgroundColour(*wxWHITE);

    const bool custom_text = !description.empty();

    auto saving_sizer = new wxBoxSizer(wxVERTICAL);
    saving_sizer->AddSpacer(FromDIP(32));

    if (!custom_text) {
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
    auto desc_label = new Label(this, desc_text);
    desc_label->SetFont(custom_text ? Label::Body_14 : Label::Body_12);
    desc_label->SetForegroundColour(wxColour("#6B6B6B"));
    if (custom_text) {
        desc_label->SetWindowStyleFlag(desc_label->GetWindowStyleFlag() | wxALIGN_CENTER_HORIZONTAL);
        saving_sizer->AddStretchSpacer(2);
        saving_sizer->Add(desc_label, 0, wxALIGN_CENTER | wxLEFT | wxRIGHT, FromDIP(50));
        saving_sizer->AddStretchSpacer(3);
    } else {
        saving_sizer->Add(desc_label, 0, wxALIGN_CENTER | wxLEFT | wxRIGHT, FromDIP(50));
    }
    saving_sizer->AddSpacer(FromDIP(32));

    SetSizer(saving_sizer);

    if (custom_text)
        SetMinSize(wxSize(-1, FromDIP(180)));
}

} // namespace Slic3r::GUI
