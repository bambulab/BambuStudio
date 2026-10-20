#include "MultiCameraPage.hpp"

#include "GUI_App.hpp"
#include "I18N.hpp"
#include "MainFrame.hpp"
#include "DeviceManager.hpp"

#include "DeviceCore/DevManager.h"
#include "DeviceCore/DevBed.h"
#include "DeviceCore/DevChamber.h"
#include "DeviceCore/DevExtruderSystem.h"
#include "DeviceCore/DevFan.h"
#include "DeviceCore/DevHMS.h"

#include "Widgets/Button.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/ProgressBar.hpp"
#include "Widgets/StateColor.hpp"

#include "libslic3r/Utils.hpp"

#include <wx/sizer.h>

#include <algorithm>
#include <cmath>

namespace Slic3r {
namespace GUI {

#define MULTI_CAMERA_CONFIG_SECTION "multi_camera"
#define MULTI_CAMERA_REFRESH_MSEC   1000
#define MULTI_CAMERA_NA_STR         "-"

static const wxColour COLOUR_TITLE("#6B6B6B");
static const wxColour COLOUR_VALUE("#323A3C");
static const wxColour COLOUR_OK(0, 174, 66);
static const wxColour COLOUR_WARN(255, 111, 0);
static const wxColour COLOUR_ERROR(208, 27, 27);

static std::string slot_config_key(int slot) { return "slot_" + std::to_string(slot); }

// avoid flicker by only touching labels whose text really changed
static void set_label(Label *label, const wxString &text, const wxColour &colour = COLOUR_VALUE)
{
    if (label->GetLabel() != text)
        label->SetLabel(text);
    if (label->GetForegroundColour() != colour)
        label->SetForegroundColour(colour);
}

static wxString temp_text(float curr, float target)
{
    if (target > 0)
        return wxString::Format("%d / %d℃", (int) curr, (int) target);
    return wxString::Format("%d℃", (int) curr);
}

static wxString fan_percent(int speed)
{
    // fan speeds are stored in the 0..255 range
    return wxString::Format("%d%%", (int) std::round(speed / 2.55));
}

static wxString speed_level_text(DevPrintingSpeedLevel lvl)
{
    switch (lvl) {
    case SPEED_LEVEL_SILENCE: return _L("Silent");
    case SPEED_LEVEL_NORMAL: return _L("Standard");
    case SPEED_LEVEL_RAPID: return _L("Sport");
    case SPEED_LEVEL_RAMPAGE: return _L("Ludicrous");
    default: return MULTI_CAMERA_NA_STR;
    }
}

static wxString print_status_text(MachineObject *obj, wxColour &colour)
{
    colour = COLOUR_VALUE;
    if (!obj->is_online()) {
        colour = COLOUR_TITLE;
        return _L("Offline");
    }
    if (obj->is_connecting())
        return _L("Connecting...");

    const std::string &status = obj->print_status;
    if (status == "RUNNING") {
        colour = COLOUR_OK;
        wxString stage = obj->get_curr_stage();
        return stage.empty() ? _L("Printing") : stage;
    }
    if (status == "PREPARE") {
        colour = COLOUR_OK;
        return _L("Preparing");
    }
    if (status == "SLICING") {
        colour = COLOUR_OK;
        return _L("Slicing");
    }
    if (status == "PAUSE") {
        colour = COLOUR_WARN;
        return _L("Paused");
    }
    if (status == "FINISH") {
        colour = COLOUR_OK;
        return _L("Finished");
    }
    if (status == "FAILED") {
        colour = COLOUR_ERROR;
        return _L("Failed");
    }
    return _L("Idle");
}

CameraSlotPanel::CameraSlotPanel(wxWindow *parent, int slot)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL)
    , m_slot(slot)
{
    SetBackgroundColour(*wxWHITE);

    // header: printer picker, status, jump to the device page
    wxBoxSizer *header_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_printer_combo = new ComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(220), FromDIP(28)), 0, nullptr, wxCB_READONLY);
    m_printer_combo->Bind(wxEVT_COMBOBOX, &CameraSlotPanel::on_printer_selected, this);

    m_status_label = new Label(this, Label::Head_13, wxEmptyString, wxALIGN_LEFT | wxST_ELLIPSIZE_END);

    StateColor btn_bg(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed), std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
                      std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal));
    StateColor btn_bd(std::pair<wxColour, int>(wxColour(144, 144, 144), StateColor::Disabled), std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));
    StateColor btn_text(std::pair<wxColour, int>(wxColour(144, 144, 144), StateColor::Disabled), std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));
    m_detail_button = new Button(this, _L("Details"));
    m_detail_button->SetBackgroundColor(btn_bg);
    m_detail_button->SetBorderColor(btn_bd);
    m_detail_button->SetTextColor(btn_text);
    m_detail_button->SetFont(Label::Body_12);
    m_detail_button->SetCornerRadius(FromDIP(4));
    m_detail_button->SetMinSize(wxSize(FromDIP(64), FromDIP(26)));
    m_detail_button->SetToolTip(_L("Open this printer in the Device page"));
    m_detail_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
        if (!m_dev_id.empty() && wxGetApp().mainframe)
            wxGetApp().mainframe->jump_to_monitor(m_dev_id);
    });

    header_sizer->Add(m_printer_combo, 0, wxALIGN_CENTER_VERTICAL);
    header_sizer->Add(m_status_label, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(10));
    header_sizer->Add(m_detail_button, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(6));

    // live view, MediaPlayCtrl stops/starts the stream with the show state of the page
    m_media_ctrl = new wxMediaCtrl3(this);
    m_media_ctrl->SetMinSize(wxSize(FromDIP(320), FromDIP(180)));
    m_media_play_ctrl = new MediaPlayCtrl(this, m_media_ctrl, wxDefaultPosition, wxSize(-1, FromDIP(32)));
    // cloud live view is refused to self-built clients, the LAN stream only needs the access code
    m_media_play_ctrl->SetPreferLan(true);

    create_info_panel();

    wxBoxSizer *main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->Add(header_sizer, 0, wxEXPAND | wxALL, FromDIP(6));
    main_sizer->Add(m_media_ctrl, 1, wxEXPAND);
    main_sizer->Add(m_media_play_ctrl, 0, wxEXPAND);
    main_sizer->Add(m_info_panel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));
    SetSizer(main_sizer);

    m_dev_id = wxGetApp().app_config->get(MULTI_CAMERA_CONFIG_SECTION, slot_config_key(m_slot));
    clear_info();
}

