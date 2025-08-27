#include <wx/sizer.h>
#include "LinkLabel.hpp"

LinkLabel::LinkLabel(wxWindow *parent, wxString const &text, std::string url, long style, wxSize size)
    : wxWindow(parent, wxID_ANY, wxDefaultPosition, size, style)
{
    m_url = wxString(url);
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    m_txt = new Label(this, text);
    m_underline = new wxPanel(this);

    Bind(wxEVT_ENTER_WINDOW, [this](auto &e) { SetCursor(wxCURSOR_HAND); });
    m_txt->Bind(wxEVT_ENTER_WINDOW, [this](auto &e) { SetCursor(wxCURSOR_HAND); });
    m_underline->Bind(wxEVT_LEAVE_WINDOW, [this](auto &e) { SetCursor(wxCURSOR_ARROW); });

    Bind(wxEVT_LEFT_DOWN, &LinkLabel::link, this);
    m_txt->Bind(wxEVT_LEFT_DOWN, &LinkLabel::link, this);
    m_underline->Bind(wxEVT_LEFT_DOWN, &LinkLabel::link, this);

    m_underline->SetMinSize(wxSize(-1, FromDIP(1)));
    m_underline->SetMaxSize(wxSize(-1, FromDIP(1)));

    SeLinkLabelBColour(*wxWHITE);

    sizer->Add(m_txt, 0, wxEXPAND, 0);
    sizer->Add(m_underline, 0, wxEXPAND, 0);
    SetSizer(sizer);
    Layout();
    Fit();
}


void LinkLabel::link(wxMouseEvent &evt)
{
    if (!m_url.IsEmpty()) {
        wxLaunchDefaultBrowser(m_url);
    }
}

bool LinkLabel::SeLinkLabelFColour(const wxColour &colour)
{
    SetForegroundColour(colour);
    m_txt->SetForegroundColour(colour);
    m_underline->SetBackgroundColour(colour);
    return true;
}

bool LinkLabel::SeLinkLabelBColour(const wxColour &colour)
{
    SetBackgroundColour(colour);
    m_txt->SetBackgroundColour(colour);
    return true;
}
