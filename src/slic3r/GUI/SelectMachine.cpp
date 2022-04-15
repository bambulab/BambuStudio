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
#include <miniz.h>
#include <algorithm>
#include "Plater.hpp"
#include "BitmapCache.hpp"

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
    : wxPanel(parent, id, pos, wxSize(parent->GetSize().GetWidth(), 8 * wxGetApp().em_unit()), style, name)
{
    m_text_color      = wxColour(38, 46, 48);
    m_bg_colour       = wxColour(255, 255, 255);
    m_hover_colour    = wxColour(248, 248, 248);
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
    m_sizer_main   = new wxBoxSizer(wxVERTICAL);
    m_sizer_body   = new wxBoxSizer(wxVERTICAL);

    // border
    m_border_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(POPUP_WIDTH * wxGetApp().em_unit(), POPUP_HEIGHT * wxGetApp().em_unit()), wxTAB_TRAVERSAL);
    m_border_panel->SetBackgroundColour(m_bg_colour);

    // client
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

    auto block_top = new wxWindow(m_client_panel, wxID_ANY, wxDefaultPosition, wxSize(0, 0));

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

        auto block_top = new wxWindow(list, wxID_ANY, wxDefaultPosition, wxSize(0, 0));

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

void SelectMachinePopup::OnLeftUp(wxMouseEvent &event)
{
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

static wxString MACHINE_BED_TYPE_STRING[BED_TYPE_COUNT] = {
    _L("Auto"),
    _L("Bmabu Cool Plate"),
    _L("Bmabu Engineering Plate"),
    _L("Bmabu High Temperature Plate")
};

static std::string MachineBedTypeString[BED_TYPE_COUNT] = {
    "auto",
    "pe",
    "pc",
    "pei",
};


SelectMachineDialog::SelectMachineDialog(Plater *plater)
    : DPIDialog(static_cast<wxWindow *>(wxGetApp().mainframe), wxID_ANY, _L("Send Task to"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
    , m_plater(plater)
    , m_export_3mf_cancel(false)
{
    //bind
    Bind(wxEVT_CLOSE_WINDOW, &SelectMachineDialog::on_cancel, this);

    for (int i = 0; i < BED_TYPE_COUNT; i++) {
        m_bedtype_list.push_back(MACHINE_BED_TYPE_STRING[i]);
    }

    // font
    SetFont(wxGetApp().normal_font());

    // icon
    std::string icon_path = (boost::format("%1%/images/BambuStudio.ico") % resources_dir()).str();
    SetIcon(wxIcon(icon_path, wxBITMAP_TYPE_ICO));

    this->SetSizeHints(wxSize(-1, -1), wxSize(-1, -1));
    this->SetBackgroundColour(m_colour_def_color);

    m_sizer_main = new wxBoxSizer(wxVERTICAL);

    m_sizer_main->SetMinSize(wxSize(0, -1));
    m_line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    m_line_top->SetBackgroundColour(wxColour(166, 169, 170));

    m_sizer_main->Add(m_line_top, 0, wxEXPAND, 0);

    m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, 30);

    m_image = new wxPanel(this, wxID_ANY, wxDefaultPosition,wxDefaultSize, wxTAB_TRAVERSAL);
    m_image->SetBackgroundColour(m_colour_def_color);

    sizer_thumbnail = new wxBoxSizer(wxVERTICAL);

    m_image->SetSizer(sizer_thumbnail);
    m_image->Layout();

    m_sizer_main->Add(m_image, 0, wxEXPAND | wxLEFT | wxRIGHT, 100);

    m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, 15);

    wxBoxSizer *m_sizer_basic = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *m_sizer_basic_time = new wxBoxSizer(wxHORIZONTAL);

    m_sizer_basic_time->Add(0, 0, 1, wxEXPAND, 5);

     auto timeimg = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("print-time", this, 18), wxDefaultPosition, wxSize(18, 18), 0);

    m_sizer_basic_time->Add(timeimg, 0, wxALL | wxEXPAND, 5);

    m_stext_time = new wxStaticText(this, wxID_ANY, L(""), wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT);
    m_stext_time->Wrap(-1);
    m_stext_time->SetForegroundColour(wxColour(50, 58, 61));
    m_stext_time->SetBackgroundColour(wxColour(255, 255, 255));

    m_sizer_basic_time->Add(m_stext_time, 0, wxALL, 5);

    m_sizer_basic->Add(m_sizer_basic_time, 1, wxEXPAND, 5);

    m_sizer_basic->Add(0, 0, 0, wxEXPAND | wxLEFT | wxRIGHT, 20);

    wxBoxSizer *m_sizer_basic_weight;
    m_sizer_basic_weight = new wxBoxSizer(wxHORIZONTAL);

    auto weightimg = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("print-weight", this, 18), wxDefaultPosition,
                                     wxSize(18, 18), 0);

    m_sizer_basic_weight->Add(weightimg, 0, wxALL | wxEXPAND | wxLEFT, 5);

    m_stext_weight = new wxStaticText(this, wxID_ANY, wxT("330g"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    m_stext_weight->Wrap(-1);
    m_stext_weight->SetForegroundColour(wxColour(50, 58, 61));
    m_stext_weight->SetBackgroundColour(m_colour_def_color);

    m_sizer_basic_weight->Add(m_stext_weight, 1, wxALL, 5);

    m_sizer_basic->Add(m_sizer_basic_weight, 1, wxEXPAND, 5);

    m_sizer_main->Add(m_sizer_basic, 0, wxEXPAND, 0);

    m_sizer_material = new wxWrapSizer(wxHORIZONTAL, wxWRAPSIZER_DEFAULT_FLAGS);
    m_sizer_main->Add(m_sizer_material, 1, wxALIGN_CENTER|wxLEFT|wxRIGHT, 80 );
    m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, 15);

    m__line_materia = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    m__line_materia->SetForegroundColour(wxColour(238, 238, 238));
    m__line_materia->SetBackgroundColour(wxColour(238, 238, 238));

    m_sizer_main->Add(m__line_materia, 0, wxEXPAND | wxLEFT | wxRIGHT, 30);

    m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, 14);

    wxBoxSizer *m_sizer_printer = new wxBoxSizer(wxHORIZONTAL);

    m_stext_printer_title = new wxStaticText(this, wxID_ANY, L("Printer"), wxDefaultPosition, wxSize(-1, -1), 0);
    m_stext_printer_title->SetFont(::Label::Head_14);
    m_stext_printer_title->Wrap(-1);
    m_stext_printer_title->SetForegroundColour(m_colour_bold_color);
    m_stext_printer_title->SetBackgroundColour(m_colour_def_color);

    m_sizer_printer->Add(m_stext_printer_title, 0, wxALL | wxLEFT, 5);
    m_sizer_printer->Add(0, 0, 0, wxEXPAND | wxLEFT, 12);

    m_comboBox_printer = new ::ComboBox(this, wxID_ANY, L(""), wxDefaultPosition, wxDefaultSize, 0, nullptr, wxCB_READONLY);
    m_comboBox_printer->Bind(wxEVT_COMBOBOX, &SelectMachineDialog::on_selection_changed, this);

    m_sizer_printer->Add(m_comboBox_printer, 1, wxEXPAND | wxRIGHT, 30);
    m_sizer_main->Add(m_sizer_printer, 0, wxEXPAND | wxLEFT | wxRIGHT, 30);

    m_panel_warn = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    wxBoxSizer *m_sizer_warn = new wxBoxSizer(wxHORIZONTAL);

    m_sizer_warn->Add(0, 0, 1, wxEXPAND, 5);

    auto warimg = new wxStaticBitmap(m_panel_warn, wxID_ANY, create_scaled_bitmap("obj_warning", m_panel_warn, wxGetApp().em_unit() * 3), wxDefaultPosition,
                                     wxSize(wxGetApp().em_unit() * 3, wxGetApp().em_unit() * 3), 0);
    m_sizer_warn->Add(warimg, 0, wxEXPAND, 0);

    m_statictext_warn = new wxStaticText(m_panel_warn, wxID_ANY, L(""), wxDefaultPosition, wxDefaultSize, 0);
    m_statictext_warn->Wrap(-1);
    m_statictext_warn->SetFont(::Label::Body_13);
    m_statictext_warn->SetForegroundColour(wxColour(255, 111, 0));

    m_sizer_warn->Add(m_statictext_warn, 0, wxALL | wxEXPAND, 5);

    m_sizer_warn->Add(0, 0, 1, wxEXPAND, 5);

    m_panel_warn->SetSizer(m_sizer_warn);
    m_panel_warn->Layout();
    m_sizer_warn->Fit(m_panel_warn);
    m_sizer_main->Add(m_panel_warn, 0, wxEXPAND | wxTOP, 2);

    m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, 14);

    m_line_bed = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    m_line_bed->SetForegroundColour(wxColour(238, 238, 238));
    m_line_bed->SetBackgroundColour(wxColour(238, 238, 238));

    m_sizer_main->Add(m_line_bed, 0, wxEXPAND | wxLEFT | wxRIGHT, 30);

    m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, 14);

    wxBoxSizer *m_sizer_bed = new wxBoxSizer(wxHORIZONTAL);

    m_staticText_bed_title = new wxStaticText(this, wxID_ANY, L("Bed style"), wxDefaultPosition, wxSize(-1, -1), 0);
    m_staticText_bed_title->SetFont(::Label::Head_14);
    m_staticText_bed_title->Wrap(-1);
    m_staticText_bed_title->SetForegroundColour(m_colour_bold_color);
    m_staticText_bed_title->SetBackgroundColour(m_colour_def_color);

    m_sizer_bed->Add(m_staticText_bed_title, 0, wxALL | wxEXPAND, 5);

    m_sizer_bed->Add(0, 0, 0, wxEXPAND | wxLEFT, 12);

    m_comboBox_bed = new ::ComboBox(this, wxID_ANY, L(""), wxDefaultPosition, wxDefaultSize, 0, nullptr, wxCB_READONLY);

    for (auto i = 0; i < m_bedtype_list.size(); i++) { m_comboBox_bed->Append(m_bedtype_list[i]); }

    m_sizer_bed->Add(m_comboBox_bed, 1, wxEXPAND | wxRIGHT, 30);
    m_sizer_main->Add(m_sizer_bed, 0, wxEXPAND | wxLEFT | wxRIGHT, 30);

    wxGridSizer *m_sizer_select = new wxGridSizer(2, 2, 0, 0);

    m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, 20);
    auto select_bed       = create_item_checkbox(L("Bed Leveling"), this, L("Bed Leveling"), "bed_leveling");
    auto select_vibration = create_item_checkbox(L("Vibration Calibration"), this, L("Vibration Calibration"), "vibration_cali");
    auto select_flow      = create_item_checkbox(L("Flow Calibration"), this, L("Flow Calibration"), "flow_cali");
    auto select_record    = create_item_checkbox(L("Record Timelapse"), this, L("Record Timelapse"), "time_lapse");

    m_sizer_select->Add(select_bed);
    m_sizer_select->Add(select_vibration);
    m_sizer_select->Add(select_flow);
    m_sizer_select->Add(select_record);
    m_sizer_main->Add(m_sizer_select, 0, wxEXPAND | wxLEFT | wxRIGHT, 40);
    m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, 11);

    // error msg
    m_panel_err             = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    wxBoxSizer *m_sizer_err = new wxBoxSizer(wxHORIZONTAL);
    m_sizer_err->Add(0, 0, 0, wxEXPAND, 5);

    auto errimg = new wxStaticBitmap(m_panel_err, wxID_ANY, create_scaled_bitmap("obj_warning", m_panel_warn, wxGetApp().em_unit() * 3), wxDefaultPosition,
                                     wxSize(wxGetApp().em_unit() * 3, wxGetApp().em_unit() * 3), 0);
    m_sizer_err->Add(errimg, 0, wxEXPAND, 0);

    m_statictext_err = new wxStaticText(m_panel_err, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
    m_statictext_err->Wrap(-1);
    m_statictext_err->SetForegroundColour(wxColour(255, 111, 0));

    m_sizer_err->Add(m_statictext_err, 0, wxALL | wxEXPAND, 5);
    m_sizer_err->Add(0, 0, 1, wxEXPAND, 5);

    m_panel_err->SetSizer(m_sizer_err);
    m_panel_err->Layout();
    m_sizer_err->Fit(m_panel_err);
    m_sizer_main->Add(m_panel_err, 0, wxEXPAND | wxLEFT, 40);

    // bottom  area
    m_panel_bottom = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, wxGetApp().em_unit() * 6), wxTAB_TRAVERSAL);
    m_panel_bottom->SetMinSize(wxSize(-1, wxGetApp().em_unit() * 6));
    m_panel_bottom->SetMaxSize(wxSize(-1, wxGetApp().em_unit() * 6));
    m_panel_bottom->SetBackgroundColour(m_colour_def_color);
    m_sizer_bottom = new wxBoxSizer(wxVERTICAL);

    // line schedule
    m_line_schedule = new wxPanel(m_panel_bottom, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    m_line_schedule->SetBackgroundColour(wxColour(238, 238, 238));
    m_sizer_bottom->Add(m_line_schedule, 0, wxEXPAND | wxLEFT | wxRIGHT, 0);


    m_simplebook = new wxSimplebook(m_panel_bottom, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0);

    // perpare mode
    m_sizer_bottom->Add(m_simplebook, 1, wxEXPAND | wxLEFT | wxRIGHT, 10);

    m_panel_prepare             = new wxPanel(m_simplebook, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    m_panel_prepare->SetBackgroundColour(m_colour_def_color);
    wxBoxSizer *m_sizer_prepare = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *m_sizer_pcont   = new wxBoxSizer(wxHORIZONTAL);

    m_sizer_prepare->Add(0, 0, 1, wxEXPAND | wxTOP, 30);
    m_sizer_pcont->Add(0, 0, 1, wxEXPAND, 0);
    m_button_ensure = new Button(m_panel_prepare, _L("Send"));
    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed), 
                            std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
                            std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
    m_button_ensure->SetBackgroundColor(btn_bg_green);
    m_button_ensure->SetBorderColor(wxColour(0, 174, 66));
    m_button_ensure->SetTextColor(wxColour(255, 255, 255));
    m_button_ensure->SetSize(wxSize(68, 24));
    m_button_ensure->SetMinSize(wxSize(72, 24));
    m_button_ensure->SetCornerRadius(12);

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
    m_panel_finish             = new wxPanel(m_simplebook, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    wxBoxSizer *m_sizer_finish = new wxBoxSizer(wxHORIZONTAL);

    m_sizer_finish->Add(0, 0, 1, wxEXPAND, 5);

    auto imgsize      = wxGetApp().em_unit() * 2 + wxGetApp().em_unit() / 2;
    auto completedimg = new wxStaticBitmap(m_panel_finish, wxID_ANY, create_scaled_bitmap("completed", m_panel_warn, imgsize), wxDefaultPosition, wxSize(imgsize, imgsize), 0);

    m_sizer_finish->Add(completedimg, 0, wxEXPAND | wxRIGHT, 12);

    m_statictext_finish = new wxStaticText(m_panel_finish, wxID_ANY, L("send completed"), wxDefaultPosition, wxDefaultSize, 0);
    m_statictext_finish->Wrap(-1);
    m_statictext_finish->SetForegroundColour(wxColour(0, 174, 66));

    m_sizer_finish->Add(m_statictext_finish, 0, wxALL | wxEXPAND, 3);

    m_sizer_finish->Add(0, 0, 1, wxEXPAND, 5);

    m_panel_finish->SetSizer(m_sizer_finish);
    m_panel_finish->Layout();
    m_sizer_finish->Fit(m_panel_finish);
     m_simplebook->AddPage(m_panel_finish, wxEmptyString, false);

    m_panel_bottom->SetSizer(m_sizer_bottom);
    m_panel_bottom->Layout();
    m_sizer_main->Add(m_panel_bottom, 0, wxEXPAND | wxLEFT | wxRIGHT, 30);

    m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, 30);

    // sending_mode();
    SetSizer(m_sizer_main);
    Layout();
    Fit();

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

    sizer_check->Add(check, 0, wxBOTTOM | wxEXPAND | wxTOP, 5);

    sizer_checkbox->Add(sizer_check, 0, wxEXPAND, 5);
    sizer_checkbox->Add(0, 0, 0, wxEXPAND | wxLEFT, 11);

    auto text = new wxStaticText(checkbox, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, 0);
    text->SetFont(::Label::Body_13);
    text->Wrap(-1);
    sizer_checkbox->Add(text, 0, wxBOTTOM | wxEXPAND | wxTOP, 5);

    checkbox->SetSizer(sizer_checkbox);
    checkbox->Layout();
    sizer_checkbox->Fit(checkbox);

    checkbox->SetToolTip(tooltip);
    text->SetToolTip(tooltip);

    text->Bind(wxEVT_LEFT_DOWN, [this, check](wxMouseEvent &) { check->SetValue(check->GetValue() ? false : true); });
    m_checkbox_list[param] = check;
    return checkbox;
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
    m_status_bar->set_status_text("exporting 3mf was cancelled");

    if (m_printer_last_select <= -1) {
        update_err_msg(_L("Please select a printer first!"));
        return;
    }

    std::string             dev_id = m_list[m_printer_last_select]->dev_id;
    Slic3r::AccountManager *c      = Slic3r::GUI::wxGetApp().getAccountManager();

    std::map<std::string, MachineObject *>::iterator it = c->myBindMachineList.find(dev_id);
    if (it == c->myBindMachineList.end()) {
        update_err_msg(_L("Please select a printer first!"));
        return;
    }

    // TODO check printing status
    if (!it->second->can_print()) {
        update_err_msg(L("current printer is busy! please select another!"));
        return;
    }

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

    sending_mode();
    m_status_bar->reset();
    m_status_bar->set_prog_block();

    m_plater->send_gcode(m_print_plate_idx, [this](int export_stage, int current, int total, bool &cancel) {
        bool cancelled = false;
        wxString msg = _L("Exporting 3mf...");
        m_status_bar->update_status(msg, cancelled, 15, true);
        m_export_3mf_cancel = cancel = cancelled;
    });

    if (m_export_3mf_cancel) {
        m_status_bar->set_status_text("exporting 3mf was cancelled");
        return;
    }
    
    m_print_job = std::make_shared<PrintJob>(m_status_bar, m_plater, dev_id);

    m_print_job->set_print_config(
        MachineBedTypeString[m_comboBox_bed->GetSelection()], 
        m_checkbox_list["bed_leveling"]->GetValue(),
        m_checkbox_list["flow_cali"]->GetValue(),
        m_checkbox_list["vibration_cali"]->GetValue(),
        m_checkbox_list["time_lapse"]->GetValue()
    );


    m_print_job->on_success([this]() { finish_mode(); });

    m_status_bar->set_cancel_callback_fina([this]() {
        if (m_print_job->is_running()) { m_print_job->cancel(); }
        prepare_mode();
        reset();
    });

    wxCommandEvent evt(m_plater->get_print_finished_event());
    m_print_job->start();
}