Label *CameraSlotPanel::create_value_label(wxWindow *parent, wxSizer *sizer, const wxString &title)
{
    Label *title_label = new Label(parent, Label::Body_12, title);
    title_label->SetForegroundColour(COLOUR_TITLE);
    Label *value_label = new Label(parent, Label::Body_12, MULTI_CAMERA_NA_STR, wxALIGN_LEFT | wxST_ELLIPSIZE_END);
    value_label->SetForegroundColour(COLOUR_VALUE);
    sizer->Add(title_label, 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(value_label, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL);
    return value_label;
}

void CameraSlotPanel::create_info_panel()
{
    m_info_panel = new wxPanel(this);
    m_info_panel->SetBackgroundColour(*wxWHITE);

    m_task_label = new Label(m_info_panel, Label::Head_14, MULTI_CAMERA_NA_STR, wxALIGN_LEFT | wxST_ELLIPSIZE_END);
    m_task_label->SetForegroundColour(COLOUR_VALUE);

    wxBoxSizer *progress_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_progress_bar = new ProgressBar(m_info_panel, wxID_ANY, 100, wxDefaultPosition, wxSize(-1, FromDIP(8)));
    m_progress_bar->SetHeight(FromDIP(8));
    m_progress_bar->SetValue(0);
    m_percent_label = new Label(m_info_panel, Label::Head_14, "0%");
    m_percent_label->SetForegroundColour(COLOUR_VALUE);
    m_percent_label->SetMinSize(wxSize(FromDIP(44), -1));
    progress_sizer->Add(m_progress_bar, 1, wxALIGN_CENTER_VERTICAL);
    progress_sizer->Add(m_percent_label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));

    // two columns of "title value" pairs
    wxFlexGridSizer *grid = new wxFlexGridSizer(0, 4, FromDIP(3), FromDIP(8));
    grid->AddGrowableCol(1, 1);
    grid->AddGrowableCol(3, 1);
    m_layer_label       = create_value_label(m_info_panel, grid, _L("Layer"));
    m_left_time_label   = create_value_label(m_info_panel, grid, _L("Remaining"));
    m_finish_time_label = create_value_label(m_info_panel, grid, _L("Finish at"));
    m_speed_label       = create_value_label(m_info_panel, grid, _L("Speed"));
    m_nozzle_label      = create_value_label(m_info_panel, grid, _L("Nozzle"));
    m_bed_label         = create_value_label(m_info_panel, grid, _L("Bed"));
    m_chamber_label     = create_value_label(m_info_panel, grid, _L("Chamber"));
    m_fan_label         = create_value_label(m_info_panel, grid, _L("Fan"));
    m_wifi_label        = create_value_label(m_info_panel, grid, _L("Wi-Fi"));
    m_hms_label         = create_value_label(m_info_panel, grid, _L("HMS"));

    wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_task_label, 0, wxEXPAND | wxTOP, FromDIP(6));
    sizer->Add(progress_sizer, 0, wxEXPAND | wxTOP, FromDIP(4));
    sizer->Add(grid, 0, wxEXPAND | wxTOP, FromDIP(6));
    m_info_panel->SetSizer(sizer);
}

