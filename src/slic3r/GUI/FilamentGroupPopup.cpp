#include "FilamentGroupPopup.hpp"
#include "GUI_App.hpp"
#include "MsgDialog.hpp"
#include "wx/dcgraph.h"
#include "I18N.hpp"
#include "PartPlate.hpp"
#include "FilamentMapDialog.hpp"
#include "DeviceCore/DevConfigUtil.h"

#include <algorithm>

namespace Slic3r { namespace GUI {

static const wxColour LabelEnableColor = wxColour("#262E30");
static const wxColour LabelDisableColor = wxColour("#ACACAC");
static const wxColour GreyColor = wxColour("#6B6B6B");
static const wxColour GreenColor = wxColour("#00AE42");
static const wxColour BackGroundColor = wxColour("#FFFFFF");


static bool should_pop_up()
{
    const auto &preset_bundle    = wxGetApp().preset_bundle;
    const auto &full_config      = preset_bundle->full_config();
    const auto  nozzle_diameters = full_config.option<ConfigOptionFloatsNullable>("nozzle_diameter");
    return nozzle_diameters->size() > 1;
}

static FilamentMapMode get_prefered_map_mode()
{
    const static std::map<std::string, int> enum_keys_map = ConfigOptionEnum<FilamentMapMode>::get_enum_values();
    auto                                   &app_config    = wxGetApp().app_config;
    std::string                             mode_str      = app_config->get("prefered_filament_map_mode");
    auto                                    iter          = enum_keys_map.find(mode_str);
    if (iter == enum_keys_map.end()) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format("Could not get prefered_filament_map_mode from app config, use AutoForFlsuh mode");
        return FilamentMapMode::fmmAutoForFlush;
    }
    return FilamentMapMode(iter->second);
}

static void set_prefered_map_mode(FilamentMapMode mode)
{
    const static std::vector<std::string> enum_values = ConfigOptionEnum<FilamentMapMode>::get_enum_names();
    auto                                 &app_config  = wxGetApp().app_config;
    std::string                           mode_str;
    if (mode < enum_values.size()) mode_str = enum_values[mode];

    if (mode_str.empty()) BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format("Set empty prefered_filament_map_mode to app config");
    app_config->set("prefered_filament_map_mode", mode_str);
}

bool play_dual_extruder_slice_video()
{
    const wxString video_url = "https://e.bambulab.com/t?c=HDB24RlwSmt77YFH";
    if (wxLaunchDefaultBrowser(video_url)) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format("Video is being played using the system's default browser.");
        return true;
    }
    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format("launch system's default browser failed");
    return false;
}

bool play_dual_extruder_print_tpu_video()
{
    const wxString video_url = "https://e.bambulab.com/t?c=fwWqpBg37Liel92N";
    if (wxLaunchDefaultBrowser(video_url)){
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format("Print Tpu Video is being played using the system's default browser.");
        return true;
    }
    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format("launch system's default browser failed");
    return false;
}

bool open_filament_group_wiki()
{
    const wxString wiki_url = "https://e.bambulab.com/t?c=mOkvsXkJ9pldGYp9";
    if (wxLaunchDefaultBrowser(wiki_url)) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format("Wiki is being displayed using the system's default browser.");
        return true;
    }
    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format("launch system's default browser failed");
    return false;
}

void FilamentGroupPopup::CreateBmps()
{
    checked_bmp = create_scaled_bitmap("map_mode_on", nullptr, 16);;
    unchecked_bmp = create_scaled_bitmap("map_mode_off", nullptr, 16);
    disabled_bmp = create_scaled_bitmap("map_mode_disabled", nullptr, 16);
    checked_hover_bmp = create_scaled_bitmap("map_mode_on_hovered", nullptr, 16);
    unchecked_hover_bmp = create_scaled_bitmap("map_mode_off_hovered", nullptr, 16);
}

