#include "SelectMachine.hpp"
#include "I18N.hpp"

#include "libslic3r/Utils.hpp"
#include "libslic3r/Thread.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "GUI_Preview.hpp"
#include "MainFrame.hpp"
#include "format.hpp"
#include "Widgets/ProgressDialog.hpp"
#include "Widgets/RoundedRectangle.hpp"
#include "Widgets/StaticBox.hpp"

#include <wx/progdlg.h>
#include <wx/clipbrd.h>
#include <wx/dcgraph.h>
#include <miniz.h>
#include <algorithm>
#include "Plater.hpp"
#include "BitmapCache.hpp"
#include "BindDialog.hpp"

namespace Slic3r { namespace GUI {

wxDEFINE_EVENT(EVT_FINISHED_UPDATE_MACHINE_LIST, wxCommandEvent);
wxDEFINE_EVENT(EVT_REQUEST_BIND_LIST, wxCommandEvent);
wxDEFINE_EVENT(EVT_PRINT_JOB_CANCEL, wxCommandEvent);

#define INITIAL_NUMBER_OF_MACHINES 0
#define LIST_REFRESH_INTERVAL 3000
#define MACHINE_LIST_REFRESH_INTERVAL 2000

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
    m_values[Col_MachinePrintingStatus].Add(from_u8(obj->print_status));
    m_values[Col_MachineIPAddress].Add(from_u8(obj->dev_ip));
    m_values[Col_MachineConnection].Add(obj->is_online() ? _L("Online") : _L("Offline"));
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
{
    wxPanel::Create(parent, id, pos, SELECT_MACHINE_ITEM_SIZE, style, name);
    Bind(wxEVT_PAINT, &MachineObjectPanel::OnPaint, this);

    SetBackgroundColour(*wxWHITE);

    // ams_placeholder_img = create_scaled_bitmap("machine_object_ams", nullptr, 27);
    m_printing_img = create_scaled_bitmap("machine_object_printing", nullptr, 12);
    m_owner_img    = create_scaled_bitmap("machine_object_owner", nullptr, 10);

    m_unbind_img        = create_scaled_bitmap("unbind", nullptr, 18);
    m_select_unbind_img = create_scaled_bitmap("unbind_selected", nullptr, 18);
    /*m_wifi_none_img     = create_scaled_bitmap("monitor_signal_no", nullptr, 18);
    m_wifi_weak_img     = create_scaled_bitmap("monitor_signal_weak", nullptr, 18);
    m_wifi_middle_img   = create_scaled_bitmap("monitor_signal_middle", nullptr, 18);
    m_wifi_strong_img   = create_scaled_bitmap("monitor_signal_strong", nullptr, 18);*/

    m_printer_statue_offline = create_scaled_bitmap("printer_statue_offline", nullptr, 15);
    m_printer_statue_busy = create_scaled_bitmap("printer_statue_busy", nullptr, 15);
    m_printer_statue_idle = create_scaled_bitmap("printer_statue_idle", nullptr, 15);

    this->Bind(wxEVT_ENTER_WINDOW, &MachineObjectPanel::on_mouse_enter, this);
    this->Bind(wxEVT_LEAVE_WINDOW, &MachineObjectPanel::on_mouse_leave, this);
    this->Bind(wxEVT_LEFT_DOWN, &MachineObjectPanel::on_mouse_left_down, this);
    this->Bind(wxEVT_LEFT_UP, &MachineObjectPanel::on_mouse_left_up, this);
}

MachineObjectPanel::~MachineObjectPanel() {}

void MachineObjectPanel::show_unbind_dialog()
{
    // if (m_info->can_abort()) return;
    UnBindMachineDilaog dlg;
    dlg.update_machine_info(m_info);
    switch (dlg.ShowModal()) {
    case wxID_YES: {
        break;
    }

    case wxID_NO: {
        break;
    }

    default:;
    }
}

void MachineObjectPanel::show_bind_dialog()
{
    // if (m_info->can_abort()) return;
    BindMachineDilaog dlg;
    dlg.update_machine_info(m_info);
    switch (dlg.ShowModal()) {
    case wxID_YES: {
        break;
    }

    case wxID_NO: {
        break;
    }

    default:;
    }
}

void MachineObjectPanel::set_printer_idle()
{
    m_state = PrinterState::IDLE;
    Refresh();
}

void MachineObjectPanel::set_printer_busy()
{
    m_state = PrinterState::BUSY;
    Refresh();
}

void MachineObjectPanel::set_printer_offline() 
{
    m_state = PrinterState::OFFLINE;
    Refresh();
}

void MachineObjectPanel::set_printer_unbind()
{
    m_showunbind = true;
    Refresh();
}

// bbs 0-nowifi 1-weak 2-middle 3-strong
void MachineObjectPanel::set_printer_wifi()
{
    m_showunbind         = false;
    auto wifi_signal     = m_info->wifi_signal;
    int  wifi_signal_val = 0;
    if (!wifi_signal.empty() && boost::ends_with(wifi_signal, "dBm")) {
        try {
            wifi_signal_val = std::stoi(wifi_signal.substr(0, wifi_signal.size() - 3));
        } catch (...) {
            ;
        }

        if (m_last_wifi_signal != wifi_signal_val) {
            if (wifi_signal_val > -45) {
                m_wifi_type = 3;
            } else if (wifi_signal_val <= -45 && wifi_signal_val >= -60) {
                m_wifi_type = 2;
            } else {
                m_wifi_type = 1;
            }
        }
        m_last_wifi_signal = wifi_signal_val;
    } else {
        m_wifi_type = 1;
    }

    Refresh();
}

void MachineObjectPanel::set_can_bind(bool canbind) { m_state_can_bind = canbind; }

void MachineObjectPanel::OnPaint(wxPaintEvent &event)
{
    wxPaintDC dc(this);
    render(dc);
}

void MachineObjectPanel::render(wxDC &dc)
{
#ifdef __WXMSW__
    wxSize     size = GetSize();
    wxMemoryDC memdc;
    wxBitmap   bmp(size.x, size.y);
    memdc.SelectObject(bmp);
    memdc.Blit({0, 0}, size, &dc, {0, 0});

    {
        wxGCDC dc2(memdc);
        doRender(dc2);
    }

    memdc.SelectObject(wxNullBitmap);
    dc.DrawBitmap(bmp, 0, 0);
#else
    doRender(dc);
#endif
}

void MachineObjectPanel::doRender(wxDC &dc)
{
    auto   left = 10;
    wxSize size = GetSize();
    dc.SetPen(*wxTRANSPARENT_PEN);

    auto dwbitmap = m_printer_statue_offline;
    if (m_state == PrinterState::IDLE) {dwbitmap = m_printer_statue_idle;}
    if (m_state == PrinterState::BUSY) { dwbitmap = m_printer_statue_busy; }
    if (m_state == PrinterState::OFFLINE) { dwbitmap = m_printer_statue_offline; }

    //dc.DrawCircle(left, size.y / 2, 3);
    dc.DrawBitmap(dwbitmap, wxPoint(left, (size.y - dwbitmap.GetSize().y) / 2));

    left += dwbitmap.GetSize().x + 11;
    dc.SetFont(Label::Body_14);
    dc.SetBackgroundMode(wxTRANSPARENT);
    dc.SetTextForeground(SELECT_MACHINE_GREY900);
    auto sizet = dc.GetTextExtent(m_info->dev_name);
    dc.DrawText(m_info->dev_name, wxPoint(left, (size.y - sizet.y) / 2));

    left = size.x - m_unbind_img.GetSize().x - 15;
    if (m_showunbind) {
        if (!m_select_unbind) {
            dc.DrawBitmap(m_unbind_img, left, (size.y - m_unbind_img.GetSize().y) / 2);
        } else {
            dc.DrawBitmap(m_select_unbind_img, left, (size.y - m_unbind_img.GetSize().y) / 2);
        }

    } else {
        /*if (m_wifi_type == 0) dc.DrawBitmap(m_wifi_none_img, left, (size.y - m_unbind_img.GetSize().y) / 2);
        if (m_wifi_type == 1) dc.DrawBitmap(m_wifi_weak_img, left, (size.y - m_unbind_img.GetSize().y) / 2);
        if (m_wifi_type == 2) dc.DrawBitmap(m_wifi_middle_img, left, (size.y - m_unbind_img.GetSize().y) / 2);
        if (m_wifi_type == 3) dc.DrawBitmap(m_wifi_strong_img, left, (size.y - m_unbind_img.GetSize().y) / 2);*/
    }

    if (m_hover) {
        dc.SetPen(SELECT_MACHINE_BRAND);
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRectangle(0, 0, size.x, size.y);
    }
}

void MachineObjectPanel::update_machine_info(/*std::string dev_id, wxString dev_name, int progress, wxString owner*/ MachineObject *info)
{
    m_info = info;
    //m_info->can_abort() ? set_printer_busy() : set_printer_idle();

    set_printer_wifi();
    Refresh();
}

void MachineObjectPanel::on_mouse_enter(wxMouseEvent &evt)
{
    m_hover = true;
    /* if (!m_info->can_abort()) {
         set_printer_unbind();
     } else {
         set_printer_wifi();
     }*/
    if (!m_state_can_bind) {
        set_printer_unbind();
    } else {
        set_printer_wifi();
    }
    Refresh();
}

void MachineObjectPanel::on_mouse_leave(wxMouseEvent &evt)
{
    m_hover = false;
    set_printer_wifi();
    Refresh();
}

void MachineObjectPanel::on_mouse_left_down(wxMouseEvent &evt)
{
    auto left   = GetSize().x - m_unbind_img.GetSize().x - 15;
    auto right  = left + m_unbind_img.GetSize().x;
    auto top    = (GetSize().y - m_unbind_img.GetSize().y) / 2;
    auto bottom = (GetSize().y - m_unbind_img.GetSize().y) / 2 + m_unbind_img.GetSize().y;
    if ((evt.GetPosition().x >= left && evt.GetPosition().x <= right) && evt.GetPosition().y >= top && evt.GetPosition().y <= bottom) { m_select_unbind = true; }
    Refresh();
    
    /* set monitor page to current device */
    if (m_state_can_bind) {
        show_bind_dialog();
    } else {
        auto left   = GetSize().x - m_unbind_img.GetSize().x - 15;
        auto right  = left + m_unbind_img.GetSize().x;
        auto top    = (GetSize().y - m_unbind_img.GetSize().y) / 2;
        auto bottom = (GetSize().y - m_unbind_img.GetSize().y) / 2 + m_unbind_img.GetSize().y;

        left -= 20;
        right += 50;
        top -= 20;
        bottom += 20;

        if ((evt.GetPosition().x >= left && evt.GetPosition().x <= right) && evt.GetPosition().y >= top && evt.GetPosition().y <= bottom) {
            show_unbind_dialog();
        } else {
            m_select_unbind = false;
            wxGetApp().mainframe->jump_to_monitor(m_info->dev_id);
            wxGetApp().mainframe->SetFocus();
        }
    }
    
    evt.Skip();
}

void MachineObjectPanel::on_mouse_left_up(wxMouseEvent &evt)
{
    evt.Skip();
}

wxIMPLEMENT_CLASS(SelectMachinePopup, wxPopupTransientWindow);

wxBEGIN_EVENT_TABLE(SelectMachinePopup, wxPopupTransientWindow) EVT_MOUSE_EVENTS(SelectMachinePopup::OnMouse) EVT_SIZE(SelectMachinePopup::OnSize)
    EVT_SET_FOCUS(SelectMachinePopup::OnSetFocus) EVT_KILL_FOCUS(SelectMachinePopup::OnKillFocus) wxEND_EVENT_TABLE()