void CameraSlotPanel::set_dev_id(const std::string &dev_id)
{
    if (m_dev_id == dev_id)
        return;
    m_dev_id           = dev_id;
    m_autoplay_pending = true;
    wxGetApp().app_config->set(MULTI_CAMERA_CONFIG_SECTION, slot_config_key(m_slot), m_dev_id);

    if (m_combo_dev_ids.empty())
        return;
    auto it = std::find(m_combo_dev_ids.begin(), m_combo_dev_ids.end(), m_dev_id);
    m_printer_combo->SetSelection(it == m_combo_dev_ids.end() ? 0 : int(it - m_combo_dev_ids.begin()));
}

void CameraSlotPanel::refresh_printer_list(const std::map<std::string, MachineObject *> &machines)
{
    // rebuild only when the list changed, so an open dropdown is not reset every tick
    std::string key;
    for (auto &it : machines)
        key += it.first + "|" + it.second->get_dev_name() + "|" + (it.second->is_online() ? "1" : "0") + ";";
    if (key == m_printer_list_key)
        return;
    m_printer_list_key = key;

    m_printer_combo->Clear();
    m_combo_dev_ids.clear();
    m_printer_combo->Append(_L("Select a printer"));
    m_combo_dev_ids.push_back("");
    for (auto &it : machines) {
        wxString name = from_u8(it.second->get_dev_name());
        if (!it.second->is_online())
            name += " (" + _L("Offline") + ")";
        m_printer_combo->Append(name);
        m_combo_dev_ids.push_back(it.first);
    }

    // keep the saved printer even if it is not listed yet (machine list still loading)
    auto sel = std::find(m_combo_dev_ids.begin(), m_combo_dev_ids.end(), m_dev_id);
    m_printer_combo->SetSelection(sel == m_combo_dev_ids.end() ? 0 : int(sel - m_combo_dev_ids.begin()));
}

void CameraSlotPanel::on_printer_selected(wxCommandEvent &evt)
{
    int sel = m_printer_combo->GetSelection();
    if (sel < 0 || sel >= (int) m_combo_dev_ids.size())
        return;
    set_dev_id(m_combo_dev_ids[sel]);
    if (on_printer_changed)
        on_printer_changed(m_slot);
}

