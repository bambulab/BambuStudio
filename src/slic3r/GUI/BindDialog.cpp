#include "BindDialog.hpp"
#include "GUI_App.hpp"

#include <wx/wx.h> 
#include <wx/sizer.h>
#include <wx/statbox.h>
#include "wx/evtloop.h"

#include "libslic3r/Model.hpp"
#include "libslic3r/Polygon.hpp"
#include "MainFrame.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"

namespace Slic3r {
namespace GUI {

BindDialog::BindDialog(Plater *plater)
    : DPIDialog(plater, wxID_ANY, _L("Bind Dialog"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX), m_plater(plater)
{
    this->SetSizeHints(wxDefaultSize, wxDefaultSize);

    wxBoxSizer *top_sizer;
    top_sizer = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer *ssdp_sizer;
    ssdp_sizer = new wxBoxSizer(wxHORIZONTAL);

    btn_start_ssdp = new wxButton(this, wxID_ANY, wxT("Start SSDP"), wxDefaultPosition, wxDefaultSize, 0);
    ssdp_sizer->Add(btn_start_ssdp, 0, wxALL, 5);

    btn_stop_ssdp = new wxButton(this, wxID_ANY, wxT("Stop SSDP"), wxDefaultPosition, wxDefaultSize, 0);
    ssdp_sizer->Add(btn_stop_ssdp, 0, wxALL, 5);

    ssdp_sizer->Add(0, 0, 1, wxEXPAND, 5);

    top_sizer->Add(ssdp_sizer, 0, wxEXPAND, 5);

    wxBoxSizer *sn_sizer;
    sn_sizer = new wxBoxSizer(wxHORIZONTAL);

    text_sn = new wxStaticText(this, wxID_ANY, wxT("Printer:"), wxDefaultPosition, wxDefaultSize, 0);
    text_sn->Wrap(-1);
    sn_sizer->Add(text_sn, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);

    cb_machine_sn = new wxComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0, NULL, 0);
    sn_sizer->Add(cb_machine_sn, 1, wxALL, 5);

    btn_refresh = new wxButton(this, wxID_ANY, wxT("Refresh"), wxDefaultPosition, wxDefaultSize, 0);
    sn_sizer->Add(btn_refresh, 0, wxALL, 5);

    top_sizer->Add(sn_sizer, 0, wxEXPAND, 5);

    m_status_bar = std::make_shared<BBLStatusBar>(this);
    auto panel = m_status_bar->get_panel();
    top_sizer->Add(panel, 1, wxALL | wxEXPAND, 0);

    text_result = new wxStaticText(this, wxID_ANY, wxT("Result Info"), wxDefaultPosition, wxDefaultSize, 0);
    text_result->Wrap(-1);
    top_sizer->Add(text_result, 0, wxALL | wxEXPAND, 5);

    wxBoxSizer *btn_sizer;
    btn_sizer = new wxBoxSizer(wxHORIZONTAL);

    btn_bind = new wxButton(this, wxID_ANY, wxT("Bind"), wxDefaultPosition, wxDefaultSize, 0);
    btn_sizer->Add(btn_bind, 0, wxALL, 5);

    /*btn_unbind = new wxButton(this, wxID_ANY, wxT("Unbind"), wxDefaultPosition, wxDefaultSize, 0);
    btn_sizer->Add(btn_unbind, 0, wxALL, 5);*/

    btn_sizer->Add(0, 0, 1, wxEXPAND, 5);

    top_sizer->Add(btn_sizer, 1, wxEXPAND, 5);

    this->SetSizer(top_sizer);
    this->Layout();

    this->Centre(wxBOTH);

    btn_start_ssdp->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(BindDialog::on_start_ssdp), NULL, this);
    btn_stop_ssdp->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(BindDialog::on_stop_ssdp), NULL, this);
    btn_bind->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(BindDialog::on_bind_printer), NULL, this);
    //btn_unbind->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(BindDialog::on_unbind_printer), NULL, this);
    btn_refresh->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(BindDialog::on_refresh), NULL, this);
}

BindDialog::~BindDialog() {
    btn_start_ssdp->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(BindDialog::on_start_ssdp), NULL, this);
    btn_stop_ssdp->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(BindDialog::on_stop_ssdp), NULL, this);
    btn_bind->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(BindDialog::on_bind_printer), NULL, this);
    //btn_unbind->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(BindDialog::on_unbind_printer), NULL, this);
    btn_refresh->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(BindDialog::on_refresh), NULL, this);
}

void BindDialog::on_start_ssdp(wxCommandEvent &event)
{
    CommuBackend *backend = wxGetApp().getCommuBackend();
    if (backend) {
        backend->start();
        backend->set_ssdp_discovery(true);
    }
}

void BindDialog::on_stop_ssdp(wxCommandEvent &event)
{
    CommuBackend *backend = wxGetApp().getCommuBackend();
    if (backend) {
        backend->set_ssdp_discovery(false);
        backend->stop();
    }
}

void BindDialog::on_bind_printer(wxCommandEvent &event)
{
    int select = cb_machine_sn->GetSelection();
    std::string sn_str = printer_list_item[select].ToStdString();
    DeviceManager *dev_manager = wxGetApp().getDeviceManager();
    std::map<std::string, MachineObject*> list = dev_manager->get_all_machine_list();
    std::map<std::string, MachineObject *>::iterator it = list.find(sn_str);
    if (it != list.end()) {
        m_bind_job = std::make_shared<BindJob>(m_status_bar, wxGetApp().plater(), it->second->dev_id);
        m_bind_job->start();
    }
}

void BindDialog::on_unbind_printer(wxCommandEvent &event)
{
    ;
}

void BindDialog::on_refresh(wxCommandEvent &event)
{
    printer_list_item.clear();
    DeviceManager *dev_manager = wxGetApp().getDeviceManager();
    std::map<std::string, MachineObject*> list = dev_manager->get_all_machine_list();

    for (auto it = list.begin(); it != list.end(); it++) {
        printer_list_item.push_back(it->second->dev_id);
    }
    cb_machine_sn->Set(printer_list_item);
}

void BindDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    Fit();
    Refresh();
}

} // GUI
} // Slic3r
