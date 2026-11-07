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
#include "DeviceCore/DevFilaSystem.h"
#include "DeviceCore/DevHMS.h"
#include "DeviceCore/DevHMSQuery.h"
#include "DeviceCore/DevLamp.h"
#include "DeviceCore/DevNozzleSystem.h"

#include "Widgets/Button.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/ProgressBar.hpp"
#include "Widgets/StateColor.hpp"

#include "libslic3r/ProjectTask.hpp"
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
static const wxColour COLOUR_PAGE_BG("#EEEEEE");

static std::string slot_config_key(int slot) { return "slot_" + std::to_string(slot); }

// avoid flicker by only touching labels whose text really changed
static void set_label(Label *label, const wxString &text, const wxColour &colour = COLOUR_VALUE)
{
    if (label->GetLabel() != text) {
        label->SetLabel(text);
        label->SetToolTip(text);
    }
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

static wxString print_error_text(MachineObject *obj)
{
    if (obj->print_error == 0)
        return {};
    if (HMSQueryMgr *hms = wxGetApp().get_hms_query_mgr()) {
        HMSResult r = hms->query_error(obj->get_dev_id(), obj->print_error);
        if (!r.text.empty())
            return r.text;
    }
    return wxString::Format(_L("Error %s"), from_u8(obj->get_print_error_str()));
}

// status shown in the cell header, following the print process like the Device page
static wxString print_process_text(MachineObject *obj, wxColour &colour)
{
    colour = COLOUR_VALUE;
    if (!obj->is_online()) {
        colour = COLOUR_TITLE;
        return _L("Offline");
    }
    if (obj->is_connecting())
        return _L("Connecting...");

    const std::string &status = obj->print_status;
    wxString           stage  = obj->get_curr_stage();
    wxString           error  = print_error_text(obj);
    auto with_detail = [](const wxString &text, const wxString &detail) {
        return detail.empty() || detail == text ? text : text + " · " + detail;
    };

    if (status == "RUNNING") {
        colour = COLOUR_OK;
        return stage.empty() ? _L("Printing") : stage;
    }
    if (status == "PREPARE") {
        colour = COLOUR_OK;
        return with_detail(_L("Preparing"), stage);
    }
    if (status == "SLICING") {
        colour = COLOUR_OK;
        return _L("Slicing");
    }
    if (status == "PAUSE") {
        colour = COLOUR_WARN;
        return with_detail(_L("Paused"), error.empty() ? stage : error);
    }
    if (status == "FINISH") {
        colour = COLOUR_OK;
        return _L("Finished");
    }
    if (status == "FAILED") {
        colour = COLOUR_ERROR;
        return with_detail(_L("Failed"), error);
    }
    return _L("Idle");
}

// AMS units are named A, B, C... and AMS HT units start at 128
static wxString ams_unit_name(const std::string &ams_id)
{
    int ams = std::atoi(ams_id.c_str());
    if (ams >= VIRTUAL_TRAY_DEPUTY_ID)
        return _L("Ext");
    return ams >= 128 ? wxString::Format("HT%d", ams - 127) : wxString(wxUniChar('A' + ams));
}

static wxString slot_name(const std::string &ams_id, const std::string &slot_id)
{
    if (std::atoi(ams_id.c_str()) >= VIRTUAL_TRAY_DEPUTY_ID)
        return _L("Ext");
    return wxString::Format("%s%d", ams_unit_name(ams_id), std::atoi(slot_id.c_str()) + 1);
}

static Button *create_button(wxWindow *parent, const wxString &text, const wxString &tooltip)
{
    StateColor bg(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed), std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
                  std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal));
    StateColor fg(std::pair<wxColour, int>(wxColour(144, 144, 144), StateColor::Disabled), std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));

    Button *button = new Button(parent, text);
    button->SetBackgroundColor(bg);
    button->SetBorderColor(fg);
    button->SetTextColor(fg);
    button->SetFont(Label::Body_12);
    button->SetCornerRadius(parent->FromDIP(4));
    button->SetMinSize(wxSize(parent->FromDIP(64), parent->FromDIP(26)));
    button->SetToolTip(tooltip);
    return button;
}

