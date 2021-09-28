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

#define INITIAL_NUMBER_OF_MACHINES  0
#define LIST_REFRESH_INTERVAL       2000

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
    list.merge(c->myBindMachineList);
    list.merge(d->get_user_machine_list());
    list.merge(d->get_free_machine_list());

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
    wxDataViewItem item = event.GetItem();
    wxVariant val;
    machine_model->GetValue(val, item, MachineListModel::Col_MachineSN);
    machine_sn = val.GetString();
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