        SelectMachinePopup::SelectMachinePopup(wxWindow *parent)
    : wxPopupTransientWindow(parent, wxBORDER_NONE | wxPU_CONTAINS_CONTROLS),
    m_dismiss(false)
{
#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif //__WINDOWS__

    Freeze();
    wxBoxSizer *m_sizer_main = new wxBoxSizer(wxVERTICAL);
    SetBackgroundColour(SELECT_MACHINE_GREY400);

    m_panel_body = new wxPanel(this, wxID_ANY, wxDefaultPosition, SELECT_MACHINE_POPUP_SIZE, wxTAB_TRAVERSAL);
    m_panel_body->SetBackgroundColour(*wxWHITE);

    wxBoxSizer *m_sizer_body = new wxBoxSizer(wxVERTICAL);

    m_scrolledWindow = new wxScrolledWindow(m_panel_body, wxID_ANY, wxDefaultPosition, SELECT_MACHINE_LIST_SIZE, wxHSCROLL | wxVSCROLL);
    m_scrolledWindow->SetScrollRate(0, 5);
    auto m_sizxer_scrolledWindow = new wxBoxSizer(wxVERTICAL);
    m_scrolledWindow->SetSizer(m_sizxer_scrolledWindow);
    m_scrolledWindow->Layout();
    m_sizxer_scrolledWindow->Fit(m_scrolledWindow);

    auto own_title        = create_title_panel(_L("My Device"));
    m_sizer_my_devices    = new wxBoxSizer(wxVERTICAL);
    auto other_title      = create_title_panel(_L("Other Device"));
    m_sizer_other_devices = new wxBoxSizer(wxVERTICAL);

    m_sizxer_scrolledWindow->Add(own_title, 0, wxEXPAND, 0);
    m_sizxer_scrolledWindow->Add(m_sizer_my_devices, 0, wxEXPAND, 0);
    m_sizxer_scrolledWindow->Add(other_title, 0, wxEXPAND, 0);
    m_sizxer_scrolledWindow->Add(m_sizer_other_devices, 0, wxEXPAND, 0);

    m_sizer_body->Add(m_scrolledWindow, 1, wxEXPAND | wxLEFT | wxTOP | wxBOTTOM, 10);
    m_panel_body->SetSizer(m_sizer_body);
    m_panel_body->Layout();
    m_sizer_main->Add(m_panel_body, 0, wxALL | wxEXPAND, 1);

    this->SetSizer(m_sizer_main);
    this->Layout();
    m_sizer_main->Fit(this);
    Thaw();

    //#ifdef __WXMAC__
    //    // On Mac, pop up window capture mouse events
    //    m_scrolledWindow->GetPanel()->Bind(wxEVT_LEFT_UP, &SelectMachinePopup::OnLeftUp, this);
    //#endif

    m_refresh_timer = new wxTimer();
    Bind(EVT_FINISHED_UPDATE_MACHINE_LIST, &SelectMachinePopup::update_machine_list, this);
    Bind(EVT_REQUEST_BIND_LIST, &SelectMachinePopup::update_other_devices, this);
    Bind(wxEVT_TIMER, &SelectMachinePopup::on_timer, this);
}

void SelectMachinePopup::Popup(wxWindow *WXUNUSED(focus))
{
    start_ssdp();
    m_refresh_timer->Stop();
    m_refresh_timer->SetOwner(this);
    m_refresh_timer->Start(MACHINE_LIST_REFRESH_INTERVAL);
    
    get_print_info_thread = Slic3r::create_thread([this] {
        DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
        dev->update_user_machine_list_info();
        
        if (!was_dismiss()) {
            wxCommandEvent event(EVT_FINISHED_UPDATE_MACHINE_LIST);
            event.SetEventObject(this);
            wxPostEvent(this, event);
        }
    });
    
    wxPostEvent(this, wxTimerEvent());
    wxPopupTransientWindow::Popup();
}

void SelectMachinePopup::OnDismiss()
{
    m_dismiss = true;

    if (m_refresh_timer) {
        m_refresh_timer->Stop();
        delete m_refresh_timer;
        m_refresh_timer = nullptr;
    }

    Slic3r::create_thread([this] {
        stop_ssdp();
        get_print_info_thread.interrupt();
        if (get_print_info_thread.joinable()) {
            get_print_info_thread.join();
        }
    });
}

bool SelectMachinePopup::ProcessLeftDown(wxMouseEvent &event) {
    return wxPopupTransientWindow::ProcessLeftDown(event);
}

bool SelectMachinePopup::Show(bool show) { return wxPopupTransientWindow::Show(show); }

wxWindow *SelectMachinePopup::create_title_panel(wxString text)
{
    auto m_panel_title_own = new wxWindow(m_scrolledWindow, wxID_ANY, wxDefaultPosition, SELECT_MACHINE_ITEM_SIZE, wxTAB_TRAVERSAL);
    m_panel_title_own->SetBackgroundColour(*wxWHITE);

    wxBoxSizer *m_sizer_title_own = new wxBoxSizer(wxHORIZONTAL);

    auto m_title_own = new wxStaticText(m_panel_title_own, wxID_ANY, text, wxDefaultPosition, wxDefaultSize, 0);
    m_title_own->Wrap(-1);
    m_sizer_title_own->Add(m_title_own, 0, wxALIGN_CENTER, 0);

    wxBoxSizer *m_sizer_line_own = new wxBoxSizer(wxHORIZONTAL);

    auto m_panel_line_own = new wxPanel(m_panel_title_own, wxID_ANY, wxDefaultPosition, wxSize(SELECT_MACHINE_ITEM_SIZE.x, FromDIP(1)), wxTAB_TRAVERSAL);
    m_panel_line_own->SetBackgroundColour(SELECT_MACHINE_GREY400);

    m_sizer_line_own->Add(m_panel_line_own, 0, wxALIGN_CENTER, 0);
    m_sizer_title_own->Add(0, 0, 0, wxLEFT, FromDIP(10));
    m_sizer_title_own->Add(m_sizer_line_own, 1, wxEXPAND | wxRIGHT, FromDIP(10));

    m_panel_title_own->SetSizer(m_sizer_title_own);
    m_panel_title_own->Layout();
    return m_panel_title_own;
}

void SelectMachinePopup::on_timer(wxTimerEvent &event)
{
    DeviceManager *dev_manager = wxGetApp().getDeviceManager();
    auto all_machine_list        = dev_manager->get_local_machine_list();
    m_free_machine_list.clear();
    m_free_machine_list = all_machine_list;
    
    /*for (auto& elem : all_machine_list) {
        MachineObject* dev = elem.second;
        if (dev->get_bind_str() == "Free") {
            this->m_free_machine_list[elem.first] = elem.second;
        }
    }*/
    wxCommandEvent bind_event(EVT_REQUEST_BIND_LIST);
    bind_event.SetEventObject(this);
    wxPostEvent(this, bind_event);

    // only update once
    if (m_refresh_timer) {
        m_refresh_timer->Stop();
    }
}

void SelectMachinePopup::update_other_devices(wxCommandEvent &event)
{
    for (auto i = 0; i < m_list_Machine_panel.GetCount(); i++) {
        MachinePanel *mpanel = m_list_Machine_panel[i];
        mpanel->mPanel->Destroy();
    }

    m_list_Machine_panel.clear();
    for (auto &elem : m_free_machine_list) {
        MachineObject *     mobj = elem.second;
        MachineObjectPanel *op = new MachineObjectPanel(m_scrolledWindow, wxID_ANY);
        op->set_can_bind(true);
        //if (mobj->can_abort()) {op->set_printer_busy();} 
        if (can_abort(mobj->print_status)) {
            op->set_printer_busy();
        }
        else {op->set_printer_idle();}
        op->update_machine_info(elem.second);
        m_sizer_other_devices->Add(op, 0, wxEXPAND, 0);

        MachinePanel *mpanel = new MachinePanel();
        mpanel->mIndex       = wxEmptyString;
        mpanel->mPanel       = op;
        m_list_Machine_panel.Add(mpanel);
    }
    Layout();
    Fit();
}

void SelectMachinePopup::update_machine_list(wxCommandEvent &event)
{
    Slic3r::DeviceManager *dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    m_bind_machine_list.clear();
    m_bind_machine_list = dev->userMachineList;

    m_scrolledWindow->Freeze();
    for (auto &elem : m_bind_machine_list) {
        MachineObject *     mobj = elem.second;
        MachineObjectPanel *op   = new MachineObjectPanel(m_scrolledWindow, wxID_ANY);
        op->set_can_bind(false);
        if (!mobj->is_online()) {
            op->set_printer_offline();
        } else {
            if (can_abort(mobj->print_status)) {
                op->set_printer_busy();
            } else {
                op->set_printer_idle();
            }
        }
        op->update_machine_info(elem.second);
        // op->Bind(wxEVT_LEFT_UP, &SelectMachinePopup::OnLeftUp, this);
        m_sizer_my_devices->Add(op, 0, wxEXPAND, 0);
    }

    Layout();
    Fit();
    m_scrolledWindow->Thaw();
}

bool SelectMachinePopup::can_abort(std::string state)
{
    if (state.compare("PAUSE") == 0 || state.compare("RUNNING") == 0 || state.compare("PREPARE") == 0) { return true; }
    return false;
}

void SelectMachinePopup::start_ssdp()
{
    BBL::SsdpDiscovery *ssdp = wxGetApp().getSsdpDiscovery();
    if (ssdp) {
        ssdp->start();
        ssdp->set_ssdp_discovery(true);
    }
}

void SelectMachinePopup::stop_ssdp()
{
    BBL::SsdpDiscovery * ssdp = wxGetApp().getSsdpDiscovery();
    if (ssdp) {
        ssdp->set_ssdp_discovery(false);
        ssdp->stop();
    }
}

void SelectMachinePopup::OnSize(wxSizeEvent &event) { event.Skip(); }

void SelectMachinePopup::OnSetFocus(wxFocusEvent &event) { event.Skip(); }

void SelectMachinePopup::OnKillFocus(wxFocusEvent &event) { event.Skip(); }

void SelectMachinePopup::OnMouse(wxMouseEvent &event) { event.Skip(); }

void SelectMachinePopup::OnLeftUp(wxMouseEvent &event)
{
    this->GetParent()->SetFocus();
    event.Skip();
}

static wxString MACHINE_BED_TYPE_STRING[BED_TYPE_COUNT] = {
    //_L("Auto"),
    _L("Bambu Cool Plate"), _L("Bamabu Engineering Plate"), _L("Bamabu High Temperature Plate")};

static std::string MachineBedTypeString[BED_TYPE_COUNT] = {
    //"auto",
    "pc",
    "pei",
    "pe",
};

SelectMachineDialog::SelectMachineDialog(Plater *plater)
    : DPIDialog(static_cast<wxWindow *>(wxGetApp().mainframe), wxID_ANY, _L("Send print job to"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
    , m_plater(plater)
    , m_export_3mf_cancel(false)
{
    // bind
    Bind(wxEVT_CLOSE_WINDOW, &SelectMachineDialog::on_cancel, this);

    for (int i = 0; i < BED_TYPE_COUNT; i++) { m_bedtype_list.push_back(MACHINE_BED_TYPE_STRING[i]); }

    // font
    SetFont(wxGetApp().normal_font());

    // icon
    std::string icon_path = (boost::format("%1%/images/BambuStudio.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    Freeze();
    SetBackgroundColour(m_colour_def_color);

    m_sizer_main = new wxBoxSizer(wxVERTICAL);

    m_sizer_main->SetMinSize(wxSize(0, -1));
    m_line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    m_line_top->SetBackgroundColour(wxColour(166, 169, 170));

    m_sizer_main->Add(m_line_top, 0, wxEXPAND, 0);

    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(22));

    m_image = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    m_image->SetBackgroundColour(m_colour_def_color);

    sizer_thumbnail = new wxBoxSizer(wxVERTICAL);
    m_image->SetSizer(sizer_thumbnail);
    m_image->Layout();

    m_sizer_main->Add(m_image, 0, wxALIGN_CENTER | wxLEFT | wxRIGHT);

    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(10));

    wxBoxSizer *m_sizer_basic        = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *m_sizer_basic_weight = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *m_sizer_basic_time   = new wxBoxSizer(wxHORIZONTAL);

    auto timeimg = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("print-time", this, 18), wxDefaultPosition, wxSize(FromDIP(18), FromDIP(18)), 0);
    m_sizer_basic_weight->Add(timeimg, 1, wxEXPAND | wxALL, FromDIP(5));
    m_stext_time = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT);
    m_sizer_basic_weight->Add(m_stext_time, 0, wxALL, FromDIP(5));
    m_sizer_basic->Add(m_sizer_basic_weight, 0, wxALIGN_CENTER, 0);
    m_sizer_basic->Add(0, 0, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(30));

    auto weightimg = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("print-weight", this, 18), wxDefaultPosition, wxSize(FromDIP(18), FromDIP(18)), 0);
    m_sizer_basic_time->Add(weightimg, 1, wxEXPAND | wxALL, FromDIP(5));
    m_stext_weight = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    m_sizer_basic_time->Add(m_stext_weight, 0, wxALL, FromDIP(5));
    m_sizer_basic->Add(m_sizer_basic_time, 0, wxALIGN_CENTER, 0);
    m_sizer_main->Add(m_sizer_basic, 0, wxALIGN_CENTER, 0);

