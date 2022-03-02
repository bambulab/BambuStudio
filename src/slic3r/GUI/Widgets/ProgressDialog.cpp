#include "wx/wxprec.h"

#ifndef WX_PRECOMP
#include "wx/utils.h"
#include "wx/frame.h"
#include "wx/button.h"
#include "wx/stattext.h"
#include "wx/sizer.h"
#include "wx/event.h"
#include "wx/gauge.h"
#include "wx/intl.h"
#include "wx/dcclient.h"
#include "wx/timer.h"
#include "wx/settings.h"
#include "wx/app.h"
#endif

#include "../GUI.hpp"
#include "../GUI_App.hpp"
#include "../I18N.hpp"
#include "ProgressDialog.hpp"
#include "wx/evtloop.h"

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

#define LAYOUT_MARGIN 12

static const int wxID_SKIP = 32000;

namespace Slic3r { namespace GUI {
void ProgressDialog::Init()
{
    // we may disappear at any moment, let the others know about it
    SetExtraStyle(GetExtraStyle() | wxWS_EX_TRANSIENT);

    // Initialize all our members that we always use (even when we don't
    // create a valid window in this class).

    m_pdStyle   = 0;
    m_parentTop = NULL;

    m_gauge   = NULL;
    m_msg     = NULL;
    m_elapsed = m_estimated = m_remaining = NULL;

    m_state   = Uncancelable;
    m_maximum = 0;

    m_timeStart = wxGetCurrentTime();
    m_timeStop  = (unsigned long) -1;
    m_break     = 0;

    m_skip = false;

    m_btnAbort = NULL;
    m_btnSkip  = NULL;

    m_display_estimated = m_last_timeupdate = m_ctdelay = 0;

    m_delay = 3;

    m_winDisabler   = NULL;
    m_tempEventLoop = NULL;

    SetWindowStyle(wxDEFAULT_DIALOG_STYLE);
}

ProgressDialog::ProgressDialog() : wxDialog() { Init(); }

ProgressDialog::ProgressDialog(const wxString &title, const wxString &message, int maximum, wxWindow *parent, int style) : wxDialog()
{
    Init();
    Create(title, message, maximum, parent, style);
    Bind(wxEVT_PAINT, &ProgressDialog::OnPaint, this);
    Bind(wxEVT_CLOSE_WINDOW, &ProgressDialog::OnClose, this);
}

void ProgressDialog::OnPaint(wxPaintEvent &evt)
{
    /*wxPaintDC dc(this);
    dc.SetPen(wxPen(wxColor(166, 169, 170), 1.5, wxSOLID));
    dc.SetBrush(wxBrush(wxColor(166, 169, 170), wxSOLID));
    dc.DrawLine(0, 0, GetSize().GetWidth(), 0);*/
}

void ProgressDialog::SetTopParent(wxWindow *parent)
{
    m_parent    = parent;
    m_parentTop = parent;
}

wxString ProgressDialog::FormatString(wxString title)
{
    auto limit_width   = m_msg->GetSize().GetWidth();
    auto current_width = 0;

    m_mode = 0;
    for (int x = 0; x < title.length(); x++) {
        current_width += GetTextExtent(title[x]).GetWidth();
        if (current_width >= limit_width && (title.length() - x) > 10) {
            m_mode = 1;

           /* auto flag_set = x;
            for (int i = 20; i >= 0; i--) {
                if (title[flag_set] == (0x20)) {
                    title.insert(x - (20 - i), "\n");
                    break;
                }
                if (title[i] == (0x2c)) {
                    title.insert(x - (20 - i), "\n");
                    break;
                }
                if (title[i] == (0x2e)) {
                    title.insert(x - (20 - i), "\n");
                    break;
                }
                if (title[i] == (0xefbc8c)) {
                    title.insert(x - (20 - i), "\n");
                    break;
                }
                if (title[i] == (0xe38082)) {
                    title.insert(x - (20 - i), "\n");
                    break;
                }

                title.insert(x, "\n");
                flag_set--;
            }*/
            title.insert(x, "\n");

            break;
        }
    }

    // set mode
    if (m_mode == 0) {
        m_msg->Show();
        m_msg_2line->Hide();
        m_msg->SetLabel(title);
    } else {
        m_msg->Hide();
        m_msg_2line->Show();
        m_msg_2line->SetLabel(title);
    }

    Fit();
    return title;
}

bool ProgressDialog::Create(const wxString &title, const wxString &message, int maximum, wxWindow *parent, int style)
{
    SetFont(wxGetApp().normal_font());
    SetTopParent(parent);

    m_pdStyle = style;

    if (!wxDialog::Create(m_parentTop, wxID_ANY, title, wxDefaultPosition, DESIGN_RESOUTION_PROGRESS_SIZE, GetWindowStyle())) return false;
    SetBackgroundColour(DESIGN_RESOUTION_DEF_BK_COLOR);

    SetSize(DESIGN_RESOUTION_PROGRESS_SIZE);
    SetMinSize(DESIGN_RESOUTION_PROGRESS_SIZE);
    SetMaxSize(DESIGN_RESOUTION_PROGRESS_SIZE);

    SetMaximum(maximum);
    EnsureActiveEventLoopExists();

#if defined(__WXMSW__) && !defined(__WXUNIVERSAL__)
    if (!HasPDFlag(wxPD_CAN_ABORT)) { EnableCloseButton(false); }
#endif // wxMSW

    m_state = HasPDFlag(wxPD_CAN_ABORT) ? Continue : Uncancelable;

    // top-level sizerTop
    sizerTop = new wxBoxSizer(wxVERTICAL);

    // def width
    auto def_size_width = wxMin(wxGetClientDisplayRect().width / 3, 305);

    // top line
    wxStaticLine *m_staticline2 = new wxStaticLine(this, wxID_ANY, wxDefaultPosition, wxSize(def_size_width, 1), wxLI_HORIZONTAL);
    sizerTop->Add(m_staticline2, 0, wxEXPAND, 0);
    auto m_block_top = new wxWindow(this, wxID_ANY, wxDefaultPosition, wxSize(def_size_width, 20));
    m_block_top->SetBackgroundColour(wxColor(DESIGN_RESOUTION_DEF_BK_COLOR));
    sizerTop->Add(m_block_top, 0, wxEXPAND);

    m_message_area = new wxWindow(this, wxID_ANY, wxDefaultPosition, wxSize(def_size_width, 38));
    m_message_area->SetBackgroundColour(DESIGN_RESOUTION_DEF_BK_COLOR);
    sizerTop->Add(m_message_area, 0, wxLEFT | wxRIGHT | wxBottom | wxEXPAND, 2 * LAYOUT_MARGIN);

    // 1 line mode
    m_msg = new wxStaticText(m_message_area, wxID_ANY, wxString(""), wxPoint(0, 10), wxSize(def_size_width, 20), wxST_ELLIPSIZE_END);
    m_msg->SetForegroundColour(wxColour(107, 107, 107));
    m_msg->SetBackgroundColour(DESIGN_RESOUTION_DEF_BK_COLOR);
    m_msg->SetMinSize(wxSize(def_size_width, 20));
    m_msg->SetMaxSize(wxSize(def_size_width, 20));
    // sizerTop->Add(m_msg, 0, wxLEFT | wxRIGHT, 2 * LAYOUT_MARGIN);

    /* m_block_text_bottom = new wxWindow(this, wxID_ANY, wxDefaultPosition, wxSize(def_size_width, 10));
      sizerTop->Add(m_block_text_bottom, 0, wxEXPAND);
      m_block_text_bottom->SetBackgroundColour(wxColor(DESIGN_RESOUTION_DEF_BK_COLOR));*/

    // 2 line mode
    m_msg_2line = new wxStaticText(m_message_area, wxID_ANY, wxString(""), wxPoint(0, 0), wxSize(def_size_width, 40), wxST_ELLIPSIZE_END);
    m_msg_2line->SetForegroundColour(wxColour(107, 107, 107));
    m_msg_2line->SetBackgroundColour(DESIGN_RESOUTION_DEF_BK_COLOR);
    m_msg_2line->SetMinSize(wxSize(def_size_width, 40));
    m_msg_2line->SetMaxSize(wxSize(def_size_width, 40));
    // sizerTop->Add(m_msg_2line, 0, wxLEFT | wxRIGHT, 2 * LAYOUT_MARGIN);

    // m_msg->SetSize(wxSize(305, 19));
    // m_msg_2line->SetSize(wxSize(305, 38));
    // m_msg->SetMinSize(wxSize(305, 19));
    // m_msg_2line->SetMinSize(wxSize(305, 38));

    // gauge
    int gauge_style = wxGA_HORIZONTAL;
    if (style & wxPD_SMOOTH) gauge_style |= wxGA_SMOOTH;
    gauge_style |= wxGA_PROGRESS;

#ifdef __WXMSW__
    maximum /= m_factor;
#endif

    auto m_block_gauge = new wxWindow(this, wxID_ANY, wxDefaultPosition, wxSize(0, 0));
    sizerTop->Add(m_block_gauge, 0, wxEXPAND | wxTOP, 2);


    m_gauge = new wxGauge(this, wxID_ANY, maximum, wxDefaultPosition, wxSize(def_size_width, 6), gauge_style);
    
    auto block_left = new wxWindow(m_gauge, -1, wxPoint(0, 0), wxSize(2, m_gauge->GetSize().GetHeight()));
    block_left->SetBackgroundColour(wxColor(255, 255, 255));

    auto block_right = new wxWindow(m_gauge, -1, wxPoint(m_gauge->GetSize().GetWidth() - 2, 0), wxSize(2, m_gauge->GetSize().GetHeight()));
    block_right->SetBackgroundColour(wxColor(255, 255, 255));

    sizerTop->Add(m_gauge, 0, wxLEFT | wxRIGHT | wxBottom, 2 * LAYOUT_MARGIN);
    m_gauge->SetValue(0);

    m_elapsed = m_estimated = m_remaining = NULL;
    size_t nTimeLabels                    = 0;

    wxSizer *const sizerLabels = new wxFlexGridSizer(2);

    if (style & wxPD_ELAPSED_TIME) {
        nTimeLabels++;

        m_elapsed = CreateLabel(GetElapsedLabel(), sizerLabels);
    }

    if (style & wxPD_ESTIMATED_TIME) {
        nTimeLabels++;

        m_estimated = CreateLabel(GetEstimatedLabel(), sizerLabels);
    }

    if (style & wxPD_REMAINING_TIME) {
        nTimeLabels++;

        m_remaining = CreateLabel(GetRemainingLabel(), sizerLabels);
    }
    sizerTop->Add(sizerLabels, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP | wxBottom, LAYOUT_MARGIN);

    wxStdDialogButtonSizer *buttonSizer = wxDialog::CreateStdDialogButtonSizer(0);

    const int borderFlags = wxALL;

    std::string icon_path = (boost::format("%1%/icons/common_dialog_confirm.png") % resources_dir()).str();
    /*m_btnAbort            = new wxBitmapButton(this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize(52, 24), wxBU_AUTODRAW | wxBORDER_NONE);
    m_btnAbort->SetBitmap(wxBitmap(icon_path, wxBITMAP_TYPE_ANY));
    m_btnAbort->SetBitmapDisabled(wxBitmap(icon_path, wxBITMAP_TYPE_ANY));
    m_btnAbort->SetBitmapPressed(wxBitmap(icon_path, wxBITMAP_TYPE_ANY));
    m_btnAbort->SetBitmapFocus(wxBitmap(icon_path, wxBITMAP_TYPE_ANY));
    m_btnAbort->SetBitmapCurrent(wxBitmap(icon_path, wxBITMAP_TYPE_ANY));*/

    // m_btnAbort = new wxButton(this, wxID_CANCEL, wxString(""), wxDefaultPosition, wxSize(52,24));

    // wxStaticBitmap *m_bitmatAbort = new wxStaticBitmap(m_btnAbort, wxID_ANY, wxBitmap(icon_path, wxBITMAP_TYPE_ANY), wxDefaultPosition, wxSize(52, 24), 0);
    // wxStaticText *textAbort = new wxStaticText(m_btnAbort, wxID_ANY, _T("Cancel"), wxPoint(5, 3), wxSize(42, 19));
    // textAbort->SetBackgroundColour(wxColor(0, 174, 66));
    // textAbort->SetForegroundColour(DESIGN_RESOUTION_DEF_BK_COLOR);

    // textAbort->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &event) {
    //    if (m_state == Finished) {
    //        event.Skip();
    //    } else {
    //        m_state = Canceled;
    //        //DisableAbort();
    //        DisableSkip();
    //        m_timeStop = wxGetCurrentTime();
    //    }
    //});

    // wxSizerFlags sizerFlags = wxSizerFlags().Border(borderFlags, LAYOUT_MARGIN);
    wxWindow *m_button_sizer = new wxWindow(this, wxID_ANY, wxDefaultPosition, wxSize(def_size_width, 26));
    m_button_sizer->SetBackgroundColour(DESIGN_RESOUTION_DEF_BK_COLOR);
    // sizerTop->Add(m_block_text_top, 0, wxEXPAND);

    if (HasPDFlag(wxPD_CAN_SKIP)) {
        m_btnSkip = new wxButton(this, wxID_SKIP, wxGetTranslation("&Skip"));
        m_btnSkip->Bind(wxEVT_COMMAND_BUTTON_CLICKED, &ProgressDialog::OnSkip, this);
        buttonSizer->SetNegativeButton(m_btnSkip);
    }

    if (HasPDFlag(wxPD_CAN_ABORT)) {
        /*m_btnAbort = new wxButton(this, wxID_CANCEL);
        buttonSizer->SetCancelButton(m_btnAbort);
        m_btnAbort->Bind(wxEVT_COMMAND_BUTTON_CLICKED, &ProgressDialog::OnCancel, this);*/

        m_button_calcel = new Button(m_button_sizer, _T("Cancel"));
        m_button_calcel->SetTextColor(wxColour(107, 107, 107));
        m_button_calcel->SetSize(60, 24);
        m_button_calcel->SetPosition(wxPoint(m_button_sizer->GetSize().GetWidth() - m_button_calcel->GetSize().GetWidth(), 0));

        m_button_calcel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &event) {
            if (m_state == Finished) {
                event.Skip();
            } else {
                m_state = Canceled;
                // DisableAbort();
                m_button_calcel->Enable(false);
                DisableSkip();
                m_timeStop = wxGetCurrentTime();
            }
        });
    }