void FilamentGroupPopup::RecreateUIElements()
{
    const wxString AutoForFlushLabel = _L("Filament-Saving Mode");
    const wxString AutoForMatchLabel = _L("Convenience Mode");
    const wxString AutoForQualityLabel = _L("Quality Mode");
    const wxString ManualLabel       = _L("Custom Mode");

    std::string pt = wxGetApp().preset_bundle->printers.get_edited_preset().get_printer_type(wxGetApp().preset_bundle);
    wxString main_nozzle_lower   = _L(DevPrinterConfigUtil::get_toolhead_display_name(pt, MAIN_EXTRUDER_ID, ToolHeadComponent::Nozzle, ToolHeadNameCase::LowerCase));
    wxString deputy_nozzle_lower = _L(DevPrinterConfigUtil::get_toolhead_display_name(pt, DEPUTY_EXTRUDER_ID, ToolHeadComponent::Nozzle, ToolHeadNameCase::LowerCase));

    const wxString AutoForFlushDetail = wxString::Format(_L("Generates filament grouping for the %s and %s based on the most filament-saving principles to minimize waste"), deputy_nozzle_lower, main_nozzle_lower);
    const wxString AutoForMatchDetail = wxString::Format(_L("Generates filament grouping for the %s and %s based on the printer's actual filament status, reducing the need for manual filament adjustment"), deputy_nozzle_lower, main_nozzle_lower);
    const wxString AutoForQualityDetail = wxString::Format(_L("Generates filament grouping for the %s and %s based on quality optimization principles"), deputy_nozzle_lower, main_nozzle_lower);
    const wxString ManualDetail       = wxString::Format(_L("Manually assign filament to the %s or %s"), deputy_nozzle_lower, main_nozzle_lower);

    const wxString AutoForFlushDesp = ""; //_L("(Post-slicing arrangement)");
    const wxString ManualDesp       = "";
    const wxString AutoForMatchDesp = "";// _L("(Pre-slicing arrangement)");
    const wxString AutoForQualityDesp = "";

    wxBoxSizer *top_sizer         = new wxBoxSizer(wxVERTICAL);
    const int   horizontal_margin = FromDIP(16);
    const int   vertical_margin   = FromDIP(15);
    const int   vertical_padding  = FromDIP(12);
    const int   ratio_spacing     = FromDIP(4);

    SetBackgroundColour(BackGroundColor);

    // Create all possible modes
    m_all_modes = {fmmAutoForFlush, fmmAutoForMatch, fmmAutoForQuality, fmmManual};
    m_available_modes = m_all_modes;

    // Resize vectors to match the number of all modes
    size_t mode_count = m_all_modes.size();
    radio_btns.resize(mode_count);
    button_labels.resize(mode_count);
    button_desps.resize(mode_count);
    detail_infos.resize(mode_count);
    button_sizers.resize(mode_count);
    label_sizers.resize(mode_count);
    mode_spacer.resize(mode_count);

    // Create vectors to hold text, descriptions, and details for each mode
    std::vector<wxString> btn_texts;
    std::vector<wxString> btn_desps;
    std::vector<wxString> mode_details;

    for (FilamentMapMode mode : m_all_modes) {
        wxString label, detail, desp;
        switch (mode) {
            case fmmAutoForFlush:
                label = AutoForFlushLabel;
                detail = AutoForFlushDetail;
                desp = AutoForFlushDesp;
                break;
            case fmmAutoForMatch:
                label = AutoForMatchLabel;
                detail = AutoForMatchDetail;
                desp = AutoForMatchDesp;
                break;
            case fmmAutoForQuality:
                label = AutoForQualityLabel;
                detail = AutoForQualityDetail;
                desp = AutoForQualityDesp;
                break;
            case fmmManual:
                label = ManualLabel;
                detail = ManualDetail;
                desp = ManualDesp;
                break;
            default:
                label = wxEmptyString;
                detail = wxEmptyString;
                desp = wxEmptyString;
                break;
        }
        btn_texts.push_back(label);
        btn_desps.push_back(desp);
        mode_details.push_back(detail);
    }

    top_sizer->AddSpacer(vertical_margin);

    for (size_t idx = 0; idx < mode_count; ++idx) {
        button_sizers[idx] = new wxBoxSizer(wxHORIZONTAL);
        radio_btns[idx]          = new wxBitmapButton(this, wxID_ANY, unchecked_bmp, wxDefaultPosition, wxDefaultSize, wxNO_BORDER);
        radio_btns[idx]->SetBackgroundColour(BackGroundColor);

        button_labels[idx] = new Label(this, btn_texts[idx]);
        button_labels[idx]->SetBackgroundColour(BackGroundColor);
        button_labels[idx]->SetForegroundColour(LabelEnableColor);
        button_labels[idx]->SetFont(Label::Body_14);

        button_desps[idx] = new Label(this, btn_desps[idx]);
        button_desps[idx]->SetBackgroundColour(BackGroundColor);
        button_desps[idx]->SetForegroundColour(LabelEnableColor);
        button_desps[idx]->SetFont(Label::Body_14);

#if 0
        global_mode_tags[idx] = new wxBitmapButton(this, wxID_ANY, global_tag_bmp, wxDefaultPosition, wxDefaultSize, wxNO_BORDER);
        global_mode_tags[idx]->SetBackgroundColour(BackGroundColor);
        global_mode_tags[idx]->SetToolTip(_L("Global settings"));
#endif
        button_sizers[idx]->Add(radio_btns[idx], 0, wxALIGN_CENTER);
        button_sizers[idx]->AddSpacer(ratio_spacing);
        button_sizers[idx]->Add(button_labels[idx], 0, wxALIGN_CENTER);
        button_sizers[idx]->Add(button_desps[idx], 0, wxALIGN_CENTER);
        //button_sizer->AddSpacer(ratio_spacing);
        //button_sizer->Add(global_mode_tags[idx], 0, wxALIGN_CENTER);

        label_sizers[idx] = new wxBoxSizer(wxHORIZONTAL);

        detail_infos[idx] = new Label(this, mode_details[idx]);
        detail_infos[idx]->SetBackgroundColour(BackGroundColor);
        detail_infos[idx]->SetForegroundColour(GreyColor);
        detail_infos[idx]->SetFont(Label::Body_12);
        detail_infos[idx]->Wrap(FromDIP(320));

        label_sizers[idx]->AddSpacer(radio_btns[idx]->GetRect().width + ratio_spacing);
        label_sizers[idx]->Add(detail_infos[idx], 1, wxALIGN_CENTER_VERTICAL);

        top_sizer->Add(button_sizers[idx], 0, wxLEFT | wxRIGHT, horizontal_margin);
        top_sizer->Add(label_sizers[idx], 0, wxLEFT | wxRIGHT, horizontal_margin);
        mode_spacer[idx] = top_sizer->AddSpacer(vertical_padding);

        radio_btns[idx]->Bind(wxEVT_LEFT_DOWN, [this, idx](auto &) { OnRadioBtn(idx);});

        radio_btns[idx]->Bind(wxEVT_ENTER_WINDOW, [this, idx](auto &) { UpdateButtonStatus(idx); });
        radio_btns[idx]->Bind(wxEVT_LEAVE_WINDOW, [this](auto &) { UpdateButtonStatus(); });

        button_labels[idx]->Bind(wxEVT_LEFT_DOWN, [this, idx](auto &) { OnRadioBtn(idx);});
        button_labels[idx]->Bind(wxEVT_ENTER_WINDOW, [this, idx](auto &) { UpdateButtonStatus(idx); });
        button_labels[idx]->Bind(wxEVT_LEAVE_WINDOW, [this](auto &) { UpdateButtonStatus(); });
    }

    // Smart filament assign section
    MakeSmartFilamentSection(top_sizer, horizontal_margin, vertical_padding);

    {
        wxBoxSizer *button_sizer = new wxBoxSizer(wxHORIZONTAL);

        auto* video_sizer = new wxBoxSizer(wxHORIZONTAL);
        video_link = new wxStaticText(this, wxID_ANY, _L("Video tutorial"));
        video_link->SetBackgroundColour(BackGroundColor);
        video_link->SetForegroundColour(GreenColor);
        video_link->SetFont(Label::Body_12.Underlined());
        video_link->SetCursor(wxCursor(wxCURSOR_HAND));
        video_link->Bind(wxEVT_LEFT_DOWN, [](wxMouseEvent&)
            {
                play_dual_extruder_slice_video();
                wxGetApp().app_config->set("play_slicing_video", "false");
            });
        video_sizer->Add(video_link, 0, wxALIGN_CENTER | wxALL, FromDIP(3));
        button_sizer->Add(video_sizer, 0, wxLEFT, horizontal_margin);
        button_sizer->AddStretchSpacer();


        auto* wiki_sizer = new wxBoxSizer(wxHORIZONTAL);
        wiki_link = new wxStaticText(this, wxID_ANY, _L("Learn more"));
        wiki_link->SetBackgroundColour(BackGroundColor);
        wiki_link->SetForegroundColour(GreenColor);
        wiki_link->SetFont(Label::Body_12.Underlined());
        wiki_link->SetCursor(wxCursor(wxCURSOR_HAND));
        wiki_link->Bind(wxEVT_LEFT_DOWN, [](wxMouseEvent&) { open_filament_group_wiki(); });
        wiki_sizer->Add(wiki_link, 0, wxALIGN_CENTER | wxALL, FromDIP(3));

        button_sizer->Add(wiki_sizer, 0, wxLEFT, horizontal_margin);

        top_sizer->Add(button_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT, horizontal_margin);
    }

    top_sizer->AddSpacer(vertical_margin);
    SetSizer(top_sizer);
    Fit();

    m_timer = new wxTimer(this);

    GUI::wxGetApp().UpdateDarkUIWin(this);

    Bind(wxEVT_PAINT, &FilamentGroupPopup::OnPaint, this);
    Bind(wxEVT_TIMER, &FilamentGroupPopup::OnTimer, this);
    Bind(wxEVT_ENTER_WINDOW, &FilamentGroupPopup::OnEnterWindow, this);
    Bind(wxEVT_LEAVE_WINDOW, &FilamentGroupPopup::OnLeaveWindow, this);
}

