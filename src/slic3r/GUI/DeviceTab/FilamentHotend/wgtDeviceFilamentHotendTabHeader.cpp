#include "wgtDeviceFilamentHotendTabHeader.h"

#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/Widgets/Button.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/wxExtensions.hpp"

#include <wx/sizer.h>

wxDEFINE_EVENT(EVT_DEVICE_FILAMENT_HOTEND_TAB_CHANGED, wxCommandEvent);
wxDEFINE_EVENT(EVT_DEVICE_FILAMENT_HOTEND_BATCH_LOAD, wxCommandEvent);
wxDEFINE_EVENT(EVT_DEVICE_FILAMENT_HOTEND_BATCH_UNLOAD, wxCommandEvent);
wxDEFINE_EVENT(EVT_DEVICE_FILAMENT_HOTEND_BATCH_QUIT, wxCommandEvent);
wxDEFINE_EVENT(EVT_DEVICE_HOTEND_READ_ALL, wxCommandEvent);

namespace Slic3r::GUI
{

wgtDeviceFilamentHotendTabHeader::wgtDeviceFilamentHotendTabHeader(wxWindow* parent,
                                                                   wxWindowID id,
                                                                   const wxPoint& pos,
                                                                   const wxSize& size,
                                                                   long style)
    : wxPanel(parent, id, pos, size, style)
{
    CreateGui();
    SetSelectedTab(TabFilament);
}

void wgtDeviceFilamentHotendTabHeader::SetSelectedTab(TabType tab)
{
    m_selected_tab = tab;
    const bool filament_active = (tab == TabFilament);

    const wxColour active_text(0x00, 0xAE, 0x42);
    const wxColour normal_text(0x32, 0x3A, 0x3D);
    const wxColour active_line(0x00, 0xAE, 0x42);
    const wxColour normal_line = GetBackgroundColour();

    if (m_label_filament) {
        m_label_filament->SetForegroundColour(filament_active ? active_text : normal_text);
        m_label_filament->SetFont(filament_active ? Label::Body_14.Bold() : Label::Body_14);
    }
    if (m_label_hotend) {
        m_label_hotend->SetForegroundColour(filament_active ? normal_text : active_text);
        m_label_hotend->SetFont(filament_active ? Label::Body_14 : Label::Body_14.Bold());
    }
    if (m_line_filament)
        m_line_filament->SetBackgroundColour(filament_active ? active_line : normal_line);
    if (m_line_hotend)
        m_line_hotend->SetBackgroundColour(filament_active ? normal_line : active_line);

    m_btn_fila_load->Show(filament_active);
    m_btn_fila_unload->Show(filament_active);
    m_btn_fila_dry->Show(filament_active);
    m_btn_hotend_read_all->Show(!filament_active);

    Refresh();
    Layout();
}

void wgtDeviceFilamentHotendTabHeader::NotifyTabChanged(TabType tab)
{
    SetSelectedTab(tab);
    wxCommandEvent evt(EVT_DEVICE_FILAMENT_HOTEND_TAB_CHANGED, GetId());
    evt.SetEventObject(this);
    evt.SetInt(static_cast<int>(tab));
    GetEventHandler()->ProcessEvent(evt);
}

void wgtDeviceFilamentHotendTabHeader::ShowLoad(bool show, const wxString& tooltip)
{
    m_btn_fila_load->Show(show && m_selected_tab == TabFilament);
    if (m_btn_fila_load->GetToolTipText() != tooltip) {
        m_btn_fila_load->SetToolTip(tooltip);
    }

    Layout();
}

void wgtDeviceFilamentHotendTabHeader::ShowUnload(bool show, const wxString& tooltip)
{
    m_btn_fila_unload->Show(show && m_selected_tab == TabFilament);
    if (m_btn_fila_unload->GetToolTipText() != tooltip) {
        m_btn_fila_unload->SetToolTip(tooltip);
    }

    Layout();
}

void wgtDeviceFilamentHotendTabHeader::EnterBatchMode()
{
    if (m_selected_tab == TabFilament) {
        m_btn_fila_load->Show(false);
        m_btn_fila_unload->Show(false);
        m_btn_fila_quit->Show(true);
        Layout();
    }
}

void wgtDeviceFilamentHotendTabHeader::QuitBatchMode()
{
    if (m_selected_tab == TabFilament) {
        m_btn_fila_load->Show(true);
        m_btn_fila_unload->Show(true);
        m_btn_fila_quit->Show(false);
        Layout();
    }
}


void wgtDeviceFilamentHotendTabHeader::OnFilamentTabClick(wxMouseEvent& evt)
{
    NotifyTabChanged(TabFilament);
    evt.Skip();
}

void wgtDeviceFilamentHotendTabHeader::OnHotendTabClick(wxMouseEvent& evt)
{
    NotifyTabChanged(TabHotend);
    evt.Skip();
}

void wgtDeviceFilamentHotendTabHeader::OnBtnLoad(wxCommandEvent&)
{
    wxCommandEvent evt(EVT_DEVICE_FILAMENT_HOTEND_BATCH_LOAD, GetId());
    evt.SetEventObject(this);
    GetEventHandler()->ProcessEvent(evt);
}

void wgtDeviceFilamentHotendTabHeader::OnBtnUnload(wxCommandEvent&)
{
    wxCommandEvent evt(EVT_DEVICE_FILAMENT_HOTEND_BATCH_UNLOAD, GetId());
    evt.SetEventObject(this);
    GetEventHandler()->ProcessEvent(evt);
}

void wgtDeviceFilamentHotendTabHeader::OnBtnQuit(wxCommandEvent&)
{
    wxCommandEvent evt(EVT_DEVICE_FILAMENT_HOTEND_BATCH_QUIT, GetId());
    evt.SetEventObject(this);
    GetEventHandler()->ProcessEvent(evt);
}

void wgtDeviceFilamentHotendTabHeader::OnBtnReadAll(wxCommandEvent&)
{
    wxCommandEvent evt(EVT_DEVICE_HOTEND_READ_ALL, GetId());
    evt.SetEventObject(this);
    GetEventHandler()->ProcessEvent(evt);
}

void wgtDeviceFilamentHotendTabHeader::CreateGui()
{
    SetBackgroundColour(*wxWHITE);

    wxBoxSizer* root = new wxBoxSizer(wxHORIZONTAL);

    wxBoxSizer* tabs_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_tab_filament_panel = new wxPanel(this, wxID_ANY);
    m_tab_filament_panel->SetBackgroundColour(*wxWHITE);
    wxBoxSizer* filament_sizer = new wxBoxSizer(wxVERTICAL);
    m_label_filament = new Label(m_tab_filament_panel, _L("Filament"));
    m_label_filament->SetFont(Label::Body_14);
    filament_sizer->Add(m_label_filament, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP | wxBOTTOM, FromDIP(6));
    m_line_filament = new wxPanel(m_tab_filament_panel, wxID_ANY);
    m_line_filament->SetMinSize(wxSize(FromDIP(26), FromDIP(2)));
    m_line_filament->SetMaxSize(wxSize(-1, FromDIP(2)));
    filament_sizer->Add(m_line_filament, 0, wxALIGN_CENTER_HORIZONTAL);
    m_tab_filament_panel->SetSizer(filament_sizer);

    m_tab_hotend_panel = new wxPanel(this, wxID_ANY);
    m_tab_hotend_panel->SetBackgroundColour(*wxWHITE);
    wxBoxSizer* hotend_sizer = new wxBoxSizer(wxVERTICAL);
    m_label_hotend = new Label(m_tab_hotend_panel, _L("Hotend"));
    m_label_hotend->SetFont(Label::Body_14);
    hotend_sizer->Add(m_label_hotend, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP | wxBOTTOM, FromDIP(6));
    m_line_hotend = new wxPanel(m_tab_hotend_panel, wxID_ANY);
    m_line_hotend->SetMinSize(wxSize(FromDIP(26), FromDIP(2)));
    m_line_hotend->SetMaxSize(wxSize(-1, FromDIP(2)));
    hotend_sizer->Add(m_line_hotend, 0, wxALIGN_CENTER_HORIZONTAL);
    m_tab_hotend_panel->SetSizer(hotend_sizer);

    tabs_sizer->Add(m_tab_filament_panel, 0, wxRIGHT, FromDIP(16));
    tabs_sizer->Add(m_tab_hotend_panel, 0, wxRIGHT, FromDIP(12));
    root->Add(tabs_sizer, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
    root->AddStretchSpacer(1);

    m_btn_fila_load = new Button(this, _L("Load"));
    m_btn_fila_unload = new Button(this, _L("Unload"));
    m_btn_fila_quit = new Button(this, _L("Abort"));
    m_btn_fila_dry = new Button(this, _L("Dry"));
    m_btn_hotend_read_all = new Button(this, _L("Read All"));
    Button* btn_fila_debug = new Button(this, _L("开发提示"));
    for (auto* btn : {m_btn_fila_load, m_btn_fila_unload, m_btn_fila_dry, m_btn_fila_quit, m_btn_hotend_read_all, btn_fila_debug}) {
        btn->SetFont(Label::Body_13);
        btn->SetBackgroundColor(StateColor::createButtonStyleGray());
        btn->SetBackgroundColour(*wxWHITE);
        btn->SetMinSize(wxSize(FromDIP(72), FromDIP(28)));
    }
    m_btn_fila_quit->SetBackgroundColor(*wxGREEN);
    m_btn_fila_quit->SetBackgroundColour(*wxGREEN);
    m_btn_fila_quit->Hide();
    m_btn_hotend_read_all->Hide();

    root->Add(m_btn_fila_load, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    root->Add(m_btn_fila_unload, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    root->Add(m_btn_fila_quit, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    root->Add(m_btn_fila_dry, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    root->Add(m_btn_hotend_read_all, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));

#if !BBL_RELEASE_TO_PUBLIC
    root->Add(btn_fila_debug, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    btn_fila_debug->Bind(wxEVT_LEFT_DOWN, [this](auto&) {
        MessageDialog dialog(nullptr, wxString::FromUTF8("已支持: 耗材面板粗稿、单个读取、单个进退料\n正在支持：批量进退料、热端面板、烘干面板"),
                             wxString::FromUTF8("提示"), wxICON_WARNING | wxOK);
        dialog.ShowModal();
    });
#endif

    root->AddSpacer(FromDIP(10));

    SetSizer(root);
    SetMaxSize(wxSize(FromDIP(578), -1));
    SetMinSize(wxSize(FromDIP(578), -1));
    SetSize(wxSize(FromDIP(578), -1));

    m_tab_filament_panel->Bind(wxEVT_LEFT_DOWN, &wgtDeviceFilamentHotendTabHeader::OnFilamentTabClick, this);
    m_label_filament->Bind(wxEVT_LEFT_DOWN, &wgtDeviceFilamentHotendTabHeader::OnFilamentTabClick, this);
    m_tab_hotend_panel->Bind(wxEVT_LEFT_DOWN, &wgtDeviceFilamentHotendTabHeader::OnHotendTabClick, this);
    m_label_hotend->Bind(wxEVT_LEFT_DOWN, &wgtDeviceFilamentHotendTabHeader::OnHotendTabClick, this);
    m_btn_fila_load->Bind(wxEVT_BUTTON, &wgtDeviceFilamentHotendTabHeader::OnBtnLoad, this);
    m_btn_fila_unload->Bind(wxEVT_BUTTON, &wgtDeviceFilamentHotendTabHeader::OnBtnUnload, this);
    m_btn_fila_quit->Bind(wxEVT_BUTTON, &wgtDeviceFilamentHotendTabHeader::OnBtnQuit, this);
    m_btn_hotend_read_all->Bind(wxEVT_BUTTON, &wgtDeviceFilamentHotendTabHeader::OnBtnReadAll, this);
}

} // namespace Slic3r::GUI
