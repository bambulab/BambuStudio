#include "BBLStatusBarSend.hpp"

#include <wx/timer.h>
#include <wx/gauge.h>
#include <wx/button.h>
#include <wx/statusbr.h>
#include <wx/frame.h>

#include "GUI_App.hpp"

#include "I18N.hpp"

#include <iostream>

namespace Slic3r {

BBLStatusBarSend::BBLStatusBarSend(wxWindow *parent, int id)
 : m_self{new wxPanel(parent, id == -1 ? wxID_ANY : id)} 
    , m_sizer(new wxBoxSizer(wxVERTICAL))
{
    m_self->SetBackgroundColour(wxColour(255,255,255));
    wxBoxSizer *m_sizer_tline = new wxBoxSizer(wxHORIZONTAL);

    m_status_text = new wxStaticText(m_self, wxID_ANY, L(""), wxDefaultPosition, wxDefaultSize, 0);
    m_status_text->SetForegroundColour(wxColour(107, 107, 107));
    m_status_text->SetFont(::Label::Body_14);
    m_status_text->Wrap(-1);
    m_sizer_tline->Add(m_status_text, 0, wxEXPAND, 0);

    m_sizer_tline->Add(0, 0, 1, wxEXPAND, 0);

    m_stext_percent = new wxStaticText(m_self, wxID_ANY, L(""), wxDefaultPosition, wxDefaultSize, 0);
    m_stext_percent->SetForegroundColour(wxColour(107, 107, 107));
    m_stext_percent->SetFont(::Label::Body_14);
    m_stext_percent->Wrap(-1);
    m_sizer_tline->Add(m_stext_percent, 0, wxEXPAND | wxRIGHT, 0);

    m_sizer_tline->Add(0, 0, 0, wxEXPAND | wxRIGHT, 0);

    m_sizer->Add(m_sizer_tline, 0, wxEXPAND, 0);

    m_sizer_eline = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *m_sizer_gauge = new wxBoxSizer(wxVERTICAL);

    m_prog = new wxGauge(m_self, wxID_ANY, 100, wxDefaultPosition, wxSize(-1, 6), wxGA_HORIZONTAL);
    m_prog->SetValue(0);
    m_sizer_gauge->Add(m_prog, 0, wxBOTTOM | wxEXPAND | wxTOP, 9);
    m_sizer_eline->Add(m_sizer_gauge, 1, wxEXPAND, 5);
    m_sizer_eline->Add(0, 0, 0, wxEXPAND | wxRIGHT, 30);

    m_cancelbutton = new Button(m_self, _L("Cancel"));
    m_cancelbutton->SetMinSize(wxSize(65, 24));
    m_cancelbutton->SetTextColor(wxColour(107, 107, 107));
    m_cancelbutton->SetBackgroundColor(wxColour(255, 255, 255));
    m_cancelbutton->SetCornerRadius(12);
    m_sizer_eline->Add(m_cancelbutton, 0, wxEXPAND, 0);

    m_cancelbutton->Bind(wxEVT_BUTTON, [this](const wxCommandEvent &) {
        if (m_cancel_cb_fina) 
            m_cancel_cb_fina();
    });

    m_sizer->Add(m_sizer_eline, 0, wxEXPAND, 0);
    m_self->SetSizer(m_sizer);
    m_self->Layout();
    m_sizer->Fit(m_self);
}

void BBLStatusBarSend::set_prog_block()
{
    auto block_left = new wxWindow(m_prog, wxID_ANY, wxPoint(0, 0), wxSize(2, m_prog->GetSize().GetHeight()));
    block_left->SetBackgroundColour(wxColour(255, 255, 255));
    auto block_right = new wxWindow(m_prog, wxID_ANY, wxPoint(m_prog->GetSize().GetWidth() - 2, 0), wxSize(2, m_prog->GetSize().GetHeight()));
    block_right->SetBackgroundColour(wxColour(255, 255, 255));
}

int BBLStatusBarSend::get_progress() const
{
    return m_prog->GetValue();
}

void BBLStatusBarSend::set_progress(int val)
{
    if(val < 0)
        return;

    bool need_layout = false;
    //add the logic for arrange/orient jobs, which don't call stop_busy
    if(val == m_prog->GetRange()) {
        m_prog->SetValue(0);
        m_sizer->Hide(m_prog);
        need_layout = true;
    }
    else
    {
        if (!m_sizer->IsShown(m_prog)) {
            m_sizer->Show(m_prog);
            m_sizer->Show(m_cancelbutton);
            need_layout = true;
        }
        m_prog->SetValue(val);
    }

    if (need_layout) {
        m_sizer->Layout();
    }
}

int BBLStatusBarSend::get_range() const
{
    return m_prog->GetRange();
}

void BBLStatusBarSend::set_range(int val)
{
    if(val != m_prog->GetRange()) {
        m_prog->SetRange(val);
    }
}

void BBLStatusBarSend::show_progress(bool show)
{
    if (show) {
        m_sizer->Show(m_prog);
        m_sizer->Layout();
    }
    else {
        m_sizer->Hide(m_prog);
        m_sizer->Layout();
    }
}

void BBLStatusBarSend::start_busy(int rate)
{
    m_busy = true;
    show_progress(true);
    show_cancel_button();
}

void BBLStatusBarSend::stop_busy()
{
    show_progress(false);
    hide_cancel_button();
    m_prog->SetValue(0);
    m_sizer->Layout();
    m_busy = false;
}

void BBLStatusBarSend::set_cancel_callback_fina(BBLStatusBarSend::CancelFn ccb) 
{ 
    m_cancel_cb_fina = ccb; 
     if (ccb) {
        m_sizer->Show(m_cancelbutton);
    } else {
        m_sizer->Hide(m_cancelbutton);
    }
}

void BBLStatusBarSend::set_cancel_callback(BBLStatusBarSend::CancelFn ccb) {
    /*  m_cancel_cb = ccb;
      if (ccb) {
          m_sizer->Show(m_cancelbutton);
      }
      else {
          m_sizer->Hide(m_cancelbutton);
      }
      m_sizer->Layout();*/
}

wxPanel* BBLStatusBarSend::get_panel()
{
    return m_self;
}

void BBLStatusBarSend::set_status_text(const wxString& txt)
{
    m_status_text->SetLabelText(txt);
}

void BBLStatusBarSend::set_status_text(const std::string& txt)
{ 
    this->set_status_text(txt.c_str());
}

void BBLStatusBarSend::set_status_text(const char *txt)
{ 
    this->set_status_text(wxString::FromUTF8(txt));
}

wxString BBLStatusBarSend::get_status_text() const
{
    return m_status_text->GetLabelText();
}

void BBLStatusBarSend::set_font(const wxFont &font)
{
    m_self->SetFont(font);
}

void BBLStatusBarSend::show_cancel_button()
{
    m_sizer->Show(m_cancelbutton);
    m_sizer->Layout();
}

void BBLStatusBarSend::hide_cancel_button()
{
    m_sizer->Hide(m_cancelbutton);
    m_sizer->Layout();
}

}