    m_sizer_material = new wxWrapSizer(wxHORIZONTAL, wxWRAPSIZER_DEFAULT_FLAGS);
    m_sizer_main->Add(m_sizer_material, 0, wxALIGN_CENTER | wxLEFT | wxRIGHT, FromDIP(80));
    m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(18));

    m__line_materia = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    m__line_materia->SetForegroundColour(wxColour(238, 238, 238));
    m__line_materia->SetBackgroundColour(wxColour(238, 238, 238));

    m_sizer_main->Add(m__line_materia, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(30));

    m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(14));

    wxBoxSizer *m_sizer_printer = new wxBoxSizer(wxHORIZONTAL);

    m_stext_printer_title = new wxStaticText(this, wxID_ANY, L("Printer"), wxDefaultPosition, wxSize(-1, -1), 0);
    m_stext_printer_title->SetFont(::Label::Head_14);
    m_stext_printer_title->Wrap(-1);
    m_stext_printer_title->SetForegroundColour(m_colour_bold_color);
    m_stext_printer_title->SetBackgroundColour(m_colour_def_color);

    m_sizer_printer->Add(m_stext_printer_title, 0, wxALL | wxLEFT, FromDIP(5));
    m_sizer_printer->Add(0, 0, 0, wxEXPAND | wxLEFT, FromDIP(12));

    m_comboBox_printer = new ::ComboBox(this, wxID_ANY, L(""), wxDefaultPosition, wxDefaultSize, 0, nullptr, wxCB_READONLY);
    m_comboBox_printer->Bind(wxEVT_COMBOBOX, &SelectMachineDialog::on_selection_changed, this);

    m_sizer_printer->Add(m_comboBox_printer, 1, wxEXPAND | wxRIGHT, FromDIP(30));
    m_sizer_main->Add(m_sizer_printer, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(30));

    m_panel_warn             = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    wxBoxSizer *m_sizer_warn = new wxBoxSizer(wxHORIZONTAL);

    m_sizer_warn->Add(0, 0, 1, wxEXPAND, FromDIP(5));

    auto warimg = new wxStaticBitmap(m_panel_warn, wxID_ANY, create_scaled_bitmap("obj_warning", m_panel_warn, 15), wxDefaultPosition,
                                     wxSize(FromDIP(15), FromDIP(15)), 0);
    m_sizer_warn->Add(warimg, 0, wxEXPAND, 0);

    m_statictext_warn = new wxStaticText(m_panel_warn, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
    m_statictext_warn->Wrap(-1);
    m_statictext_warn->SetFont(::Label::Body_13);
    m_statictext_warn->SetForegroundColour(wxColour(255, 111, 0));

    m_sizer_warn->Add(m_statictext_warn, 0, wxALL | wxEXPAND, FromDIP(5));

    m_sizer_warn->Add(0, 0, 1, wxEXPAND, FromDIP(5));

    m_panel_warn->SetSizer(m_sizer_warn);
    m_panel_warn->Layout();
    m_sizer_warn->Fit(m_panel_warn);
    m_sizer_main->Add(m_panel_warn, 0, wxEXPAND | wxTOP, FromDIP(2));

    m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(14));

    m_line_bed = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    m_line_bed->SetForegroundColour(wxColour(238, 238, 238));
    m_line_bed->SetBackgroundColour(wxColour(238, 238, 238));

    m_sizer_main->Add(m_line_bed, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(30));

    m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(14));

    // BBS hide bed choice
    // wxBoxSizer *m_sizer_bed = new wxBoxSizer(wxHORIZONTAL);
    // m_staticText_bed_title = new wxStaticText(this, wxID_ANY, L("Bed style"), wxDefaultPosition, wxSize(-1, -1), 0);
    // m_staticText_bed_title->SetFont(::Label::Head_14);
    // m_staticText_bed_title->Wrap(-1);
    // m_staticText_bed_title->SetForegroundColour(m_colour_bold_color);
    // m_staticText_bed_title->SetBackgroundColour(m_colour_def_color);
    // m_sizer_bed->Add(m_staticText_bed_title, 0, wxALL | wxEXPAND, 5);
    // m_sizer_bed->Add(0, 0, 0, wxEXPAND | wxLEFT, 12);
    // m_comboBox_bed = new ::ComboBox(this, wxID_ANY, L(""), wxDefaultPosition, wxDefaultSize, 0, nullptr, wxCB_READONLY);
    // for (auto i = 0; i < m_bedtype_list.size(); i++) { m_comboBox_bed->Append(m_bedtype_list[i]); }
    // m_sizer_bed->Add(m_comboBox_bed, 1, wxEXPAND | wxRIGHT, 30);
    // m_sizer_main->Add(m_sizer_bed, 0, wxEXPAND | wxLEFT | wxRIGHT, 30);

    m_sizer_select = new wxGridSizer(2, 2, 0, 0);

    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(16));
    select_bed       = create_item_checkbox(_L("Bed Leveling"), this, _L("Bed Leveling"), "bed_leveling");
    select_vibration = create_item_checkbox(_L("Vibration Calibration"), this, _L("Vibration Calibration"), "vibration_cali");
    select_flow      = create_item_checkbox(_L("Flow Calibration"), this, _L("Flow Calibration"), "flow_cali");
    select_layer_inspect = create_item_checkbox(_L("First Layer Inspection"), this, _L("First Layer Inspection"), "layer_inspect");
    select_record    = create_item_checkbox(_L("Record Timelapse"), this, _L("Record Timelapse"), "time_lapse");

    m_sizer_select->Add(select_bed);
    m_sizer_select->Add(select_vibration);
    m_sizer_select->Add(select_flow);
    m_sizer_select->Add(select_layer_inspect);
    m_sizer_select->Add(select_record);
    m_sizer_main->Add(m_sizer_select, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(40));
    m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(11));

    // error msg
    m_panel_err             = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    wxBoxSizer *m_sizer_err = new wxBoxSizer(wxHORIZONTAL);
    m_sizer_err->Add(0, 0, 0, wxEXPAND, FromDIP(5));

    auto errimg = new wxStaticBitmap(m_panel_err, wxID_ANY, create_scaled_bitmap("obj_warning", m_panel_warn, 30), wxDefaultPosition,
                                     wxSize(wxGetApp().em_unit() * 3, wxGetApp().em_unit() * 3), 0);
    m_sizer_err->Add(errimg, 0, wxEXPAND, 0);

    m_statictext_err = new wxStaticText(m_panel_err, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
    m_statictext_err->Wrap(-1);
    m_statictext_err->SetForegroundColour(wxColour(255, 111, 0));

    m_sizer_err->Add(m_statictext_err, 0, wxALL | wxEXPAND, FromDIP(5));
    m_sizer_err->Add(0, 0, 1, wxEXPAND, FromDIP(5));

    m_panel_err->SetSizer(m_sizer_err);
    m_panel_err->Layout();
    m_sizer_err->Fit(m_panel_err);
    m_sizer_main->Add(m_panel_err, 0, wxEXPAND | wxLEFT, FromDIP(40));

    // bottom  area
    /*  m_panel_bottom = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, wxGetApp().em_unit() * 7), wxTAB_TRAVERSAL);
      m_panel_bottom->SetMinSize(wxSize(-1, wxGetApp().em_unit() * 7));
      m_panel_bottom->SetMaxSize(wxSize(-1, wxGetApp().em_unit() * 7));
      m_panel_bottom->SetBackgroundColour(m_colour_def_color);*/
    m_sizer_bottom = new wxBoxSizer(wxVERTICAL);

    // line schedule
    m_line_schedule = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    m_line_schedule->SetBackgroundColour(wxColour(238, 238, 238));
    m_sizer_bottom->Add(m_line_schedule, 0, wxEXPAND | wxLEFT | wxRIGHT, 0);

    m_simplebook = new wxSimplebook(this, wxID_ANY, wxDefaultPosition, SELECT_MACHINE_DIALOG_SIMBOOK_SIZE, 0);
    m_sizer_bottom->Add(m_simplebook, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(10));

    // perpare mode
    m_panel_prepare = new wxPanel(m_simplebook, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    m_panel_prepare->SetBackgroundColour(m_colour_def_color);
    // m_panel_prepare->SetBackgroundColour(wxColour(135,206,250));
    wxBoxSizer *m_sizer_prepare = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *m_sizer_pcont   = new wxBoxSizer(wxHORIZONTAL);

    m_sizer_prepare->Add(0, 0, 1, wxTOP, FromDIP(22));
    m_sizer_pcont->Add(0, 0, 1, wxEXPAND, 0);
    m_button_ensure = new Button(m_panel_prepare, _L("Send"));
    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed), std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
                            std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
    m_button_ensure->SetBackgroundColor(btn_bg_green);
    m_button_ensure->SetBorderColor(wxColour(0, 174, 66));
    m_button_ensure->SetTextColor(wxColour(255, 255, 255));
    m_button_ensure->SetSize(SELECT_MACHINE_DIALOG_BUTTON_SIZE);
    m_button_ensure->SetMinSize(SELECT_MACHINE_DIALOG_BUTTON_SIZE);
    m_button_ensure->SetCornerRadius(FromDIP(12));

    m_button_ensure->Bind(wxEVT_BUTTON, &SelectMachineDialog::on_ok, this);
    m_sizer_pcont->Add(m_button_ensure, 0, wxEXPAND | wxRIGHT, 0);
    m_sizer_prepare->Add(m_sizer_pcont, 0, wxEXPAND, 0);
    m_panel_prepare->SetSizer(m_sizer_prepare);
    m_panel_prepare->Layout();
    m_simplebook->AddPage(m_panel_prepare, wxEmptyString, true);

    // sending mode
    m_status_bar    = std::make_shared<BBLStatusBarSend>(m_simplebook);
    m_panel_sending = m_status_bar->get_panel();
    m_simplebook->AddPage(m_panel_sending, wxEmptyString, false);

    // finish mode
    m_panel_finish = new wxPanel(m_simplebook, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    m_panel_finish->SetBackgroundColour(wxColour(135, 206, 250));
    wxBoxSizer *m_sizer_finish   = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *m_sizer_finish_v = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *m_sizer_finish_h = new wxBoxSizer(wxHORIZONTAL);

    auto imgsize      = FromDIP(25);
    auto completedimg = new wxStaticBitmap(m_panel_finish, wxID_ANY, create_scaled_bitmap("completed", m_panel_warn, 25), wxDefaultPosition, wxSize(imgsize, imgsize), 0);
    m_sizer_finish_h->Add(completedimg, 0, wxALIGN_CENTER | wxALL, FromDIP(5));

    m_statictext_finish = new wxStaticText(m_panel_finish, wxID_ANY, L("send completed"), wxDefaultPosition, wxDefaultSize, 0);
    m_statictext_finish->Wrap(-1);
    m_statictext_finish->SetForegroundColour(wxColour(0, 174, 66));
    m_sizer_finish_h->Add(m_statictext_finish, 0, wxALIGN_CENTER | wxALL, FromDIP(5));

    m_sizer_finish_v->Add(m_sizer_finish_h, 1, wxALIGN_CENTER, 0);

    m_sizer_finish->Add(m_sizer_finish_v, 1, wxALIGN_CENTER, 0);

    m_panel_finish->SetSizer(m_sizer_finish);
    m_panel_finish->Layout();
    m_sizer_finish->Fit(m_panel_finish);
    m_simplebook->AddPage(m_panel_finish, wxEmptyString, false);

    m_sizer_main->Add(m_sizer_bottom, 0, wxLEFT | wxRIGHT, FromDIP(30));

    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(12));

    // bind
    Bind(EVT_FINISHED_UPDATE_MACHINE_LIST, &SelectMachineDialog::update_printer_combobox, this);
    Bind(EVT_PRINT_JOB_CANCEL, &SelectMachineDialog::on_print_job_cancel, this);

    // sending_mode();
    SetSizer(m_sizer_main);
    Layout();
    Fit();
    Thaw();

    // init_model();
    prepare_mode();
    init_bind();
    init_timer();
    CenterOnParent();
}

