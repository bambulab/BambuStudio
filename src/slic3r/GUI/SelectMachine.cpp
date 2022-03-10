#include "SelectMachine.hpp"
#include "I18N.hpp"

#include "libslic3r/Utils.hpp"
#include "libslic3r/Thread.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "format.hpp"
#include "Widgets/ProgressDialog.hpp"

#include <wx/progdlg.h>
#include <wx/clipbrd.h>
#include "Widgets/Label.hpp"

#include <algorithm>

namespace Slic3r { namespace GUI {

#define INITIAL_NUMBER_OF_MACHINES 0
#define LIST_REFRESH_INTERVAL 3000
#define MACHINE_LIST_REFRESH_INTERVAL 100

MachineListModel::MachineListModel() : wxDataViewVirtualListModel(INITIAL_NUMBER_OF_MACHINES) { ; }

void MachineListModel::display_machines(std::map<std::string, MachineObject *> list)
{
    for (int i = 0; i < Col_Max; i++) { m_values[i].clear(); }

    std::vector<MachineObject *>                     list_array;
    std::map<std::string, MachineObject *>::iterator it;
    for (it = list.begin(); it != list.end(); it++) { list_array.push_back(it->second); }

    std::sort(list_array.begin(), list_array.end(), [](MachineObject *obj1, MachineObject *obj2) { return obj1->dev_name < obj2->dev_name; });

    std::vector<MachineObject *>::iterator iter;
    for (iter = list_array.begin(); iter != list_array.end(); iter++) { this->add_machine(*iter, false); }
    Reset(list_array.size());
}

void MachineListModel::add_machine(MachineObject *obj, bool reset)
{
    m_values[Col_MachineName].Add(from_u8(obj->dev_name));
    m_values[Col_MachineSN].Add(from_u8(obj->dev_id));
    m_values[Col_MachinePrintingStatus].Add(from_u8(obj->iot_task_status));
    m_values[Col_MachineTaskName].Add(from_u8(obj->iot_printing_taskname));
    m_values[Col_MachineIPAddress].Add(from_u8(obj->dev_ip));
    m_values[Col_MachineConnection].Add(obj->is_online ? _L("Online") : _L("Offline"));
    if (reset) Reset(m_values[Col_MachineName].GetCount());
}

int MachineListModel::find_row_by_sn(wxString sn)
{
    wxVariant val;
    for (int i = 0; i < this->GetCount(); i++) {
        GetValueByRow(val, i, Col_MachineSN);
        if (val == sn) { return i; }
    }

    return -1;
}

void MachineListModel::GetValueByRow(wxVariant &variant, unsigned int row, unsigned int col) const
{
    if (row > m_values[col].GetCount())
        variant = wxString::Format("virtual row %d", row);
    else
        variant = m_values[col][row];
}

bool MachineListModel::GetAttrByRow(unsigned int row, unsigned int col, wxDataViewItemAttr &attr) const { return true; }

bool MachineListModel::SetValueByRow(const wxVariant &variant, unsigned int row, unsigned int col)
{
    if (row >= m_values[col].GetCount()) return false;
    m_values[col][row] = variant.GetString();
    return true;
}

MachineObjectPanel::MachineObjectPanel(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size, long style, const wxString &name)
    : wxPanel(parent, id, pos, wxSize(parent->GetSize().GetWidth(), 8 * wxGetApp().em_unit()), style, name)
{
    m_text_color   = wxColour(38, 46, 48);
    m_bg_colour    = wxColour(255, 255, 255);
    m_hover_colour = wxColour(248, 248, 248);
    m_leftdown_colour = wxColour(238, 238, 238);
    Bind(wxEVT_PAINT, &MachineObjectPanel::OnPaint, this);

    SetBackgroundColour(m_bg_colour);

    // ams_placeholder_img = create_scaled_bitmap("machine_object_ams", nullptr, 27);
    m_printing_img = create_scaled_bitmap("machine_object_printing", nullptr, 12);
    m_owner_img    = create_scaled_bitmap("machine_object_owner", nullptr, 10);

    this->Bind(wxEVT_ENTER_WINDOW, &MachineObjectPanel::on_mouse_enter, this);

    this->Bind(wxEVT_LEAVE_WINDOW, &MachineObjectPanel::on_mouse_leave, this);

    this->Bind(wxEVT_LEFT_DOWN, &MachineObjectPanel::on_mouse_left_down, this);
    this->Bind(wxEVT_LEFT_UP, &MachineObjectPanel::on_mouse_left_up, this);
}

void MachineObjectPanel::DrawTextString(wxDC &dc, const wxString &text, const wxPoint &pt, bool bold)
{
    // dc.SetFont(wxGetApp().small_font());
    if (bold) {
        dc.SetFont(Label::Head_14);
    } else {
        dc.SetFont(Label::Body_13);
    }

    dc.SetBackgroundMode(wxTRANSPARENT);
    dc.SetTextForeground(m_text_color);
    /// dc.SetTextBackground(*wxWHITE);
    dc.DrawText(text, pt);
}

MachineObjectPanel::~MachineObjectPanel() {}

void MachineObjectPanel::OnPaint(wxPaintEvent &event)
{
    wxPaintDC dc(this);
    auto      top  = 5;
    auto      left = 0;

    // line 1
    dc.DrawBitmap(m_bitmap_type, wxGetApp().em_unit() / 2, top, false);
    left = m_bitmap_type.GetWidth() + wxGetApp().em_unit() + wxGetApp().em_unit() / 2;

    DrawTextString(dc, m_printer_name, wxPoint(left, top), true);
    top += 2 * wxGetApp().em_unit() + wxGetApp().em_unit() / 2;

    // line2
    dc.DrawBitmap(m_printing_img, left, top + wxGetApp().em_unit() / 3, false);
    DrawTextString(dc, m_printer_time, wxPoint(left + m_printing_img.GetWidth() + wxGetApp().em_unit(), top + wxGetApp().em_unit() / 4));
    top += 2 * wxGetApp().em_unit() + wxGetApp().em_unit() / 2;

    // line3
    dc.DrawBitmap(m_owner_img, left, top + wxGetApp().em_unit() / 3, false);
    DrawTextString(dc, m_printer_task, wxPoint(left + m_printing_img.GetWidth() + wxGetApp().em_unit(), top + wxGetApp().em_unit() / 4));
    // DrawTextString(dc, wxT("-1h3m, 49%"), wxPoint(20, 22));
    // DrawTextString(dc, wxT("Stone Li Stone Li Stone Li Stone Li..."), wxPoint(20, 42));
}

void MachineObjectPanel::update_machine_info(std::string dev_id, wxString dev_name, int progress, wxString owner)
{
    m_dev_id = dev_id;

    m_bitmap_type = create_scaled_bitmap("machine_obejct_type", nullptr, 18);

    m_printer_name = wxString::Format("%s", dev_name);
    m_printer_time = wxString::Format("%d%% (SN: %s)", progress, dev_id);
    m_printer_task = wxString::Format("%s", owner);
}

void MachineObjectPanel::on_mouse_enter(wxMouseEvent &evt)
{
    this->SetBackgroundColour(m_hover_colour);
    Refresh();
}

void MachineObjectPanel::on_mouse_leave(wxMouseEvent &evt)
{
    this->SetBackgroundColour(m_bg_colour);
    Refresh();
}

void MachineObjectPanel::on_mouse_left_down(wxMouseEvent &evt)
{
    this->SetBackgroundColour(m_leftdown_colour);
    Refresh();
}

void MachineObjectPanel::on_mouse_left_up(wxMouseEvent &evt)
{
    /* set monitor page to current device */
    wxGetApp().mainframe->jump_to_monitor(m_dev_id);
}

wxIMPLEMENT_CLASS(SelectMachinePopup, wxPopupTransientWindow);

wxBEGIN_EVENT_TABLE(SelectMachinePopup, wxPopupTransientWindow) EVT_MOUSE_EVENTS(SelectMachinePopup::OnMouse) EVT_SIZE(SelectMachinePopup::OnSize)
    EVT_SET_FOCUS(SelectMachinePopup::OnSetFocus) EVT_KILL_FOCUS(SelectMachinePopup::OnKillFocus) wxEND_EVENT_TABLE()