FilamentGroupPopup::FilamentGroupPopup(wxWindow *parent, const std::vector<FilamentMapMode>& available_modes) : PopupWindow(parent, wxBORDER_NONE | wxPU_CONTAINS_CONTROLS)
{
    CreateBmps();
    RecreateUIElements();
    m_mode  = get_prefered_map_mode();
}

void FilamentGroupPopup::DrawRoundedCorner(int radius)
{
#ifdef __WIN32__
    HWND hwnd = GetHWND();
    if (hwnd) {
        HRGN hrgn = CreateRoundRectRgn(0, 0, GetRect().GetWidth(), GetRect().GetHeight(), radius, radius);
        SetWindowRgn(hwnd, hrgn, FALSE);

        SetWindowLong(hwnd, GWL_EXSTYLE, GetWindowLong(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
        SetLayeredWindowAttributes(hwnd, 0, 0, LWA_COLORKEY);
    }
#endif
}

void FilamentGroupPopup::Init(const std::vector<FilamentMapMode>& available_modes)
{
    static bool is_dark_mode = wxGetApp().dark_mode();
    if (is_dark_mode != wxGetApp().dark_mode()) {
        CreateBmps();
        is_dark_mode = wxGetApp().dark_mode();
    }

    const wxString AutoForMatchDesp = "";// _L("(Pre-slicing arrangement)");
    const wxString MachineSyncTip   = _L("(Sync with printer)");
    m_available_modes = available_modes.empty() ? m_all_modes : available_modes;

    // Update UI visibility based on available modes
    for (size_t i = 0; i < m_all_modes.size(); ++i) {
        FilamentMapMode mode = m_all_modes[i];
        bool is_available = (std::find(m_available_modes.begin(), m_available_modes.end(), mode) != m_available_modes.end());
        if (is_available) {
            // Show the mode
            button_sizers[i]->ShowItems(true);
            label_sizers[i]->ShowItems(true);
            button_sizers[i]->Show(true);
            label_sizers[i]->Show(true);
            mode_spacer[i]->Show(true);
        } else {
            // Hide the mode
            button_sizers[i]->ShowItems(false);
            label_sizers[i]->ShowItems(false);
            button_sizers[i]->Show(false);
            label_sizers[i]->Show(false);
            mode_spacer[i]->Show(false);
        }
    }

    // Recalculate layout and fit window to new size
    Layout();
    Fit();

    // Update button states based on connection status
    for (size_t i = 0; i < m_all_modes.size(); ++i) {
        FilamentMapMode mode = m_all_modes[i];
        bool is_available = (std::find(m_available_modes.begin(), m_available_modes.end(), mode) != m_available_modes.end());

        if (!is_available) {
            continue;
        }
        if (mode == fmmAutoForMatch) {
            if (m_connected) {
                button_labels[i]->SetForegroundColour(LabelEnableColor);
                button_desps[i]->SetForegroundColour(LabelEnableColor);
                detail_infos[i]->SetForegroundColour(GreyColor);
                radio_btns[i]->SetBitmap(unchecked_bmp);
                button_desps[i]->SetLabel(AutoForMatchDesp);
            }
            else {
                button_labels[i]->SetForegroundColour(LabelDisableColor);
                button_desps[i]->SetForegroundColour(LabelDisableColor);
                detail_infos[i]->SetForegroundColour(LabelDisableColor);
                radio_btns[i]->SetBitmap(disabled_bmp);
                button_desps[i]->SetLabel(MachineSyncTip);
            }
        }
    }

    m_mode = GetFilamentMapMode();
    if (m_mode == fmmAutoForMatch && !m_connected) {
        SetFilamentMapMode(fmmAutoForFlush);
        m_mode = fmmAutoForFlush;
    }
    else if (std::find(m_available_modes.begin(), m_available_modes.end(), m_mode) == m_available_modes.end()) {
        SetFilamentMapMode(fmmAutoForFlush);
        m_mode = fmmAutoForFlush;
    }
    else if (m_slice_all) {
        // reset the filament map mode in slice all mode
        SetFilamentMapMode(m_mode);
    }

    UpdateSmartFilamentSection();
    UpdateButtonStatus();
    GetSizer()->Layout();
    GetSizer()->SetSizeHints(this);
    GUI::wxGetApp().UpdateDarkUIWin(this);
}

void FilamentGroupPopup::tryPopup(Plater* plater,PartPlate* partplate,bool slice_all)
{
    if (should_pop_up()) {
        bool connect_status = plater->get_machine_sync_status();
        this->partplate_ref = partplate;
        this->plater_ref = plater;
        this->m_sync_plate = true;
        this->m_slice_all = slice_all;

        std::vector<FilamentMapMode> requested_modes = { fmmAutoForFlush, fmmAutoForMatch, fmmAutoForQuality };
        Print* print_obj = partplate ? partplate->fff_print() : nullptr;
        std::vector<FilamentMapMode> new_available_modes = resolve_available_auto_modes(print_obj, requested_modes, connect_status);

        if (wxGetApp().sidebar().is_fila_switch_ready()) {
            new_available_modes.erase(
                std::remove(new_available_modes.begin(), new_available_modes.end(), fmmAutoForMatch),
                new_available_modes.end()
            );
        }

        new_available_modes.push_back(fmmManual);

        // Check if available modes changed
        bool modes_changed = (m_available_modes != new_available_modes);
        m_available_modes = new_available_modes;

        if (m_active) {
            if (m_connected != connect_status || modes_changed) { Init(m_available_modes); }
            m_connected = connect_status;
            ResetTimer();
        }
        else {
            m_connected = connect_status;
            m_active = true;
            Init(m_available_modes);
            ResetTimer();
            DrawRoundedCorner(16);
            PopupWindow::Popup();
        }
    }
}

FilamentMapMode FilamentGroupPopup::GetFilamentMapMode() const
{
    const auto& proj_config = wxGetApp().preset_bundle->project_config;
    if (m_sync_plate)
        return partplate_ref->get_real_filament_map_mode(proj_config);

    return plater_ref->get_global_filament_map_mode();
}

void FilamentGroupPopup::SetFilamentMapMode(const FilamentMapMode mode)
{
    if (m_sync_plate) {
        if (m_slice_all) {
            auto plate_list = plater_ref->get_partplate_list().get_plate_list();
            for (int i = 0; i < plate_list.size(); ++i) {
                plate_list[i]->set_filament_map_mode(mode);
            }
        }
        else {
            partplate_ref->set_filament_map_mode(mode);
        }
        return;
    }
    plater_ref->set_global_filament_map_mode(mode);
}


void FilamentGroupPopup::tryClose() { StartTimer(); }

void FilamentGroupPopup::OnPaint(wxPaintEvent&)
{
    DrawRoundedCorner(16);
}

void FilamentGroupPopup::StartTimer() { m_timer->StartOnce(300); }

void FilamentGroupPopup::ResetTimer()
{
    if (m_timer->IsRunning()) { m_timer->Stop(); }
}

void FilamentGroupPopup::OnRadioBtn(int idx)
{
    if (idx < 0 || static_cast<size_t>(idx) >= m_all_modes.size())
        return;
        
    FilamentMapMode mode = m_all_modes.at(idx);
    
    // Check if this mode is available
    if (std::find(m_available_modes.begin(), m_available_modes.end(), mode) == m_available_modes.end())
        return;
    
    if (mode == FilamentMapMode::fmmAutoForMatch && !m_connected)
        return;
    if (m_mode != mode) {
        m_mode = mode;
        SetFilamentMapMode(m_mode);
        plater_ref->update();
        UpdateButtonStatus(idx);
    }
}

void FilamentGroupPopup::OnTimer(wxTimerEvent &event) { Dismiss(); }

void FilamentGroupPopup::Dismiss() {
    m_active = false;
    PopupWindow::Dismiss();
    m_timer->Stop();
}

void FilamentGroupPopup::OnLeaveWindow(wxMouseEvent &)
{
    wxPoint pos = this->ScreenToClient(wxGetMousePosition());
    if (this->GetClientRect().Contains(pos)) return;
    StartTimer();
}

void FilamentGroupPopup::OnEnterWindow(wxMouseEvent &) { ResetTimer(); }

void FilamentGroupPopup::UpdateButtonStatus(int hover_idx)
{
    for (size_t i = 0; i < m_all_modes.size(); ++i) {
        FilamentMapMode mode = m_all_modes[i];
        
        // Skip unavailable modes
        if (std::find(m_available_modes.begin(), m_available_modes.end(), mode) == m_available_modes.end())
            continue;
            
#if 0  // do not display global mode tag
        if (mode == global_mode)
            global_mode_tags[i]->Show();
        else
            global_mode_tags[i]->Hide();
#endif
        if (mode == fmmAutoForMatch && !m_connected) {
            button_labels[i]->SetFont(Label::Body_14);
            continue;
        }
        // process checked and unchecked status
        if (mode == m_mode) {
            if (static_cast<int>(i) == hover_idx)
                radio_btns[i]->SetBitmap(checked_hover_bmp);
            else
                radio_btns[i]->SetBitmap(checked_bmp);
            button_labels[i]->SetFont(Label::Head_14);
        } else {
            if (static_cast<int>(i) == hover_idx)
                radio_btns[i]->SetBitmap(unchecked_hover_bmp);
            else
                radio_btns[i]->SetBitmap(unchecked_bmp);
            button_labels[i]->SetFont(Label::Body_14);
        }
    }

    Layout();
    Fit();
}

void FilamentGroupPopup::MakeSmartFilamentSection(wxSizer *top_sizer, int horizontal_margin, int vertical_padding)
{
    m_smart_filament_panel = new StaticBox(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0);
    m_smart_filament_panel->SetCornerRadius(FromDIP(4));
    m_smart_filament_panel->SetBorderWidth(FromDIP(1));
    m_smart_filament_panel->SetBorderColor(wxColour("#CECECE"));
    m_smart_filament_panel->SetBackgroundColor(StateColor(std::pair<wxColour, int>(wxColour("#F8F8F8"), StateColor::Normal)));

    auto *label = new Label(m_smart_filament_panel, _L("Enable smart filament assign: Assign one filament to multiple nozzles to maximize savings"));
    label->SetFont(Label::Body_12);
    label->SetForegroundColour(GreyColor);
    label->SetBackgroundColour(wxColour("#F8F8F8"));
    label->Wrap(FromDIP(240));

    m_smart_filament_switch = new SwitchButton(m_smart_filament_panel);
    m_smart_filament_switch->Bind(wxEVT_TOGGLEBUTTON, &FilamentGroupPopup::OnSmartFilamentToggle, this);
#ifdef __WXOSX__
    // wxEVT_TOGGLEBUTTON event not handled well by PopupWindow on MacOS
    // we bind a wxEVT_LEFT_DOWN event as a workaround
    m_smart_filament_switch->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &) {
        wxCommandEvent evt(wxEVT_TOGGLEBUTTON);
        evt.SetInt(!m_smart_filament_switch->GetValue());
        m_smart_filament_switch->Command(evt);
    });
#endif

    auto *panel_sizer = new wxBoxSizer(wxHORIZONTAL);
    panel_sizer->Add(label, 1, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(10));
    panel_sizer->Add(m_smart_filament_switch, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10));
    m_smart_filament_panel->SetSizer(panel_sizer);

    top_sizer->Add(m_smart_filament_panel, 0, wxEXPAND | wxLEFT | wxRIGHT, horizontal_margin);
    m_smart_filament_spacer = top_sizer->AddSpacer(vertical_padding);

    // Hidden by default; shown in Init() when fila_switch_ready
    m_smart_filament_panel->Show(false);
    m_smart_filament_spacer->Show(false);
}

void FilamentGroupPopup::OnSmartFilamentToggle(wxCommandEvent &event)
{
    auto &config           = wxGetApp().preset_bundle->project_config;
    auto *dynamic_filament = dynamic_cast<ConfigOptionBool *>(config.option("enable_filament_dynamic_map"));
    if (dynamic_filament) { dynamic_filament->value = m_smart_filament_switch->GetValue(); }
    event.Skip();
}

void FilamentGroupPopup::UpdateSmartFilamentSection()
{
    bool show = wxGetApp().sidebar().is_fila_switch_ready();
    m_smart_filament_panel->Show(show);
    m_smart_filament_spacer->Show(show);

    if (show) {
        auto &config           = wxGetApp().preset_bundle->project_config;
        auto *dynamic_filament = dynamic_cast<ConfigOptionBool *>(config.option("enable_filament_dynamic_map"));
        if (dynamic_filament) { m_smart_filament_switch->SetValue(dynamic_filament->value); }
    }
}

}} // namespace Slic3r::GUI
