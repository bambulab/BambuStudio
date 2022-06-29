#include "MediaFilePanel.h"
#include "ImageGrid.h"
#include "I18N.hpp"
#include "GUI_App.hpp"

#include "Widgets/Button.hpp"
#include "Widgets/SwitchButton.hpp"
#include "Widgets/Label.hpp"
#include "Printer/PrinterFileSystem.h"

namespace Slic3r {
namespace GUI {

MediaFilePanel::MediaFilePanel(wxWindow * parent)
    : wxPanel(parent, wxID_ANY)
{
    SetBackgroundColour(0xEEEEEE);
    Hide();

    m_tab_panel = new ::StaticBox(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_tab_panel->SetCornerRadius(5);
    m_tab_panel->SetMinSize({-1, 48 * em_unit(this) / 10});
    m_tab_button_year = new ::Button(m_tab_panel, _L("Year"), "", wxBORDER_NONE);
    m_tab_button_month = new ::Button(m_tab_panel, _L("Month"), "", wxBORDER_NONE);
    m_tab_button_all = new ::Button(m_tab_panel, _L("All Files"), "", wxBORDER_NONE);
    m_switch_label = new ::Label(::Label::Body_14, _L("Batch Operation"), this);
    m_switch_button = new ::SwitchButton(this);

    m_image_grid = new ImageGrid(this);

    wxBoxSizer * sizer = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer * top_sizer = new wxBoxSizer(wxHORIZONTAL);
    top_sizer->SetMinSize({-1, 75 * em_unit(this) / 10});
    top_sizer->AddStretchSpacer(1);

    wxBoxSizer * tab_sizer = new wxBoxSizer(wxHORIZONTAL);
    tab_sizer->Add(m_tab_button_year, 0, wxALIGN_CENTER_VERTICAL| wxLEFT | wxRIGHT, 24);
    tab_sizer->Add(m_tab_button_month, 0, wxALIGN_CENTER_VERTICAL);
    tab_sizer->Add(m_tab_button_all, 0, wxALIGN_CENTER_VERTICAL| wxLEFT | wxRIGHT, 24);
    m_tab_panel->SetSizer(tab_sizer);
    top_sizer->Add(m_tab_panel, 0, wxALIGN_CENTER_VERTICAL);

    wxBoxSizer * top_right_sizer = new wxBoxSizer(wxHORIZONTAL);
    top_right_sizer->AddStretchSpacer(1);
    top_right_sizer->Add(m_switch_label, 0, wxALIGN_CENTER_VERTICAL);
    top_right_sizer->Add(m_switch_button, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 24);
    top_sizer->Add(top_right_sizer, 1, wxALIGN_CENTER_VERTICAL);

    sizer->Add(top_sizer, 0, wxEXPAND);
    sizer->Add(m_image_grid, 1, wxEXPAND);

    SetSizer(sizer);

    auto button_clicked = [this](wxEvent& e) {
        auto mode = PrinterFileSystem::G_NONE;
        if (e.GetEventObject() == m_tab_button_year)
            mode = PrinterFileSystem::G_YEAR;
        else if (e.GetEventObject() == m_tab_button_month)
            mode = PrinterFileSystem::G_MONTH;
        m_image_grid->SetGroupMode(mode);
    };
    m_tab_button_year->Bind(wxEVT_COMMAND_BUTTON_CLICKED, button_clicked);
    m_tab_button_month->Bind(wxEVT_COMMAND_BUTTON_CLICKED, button_clicked);
    m_tab_button_all->Bind(wxEVT_COMMAND_BUTTON_CLICKED, button_clicked);

    m_switch_button->Bind(wxEVT_TOGGLEBUTTON, [this](auto& e) {
        e.Skip();
        m_image_grid->SetSelecting(m_switch_button->GetValue());
    });
    wxCommandEvent e(wxEVT_CHECKBOX);
    auto b = m_tab_button_all;
    e.SetEventObject(b);
    b->GetEventHandler()->ProcessEvent(e);

    auto onShowHide = [this](auto &e) {
        if (m_isBeingDeleted) return;
        auto fs = m_image_grid ? m_image_grid->GetFileSystem() : nullptr;
        if (fs) e.IsShown() && IsShown() ? fs->Start() : fs->Stop();
    };
    Bind(wxEVT_SHOW, onShowHide);
    parent->GetParent()->Bind(wxEVT_SHOW, onShowHide);
}

void MediaFilePanel::SetMachineObject(MachineObject* obj)
{
    std::string machine = obj ? obj->dev_id : "";
    if (machine == m_machine)
        return;
    m_machine = machine;
    auto fs = m_image_grid->GetFileSystem();
    if (fs) {
        m_image_grid->SetFileSystem(nullptr);
        fs->Unbind(EVT_MODE_CHANGED, &MediaFilePanel::fileChanged, this);
        fs->Stop(true);
    }
    if (!m_machine.empty()) {
        boost::shared_ptr<PrinterFileSystem> fs(new PrinterFileSystem);
        m_image_grid->SetFileSystem(fs);
        fs->Bind(EVT_MODE_CHANGED, &MediaFilePanel::fileChanged, this);
        fs->Bind(EVT_STATUS_CHANGED, [this, wfs = boost::weak_ptr(fs)](auto &e) {
            boost::shared_ptr fs(wfs);
            if (m_image_grid->GetFileSystem() != fs) // canceled
                return;
            wxString msg;
            switch (e.GetInt()) {
            case PrinterFileSystem::Initializing: msg = _L("Initializing..."); fetchUrl(boost::weak_ptr(fs)); break;
            case PrinterFileSystem::Connecting: msg = _L("Connecting..."); break;
            case PrinterFileSystem::Failed: msg = _L("Connect failed [%d]!"); break;
            case PrinterFileSystem::ListSyncing: msg = _L("Loading file list..."); break;
            }
            if (fs->GetCount() == 0)
                m_image_grid->SetStatus(msg);
            else
                (void) 0; // TODO: show dialog
        });
        if (IsShown()) fs->Start();
    }
    wxCommandEvent e(EVT_MODE_CHANGED);
    fileChanged(e);
}

void MediaFilePanel::Rescale()
{
    m_tab_button_year->Rescale();
    m_tab_button_month->Rescale();
    m_tab_button_all->Rescale();
    m_tab_panel->SetMinSize({-1, 48 * em_unit(this) / 10});
    auto top_sizer = GetSizer()->GetItem((size_t) 0)->GetSizer();
    top_sizer->SetMinSize({-1, 75 * em_unit(this) / 10});
    m_image_grid->Rescale();
    m_switch_button->Rescale();
}

void MediaFilePanel::fileChanged(wxCommandEvent& e1)
{
    e1.Skip();
    auto fs = m_image_grid->GetFileSystem();
    if (fs)
        m_image_grid->SetStatus(fs->GetCount() ? L"" : _L("No files"));
    auto mode = fs ? fs->GetGroupMode() : 0;
    if (m_last_mode == mode)
        return;
    ::Button* buttons[] = {m_tab_button_all, m_tab_button_month, m_tab_button_year};
    wxCommandEvent e(wxEVT_CHECKBOX);
    auto b = buttons[m_last_mode];
    e.SetEventObject(b);
    b->GetEventHandler()->ProcessEvent(e);
    b = buttons[mode];
    e.SetEventObject(b);
    b->GetEventHandler()->ProcessEvent(e);
    m_last_mode = mode;
}

void MediaFilePanel::fetchUrl(boost::weak_ptr<PrinterFileSystem> wfs)
{
    BBL::BambuNetworkAgent* agent = wxGetApp().getAgent();
    if (agent) {
        agent->get_camera_url(m_machine,
            [this, wfs](std::string url) {
            BOOST_LOG_TRIVIAL(info) << "MediaFilePanel::fetchUrl: camera_url: " << url;
            CallAfter([this, url, wfs] {
                boost::shared_ptr fs(wfs);
                if (fs != m_image_grid->GetFileSystem()) return;
                fs->SetUrl(url);
            });
        });
    }
}

MediaFileFrame::MediaFileFrame(wxWindow* parent)
    : DPIFrame(parent, wxID_ANY, "Media Files", wxDefaultPosition, { 1600, 900 })
{
    m_panel = new MediaFilePanel(this);
    wxBoxSizer * sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_panel, 1, wxEXPAND);
    SetSizer(sizer);

    Bind(wxEVT_CLOSE_WINDOW, [this](auto & e){
        Hide();
        e.Veto();
    });
}

void MediaFileFrame::on_dpi_changed(const wxRect& suggested_rect) { m_panel->Rescale(); Refresh(); }

}}