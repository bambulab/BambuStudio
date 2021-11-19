#include "SelectMachine.hpp"
#include "I18N.hpp"

#include "libslic3r/Utils.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "format.hpp"

#include <wx/clipbrd.h>

namespace Slic3r { 
namespace GUI {

#define INITIAL_NUMBER_OF_MACHINES      0
#define LIST_REFRESH_INTERVAL           2000
#define MACHINE_LIST_REFRESH_INTERVAL   2000

MachineListModel::MachineListModel() :
    wxDataViewVirtualListModel(INITIAL_NUMBER_OF_MACHINES)
{
    ;
}

void MachineListModel::display_machines(std::map<std::string, MachineObject*> list)
{
    m_nameColValues.clear();
    m_snColValues.clear();
    m_bindColValues.clear();
    m_connectionColValues.clear();

    std::map<std::string, MachineObject*>::iterator it;
    for (it = list.begin(); it != list.end(); it++) {
        MachineObject* obj = it->second;
        m_nameColValues.Add(obj->dev_name);
        m_snColValues.Add(obj->dev_id);
        m_bindColValues.Add(obj->get_bind_str());
        if (obj->dev_ip.empty()) {
            wxString conn_str = wxString::Format("WAN: %s", obj->is_online ? "Online" : "Offline");
            m_connectionColValues.Add(conn_str);
        }
        else {
            wxString conn_str = wxString::Format("LAN: %s", obj->dev_ip);
            m_connectionColValues.Add(conn_str);
        }
    }
    Reset(list.size());
}

void MachineListModel::add_machine(MachineObject* obj)
{
    //TODO convert string to wxString
    m_nameColValues.Add(obj->dev_name);
    m_snColValues.Add(obj->dev_id);
    m_bindColValues.Add(obj->get_bind_str());
    m_connectionColValues.Add(obj->dev_ip);
    Reset(m_nameColValues.GetCount());
}

int MachineListModel::find_row_by_sn(wxString sn)
{
    wxVariant val;
    for (int i = 0; i < this->GetCount(); i++) {
        GetValueByRow(val, i, Col_MachineSN);
        if (val == sn) {
            return i;
        }
    }

    return -1;
}

void MachineListModel::GetValueByRow(wxVariant& variant,
    unsigned int row, unsigned int col) const
{
    switch (col) {
    case Col_MachineName:
        if (row >= m_nameColValues.GetCount())
            variant = wxString::Format("virtual row %d", row);
        else
            variant = m_nameColValues[row];
        break;
    case Col_MachineSN:
        if (row >= m_snColValues.GetCount())
            variant = wxString::Format("virtual row %d", row);
        else
            variant = m_snColValues[row];
        break;
    case Col_MachineBind:
        if (row >= m_bindColValues.GetCount())
            variant = wxString::Format("virtual row %d", row);
        else
            variant = m_bindColValues[row];
        break;
    case Col_MachineConnection:
        if (row >= m_connectionColValues.GetCount())
            variant = wxString::Format("virtual row %d", row);
        else
            variant = m_connectionColValues[row];
        break;
    default:
        break;
    }
}

bool MachineListModel::GetAttrByRow(unsigned int row, unsigned int col,
    wxDataViewItemAttr& attr) const
{
    return true;
}

bool MachineListModel::SetValueByRow(const wxVariant& variant,
    unsigned int row, unsigned int col)
{
    switch (col)
    {
    case Col_MachineName:
        if (row >= m_nameColValues.GetCount())
            return false;
        m_nameColValues[row] = variant.GetString();
        return true;
    case Col_MachineSN:
        if (row >= m_snColValues.GetCount())
            return false;
        m_snColValues[row] = variant.GetString();
        return true;
    case Col_MachineBind:
        if (row >= m_bindColValues.GetCount())
            return false;
        m_bindColValues[row] = variant.GetString();
        return true;
    case Col_MachineConnection:
        if (row >= m_connectionColValues.GetCount())
            return false;
        m_connectionColValues[row] = variant.GetString();
        return true;
    default:
        break;
    }
    return false;
}


MachineObjectPanel::MachineObjectPanel( wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style, const wxString& name ) : wxPanel( parent, id, pos, size, style, name )
{
    m_bg_colour = wxColour(43, 52, 54);
    m_hover_colour = wxColour(61, 70, 72);

    init_bitmap();

	wxBoxSizer* bSizer_top;
	bSizer_top = new wxBoxSizer( wxHORIZONTAL );

	m_bitmap_type = new wxStaticBitmap( this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize( 27,27 ), 0 );
	bSizer_top->Add( m_bitmap_type, 0, wxALL, 5 );

	wxBoxSizer* bSizer_middle;
	bSizer_middle = new wxBoxSizer( wxVERTICAL );

	m_staticText_printer = new wxStaticText( this, wxID_ANY, wxT("BBL_Printer"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_printer->Wrap( -1 );
	bSizer_middle->Add( m_staticText_printer, 0, wxALL|wxEXPAND, 5 );

	wxBoxSizer* bSizer_bottom;
	bSizer_bottom = new wxBoxSizer( wxHORIZONTAL );

	bSizer_bottom->SetMinSize( wxSize( 60,-1 ) );
	wxBoxSizer* bSizer_info;
	bSizer_info = new wxBoxSizer( wxVERTICAL );

	wxBoxSizer* bSizer_printing_info;
	bSizer_printing_info = new wxBoxSizer( wxHORIZONTAL );

	m_bitmap_info = new wxStaticBitmap( this, wxID_ANY, printing_img, wxDefaultPosition, wxSize( 8,8 ), 0 );
	bSizer_printing_info->Add( m_bitmap_info, 0, wxALIGN_CENTER|wxALL, 5 );

	m_staticText_printing = new wxStaticText( this, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_printing->Wrap( -1 );
	bSizer_printing_info->Add( m_staticText_printing, 1, wxALIGN_CENTER|wxALL|wxEXPAND, 5 );


	bSizer_info->Add( bSizer_printing_info, 1, wxEXPAND, 5 );

	wxBoxSizer* bSizer_bind_info;
	bSizer_bind_info = new wxBoxSizer( wxHORIZONTAL );

	m_bitmap_bind = new wxStaticBitmap( this, wxID_ANY, owner_img, wxDefaultPosition, wxSize( 8,8 ), 0 );
	bSizer_bind_info->Add( m_bitmap_bind, 0, wxALIGN_CENTER|wxALL, 5 );

	m_staticText_bind_info = new wxStaticText( this, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_bind_info->Wrap( -1 );
	bSizer_bind_info->Add( m_staticText_bind_info, 1, wxALIGN_CENTER|wxALL|wxEXPAND, 5 );


	bSizer_info->Add( bSizer_bind_info, 1, wxEXPAND, 5 );


	bSizer_bottom->Add( bSizer_info, 1, wxEXPAND, 5 );

	wxBoxSizer* bSizer_ams;
	bSizer_ams = new wxBoxSizer( wxVERTICAL );

	m_bitmap_ams = new wxStaticBitmap( this, wxID_ANY, ams_placeholder_img, wxDefaultPosition, wxSize( 32,32 ), 0 );
	m_bitmap_ams->SetMinSize( wxSize( 32,32 ) );

	bSizer_ams->Add( m_bitmap_ams, 0, wxALIGN_CENTER|wxALL, 5 );


	bSizer_bottom->Add( bSizer_ams, 0, wxALL, 5 );


	bSizer_middle->Add( bSizer_bottom, 0, wxALL|wxEXPAND, 0 );


	bSizer_top->Add( bSizer_middle, 1, wxEXPAND, 5 );


	this->SetSizer( bSizer_top );
	this->Layout();

    m_staticText_printer->Bind(wxEVT_ENTER_WINDOW,  &MachineObjectPanel::on_mouse_enter, this);
    m_bitmap_type->Bind(wxEVT_ENTER_WINDOW,         &MachineObjectPanel::on_mouse_enter, this);
    m_bitmap_info->Bind(wxEVT_ENTER_WINDOW,         &MachineObjectPanel::on_mouse_enter, this);
    m_bitmap_bind->Bind(wxEVT_ENTER_WINDOW,         &MachineObjectPanel::on_mouse_enter, this);
    m_bitmap_ams->Bind(wxEVT_ENTER_WINDOW,         &MachineObjectPanel::on_mouse_enter, this);
    m_staticText_printing->Bind(wxEVT_ENTER_WINDOW, &MachineObjectPanel::on_mouse_enter, this);
	m_staticText_bind_info->Bind(wxEVT_ENTER_WINDOW,&MachineObjectPanel::on_mouse_enter, this);
    this->Bind(wxEVT_ENTER_WINDOW,                  &MachineObjectPanel::on_mouse_enter, this);

    m_staticText_printer->Bind(wxEVT_LEFT_UP,       &MachineObjectPanel::on_mouse_left_up, this);
    m_bitmap_type->Bind(wxEVT_LEFT_UP,              &MachineObjectPanel::on_mouse_left_up, this);
    m_bitmap_info->Bind(wxEVT_LEFT_UP,              &MachineObjectPanel::on_mouse_left_up, this);
    m_bitmap_bind->Bind(wxEVT_LEFT_UP,              &MachineObjectPanel::on_mouse_left_up, this);
    m_bitmap_ams->Bind(wxEVT_LEFT_UP,              &MachineObjectPanel::on_mouse_left_up, this);
    m_staticText_printing->Bind(wxEVT_LEFT_UP,      &MachineObjectPanel::on_mouse_left_up, this);
	m_staticText_bind_info->Bind(wxEVT_LEFT_UP,     &MachineObjectPanel::on_mouse_left_up, this);
    this->Bind(wxEVT_LEFT_UP,                       &MachineObjectPanel::on_mouse_left_up, this);
}

void MachineObjectPanel::init_bitmap()
{
    ams_placeholder_img = create_scaled_bitmap("machine_object_ams", nullptr, 27);
    printing_img = create_scaled_bitmap("machine_object_printing", nullptr, 8);
    owner_img = create_scaled_bitmap("machine_object_owner", nullptr, 8);
}

MachineObjectPanel::~MachineObjectPanel()
{
}

void MachineObjectPanel::update_machine_info(MachineObject* obj)
{
    if (!obj) return;

    obj_ = obj;

    wxString machine_name_text = wxString::Format("%s", obj->dev_name);
    m_staticText_printer->SetLabelText(machine_name_text);

    BBLSubTask* subtask = obj->get_subtask();
    if (subtask) {
        int left_progress = std::max(100 - subtask->task_progress, 0);
        int left_second = 0;
        if (!subtask->task_prediction.empty()) {
            left_second = stoi(subtask->task_prediction) * left_progress / 100;
        }
        std::string left_str = left_second == 0 ? "N/A" : get_time_dhms(left_second);
        wxString printing_text = wxString::Format("%s Left, %d%%", left_str, subtask->task_progress);
        m_staticText_printing->SetLabelText(printing_text);
    }

    wxString bind_text = wxString::Format("%s", obj->get_bind_str());
    m_staticText_bind_info->SetLabelText(bind_text);

    wxBitmap machine_type_img = create_scaled_bitmap("machine_obejct_type", nullptr, 21);
    m_bitmap_type->SetBitmap(machine_type_img);

    wxBitmap machine_ams = create_scaled_bitmap("machine_object_ams", nullptr, 32);
    m_bitmap_ams->SetBitmap(machine_ams);

    this->Fit();
    this->Layout();
}

void MachineObjectPanel::on_mouse_enter(wxMouseEvent& evt)
{
    this->SetBackgroundColour(m_hover_colour);
    Refresh();
}

void MachineObjectPanel::on_mouse_leave(wxMouseEvent& evt)
{
    this->SetBackgroundColour(m_bg_colour);
    Refresh();
}

void MachineObjectPanel::on_mouse_left_up(wxMouseEvent& evt)
{
    Slic3r::AccountManager* c = Slic3r::GUI::wxGetApp().getAccountManager();
    c->default_machine = obj_->dev_id;
    wxGetApp().mainframe->jump_to_monitor(obj_->dev_id);       
}


wxIMPLEMENT_CLASS(SelectMachinePopup,wxPopupTransientWindow);

wxBEGIN_EVENT_TABLE(SelectMachinePopup,wxPopupTransientWindow)
    EVT_MOUSE_EVENTS( SelectMachinePopup::OnMouse )
    EVT_SIZE( SelectMachinePopup::OnSize )
    EVT_SET_FOCUS( SelectMachinePopup::OnSetFocus )
    EVT_KILL_FOCUS( SelectMachinePopup::OnKillFocus )
wxEND_EVENT_TABLE()

SelectMachinePopup::SelectMachinePopup( wxWindow *parent, bool scrolled)
                     :wxPopupTransientWindow( parent,
                                              wxBORDER_NONE |
                                              wxPU_CONTAINS_CONTROLS )
{
#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif //__WINDOWS__

    m_bg_colour = wxColour(43, 52, 54);
    m_hover_colour = wxColour(61, 70, 72);

    m_panel = new wxScrolledWindow( this, wxID_ANY );
    m_panel->SetBackgroundColour(m_bg_colour);
    topSizer = new wxBoxSizer( wxVERTICAL );
    topSizer->SetMinSize(POPUP_WIDTH, POPUP_HEIGHT);

    m_staticText_select = new wxStaticText( m_panel, wxID_ANY, wxT("Select Your Printer"), wxDefaultPosition, wxDefaultSize, 0 );
    m_staticText_select->Wrap( -1 );
    topSizer->Add(m_staticText_select, 0, wxALL | wxEXPAND, 5);

    m_panel->Bind(wxEVT_MOTION, &SelectMachinePopup::OnMouse, this);

    m_panel->SetSizer( topSizer );
    if ( scrolled )
    {
        // Set the fixed size to ensure that the scrollbars are shown.
        m_panel->SetSize(POPUP_WIDTH, POPUP_HEIGHT);

        // And also actually enable them.
        m_panel->SetScrollRate(10, 10);
    }
    else
    {
        // Use the fitting size for the panel if we don't need scrollbars.
        topSizer->Fit(m_panel);
    }

    SetClientSize(m_panel->GetSize());

    Bind(wxEVT_TIMER, &SelectMachinePopup::on_timer, this);
}

void SelectMachinePopup::Popup(wxWindow* WXUNUSED(focus))
{
    m_refresh_timer = new wxTimer();
    m_refresh_timer->SetOwner(this);
    m_refresh_timer->Start(MACHINE_LIST_REFRESH_INTERVAL);
    wxPostEvent(this, wxTimerEvent());
    wxPopupTransientWindow::Popup();
}

void SelectMachinePopup::OnDismiss()
{
    if (m_refresh_timer) {
        m_refresh_timer->Stop();
    }
    wxPopupTransientWindow::OnDismiss();
}

bool SelectMachinePopup::ProcessLeftDown(wxMouseEvent& event)
{
    return wxPopupTransientWindow::ProcessLeftDown(event);
}
bool SelectMachinePopup::Show( bool show )
{
    return wxPopupTransientWindow::Show(show);
}

void SelectMachinePopup::update_machine_list(std::vector<MachineObject*> obj_list)
{
    Freeze();
    m_obj_list = obj_list;
    if (obj_panels.size() < obj_list.size()) {
        int added_panels = obj_list.size() - obj_panels.size();
        for (int i = 0; i < added_panels; i++) {
            MachineObjectPanel* new_panel = new MachineObjectPanel(m_panel, wxID_ANY);
            obj_panels.push_back(new_panel);
        }
    }
    if (obj_panels.size() > obj_list.size()) {
        int deled_panels = obj_panels.size() - obj_list.size();
        for (int i = 0; i < deled_panels; i++) {
            MachineObjectPanel* old_panel = obj_panels.back();
            obj_panels.pop_back();
            delete old_panel;
        }
    }

    topSizer->Clear();
    topSizer->Add(m_staticText_select, 0, wxALL | wxEXPAND, 5);

    int height = 0;
    // empty list
    if (obj_list.empty()) {
        ;
    } else {
        for (int i = 0; i < obj_list.size(); i++) {
            MachineObjectPanel* obj_panel = obj_panels[i];
            obj_panel->update_machine_info(obj_list[i]);
            wxStaticLine* m_staticline = new wxStaticLine(m_panel, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxLI_HORIZONTAL);
            m_staticline->SetBackgroundColour(wxColour(115, 115, 115));
            topSizer->Add(m_staticline, 0, wxEXPAND | wxALL, 5);
            topSizer->Add(obj_panel, 0, wxEXPAND | wxALL, 5);
            height += m_staticline->GetSize().GetHeight();
            height += obj_panel->GetSize().GetHeight();
        }
    }

    m_panel->SetSizer(topSizer);
    m_panel->SetSize(POPUP_WIDTH, POPUP_HEIGHT);
    m_panel->SetScrollRate(10, 10);
    m_panel->Layout();
    topSizer->Fit(m_panel);
    topSizer->Add(m_panel, 1, wxEXPAND | wxALL, 5);
    this->Fit();
    this->Layout();
    Thaw();
}

void SelectMachinePopup::OnSize(wxSizeEvent &event)
{
    event.Skip();
}

void SelectMachinePopup::OnSetFocus(wxFocusEvent &event)
{
    event.Skip();
}

void SelectMachinePopup::OnKillFocus(wxFocusEvent &event)
{
    event.Skip();
}

void SelectMachinePopup::OnMouse(wxMouseEvent &event)
{
    for (int i = 0; i < m_obj_list.size(); i++) {
        wxRect rect(obj_panels[i]->GetRect());
        rect.SetX(-100000);
        rect.SetWidth(2000000);
        wxColour colour = m_bg_colour;

        if (rect.Contains(event.GetPosition())) {
            colour = m_hover_colour;
        }

        obj_panels[i]->SetBackgroundColour(colour);
        obj_panels[i]->Refresh();
    }
    event.Skip();
}

void SelectMachinePopup::OnButton(wxCommandEvent& event)
{
    ;
}

void SelectMachinePopup::on_timer(wxTimerEvent& event)
{
    Slic3r::AccountManager* account_manager = Slic3r::GUI::wxGetApp().getAccountManager();
    std::vector<MachineObject*> show_list = account_manager->get_select_machine_list();
    update_machine_list(show_list);
    account_manager->request_bind_list();
}

SelectMachineDialog::SelectMachineDialog()
	: DPIDialog(static_cast<wxWindow*>(wxGetApp().mainframe), wxID_ANY, _L("Select Printer"), wxDefaultPosition,
	wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    /* auto created by wxFormBuilder */
    //this->SetSizeHints(wxSize(550, 480), wxSize(1920, 1280));

    wxBoxSizer* bSizer;
    bSizer = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer* bSizer_machines;
    bSizer_machines = new wxBoxSizer(wxVERTICAL);

    bSizer_machines->SetMinSize(wxSize(-1, 300));
    m_dataViewListCtrl_machines = new wxDataViewCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0);
    bSizer_machines->Add(m_dataViewListCtrl_machines, 1, wxALL | wxEXPAND, 5);


    bSizer->Add(bSizer_machines, 1, wxALL | wxEXPAND, 5);

    wxBoxSizer* bSizer_tips;
    bSizer_tips = new wxBoxSizer(wxHORIZONTAL);

    m_staticText_left = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_left->Wrap(-1);
    bSizer_tips->Add(m_staticText_left, 0, wxALIGN_CENTER | wxALL, 5);


    bSizer_tips->Add(0, 0, 1, wxEXPAND, 5);

    m_hyperlink_add_machine = new wxHyperlinkCtrl(this, wxID_ANY, wxT("how to add a printer?"), wxT("http://www.wxformbuilder.org"), wxDefaultPosition, wxDefaultSize, wxHL_DEFAULT_STYLE);
    bSizer_tips->Add(m_hyperlink_add_machine, 0, wxALIGN_CENTER | wxALIGN_RIGHT | wxALL | wxRIGHT, 5);


    bSizer->Add(bSizer_tips, 0, wxEXPAND, 5);

    wxBoxSizer* bSizer_buttons;
    bSizer_buttons = new wxBoxSizer(wxHORIZONTAL);


    bSizer_buttons->Add(0, 0, 1, wxEXPAND, 5);

    m_button_cancel = new wxButton(this, wxID_ANY, wxT("Cancel"), wxDefaultPosition, wxDefaultSize, 0);
    bSizer_buttons->Add(m_button_cancel, 0, wxALIGN_CENTER | wxALL, 5);

    m_button_ensure = new wxButton(this, wxID_ANY, wxT("OK"), wxDefaultPosition, wxDefaultSize, 0);
    bSizer_buttons->Add(m_button_ensure, 0, wxALIGN_CENTER | wxALL, 5);


    bSizer->Add(bSizer_buttons, 0, wxEXPAND, 5);


    this->SetSizer(bSizer);
    this->Layout();

    this->Centre(wxBOTH);

    this->SetSizeHints(wxSize(550, 480), wxSize(1920, 1280));

    // Connect Events
    m_button_cancel->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(SelectMachineDialog::on_cancel), NULL, this);
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
    m_dataViewListCtrl_machines->AppendTextColumn("Printer Name",
        MachineListModel::Col_MachineName,
        wxDATAVIEW_CELL_INERT,
        wxCOL_WIDTH_AUTOSIZE,
        wxALIGN_NOT,
        wxDATAVIEW_COL_SORTABLE);

    m_dataViewListCtrl_machines->AppendTextColumn("SN(dev_id)",
        MachineListModel::Col_MachineSN,
        wxDATAVIEW_CELL_INERT,
        wxCOL_WIDTH_AUTOSIZE,
        wxALIGN_NOT,
        wxDATAVIEW_COL_RESIZABLE);
    
    m_dataViewListCtrl_machines->AppendTextColumn("Owner",
        MachineListModel::Col_MachineBind,
        wxDATAVIEW_CELL_INERT,
        wxCOL_WIDTH_AUTOSIZE,
        wxALIGN_NOT,
        wxDATAVIEW_COL_RESIZABLE);

    m_dataViewListCtrl_machines->AppendTextColumn("Connection",
        MachineListModel::Col_MachineConnection,
        wxDATAVIEW_CELL_INERT,
        wxCOL_WIDTH_AUTOSIZE,
        wxALIGN_NOT,
        wxDATAVIEW_COL_RESIZABLE);
}

void SelectMachineDialog::init_bind()
{
    Bind(wxEVT_TIMER, &SelectMachineDialog::on_timer, this);
    m_dataViewListCtrl_machines->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED,
        &SelectMachineDialog::on_selection_changed, this);
}

void SelectMachineDialog::init_timer()
{
    m_refresh_timer = new wxTimer();
    m_refresh_timer->SetOwner(this);
    m_refresh_timer->Start(LIST_REFRESH_INTERVAL);
    wxPostEvent(this, wxTimerEvent());
}

void SelectMachineDialog::on_cancel(wxCommandEvent& event)
{
    this->EndModal(wxID_CANCEL);
}

void SelectMachineDialog::on_ok(wxCommandEvent& event)
{
    this->EndModal(wxID_OK);
}

void SelectMachineDialog::on_timer(wxTimerEvent& event)
{
    // update machine list, collections of bind list and local free
    Slic3r::AccountManager* c = Slic3r::GUI::wxGetApp().getAccountManager();
    Slic3r::DeviceManager* d = Slic3r::GUI::wxGetApp().getDeviceManager();
    std::map<std::string, MachineObject*> list;

    if (c->is_user_login()) {
        c->request_bind_list();
        d->query_bind_status(nullptr, nullptr);
    }
    
    // same machine only appear once
    std::map<std::string, MachineObject*>::iterator it;
    for (it = c->myBindMachineList.begin(); it != c->myBindMachineList.end(); it++) {
        if (it->second->is_online) {
            list.insert(std::make_pair(it->first, it->second));
        }
    }
    list.merge(d->get_user_machine_list());

    machine_model->display_machines(list);

    // select old items
    wxVariant val;
    int row = machine_model->find_row_by_sn(machine_sn);
    if (row >= 0) {
        wxDataViewItem item = machine_model->GetItem(row);
        m_dataViewListCtrl_machines->Select(item);
    }
}

void SelectMachineDialog::on_selection_changed(wxDataViewEvent& event)
{
    Slic3r::AccountManager* c = Slic3r::GUI::wxGetApp().getAccountManager();
    wxDataViewItem item = event.GetItem();
    wxVariant val;
    machine_model->GetValue(val, item, MachineListModel::Col_MachineSN);
    machine_sn = val.GetString();
    c->default_machine = machine_sn.ToStdString();
}

void SelectMachineDialog::on_dpi_changed(const wxRect& suggested_rect)
{
	Fit();
	Refresh();
}

SelectMachineDialog::~SelectMachineDialog()
{
    m_refresh_timer->Stop();

    // Disconnect Events
    m_button_cancel->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(SelectMachineDialog::on_cancel), NULL, this);
    m_button_ensure->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(SelectMachineDialog::on_ok), NULL, this);
}

} // namespace GUI
} // namespace Slic3r