    if (!HasPDFlag(wxPD_CAN_SKIP | wxPD_CAN_ABORT)) buttonSizer->AddSpacer(LAYOUT_MARGIN);

    // buttonSizer->Realize();
    // sizerTop->Add(buttonSizer, 1, wxEXPAND | wxRIGHT, 35);

    sizerTop->Add(m_button_sizer, 1, wxEXPAND, 0);

    auto m_block_bottom = new wxWindow(this, wxID_ANY, wxDefaultPosition, wxSize(def_size_width, 15));
    m_block_bottom->SetBackgroundColour(DESIGN_RESOUTION_DEF_BK_COLOR);
    sizerTop->Add(m_block_bottom, 0, wxEXPAND);

    SetSizerAndFit(sizerTop);
    FormatString(message);

    Centre(wxCENTER_FRAME | wxBOTH);

    DisableOtherWindows();

    Show();
    Enable();
    if (m_elapsed) { SetTimeLabel(0, m_elapsed); }

    Update();
    return true;
}

void ProgressDialog::UpdateTimeEstimates(int value, unsigned long &elapsedTime, unsigned long &estimatedTime, unsigned long &remainingTime)
{
    unsigned long elapsed = wxGetCurrentTime() - m_timeStart;
    if (value != 0 && (m_last_timeupdate < elapsed || value == m_maximum)) {
        m_last_timeupdate       = elapsed;
        unsigned long estimated = m_break + (unsigned long) (((double) (elapsed - m_break) * m_maximum) / ((double) value));
        if (estimated > m_display_estimated && m_ctdelay >= 0) {
            ++m_ctdelay;
        } else if (estimated < m_display_estimated && m_ctdelay <= 0) {
            --m_ctdelay;
        } else {
            m_ctdelay = 0;
        }
        if (m_ctdelay >= m_delay             // enough confirmations for a higher value
            || m_ctdelay <= (m_delay * -1)   // enough confirmations for a lower value
            || value == m_maximum            // to stay consistent
            || elapsed > m_display_estimated // to stay consistent
            || (elapsed > 0 && elapsed < 4)  // additional updates in the beginning
        ) {
            m_display_estimated = estimated;
            m_ctdelay           = 0;
        }
    }

    if (value != 0) {
        long display_remaining = m_display_estimated - elapsed;
        if (display_remaining < 0) { display_remaining = 0; }

        estimatedTime = m_display_estimated;
        remainingTime = display_remaining;
    }

    elapsedTime = elapsed;
}

