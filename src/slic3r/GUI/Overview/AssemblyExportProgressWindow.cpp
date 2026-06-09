#include "AssemblyExportProgressWindow.hpp"

#include "../GUI_App.hpp"

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

    m_gauge = new wxGauge(panel, wxID_ANY, 100, wxDefaultPosition, wxSize(FromDIP(480), FromDIP(8)), wxGA_HORIZONTAL | wxGA_SMOOTH);

    wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_message, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(10));
    sizer->Add(m_gauge, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, FromDIP(10));
    panel->SetSizer(sizer);

    wxBoxSizer *root_sizer = new wxBoxSizer(wxVERTICAL);
    root_sizer->Add(panel, 1, wxEXPAND);
    SetSizer(root_sizer);
    Fit();
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
