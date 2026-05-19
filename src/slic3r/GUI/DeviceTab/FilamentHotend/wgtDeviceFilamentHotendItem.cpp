#include "wgtDeviceFilamentHotendItem.h"
#include "slic3r/GUI/AMSMaterialsSetting.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/Widgets/AnimaController.hpp"
#include "slic3r/GUI/Widgets/Button.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"
#include "slic3r/GUI/Widgets/StaticBox.hpp"
#include "slic3r/GUI/Widgets/wgtCircleItem.hpp"
#include "slic3r/GUI/wxExtensions.hpp"

#include "slic3r/GUI/DeviceCore/DevDefs.h"
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"

#include <algorithm>
#include <wx/dcbuffer.h>
#include <wx/display.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>

wxDEFINE_EVENT(EVT_DEVICE_FILAMENT_HOTEND_ITEM_SELECTION_CHANGED, wxCommandEvent);

// Private impls
namespace Slic3r::GUI {

namespace {

enum FilamentHotendMenuActionId {
    ID_FILAMENT_HOTEND_MENU_EDIT = wxID_HIGHEST + 510,
    ID_FILAMENT_HOTEND_MENU_LOAD,
    ID_FILAMENT_HOTEND_MENU_UNLOAD,
    ID_FILAMENT_HOTEND_MENU_READ
};

wxString format_weight(const std::string& value)
{
    if (!value.empty() && value != "0") {
        const wxString& weight = wxString::FromUTF8(value);
        if (!weight.EndsWith("g")) {
            return weight + "g";
        }
    }

    return "? g";
}

wxColour get_weight_text_color(const wxColour &color)
{
    const int luminance = (color.Red() * 299 + color.Green() * 587 + color.Blue() * 114) / 1000;
    return luminance >= 160 ? wxColour("#323232") : *wxWHITE;
}

wxColour get_reading_color() { return wxColour("#1E39F6"); }

int nozzle_pos_to_display_idx(int nozzle_pos)
{
    return nozzle_pos >= 0x10 ? (nozzle_pos - 0x10 + 1) : (nozzle_pos + 1);
}

class wgtFilamentHotendMenuDialog final : public wxDialog
{
public:
    wgtFilamentHotendMenuDialog(wxWindow *parent)
        : wxDialog(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxFRAME_NO_TASKBAR | wxSTAY_ON_TOP)
    {
#ifdef __WINDOWS__
        SetDoubleBuffered(true);
#endif
        auto *root = new wxBoxSizer(wxVERTICAL);
        root->Add(CreateActionButton(_L("Edit"), ID_FILAMENT_HOTEND_MENU_EDIT), 0, wxEXPAND);
        root->Add(CreateActionButton(_L("Load"), ID_FILAMENT_HOTEND_MENU_LOAD), 0, wxEXPAND);
        root->Add(CreateActionButton(_L("Unload"), ID_FILAMENT_HOTEND_MENU_UNLOAD), 0, wxEXPAND);
        root->Add(CreateActionButton(_L("Refresh"), ID_FILAMENT_HOTEND_MENU_READ), 0, wxEXPAND);
        SetSizerAndFit(root);
        SetBackgroundColour(*wxWHITE);
        SetCanFocus(false);
    }

private:
    Button* CreateActionButton(const wxString& label, wxWindowID id)
    {
        auto* button = new Button(this, label, "", wxBORDER_NONE, 0, id);
        button->SetMinSize(wxSize(FromDIP(120), FromDIP(25)));
        button->SetPaddingSize(wxSize(FromDIP(14), FromDIP(10)));
        button->SetCenter(false);
        button->SetTextColorNormal(wxColour("#262E30"));
        button->SetCanFocus(false);
        button->SetBackgroundColour(*wxWHITE);
        return button;
    }
};

} // namespace

wgtDeviceFilamentHotendItemTube::wgtDeviceFilamentHotendItemTube(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
{
    CreateGui();
}

void wgtDeviceFilamentHotendItemTube::SetTubeColor(const wxColour& color)
{
    const bool active = (color != wxColour("#D9D9D9"));
    ApplyBitmapByState(active ? DeviceFilamentHotendLineState::Transporting : DeviceFilamentHotendLineState::Standby);
}

void wgtDeviceFilamentHotendItemTube::SetTubeVisible(bool visible)
{
    if (m_tube_bitmap_widget) {
        m_tube_bitmap_widget->Show(visible);
    }
    Layout();
    Refresh();
}

void wgtDeviceFilamentHotendItemTube::SetLineState(DeviceFilamentHotendLineState state)
{
    ApplyBitmapByState(state);
}

void wgtDeviceFilamentHotendItemTube::SetLineHeight(int height_dip)
{
    if (height_dip <= 0 || m_line_height_dip == height_dip) {
        return;
    }

    m_line_height_dip = height_dip;
    delete m_tube_bitmap_standby;
    delete m_tube_bitmap_transporting;
    delete m_tube_bitmap_at_nozzle;
    delete m_tube_bitmap_error;
    m_tube_bitmap_standby = new ScalableBitmap(this, "dev_filament_hotend_line_standby", m_line_height_dip);
    m_tube_bitmap_transporting = new ScalableBitmap(this, "dev_filament_hotend_line_transporting", m_line_height_dip);
    m_tube_bitmap_at_nozzle = new ScalableBitmap(this, "dev_filament_hotend_line_at_nozzle", m_line_height_dip);
    m_tube_bitmap_error = new ScalableBitmap(this, "dev_filament_hotend_line_error", m_line_height_dip);

    if (m_tube_bitmap_widget) {
        m_tube_bitmap_widget->SetMinSize(wxSize(FromDIP(12), FromDIP(m_line_height_dip)));
        m_tube_bitmap_widget->SetMaxSize(wxSize(FromDIP(12), FromDIP(m_line_height_dip)));
    }
    ApplyBitmapByState(m_tube_line_state);
    Layout();
}

void wgtDeviceFilamentHotendItemTube::ApplyBitmapByState(DeviceFilamentHotendLineState state)
{
    if (!m_tube_bitmap_widget || !m_tube_bitmap_standby || !m_tube_bitmap_transporting || !m_tube_bitmap_at_nozzle || !m_tube_bitmap_error) {
        return;
    }

    if (m_tube_line_state == state && m_tube_bitmap_widget->GetBitmap().IsOk()) {
        return;
    }

    m_tube_line_state = state;
    ScalableBitmap* target_bitmap = m_tube_bitmap_standby;
    switch (m_tube_line_state) {
    case DeviceFilamentHotendLineState::None:
        m_tube_bitmap_widget->Show(false);
        break;
    case DeviceFilamentHotendLineState::Standby:
        m_tube_bitmap_widget->Show(true);
        target_bitmap = m_tube_bitmap_standby;
        break;
    case DeviceFilamentHotendLineState::Transporting:
        m_tube_bitmap_widget->Show(true);
        target_bitmap = m_tube_bitmap_transporting;
        break;
    case DeviceFilamentHotendLineState::AtNozzle:
        m_tube_bitmap_widget->Show(true);
        target_bitmap = m_tube_bitmap_at_nozzle;
        break;
    case DeviceFilamentHotendLineState::Error:
        m_tube_bitmap_widget->Show(true);
        target_bitmap = m_tube_bitmap_error;
        break;
    default:
        m_tube_bitmap_widget->Show(true);
        target_bitmap = m_tube_bitmap_standby;
        break;
    }

    if (m_tube_bitmap_widget->IsShown()) {
        m_tube_bitmap_widget->SetBitmap(target_bitmap->bmp());
    }
    m_tube_bitmap_widget->Refresh();
}

void wgtDeviceFilamentHotendItemTube::CreateGui()
{
    SetBackgroundColour(*wxWHITE);

    auto* tube_sizer = new wxBoxSizer(wxHORIZONTAL);
    tube_sizer->AddStretchSpacer(1);

    m_tube_bitmap_standby = new ScalableBitmap(this, "dev_filament_hotend_line_standby", m_line_height_dip);
    m_tube_bitmap_transporting = new ScalableBitmap(this, "dev_filament_hotend_line_transporting", m_line_height_dip);
    m_tube_bitmap_at_nozzle = new ScalableBitmap(this, "dev_filament_hotend_line_at_nozzle", m_line_height_dip);
    m_tube_bitmap_error = new ScalableBitmap(this, "dev_filament_hotend_line_error", m_line_height_dip);
    m_tube_bitmap_widget = new wxStaticBitmap(this, wxID_ANY, m_tube_bitmap_standby->bmp());
    m_tube_bitmap_widget->SetMinSize(wxSize(FromDIP(12), FromDIP(m_line_height_dip)));
    m_tube_bitmap_widget->SetMaxSize(wxSize(FromDIP(12), FromDIP(m_line_height_dip)));
    tube_sizer->Add(m_tube_bitmap_widget, 0, wxEXPAND | wxALIGN_CENTER_HORIZONTAL);

    tube_sizer->AddStretchSpacer(1);
    SetSizer(tube_sizer);
    ApplyBitmapByState(DeviceFilamentHotendLineState::Standby);
}

wgtDeviceFilamentHotendItemSpool::wgtDeviceFilamentHotendItemSpool(wxWindow* parent, const std::vector<std::string>& reading_icon_list)
    : wxPanel(parent, wxID_ANY)
{
    CreateGui(reading_icon_list);
}

void wgtDeviceFilamentHotendItemSpool::SetFilamentType(const wxString& filament_type)
{
    if (m_label_type) {
        m_label_type->SetLabel(filament_type);
    }
}

void wgtDeviceFilamentHotendItemSpool::SetWeight(const wxString& weight)
{
    SetStatusText(weight);
}

void wgtDeviceFilamentHotendItemSpool::SetStatusText(const wxString& status_text)
{
    if (m_label_status) {
        m_label_status->SetLabel(status_text);
        m_label_status->Show(!status_text.empty());
    }
}

void wgtDeviceFilamentHotendItemSpool::SetReading(bool is_reading)
{
    if (!m_reading_icon) {
        return;
    }

    if (is_reading) {
        if (!m_reading_icon->IsPlaying()) {
            m_reading_icon->Play();
        }
        m_reading_icon->Show();
    } else {
        if (m_reading_icon->IsPlaying()) {
            m_reading_icon->Stop();
        }
        m_reading_icon->Hide();
    }
}

void wgtDeviceFilamentHotendItemSpool::ApplyVisualState(const wxColour& type_text_fg,
                                                        const wxColour& status_text_fg,
                                                        const wxColour& ring_color,
                                                        const wxColour& center_color,
                                                        bool show_type,
                                                        bool show_status,
                                                        bool show_reading,
                                                        bool show_slash)
{
    if (m_label_type) {
        m_label_type->SetForegroundColour(type_text_fg);
        m_label_type->Show(show_type);
    }

    if (m_label_status) {
        m_label_status->SetForegroundColour(status_text_fg);
        m_label_status->Show(show_status);
        m_label_status->Refresh();
    }

    m_disc_ring_color = ring_color;
    m_disc_center_color = center_color;
    m_disc_show_slash = !show_reading && show_slash;
    if (m_panel_disc_outer) {
        m_panel_disc_outer->Refresh();
    }
    if (m_reading_icon) {
        m_reading_icon->SetBackgroundColour(center_color);
        m_reading_icon->Show(show_reading);
        m_reading_icon->Refresh();
    }
}

void wgtDeviceFilamentHotendItemSpool::RefreshSpool()
{
    Refresh();
}

void wgtDeviceFilamentHotendItemSpool::UpdateSpoolLayout()
{
    if (m_panel_disc_outer) {
        m_panel_disc_outer->Layout();
        m_panel_disc_outer->Refresh();
    }
    Layout();
}

void wgtDeviceFilamentHotendItemSpool::CreateGui(const std::vector<std::string>& reading_icon_list)
{
    SetBackgroundColour(wxColour("#F2F2F2"));
    SetMinSize(wxSize(FromDIP(88), FromDIP(90)));
    SetMaxSize(wxSize(FromDIP(88), FromDIP(90)));

    auto* spool_sizer = new wxBoxSizer(wxVERTICAL);

    m_panel_disc_outer = new wxPanel(this, wxID_ANY);
    m_panel_disc_outer->SetMinSize(wxSize(FromDIP(52), FromDIP(52)));
    m_panel_disc_outer->SetMaxSize(wxSize(FromDIP(52), FromDIP(52)));
    m_panel_disc_outer->SetBackgroundColour(wxColour("#F2F2F2"));
    m_panel_disc_outer->SetBackgroundStyle(wxBG_STYLE_PAINT);
    m_panel_disc_outer->Bind(wxEVT_PAINT, [this](wxPaintEvent& evt) {
        wxAutoBufferedPaintDC dc(m_panel_disc_outer);
        dc.SetBackground(wxBrush(wxColour("#F2F2F2")));
        dc.Clear();

        wxRect rect = m_panel_disc_outer->GetClientRect();
        if (rect.width <= 0 || rect.height <= 0) {
            evt.Skip();
            return;
        }

        const int cx = rect.width / 2;
        const int cy = rect.height / 2;
        const int outer_r = std::min(rect.width, rect.height) / 2 - FromDIP(1);
        const int ring_r = std::max(outer_r - FromDIP(5), FromDIP(1));
        const int center_r = std::max(outer_r - FromDIP(16), FromDIP(1));

        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(wxColour("#CFCFCF")));
        dc.DrawCircle(cx, cy, outer_r);

        dc.SetBrush(wxBrush(m_disc_ring_color));
        dc.DrawCircle(cx, cy, ring_r);

        dc.SetBrush(wxBrush(m_disc_center_color));
        dc.DrawCircle(cx, cy, center_r);

        if (m_disc_show_slash) {
            // Use a real diagonal stroke for empty-slot mark to match UX.
            wxPen slash_pen(wxColour("#BEBEBE"), FromDIP(2));
            dc.SetPen(slash_pen);
            dc.DrawLine(cx - FromDIP(5), cy + FromDIP(5), cx + FromDIP(5), cy - FromDIP(5));
        }
    });
    auto* outer_sizer = new wxBoxSizer(wxVERTICAL);
    outer_sizer->AddStretchSpacer(1);

    m_reading_icon = new AnimaIcon(m_panel_disc_outer, wxID_ANY, reading_icon_list, "refresh_printer", 100);
    m_reading_icon->SetMinSize(wxSize(FromDIP(20), FromDIP(20)));
    m_reading_icon->Hide();
    outer_sizer->Add(m_reading_icon, 0, wxALIGN_CENTER_HORIZONTAL);
    outer_sizer->AddStretchSpacer(1);
    m_panel_disc_outer->SetSizer(outer_sizer);
    spool_sizer->Add(m_panel_disc_outer, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(2));

    spool_sizer->AddStretchSpacer(1);

    m_panel_text_area = new wxPanel(this, wxID_ANY);
    m_panel_text_area->SetBackgroundColour(*wxWHITE);
    m_panel_text_area->SetBackgroundStyle(wxBG_STYLE_PAINT);
    m_panel_text_area->SetMinSize(wxSize(FromDIP(88), FromDIP(34)));
    m_panel_text_area->SetMaxSize(wxSize(FromDIP(88), FromDIP(34)));
    m_panel_text_area->Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(m_panel_text_area);
        dc.SetBackground(wxBrush(*wxWHITE));
        dc.Clear();
        dc.SetPen(wxPen(wxColour("#E5E5E5"), 1));
        dc.DrawLine(0, 0, m_panel_text_area->GetClientSize().GetWidth(), 0);
    });
    auto* text_sizer = new wxBoxSizer(wxVERTICAL);

    m_label_type = new Label(m_panel_text_area, "?");
    m_label_type->SetFont(Label::Body_12);
    text_sizer->Add(m_label_type, 0, wxLEFT | wxTOP, FromDIP(4));

    m_label_status = new Label(m_panel_text_area, wxEmptyString);
    m_label_status->SetFont(Label::Body_12);
    text_sizer->Add(m_label_status, 0, wxLEFT | wxTOP, FromDIP(0));

    m_panel_text_area->SetSizer(text_sizer);
    spool_sizer->Add(m_panel_text_area, 0, wxEXPAND | wxTOP, FromDIP(0));

    SetSizer(spool_sizer);
}