        SelectMachinePopup::SelectMachinePopup(wxWindow *parent)
    : wxPopupTransientWindow(parent, wxBORDER_NONE | wxPU_CONTAINS_CONTROLS)
{
#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif //__WINDOWS__

    m_bg_colour    = wxColour(255, 255, 255);
    m_hover_colour = wxColour(61, 70, 72);
    m_bold_colour  = wxColour(38, 46, 48);
    m_thumb_Ccolor = wxColour(196, 196, 196);

    SetBackgroundColour(wxColour(238, 238, 238));
    SetSizeHints(wxDefaultSize, wxDefaultSize);

    m_sizer_border = new wxBoxSizer(wxVERTICAL);
    m_sizer_main = new wxBoxSizer(wxVERTICAL);
    m_sizer_body = new wxBoxSizer(wxVERTICAL);

    //border
    m_border_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(POPUP_WIDTH * wxGetApp().em_unit(), POPUP_HEIGHT * wxGetApp().em_unit()), wxTAB_TRAVERSAL);
    m_border_panel->SetBackgroundColour(m_bg_colour);


    //client
    m_client_panel = new wxPanel(m_border_panel, wxID_ANY, wxDefaultPosition, wxSize(POPUP_WIDTH * wxGetApp().em_unit(), POPUP_HEIGHT * wxGetApp().em_unit()), wxTAB_TRAVERSAL);
    m_client_panel->SetBackgroundColour(m_bg_colour);