CameraSlotPanel::CameraSlotPanel(wxWindow *parent, int slot)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL)
    , m_slot(slot)
{
    SetBackgroundColour(*wxWHITE);

    m_printer_combo = new ComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(200), FromDIP(28)), 0, nullptr, wxCB_READONLY);
    m_printer_combo->Bind(wxEVT_COMBOBOX, &CameraSlotPanel::on_printer_selected, this);

    m_status_label = new Label(this, Label::Head_13, wxEmptyString, wxALIGN_LEFT | wxST_ELLIPSIZE_END);

    m_detail_button = create_button(this, _L("Details"), _L("Open this printer in the Device page"));
    m_detail_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
        if (!m_dev_id.empty() && wxGetApp().mainframe)
            wxGetApp().mainframe->jump_to_monitor(m_dev_id);
    });

    // live view, MediaPlayCtrl stops/starts the stream with the show state of the page
    m_media_ctrl = new wxMediaCtrl3(this);
    m_media_ctrl->SetMinSize(wxSize(FromDIP(320), FromDIP(180)));
    m_media_play_ctrl = new MediaPlayCtrl(this, m_media_ctrl, wxDefaultPosition, wxSize(-1, FromDIP(32)));
    // several streams at once, a modal dialog per failing stream would be confusing
    m_media_play_ctrl->SetShowLanFailedDialog(false);

    create_info_panel();
    layout_body();

    m_dev_id = wxGetApp().app_config->get(MULTI_CAMERA_CONFIG_SECTION, slot_config_key(m_slot));
    clear_info();
}

Label *CameraSlotPanel::add_info_item(const wxString &title)
{
    InfoItem item;
    item.title = new Label(m_info_panel, Label::Body_12, title);
    item.title->SetForegroundColour(COLOUR_TITLE);
    item.value = new Label(m_info_panel, Label::Body_12, MULTI_CAMERA_NA_STR, wxALIGN_LEFT | wxST_ELLIPSIZE_END);
    item.value->SetForegroundColour(COLOUR_VALUE);
    item.value->SetMinSize(wxSize(FromDIP(40), -1));
    m_info_items.push_back(item);
    return item.value;
}

void CameraSlotPanel::create_info_panel()
{
    m_info_panel = new wxPanel(this);
    m_info_panel->SetBackgroundColour(*wxWHITE);

    m_task_label = new Label(m_info_panel, Label::Head_14, MULTI_CAMERA_NA_STR, wxALIGN_LEFT | wxST_ELLIPSIZE_END);
    m_task_label->SetMinSize(wxSize(FromDIP(60), -1));
    m_plate_label = new Label(m_info_panel, Label::Body_12, wxEmptyString);
    m_plate_label->SetForegroundColour(COLOUR_TITLE);

    m_progress_bar = new ProgressBar(m_info_panel, wxID_ANY, 100, wxDefaultPosition, wxSize(-1, FromDIP(8)));
    m_progress_bar->SetHeight(FromDIP(8));
    m_progress_bar->SetValue(0);
    m_percent_label = new Label(m_info_panel, Label::Head_14, "0%");
    m_percent_label->SetMinSize(wxSize(FromDIP(44), -1));

    m_filament_swatch = new wxPanel(m_info_panel, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(10), FromDIP(10)));
    m_filament_swatch->SetBackgroundColour(*wxWHITE);

    // the order here is the display order
    m_layer_label       = add_info_item(_L("Layer"));
    m_left_time_label   = add_info_item(_L("Remaining"));
    m_finish_time_label = add_info_item(_L("Finish at"));
    m_total_time_label  = add_info_item(_L("Total time"));
    m_weight_label      = add_info_item(_L("Filament usage"));
    m_speed_label       = add_info_item(_L("Speed"));
    m_nozzle_label      = add_info_item(_L("Nozzle"));
    m_bed_label         = add_info_item(_L("Bed"));
    m_chamber_label     = add_info_item(_L("Chamber"));
    m_fan_label         = add_info_item(_L("Fan"));
    m_filament_label    = add_info_item(_L("Filament"));
    m_ams_label         = add_info_item(_L("AMS"));
    m_nozzle_info_label = add_info_item(_L("Nozzle type"));
    m_lamp_label        = add_info_item(_L("Lamp"));
    m_wifi_label        = add_info_item(_L("Wi-Fi"));
    m_hms_label         = add_info_item(_L("HMS"));

    layout_info();
}