wxWindow *SelectMachineDialog::create_item_checkbox(wxString title, wxWindow *parent, wxString tooltip, std::string param)
{
    auto checkbox = new wxWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    checkbox->SetBackgroundColour(m_colour_def_color);

    wxBoxSizer *sizer_checkbox = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *sizer_check    = new wxBoxSizer(wxVERTICAL);

    auto check = new ::CheckBox(checkbox);

    sizer_check->Add(check, 0, wxBOTTOM | wxEXPAND | wxTOP, FromDIP(5));

    sizer_checkbox->Add(sizer_check, 0, wxEXPAND, FromDIP(5));
    sizer_checkbox->Add(0, 0, 0, wxEXPAND | wxLEFT, FromDIP(11));

    auto text = new wxStaticText(checkbox, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, 0);
    text->SetFont(::Label::Body_13);
    text->SetForegroundColour(wxColour(107, 107, 107));
    text->Wrap(-1);
    sizer_checkbox->Add(text, 0, wxBOTTOM | wxEXPAND | wxTOP, FromDIP(5));

    checkbox->SetSizer(sizer_checkbox);
    checkbox->Layout();
    sizer_checkbox->Fit(checkbox);

    checkbox->SetToolTip(tooltip);
    text->SetToolTip(tooltip);

    text->Bind(wxEVT_LEFT_DOWN, [this, check](wxMouseEvent &) { check->SetValue(check->GetValue() ? false : true); });
    m_checkbox_list[param] = check;
    return checkbox;
}