std::vector<std::string> SelectMachineDialog::sort_string(std::vector<std::string> strArray)
{
    std::vector<std::string> outputArray;
    std::sort(strArray.begin(), strArray.end());
    std::vector<std::string>::iterator st;
    for (st = strArray.begin(); st != strArray.end(); st++) {
        outputArray.push_back(*st);
    }

    return outputArray;
}

void SelectMachineDialog::on_timer(wxTimerEvent &event)
{
    if (m_print_job && m_need_disable_btn_ensure && m_print_job->is_finalized() && !m_print_job->is_finished()) {
        m_button_ensure->Enable();
        m_need_disable_btn_ensure = false;
    }

    // update machine list, collections of bind list and local free
    Slic3r::AccountManager *c = Slic3r::GUI::wxGetApp().getAccountManager();

    if (c->is_user_login()) {
        boost::thread get_print_info_thread = Slic3r::create_thread([this] {
            Slic3r::AccountManager *acc = Slic3r::GUI::wxGetApp().getAccountManager();
            int                     err_code;
            std::string             err_msg;
            acc->update_my_machine_list_info(err_code, err_msg, true);
        });
    }

    // clear machine list
    m_list.clear();
    m_comboBox_printer->Clear();

    std::vector<std::string> machine_list;

    // same machine only appear once
    std::map<std::string, MachineObject *>::iterator it;
    for (it = c->myBindMachineList.begin(); it != c->myBindMachineList.end(); it++) {
        if (it->second && it->second->is_online()) {
            machine_list.push_back(it->second->dev_name);
        }
    }

    machine_list = sort_string(machine_list);
    std::vector<std::string>::iterator tt;
    for (tt = machine_list.begin(); tt != machine_list.end(); tt++) {

        for (it = c->myBindMachineList.begin(); it != c->myBindMachineList.end(); it++) {

            if (it->second->dev_name == *tt) {
                m_list.push_back(it->second);
                m_comboBox_printer->Append(it->second->dev_name);
                break;
            }
        }
    }


    if (m_list.size() > 0) {
        if (m_printer_last_select <= -1) { m_printer_last_select = 0; }
        m_comboBox_printer->SetSelection(m_printer_last_select);
    } else {
        m_printer_last_select = -1;
    }
}

