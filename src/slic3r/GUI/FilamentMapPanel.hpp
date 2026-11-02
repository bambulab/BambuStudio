#pragma once

#include "GUI.hpp"
#include "DragDropPanel.hpp"
#include <wx/string.h>
#include <set>
#include <vector>

namespace Slic3r::GUI {

wxDECLARE_EVENT(wxEVT_INVALID_MANUAL_MAP, wxCommandEvent);

std::vector<int> get_fixed_nozzle_indices(int total_count, int ams_index);
wxString get_ams_nozzle_label(int ams_index_0based);
wxString get_fixed_nozzle_label(int total_count, int ams_index_0based);
int get_effective_ams_nozzle_idx_for_active_diameter(const ConfigBase& config);
// Whether a groupable nozzle (installed, matching the active diameter unless in multi-diameter mode) is TPU285.
bool has_groupable_tpu285_nozzle(const ConfigBase& config);
bool is_multi_diameter_without_ams(const ConfigBase& config);
// Skip grouping when no AMS is connected and grouping cannot make a useful choice.
bool should_skip_filament_grouping(const ConfigBase& config);
// Show custom grouping only when the current nozzle topology provides a meaningful drag target.
bool can_customize_filament_grouping(const ConfigBase& config);
// Auto grouping modes offered on single-extruder multi-nozzle machines, shared by every grouping entry.
std::vector<FilamentMapMode> get_single_ext_multi_nozzle_auto_modes(const ConfigBase& config);
class FilamentMapBtnPanel;

class FilamentMapPanel : public wxPanel
{
public:
    FilamentMapPanel(wxWindow *parent) : wxPanel(parent) {};
    virtual FilamentMapMode GetMode() const = 0;
};

class FilamentMapManualPanel : public FilamentMapPanel
{
public:
    FilamentMapManualPanel(wxWindow                       *parent,
                           const std::vector<std::string> &type,
                           const std::vector<int>         &filament_list,
                           const std::vector<int>         &filament_map,
                           const std::vector<int>         &filament_volume_map);
    ~FilamentMapManualPanel();
    std::vector<int> GetFilamentMaps() const;
    std::vector<int> GetFilamentVolumeMaps() const;
    std::vector<int> GetLeftFilaments() const { return m_left_panel->GetAllFilaments(); }
    std::vector<int> GetRightFilaments() const { return m_right_panel->GetAllFilaments(); }

    std::vector<int> GetRightHighFlowFilaments() const { return m_right_panel->GetHighFlowFilaments(); }
    std::vector<int> GetRightStandardFilaments() const { return m_right_panel->GetStandardFilaments(); }
    std::vector<int> GetRightTPUHighFlowFilaments() const { return m_right_panel->GetTPUHighFlowFilaments(); }
    void UpdateNozzleVolumeType();
    void UpdateNozzleCountDisplay();

    bool Show(bool show = true) override;

    FilamentMapMode GetMode() const override { return FilamentMapMode::fmmManual; }

private:
    void OnTimer(wxTimerEvent &evt);
    void OnSwitchFilament(wxCommandEvent &);
    void SyncPanelHeights();
    void OnDragDropCompleted(wxCommandEvent &evt);
    void OnSuggestionClicked(wxCommandEvent& event);
    DragDropPanel *m_left_panel;
    SeparatedDragDropPanel *m_right_panel;

    Label *m_description;
    Label *m_tips;
    Label *m_errors;
    wxPanel *m_suggestion_panel;

    ScalableButton *m_switch_btn;

