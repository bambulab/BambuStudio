#include "DeviceErrorDialog.hpp"
#include "HMS.hpp"

#include "DeviceManager.hpp"
#include "DeviceCore/DevManager.h"
#include "Widgets/Button.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "ReleaseNote.hpp"

#include <wx/modalhook.h>

namespace Slic3r {
namespace GUI
{

wxDEFINE_EVENT(EVT_ELEVATE_ERROR_DIALOG, wxCommandEvent);

// Detects when any OTHER dialog is about to enter its modal loop. A pre-existing
// non-modal error dialog would otherwise be disabled by that dialog's
// wxWindowDisabler (visible-but-dead). We post an event so the error dialog can
// re-enable itself and nest its own ShowModal() on top, staying both on-top and
// interactive across Win/macOS/GTK.
class ErrorDialogModalHook : public wxModalDialogHook
{
public:
    explicit ErrorDialogModalHook(DeviceErrorDialog* dlg) : m_dlg(dlg) {}

    int Enter(wxDialog* dialog) override
    {
        if (dialog != m_dlg && m_dlg->IsShown() && !m_dlg->IsModal() && !m_dlg->m_elevate_pending) {
            m_dlg->m_elevate_pending = true;
            wxCommandEvent event(EVT_ELEVATE_ERROR_DIALOG);
            wxPostEvent(m_dlg, event);
        }
        return wxID_NONE;
    }

private:
    DeviceErrorDialog* m_dlg{nullptr};
};

static std::unordered_set<std::string> message_containing_retry{
    "0701-8004",
    "0701-8005",
    "0701-8007",
    "0701-8012",
    "0702-8012",
    "0703-8012",
    "07FF-8012",
    "07FF-8013",
};


DeviceErrorDialog::DeviceErrorDialog(MachineObject* obj, wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style)
    : DPIDialog(parent, id, title, pos, size, style), m_dev_id(obj ? obj->get_dev_id() : std::string())
{
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));
    SetBackgroundColour(*wxWHITE);

    SetTitle(_L("Error"));

    auto        m_line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(350), 1));
    m_line_top->SetBackgroundColour(wxColour(166, 169, 170));

    m_scroll_area = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    m_scroll_area->SetScrollRate(0, 5);
    m_scroll_area->SetBackgroundColour(*wxWHITE);
    m_scroll_area->SetMinSize(wxSize(FromDIP(320), FromDIP(250)));

    wxBoxSizer* text_sizer = new wxBoxSizer(wxVERTICAL);

    m_error_msg_label = new Label(m_scroll_area, wxEmptyString, LB_AUTO_WRAP);
    m_error_picture = new wxStaticBitmap(m_scroll_area, wxID_ANY, wxBitmap(), wxDefaultPosition, wxSize(FromDIP(300), FromDIP(180)));

    //Label* dev_name = new Label(m_scroll_area, wxString::FromUTF8(obj->dev_name) + ":", LB_AUTO_WRAP);
    //dev_name->SetMaxSize(wxSize(FromDIP(300), -1));
    //dev_name->SetMinSize(wxSize(FromDIP(300), -1));
    //dev_name->Wrap(FromDIP(300));
    //text_sizer->Add(dev_name, 0, wxALIGN_CENTER, FromDIP(5));
    //text_sizer->AddSpacer(5);
    text_sizer->Add(m_error_picture, 0, wxALIGN_CENTER, FromDIP(5));
    text_sizer->AddSpacer(10);
    text_sizer->Add(m_error_msg_label, 0, wxALIGN_CENTER, FromDIP(5));

    m_error_code_label = new Label(m_scroll_area, wxEmptyString, LB_AUTO_WRAP);
    text_sizer->AddSpacer(5);
    text_sizer->Add(m_error_code_label, 0, wxALIGN_CENTER, FromDIP(5));
    m_scroll_area->SetSizer(text_sizer);

    auto bottom_sizer = new wxBoxSizer(wxVERTICAL);
    m_sizer_button = new wxBoxSizer(wxVERTICAL);
    bottom_sizer->Add(m_sizer_button, 0, wxEXPAND | wxRIGHT | wxLEFT, 0);

    wxBoxSizer* m_center_sizer = new wxBoxSizer(wxVERTICAL);
    m_center_sizer->Add(0, 0, 1, wxTOP, FromDIP(5));
    m_center_sizer->Add(m_scroll_area, 0, wxEXPAND | wxRIGHT | wxLEFT, FromDIP(15));
    m_center_sizer->Add(bottom_sizer, 0, wxEXPAND | wxRIGHT | wxLEFT, FromDIP(20));
    m_center_sizer->Add(0, 0, 0, wxTOP, FromDIP(10));

    m_sizer_main = new wxBoxSizer(wxVERTICAL);
    m_sizer_main->Add(m_line_top, 0, wxEXPAND, 0);
    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(5));
    m_sizer_main->Add(m_center_sizer, 0, wxBOTTOM | wxEXPAND, FromDIP(5));

    SetSizer(m_sizer_main);
    Layout();
    m_sizer_main->Fit(this);

    init_button_list();

    CenterOnParent();
    wxGetApp().UpdateDlgDarkUI(this);

    Bind(wxEVT_WEBREQUEST_STATE, &DeviceErrorDialog::on_webrequest_state, this);
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &e){
        if (!m_uiop_sent) {
            if (MachineObject* machine = get_machine_object()) {
                m_uiop_sent = true;
                machine->command_clean_print_error_uiop(m_error_code);
            }
        }
        if (!IsModal()) {
            Destroy();
            return;
        }
        e.Skip();
    });

    m_request_timer = new wxTimer(this);
    Bind(wxEVT_TIMER, &DeviceErrorDialog::on_request_timeout, this, m_request_timer->GetId());

    Bind(EVT_ELEVATE_ERROR_DIALOG, &DeviceErrorDialog::elevate_to_modal, this);
    m_modal_hook = new ErrorDialogModalHook(this);
    m_modal_hook->Register();
}