void SelectMachineDialog::update_select_layout(PRINTER_TYPE type)
{
    if (type == PRINTER_TYPE::PRINTER_3DPrinter_UKNOWN) {
        select_bed->Show();
        select_vibration->Show();
        select_flow->Show();
        select_record->Show(false);
        select_layer_inspect->Show();
    }

    if (type == PRINTER_TYPE::PRINTER_3DPrinter_X1) {
        select_bed->Show();
        select_vibration->Show();
        select_flow->Show();
        select_record->Show(false);
        select_layer_inspect->Show();
    }

    if (type == PRINTER_TYPE::PRINTER_3DPrinter_X1_Carbon) {
        select_bed->Show();
        select_vibration->Show();
        select_flow->Show();
        select_record->Show(false);
        select_layer_inspect->Show();
    }

    if (type == PRINTER_TYPE::PRINTER_3DPrinter_P1) {
        select_bed->Show();
        select_vibration->Show();
        select_flow->Show(false);
        select_record->Show(false);
        select_layer_inspect->Show(false);
    }

    Fit();
}

void SelectMachineDialog::prepare_mode()
{
    m_panel_warn->Hide();
    m_panel_err->Hide();
    m_simplebook->SetSelection(0);
    Fit();
}

void SelectMachineDialog::sending_mode()
{
    m_panel_warn->Hide();
    m_panel_err->Hide();
    m_simplebook->SetSelection(1);
    Fit();
}