// static
wxString ProgressDialog::GetFormattedTime(unsigned long timeInSec)
{
    wxString timeAsHMS;

    if (timeInSec == (unsigned long) -1) {
        timeAsHMS = wxGetTranslation("Unknown");
    } else {
        unsigned hours   = timeInSec / 3600;
        unsigned minutes = (timeInSec % 3600) / 60;
        unsigned seconds = timeInSec % 60;
        timeAsHMS.Printf("%u:%02u:%02u", hours, minutes, seconds);
    }

    return timeAsHMS;
}

void ProgressDialog::EnsureActiveEventLoopExists()
{
    if (!wxEventLoopBase::GetActive()) {
        m_tempEventLoop = new wxEventLoop;
        wxEventLoop::SetActive(m_tempEventLoop);
    }
}

wxStaticText *ProgressDialog::CreateLabel(const wxString &text, wxSizer *sizer)
{
    wxStaticText *label = new wxStaticText(this, wxID_ANY, text);
    wxStaticText *value = new wxStaticText(this, wxID_ANY, wxGetTranslation("unknown"));

    // select placement most native or nice on target GUI
#if defined(__WXMSW__) || defined(__WXMAC__) || defined(__WXGTK20__)
    // value and time centered in one row
    sizer->Add(label, 1, wxALIGN_RIGHT | wxTOP | wxRIGHT, LAYOUT_MARGIN);
    sizer->Add(value, 1, wxALIGN_LEFT | wxTOP, LAYOUT_MARGIN);
#else
    // value and time to the right in one row
    sizer->Add(label);
    sizer->Add(value, 0, wxLEFT, LAYOUT_MARGIN);
#endif

    return value;
}