void CameraSlotPanel::layout_info()
{
    // pairs of "title value" per row: a single column beside the video, two below it
    int pairs = m_horizontal ? 1 : 2;

    // the old sizers are deleted first, a window may only belong to one sizer
    m_info_panel->SetSizer(nullptr);
    for (InfoItem &item : m_info_items) {
        wxBoxSizer *value_sizer = new wxBoxSizer(wxHORIZONTAL);
        if (item.value == m_filament_label)
            value_sizer->Add(m_filament_swatch, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
        value_sizer->Add(item.value, 1, wxALIGN_CENTER_VERTICAL);
        item.value_sizer = value_sizer;
    }

    wxFlexGridSizer *grid = new wxFlexGridSizer(0, pairs * 2, FromDIP(3), FromDIP(8));
    for (int i = 0; i < pairs; ++i)
        grid->AddGrowableCol(i * 2 + 1, 1);
    for (InfoItem &item : m_info_items) {
        grid->Add(item.title, 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(item.value_sizer, 1, wxEXPAND);
    }

    wxBoxSizer *task_sizer = new wxBoxSizer(wxHORIZONTAL);
    task_sizer->Add(m_task_label, 1, wxALIGN_CENTER_VERTICAL);
    task_sizer->Add(m_plate_label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));

    wxBoxSizer *progress_sizer = new wxBoxSizer(wxHORIZONTAL);
    progress_sizer->Add(m_progress_bar, 1, wxALIGN_CENTER_VERTICAL);
    progress_sizer->Add(m_percent_label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));

    wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(task_sizer, 0, wxEXPAND | wxTOP, FromDIP(6));
    sizer->Add(progress_sizer, 0, wxEXPAND | wxTOP, FromDIP(4));
    sizer->Add(grid, 0, wxEXPAND | wxTOP, FromDIP(8));
    m_info_panel->SetSizer(sizer);
}

void CameraSlotPanel::layout_body()
{
    SetSizer(nullptr);

    wxBoxSizer *header_sizer = new wxBoxSizer(wxHORIZONTAL);
    header_sizer->Add(m_printer_combo, 0, wxALIGN_CENTER_VERTICAL);
    header_sizer->Add(m_status_label, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(10));
    header_sizer->Add(m_detail_button, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(6));

    wxBoxSizer *video_sizer = new wxBoxSizer(wxVERTICAL);
    video_sizer->Add(m_media_ctrl, 1, wxEXPAND);
    video_sizer->Add(m_media_play_ctrl, 0, wxEXPAND);

    wxBoxSizer *main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->Add(header_sizer, 0, wxEXPAND | wxALL, FromDIP(6));
    if (m_horizontal) {
        wxBoxSizer *body_sizer = new wxBoxSizer(wxHORIZONTAL);
        body_sizer->Add(video_sizer, 3, wxEXPAND);
        body_sizer->Add(m_info_panel, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(12));
        main_sizer->Add(body_sizer, 1, wxEXPAND | wxBOTTOM, FromDIP(6));
    } else {
        main_sizer->Add(video_sizer, 1, wxEXPAND);
        main_sizer->Add(m_info_panel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));
    }
    SetSizer(main_sizer);
}

void CameraSlotPanel::set_horizontal(bool horizontal)
{
    if (m_horizontal == horizontal)
        return;
    m_horizontal = horizontal;
    layout_info();
    layout_body();
    Layout();
}

void CameraSlotPanel::set_camera_source(MultiCameraSource source)
{
    m_media_play_ctrl->SetPreferLan(source == MultiCameraSource::LanFirst);
    m_media_play_ctrl->SetLanFallback(source != MultiCameraSource::CloudOnly);
}

void CameraSlotPanel::set_keep_alive(bool keep_alive) { m_media_play_ctrl->SetKeepAlive(keep_alive); }

void CameraSlotPanel::play()
{
    m_autoplay_pending = false;
    if (m_media_play_ctrl->IsEnabled())
        m_media_play_ctrl->jump_to_play();
    else
        m_autoplay_pending = true; // start once the printer info is ready
}

void CameraSlotPanel::stop()
{
    m_autoplay_pending = false;
    m_media_play_ctrl->StopPlay();
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
    update_temperatures(obj);
    update_filament(obj);
    update_others(obj);
    m_info_panel->Layout();
}