void SelectMachineDialog::finish_mode()
{
    m_panel_warn->Hide();
    m_panel_err->Hide();
    m_simplebook->SetSelection(2);
    Fit();
}

void SelectMachineDialog::update_warn_msg(wxString msg)
{
    if (msg.empty()) {
        m_statictext_warn->SetLabel(wxEmptyString);
        m_panel_warn->Hide();
    } else {
        m_statictext_warn->SetLabel(msg);
        m_panel_warn->Show();
    }

    Fit();
    Refresh();
}

void SelectMachineDialog::update_err_msg(wxString msg)
{
    if (msg.empty()) {
        m_statictext_err->SetLabel(wxEmptyString);
        m_panel_err->Hide();
    } else {
        m_statictext_err->SetLabel(msg);
        m_panel_err->Show();
    }

    Fit();
    Refresh();
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
    // m_dataViewListCtrl_machines->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &SelectMachineDialog::on_selection_changed, this);
}

void SelectMachineDialog::init_timer()
{
    m_refresh_timer = new wxTimer();
    m_refresh_timer->SetOwner(this);
}

void SelectMachineDialog::on_cancel(wxCloseEvent &event)
{
    if (m_print_job) {
        if (m_print_job->is_running()) {
            m_print_job->cancel();
            m_print_job->join();
        }
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
    update_err_msg(wxEmptyString);
    int result = 0;
    m_status_bar->set_status_text("Exporting 3mf was cancelled.");

    if (m_printer_last_select.empty()) {
        update_err_msg(_L("Please select a printer first."));
        return;
    }

    BBL::AccountManager *c = Slic3r::GUI::wxGetApp().getAccountManager();
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    MachineObject* obj_ = dev->get_user_machine(m_printer_last_select);
    if (obj_ == nullptr) {
        update_err_msg(_L("Please select a printer first."));
        return;
    }

    // check printing status
    /*if (!obj_->can_print()) {
        update_err_msg(_L("Current printer is busy. Please select another one."));
        return;
    }*/

    // check upgrading status
    if (obj_->upgrade_display_state == MachineObject::UpgradingDisplayState::UpgradingInProgress) {
        update_err_msg(_L("The printer is being updated. Please try again after the update."));
        return;
    }

     BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "for send task, current printer id =  "<< m_printer_last_select << std::endl;

#if !BBL_RELEASE_TO_PUBLIC
#ifdef BBL_CHECK_USER_REPORT
    int  task_id   = 0;
    bool printable = true;
    c->user_check_report(&task_id, &printable);
    if (task_id != 0 && !printable) {
        update_err_msg(_L("Please fill report first."));
        std::string report_url = (boost::format("https://autotest.bambu-lab.com/slicerAddReport?task_id=%1%&token=%2%") % task_id % c->get_curr_user()->m_autotest_token).str();
        wxLaunchDefaultBrowser(report_url);
        return;
    }
#endif
#endif

    m_need_disable_btn_ensure = true;
    m_button_ensure->Disable();

    sending_mode();
    m_status_bar->reset();
    m_status_bar->set_prog_block();

    result = m_plater->send_gcode(m_print_plate_idx, [this](int export_stage, int current, int total, bool &cancel) {
        bool     cancelled = false;
        wxString msg       = _L("Exporting 3mf...");
        m_status_bar->update_status(msg, cancelled, 15, true);
        m_export_3mf_cancel = cancel = cancelled;
    });

    if (result < 0) {
        wxString msg = _L("Internal error.") + _devL(" ") + _L("Exporting 3mf failed, please slice again.");
        m_status_bar->set_status_text(msg);
        return;
    }

    if (m_export_3mf_cancel) {
        m_status_bar->set_status_text("Exporting 3mf was cancelled");
        return;
    }

    m_print_job = std::make_shared<PrintJob>(m_status_bar, m_plater, m_printer_last_select);

    m_print_job->set_print_config(
        // MachineBedTypeString[m_comboBox_bed->GetSelection()],
        MachineBedTypeString[0],
        m_checkbox_list["bed_leveling"]->GetValue(),
        m_checkbox_list["flow_cali"]->GetValue(),
        m_checkbox_list["vibration_cali"]->GetValue(),
        m_checkbox_list["time_lapse"]->GetValue(),
        m_checkbox_list["layer_inspect"]->GetValue());

    m_print_job->on_success([this]() { finish_mode(); });

    m_status_bar->set_cancel_callback_fina([this]() {
        m_print_job->cancel();
        wxCommandEvent *event = new wxCommandEvent(EVT_PRINT_JOB_CANCEL);
        wxQueueEvent(this, event);
    });

    wxCommandEvent evt(m_plater->get_print_finished_event());
    m_print_job->start();
}

void SelectMachineDialog::on_print_job_cancel(wxCommandEvent &evt)
{
    if (m_print_job->is_running()) { m_print_job->join(5 * 1000); }
    prepare_mode();
    reset();
}

std::vector<std::string> SelectMachineDialog::sort_string(std::vector<std::string> strArray)
{
    std::vector<std::string> outputArray;
    std::sort(strArray.begin(), strArray.end());
    std::vector<std::string>::iterator st;
    for (st = strArray.begin(); st != strArray.end(); st++) { outputArray.push_back(*st); }

    return outputArray;
}

void SelectMachineDialog::update_printer_combobox(wxCommandEvent &event)
{
    if (m_print_job && m_need_disable_btn_ensure && m_print_job->is_finalized() && !m_print_job->is_finished()) {
        m_button_ensure->Enable();
        m_need_disable_btn_ensure = false;
    }

    Slic3r::DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();

    // clear machine list
    m_list.clear();
    m_comboBox_printer->Clear();

    std::vector<std::string> machine_list;

    std::map<std::string, MachineObject *> my_bind_machine_list = dev->userMachineList;
    // same machine only appear once
    for (auto it = my_bind_machine_list.begin(); it != my_bind_machine_list.end(); it++) {
        if (it->second && it->second->is_online()) {
            machine_list.push_back(it->second->dev_name);
        }
    }
    
    machine_list = sort_string(machine_list);
    for (auto tt = machine_list.begin(); tt != machine_list.end(); tt++) {
        for (auto it = my_bind_machine_list.begin(); it != my_bind_machine_list.end(); it++) {
            if (it->second->dev_name == *tt) {
                m_list.push_back(it->second);
                m_comboBox_printer->Append(from_u8(it->second->dev_name));
                break;
            }
        }
    }

    if (m_list.size() > 0) {
        if (m_printer_last_select.empty()) {
            m_printer_last_select = m_list[0]->dev_id;
            update_select_layout(m_list[0]->printer_type);
        }
        for (auto i = 0; i < m_list.size(); i++) {
            if (m_list[i]->dev_id == m_printer_last_select) {
                update_select_layout(m_list[i]->printer_type);
                m_comboBox_printer->SetSelection(i);
            }
        }
    } else {
        m_printer_last_select = "";
        update_select_layout(PRINTER_TYPE::PRINTER_3DPrinter_UKNOWN);
        m_comboBox_printer->SetTextLabel("");
        update_err_msg(_L("No printer available"));
    }

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ <<  "for send task, current printer id =  "<< m_printer_last_select << std::endl;
}

void SelectMachineDialog::on_timer(wxTimerEvent &event)
{
    BBL::AccountManager *c = Slic3r::GUI::wxGetApp().getAccountManager();
    if (c->is_user_login()) {
        if (this == NULL || this == nullptr) { return; }
        boost::thread get_print_info_thread = Slic3r::create_thread([this] {
            DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
            dev->update_user_machine_list_info();

            wxCommandEvent event(EVT_FINISHED_UPDATE_MACHINE_LIST);
            event.SetEventObject(this);
            wxPostEvent(this, event);
        });
    }
}

void SelectMachineDialog::on_selection_changed(wxCommandEvent &event)
{
    update_err_msg(wxEmptyString);
    BBL::AccountManager *acc = Slic3r::GUI::wxGetApp().getAccountManager();
    if (event.GetString().empty()) { return; }

    auto dev_name = event.GetString().ToStdString();
    auto selection = event.GetSelection();

    for (int i = 0; i < m_list.size(); i++) {
        if (m_list[i]->dev_name == dev_name && i == selection) {
            m_printer_last_select = m_list[i]->dev_id;
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ <<  "for send task, current printer id =  "<< m_printer_last_select << std::endl;
            DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
            dev->set_monitoring_machine(m_list[i]->dev_id);
            update_select_layout(m_list[i]->printer_type);
            break;
        }
    }
}

void SelectMachineDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    m_button_ensure->SetMinSize(SELECT_MACHINE_DIALOG_BUTTON_SIZE);
    m_status_bar->msw_rescale();
    Fit();
    Refresh();
}

