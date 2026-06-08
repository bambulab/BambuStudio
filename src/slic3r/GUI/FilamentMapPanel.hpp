#pragma once

#include "GUI.hpp"
#include "DragDropPanel.hpp"
#include <wx/string.h>

namespace Slic3r::GUI {

wxDECLARE_EVENT(wxEVT_INVALID_MANUAL_MAP, wxCommandEvent);
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
                           const std::vector<std::string> &color,
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
    std::vector<std::string> m_filament_color;
    std::vector<std::string> m_filament_type;
    wxTimer* m_timer;
    int m_invalid_id{ -1 };
    bool m_force_validation{ false };
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
    FilamentMapSavingPanel(wxWindow *parent);
    FilamentMapMode GetMode() const override { return FilamentMapMode::fmmAutoForFlush; }
};

} // namespace Slic3r::GUI