void SelectMachineDialog::on_selection_changed(wxCommandEvent &event)
{
    Slic3r::AccountManager *c = Slic3r::GUI::wxGetApp().getAccountManager();
    if (event.GetString().empty()) { return; }
    if (m_printer_last_select == -1) { return; }
    m_printer_last_select = event.GetSelection();
    c->default_machine    = m_list[m_printer_last_select]->dev_id;
}

void SelectMachineDialog::on_dpi_changed(const wxRect &suggested_rect)
{
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
    //thumbmail
    sizer_thumbnail->Clear();
    ThumbnailData *data = wxGetApp().plater()->get_thumbnail()[m_plater->get_partplate_list().get_curr_plate_index()];
    wxImage image(data->width, data->height);
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


    //material info
    auto extruders      = m_plater->get_partplate_list().get_curr_plate()->get_extruders();
    //auto materials      = wxGetApp().preset_bundle->filament_presets;
    BitmapCache bmcache;

    

    MaterialHash::iterator iter = m_materialList.begin();
    while (iter != m_materialList.end()) {
        int       id = iter->first;
        Material *item   = iter->second;
        item->item->Destroy();
        delete item;
        iter++;
    }

    m_sizer_material->Clear();
    m_materialList.clear();

    for (auto i = 0; i < extruders.size(); i++) {
        auto extruder = extruders[i]-1;
        auto colour   = wxGetApp().preset_bundle->project_config.opt_string("filament_colour", (unsigned int) extruder);
        unsigned char rgb[3];
        bmcache.parse_color(colour, rgb);

        auto colour_rgb = wxColour((int)rgb[0], (int)rgb[1], (int)rgb[2]);
        //auto       bk = new Button(this, _L(materials[extruder]));


        auto item = new StaticBox(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);

        wxBoxSizer *item_sizer_h = new wxBoxSizer(wxHORIZONTAL);
        wxBoxSizer *item_sizer_v = new wxBoxSizer(wxVERTICAL);

        auto item_name = new wxStaticText(item, wxID_ANY, _L(materials[extruder]), wxDefaultPosition, wxDefaultSize, 0);
        item_name->SetBackgroundColour(colour_rgb);
        item_name->SetFont(::Label::Body_13);
        item_sizer_v->Add(item_name, 0, wxALIGN_CENTER | wxBOTTOM | wxTOP, 3);
        item_sizer_h->Add(item_sizer_v, 0, wxALIGN_CENTER | wxLEFT | wxRIGHT, 8);

        item->SetBackgroundColor(colour_rgb);
        if (colour_rgb.GetLuminance() < 0.5) {
            item->SetBorderColor(colour_rgb);
        } else {
            item->SetBorderColor(*wxBLACK);
        }

        auto  textcolor = colour_rgb.GetLuminance() < 0.5? *wxWHITE : *wxBLACK;


        item_name->SetForegroundColour(textcolor);

        //item->SetSize(wxSize(-1, 23));
        //item->SetMinSize(wxSize(-1, 23));
        //item->SetMaxSize(wxSize(-1, 23));
        item->SetCornerRadius(11);
        item->Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent &e) {});
        item->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent &e) {});
        item->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) {});

        //item->Layout();

        item->SetSizer(item_sizer_h);
        item->Layout();
        item_sizer_h->Fit(item);
        item->Refresh();

        Material* material_item = new Material();
        material_item->id   = 0;
        material_item->item = item;
        m_materialList[i] = material_item;
        m_sizer_material->Add(item, 0, wxEXPAND | wxLEFT | wxRIGHT, 5);
    }

    m_sizer_material->Layout();

    // basic info
    auto aprint_stats = m_plater->get_partplate_list().get_current_fff_print().print_statistics();
    auto time         = short_time(aprint_stats.estimated_normal_print_time);
    char weight[64];
    ::sprintf(weight, "  %.2f g", aprint_stats.total_weight);

    m_stext_time->SetLabel(time);
    m_stext_weight->SetLabel(weight);

    // bed type
    m_bed_last_select = 0;
    m_comboBox_bed->SetSelection(m_bed_last_select);

    // checkbox
    m_checkbox_list["bed_leveling"]->SetValue(true);
    m_checkbox_list["flow_cali"]->SetValue(true);
    m_checkbox_list["vibration_cali"]->SetValue(false);
    m_checkbox_list["time_lapse"]->SetValue(false);

    prepare_mode();

    if (show) {
        m_refresh_timer->Start(LIST_REFRESH_INTERVAL);
        wxPostEvent(this, wxTimerEvent());
    } else {
        m_refresh_timer->Stop();
    }
    return DPIDialog::Show(show);
}

SelectMachineDialog::~SelectMachineDialog() { /* m_button_ensure->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(SelectMachineDialog::on_ok), NULL, this); */ }

}} // namespace Slic3r::GUI
