#pragma once

#include <wx/colour.h>
#include <wx/dialog.h>
#include <wx/panel.h>
#include <wx/string.h>
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include <chrono>
#include <memory>
#include <string>
#include <vector>

class Label;
class AnimaIcon;
class ScalableButton;
class StaticBox;
class wgtCircleItem;
class wxPanel;

wxDECLARE_EVENT(EVT_DEVICE_FILAMENT_HOTEND_ITEM_SELECTION_CHANGED, wxCommandEvent);

namespace Slic3r::GUI {

class AMSMaterialsSetting;
class wgtDeviceFilamentHotendItemSpool;
class wgtDeviceFilamentHotendItemTube;
class wgtDeviceFilamentHotendItemHotend;

class wgtDeviceFilamentHotendItem : public wxPanel
{
public:
    explicit wgtDeviceFilamentHotendItem(wxWindow *parent);
    ~wgtDeviceFilamentHotendItem() override {};

    const std::string& GetAmsId() const { return m_ams_id; }
    const std::string& GetSlotId() const { return m_slot_id; }

    void SetNozzlePos(int index);
    int GetNozzlePos() const { return m_nozzle_pos; }

    void SetRunning(bool running);
    void SetIsReading(bool is_reading);
    void UpdateBy(std::shared_ptr<DevFilaSystem> fila_system, std::optional<DevAmsSlotId> ams_slot_id);

    void SetSelected(bool selected, bool notify = false);
    bool IsSelected() const { return m_selected; }
    void SetEnableSelection(bool enable) { m_enable_selection = enable; }

    bool IsEnabledState() const { return m_enabled_state; }


private:
    void CreateGui();

    void Reset();
    void SetFilamentType(const wxString &type);
    void SetFilamentColor(const wxColour &color);
    void SetHasFilament(bool has_filament);
    void SetEnabledState(bool enabled);
    void SetWeight(const wxString &weight);

    void RefreshVisualState();
    void NotifySelectionChanged();

    //  events
    void OnMenuButton(wxCommandEvent &event);
    void OnMenuEdit(wxCommandEvent &event);
    void OnMenuLoad(wxCommandEvent &event);
    void OnMenuRead(wxCommandEvent &event);
    void ShowMenuPopup();
    void DismissMenuPopup();

private:
    StaticBox      *m_panel_card{nullptr};
    wgtCircleItem  *m_badge_index{nullptr};
    ScalableButton *m_btn_menu{nullptr};
    wgtDeviceFilamentHotendItemSpool *m_panel_spool{nullptr};
    wgtDeviceFilamentHotendItemTube *m_panel_tube{nullptr};
    wgtDeviceFilamentHotendItemHotend *m_panel_hotend{nullptr};

    // popup
    wxDialog                            *m_menu_popup{nullptr};
    std::chrono::steady_clock::time_point m_menu_dismiss_time{};

    // dialog
    AMSMaterialsSetting* m_filament_setting_dlg{ nullptr };

    // data
    int                                   m_nozzle_pos{-1};
    std::string                           m_ams_id;
    std::string                           m_slot_id;
    std::weak_ptr<Slic3r::DevFilaSystem>  m_fila_system;

    // states
    wxString                              m_filament_type{ "?" };
    wxString                              m_weight_text;
    wxColour                              m_filament_color{ wxColour("#D9D9D9") };
    bool                                  m_has_filament{ false };
    bool                                  m_is_reading{ false };
    bool                                  m_selected{ false };
    bool                                  m_running{ false };
    bool                                  m_enable_selection{ true };
    bool                                  m_enabled_state{ false };
    bool                                  m_visual_dirty{ true };
    bool                                  m_layout_dirty{ true };
};


class wgtDeviceFilamentHotendItemTube : public wxPanel
{
public:
    explicit wgtDeviceFilamentHotendItemTube(wxWindow* parent);
    ~wgtDeviceFilamentHotendItemTube() override = default;

    void SetTubeColor(const wxColour& color);
    void SetTubeVisible(bool visible);

private:
    void CreateGui();

private:
    wxPanel* m_panel_tube_line{ nullptr };
};

class wgtDeviceFilamentHotendItemSpool : public wxPanel
{
public:
    wgtDeviceFilamentHotendItemSpool(wxWindow* parent, const std::vector<std::string>& reading_icon_list);
    ~wgtDeviceFilamentHotendItemSpool() override = default;

    void SetFilamentType(const wxString& filament_type);
    void SetWeight(const wxString& weight);
    void SetReading(bool is_reading);
    void ApplyVisualState(const wxColour& text_fg,
                          const wxColour& rail_color,
                          const wxColour& spool_line,
                          const wxColour& block_color,
                          const wxColour& weight_text_fg,
                          bool show_type,
                          bool show_weight,
                          bool show_reading);
    void RefreshSpool();
    void UpdateSpoolLayout();

private:
    void CreateGui(const std::vector<std::string>& reading_icon_list);

private:
    wxPanel* m_panel_spool_left_bar{ nullptr };
    wxPanel* m_panel_spool_center{ nullptr };
    wxPanel* m_panel_spool_top_line{ nullptr };
    wxPanel* m_panel_spool_right_bar{ nullptr };
    Label* m_label_type{ nullptr };
    wxPanel* m_panel_color_block{ nullptr };
    Label* m_label_weight{ nullptr };
    AnimaIcon* m_reading_icon{ nullptr };
};

class wgtDeviceFilamentHotendItemHotend : public wxPanel
{
public:
    explicit wgtDeviceFilamentHotendItemHotend(wxWindow* parent);
    ~wgtDeviceFilamentHotendItemHotend() override = default;

    void SetHotendColor(const wxColour& color);

private:
    void CreateGui();

private:
    wxPanel* m_panel_hotend_top{ nullptr };
    wxPanel* m_panel_hotend_bottom{ nullptr };
};


} // namespace Slic3r::GUI
