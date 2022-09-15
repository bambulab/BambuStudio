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


static const wxColour TEXT_LIGHT_GRAY = wxColour(107, 107, 107);

namespace Slic3r {
namespace GUI {

wxDEFINE_EVENT(EVT_PUBLISH_JOB_CANCEL, wxCommandEvent);

static wxString PUBLISH_STEP_STRING[STEP_COUNT] = {
    _L("Slice all plate to obtain time and filament estimation"),
    _L("Packing project data into 3mf file"),
    _L("Uploading 3mf"),
    _L("Jump to model publish web page")
};

static wxString NOTE_STRING = _L("Note: The preparation may takes several minutes. Please be patiant.");

PublishDialog::PublishDialog(Plater *plater)
    : DPIDialog(static_cast<wxWindow *>(wxGetApp().mainframe), wxID_ANY, _L("Publish"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
    , m_plater(plater)
{
    this->SetSize(wxSize(FromDIP(540),FromDIP(400)));

    this->SetSizeHints(wxDefaultSize, wxDefaultSize);

    wxBoxSizer *top_sizer = new wxBoxSizer(wxHORIZONTAL);

    wxBoxSizer *m_main_sizer = new wxBoxSizer(wxVERTICAL);

    m_main_sizer->Add(0, FromDIP(30), 0, wxEXPAND, 0);

    m_step_panel = new wxPanel(this, wxID_ANY);
    wxBoxSizer *step_sizer = create_publish_step_sizer();
    m_step_panel->SetSizer(step_sizer);
    m_step_panel->SetBackgroundColour(wxColour(248, 248, 248));

    m_main_sizer->Add(m_step_panel, 1, wxEXPAND, 0);

    m_main_sizer->Add(0, FromDIP(20), 0, wxEXPAND, 0);

    m_text_note = new wxStaticText(this, wxID_ANY, NOTE_STRING, wxDefaultPosition, wxDefaultSize, 0);
    m_text_note->SetFont(Label::Body_14);
    m_text_note->SetForegroundColour(TEXT_LIGHT_GRAY);
    m_text_note->Wrap(-1);
    m_main_sizer->Add(m_text_note, 0, wxALL | wxEXPAND, 0);
    m_main_sizer->Add(0, FromDIP(10), 0, wxEXPAND, 0);

    wxStaticLine *m_staticline = new wxStaticLine(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL);
    m_main_sizer->Add(m_staticline, 0, wxALL | wxEXPAND, FromDIP(0));
    m_main_sizer->Add(0, FromDIP(10), 0, wxEXPAND, 0);

    wxBoxSizer *m_progress_text_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_text_progress = new wxStaticText(this, wxID_ANY, _L(""), wxDefaultPosition, wxDefaultSize, 0);
    m_text_progress->Wrap(-1);
    m_text_progress->SetFont(Label::Body_12);
    m_text_progress->SetForegroundColour(TEXT_LIGHT_GRAY);
    
    m_progress_text_sizer->Add(FromDIP(20), 0, 0, wxEXPAND | wxALL, 0);
    m_progress_text_sizer->Add(m_text_progress, 1, wxALL | wxEXPAND, 0);
    m_main_sizer->Add(m_progress_text_sizer, 0, wxALL | wxEXPAND, 0);

    wxBoxSizer *m_progress_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_progress_sizer->Add(FromDIP(20), 0, 0, wxEXPAND | wxALL, 0);

    m_status_bar = std::make_shared<BBLStatusBarSend>(this);
    m_progress_sizer->Add(m_status_bar->get_panel(), 1, wxALIGN_CENTER_VERTICAL | wxALL, 0);
    
    m_progress_sizer->Add(FromDIP(20), 0, 0, wxEXPAND, 0);

    m_btn_cancel = new Button(this, _L("Cancel"));
    m_progress_sizer->Add(m_btn_cancel, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));
    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed),
                            std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
                            std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal));
    StateColor text_color(std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Pressed),
                          std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Hovered),
                          std::pair<wxColour, int>(TEXT_LIGHT_GRAY, StateColor::Normal));
    m_btn_cancel->SetFont(Label::Body_12);
    m_btn_cancel->SetBackgroundColor(btn_bg_green);
    m_btn_cancel->SetBorderColor(wxColour(0, 174, 66));
    m_btn_cancel->SetTextColor(text_color);
    m_btn_cancel->SetSize(wxSize(FromDIP(60), FromDIP(20)));
    m_btn_cancel->SetMinSize(wxSize(FromDIP(60), FromDIP(20)));
    m_btn_cancel->SetCornerRadius(FromDIP(10));
    m_btn_cancel->Hide();

    m_progress_sizer->Add(FromDIP(20), 0, 0, wxEXPAND | wxALL, 0);

    m_main_sizer->Add(m_progress_sizer, 0, wxEXPAND, FromDIP(0));

    wxBoxSizer *m_bottom_sizer;
    m_bottom_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_bottom_sizer->Add(FromDIP(20), 0, 0, wxEXPAND, 0);

    m_text_errors = new wxStaticText(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, 0);
    m_text_errors->Wrap(-1);
    m_text_errors->SetFont(Label::Body_12);
    m_text_errors->SetForegroundColour(wxColour(255, 111, 0));
    m_bottom_sizer->Add(m_text_errors, 1, wxALL, 0);

    m_main_sizer->Add(m_bottom_sizer, 0, wxEXPAND, 0);
    m_main_sizer->Add(0, FromDIP(10), 0, wxEXPAND, 0);

    top_sizer->Add(FromDIP(30), 0, 0, wxEXPAND, 0);
    top_sizer->Add(m_main_sizer, 1, wxALL | wxEXPAND, 0);
    top_sizer->Add(FromDIP(30), 0, 0, wxEXPAND, 0);

    this->SetSizer(top_sizer);
    this->Layout();

    this->Centre(wxBOTH);

    Bind(EVT_PUBLISH_JOB_CANCEL, &PublishDialog::on_publish_job_cancel, this);

    Bind(wxEVT_CLOSE_WINDOW, &PublishDialog::on_close, this);
}