DeviceErrorDialog::~DeviceErrorDialog()
{
    if (m_modal_hook) {
        m_modal_hook->Unregister();
        delete m_modal_hook;
        m_modal_hook = nullptr;
    }

    if (m_request_timer) {
        m_request_timer->Stop();
        m_request_timer->Disconnect();
        delete m_request_timer;
        m_request_timer = nullptr;
    }

    if (web_request.IsOk() && web_request.GetState() == wxWebRequest::State_Active)
    {
        BOOST_LOG_TRIVIAL(info) << "web_request: cancelled";
        web_request.Cancel();
    }
    m_error_picture->SetBitmap(wxBitmap());
}

MachineObject* DeviceErrorDialog::get_machine_object() const
{
    if (m_dev_id.empty()) { return nullptr; }

    DeviceManager* dev_manager = wxGetApp().getDeviceManager();
    if (!dev_manager) { return nullptr; }
    if (MachineObject* obj = dev_manager->get_user_machine(m_dev_id)) { return obj; }
    return dev_manager->get_local_machine(m_dev_id);
}

void DeviceErrorDialog::on_request_timeout(wxTimerEvent& event)
{
    if (m_request_cancelled.load()) { return; }
    m_error_picture->SetBitmap(get_default_error_image());
    Layout();
    Fit();
}

void DeviceErrorDialog::on_webrequest_state(wxWebRequestEvent& evt)
{
    BOOST_LOG_TRIVIAL(trace) << "monitor: monitor_panel web request state = " << evt.GetState();

    m_request_cancelled.store(true);
    clear_request_timer();

    switch (evt.GetState())
    {
    case wxWebRequest::State_Completed:
    {
        wxImage img(*evt.GetResponse().GetStream());
        wxImage resize_img = img.Scale(FromDIP(320), FromDIP(180), wxIMAGE_QUALITY_HIGH);
        wxBitmap error_prompt_pic = resize_img;
        m_error_picture->SetBitmap(error_prompt_pic);
        Layout();
        Fit();

        break;
    }
    case wxWebRequest::State_Failed:
    case wxWebRequest::State_Cancelled:
    case wxWebRequest::State_Unauthorized:
    {
        m_error_picture->SetBitmap(wxBitmap());
        m_error_picture->Hide();
        Layout();
        Fit();
        break;
    }
    case wxWebRequest::State_Active:
    case wxWebRequest::State_Idle: break;
    default: break;
    }
}