void CameraSlotPanel::update_status(MachineObject *obj)
{
    wxColour colour;
    wxString text = print_process_text(obj, colour);
    set_label(m_status_label, text, colour);
}

void CameraSlotPanel::update_task(MachineObject *obj)
{
    const std::string &status   = obj->print_status;
    bool               has_task = obj->is_in_printing() || status == "FINISH" || status == "FAILED";

    if (!has_task) {
        set_label(m_task_label, _L("No task"), COLOUR_TITLE);
        set_label(m_plate_label, wxEmptyString, COLOUR_TITLE);
        m_progress_bar->SetValue(0);
        set_label(m_percent_label, "0%");
        for (Label *label : {m_layer_label, m_left_time_label, m_finish_time_label, m_total_time_label, m_weight_label})
            set_label(label, MULTI_CAMERA_NA_STR);
        return;
    }

    set_label(m_task_label, obj->subtask_name.empty() ? _L("Untitled") : from_u8(obj->subtask_name));
    set_label(m_plate_label, obj->m_plate_index > 0 ? wxString::Format(_L("Plate %d"), obj->m_plate_index) : wxString(), COLOUR_TITLE);

    int percent = std::clamp(obj->mc_print_percent, 0, 100);
    m_progress_bar->SetProgressForedColour(status == "PAUSE" ? COLOUR_WARN : status == "FAILED" ? COLOUR_ERROR : COLOUR_OK);
    m_progress_bar->SetValue(percent);
    set_label(m_percent_label, wxString::Format("%d%%", percent));

    set_label(m_layer_label, obj->total_layers > 0 ? wxString::Format("%d / %d", obj->curr_layer, obj->total_layers) : wxString(MULTI_CAMERA_NA_STR));

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

    // estimates from the sliced project, only known for cloud tasks
    BBLSliceInfo *slice_info = obj->slice_info;
    if (slice_info && slice_info->prediction > 0)
        set_label(m_total_time_label, from_u8(get_bbl_time_dhms(slice_info->prediction)));
    else
        set_label(m_total_time_label, MULTI_CAMERA_NA_STR);
    if (slice_info && slice_info->weight > 0)
        set_label(m_weight_label, wxString::Format("%.2f g", slice_info->weight));
    else
        set_label(m_weight_label, MULTI_CAMERA_NA_STR);
}

void CameraSlotPanel::update_temperatures(MachineObject *obj)
{
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

    set_label(m_speed_label, wxString::Format("%s (%d%%)", speed_level_text(obj->GetPrintingSpeedLevel()), obj->printing_speed_mag));

    if (DevFan *fan = obj->GetFan()) {
        wxString fan_text = _L("Part") + " " + fan_percent(fan->GetCoolingFanSpeed());
        if (fan->GetSupportAuxFanData())
            fan_text += "  " + _L("Aux") + " " + fan_percent(fan->GetBigFan1Speed()) + "  " + _L("Chamber") + " " + fan_percent(fan->GetBigFan2Speed());
        set_label(m_fan_label, fan_text);
    }
}

void CameraSlotPanel::update_filament(MachineObject *obj)
{
    DevExtderSystem *extder_system = obj->GetExtderSystem();
    DevAmsTray *     tray          = obj->get_curr_tray();
    if (tray && extder_system && !tray->get_display_filament_type().empty()) {
        set_label(m_filament_label, wxString::Format("%s (%s)", from_u8(tray->get_display_filament_type()),
                                                     slot_name(extder_system->GetCurrentAmsId(), extder_system->GetCurrentSlotId())));
        wxColour colour(wxString("#") + from_u8(tray->color.substr(0, 6)));
        if (colour.IsOk() && m_filament_swatch->GetBackgroundColour() != colour) {
            m_filament_swatch->SetBackgroundColour(colour);
            m_filament_swatch->Refresh();
        }
    } else {
        set_label(m_filament_label, MULTI_CAMERA_NA_STR);
        m_filament_swatch->SetBackgroundColour(*wxWHITE);
    }

    // humidity of every AMS unit, e.g. "A: level 2  B: level 3"
    wxString ams_text;
    if (auto fila_system = obj->GetFilaSystem()) {
        for (auto &it : fila_system->GetAmsList()) {
            DevAms *ams = it.second;
            if (!ams)
                continue;
            wxString humidity;
            if (ams->SupportHumidityPercent())
                humidity = wxString::Format("%d%%", ams->GetHumidityPercent());
            else if (ams->SupportHumidityLevel())
                humidity = wxString::Format(_L("level %d"), ams->GetHumidityLevel());
            else
                continue;
            if (!ams_text.empty())
                ams_text += "  ";
            ams_text += ams_unit_name(ams->GetAmsId()) + ": " + humidity;
        }
    }
    set_label(m_ams_label, ams_text.empty() ? wxString(MULTI_CAMERA_NA_STR) : _L("Humidity") + " " + ams_text);

    if (extder_system && extder_system->GetTotalExtderCount() > 0) {
        float diameter = extder_system->GetNozzleDiameter(MAIN_EXTRUDER_ID);
        set_label(m_nozzle_info_label, wxString::Format("%s %.1f mm", DevNozzle::GetNozzleTypeStr(extder_system->GetNozzleType(MAIN_EXTRUDER_ID)), diameter));
    }
}

