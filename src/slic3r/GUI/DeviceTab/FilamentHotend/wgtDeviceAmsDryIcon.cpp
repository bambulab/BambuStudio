#include "wgtDeviceAmsDryIcon.h"

#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"
#include "slic3r/GUI/wxExtensions.hpp"

#include <wx/sizer.h>

namespace Slic3r::GUI {

wgtDeviceAmsDryIcon::wgtDeviceAmsDryIcon(wxWindow *parent) : wxPanel(parent, wxID_ANY) { CreateGui(); }

void wgtDeviceAmsDryIcon::ClearSummary()
{
    if (m_label_main && m_label_main_percent && m_label_other && m_label_other_percent) {
        m_label_main->SetLabel("");
        m_label_main_percent->SetLabel("");
        m_label_other->SetLabel("");
        m_label_other_percent->SetLabel("");
        m_panel_main_dot->Hide();
        m_panel_other_dot->Hide();
        Layout();
    }
}

void wgtDeviceAmsDryIcon::SetSummary(const wxString &main_label, const wxString &main_percent, const wxString &other_label, const wxString &other_percent)
{
    if (m_label_main && m_label_main_percent && m_label_other && m_label_other_percent) {
        m_label_main->SetLabel(main_label);
        m_label_main_percent->SetLabel(main_percent);
        m_label_other->SetLabel(other_label);
        m_label_other_percent->SetLabel(other_percent);
        m_panel_main_dot->Show();
        m_panel_other_dot->Show();
        Layout();
    }
}

void wgtDeviceAmsDryIcon::CreateGui()
{
    SetBackgroundColour(*wxWHITE);
    SetMinSize(wxSize(FromDIP(92), FromDIP(160)));

    wxBoxSizer *root = new wxBoxSizer(wxVERTICAL);
    root->AddSpacer(FromDIP(12));

    m_label_main = new Label(this, "5&7");
    m_label_main->SetFont(Label::Body_14);
    m_label_main->SetForegroundColour(wxColour("#909090"));
    root->Add(m_label_main, 0, wxBOTTOM, FromDIP(4));

    wxBoxSizer *main_row = new wxBoxSizer(wxHORIZONTAL);
    m_panel_main_dot     = new wxPanel(this, wxID_ANY);
    m_panel_main_dot->SetBackgroundColour(wxColour("#5BA8FF"));
    m_panel_main_dot->SetMinSize(wxSize(FromDIP(8), FromDIP(8)));
    m_panel_main_dot->SetMaxSize(wxSize(FromDIP(8), FromDIP(8)));
    main_row->Add(m_panel_main_dot, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));

    m_label_main_percent = new Label(this, "N/A");
    m_label_main_percent->SetFont(Label::Body_14);
    m_label_main_percent->SetForegroundColour(wxColour("#6A6A6A"));
    main_row->Add(m_label_main_percent, 0, wxALIGN_CENTER_VERTICAL);
    root->Add(main_row, 0, wxBOTTOM, FromDIP(14));

    m_label_other = new Label(this, _L("Other"));
    m_label_other->SetFont(Label::Body_14);
    m_label_other->SetForegroundColour(wxColour("#909090"));
    root->Add(m_label_other, 0, wxBOTTOM, FromDIP(4));

    wxBoxSizer *other_row = new wxBoxSizer(wxHORIZONTAL);
    m_panel_other_dot     = new wxPanel(this, wxID_ANY);
    m_panel_other_dot->SetBackgroundColour(wxColour("#5BA8FF"));
    m_panel_other_dot->SetMinSize(wxSize(FromDIP(8), FromDIP(8)));
    m_panel_other_dot->SetMaxSize(wxSize(FromDIP(8), FromDIP(8)));
    other_row->Add(m_panel_other_dot, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));

    m_label_other_percent = new Label(this, "N/A");
    m_label_other_percent->SetFont(Label::Body_14);
    m_label_other_percent->SetForegroundColour(wxColour("#6A6A6A"));
    other_row->Add(m_label_other_percent, 0, wxALIGN_CENTER_VERTICAL);
    root->Add(other_row, 0);

    root->AddStretchSpacer(1);
    root->AddSpacer(FromDIP(12));
    SetSizer(root);
}

} // namespace Slic3r::GUI