void CameraSlotPanel::update(MachineObject *obj)
{
    m_detail_button->Enable(obj != nullptr);
    m_media_play_ctrl->SetMachineObject(obj);

    if (!obj) {
        clear_info();
        set_label(m_status_label, m_dev_id.empty() ? wxString() : _L("Printer not found"), COLOUR_TITLE);
        return;
    }

    // MediaPlayCtrl only auto-retries after a first manual play, so start it for the user
    if (m_autoplay_pending && m_media_play_ctrl->IsEnabled()) {
        m_autoplay_pending = false;
        m_media_play_ctrl->jump_to_play();
    }

    update_status(obj);
    if (!obj->is_online()) {
        clear_info();
        return;
    }
    update_task(obj);
    update_devices(obj);
    m_info_panel->Layout();
}

void CameraSlotPanel::update_status(MachineObject *obj)
{
    wxColour colour;
    wxString text = print_status_text(obj, colour);
    set_label(m_status_label, text, colour);
}

void CameraSlotPanel::update_task(MachineObject *obj)
{
    const std::string &status = obj->print_status;
    bool has_task = obj->is_in_printing() || status == "FINISH" || status == "FAILED";

    if (!has_task) {
        set_label(m_task_label, _L("No task"), COLOUR_TITLE);
        m_progress_bar->SetValue(0);
        set_label(m_percent_label, "0%");
        set_label(m_layer_label, MULTI_CAMERA_NA_STR);
        set_label(m_left_time_label, MULTI_CAMERA_NA_STR);
        set_label(m_finish_time_label, MULTI_CAMERA_NA_STR);
        return;
    }

    set_label(m_task_label, obj->subtask_name.empty() ? _L("Untitled") : from_u8(obj->subtask_name));

    int percent = std::clamp(obj->mc_print_percent, 0, 100);
    m_progress_bar->SetProgressForedColour(status == "PAUSE" ? COLOUR_WARN : status == "FAILED" ? COLOUR_ERROR : COLOUR_OK);
    m_progress_bar->SetValue(percent);
    set_label(m_percent_label, wxString::Format("%d%%", percent));

    if (obj->total_layers > 0)
        set_label(m_layer_label, wxString::Format("%d / %d", obj->curr_layer, obj->total_layers));
    else
        set_label(m_layer_label, MULTI_CAMERA_NA_STR);

    if (obj->is_in_printing() && obj->mc_left_time > 0) {
        bool          use_12h_format = wxGetApp().app_config->get("use_12h_time_format") == "true";
        BBLFinishTime finish_time    = get_bbl_finish_time(obj->mc_left_time, use_12h_format);
        wxString      finish_text    = from_u8(finish_time.time);
        if (finish_time.day_offset > 0)
            finish_text += wxString::Format(" (+%d)", finish_time.day_offset);
        set_label(m_left_time_label, from_u8(get_bbl_monitor_time_dhm(obj->mc_left_time)));
        set_label(m_finish_time_label, finish_text.empty() ? wxString(MULTI_CAMERA_NA_STR) : finish_text);
    } else {
        set_label(m_left_time_label, MULTI_CAMERA_NA_STR);
        set_label(m_finish_time_label, MULTI_CAMERA_NA_STR);
    }
}