void CameraSlotPanel::update_others(MachineObject *obj)
{
    if (DevLamp *lamp = obj->GetLamp())
        set_label(m_lamp_label, lamp->IsChamberLightOn() ? _L("On") : _L("Off"));

    set_label(m_wifi_label, obj->wifi_signal.empty() ? wxString(MULTI_CAMERA_NA_STR) : from_u8(obj->wifi_signal));

    // show the most severe HMS message, the Device page lists all of them
    const DevHMSItem *worst = nullptr;
    size_t            count = 0;
    if (obj->GetHMS()) {
        for (const DevHMSItem &item : obj->GetHMS()->GetHMSItems()) {
            if (item.get_level() == HMS_UNKNOWN || item.get_level() >= HMS_MSG_LEVEL_MAX)
                continue;
            ++count;
            if (!worst || item.get_level() < worst->get_level())
                worst = &item;
        }
    }
    if (obj->print_error != 0) {
        set_label(m_hms_label, print_error_text(obj), COLOUR_ERROR);
    } else if (worst) {
        wxString text;
        if (HMSQueryMgr *hms = wxGetApp().get_hms_query_mgr())
            text = hms->query_hms(obj->get_dev_id(), worst->get_long_error_code()).text;
        if (text.empty())
            text = from_u8(worst->get_long_error_code());
        if (count > 1)
            text = wxString::Format("(%d) ", int(count)) + text;
        set_label(m_hms_label, text, worst->get_level() <= HMS_SERIOUS ? COLOUR_ERROR : COLOUR_WARN);
    } else {
        set_label(m_hms_label, _L("Normal"), COLOUR_OK);
    }
}

void CameraSlotPanel::clear_info()
{
    set_label(m_task_label, MULTI_CAMERA_NA_STR, COLOUR_TITLE);
    set_label(m_plate_label, wxEmptyString, COLOUR_TITLE);
    m_progress_bar->SetValue(0);
    set_label(m_percent_label, "0%");
    for (InfoItem &item : m_info_items)
        set_label(item.value, MULTI_CAMERA_NA_STR);
    m_filament_swatch->SetBackgroundColour(*wxWHITE);
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
    SetBackgroundColour(COLOUR_PAGE_BG);

    AppConfig * config     = wxGetApp().app_config;
    std::string layout_str = config->get(MULTI_CAMERA_CONFIG_SECTION, "layout");
    int         layout     = layout_str.empty() ? int(MultiCameraLayout::Quad) : std::atoi(layout_str.c_str());
    m_layout               = layout >= 0 && layout < int(MultiCameraLayout::Count) ? MultiCameraLayout(layout) : MultiCameraLayout::Quad;
    int source             = std::atoi(config->get(MULTI_CAMERA_CONFIG_SECTION, "camera_source").c_str());
    m_source               = source >= 0 && source < int(MultiCameraSource::Count) ? MultiCameraSource(source) : MultiCameraSource::Auto;

    create_toolbar();

    // MediaPlayCtrl listens to the show events of its grand-grand-parent, which must be this page,
    // so the slots always stay children of m_grid_panel and layouts only change sizers
    m_grid_panel = new wxPanel(this);
    m_grid_panel->SetBackgroundColour(COLOUR_PAGE_BG);
    bool keep_alive = config->get(MULTI_CAMERA_CONFIG_SECTION, "keep_alive") == "1";
    for (int i = 0; i < MULTI_CAMERA_SLOT_COUNT; ++i) {
        CameraSlotPanel *slot    = new CameraSlotPanel(m_grid_panel, i);
        slot->on_printer_changed = [this](int slot) { on_slot_changed(slot); };
        slot->set_keep_alive(keep_alive);
        m_slots.push_back(slot);
    }
    apply_camera_source();
    apply_layout();

    wxBoxSizer *main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->Add(m_toolbar, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));
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