wgtDeviceFilamentHotendItemHotend::wgtDeviceFilamentHotendItemHotend(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
{
    CreateGui();
}

void wgtDeviceFilamentHotendItemHotend::SetHotendColor(const wxColour& color)
{
    const bool active = (color != wxColour("#D9D9D9"));
    SetHotendState(active ? DeviceFilamentHotendNozzleState::Active : DeviceFilamentHotendNozzleState::Normal);
}

void wgtDeviceFilamentHotendItemHotend::SetHotendState(DeviceFilamentHotendNozzleState state)
{
    if (m_hotend_state == state && m_hotend_bitmap_widget && m_hotend_bitmap_widget->GetBitmap().IsOk()) {
        return;
    }

    m_hotend_state = state;
    if (!m_hotend_bitmap_widget || !m_hotend_bitmap_normal || !m_hotend_bitmap_active || !m_hotend_bitmap_unknown || !m_hotend_bitmap_error) {
        return;
    }

    ScalableBitmap* target = m_hotend_bitmap_normal;
    switch (m_hotend_state) {
    case DeviceFilamentHotendNozzleState::Active:
        target = m_hotend_bitmap_active;
        break;
    case DeviceFilamentHotendNozzleState::Unknown:
        target = m_hotend_bitmap_unknown;
        break;
    case DeviceFilamentHotendNozzleState::Error:
        target = m_hotend_bitmap_error;
        break;
    case DeviceFilamentHotendNozzleState::Normal:
    default:
        target = m_hotend_bitmap_normal;
        break;
    }

    m_hotend_bitmap_widget->SetBitmap(target->bmp());
    m_hotend_bitmap_widget->Refresh();
    Refresh();
}

void wgtDeviceFilamentHotendItemHotend::CreateGui()
{
    SetBackgroundColour(*wxWHITE);
    SetMinSize(wxSize(FromDIP(24), FromDIP(24)));

    m_hotend_bitmap_normal = new ScalableBitmap(this, "dev_filament_hotend_nozzle_normal", 24);
    m_hotend_bitmap_active = new ScalableBitmap(this, "dev_filament_hotend_nozzle_active", 24);
    m_hotend_bitmap_unknown = new ScalableBitmap(this, "dev_filament_hotend_nozzle_unknown", 24);
    m_hotend_bitmap_error = new ScalableBitmap(this, "dev_filament_hotend_nozzle_error", 24);
    m_hotend_bitmap_widget = new wxStaticBitmap(this, wxID_ANY, m_hotend_bitmap_normal->bmp());
    m_hotend_bitmap_widget->SetMinSize(wxSize(FromDIP(24), FromDIP(24)));
    m_hotend_bitmap_widget->SetMaxSize(wxSize(FromDIP(24), FromDIP(24)));

    auto* hotend_sizer = new wxBoxSizer(wxVERTICAL);
    hotend_sizer->Add(m_hotend_bitmap_widget, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(1));

    SetSizer(hotend_sizer);
    SetHotendState(DeviceFilamentHotendNozzleState::Normal);
}

wgtDeviceFilamentHotendItem::wgtDeviceFilamentHotendItem(wxWindow *parent) : wxPanel(parent, wxID_ANY) { CreateGui(); }

void wgtDeviceFilamentHotendItem::Reset()
{
    m_machine_obj = nullptr;
    m_ams_id.clear();
    m_slot_id.clear();
    m_running = false;
    m_card_state = DeviceFilamentHotendCardState::Empty;
    m_line_state = DeviceFilamentHotendLineState::Standby;
    m_nozzle_state = DeviceFilamentHotendNozzleState::Normal;
    SetIsReading(false);
    SetFilamentType("?");
    SetWeight(wxEmptyString);
    SetFilamentColor(wxColour("#D9D9D9"));
    SetHasFilament(false);
    SetSelected(false);
    SetEnabledState(false);
    RefreshVisualState();
}

void wgtDeviceFilamentHotendItem::UpdateByMachine(Slic3r::MachineObject* machine_obj,
                                                  std::optional<DevAmsSlotId> ams_slot_id)
{
    m_machine_obj = machine_obj;
    auto fila_system = machine_obj ? machine_obj->GetFilaSystem() : nullptr;
    m_fila_system = fila_system;
    if (machine_obj && fila_system && ams_slot_id.has_value()) {
        auto tray = machine_obj->get_tray(ams_slot_id->first, ams_slot_id->second);
        if (tray.has_value()) {
            m_ams_id = tray->ams_id;
            m_slot_id = tray->id;

            const bool has_filament = tray->is_exists;
            const std::string display_filament = !tray->sub_brands.empty() ? tray->sub_brands : tray->get_display_filament_type();
            SetFilamentType(display_filament.empty() ? "?" : wxString::FromUTF8(display_filament));
            SetWeight(wxEmptyString);
            SetFilamentColor(has_filament ? tray->get_color() : wxColour("#D9D9D9"));
            SetHasFilament(has_filament);
            SetEnabledState(true);
            SetIsReading(tray->is_reading(machine_obj->tray_reading_bits));
            m_card_state = ResolveCardState(machine_obj, *tray);
            m_line_state = ResolveLineState(m_card_state);

            RefreshVisualState();
            return;
        }
    }

    Reset();
}

#define FILAMENT_HOTEND_CARD_SIZE wxSize(FromDIP(88), FromDIP(112))
#define FILAMENT_HOTEND_ITEM_TOP_SIZE wxSize(FromDIP(88), FromDIP(236))
#define FILAMENT_HOTEND_ITEM_BOTTOM_SIZE wxSize(FromDIP(88), FromDIP(118))
void wgtDeviceFilamentHotendItem::CreateGui()
{
    const wxColour card_bg   = wxColour("#F2F2F2");
    const wxColour border_bg = wxColour("#D9D9D9");

    SetBackgroundColour(*wxWHITE);
    SetMinSize(FILAMENT_HOTEND_ITEM_TOP_SIZE);
    SetMaxSize(FILAMENT_HOTEND_ITEM_TOP_SIZE);

    wxBoxSizer *root = new wxBoxSizer(wxVERTICAL);
    m_panel_card     = new StaticBox(this, wxID_ANY);
    m_panel_card->SetCornerRadius(FromDIP(4));
    m_panel_card->SetBorderWidth(1);
    m_panel_card->SetBackgroundColorNormal(card_bg);
    m_panel_card->SetBorderColorNormal(border_bg);
    m_panel_card->SetMinSize(FILAMENT_HOTEND_CARD_SIZE);
    m_panel_card->SetMaxSize(FILAMENT_HOTEND_CARD_SIZE);

    wxBoxSizer *card_sizer = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *top_row    = new wxBoxSizer(wxHORIZONTAL);

    m_badge_index = new wgtCircleItem(m_panel_card, wxID_ANY);
    top_row->AddSpacer(FromDIP(3));
    top_row->Add(m_badge_index, 0, wxALIGN_CENTER_VERTICAL);
    top_row->AddStretchSpacer(1);

    m_btn_menu = new ScalableButton(m_panel_card, wxID_ANY, "dev_filament_menu_icon", wxEmptyString, wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER, false, 14);
    m_btn_menu->SetBackgroundColour(card_bg);
    m_btn_menu->SetMinSize(wxSize(FromDIP(14), FromDIP(14)));
    m_btn_menu->SetMaxSize(wxSize(FromDIP(14), FromDIP(14)));
    m_btn_menu->Bind(wxEVT_BUTTON, &wgtDeviceFilamentHotendItem::OnMenuButton, this);
    top_row->Add(m_btn_menu, 0, wxALIGN_TOP);

    card_sizer->Add(top_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(2));
    card_sizer->AddSpacer(FromDIP(0));

    std::vector<std::string> reading_icon_list{"ams_rfid_1", "ams_rfid_2", "ams_rfid_3", "ams_rfid_4"};
    m_panel_spool = new wgtDeviceFilamentHotendItemSpool(m_panel_card, reading_icon_list);
    card_sizer->Add(m_panel_spool, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(0));

    card_sizer->AddSpacer(FromDIP(0));

    m_panel_card->SetSizer(card_sizer);
    root->Add(m_panel_card, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(0));

    m_panel_tube = new wgtDeviceFilamentHotendItemTube(this);
    root->Add(m_panel_tube, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(0));

    // Hotend row is rendered by parent layout, keep this hidden here.
    m_panel_hotend = nullptr;

    SetSizer(root);
    Reset();
    Layout();

    // events
    m_panel_card->Bind(wxEVT_LEFT_DOWN, [this](const auto& e) {
        if (!m_enabled_state || m_running || !m_enable_selection) {
            return;
        }
        this->SetSelected(!m_selected, true);
        this->RefreshVisualState();
    });
}

void wgtDeviceFilamentHotendItem::SetNozzlePos(int index)
{
    if (m_nozzle_pos == index) return;

    m_nozzle_pos = index;
    if (m_badge_index) {
        m_badge_index->SetValue(nozzle_pos_to_display_idx(m_nozzle_pos));
    }

    SetLayoutTier((nozzle_pos_to_display_idx(m_nozzle_pos) % 2) == 0);

    m_visual_dirty = true;
}

void wgtDeviceFilamentHotendItem::SetLayoutTier(bool top_tier)
{
    if (m_is_top_tier == top_tier && GetMinSize().IsFullySpecified()) {
        return;
    }

    m_is_top_tier = top_tier;
    const int line_height = m_is_top_tier ? 128 : 14;
    if (m_panel_tube) {
        m_panel_tube->SetLineHeight(line_height);
    }

    const wxSize item_size = m_is_top_tier ? FILAMENT_HOTEND_ITEM_TOP_SIZE : FILAMENT_HOTEND_ITEM_BOTTOM_SIZE;
    SetMinSize(item_size);
    SetMaxSize(item_size);
    m_layout_dirty = true;
    Layout();
}

void wgtDeviceFilamentHotendItem::SetFilamentType(const wxString &type)
{
    const wxString display_type = type.empty() ? "?" : type;
    if (m_filament_type == display_type) return;

    m_filament_type = display_type;
    if (m_panel_spool) { m_panel_spool->SetFilamentType(m_filament_type); }
    m_visual_dirty = true;
    m_layout_dirty = true;
}

void wgtDeviceFilamentHotendItem::SetFilamentColor(const wxColour &color)
{
    if (m_filament_color == color) return;

    m_filament_color = color;
    m_visual_dirty   = true;
}

bool wgtDeviceFilamentHotendItem::IsSlotLoadedInExtruder(Slic3r::MachineObject* machine_obj) const
{
    if (!machine_obj || m_ams_id.empty() || m_slot_id.empty()) {
        return false;
    }

    for (const auto& ext : machine_obj->GetExtderSystem()->GetExtruders()) {
        if (ext.GetSlotNow().ams_id == m_ams_id && ext.GetSlotNow().slot_id == m_slot_id && ext.HasFilamentInExt()) {
            return true;
        }
    }
    return false;
}

DeviceFilamentHotendCardState wgtDeviceFilamentHotendItem::ResolveCardState(Slic3r::MachineObject* machine_obj, const Slic3r::DevAmsTray& tray) const
{
    if (!tray.is_exists) {
        return DeviceFilamentHotendCardState::Empty;
    }

    if (machine_obj && tray.is_reading(machine_obj->tray_reading_bits)) {
        return DeviceFilamentHotendCardState::Reading;
    }

    const bool loaded_in_extruder = IsSlotLoadedInExtruder(machine_obj);
    const bool is_filament_change = machine_obj && machine_obj->ams_status_main == AMS_STATUS_MAIN_FILAMENT_CHANGE;
    if (machine_obj && machine_obj->ams_status_main == AMS_STATUS_MAIN_UNKNOWN) {
        return DeviceFilamentHotendCardState::Error;
    }

    if (is_filament_change && (m_running || loaded_in_extruder)) {
        return loaded_in_extruder ? DeviceFilamentHotendCardState::Unloading : DeviceFilamentHotendCardState::Loading;
    }

    if (tray.setting_id.empty() || tray.m_fila_type.empty()) {
        return DeviceFilamentHotendCardState::Unknown;
    }

    return loaded_in_extruder ? DeviceFilamentHotendCardState::ReadyToUse : DeviceFilamentHotendCardState::ReadyToLoad;
}

DeviceFilamentHotendLineState wgtDeviceFilamentHotendItem::ResolveLineState(DeviceFilamentHotendCardState card_state) const
{
    switch (card_state) {
    case DeviceFilamentHotendCardState::Reading:
        return DeviceFilamentHotendLineState::None;
    case DeviceFilamentHotendCardState::Loading:
    case DeviceFilamentHotendCardState::Unloading:
        return DeviceFilamentHotendLineState::Transporting;
    case DeviceFilamentHotendCardState::ReadyToUse:
        return DeviceFilamentHotendLineState::AtNozzle;
    case DeviceFilamentHotendCardState::Error:
        return DeviceFilamentHotendLineState::Error;
    case DeviceFilamentHotendCardState::ReadyToLoad:
    case DeviceFilamentHotendCardState::Empty:
    case DeviceFilamentHotendCardState::Unknown:
    default:
        return DeviceFilamentHotendLineState::Standby;
    }
}

DeviceFilamentHotendNozzleState wgtDeviceFilamentHotendItem::ResolveNozzleState(DeviceFilamentHotendCardState card_state, DeviceFilamentHotendLineState line_state) const
{
    if (card_state == DeviceFilamentHotendCardState::Error) {
        return DeviceFilamentHotendNozzleState::Error;
    }
    if (card_state == DeviceFilamentHotendCardState::Unknown) {
        return DeviceFilamentHotendNozzleState::Unknown;
    }
    if (line_state == DeviceFilamentHotendLineState::Transporting || line_state == DeviceFilamentHotendLineState::AtNozzle) {
        return DeviceFilamentHotendNozzleState::Active;
    }
    return DeviceFilamentHotendNozzleState::Normal;
}

wxString wgtDeviceFilamentHotendItem::ResolveCardStateDisplayText(DeviceFilamentHotendCardState card_state) const
{
    switch (card_state) {
    case DeviceFilamentHotendCardState::Reading:
        return _L("读取中");
    case DeviceFilamentHotendCardState::Loading:
        return _L("进料中");
    case DeviceFilamentHotendCardState::Unloading:
        return _L("退料中");
    case DeviceFilamentHotendCardState::ReadyToLoad:
        return _L("待进料");
    case DeviceFilamentHotendCardState::ReadyToUse:
        return _L("已进料");
    case DeviceFilamentHotendCardState::Empty:
        return _L("--");
    case DeviceFilamentHotendCardState::Unknown:
        return _L("未知耗材");
    case DeviceFilamentHotendCardState::Error:
        return _L("错误状态");
    default:
        return wxEmptyString;
    }
}

void wgtDeviceFilamentHotendItem::SetHasFilament(bool has_filament)
{
    if (m_has_filament == has_filament) return;

    m_has_filament = has_filament;
    m_visual_dirty = true;
}

void wgtDeviceFilamentHotendItem::SetSelected(bool selected, bool notify)
{
    if (m_selected == selected) return;

    m_selected     = selected;
    m_visual_dirty = true;
    RefreshVisualState();
    if (notify) {
        NotifySelectionChanged();
    }
}

void wgtDeviceFilamentHotendItem::SetRunning(bool running)
{
    if (m_running == running) {
        return;
    }

    m_running = running;
    if (m_machine_obj && !m_ams_id.empty() && !m_slot_id.empty()) {
        auto tray = m_machine_obj->get_tray(m_ams_id, m_slot_id);
        if (tray.has_value()) {
            m_card_state = ResolveCardState(m_machine_obj, *tray);
            m_line_state = ResolveLineState(m_card_state);
        }
    }
    if (m_btn_menu) {
        m_btn_menu->Enable(m_enabled_state && !m_running);
    }
    m_visual_dirty = true;
    RefreshVisualState();
}

void wgtDeviceFilamentHotendItem::SetEnabledState(bool enabled)
{
    if (m_enabled_state == enabled) return;

    m_enabled_state = enabled;
    if (m_badge_index) { m_badge_index->Enable(enabled); }
    if (m_btn_menu) { m_btn_menu->Enable(enabled && !m_running); }
    if (!enabled) { DismissMenuPopup(); }
    m_visual_dirty = true;
}

void wgtDeviceFilamentHotendItem::SetIsReading(bool is_reading)
{
    if (m_is_reading == is_reading) return;

    m_is_reading = is_reading;
    if (m_panel_spool) {
        m_panel_spool->SetReading(m_is_reading);
    }

    m_visual_dirty = true;
    m_layout_dirty = true;
    RefreshVisualState();
}

void wgtDeviceFilamentHotendItem::SetWeight(const wxString &weight)
{
    if (m_weight_text == weight) return;

    m_weight_text = weight;
    if (!m_panel_spool) return;

    m_panel_spool->SetWeight(m_weight_text);
    m_visual_dirty = true;
    m_layout_dirty = true;
}

void wgtDeviceFilamentHotendItem::NotifySelectionChanged()
{
    wxCommandEvent event(EVT_DEVICE_FILAMENT_HOTEND_ITEM_SELECTION_CHANGED, GetId());
    event.SetEventObject(this);
    event.SetInt(m_selected ? 1 : 0);
    GetEventHandler()->ProcessEvent(event);
}

void wgtDeviceFilamentHotendItem::OnMenuButton(wxCommandEvent &event)
{
    if (m_menu_popup && m_menu_popup->IsShown()) {
        DismissMenuPopup();
        return;
    }

    ShowMenuPopup();
    event.Skip();
}

void wgtDeviceFilamentHotendItem::OnMenuEdit(wxCommandEvent &event)
{
    DismissMenuPopup();
    auto fila_system = m_fila_system.lock();
    if (!fila_system || m_ams_id.empty() || m_slot_id.empty()) {
        event.Skip();
        return;
    }

    auto *obj = fila_system->GetOwner();
    if (!obj) {
        event.Skip();
        return;
    }

    if (!m_filament_setting_dlg) { m_filament_setting_dlg = new AMSMaterialsSetting(this, wxID_ANY); }

    int current_position_x = GetScreenPosition().x;
    int current_position_y = GetScreenPosition().y - FromDIP(40);
    int screen_bottom      = wxDisplay(GetParent()).GetGeometry().GetHeight() - FromDIP(50);
    current_position_y     = current_position_y + m_filament_setting_dlg->GetSize().GetHeight() > screen_bottom ? screen_bottom - m_filament_setting_dlg->GetSize().GetHeight() : current_position_y;

    m_filament_setting_dlg->obj = obj;

    try {
        const bool  is_virtual_tray = devPrinterUtil::IsVirtualSlot(m_ams_id);
        int         ams_id          = std::stoi(m_ams_id);
        std::string sn_number;
        std::string filament;
        std::string temp_max;
        std::string temp_min;
        wxString    k_val;
        wxString    n_val;

        if (is_virtual_tray) {
            const int nozzle_index = ams_id == VIRTUAL_TRAY_MAIN_ID ? 0 : 1;
            if (nozzle_index < 0 || nozzle_index >= static_cast<int>(obj->vt_slot.size())) {
                event.Skip();
                return;
            }

            const auto &vt_slot = obj->vt_slot[nozzle_index];
            k_val               = wxString::Format("%.3f", vt_slot.k);
            n_val               = wxString::Format("%.3f", vt_slot.n);
            wxColor color       = Slic3r::DevAmsTray::decode_color(vt_slot.color);
            m_filament_setting_dlg->ams_filament_id = vt_slot.setting_id;

            std::vector<wxColour> cols;
            for (const auto &col : vt_slot.cols) { cols.push_back(Slic3r::DevAmsTray::decode_color(col)); }
            m_filament_setting_dlg->set_ctype(vt_slot.ctype);

            if (m_filament_setting_dlg->ams_filament_id.empty()) {
                m_filament_setting_dlg->set_empty_color(color);
            } else {
                m_filament_setting_dlg->set_color(color);
                m_filament_setting_dlg->set_colors(cols);
            }

            m_filament_setting_dlg->m_is_third = !Slic3r::DevFilaSystem::IsBBL_Filament(vt_slot.tag_uid);
            if (!m_filament_setting_dlg->m_is_third) {
                sn_number = vt_slot.uuid;
                filament  = vt_slot.sub_brands;
                temp_max  = vt_slot.nozzle_temp_max;
                temp_min  = vt_slot.nozzle_temp_min;
            }
        } else {
            auto *tray_ptr = fila_system->GetAmsTray(m_ams_id, m_slot_id);
            if (!tray_ptr) {
                event.Skip();
                return;
            }

            k_val         = wxString::Format("%.3f", tray_ptr->k);
            n_val         = wxString::Format("%.3f", tray_ptr->n);
            wxColor color = Slic3r::DevAmsTray::decode_color(tray_ptr->color);

            std::vector<wxColour> cols;
            for (const auto &col : tray_ptr->cols) { cols.push_back(Slic3r::DevAmsTray::decode_color(col)); }
            m_filament_setting_dlg->set_ctype(tray_ptr->ctype);
            m_filament_setting_dlg->ams_filament_id = tray_ptr->setting_id;

            if (m_filament_setting_dlg->ams_filament_id.empty()) {
                m_filament_setting_dlg->set_empty_color(color);
            } else {
                m_filament_setting_dlg->set_color(color);
                m_filament_setting_dlg->set_colors(cols);
            }

            m_filament_setting_dlg->m_is_third = !Slic3r::DevFilaSystem::IsBBL_Filament(tray_ptr->tag_uid);
            if (!m_filament_setting_dlg->m_is_third) {
                sn_number = tray_ptr->uuid;
                filament  = tray_ptr->sub_brands;
                temp_max  = tray_ptr->nozzle_temp_max;
                temp_min  = tray_ptr->nozzle_temp_min;
            }
        }

        m_filament_setting_dlg->Move(wxPoint(current_position_x, current_position_y));
        m_filament_setting_dlg->Popup(filament, sn_number, temp_min, temp_max, k_val, n_val);
    } catch (...) {
    }

    event.Skip();
}

void wgtDeviceFilamentHotendItem::OnMenuLoad(wxCommandEvent &event)
{
    DismissMenuPopup();

    if (auto ptr = m_fila_system.lock()) {
        try {
            int old_temp = -1;
            int new_temp = -1;

            auto targ_tray = ptr->GetOwner()->get_tray(m_ams_id, m_slot_id);
            if (targ_tray.has_value()) {
                if (!targ_tray->nozzle_temp_max.empty() && !targ_tray->nozzle_temp_min.empty()) {
                    new_temp = (atoi(targ_tray->nozzle_temp_min.c_str()) + atoi(targ_tray->nozzle_temp_max.c_str())) / 2;
                }
            } else {
                return;
            }

            DevAmsTray* curr_tray = ptr->GetOwner()->get_curr_tray();
            if (curr_tray) {
                if (!curr_tray->nozzle_temp_max.empty() && !curr_tray->nozzle_temp_min.empty()) {
                    old_temp = (atoi(curr_tray->nozzle_temp_min.c_str()) + atoi(curr_tray->nozzle_temp_max.c_str())) / 2;
                }
            }

            ptr->GetOwner()->command_ams_change_filament(true, m_ams_id, m_slot_id, old_temp, new_temp, 0);
        } catch (const std::exception&) {
        }
    }
}

void wgtDeviceFilamentHotendItem::OnMenuUnLoad(wxCommandEvent& event)
{
    DismissMenuPopup();

    if (auto ptr = m_fila_system.lock()) {
        try {
            for (auto ext : ptr->GetOwner()->GetExtderSystem()->GetExtruders()) {
                if (ext.GetSlotNow().ams_id == m_ams_id && ext.GetSlotNow().slot_id == m_slot_id) {
                    ptr->GetOwner()->command_ams_change_filament(false, m_ams_id, "255");
                }
            }
        } catch (...) {}
    }
}

void wgtDeviceFilamentHotendItem::OnMenuRead(wxCommandEvent &event)
{
    DismissMenuPopup();

    if (auto ptr = m_fila_system.lock()) {
        const auto& ams_item = ptr->GetAmsById(m_ams_id);
        if (!ams_item) {
            return;
        }

        try {
            MachineObject* obj_ = ptr->GetOwner();
            if (obj_->is_enable_np || obj_->is_enable_ams_np) {
                obj_->command_ams_refresh_rfid2(stoi(m_ams_id), stoi(m_slot_id));
            } else {
                obj_->command_ams_refresh_rfid(std::to_string(ams_item->GetTrayId(stoi(m_slot_id))));
            }
        } catch (...) {
            ;
        }
    }
}

void wgtDeviceFilamentHotendItem::ShowMenuPopup()
{
    const auto now = std::chrono::steady_clock::now();
    if (now - m_menu_dismiss_time < std::chrono::milliseconds(200)) {
        m_menu_dismiss_time = std::chrono::steady_clock::time_point{};
        return;
    }

    if (!m_menu_popup) {
        m_menu_popup = new wgtFilamentHotendMenuDialog(this);
        m_menu_popup->Bind(wxEVT_BUTTON, &wgtDeviceFilamentHotendItem::OnMenuEdit, this, ID_FILAMENT_HOTEND_MENU_EDIT);
        m_menu_popup->Bind(wxEVT_BUTTON, &wgtDeviceFilamentHotendItem::OnMenuLoad, this, ID_FILAMENT_HOTEND_MENU_LOAD);
        m_menu_popup->Bind(wxEVT_BUTTON, &wgtDeviceFilamentHotendItem::OnMenuUnLoad, this, ID_FILAMENT_HOTEND_MENU_UNLOAD);
        m_menu_popup->Bind(wxEVT_BUTTON, &wgtDeviceFilamentHotendItem::OnMenuRead, this, ID_FILAMENT_HOTEND_MENU_READ);
        m_menu_popup->Bind(wxEVT_SHOW, [this](wxShowEvent &evt) {
            if (!evt.IsShown()) { m_menu_dismiss_time = std::chrono::steady_clock::now(); }
            evt.Skip();
        });
        m_menu_popup->Bind(wxEVT_ACTIVATE, [this](wxActivateEvent &evt) {
            if (!evt.GetActive() && m_menu_popup && m_menu_popup->IsShown()) { DismissMenuPopup(); }
            evt.Skip();
        });
    }

    m_menu_popup->Layout();
    m_menu_popup->Fit();

    const wxSize  popup_size = m_menu_popup->GetSize();
    const wxSize  btn_size   = m_btn_menu->GetSize();
    wxPoint       pos        = m_btn_menu->ClientToScreen(wxPoint(0, btn_size.y + FromDIP(12)));
    const int     x_offset   = std::max(0, popup_size.GetWidth() - btn_size.GetWidth());
    pos.x -= x_offset;

    const int display_idx = wxDisplay::GetFromPoint(pos);
    if (display_idx != wxNOT_FOUND) {
        const wxRect screen_rect = wxDisplay(display_idx).GetGeometry();
        pos.x                    = std::max(screen_rect.GetLeft(), std::min(pos.x, screen_rect.GetRight() - popup_size.GetWidth()));
        pos.y                    = std::max(screen_rect.GetTop(), std::min(pos.y, screen_rect.GetBottom() - popup_size.GetHeight()));
    }

    m_menu_popup->Move(pos);
    m_menu_popup->Show();
    m_menu_popup->Raise();
    m_menu_popup->SetFocus();
}

void wgtDeviceFilamentHotendItem::DismissMenuPopup()
{
    if (m_menu_popup && m_menu_popup->IsShown()) {
        m_menu_popup->Show(false);
        m_menu_dismiss_time = std::chrono::steady_clock::now();
    }
}

void wgtDeviceFilamentHotendItem::RefreshVisualState()
{
    if (!m_visual_dirty && !m_layout_dirty) return;

    const bool is_empty_like       = (m_card_state == DeviceFilamentHotendCardState::Empty
                                      || m_card_state == DeviceFilamentHotendCardState::Unknown);
    const wxColour type_text_fg    = !m_enabled_state ? wxColour("#A3A3A3")
                                  : (is_empty_like ? wxColour("#A3A3A3")
                                  : (m_card_state == DeviceFilamentHotendCardState::Error ? wxColour("#D95047") : wxColour("#1F1F1F")));
    const wxColour status_text_fg  = !m_enabled_state ? wxColour("#BDBDBD")
                                  : (m_card_state == DeviceFilamentHotendCardState::Empty ? wxColour("#BDBDBD")
                                  : (m_card_state == DeviceFilamentHotendCardState::Unknown ? wxColour("#A3A3A3")
                                  : type_text_fg));
    const wxColour ring_color      = !m_enabled_state ? wxColour("#D9D9D9")
                                  : (m_card_state == DeviceFilamentHotendCardState::Reading ? wxColour("#D9D9D9")
                                  : (m_card_state == DeviceFilamentHotendCardState::Error ? wxColour("#D95047")
                                  : (m_has_filament ? m_filament_color : wxColour("#D9D9D9"))));
    const wxColour center_color    = (m_card_state == DeviceFilamentHotendCardState::Empty
                                      || m_card_state == DeviceFilamentHotendCardState::Reading)
                                      ? wxColour("#D9D9D9")
                                      : wxColour("#E9E9E9");
    const bool show_reading = (m_card_state == DeviceFilamentHotendCardState::Reading);
    const wxString state_text = ResolveCardStateDisplayText(m_card_state);
    const bool show_type = (m_card_state != DeviceFilamentHotendCardState::Reading
                            && m_card_state != DeviceFilamentHotendCardState::Empty
                            && m_card_state != DeviceFilamentHotendCardState::Unknown);
    const bool show_slash = (m_card_state == DeviceFilamentHotendCardState::Empty);
    if (m_panel_spool) {
        m_panel_spool->SetStatusText(state_text);
        m_panel_spool->ApplyVisualState(type_text_fg, status_text_fg, ring_color, center_color, show_type, !state_text.empty(), show_reading, show_slash);
    }
    m_panel_spool->RefreshSpool();

    const wxColour card_border    = !m_enabled_state ? wxColour("#EAEAEA") : m_running ? wxColour("#1E39F6") : m_selected ? wxColour("#00AE42") : wxColour("#D9D9D9");
    m_panel_card->SetBorderColorNormal(card_border);

    m_panel_card->Refresh();
    m_btn_menu->Refresh();
    if (m_panel_tube) {
        m_line_state = ResolveLineState(m_card_state);
        m_panel_tube->SetLineState(m_line_state);
        m_panel_tube->Refresh();
    }
    m_nozzle_state = ResolveNozzleState(m_card_state, m_line_state);
    if (m_panel_hotend) {
        const bool show_hotend = (m_line_state == DeviceFilamentHotendLineState::AtNozzle
                                  || m_line_state == DeviceFilamentHotendLineState::Transporting
                                  || m_line_state == DeviceFilamentHotendLineState::Error);
        m_panel_hotend->Show(show_hotend);
        m_panel_hotend->SetHotendState(m_nozzle_state);
        m_panel_hotend->Refresh();
    }

    if (m_layout_dirty) {
        if (m_panel_spool) { m_panel_spool->UpdateSpoolLayout(); }
        m_panel_card->Layout();
        Layout();
    }

    Refresh();
    m_visual_dirty = false;
    m_layout_dirty = false;
}

} // namespace Slic3r::GUI