void DeviceErrorDialog::init_button(ActionButton style, wxString buton_text)
{
    if (btn_bg_white.count() == 0)
    {
        btn_bg_white = StateColor(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed),
                                  std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
                                  std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));
    }

    Button* print_error_button = new Button(this, buton_text);
    print_error_button->SetBackgroundColor(btn_bg_white);
    print_error_button->SetBorderColor(wxColour(38, 46, 48));
    print_error_button->SetFont(Label::Body_14);
    print_error_button->SetSize(wxSize(FromDIP(300), FromDIP(30)));
    print_error_button->SetMinSize(wxSize(FromDIP(300), FromDIP(30)));
    print_error_button->SetMaxSize(wxSize(-1, FromDIP(30)));
    print_error_button->SetCornerRadius(FromDIP(5));
    print_error_button->Hide();
    m_button_list[style] = print_error_button;
    m_button_list[style]->Bind(wxEVT_LEFT_DOWN, [this, style](wxMouseEvent& e)
        {
            this->on_button_click(style);
            e.Skip();
        });
}

void DeviceErrorDialog::init_button_list()
{
    init_button(RESUME_PRINTING, _L("Resume Printing"));
    init_button(RESUME_PRINTING_DEFECTS, _L("Resume (defects acceptable)"));
    init_button(RESUME_PRINTING_PROBELM_SOLVED, _L("Resume (problem solved)"));
    init_button(STOP_PRINTING, _L("Stop Printing"));// pop up recheck dialog?
    init_button(CHECK_ASSISTANT, _L("Check Assistant"));
    init_button(FILAMENT_EXTRUDED, _L("Filament Extruded, Continue"));
    init_button(RETRY_FILAMENT_EXTRUDED, _L("Not Extruded Yet, Retry"));
    init_button(CONTINUE, _L("Finished, Continue"));
    init_button(LOAD_VIRTUAL_TRAY, _L("Load Filament"));
    init_button(OK_BUTTON, _L("OK"));
    init_button(FILAMENT_LOAD_RESUME, _L("Filament Loaded, Resume"));
    init_button(JUMP_TO_LIVEVIEW, _L("View Liveview"));
    init_button(NO_REMINDER_NEXT_TIME, _L("No Reminder Next Time"));
    init_button(REFRESH_NOZZLE, _L("Recheck"));
    init_button(IGNORE_NO_REMINDER_NEXT_TIME, _L("Ignore. Don't Remind Next Time"));
    init_button(IGNORE_RESUME, _L("Ignore this and Resume"));
    init_button(PROBLEM_SOLVED_RESUME, _L("Problem Solved and Resume"));
    init_button(TURN_OFF_FIRE_ALARM, _L("Got it, Turn off the Fire Alarm."));
    init_button(RETRY_PROBLEM_SOLVED, _L("Retry (problem solved)"));
    init_button(CANCLE, _L("Cancle"));
    init_button(STOP_DRYING, _L("Stop Drying"));
    init_button(PROCEED, _L("Proceed"));
    init_button(OK_JUMP_RACK, "OK");
    init_button(ABORT, _L("Abort"));
    init_button(DISABLE_PURIFICATION, _L("Disable Purification for This Print"));
    init_button(DONT_REMIND_NEXT_TIME, _L("Don't Remind Me"));

    init_button(DBL_CHECK_CANCEL, _L("Cancel"));
    init_button(DBL_CHECK_DONE, _L("Done"));
    init_button(DBL_CHECK_RETRY, _L("Retry"));
    init_button(DBL_CHECK_RESUME, _L("Resume"));
    init_button(DBL_CHECK_OK, _L("Confirm"));
}

void DeviceErrorDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    for (auto used_button : m_used_button) { used_button->Rescale();}
    wxGetApp().UpdateDlgDarkUI(this);
    Refresh();
}

wxString DeviceErrorDialog::parse_error_level(int error_code)
{
    int level = (error_code & 0x0000F000) >> 12;
    switch (level) {
    case 0x4: return _L("Error");
    case 0x8: return _L("Warning");
    case 0xC: return _L("Info");
    default: return _L("Unknown");
    }
}

static const std::unordered_set<string> s_jump_liveview_error_codes = { "0300-8003", "0300-8002", "0300-800A"};