void PublishDialog::on_publish_job_cancel(wxCommandEvent &evt)
{
    this->cancel();
}

void PublishDialog::cancel()
{
    m_was_cancelled = true;
    m_text_progress->SetLabelText(_L("Publish was cancelled"));
    this->EndModal(wxID_OK);
}

void PublishDialog::start_publish(PublishParams &params)
{
    m_status_bar->set_cancel_callback_fina([this]() {
        BOOST_LOG_TRIVIAL(info) << "print_job: enter canceled";
        if (m_publish_job) {
            if (m_publish_job->is_running()) {
                BOOST_LOG_TRIVIAL(info) << "print_job: canceled";
                m_publish_job->cancel();
            }
            m_publish_job->join();
        }
        m_was_cancelled = true;
        wxCommandEvent* event = new wxCommandEvent(EVT_PUBLISH_JOB_CANCEL);
        wxQueueEvent(this, event);
        });

    m_publish_job = std::make_shared<PublishJob>(m_status_bar, m_plater);

    m_publish_job->publish_params.project_3mf_file = params.project_3mf_file;
    m_publish_job->publish_params.preset_name = params.preset_name;
    m_publish_job->publish_params.project_model_id = params.project_model_id;
    m_publish_job->publish_params.project_name = params.project_name;

    m_publish_job->start();
    BOOST_LOG_TRIVIAL(info) << "publish_job: start publish job";
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

    if (percent >= 0) {
        m_status_bar->set_progress(percent);
    }
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
        if (percent > 0) {
            m_status_bar->set_progress(percent);
        } else {
            m_status_bar->set_progress(0);
        }
    } else if (step == PublishStep::STEP_PACKING) {
        m_text_progress->SetLabelText(_L("Packing data to 3mf"));
        if (percent > 0)
            m_status_bar->set_progress(percent);
        else
            m_status_bar->set_progress(75);
    } else if (step == PublishStep::STEP_UPLOADING) {
        m_text_progress->SetLabelText(_L("Packing data to 3mf"));
        if (percent > 0)
            m_status_bar->set_progress(percent);
        else
            m_status_bar->set_progress(85);
    } else if (step == PublishStep::STEP_FILL_INFO) {
        m_text_progress->SetLabelText(_L("Jump to webpage"));
        m_status_bar->set_progress(100);
    }

    if (yield)
        wxEventLoopBase::GetActive()->YieldFor(wxEVT_CATEGORY_UI | wxEVT_CATEGORY_USER_INPUT);
}

wxBoxSizer *PublishDialog::create_publish_step_sizer()
{
    auto sizer = new wxBoxSizer(wxHORIZONTAL);

    sizer->Add(FromDIP(30), 0, 0, wxEXPAND, 0);

    auto middle_sizer = new wxBoxSizer(wxVERTICAL);

    m_publish_steps = new StepIndicator(m_step_panel, wxID_ANY);
    StateColor bg_color(std::pair<wxColour, int>(wxColour(248, 248, 248), StateColor::Normal));
    m_publish_steps->SetBackgroundColor(bg_color);
    m_publish_steps->SetFont(Label::Body_14);

    for (int i = 0; i < (int) PublishStep::STEP_PUBLISH_COUNT; i++) {
        m_publish_steps->AppendItem(PUBLISH_STEP_STRING[i]);
    }

    middle_sizer->Add(0, FromDIP(30), 0, wxEXPAND, 0);
    middle_sizer->Add(m_publish_steps, 1, wxALL | wxEXPAND, 0);
    middle_sizer->Add(0, FromDIP(30), 0, wxEXPAND, 0);

    sizer->Add(middle_sizer, 1, wxALL | wxEXPAND, 0);

    sizer->Add(FromDIP(30), 0, 0, wxEXPAND, 0);

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
