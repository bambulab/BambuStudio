#include "PublishDialog.hpp"
#include "GUI_App.hpp"

#include <wx/wx.h> 
#include <wx/sizer.h>
#include <wx/statbox.h>
#include "wx/evtloop.h"

#include "libslic3r/Model.hpp"
#include "libslic3r/Polygon.hpp"
#include "MainFrame.hpp"
#include "GUI_App.hpp"


namespace Slic3r {
namespace GUI {

static wxString PUBLISH_STEP_STRING[STEP_COUNT] = {
    _L("Slice all plate to obtain time and filament estimation"),
    _L("Packing project data into 3mf file"),
    _L("Uploading 3mf"),
    _L("Jump to model publish web page")
};

static wxString NOTE_STRING = _L("Note: The preparation may takes several minutes. Please be patiant.");

PublishDialog::PublishDialog(Plater *plater)
    //: ProgressDialog(_L("Publish"), "", 100, static_cast<wxWindow *>(wxGetApp().mainframe))
    : DPIDialog(static_cast<wxWindow *>(wxGetApp().mainframe), wxID_ANY, _L("Publish"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
    , m_plater(plater)
{
    this->SetSizeHints(wxDefaultSize, wxDefaultSize);

    wxBoxSizer *m_main_sizer;
    m_main_sizer = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer *m_top_sizer = create_publish_step_sizer();

    m_main_sizer->Add(m_top_sizer, 1, wxEXPAND, 5);

    m_text_note = new wxStaticText(this, wxID_ANY, NOTE_STRING, wxDefaultPosition, wxDefaultSize, 0);
    m_text_note->Wrap(-1);
    m_main_sizer->Add(m_text_note, 0, wxALL | wxEXPAND, 5);

    m_text_progress = new wxStaticText(this, wxID_ANY, _L(""), wxDefaultPosition, wxDefaultSize, 0);
    m_text_progress->Wrap(-1);
    m_main_sizer->Add(m_text_progress, 0, wxALL | wxEXPAND, 5);

    wxBoxSizer *m_progress_sizer;
    m_progress_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_progress = new wxGauge(this, wxID_ANY, 100, wxDefaultPosition, wxDefaultSize, wxGA_HORIZONTAL);
    m_progress_sizer->Add(m_progress, 1, wxALIGN_CENTER_VERTICAL | wxALL, 5);

    m_btn_cancel = new wxButton(this, wxID_ANY, _L("Cancel"), wxDefaultPosition, wxDefaultSize, 0);
    m_progress_sizer->Add(m_btn_cancel, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);

    m_main_sizer->Add(m_progress_sizer, 0, wxEXPAND, 5);

    wxBoxSizer *m_bottom_sizer;
    m_bottom_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_text_errors = new wxStaticText(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, 0);
    m_text_errors->Wrap(-1);
    m_bottom_sizer->Add(m_text_errors, 1, wxALL, 5);

    m_main_sizer->Add(m_bottom_sizer, 0, wxEXPAND, 5);

    this->SetSizer(m_main_sizer);
    this->Layout();

    this->Centre(wxBOTH);

    m_btn_cancel->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            this->cancel();
        });

    Bind(wxEVT_CLOSE_WINDOW, &PublishDialog::on_close, this);
}

void PublishDialog::cancel()
{
    m_was_cancelled = true;
    m_btn_cancel->Enable(false);
    m_text_progress->SetLabelText(_L("Publish was cancelled"));
    wxCloseEvent evt;
    this->on_close(evt);
}

void PublishDialog::start_slicing()
{
    SetPublishStep(PublishStep::STEP_SLICING);

    wxCommandEvent *evt = new wxCommandEvent(EVT_PUBLISH);
    evt->SetInt(EVT_PUBLISHING_START);
    wxQueueEvent(m_plater, evt);
}

bool PublishDialog::UpdateStatus(wxString &msg, int percent, bool yield)
{
    if (m_was_cancelled) return false;

    if (percent >= 0)
        m_progress->SetValue(percent);
    m_text_progress->SetLabelText(msg);

    if (yield)
        wxEventLoopBase::GetActive()->YieldFor(wxEVT_CATEGORY_UI | wxEVT_CATEGORY_USER_INPUT);
    return true;
}

void PublishDialog::Pulse(wxString &msg, bool &skip)
{
    if (!msg.IsEmpty())
        m_text_progress->SetLabelText(msg);
    wxEventLoopBase::GetActive()->YieldFor(wxEVT_CATEGORY_UI | wxEVT_CATEGORY_USER_INPUT);
    skip = m_was_cancelled;
}

void PublishDialog::SetPublishStep(PublishStep step, bool yield, int percent)
{
    m_publish_steps->SelectItem((int)step);
    if (step == PublishStep::STEP_SLICING) {
        m_text_progress->SetLabelText(_L("Slicing Plate 1"));
        if (percent > 0)
            m_progress->SetValue(percent);
        else
            m_progress->SetValue(0);
    } else if (step == PublishStep::STEP_PACKING) {
        m_text_progress->SetLabelText(_L("Packing data to 3mf"));
        if (percent > 0)
            m_progress->SetValue(percent);
        else
            m_progress->SetValue(70);
    } else if (step == PublishStep::STEP_UPLOADING) {
        m_text_progress->SetLabelText(_L("Packing data to 3mf"));
        if (percent > 0)
            m_progress->SetValue(percent);
        else
            m_progress->SetValue(85);
    } else if (step == PublishStep::STEP_FILL_INFO) {
        m_text_progress->SetLabelText(_L("Jump to webpage"));
        m_progress->SetValue(95);
    }

    if (yield)
        wxEventLoopBase::GetActive()->YieldFor(wxEVT_CATEGORY_UI | wxEVT_CATEGORY_USER_INPUT);
}

wxBoxSizer *PublishDialog::create_publish_step_sizer()
{
    auto sizer = new wxBoxSizer(wxHORIZONTAL);

    sizer->Add(10, 0, 0, wxEXPAND, 0);

    m_publish_steps = new StepIndicator(this, wxID_ANY);

    for (int i = 0; i < (int) PublishStep::STEP_PUBLISH_COUNT; i++) {
        m_publish_steps->AppendItem(PUBLISH_STEP_STRING[i]);
    }

    sizer->Add(m_publish_steps, 1, wxALL | wxEXPAND, 5);

    sizer->Add(10, 0, 0, wxEXPAND, 0);

    return sizer;
}

void PublishDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    Fit();
    Refresh();
}

void PublishDialog::on_close(wxCloseEvent &event)
{
    wxCommandEvent *evt = new wxCommandEvent(EVT_PUBLISH);
    evt->SetInt(EVT_PUBLISHING_STOP);
    wxQueueEvent(m_plater, evt);
}

void PublishDialog::reset()
{
    m_btn_cancel->Enable();
    m_was_cancelled = false;
    SetPublishStep(PublishStep::STEP_SLICING);
    m_text_errors->Hide();
}


} // GUI
} // Slic3r