void DeviceErrorDialog::apply_result(const HMSResult& r)
{
    const std::string error_str   = MachineObject::get_error_code_str(m_error_code);
    const wxString    error_level = parse_error_level(m_error_code);

    wxString error_msg = r.text;
    if (r.status == HMSStatus::Loading)      error_msg = _L("Loading error details ...");
    else if (r.status == HMSStatus::Failed)  error_msg = _L("Unable to load error details.");
    else if (error_msg.IsEmpty())            error_msg = _L("Unknown error.");

    if (message_containing_retry.count(error_str)) {
        std::vector<int> pseudo_button = convert_to_pseudo_buttons(error_str);
        update_contents(error_level, error_msg, error_str, wxEmptyString, pseudo_button);
    } else {
        std::vector<int> used_button;
        wxString         error_image_url;
        if (r.status == HMSStatus::Ready) {
            HMSResult action    = wxGetApp().get_hms_query_mgr()->query_action(m_dev_id, m_error_code);
            used_button         = action.actions;
            error_image_url     = action.image_url;
        }
        if (s_jump_liveview_error_codes.count(error_str)) { used_button.emplace_back(DeviceErrorDialog::JUMP_TO_LIVEVIEW); } // special case
        update_contents(error_level, error_msg, error_str, error_image_url, used_button);
    }

    wxGetApp().UpdateDlgDarkUI(this);
}

void DeviceErrorDialog::apply_loading()
{
    const std::string error_str = MachineObject::get_error_code_str(m_error_code);
    const wxString    show_time = wxDateTime::Now().Format("%H%M%d");

    m_error_code_label->SetMaxSize(wxSize(FromDIP(300), -1));
    m_error_code_label->SetMinSize(wxSize(FromDIP(300), -1));
    m_error_code_label->SetLabelText(wxString::Format("[%S %S]", wxString::FromUTF8(error_str), show_time));

    m_error_msg_label->SetMaxSize(wxSize(FromDIP(300), -1));
    m_error_msg_label->SetMinSize(wxSize(FromDIP(300), -1));
    m_error_msg_label->SetLabelText(_L("Loading error details ..."));

    SetTitle(parse_error_level(m_error_code));

    const MachineObject* obj           = get_machine_object();
    const bool           reserve_image = obj && !obj->m_print_error_img_id.empty();
    if (m_error_picture) {
        if (reserve_image) {
            m_error_picture->SetBitmap(get_default_loading_image());
            m_error_picture->Show();
        } else {
            m_error_picture->Hide();
        }
    }

    m_scroll_area->Layout();
    const int text_h = m_error_msg_label->GetBestSize().y;
    if (text_h < FromDIP(360)) {
        const int extra = reserve_image ? FromDIP(220) : FromDIP(50);
        m_scroll_area->SetMinSize(wxSize(FromDIP(320), text_h + extra));
    } else {
        m_scroll_area->SetMinSize(wxSize(FromDIP(320), FromDIP(340)));
    }

    Layout();
    Fit();
}

void DeviceErrorDialog::handle_hms_result(const HMSResult& r)
{
    if (IsBeingDeleted()) { return; }
    if (r.status == HMSStatus::Ready && r.is_internal) { Close(); return; }

    if (r.status == HMSStatus::Loading)
        apply_loading();
    else
        apply_result(r);

    show_error_dialog();
}

static bool is_other_modal_dialog_shown(const DeviceErrorDialog *error_dialog)
{
    for (wxWindowList::compatibility_iterator node = wxTopLevelWindows.GetFirst(); node != nullptr; node = node->GetNext()) {
        auto *dialog = dynamic_cast<wxDialog *>(node->GetData());
        if (dialog != nullptr && dialog != error_dialog && dialog->IsShown() && dialog->IsModal()) { return true; }
    }

    return false;
}

void DeviceErrorDialog::show_error_dialog()
{
    if (IsModal()) {
        Raise();
        return;
    }

#ifdef __WXOSX__
    SetWindowStyleFlag(GetWindowStyleFlag() | wxSTAY_ON_TOP);
#endif

    // Another modal dialog already owns the event loop: showing non-modally now
    // would leave us disabled (visible-but-dead), so become the top nested modal.
    if (is_other_modal_dialog_shown(this)) {
        Enable(true);
        Raise();
        ShowModal();
        Destroy();
        return;
    }

    Show();
    Raise();
    this->RequestUserAttention(wxUSER_ATTENTION_ERROR);
}