    // title info
     m_staticText_select = new wxStaticText(m_client_panel, wxID_ANY, wxT("Select Your Printer"), wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
     m_staticText_select->SetForegroundColour(m_bold_colour);
     m_staticText_select->SetFont(wxGetApp().bold_font());
     m_staticText_select->Wrap(-1);

    // block line
     m_block_line = new wxWindow(m_client_panel, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
     m_block_line->SetForegroundColour(wxColour(115, 115, 115));
     m_block_line->SetBackgroundColour(wxColour(115, 115, 115));

     auto block_top = new wxWindow(m_client_panel, wxID_ANY, wxDefaultPosition, wxSize(0,0));

     m_sizer_body->Add(m_staticText_select, 0, wxEXPAND | wxTOP, wxGetApp().em_unit() + wxGetApp().em_unit() / 2);
     m_sizer_body->Add(block_top, 0, wxEXPAND | wxTOP, wxGetApp().em_unit());
     m_sizer_body->Add(m_block_line, 0, wxEXPAND | wxRIGHT, wxGetApp().em_unit() + wxGetApp().em_unit() / 2);


     m_client_panel->SetSizer(m_sizer_body);
     m_client_panel->Layout();
     m_sizer_body->Fit(m_client_panel);
     m_sizer_main->Add(m_client_panel, 1, wxEXPAND | wxLEFT, 13);

    m_border_panel->SetSizer(m_sizer_main);
    m_border_panel->Layout();
    m_sizer_border->Add(m_border_panel, 0, wxEXPAND | wxALL, 1);

    SetSizer(m_sizer_border);
    Layout();
    m_sizer_border->Fit(this);


    //#ifdef __WXMAC__
    //    // On Mac, pop up window capture mouse events
    //    m_scrolledWindow->GetPanel()->Bind(wxEVT_LEFT_UP, &SelectMachinePopup::OnLeftUp, this);
    //#endif

    m_refresh_timer = new wxTimer();
    Bind(wxEVT_TIMER, &SelectMachinePopup::on_timer, this);
}

void SelectMachinePopup::Popup(wxWindow *WXUNUSED(focus))
{
    m_refresh_timer->Stop();
    m_refresh_timer->SetOwner(this);
    m_refresh_timer->Start(MACHINE_LIST_REFRESH_INTERVAL);
    wxPostEvent(this, wxTimerEvent());
    wxPopupTransientWindow::Popup();
}

void SelectMachinePopup::OnDismiss()
{
    if (m_refresh_timer) {
        m_refresh_timer->Stop();
        delete m_refresh_timer;
        m_refresh_timer = nullptr;
    }
    wxPopupTransientWindow::OnDismiss();
}

bool SelectMachinePopup::ProcessLeftDown(wxMouseEvent &event) { return wxPopupTransientWindow::ProcessLeftDown(event); }
bool SelectMachinePopup::Show(bool show)
{
    if (show) {
        /* create thread to get print info */

        get_print_info_thread = Slic3r::create_thread([this] {
            Slic3r::AccountManager *c = Slic3r::GUI::wxGetApp().getAccountManager();
            int                     err_code;
            std::string             err_msg;
            c->update_my_machine_list_info(err_code, err_msg);
            this->update = true;
        });
    } else {
        get_print_info_thread.interrupt();
        if (get_print_info_thread.joinable()) get_print_info_thread.join();
    }

    return wxPopupTransientWindow::Show(show);
}

void SelectMachinePopup::update_machine_list(std::vector<MachineObject *> obj_list)
{
    Freeze();
    m_obj_list = obj_list;

    // scroll window
    m_scrolledWindow = new ScrolledWindow(m_client_panel, wxID_ANY, wxDefaultPosition, wxSize(m_client_panel->GetSize().GetWidth(), 42 * wxGetApp().em_unit()), wxVSCROLL, 6, 6);
    m_scrolledWindow->SetMarginColor(m_bg_colour);
    m_scrolledWindow->SetScrollbarColor(m_thumb_Ccolor);
    m_scrolledWindow->SetBackgroundColour(m_bg_colour);

    auto listsizer = new wxBoxSizer(wxVERTICAL);
    auto list      = new wxWindow(m_scrolledWindow->GetPanel(), -1);
    list->SetBackgroundColour(m_bg_colour);
    list->SetSize(wxSize(m_scrolledWindow->GetSize().GetWidth(), -1));

    for (auto i = 0; i < m_obj_list.size(); i++) {
        MachineObjectPanel *op = new MachineObjectPanel(list, wxID_ANY);
        obj_panels.push_back(op);
        op->update_machine_info(m_obj_list[i]->dev_id, from_u8(m_obj_list[i]->dev_name), m_obj_list[i]->mc_print_percent, from_u8(m_obj_list[i]->iot_printing_taskname));

        auto block_line = new wxWindow(list, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
        block_line->SetForegroundColour(wxColour(206, 206, 206));
        block_line->SetBackgroundColour(wxColour(206, 206, 206));

        auto block_top = new wxWindow(list, wxID_ANY, wxDefaultPosition, wxSize(0,0));
  
        listsizer->Add(op, 0, wxEXPAND, 0);
        listsizer->Add(block_top, 0, wxEXPAND | wxTOP, wxGetApp().em_unit() / 3);
        listsizer->Add(block_line, 0, wxEXPAND | wxRIGHT, wxGetApp().em_unit() + wxGetApp().em_unit() / 2);

        op->Bind(wxEVT_LEFT_UP, &SelectMachinePopup::OnLeftUp, this);
    }

    list->SetSizer(listsizer);
    list->Fit();
    m_scrolledWindow->SetScrollbars(1, 1, 0, list->GetSize().GetHeight());

    m_sizer_body->Add(m_scrolledWindow, 0, wxEXPAND | wxTOP, 5);
    m_sizer_body->Fit(m_client_panel);
    m_sizer_body->Layout();
    Thaw();
}

void SelectMachinePopup::OnSize(wxSizeEvent &event) { event.Skip(); }

void SelectMachinePopup::OnSetFocus(wxFocusEvent &event) { event.Skip(); }

void SelectMachinePopup::OnKillFocus(wxFocusEvent &event) { event.Skip(); }

void SelectMachinePopup::OnMouse(wxMouseEvent &event) { event.Skip(); }

void SelectMachinePopup::OnLeftUp(wxMouseEvent &event) {
    this->GetParent()->SetFocus();
    event.Skip();
}

void SelectMachinePopup::on_timer(wxTimerEvent &event)
{
    if (update) {
        Slic3r::AccountManager *     c = Slic3r::GUI::wxGetApp().getAccountManager();
        std::vector<MachineObject *> show_list;
        for (auto &elem : c->myBindMachineList) { show_list.push_back(elem.second); }
        update_machine_list(show_list);
        update = false;
    }
}

SelectMachineDialog::SelectMachineDialog(Plater *plater)
    : DPIDialog(static_cast<wxWindow *>(wxGetApp().mainframe), wxID_ANY, _L("Send Task to"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , m_plater(plater)
    , m_export_3mf_cancel(false)
{
    /* auto created by wxFormBuilder */
    this->SetSizeHints(wxSize(550, 480), wxSize(1920, 1280));

    wxBoxSizer *bSizer;
    bSizer = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer *bSizer_machines;
    bSizer_machines = new wxBoxSizer(wxVERTICAL);

    bSizer_machines->SetMinSize(wxSize(-1, 300));
    m_dataViewListCtrl_machines = new wxDataViewCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0);
    bSizer_machines->Add(m_dataViewListCtrl_machines, 1, wxALL | wxEXPAND, 5);

    bSizer->Add(bSizer_machines, 1, wxALL | wxEXPAND, 5);

    wxBoxSizer *bSizer_tips;
    bSizer_tips = new wxBoxSizer(wxHORIZONTAL);

    m_staticText_left = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_left->Wrap(-1);
    bSizer_tips->Add(m_staticText_left, 0, wxALIGN_CENTER | wxALL, 5);

    bSizer_tips->Add(0, 0, 1, wxEXPAND, 5);

    m_hyperlink_add_machine = new wxHyperlinkCtrl(this, wxID_ANY, wxT("how to add a printer?"), wxT("http://www.wxformbuilder.org"), wxDefaultPosition, wxDefaultSize,
                                                  wxHL_DEFAULT_STYLE);
    bSizer_tips->Add(m_hyperlink_add_machine, 0, wxALIGN_CENTER | wxALIGN_RIGHT | wxALL | wxRIGHT, 5);

    bSizer->Add(bSizer_tips, 0, wxEXPAND, 5);

    bSizer->Add(0, 10, 0, wxEXPAND, 5);

    m_panel_status = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    bSizer->Add(m_panel_status, 0, wxEXPAND | wxALL, 5);

    /* add BBLStatusBar */
    m_status_bar   = std::make_shared<BBLStatusBar>(this);
    m_panel_status = m_status_bar->get_panel();
    bSizer->Add(m_panel_status, 0, wxEXPAND | wxALL, 0);

    bSizer->Add(0, 10, 0, wxEXPAND, 5);

    wxBoxSizer *bSizer_buttons;
    bSizer_buttons = new wxBoxSizer(wxHORIZONTAL);

    bSizer_buttons->Add(0, 0, 1, wxEXPAND, 5);

    m_button_ensure = new wxButton(this, wxID_ANY, wxT("OK"), wxDefaultPosition, wxDefaultSize, 0);
    bSizer_buttons->Add(m_button_ensure, 0, wxALIGN_CENTER | wxALL, 5);

    bSizer->Add(bSizer_buttons, 0, wxEXPAND, 5);

    this->SetSizer(bSizer);
    this->Layout();

    this->Centre(wxBOTH);

    this->SetSizeHints(wxSize(550, 480), wxSize(1920, 1280));

    // Connect Events
    m_button_ensure->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(SelectMachineDialog::on_ok), NULL, this);

    init_model();
    init_bind();
    init_timer();

    Fit();
    CenterOnParent();
}

void SelectMachineDialog::init_model()
{
    machine_model = new MachineListModel;
    m_dataViewListCtrl_machines->AssociateModel(machine_model.get());
    m_dataViewListCtrl_machines->AppendTextColumn("Printer Name", MachineListModel::Col_MachineName, wxDATAVIEW_CELL_INERT, wxCOL_WIDTH_AUTOSIZE, wxALIGN_NOT,
                                                  wxDATAVIEW_COL_SORTABLE);

    m_dataViewListCtrl_machines->AppendTextColumn("SN(dev_id)", MachineListModel::Col_MachineSN, wxDATAVIEW_CELL_INERT, wxCOL_WIDTH_AUTOSIZE, wxALIGN_NOT,
                                                  wxDATAVIEW_COL_RESIZABLE);

    m_dataViewListCtrl_machines->AppendTextColumn("Status", MachineListModel::Col_MachinePrintingStatus, wxDATAVIEW_CELL_INERT, wxCOL_WIDTH_AUTOSIZE, wxALIGN_NOT,
                                                  wxDATAVIEW_COL_RESIZABLE);

    m_dataViewListCtrl_machines->AppendTextColumn("TaskName", MachineListModel::Col_MachineTaskName, wxDATAVIEW_CELL_INERT, wxCOL_WIDTH_AUTOSIZE, wxALIGN_NOT,
                                                  wxDATAVIEW_COL_RESIZABLE);

    m_dataViewListCtrl_machines->AppendTextColumn("Connection", MachineListModel::Col_MachineConnection, wxDATAVIEW_CELL_INERT, wxCOL_WIDTH_AUTOSIZE, wxALIGN_NOT,
                                                  wxDATAVIEW_COL_RESIZABLE);
}

void SelectMachineDialog::init_bind()
{
    Bind(wxEVT_TIMER, &SelectMachineDialog::on_timer, this);
    m_dataViewListCtrl_machines->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &SelectMachineDialog::on_selection_changed, this);
}

void SelectMachineDialog::init_timer()
{
    m_refresh_timer = new wxTimer();
    m_refresh_timer->SetOwner(this);
}

void SelectMachineDialog::on_cancel(wxCommandEvent &event)
{
    if (m_print_job) {
        if (m_print_job->is_running()) m_print_job->cancel();
        m_print_job->join();
    }
    this->EndModal(wxID_CANCEL);
}

void SelectMachineDialog::reset()
{
    m_status_bar->set_status_text("");
    m_button_ensure->Enable();
}

void SelectMachineDialog::on_ok(wxCommandEvent &event)
{
    wxDataViewItem item = m_dataViewListCtrl_machines->GetSelection();
    wxVariant      val;
    machine_model->GetValue(val, item, MachineListModel::Col_MachineSN);
    std::string dev_id = val.GetString().ToStdString();

    Slic3r::AccountManager *                         c  = Slic3r::GUI::wxGetApp().getAccountManager();
    std::map<std::string, MachineObject *>::iterator it = c->myBindMachineList.find(dev_id);
    if (it == c->myBindMachineList.end()) {
        wxString msg = _L("Please select a printer first!");
        m_status_bar->set_status_text(msg);
        return;
    }

    // TODO check printing status
    /* if (!it->second->can_print()) {
        m_status_bar->set_status_text("current printer is busy! please select another!");
        return;
    }
    */

#ifdef BBL_CHECK_USER_REPORT
    int  task_id   = 0;
    bool printable = true;
    c->user_check_report(&task_id, &printable);
    if (task_id != 0 && !printable) {
        m_status_bar->set_status_text(_L("Please fill report first!"));
        std::string report_url = (boost::format("https://autotest.bambu-lab.com/slicerAddReport?task_id=%1%&token=%2%") % task_id % c->get_curr_user()->m_autotest_token).str();
        wxLaunchDefaultBrowser(report_url);
        return;
    }
#endif

    m_need_disable_btn_ensure = true;
    m_button_ensure->Disable();

    ProgressDialog *progress_dlg = new ProgressDialog("Creating 3mf file", "", 100, this, wxPD_AUTO_HIDE | wxPD_CAN_ABORT | wxPD_APP_MODAL);

    m_plater->send_gcode(m_print_plate_idx, [this, progress_dlg](int export_stage, int current, int total, bool &cancel) {
        bool     cont             = true;
        wxString msg              = wxString::Format("exporting 3mf stage %d, %d/%d", export_stage, current, total);
        cont                      = progress_dlg->Pulse(msg);
        this->m_export_3mf_cancel = cancel = !cont;
    });
    delete progress_dlg;
    if (this->m_export_3mf_cancel) {
        this->m_status_bar->set_status_text("exporting 3mf was cancelled");
        return;
    }

    m_status_bar->set_cancel_callback([this]() {
        if (m_print_job->is_running()) m_print_job->cancel();
    });

    m_print_job = std::make_shared<PrintJob>(m_status_bar, m_plater, dev_id);
    m_print_job->start();
}

void SelectMachineDialog::on_timer(wxTimerEvent &event)
{
    if (m_print_job && m_need_disable_btn_ensure && m_print_job->is_finalized() && !m_print_job->is_finished()) {
        m_button_ensure->Enable();
        m_need_disable_btn_ensure = false;
    }

    // update machine list, collections of bind list and local free
    Slic3r::AccountManager *               c = Slic3r::GUI::wxGetApp().getAccountManager();
    std::map<std::string, MachineObject *> list;

    if (c->is_user_login()) {
        boost::thread get_print_info_thread = Slic3r::create_thread([this] {
            Slic3r::AccountManager *acc = Slic3r::GUI::wxGetApp().getAccountManager();
            int                     err_code;
            std::string             err_msg;
            acc->update_my_machine_list_info(err_code, err_msg, true);
        });
    }

    // same machine only appear once
    std::map<std::string, MachineObject *>::iterator it;
    for (it = c->myBindMachineList.begin(); it != c->myBindMachineList.end(); it++) {
        if (it->second && it->second->is_online) { list.insert(std::make_pair(it->first, it->second)); }
    }

    machine_model->display_machines(list);

    // select old items
    wxVariant val;
    int       row = machine_model->find_row_by_sn(machine_sn);
    if (row >= 0) {
        wxDataViewItem item = machine_model->GetItem(row);
        m_dataViewListCtrl_machines->Select(item);
    }
}

void SelectMachineDialog::on_selection_changed(wxDataViewEvent &event)
{
    Slic3r::AccountManager *c    = Slic3r::GUI::wxGetApp().getAccountManager();
    wxDataViewItem          item = event.GetItem();
    wxVariant               val;
    machine_model->GetValue(val, item, MachineListModel::Col_MachineSN);
    machine_sn         = val.GetString();
    c->default_machine = machine_sn.ToStdString();
}

void SelectMachineDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    Fit();
    Refresh();
}

bool SelectMachineDialog::Show(bool show)
{
    if (show) {
        m_refresh_timer->Start(LIST_REFRESH_INTERVAL);
        wxPostEvent(this, wxTimerEvent());
    } else {
        m_refresh_timer->Stop();
    }
    return DPIDialog::Show(show);
}

SelectMachineDialog::~SelectMachineDialog() { m_button_ensure->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(SelectMachineDialog::on_ok), NULL, this); }

}} // namespace Slic3r::GUI
