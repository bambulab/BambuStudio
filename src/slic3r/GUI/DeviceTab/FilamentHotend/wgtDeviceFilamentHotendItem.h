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
class ScalableBitmap;
class ScalableButton;
class StaticBox;
class wgtCircleItem;
class wxPanel;
class wxStaticBitmap;

wxDECLARE_EVENT(EVT_DEVICE_FILAMENT_HOTEND_ITEM_SELECTION_CHANGED, wxCommandEvent);

namespace Slic3r {
class MachineObject;
}

namespace Slic3r::GUI {

class AMSMaterialsSetting;
class wgtDeviceFilamentHotendItemSpool;
class wgtDeviceFilamentHotendItemTube;
class wgtDeviceFilamentHotendItemHotend;

enum class DeviceFilamentHotendCardState {
    Reading,
    Loading,
    Unloading,
    ReadyToLoad,
    ReadyToUse,
    Empty,
    Unknown,
    Error
};

enum class DeviceFilamentHotendLineState {
    None,
    Standby,
    Transporting,
    AtNozzle,
    Error
};

enum class DeviceFilamentHotendNozzleState {
    Normal,
    Active,
    Unknown,
    Error
};

class wgtDeviceFilamentHotendItem : public wxPanel
{
public:
    explicit wgtDeviceFilamentHotendItem(wxWindow *parent);
    ~wgtDeviceFilamentHotendItem() override {};

    const std::string& GetAmsId() const { return m_ams_id; }
    const std::string& GetSlotId() const { return m_slot_id; }

    void SetNozzlePos(int index);
    int GetNozzlePos() const { return m_nozzle_pos; }
    void SetLayoutTier(bool top_tier);
    DeviceFilamentHotendNozzleState GetNozzleState() const { return m_nozzle_state; }

    void SetRunning(bool running);
    void SetIsReading(bool is_reading);
    void UpdateByMachine(Slic3r::MachineObject* machine_obj, std::optional<DevAmsSlotId> ams_slot_id);

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
    wxString ResolveCardStateDisplayText(DeviceFilamentHotendCardState card_state) const;

    DeviceFilamentHotendCardState ResolveCardState(Slic3r::MachineObject* machine_obj, const Slic3r::DevAmsTray& tray) const;
    DeviceFilamentHotendLineState ResolveLineState(DeviceFilamentHotendCardState card_state) const;
    DeviceFilamentHotendNozzleState ResolveNozzleState(DeviceFilamentHotendCardState card_state, DeviceFilamentHotendLineState line_state) const;
    bool IsSlotLoadedInExtruder(Slic3r::MachineObject* machine_obj) const;

    void RefreshVisualState();
    void NotifySelectionChanged();

    //  events
    void OnMenuButton(wxCommandEvent &event);
    void OnMenuEdit(wxCommandEvent &event);
    void OnMenuLoad(wxCommandEvent &event);
    void OnMenuUnLoad(wxCommandEvent &event);
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
    Slic3r::MachineObject*                 m_machine_obj{ nullptr };

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
    bool                                  m_is_top_tier{ false };
    DeviceFilamentHotendCardState         m_card_state{ DeviceFilamentHotendCardState::Empty };
    DeviceFilamentHotendLineState         m_line_state{ DeviceFilamentHotendLineState::Standby };
    DeviceFilamentHotendNozzleState       m_nozzle_state{ DeviceFilamentHotendNozzleState::Normal };
};


class wgtDeviceFilamentHotendItemTube : public wxPanel
{
public:
    explicit wgtDeviceFilamentHotendItemTube(wxWindow* parent);
    ~wgtDeviceFilamentHotendItemTube() override = default;

    void SetTubeColor(const wxColour& color);
    void SetTubeVisible(bool visible);
    void SetLineState(DeviceFilamentHotendLineState state);
    void SetLineHeight(int height_dip);

private:
    void CreateGui();
    void ApplyBitmapByState(DeviceFilamentHotendLineState state);

private:
    wxStaticBitmap* m_tube_bitmap_widget{ nullptr };
    ScalableBitmap* m_tube_bitmap_standby{ nullptr };
    ScalableBitmap* m_tube_bitmap_transporting{ nullptr };
    ScalableBitmap* m_tube_bitmap_at_nozzle{ nullptr };
    ScalableBitmap* m_tube_bitmap_error{ nullptr };
    DeviceFilamentHotendLineState m_tube_line_state{ DeviceFilamentHotendLineState::Standby };
    int m_line_height_dip{ 92 };
};

class wgtDeviceFilamentHotendItemSpool : public wxPanel
{
public:
    wgtDeviceFilamentHotendItemSpool(wxWindow* parent, const std::vector<std::string>& reading_icon_list);
    ~wgtDeviceFilamentHotendItemSpool() override = default;

    void SetFilamentType(const wxString& filament_type);
    void SetWeight(const wxString& weight); // kept for compatibility, same as SetStatusText
    void SetStatusText(const wxString& status_text);
    void SetReading(bool is_reading);
    void ApplyVisualState(const wxColour& type_text_fg,
                          const wxColour& status_text_fg,
                          const wxColour& ring_color,
                          const wxColour& center_color,
                          bool show_type,
                          bool show_status,
                          bool show_reading,
                          bool show_slash);
    void RefreshSpool();
    void UpdateSpoolLayout();

private:
    void CreateGui(const std::vector<std::string>& reading_icon_list);

private:
    wxPanel* m_panel_disc_outer{ nullptr };
    wxPanel* m_panel_text_area{ nullptr };
    Label* m_label_type{ nullptr };
    Label* m_label_status{ nullptr };
    AnimaIcon* m_reading_icon{ nullptr };
    wxColour m_disc_ring_color{ wxColour("#D9D9D9") };
    wxColour m_disc_center_color{ wxColour("#E9E9E9") };
    bool m_disc_show_slash{ false };
};

class wgtDeviceFilamentHotendItemHotend : public wxPanel
{
public:
    explicit wgtDeviceFilamentHotendItemHotend(wxWindow* parent);
    ~wgtDeviceFilamentHotendItemHotend() override = default;

    void SetHotendState(DeviceFilamentHotendNozzleState state);
    void SetHotendColor(const wxColour& color);

private:
    void CreateGui();

private:
    wxStaticBitmap* m_hotend_bitmap_widget{ nullptr };
    ScalableBitmap* m_hotend_bitmap_normal{ nullptr };
    ScalableBitmap* m_hotend_bitmap_active{ nullptr };
    ScalableBitmap* m_hotend_bitmap_unknown{ nullptr };
    ScalableBitmap* m_hotend_bitmap_error{ nullptr };
    DeviceFilamentHotendNozzleState m_hotend_state{ DeviceFilamentHotendNozzleState::Normal };
};


} // namespace Slic3r::GUI