void DeviceErrorDialog::elevate_to_modal(wxCommandEvent& event)
{
    m_elevate_pending = false;

    if (!IsShown() || IsModal()) { return; }

    // The other dialog's ShowModal() disabled us via wxWindowDisabler; re-enable,
    // rise above it, and take over as the active nested modal so we stay closable.
    Enable(true);
    Raise();
    ShowModal();
    Destroy();
}

wxString DeviceErrorDialog::show_error_code(int error_code)
{
    if (m_error_code == error_code) { return wxEmptyString; }
    m_error_code = error_code;
    m_uiop_sent = false;

    HMSResult r = wxGetApp().get_hms_query_mgr()->query_error(
        m_dev_id, error_code, [this](const HMSResult& r) { handle_hms_result(r); }, m_hms_sub);

    // fail-open: suppress only a KNOWN internal error (Ready). m_hms_sub is empty here, so
    // nothing is subscribed and the dialog is never shown.
    if (r.status == HMSStatus::Ready && r.is_internal) { return wxEmptyString; }

    handle_hms_result(r);
    return r.text;
}

std::vector<int> DeviceErrorDialog::convert_to_pseudo_buttons(std::string error_str)
{
    std::vector<int> pseudo_button;

    pseudo_button.emplace_back(DBL_CHECK_RETRY);
    pseudo_button.emplace_back(DBL_CHECK_OK);

    return pseudo_button;
}

void DeviceErrorDialog::clear_request_timer()
{
    if (m_request_timer && m_request_timer->IsRunning()) {
        m_request_timer->Stop();
    }
}

wxBitmap DeviceErrorDialog::get_default_loading_image()
{
    const int w = FromDIP(320);
    const int h = FromDIP(180);

    wxBitmap bmp(wxSize(w, h));
    wxMemoryDC dc(bmp);
    dc.SetBackground(wxBrush(wxColour(238, 238, 238))); // gray300
    dc.Clear();

    ScalableBitmap icon = ScalableBitmap(this, "dev_hms_diag_loading", 80);
    wxBitmap icon_bmp = icon.bmp();
    if (icon_bmp.IsOk()) {
        int ix = (w - icon_bmp.GetWidth()) / 2;
        int iy = (h - icon_bmp.GetHeight()) / 3;
        dc.DrawBitmap(icon_bmp, ix, iy, true);
    }

    dc.SetTextForeground(wxColour(158, 158, 158)); // gray500
    dc.SetFont(::Label::Body_14);
    const wxString txt = _L("Loading ...");
    wxSize txtSize = dc.GetTextExtent(txt);
    int tx = (w - txtSize.GetWidth()) / 2;
    int ty = h - txtSize.GetHeight() - FromDIP(45);
    dc.DrawText(txt, tx, ty);

    dc.SelectObject(wxNullBitmap);
    return bmp;
}

wxBitmap DeviceErrorDialog::get_default_error_image()
{
    const int w = FromDIP(320);
    const int h = FromDIP(180);

    wxBitmap bmp(wxSize(w, h));
    wxMemoryDC dc(bmp);
    dc.SetBackground(wxBrush(wxColour(238, 238, 238))); // gray300
    dc.Clear();

    ScalableBitmap icon = ScalableBitmap(this, "dev_hms_diag_loading", 80);
    wxBitmap icon_bmp = icon.bmp();
    if (icon_bmp.IsOk()) {
        int ix = (w - icon_bmp.GetWidth()) / 2;
        int iy = (h - icon_bmp.GetHeight()) / 3;
        dc.DrawBitmap(icon_bmp, ix, iy, true);
    }

    dc.SetTextForeground(wxColour(158, 158, 158)); // gray500
    dc.SetFont(::Label::Body_14);
    const wxString txt = _L("Network unavailable");
    wxSize txtSize = dc.GetTextExtent(txt);
    int tx = (w - txtSize.GetWidth()) / 2;
    int ty = h - txtSize.GetHeight() - FromDIP(45);
    dc.DrawText(txt, tx, ty);

    dc.SelectObject(wxNullBitmap);
    return bmp;
}