// ----------------------------------------------------------------------------
// ProgressDialog operations
// ----------------------------------------------------------------------------

bool ProgressDialog::Update(int value, const wxString &newmsg, bool *skip)
{
    if (!DoBeforeUpdate(skip)) return false;

    wxCHECK_MSG(m_gauge, false, "dialog should be fully created");

#ifdef __WXMSW__
    value /= m_factor;
#endif // __WXMSW__

    wxASSERT_MSG(value <= m_maximum, wxT("invalid progress value"));
    m_gauge->SetValue(value);

    UpdateMessage(newmsg);

    if ((m_elapsed || m_remaining || m_estimated) && (value != 0)) {
        unsigned long elapsed;
        unsigned long display_remaining;

        UpdateTimeEstimates(value, elapsed, m_display_estimated, display_remaining);

        SetTimeLabel(elapsed, m_elapsed);
        SetTimeLabel(m_display_estimated, m_estimated);
        SetTimeLabel(display_remaining, m_remaining);
    }

    if (value == m_maximum) {
        if (m_state == Finished) {
            // ignore multiple calls to Update(m_maximum): it may sometimes be
            // troublesome to ensure that Update() is not called twice with the
            // same value (e.g. because of the rounding errors) and if we don't
            // return now we're going to generate asserts below
            return true;
        }

        // so that we return true below and that out [Cancel] handler knew what
        // to do
        m_state = Finished;
        if (!HasPDFlag(wxPD_AUTO_HIDE)) {
            EnableClose();
            DisableSkip();
#if defined(__WXMSW__) && !defined(__WXUNIVERSAL__)
            EnableCloseButton();
#endif // __WXMSW__

            if (newmsg.empty()) {
                // also provide the finishing message if the application didn't
                m_msg->SetLabel(wxGetTranslation("Done."));
            }

            // allow the window to repaint:
            // NOTE: since we yield only for UI events with this call, there
            //       should be no side-effects
            wxEventLoopBase::GetActive()->YieldFor(wxEVT_CATEGORY_UI);

            // NOTE: this call results in a new event loop being created
            //       and to a call to ProcessPendingEvents() (which may generate
            //       unwanted re-entrancies).
            (void) ShowModal();
        } else // auto hide
        {
            // reenable other windows before hiding this one because otherwise
            // Windows wouldn't give the focus back to the window which had
            // been previously focused because it would still be disabled
            ReenableOtherWindows();

            Hide();
        }
    } else // not at maximum yet
    {
        DoAfterUpdate();
    }

    // update the display in case yielding above didn't do it
    Update();

    return m_state != Canceled;
}