void CameraSlotPanel::update_devices(MachineObject *obj)
{
    set_label(m_speed_label, wxString::Format("%s (%d%%)", speed_level_text(obj->GetPrintingSpeedLevel()), obj->printing_speed_mag));

    DevExtderSystem *extder_system = obj->GetExtderSystem();
    if (extder_system && extder_system->GetTotalExtderCount() > 1) {
        // the deputy extruder is the left one on dual nozzle printers
        set_label(m_nozzle_label, wxString::Format("L %s  R %s",
                                                   temp_text(extder_system->GetNozzleTempCurrent(DEPUTY_EXTRUDER_ID), extder_system->GetNozzleTempTarget(DEPUTY_EXTRUDER_ID)),
                                                   temp_text(extder_system->GetNozzleTempCurrent(MAIN_EXTRUDER_ID), extder_system->GetNozzleTempTarget(MAIN_EXTRUDER_ID))));
    } else if (extder_system) {
        set_label(m_nozzle_label, temp_text(extder_system->GetNozzleTempCurrent(MAIN_EXTRUDER_ID), extder_system->GetNozzleTempTarget(MAIN_EXTRUDER_ID)));
    }

    if (DevBed *bed = obj->GetBed())
        set_label(m_bed_label, temp_text(bed->GetBedTemp(), bed->GetBedTempTarget()));

    auto chamber = obj->GetChamber();
    if (chamber && chamber->SupportChamberTempDisplay())
        set_label(m_chamber_label, temp_text(chamber->GetChamberTemp(), chamber->SupportChamberEdit() ? chamber->GetChamberTempTarget() : 0));
    else
        set_label(m_chamber_label, MULTI_CAMERA_NA_STR);

    if (DevFan *fan = obj->GetFan()) {
        wxString fan_text = _L("Part") + " " + fan_percent(fan->GetCoolingFanSpeed());
        if (fan->GetSupportAuxFanData())
            fan_text += "  " + _L("Aux") + " " + fan_percent(fan->GetBigFan1Speed()) + "  " + _L("Chamber") + " " + fan_percent(fan->GetBigFan2Speed());
        set_label(m_fan_label, fan_text);
    }

    set_label(m_wifi_label, obj->wifi_signal.empty() ? wxString(MULTI_CAMERA_NA_STR) : from_u8(obj->wifi_signal));

    size_t hms_count = obj->GetHMS() ? obj->GetHMS()->GetHMSItems().size() : 0;
    if (obj->print_error != 0)
        set_label(m_hms_label, wxString::Format(_L("Error %s"), from_u8(obj->get_print_error_str())), COLOUR_ERROR);
    else if (hms_count > 0)
        set_label(m_hms_label, wxString::Format(_L("%d message(s)"), int(hms_count)), COLOUR_WARN);
    else
        set_label(m_hms_label, _L("Normal"), COLOUR_OK);
}

void CameraSlotPanel::clear_info()
{
    set_label(m_task_label, MULTI_CAMERA_NA_STR, COLOUR_TITLE);
    m_progress_bar->SetValue(0);
    set_label(m_percent_label, "0%");
    for (Label *label : {m_layer_label, m_left_time_label, m_finish_time_label, m_speed_label, m_nozzle_label, m_bed_label, m_chamber_label, m_fan_label, m_wifi_label,
                         m_hms_label})
        set_label(label, MULTI_CAMERA_NA_STR);
}

void CameraSlotPanel::msw_rescale()
{
    m_printer_combo->Rescale();
    m_detail_button->Rescale();
    m_progress_bar->Rescale();
    m_media_play_ctrl->msw_rescale();
    Layout();
}

MultiCameraPage::MultiCameraPage(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size, long style)
    : wxPanel(parent, id, pos, size, style)
{
    SetBackgroundColour(wxColour("#EEEEEE"));

    // MediaPlayCtrl listens to the show events of its grand-grand-parent, which must be this page
    m_grid_panel = new wxPanel(this);
    m_grid_panel->SetBackgroundColour(wxColour("#EEEEEE"));
    wxGridSizer *grid_sizer = new wxGridSizer(2, 2, FromDIP(8), FromDIP(8));
    for (int i = 0; i < MULTI_CAMERA_SLOT_COUNT; ++i) {
        CameraSlotPanel *slot   = new CameraSlotPanel(m_grid_panel, i);
        slot->on_printer_changed = [this](int slot) { on_slot_changed(slot); };
        grid_sizer->Add(slot, 1, wxEXPAND);
        m_slots.push_back(slot);
    }
    m_grid_panel->SetSizer(grid_sizer);

    wxBoxSizer *main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->Add(m_grid_panel, 1, wxEXPAND | wxALL, FromDIP(8));
    SetSizer(main_sizer);
    Layout();

    wxGetApp().UpdateDarkUIWin(this);

    m_refresh_timer = new wxTimer(this);
    Bind(wxEVT_TIMER, &MultiCameraPage::on_timer, this);
}