wxImage *SelectMachineDialog::LoadImageFromBlob(const unsigned char *data, int size)
{
    if (data != NULL) {
        wxMemoryInputStream mi(data, size);
        wxImage *           img = new wxImage(mi, wxBITMAP_TYPE_ANY);
        if (img != NULL && img->IsOk()) return img;
        // wxLogDebug( wxT("DB::LoadImageFromBlob error: data=%p size=%d"), data, size);
        // caller is responsible for deleting the pointer
        delete img;
    }
    return NULL;
}

bool SelectMachineDialog::Show(bool show)
{
    // thumbmail
    Freeze();
    sizer_thumbnail->Clear();
    ThumbnailData *data = wxGetApp().plater()->get_thumbnail()[m_plater->get_partplate_list().get_curr_plate_index()];
    wxImage        image(data->width, data->height);
    image.InitAlpha();

    for (unsigned int r = 0; r < data->height; ++r) {
        unsigned int rr = (data->height - 1 - r) * data->width;
        for (unsigned int c = 0; c < data->width; ++c) {
            unsigned char *px = (unsigned char *) data->pixels.data() + 4 * (rr + c);
            image.SetRGB((int) c, (int) r, px[0], px[1], px[2]);
            image.SetAlpha((int) c, (int) r, px[3]);
        }
    }

    auto bitmap       = new wxBitmap(image);
    auto staticbitmap = new wxStaticBitmap(m_image, wxID_ANY, *bitmap, wxDefaultPosition, wxDefaultSize);
    sizer_thumbnail->Add(staticbitmap, 0, wxEXPAND, 0);
    sizer_thumbnail->Layout();

    std::vector<std::string> materials;
    {
        auto preset_bundle = wxGetApp().preset_bundle;
        for (auto filament_name : preset_bundle->filament_presets) {
            for (auto iter = preset_bundle->filaments.lbegin(); iter != preset_bundle->filaments.end(); iter++) {
                if (filament_name.compare(iter->name) == 0) {
                    ConfigOption *       opt      = iter->config.option("filament_type");
                    ConfigOptionStrings *opt_strs = dynamic_cast<ConfigOptionStrings *>(opt);
                    materials.push_back(opt_strs->get_at(0));
                }
            }
        }
    }

    // material info
    auto extruders = m_plater->get_partplate_list().get_curr_plate()->get_extruders();
    // auto materials      = wxGetApp().preset_bundle->filament_presets;
    BitmapCache bmcache;

    MaterialHash::iterator iter = m_materialList.begin();
    while (iter != m_materialList.end()) {
        int       id   = iter->first;
        Material *item = iter->second;
        item->item->Destroy();
        delete item;
        iter++;
    }

    m_sizer_material->Clear();
    m_materialList.clear();

    for (auto i = 0; i < extruders.size(); i++) {
        auto          extruder = extruders[i] - 1;
        auto          colour   = wxGetApp().preset_bundle->project_config.opt_string("filament_colour", (unsigned int) extruder);
        unsigned char rgb[3];
        bmcache.parse_color(colour, rgb);

        auto colour_rgb = wxColour((int) rgb[0], (int) rgb[1], (int) rgb[2]);
        // auto       bk = new Button(this, _L(materials[extruder]));

        auto item = new StaticBox(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);

        wxBoxSizer *item_sizer_h = new wxBoxSizer(wxHORIZONTAL);
        wxBoxSizer *item_sizer_v = new wxBoxSizer(wxVERTICAL);

        if (extruder >= 0 && extruder < materials.size()) {
            auto item_name = new wxStaticText(item, wxID_ANY, _L(materials[extruder]), wxDefaultPosition, wxDefaultSize, 0);
            item_name->SetBackgroundColour(colour_rgb);
            item_name->SetFont(::Label::Body_13);
            item_sizer_v->Add(item_name, 0, wxALIGN_CENTER | wxBOTTOM | wxTOP, FromDIP(1));
            item_sizer_h->Add(item_sizer_v, 0, wxALIGN_CENTER | wxLEFT | wxRIGHT, FromDIP(8));

            item->SetBackgroundColor(colour_rgb);
            if (colour_rgb.GetLuminance() < 0.5) {
                item->SetBorderColor(colour_rgb);
            } else {
                item->SetBorderColor(*wxBLACK);
            }

            auto textcolor = colour_rgb.GetLuminance() < 0.5 ? *wxWHITE : *wxBLACK;

            item_name->SetForegroundColour(textcolor);
        }
        item->SetSize(wxSize(-1, FromDIP(20)));
        item->SetCornerRadius(FromDIP(9));
        item->Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent &e) {});
        item->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent &e) {});
        item->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) {});

        // item->Layout();

        item->SetSizer(item_sizer_h);
        item->Layout();
        item_sizer_h->Fit(item);
        item->Refresh();

        Material *material_item = new Material();
        material_item->id       = 0;
        material_item->item     = item;
        m_materialList[i]       = material_item;
        m_sizer_material->Add(item, 0, wxLEFT | wxRIGHT, FromDIP(5));
    }

    m_sizer_material->Layout();

    // basic info
    auto aprint_stats = m_plater->get_partplate_list().get_current_fff_print().print_statistics();
    wxString time;
    PartPlate *plate = m_plater->get_partplate_list().get_curr_plate();
    if (plate) {
        if (plate->get_slice_result()) {
            time = wxString::Format("%s", get_bbl_remain_time_dhms(plate->get_slice_result()->print_statistics.modes[0].time));
        }
    }

    char weight[64];
    ::sprintf(weight, "  %.2f g", aprint_stats.total_weight);

    m_stext_time->SetLabel(time);
    m_stext_weight->SetLabel(weight);

    // bed type
    m_bed_last_select = 0;
    // m_comboBox_bed->SetSelection(m_bed_last_select);

    // checkbox default values
    m_checkbox_list["bed_leveling"]->SetValue(true);
    m_checkbox_list["flow_cali"]->SetValue(true);
    m_checkbox_list["vibration_cali"]->SetValue(false);
    m_checkbox_list["time_lapse"]->SetValue(false);
    m_checkbox_list["layer_inspect"]->SetValue(true);

    prepare_mode();
    // sending_mode();

    if (show) {
        m_refresh_timer->Start(LIST_REFRESH_INTERVAL);
        wxPostEvent(this, wxTimerEvent());
    } else {
        m_refresh_timer->Stop();
    }

    Thaw();
    return DPIDialog::Show(show);
}

SelectMachineDialog::~SelectMachineDialog()
{ /* m_button_ensure->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(SelectMachineDialog::on_ok), NULL, this); */
}

}} // namespace Slic3r::GUI