bool ProgressDialog::Pulse(const wxString &newmsg, bool *skip)
{
    if (!DoBeforeUpdate(skip)) return false;

    wxCHECK_MSG(m_gauge, false, "dialog should be fully created");

    // show a bit of progress
    m_gauge->Pulse();

    UpdateMessage(newmsg);

    if (m_elapsed || m_remaining || m_estimated) {
        unsigned long elapsed = wxGetCurrentTime() - m_timeStart;

        SetTimeLabel(elapsed, m_elapsed);
        SetTimeLabel((unsigned long) -1, m_estimated);
        SetTimeLabel((unsigned long) -1, m_remaining);
    }

    DoAfterUpdate();

    return m_state != Canceled;
}

bool ProgressDialog::DoBeforeUpdate(bool *skip)
{
    // we have to yield because not only we want to update the display but
    // also to process the clicks on the cancel and skip buttons
    // NOTE: using YieldFor() this call shouldn't give re-entrancy problems
    //       for event handlers not interested to UI/user-input events.
    wxEventLoopBase::GetActive()->YieldFor(wxEVT_CATEGORY_UI | wxEVT_CATEGORY_USER_INPUT);

    Update();

    if (m_skip && skip && !*skip) {
        *skip  = true;
        m_skip = false;
        EnableSkip();
    }

    return m_state != Canceled;
}