MultiCameraPage::~MultiCameraPage()
{
    if (m_refresh_timer)
        m_refresh_timer->Stop();
    delete m_refresh_timer;
}

bool MultiCameraPage::Show(bool show)
{
    // show first, the live view only starts on a panel shown on screen
    bool result = wxPanel::Show(show);
    if (show) {
        m_subscribed.clear();
        update_subscribe();
        for (CameraSlotPanel *slot : m_slots)
            slot->request_autoplay();
        m_refresh_timer->Start(MULTI_CAMERA_REFRESH_MSEC);
        wxTimerEvent evt;
        on_timer(evt);
    } else {
        m_refresh_timer->Stop();
        if (DeviceManager *dev = wxGetApp().getDeviceManager()) {
            dev->set_full_parse_devices({});
            if (!m_subscribed.empty())
                dev->subscribe_device_list(std::vector<std::string>());
        }
        m_subscribed.clear();
    }
    return result;
}

void MultiCameraPage::on_slot_changed(int slot)
{
    // a printer accepts a single live view client, keep it in one slot only
    const std::string &dev_id = m_slots[slot]->get_dev_id();
    if (!dev_id.empty()) {
        for (CameraSlotPanel *other : m_slots) {
            if (other != m_slots[slot] && other->get_dev_id() == dev_id)
                other->set_dev_id("");
        }
    }
    update_subscribe();
    wxTimerEvent evt;
    on_timer(evt);
}

void MultiCameraPage::update_subscribe()
{
    DeviceManager *dev = wxGetApp().getDeviceManager();
    if (!dev)
        return;

    std::vector<std::string> dev_ids;
    for (CameraSlotPanel *slot : m_slots) {
        if (!slot->get_dev_id().empty())
            dev_ids.push_back(slot->get_dev_id());
    }
    if (dev_ids == m_subscribed)
        return;
    m_subscribed = dev_ids;

    // non selected cloud printers only parse a few key fields unless asked otherwise
    dev->set_full_parse_devices(dev_ids);

    std::vector<std::string> cloud_ids;
    for (auto &dev_id : dev_ids) {
        MachineObject *obj = dev->get_my_machine(dev_id);
        if (obj && !obj->is_lan_mode_printer())
            cloud_ids.push_back(dev_id);
    }
    dev->subscribe_device_list(cloud_ids);

    // the subscription takes a moment, ask for the full status a few times
    m_push_all_retry = 6;
    request_full_status();
}

void MultiCameraPage::request_full_status()
{
    DeviceManager *dev = wxGetApp().getDeviceManager();
    if (!dev || m_push_all_retry <= 0)
        return;
    --m_push_all_retry;
    for (CameraSlotPanel *slot : m_slots) {
        if (slot->get_dev_id().empty())
            continue;
        MachineObject *obj = dev->get_my_machine(slot->get_dev_id());
        if (!obj || !obj->is_online())
            continue;
        obj->command_request_push_all(true);
        if (!obj->is_info_ready())
            obj->command_get_version();
    }
}

void MultiCameraPage::on_timer(wxTimerEvent &event)
{
    DeviceManager *dev = wxGetApp().getDeviceManager();
    if (!dev || !IsShown())
        return;

    if (++m_tick % 5 == 0)
        request_full_status();

    std::map<std::string, MachineObject *> machines = dev->get_my_machine_list();
    for (CameraSlotPanel *slot : m_slots) {
        slot->refresh_printer_list(machines);
        // never keep MachineObject pointers between ticks, the device manager may delete them
        MachineObject *obj = nullptr;
        if (!slot->get_dev_id().empty()) {
            auto it = machines.find(slot->get_dev_id());
            if (it != machines.end())
                obj = it->second;
        }
        slot->update(obj);
    }
}

void MultiCameraPage::msw_rescale()
{
    for (CameraSlotPanel *slot : m_slots)
        slot->msw_rescale();
    Layout();
    Refresh();
}

void MultiCameraPage::on_sys_color_changed()
{
    wxGetApp().UpdateDarkUIWin(this);
    Refresh();
}

} // namespace GUI
} // namespace Slic3r