void MultiCameraPage::create_toolbar()
{
    AppConfig *config = wxGetApp().app_config;

    m_toolbar = new wxPanel(this);
    m_toolbar->SetBackgroundColour(*wxWHITE);

    Label *layout_title = new Label(m_toolbar, Label::Body_13, _L("Layout"));
    m_layout_combo      = new ComboBox(m_toolbar, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(180), FromDIP(28)), 0, nullptr, wxCB_READONLY);
    m_layout_combo->Append(_L("1 printer"));
    m_layout_combo->Append(_L("2 printers"));
    m_layout_combo->Append(_L("3 printers (columns)"));
    m_layout_combo->Append(_L("3 printers (1 large)"));
    m_layout_combo->Append(_L("4 printers (grid)"));
    m_layout_combo->SetSelection(int(m_layout));
    m_layout_combo->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent &) {
        int sel = m_layout_combo->GetSelection();
        if (sel < 0 || sel >= int(MultiCameraLayout::Count) || MultiCameraLayout(sel) == m_layout)
            return;
        m_layout = MultiCameraLayout(sel);
        wxGetApp().app_config->set(MULTI_CAMERA_CONFIG_SECTION, "layout", std::to_string(sel));
        apply_layout();
        update_subscribe();
        if (!m_user_stopped) {
            for (CameraSlotPanel *slot : m_slots)
                slot->request_autoplay();
        }
    });

    Label *source_title = new Label(m_toolbar, Label::Body_13, _L("Camera"));
    m_source_combo      = new ComboBox(m_toolbar, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(140), FromDIP(28)), 0, nullptr, wxCB_READONLY);
    m_source_combo->Append(_L("Auto"));
    m_source_combo->Append(_L("LAN first"));
    m_source_combo->Append(_L("Cloud only"));
    m_source_combo->SetSelection(int(m_source));
    m_source_combo->SetToolTip(_L("Auto: use the cloud and switch to the LAN when the cloud refuses the live view.\n"
                                  "LAN first: connect to the printer directly when its IP is known (same network only).\n"
                                  "Cloud only: same as the Device page."));
    m_source_combo->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent &) {
        int sel = m_source_combo->GetSelection();
        if (sel < 0 || sel >= int(MultiCameraSource::Count) || MultiCameraSource(sel) == m_source)
            return;
        m_source = MultiCameraSource(sel);
        wxGetApp().app_config->set(MULTI_CAMERA_CONFIG_SECTION, "camera_source", std::to_string(sel));
        apply_camera_source();
        // reconnect with the new source
        for (int i = 0; i < visible_slot_count(); ++i) {
            m_slots[i]->stop();
            if (!m_user_stopped)
                m_slots[i]->play();
        }
    });

    m_keep_alive_check = new CheckBox(m_toolbar);
    m_keep_alive_check->SetValue(config->get(MULTI_CAMERA_CONFIG_SECTION, "keep_alive") == "1");
    Label *keep_alive_title = new Label(m_toolbar, Label::Body_13, _L("Keep streaming"));
    wxString keep_alive_tip = _L("Do not stop the live view when there is no operation or no printing for a while.");
    keep_alive_title->SetToolTip(keep_alive_tip);
    m_keep_alive_check->SetToolTip(keep_alive_tip);
    m_keep_alive_check->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent &e) {
        bool keep_alive = m_keep_alive_check->GetValue();
        wxGetApp().app_config->set(MULTI_CAMERA_CONFIG_SECTION, "keep_alive", keep_alive ? "1" : "0");
        for (CameraSlotPanel *slot : m_slots)
            slot->set_keep_alive(keep_alive);
        e.Skip();
    });

    m_play_all_button = create_button(m_toolbar, _L("Play all"), _L("Start the live view of all printers"));
    m_play_all_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
        m_user_stopped = false;
        for (int i = 0; i < visible_slot_count(); ++i)
            m_slots[i]->play();
    });
    m_stop_all_button = create_button(m_toolbar, _L("Stop all"), _L("Stop the live view of all printers, printing is not affected"));
    m_stop_all_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
        m_user_stopped = true;
        for (CameraSlotPanel *slot : m_slots)
            slot->stop();
    });

    wxBoxSizer *sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(layout_title, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(10));
    sizer->Add(m_layout_combo, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(6));
    sizer->Add(source_title, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(20));
    sizer->Add(m_source_combo, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(6));
    sizer->Add(m_keep_alive_check, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(20));
    sizer->Add(keep_alive_title, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(4));
    sizer->AddStretchSpacer(1);
    sizer->Add(m_play_all_button, 0, wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM, FromDIP(6));
    sizer->Add(m_stop_all_button, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(10));
    m_toolbar->SetSizer(sizer);
}