void ProgressDialog::DoAfterUpdate()
{
    // allow the window to repaint:
    // NOTE: since we yield only for UI events with this call, there
    //       should be no side-effects
    wxEventLoopBase::GetActive()->YieldFor(wxEVT_CATEGORY_UI);
}

void ProgressDialog::Resume()
{
    m_state   = Continue;
    m_ctdelay = m_delay; // force an update of the elapsed/estimated/remaining time
    m_break += wxGetCurrentTime() - m_timeStop;

    EnableAbort();
    m_button_calcel->Enable(true);
    EnableSkip();
    m_skip = false;
}

bool ProgressDialog::Show(bool show)
{
    // reenable other windows before hiding this one because otherwise
    // Windows wouldn't give the focus back to the window which had
    // been previously focused because it would still be disabled
    if (!show) ReenableOtherWindows();

    return wxDialog::Show(show);
}

int ProgressDialog::GetValue() const
{
    wxCHECK_MSG(m_gauge, -1, "dialog should be fully created");

    return m_gauge->GetValue();
}

int ProgressDialog::GetRange() const { return m_maximum; }

wxString ProgressDialog::GetMessage() const { return m_msg->GetLabel(); }

void ProgressDialog::SetRange(int maximum)
{
    wxCHECK_RET(m_gauge, "dialog should be fully created");

    wxCHECK_RET(maximum > 0, "Invalid range");

    m_gauge->SetRange(maximum);

    SetMaximum(maximum);
}

void ProgressDialog::SetMaximum(int maximum)
{
    m_maximum = maximum;

#if defined(__WXMSW__)
    // we can't have values > 65,536 in the progress control under Windows, so
    // scale everything down
    m_factor = m_maximum / 65536 + 1;
    m_maximum /= m_factor;
#endif // __WXMSW__
}

bool ProgressDialog::WasCancelled() const { return HasPDFlag(wxPD_CAN_ABORT) && m_state == Canceled; }

bool ProgressDialog::WasSkipped() const { return HasPDFlag(wxPD_CAN_SKIP) && m_skip; }

// static
void ProgressDialog::SetTimeLabel(unsigned long val, wxStaticText *label)
{
    if (label) {
        wxString s;

        if (val != (unsigned long) -1) {
            s = GetFormattedTime(val);
        } else {
            s = wxGetTranslation("Unknown");
        }

        if (s != label->GetLabel()) label->SetLabel(s);
    }
}

// ----------------------------------------------------------------------------
// event handlers
// ----------------------------------------------------------------------------