bool DeviceErrorDialog::get_fail_snapshot_from_cloud()
{
    const MachineObject* obj = get_machine_object();
    if (!obj || obj->m_print_error_img_id.empty()) { return false; }

    NetworkAgent* agent = GUI::wxGetApp().getAgent();
    if (!agent) { return false; }

    int ret = agent->get_hms_snapshot(m_dev_id, obj->m_print_error_img_id,
    [this](std::string body, int status) {
        if (status == 200) {
            wxMemoryInputStream stream(body.data(), body.size());
            wxImage             success_image;
            if (success_image.LoadFile(stream, wxBITMAP_TYPE_ANY)) {
                CallAfter([this, success_image]() {
                    this->m_request_cancelled.store(true);
                    clear_request_timer();
                    wxImage resize_img = success_image.Scale(FromDIP(320), FromDIP(180), wxIMAGE_QUALITY_HIGH);
                    wxBitmap error_prompt_pic = resize_img;
                    m_error_picture->SetBitmap(error_prompt_pic);
                    Layout();
                    Fit();
                });
            } else {
                BOOST_LOG_TRIVIAL(error) << "get_fail_snapshot_from_cloud: failed to resolve stream";
                CallAfter([this]() {
                    if (!this->get_fail_snapshot_from_local(this->m_local_img_url)) {
                        m_error_picture->SetBitmap(this->get_default_error_image());
                        Layout();
                    }
                });
            }
        } else {
            BOOST_LOG_TRIVIAL(error) << "get_fail_snapshot_from_cloud: status = " << status;
            CallAfter([this]() {
                if (!this->get_fail_snapshot_from_local(this->m_local_img_url)) {
                    m_error_picture->SetBitmap(this->get_default_error_image());
                    Layout();
                }
            });
        }
    });

    return ret == 0;
}

bool DeviceErrorDialog::get_fail_snapshot_from_local(const wxString& image_url)
{
    if (image_url.empty()) {
        return false;
    }

    const wxImage& img = wxGetApp().get_hms_query_mgr()->query_image_from_local(image_url);
    if (!img.IsOk() && image_url.Contains("http"))
    {
        web_request = wxWebSession::GetDefault().CreateRequest(this, image_url);
        BOOST_LOG_TRIVIAL(trace) << "monitor: create new webrequest, state = " << web_request.GetState();
        if (web_request.GetState() == wxWebRequest::State_Idle) {
            web_request.Start();
        }
        BOOST_LOG_TRIVIAL(trace) << "monitor: start new webrequest, state = " << web_request.GetState();
    }
    else
    {
        m_request_cancelled.store(true);
        clear_request_timer();
        const wxImage& resize_img = img.Scale(FromDIP(320), FromDIP(180), wxIMAGE_QUALITY_HIGH);
        m_error_picture->SetBitmap(wxBitmap(resize_img));
    }

    return true;
}

