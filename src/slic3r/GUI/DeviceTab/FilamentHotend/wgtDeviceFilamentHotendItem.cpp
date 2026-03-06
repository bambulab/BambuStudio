#include "wgtDeviceFilamentHotendItem.h"
#include "slic3r/GUI/AMSMaterialsSetting.hpp"
#include "slic3r/GUI/DeviceCore/DevDefs.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/Widgets/AnimaController.hpp"
#include "slic3r/GUI/Widgets/Button.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"
#include "slic3r/GUI/Widgets/StaticBox.hpp"
#include "slic3r/GUI/Widgets/wgtCircleItem.hpp"
#include "slic3r/GUI/wxExtensions.hpp"

#include <algorithm>
#include <wx/dcbuffer.h>
#include <wx/display.h>
#include <wx/sizer.h>

wxDEFINE_EVENT(EVT_DEVICE_FILAMENT_HOTEND_ITEM_SELECTION_CHANGED, wxCommandEvent);

// Private impls
namespace Slic3r::GUI {

namespace {

enum FilamentHotendMenuActionId {
    ID_FILAMENT_HOTEND_MENU_EDIT = wxID_HIGHEST + 510,
    ID_FILAMENT_HOTEND_MENU_LOAD,
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
        root->Add(CreateActionButton(_L("Reread"), ID_FILAMENT_HOTEND_MENU_READ), 0, wxEXPAND);
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
    if (m_panel_tube_line) {
        m_panel_tube_line->SetBackgroundColour(color);
        m_panel_tube_line->Refresh();
    }
    Refresh();
}

void wgtDeviceFilamentHotendItemTube::SetTubeVisible(bool visible)
{
    if (m_panel_tube_line) {
        m_panel_tube_line->Show(visible);
    }
    Layout();
    Refresh();
}

void wgtDeviceFilamentHotendItemTube::CreateGui()
{
    SetBackgroundColour(*wxWHITE);

    auto* tube_sizer = new wxBoxSizer(wxHORIZONTAL);
    tube_sizer->AddStretchSpacer(1);

    m_panel_tube_line = new wxPanel(this, wxID_ANY);
    m_panel_tube_line->SetMinSize(wxSize(FromDIP(2), FromDIP(16)));
    tube_sizer->Add(m_panel_tube_line, 0, wxEXPAND | wxALIGN_CENTER_HORIZONTAL);

    tube_sizer->AddStretchSpacer(1);
    SetSizer(tube_sizer);
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
    if (m_label_weight) {
        m_label_weight->SetLabel(weight);
        m_label_weight->Show(!weight.empty());
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

void wgtDeviceFilamentHotendItemSpool::ApplyVisualState(const wxColour& text_fg,
                                                        const wxColour& rail_color,
                                                        const wxColour& spool_line,
                                                        const wxColour& block_color,
                                                        const wxColour& weight_text_fg,
                                                        bool show_type,
                                                        bool show_weight,
                                                        bool show_reading)
{
    if (m_label_type) {
        m_label_type->SetForegroundColour(text_fg);
        m_label_type->Show(show_type);
    }
    if (m_panel_spool_left_bar) {
        m_panel_spool_left_bar->SetBackgroundColour(rail_color);
        m_panel_spool_left_bar->Refresh();
    }
    if (m_panel_spool_top_line) {
        m_panel_spool_top_line->SetBackgroundColour(spool_line);
        m_panel_spool_top_line->Refresh();
    }
    if (m_panel_spool_right_bar) {
        m_panel_spool_right_bar->SetBackgroundColour(rail_color);
        m_panel_spool_right_bar->Refresh();
    }
    if (m_panel_color_block) {
        m_panel_color_block->SetBackgroundColour(block_color);
        m_panel_color_block->Refresh();
    }
    if (m_label_weight) {
        m_label_weight->SetBackgroundColour(block_color);
        m_label_weight->SetForegroundColour(weight_text_fg);
        m_label_weight->Show(show_weight);
        m_label_weight->Refresh();
    }
    if (m_reading_icon) {
        m_reading_icon->SetBackgroundColour(block_color);
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
    if (m_panel_spool_center) {
        m_panel_spool_center->Layout();
    }
    Layout();
}

void wgtDeviceFilamentHotendItemSpool::CreateGui(const std::vector<std::string>& reading_icon_list)
{
    const wxColour spool_bg = wxColour("#F2F2F2");

    SetBackgroundColour(*wxWHITE);
    SetMinSize(wxSize(FromDIP(52), FromDIP(60)));

    auto* spool_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_panel_spool_left_bar = new wxPanel(this, wxID_ANY);
    m_panel_spool_left_bar->SetMinSize(wxSize(FromDIP(4), FromDIP(60)));
    spool_sizer->Add(m_panel_spool_left_bar, 0, wxEXPAND);

    m_panel_spool_center = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_panel_spool_center->SetBackgroundColour(spool_bg);
    m_panel_spool_center->SetMinSize(wxSize(FromDIP(44), FromDIP(40)));
    auto* center_sizer = new wxBoxSizer(wxVERTICAL);

    m_panel_spool_top_line = new wxPanel(m_panel_spool_center, wxID_ANY);
    m_panel_spool_top_line->SetMinSize(wxSize(FromDIP(44), FromDIP(1)));
    center_sizer->Add(m_panel_spool_top_line, 0, wxEXPAND | wxTOP, FromDIP(1));
    center_sizer->AddSpacer(FromDIP(2));

    m_label_type = new Label(m_panel_spool_center, "?");
    m_label_type->SetFont(Label::Body_13);
    center_sizer->Add(m_label_type, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(4));
    center_sizer->AddStretchSpacer(1);

    m_panel_color_block = new wxPanel(m_panel_spool_center, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_panel_color_block->SetMinSize(wxSize(FromDIP(42), FromDIP(31)));
    auto* color_sizer = new wxBoxSizer(wxVERTICAL);
    m_label_weight    = new Label(m_panel_color_block, wxEmptyString);
    m_label_weight->SetFont(Label::Body_12);
    m_reading_icon = new AnimaIcon(m_panel_color_block, wxID_ANY, reading_icon_list, "refresh_printer", 100);
    m_reading_icon->SetMinSize(wxSize(FromDIP(20), FromDIP(20)));
    m_reading_icon->Hide();
    color_sizer->AddStretchSpacer(1);
    color_sizer->Add(m_reading_icon, 0, wxALIGN_CENTER_HORIZONTAL);
    color_sizer->Add(m_label_weight, 0, wxALIGN_CENTER_HORIZONTAL);
    color_sizer->AddStretchSpacer(1);
    m_panel_color_block->SetSizer(color_sizer);
    center_sizer->Add(m_panel_color_block, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(1));
    m_panel_spool_center->SetSizer(center_sizer);
    spool_sizer->Add(m_panel_spool_center, 0, wxALIGN_CENTER_VERTICAL);

    m_panel_spool_right_bar = new wxPanel(this, wxID_ANY);
    m_panel_spool_right_bar->SetMinSize(wxSize(FromDIP(4), FromDIP(80)));
    spool_sizer->Add(m_panel_spool_right_bar, 0, wxEXPAND);

    SetSizer(spool_sizer);
}

wgtDeviceFilamentHotendItemHotend::wgtDeviceFilamentHotendItemHotend(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
{
    CreateGui();
}

void wgtDeviceFilamentHotendItemHotend::SetHotendColor(const wxColour& color)
{
    if (m_panel_hotend_top) {
        m_panel_hotend_top->SetBackgroundColour(color);
        m_panel_hotend_top->Refresh();
    }
    if (m_panel_hotend_bottom) {
        m_panel_hotend_bottom->SetBackgroundColour(color);
        m_panel_hotend_bottom->Refresh();
    }
    Refresh();
}

void wgtDeviceFilamentHotendItemHotend::CreateGui()
{
    SetBackgroundColour(*wxWHITE);
    SetMinSize(wxSize(FromDIP(24), FromDIP(24)));

    auto* hotend_sizer = new wxBoxSizer(wxVERTICAL);
    hotend_sizer->AddSpacer(FromDIP(2));

    m_panel_hotend_top = new wxPanel(this, wxID_ANY);
    m_panel_hotend_top->SetMinSize(wxSize(FromDIP(8), FromDIP(10)));
    hotend_sizer->Add(m_panel_hotend_top, 0, wxALIGN_CENTER_HORIZONTAL);

    hotend_sizer->AddSpacer(FromDIP(2));

    m_panel_hotend_bottom = new wxPanel(this, wxID_ANY);
    m_panel_hotend_bottom->SetMinSize(wxSize(FromDIP(8), FromDIP(6)));
    hotend_sizer->Add(m_panel_hotend_bottom, 0, wxALIGN_CENTER_HORIZONTAL);

    SetSizer(hotend_sizer);
}

wgtDeviceFilamentHotendItem::wgtDeviceFilamentHotendItem(wxWindow *parent) : wxPanel(parent, wxID_ANY) { CreateGui(); }

void wgtDeviceFilamentHotendItem::Reset()
{
    m_ams_id.clear();
    m_slot_id.clear();
    m_running = false;
    SetIsReading(false);
    SetFilamentType("?");
    SetWeight(wxEmptyString);
    SetFilamentColor(wxColour("#D9D9D9"));
    SetHasFilament(false);
    SetSelected(false);
    SetEnabledState(false);
    RefreshVisualState();
}

void wgtDeviceFilamentHotendItem::UpdateBy(std::shared_ptr<DevFilaSystem> fila_system,
                                           std::optional<DevAmsSlotId> ams_slot_id)
{
    m_fila_system = fila_system;
    if (fila_system && ams_slot_id.has_value()) {
        auto tray = fila_system->GetOwner()->get_tray(ams_slot_id->first, ams_slot_id->second);
        if (tray.has_value()) {
            m_ams_id = tray->ams_id;
            m_slot_id = tray->id;

            const bool has_filament = tray->is_exists;
            SetFilamentType(tray->m_fila_type.empty() ? "?" : wxString::FromUTF8(tray->m_fila_type));
            SetWeight(has_filament ? format_weight(tray->weight) : "");
            SetFilamentColor(has_filament ? tray->get_color() : wxColour("#D9D9D9"));
            SetHasFilament(has_filament);
            SetEnabledState(true);
            SetIsReading(tray->is_reading(fila_system->GetOwner()->tray_reading_bits));

            RefreshVisualState();
            return;
        }
    }

    Reset();
}

#define FILAMENT_HOTEND_ITEM_SIZE wxSize(FromDIP(88), FromDIP(160))
void wgtDeviceFilamentHotendItem::CreateGui()
{
    const wxColour card_bg   = wxColour("#F7F7F7");
    const wxColour hotend_bg = wxColour("#D9D9D9");
    const wxColour border_bg = wxColour("#D9D9D9");

    SetBackgroundColour(card_bg);
    SetMinSize(FILAMENT_HOTEND_ITEM_SIZE);
    SetMaxSize(FILAMENT_HOTEND_ITEM_SIZE);

    wxBoxSizer *root = new wxBoxSizer(wxVERTICAL);
    m_panel_card     = new StaticBox(this, wxID_ANY);
    m_panel_card->SetCornerRadius(FromDIP(5));
    m_panel_card->SetBorderWidth(1);
    m_panel_card->SetBackgroundColorNormal(card_bg);
    m_panel_card->SetBorderColorNormal(border_bg);
    m_panel_card->SetMinSize(FILAMENT_HOTEND_ITEM_SIZE);
    m_panel_card->SetMaxSize(FILAMENT_HOTEND_ITEM_SIZE);

    wxBoxSizer *card_sizer = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *top_row    = new wxBoxSizer(wxHORIZONTAL);

    m_badge_index = new wgtCircleItem(m_panel_card, wxID_ANY);
    top_row->AddSpacer(FromDIP(10));
    top_row->AddStretchSpacer(1);
    top_row->Add(m_badge_index, 0, wxALIGN_CENTER_VERTICAL);
    top_row->AddStretchSpacer(1);

    m_btn_menu = new ScalableButton(m_panel_card, wxID_ANY, "dev_filament_menu_icon", wxEmptyString, wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER, false, 16);
    m_btn_menu->SetBackgroundColour(card_bg);
    m_btn_menu->SetMinSize(wxSize(FromDIP(16), FromDIP(16)));
    m_btn_menu->SetMaxSize(wxSize(FromDIP(16), FromDIP(16)));
    m_btn_menu->Bind(wxEVT_BUTTON, &wgtDeviceFilamentHotendItem::OnMenuButton, this);
    top_row->Add(m_btn_menu, 0, wxALIGN_TOP);

    card_sizer->Add(top_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(5));
    card_sizer->AddSpacer(FromDIP(4));

    std::vector<std::string> reading_icon_list{"ams_rfid_1", "ams_rfid_2", "ams_rfid_3", "ams_rfid_4"};
    m_panel_spool = new wgtDeviceFilamentHotendItemSpool(m_panel_card, reading_icon_list);
    card_sizer->Add(m_panel_spool, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(4));

    m_panel_tube = new wgtDeviceFilamentHotendItemTube(m_panel_card);
    card_sizer->Add(m_panel_tube, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(4));

    m_panel_hotend = new wgtDeviceFilamentHotendItemHotend(m_panel_card);
    m_panel_hotend->SetHotendColor(hotend_bg);
    card_sizer->Add(m_panel_hotend, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(1));

    card_sizer->AddStretchSpacer(1);
    card_sizer->AddSpacer(FromDIP(8));

    m_panel_card->SetSizer(card_sizer);
    root->Add(m_panel_card, 1, wxEXPAND | wxALL, FromDIP(1));

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
        if (m_nozzle_pos >= 0x10) {
            m_badge_index->SetValue(m_nozzle_pos - 0x10 + 1);
        } else {
            m_badge_index->SetValue(m_nozzle_pos + 1);
        }
    }

    m_visual_dirty = true;
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

    m_panel_tube->SetTubeColor(m_filament_color);
    m_visual_dirty   = true;
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

    const wxColour reading_color  = get_reading_color();
    const wxColour text_fg        = !m_enabled_state ? wxColour("#A3A3A3") : wxColour("#1F1F1F");
    const wxColour rail_color     = !m_enabled_state ? wxColour("#E5E5E5") : wxColour("#CFD4DB");
    const wxColour spool_line     = !m_enabled_state ? wxColour("#C9C9C9") : wxColour("#6A6A6A");
    const wxColour block_color    = !m_enabled_state ? wxColour("#D9D9D9") : m_is_reading ? reading_color : m_has_filament ? m_filament_color : wxColour("#D9D9D9");
    const wxColour weight_text_fg = get_weight_text_color(block_color);
    m_panel_spool->ApplyVisualState(text_fg, rail_color, spool_line, block_color, weight_text_fg, !m_is_reading, !m_weight_text.empty() && !m_is_reading, m_is_reading);
    m_panel_spool->RefreshSpool();

    const wxColour card_border    = !m_enabled_state ? wxColour("#EAEAEA") : m_running ? wxColour("#1E39F6") : m_selected ? wxColour("#00AE42") : wxColour("#D9D9D9");
    m_panel_card->SetBorderColorNormal(card_border);

    m_panel_card->Refresh();
    m_btn_menu->Refresh();
    m_panel_tube->Refresh();
    m_panel_hotend->Refresh();

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