void ProgressDialog::OnCancel(wxCommandEvent &event)
{
    if (m_state == Finished) {
        // this means that the count down is already finished and we're being
        // shown as a modal dialog - so just let the default handler do the job
        event.Skip();
    } else {
        // request to cancel was received, the next time Update() is called we
        // will handle it
        m_state = Canceled;

        // update the buttons state immediately so that the user knows that the
        // request has been noticed
        DisableAbort();
        DisableSkip();

        // save the time when the dialog was stopped
        m_timeStop = wxGetCurrentTime();
    }
}

void ProgressDialog::OnSkip(wxCommandEvent &WXUNUSED(event))
{
    DisableSkip();
    m_skip = true;
}

void ProgressDialog::OnClose(wxCloseEvent &event)
{
    if (m_state == Uncancelable) {
        // can't close this dialog
        event.Veto();
    } else if (m_state == Finished) {
        // let the default handler close the window as we already terminated
        event.Skip();
    } else {
        // next Update() will notice it
        m_state = Canceled;
        DisableAbort();
        DisableSkip();

        m_timeStop = wxGetCurrentTime();
    }
}

// ----------------------------------------------------------------------------
// destruction
// ----------------------------------------------------------------------------

ProgressDialog::~ProgressDialog()
{
    // normally this should have been already done, but just in case
    ReenableOtherWindows();

    if (m_tempEventLoop) {
        // If another event loop has been installed as active during the life
        // time of this object, we shouldn't deactivate it, but we also can't
        // delete our m_tempEventLoop in this case because it risks leaving the
        // new event loop with a dangling pointer, which it will set back as
        // the active loop when it exits, resulting in a crash. So we have no
        // choice but to just leak this pointer then, which is, of course, bad
        // and usually easily avoidable by just destroying the progress dialog
        // sooner, so warn the programmer about it.
        wxCHECK_RET(wxEventLoopBase::GetActive() == m_tempEventLoop, "current event loop must not be changed during "
                                                                     "ProgressDialog lifetime");

        wxEventLoopBase::SetActive(NULL);
        delete m_tempEventLoop;
    }
}

void ProgressDialog::DisableOtherWindows()
{
    if (HasPDFlag(wxPD_APP_MODAL)) {
        m_winDisabler = new wxWindowDisabler(this);
    } else {
        if (m_parentTop) m_parentTop->Disable();
        m_winDisabler = NULL;
    }
}

void ProgressDialog::ReenableOtherWindows()
{
    if (HasPDFlag(wxPD_APP_MODAL)) {
        wxDELETE(m_winDisabler);
    } else {
        if (m_parentTop) m_parentTop->Enable();
    }
}

// ----------------------------------------------------------------------------
// private functions
// ----------------------------------------------------------------------------

void ProgressDialog::EnableSkip(bool enable)
{
    if (HasPDFlag(wxPD_CAN_SKIP)) {
        if (m_btnSkip) m_btnSkip->Enable(enable);
    }
}

void ProgressDialog::EnableAbort(bool enable)
{
    if (HasPDFlag(wxPD_CAN_ABORT)) {
        if (m_btnAbort) m_btnAbort->Enable(enable);
    }
}

void ProgressDialog::EnableClose()
{
    if (HasPDFlag(wxPD_CAN_ABORT)) {
        if (m_btnAbort) {
            m_btnAbort->Enable();
            m_btnAbort->SetLabel(wxGetTranslation("Close"));
        }
    }
}

void ProgressDialog::UpdateMessage(const wxString &newmsg)
{
    if (!newmsg.empty() && newmsg != m_msg->GetLabel()) {
        const wxSize sizeOld = m_msg->GetSize();

        FormatString(newmsg);
        // Fit();
        // m_msg->SetLabel(newmsg);

        if (m_msg->GetSize().x > sizeOld.x) {
            // Resize the dialog to fit its new, longer contents instead of
            // just truncating it.
            Fit();
        }

        // allow the window to repaint:
        // NOTE: since we yield only for UI events with this call, there
        //       should be no side-effects
        wxEventLoopBase::GetActive()->YieldFor(wxEVT_CATEGORY_UI);
    }
}
}} // namespace Slic3r::GUI