void DeviceErrorDialog::update_contents(const wxString& title, const wxString& text, const wxString& error_code, const wxString& image_url, const std::vector<int>& btns)
{
    if (error_code.empty()) { return; }

    /* buttons*/
    {
        m_sizer_button->Clear();
        m_used_button.clear();

        // Show the used buttons
        bool need_remove_close_btn = false;
        std::unordered_set<int> shown_btns;
        for (int button_id : btns)
        {
            need_remove_close_btn |= (button_id == REMOVE_CLOSE_BTN); // special case, do not show close button

            auto iter = m_button_list.find(button_id);
            if (iter != m_button_list.end())
            {
                m_sizer_button->Add(iter->second, 0, wxALL, FromDIP(5));
                iter->second->Show();
                m_used_button.insert(iter->second);
            }
        }

        // Special case, do not show close button
        if (need_remove_close_btn)
        {
            SetWindowStyle(GetWindowStyle() & ~wxCLOSE_BOX);
        }
        else
        {
            SetWindowStyle(GetWindowStyle() | wxCLOSE_BOX);
        }

        // Hide unused buttons
        for (const auto& pair : m_button_list)
        {
            if (m_used_button.count(pair.second) == 0) { pair.second->Hide(); }
        }
    }

    /* image */
    m_local_img_url = image_url;
    m_error_picture->SetBitmap(get_default_loading_image());
    if (m_request_timer->IsRunning()) {
        m_request_timer->Stop();
    }
    m_request_timer->StartOnce(10000);
    if (get_fail_snapshot_from_cloud())
    {
        m_error_picture->Show();
    }
    else if (get_fail_snapshot_from_local(image_url))
    {
        m_error_picture->Show();
    }
    else
    {
        m_error_picture->Hide();
    }

    /* error code*/
    const wxString& show_time = wxDateTime::Now().Format("%H%M%d");
    const wxString& error_code_msg = wxString::Format("[%S %S]", error_code, show_time);
    m_error_code_label->SetMaxSize(wxSize(FromDIP(300), -1));
    m_error_code_label->SetMinSize(wxSize(FromDIP(300), -1));
    m_error_code_label->SetLabelText(error_code_msg);

    /* error message*/
    m_error_msg_label->SetMaxSize(wxSize(FromDIP(300), -1));
    m_error_msg_label->SetMinSize(wxSize(FromDIP(300), -1));
    m_error_msg_label->SetLabelText(text);

    /* dialog title*/
    SetTitle(title);

    /* update layout*/
    {
        const MachineObject* obj = get_machine_object();

        m_scroll_area->Layout();
        auto text_size = m_error_msg_label->GetBestSize();
        if (text_size.y < FromDIP(360))
        {
            if (!image_url.empty() || (obj && !obj->m_print_error_img_id.empty()))
            {
                m_scroll_area->SetMinSize(wxSize(FromDIP(320), text_size.y + FromDIP(220)));
            }
            else
            {
                m_scroll_area->SetMinSize(wxSize(FromDIP(320), text_size.y + FromDIP(50)));
            }
        }
        else
        {
            m_scroll_area->SetMinSize(wxSize(FromDIP(320), FromDIP(340)));
        }

        Layout();
        Fit();
    }
};

