#include "AssemblyExportProgressWindow.hpp"

#include "../GUI_App.hpp"
#include "../I18N.hpp"
#include "../Widgets/Button.hpp"
#include "../Widgets/Label.hpp"
#include "../Widgets/StateColor.hpp"

#include <wx/gauge.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/window.h>

#include <algorithm>

namespace Slic3r {
namespace GUI {

AssemblyExportProgressWindow::AssemblyExportProgressWindow(wxWindow *parent)
    : wxFrame(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
              wxFRAME_NO_TASKBAR | wxBORDER_SIMPLE | wxSTAY_ON_TOP)
{
    SetFont(wxGetApp().normal_font());
    SetBackgroundColour(wxColour(255, 255, 255));

    wxPanel *panel = new wxPanel(this, wxID_ANY);
    panel->SetBackgroundColour(wxColour(255, 255, 255));

    m_message = new wxStaticText(panel, wxID_ANY, wxEmptyString);
    m_message->SetForegroundColour(wxColour(107, 107, 107));
    m_message->SetFont(::Label::Body_13);

    m_gauge = new wxGauge(panel, wxID_ANY, 100, wxDefaultPosition, wxSize(FromDIP(360), FromDIP(6)), wxGA_HORIZONTAL | wxGA_SMOOTH);
    m_gauge->SetMinSize(wxSize(FromDIP(300), FromDIP(6)));

    m_percent = new wxStaticText(panel, wxID_ANY, "0%", wxDefaultPosition, wxDefaultSize, 0);
    m_percent->SetForegroundColour(wxColour(107, 107, 107));
    m_percent->SetFont(::Label::Body_13);
    m_percent->SetMinSize(wxSize(FromDIP(40), -1));

    StateColor btn_bg(std::pair<wxColour, int>(wxColour(0x90, 0x90, 0x90), StateColor::Disabled),
                      std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed),
                      std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
                      std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));
    StateColor btn_bd(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Disabled),
                      std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));
    StateColor btn_txt(std::pair<wxColour, int>(wxColour("#FFFFFE"), StateColor::Disabled),
                       std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Normal));

    m_cancel = new Button(panel, _L("Cancel"));
    m_cancel->SetMinSize(wxSize(FromDIP(58), FromDIP(22)));
    m_cancel->SetMaxSize(wxSize(FromDIP(58), FromDIP(22)));
    m_cancel->SetBackgroundColor(btn_bg);
    m_cancel->SetBorderColor(btn_bd);
    m_cancel->SetTextColor(btn_txt);
    m_cancel->SetCornerRadius(FromDIP(12));
    m_cancel->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { on_cancel(); });

    wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(m_gauge, 1, wxALIGN_CENTER_VERTICAL);
    row->Add(m_percent, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(10));
    row->Add(m_cancel, 0, wxALIGN_CENTER_VERTICAL);

    wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_message, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(10));
    sizer->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, FromDIP(10));
    panel->SetSizer(sizer);

    wxBoxSizer *root_sizer = new wxBoxSizer(wxVERTICAL);
    root_sizer->Add(panel, 1, wxEXPAND);
    SetSizer(root_sizer);
    Fit();
}

void AssemblyExportProgressWindow::set_cancel_callback(std::function<void()> cb)
{
    m_cancel_cb = std::move(cb);
}

void AssemblyExportProgressWindow::enable_cancel(bool enable)
{
    if (m_cancel)
        m_cancel->Enable(enable);
}

void AssemblyExportProgressWindow::on_cancel()
{
    if (m_cancel)
        m_cancel->Enable(false);
    if (m_cancel_cb)
        m_cancel_cb();
}

void AssemblyExportProgressWindow::update_progress(const wxString &message, int value, int maximum, wxWindow *anchor)
{
    if (maximum <= 0)
        maximum = 1;
    value = std::max(0, std::min(value, maximum));

    if (m_message)
        m_message->SetLabel(message);
    if (m_gauge) {
        if (m_gauge->GetRange() != maximum)
            m_gauge->SetRange(maximum);
        m_gauge->SetValue(value);
    }
    if (m_percent)
        m_percent->SetLabel(wxString::Format("%d%%", value * 100 / maximum));

    Layout();
    Fit();
    position_near_anchor(anchor);
    if (!IsShown())
        ShowWithoutActivating();
    Raise();
}

void AssemblyExportProgressWindow::position_near_anchor(wxWindow *anchor)
{
    if (!anchor)
        return;

    const wxSize anchor_size = anchor->GetClientSize();
    const wxSize win_size    = GetSize();
    const int margin         = FromDIP(16);
    wxPoint pos = anchor->ClientToScreen(wxPoint(margin, std::max(margin, anchor_size.y - win_size.y - margin)));
    Move(pos);
}

} // namespace GUI
} // namespace Slic3r