int MultiCameraPage::visible_slot_count() const
{
    switch (m_layout) {
    case MultiCameraLayout::Single: return 1;
    case MultiCameraLayout::Double: return 2;
    case MultiCameraLayout::TripleColumns:
    case MultiCameraLayout::TripleFocus: return 3;
    default: return 4;
    }
}

void MultiCameraPage::apply_layout()
{
    int gap   = FromDIP(8);
    int count = visible_slot_count();

    m_grid_panel->SetSizer(nullptr);
    for (int i = 0; i < MULTI_CAMERA_SLOT_COUNT; ++i) {
        // a single printer has room for the details beside the video
        m_slots[i]->set_horizontal(m_layout == MultiCameraLayout::Single);
        m_slots[i]->Show(i < count);
    }

    wxSizer *sizer = nullptr;
    switch (m_layout) {
    case MultiCameraLayout::TripleFocus: {
        wxBoxSizer *side = new wxBoxSizer(wxVERTICAL);
        side->Add(m_slots[1], 1, wxEXPAND);
        side->Add(m_slots[2], 1, wxEXPAND | wxTOP, gap);
        sizer = new wxBoxSizer(wxHORIZONTAL);
        sizer->Add(m_slots[0], 2, wxEXPAND);
        sizer->Add(side, 1, wxEXPAND | wxLEFT, gap);
        break;
    }
    case MultiCameraLayout::Quad: {
        sizer = new wxGridSizer(2, 2, gap, gap);
        for (int i = 0; i < count; ++i)
            sizer->Add(m_slots[i], 1, wxEXPAND);
        break;
    }
    default: {
        // one row of equal cells
        sizer = new wxBoxSizer(wxHORIZONTAL);
        for (int i = 0; i < count; ++i)
            sizer->Add(m_slots[i], 1, wxEXPAND | (i > 0 ? wxLEFT : 0), gap);
        break;
    }
    }
    m_grid_panel->SetSizer(sizer);
    m_grid_panel->Layout();
}

void MultiCameraPage::apply_camera_source()
{
    for (CameraSlotPanel *slot : m_slots)
        slot->set_camera_source(m_source);
}

bool MultiCameraPage::Show(bool show)
{
    // show first, the live view only starts on a panel shown on screen
    bool result = wxPanel::Show(show);
    if (show) {
        m_subscribed.clear();
        update_subscribe();
        if (!m_user_stopped) {
            for (CameraSlotPanel *slot : m_slots)
                slot->request_autoplay();
        }
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
    for (int i = 0; i < visible_slot_count(); ++i) {
        if (!m_slots[i]->get_dev_id().empty())
            dev_ids.push_back(m_slots[i]->get_dev_id());
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
    for (const std::string &dev_id : m_subscribed) {
        MachineObject *obj = dev->get_my_machine(dev_id);
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
    for (int i = 0; i < visible_slot_count(); ++i) {
        CameraSlotPanel *slot = m_slots[i];
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
    m_layout_combo->Rescale();
    m_source_combo->Rescale();
    m_play_all_button->Rescale();
    m_stop_all_button->Rescale();
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