void DeviceErrorDialog::on_button_click(ActionButton btn_id)
{
    // Resolved once per click: the machine may already be gone, in which case the
    // printer commands below are silently dropped while navigation still works.
    MachineObject* obj = get_machine_object();
    if (!obj) {
        BOOST_LOG_TRIVIAL(warning) << "DeviceErrorDialog: machine is gone, drop action " << btn_id;
    }

    switch (btn_id) {
    case DeviceErrorDialog::RESUME_PRINTING: {
        if (obj) { obj->command_hms_resume(std::to_string(m_error_code), obj->job_id_); }
        break;
    }
    case DeviceErrorDialog::RESUME_PRINTING_DEFECTS: {
        if (obj) { obj->command_hms_resume(std::to_string(m_error_code), obj->job_id_); }
        break;
    }
    case DeviceErrorDialog::RESUME_PRINTING_PROBELM_SOLVED: {
        if (obj) { obj->command_hms_resume(std::to_string(m_error_code), obj->job_id_); }
        break;
    }
    case DeviceErrorDialog::STOP_PRINTING: {
        if (obj) { obj->command_hms_stop(std::to_string(m_error_code), obj->job_id_); }
        break;
    }
    case DeviceErrorDialog::CHECK_ASSISTANT: {
        wxGetApp().mainframe->m_monitor->jump_to_HMS(); // go to assistant page
        break;
    }
    case DeviceErrorDialog::FILAMENT_EXTRUDED: {
        if (obj) { obj->command_ams_control("done"); }
        break;
    }
    case DeviceErrorDialog::RETRY_FILAMENT_EXTRUDED: {
        if (obj) { obj->command_ams_control("resume"); }
        break;
    }
    case DeviceErrorDialog::CONTINUE: {
        if (obj) { obj->command_ams_control("resume"); }
        break;
    }
    case DeviceErrorDialog::LOAD_VIRTUAL_TRAY: {
        //m_ams_control->SwitchAms(std::to_string(VIRTUAL_TRAY_MAIN_ID));
        //on_ams_load_curr();
        break;/*AP, unknown what it is*/
    }
    case DeviceErrorDialog::OK_BUTTON: {
        if (obj) { obj->command_clean_print_error(obj->subtask_id_, m_error_code); }
        break;/*do nothing*/
    }
    case DeviceErrorDialog::FILAMENT_LOAD_RESUME: {
        if (obj) { obj->command_hms_resume(std::to_string(m_error_code), obj->job_id_); }
        break;
    }
    case DeviceErrorDialog::JUMP_TO_LIVEVIEW: {
        Slic3r::GUI::wxGetApp().mainframe->jump_to_monitor();
        Slic3r::GUI::wxGetApp().mainframe->m_monitor->jump_to_LiveView();
        break;
    }
    case DeviceErrorDialog::NO_REMINDER_NEXT_TIME: {
        if (obj) { obj->command_hms_idle_ignore(std::to_string(m_error_code), 0); } /*the type is 0, supported by AP*/
        break;
    }
    case DeviceErrorDialog::REFRESH_NOZZLE: {
        if (obj) { obj->command_refresh_nozzle(); }
        break;
    }
    case DeviceErrorDialog::IGNORE_NO_REMINDER_NEXT_TIME: {
        if (obj) { obj->command_hms_ignore(std::to_string(m_error_code), obj->job_id_); }
        break;
    }
    case DeviceErrorDialog::IGNORE_RESUME: {
        if (obj) { obj->command_hms_ignore(std::to_string(m_error_code), obj->job_id_); }
        break;
    }
    case DeviceErrorDialog::PROBLEM_SOLVED_RESUME: {
        if (obj) { obj->command_hms_resume(std::to_string(m_error_code), obj->job_id_); }
        break;
    }
    case DeviceErrorDialog::TURN_OFF_FIRE_ALARM: {
        if (obj) { obj->command_stop_buzzer(); }
        break;
    }
    case DeviceErrorDialog::RETRY_PROBLEM_SOLVED: {
        if (obj) { obj->command_ams_control("resume"); }
        break;
    }
    case DeviceErrorDialog::CANCLE: {
        break;
    }
    case DeviceErrorDialog::STOP_DRYING: {
        if (obj) { obj->command_ams_drying_stop(); }
        break;
    }
    case DeviceErrorDialog::PROCEED: {
        if (obj && !m_action_json.is_null()){
            try{
                obj->command_ack_proceed(m_action_json);
            } catch(...){
                BOOST_LOG_TRIVIAL(error) << "DeviceErrorDialog: Action Proceed missing params.";
            }
        }
        break;
    }
    case DeviceErrorDialog::OK_JUMP_RACK:
    {
        Slic3r::GUI::wxGetApp().mainframe->jump_to_monitor();
        Slic3r::GUI::wxGetApp().mainframe->m_monitor->jump_to_Rack();
        break;
    }
    case DeviceErrorDialog::ABORT:
    {
        if (obj) { obj->command_ams_control("abort"); }
        break;
    }

    case DeviceErrorDialog::DISABLE_PURIFICATION:
    {
        if (obj) { obj->command_purification_disable(); }
        break;
    }

    case DeviceErrorDialog::DONT_REMIND_NEXT_TIME:
    {
        if (obj && !m_action_json.is_null()){
            obj->command_dont_remind_next_time(m_action_json);
        }
        break;
    }

    case DeviceErrorDialog::DBL_CHECK_CANCEL: {
        // post EVT_SECONDARY_CHECK_CANCEL
        // no event
        break;
    }
    case DeviceErrorDialog::DBL_CHECK_DONE: {
        // post EVT_SECONDARY_CHECK_DONE
        if (obj) { obj->command_ams_control("done"); }
        break;
    }
    case DeviceErrorDialog::DBL_CHECK_RETRY: {
        // post EVT_SECONDARY_CHECK_RETRY
        wxCommandEvent event(EVT_SECONDARY_CHECK_RETRY);
        wxPostEvent(GetParent(), event);
        break;
    }
    case DeviceErrorDialog::DBL_CHECK_RESUME: {
        // post EVT_SECONDARY_CHECK_RESUME
        wxCommandEvent event(EVT_SECONDARY_CHECK_RESUME);
        wxPostEvent(GetParent(), event);
        break;
    }
    case DeviceErrorDialog::DBL_CHECK_OK: {
        // post EVT_SECONDARY_CHECK_CONFIRM
        if (obj) { obj->command_clean_print_error(obj->subtask_id_, m_error_code); }
        break;
    }

    default: break;
    }

    CallAfter([this]() { Close(); });
}

}
} // namespace Slic3r::GUI