    std::vector<int>         m_filament_map;
    std::vector<int>         m_filament_volume_map;
    std::vector<int>         m_filament_list;
    std::vector<std::string> m_filament_type;
    std::vector<std::set<std::string>> m_allowed_diameters;
    std::vector<std::set<std::string>> m_extruder_diameters;
    std::vector<std::string>           m_cached_selected_diameters;
    std::vector<std::string>           m_cached_filament_presets;
    std::string                        m_cached_printer_model;
    std::string                        m_cached_printer_preset;
    wxTimer* m_timer;
    int m_invalid_id{ -1 };
    bool m_force_validation{ false };
    // Multi-diameter safety net: last diameter-mismatch message shown (empty = none), used to drive the
    // OnTimer UI refresh independently of the nozzle-availability error state.
    wxString m_diameter_error;
};

class FilamentMapMultiNozzlePanel : public FilamentMapPanel
{
public:
    FilamentMapMultiNozzlePanel(wxWindow                       *parent,
                        const std::vector<std::string> &type,
                        const std::vector<int>         &filament_list,
                        const std::vector<int>         &filament_map,
                        const std::vector<int>         &filament_volume_map);
    ~FilamentMapMultiNozzlePanel();
    std::vector<int> GetFilamentMaps() const;
    std::vector<int> GetFilamentVolumeMaps() const;
    std::vector<int> GetLeftFilaments() const { return m_left_panel ? m_left_panel->GetAllFilaments() : std::vector<int>{}; }
    std::vector<int> GetRightFilaments() const { return m_right_panel->GetAllFilaments(); }

    std::vector<int> GetLeftHighFlowFilaments() const { return m_left_panel ? m_left_panel->GetHighFlowFilaments() : std::vector<int>{}; }
    std::vector<int> GetLeftStandardFilaments() const { return m_left_panel ? m_left_panel->GetStandardFilaments() : std::vector<int>{}; }
    std::vector<int> GetLeftTPUHighFlowFilaments() const { return m_left_panel ? m_left_panel->GetTPUHighFlowFilaments() : std::vector<int>{}; }

    std::vector<int> GetRightHighFlowFilaments() const { return m_right_panel->GetHighFlowFilaments(); }
    std::vector<int> GetRightStandardFilaments() const { return m_right_panel->GetStandardFilaments(); }
    std::vector<int> GetRightTPUHighFlowFilaments() const { return m_right_panel->GetTPUHighFlowFilaments(); }
    void UpdateNozzleVolumeType();
    void UpdateNozzleCountDisplay();

    bool Show(bool show = true) override;

    FilamentMapMode GetMode() const override { return FilamentMapMode::fmmManual; }

private:
    void OnTimer(wxTimerEvent &evt);
    void SyncPanelHeights();
    void OnDragDropCompleted(wxCommandEvent &evt);
    void OnDragHover(wxCommandEvent &evt);

    SeparatedDragDropPanel *m_left_panel;
    SeparatedDragDropPanel *m_right_panel;

    Label   *m_description;
    Label   *m_tips;
    Label   *m_errors;

    std::vector<int>         m_filament_map;
    std::vector<int>         m_filament_volume_map;
    std::vector<int>         m_filament_list;
    std::vector<std::string> m_filament_type;
    std::vector<std::set<std::string>> m_allowed_diameters;
    std::vector<std::string>           m_cached_selected_diameters;
    std::vector<std::string>           m_cached_filament_presets;
    std::string                        m_cached_printer_model;
    std::string                        m_cached_printer_preset;
    wxString                           m_diameter_error;
    wxTimer *m_timer;
    int      m_invalid_id{-1};
    bool     m_force_validation{false};
    int      m_ams_nozzle_idx{0};
    bool     m_topology_changed{false};
    int      m_total_nozzle_count{1};
    bool     m_pending_height_sync{false};
    bool     m_show_drag_unsupported_tip{false};
};

class FilamentMapAutoPanel : public FilamentMapPanel
{
public:
    FilamentMapAutoPanel(wxWindow *parent, FilamentMapMode mode, bool machine_synced, const std::vector<FilamentMapMode> &available_modes = {});
    FilamentMapMode GetMode() const override { return m_mode; }

private:
    void OnModeSwitch(FilamentMapMode mode);
    void UpdateStatus();

    static std::string GetIconForMode(FilamentMapMode mode);

    std::vector<FilamentMapBtnPanel*> m_mode_panels;
    std::vector<FilamentMapMode> m_available_modes;

    FilamentMapMode      m_mode;
    bool                 m_machine_synced;
};

class FilamentMapSavingPanel : public FilamentMapPanel
{
public:
    FilamentMapSavingPanel(wxWindow *parent, const wxString &description = wxString(), FilamentMapMode mode = FilamentMapMode::fmmAutoForFlush);
    FilamentMapMode GetMode() const override { return m_mode; }

private:
    FilamentMapMode m_mode{FilamentMapMode::fmmAutoForFlush};
};

} // namespace Slic3r::GUI