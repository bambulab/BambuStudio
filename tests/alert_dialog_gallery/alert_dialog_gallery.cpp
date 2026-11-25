// Interactive gallery for AlertDialog: combine every option, open the dialog, check the result.
// Not an automated test; build the target explicitly and run it.

#include <wx/app.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dcmemory.h>
#include <wx/frame.h>
#include <wx/gauge.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/timer.h>
#include <wx/windowptr.h>

#include <boost/filesystem.hpp>

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/AlertDialog.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"
#include "slic3r/GUI/Widgets/StateColor.hpp"
#ifdef _WIN32
#include <windows.h>
#include "slic3r/GUI/dark_mode.hpp"
#endif

using namespace Slic3r;
using namespace Slic3r::GUI;

namespace {

wxString utf8(const char *text) { return wxString::FromUTF8(text); }

const char *MESSAGES[] = {
    "",
    "Are you sure you want to cancel printing?",
    u8"这里是正文内容这里是正文内容这里是正文内容这里是正文内容这里是正文内容这里是正文内容这里是正文内容这里是正文内容这里是正文内容这里是正文内容这里是正文内容",
    "The selected filament is not compatible with the current nozzle. Printing with this combination may "
    "clog the nozzle or damage the hotend. Please check the filament settings before continuing.",
};

const char *TIPS[] = {
    u8"Tips：这里是备注内容这里是备注内容这里是备注内容",
    "Tips: the setting can be changed later in Preferences.",
    "Tips: a third tip with a much longer text that needs to wrap onto a second line to check the note spacing.",
};

const char *WARNINGS[] = {
    u8"可能出现的提示提示提示提示提示提示提示提示提示提示",
    "The AMS slot is empty.",
    "A long warning line that wraps onto another line so the icon alignment against multi-line text can be checked.",
};

const char *SECONDARY_LABELS[] = {"Cancel", "Later", "Learn more"};

wxString very_long_message()
{
    wxString text;
    for (int i = 1; i <= 30; ++i)
        text += wxString::Format("Line %d of a very long message that must scroll instead of growing the dialog.\n", i);
    return text;
}

// Grey placeholder with its aspect ratio written in the middle
wxBitmap placeholder(int width, int height)
{
    wxBitmap   bmp(width, height);
    wxMemoryDC dc(bmp);
    dc.SetBackground(wxBrush(ThemeColor::Grey350));
    dc.Clear();
    dc.SetTextForeground(ThemeColor::TextMuted);
    const wxString text = wxString::Format("%d x %d", width, height);
    const wxSize   ext  = dc.GetTextExtent(text);
    dc.DrawText(text, (width - ext.x) / 2, (height - ext.y) / 2);
    dc.SelectObject(wxNullBitmap);
    return bmp;
}

class GalleryFrame : public wxFrame
{
public:
    GalleryFrame() : wxFrame(nullptr, wxID_ANY, "AlertDialog gallery", wxDefaultPosition, wxDefaultSize)
    {
        auto *panel = new wxPanel(this);
        auto *grid  = new wxFlexGridSizer(2, wxSize(12, 8));
        grid->AddGrowableCol(1);

        auto row = [&](const wxString &label, wxWindow *ctrl) {
            grid->Add(new wxStaticText(panel, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
            grid->Add(ctrl, 1, wxEXPAND);
        };

        m_title = new wxTextCtrl(panel, wxID_ANY, utf8(u8"弹窗标题"));
        row("Title", m_title);

        m_message = new wxChoice(panel, wxID_ANY);
        m_message->Append({"None", "Short", "CJK paragraph", "Long (wraps)", "Very long (scrolls)"});
        m_message->SetSelection(2);
        row("Message", m_message);

        m_image = new wxChoice(panel, wxID_ANY);
        m_image->Append({"None", "3:2 (360x240)", "Small 3:2 (90x60, upscaled)", "Wide 2:1", "Tall 1:2"});
        m_image->SetSelection(1);
        row("Image", m_image);

        m_tips = new wxSpinCtrl(panel, wxID_ANY, "1", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 3, 1);
        row("Tips", m_tips);

        m_warnings = new wxSpinCtrl(panel, wxID_ANY, "1", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 3, 1);
        row("Warnings", m_warnings);

        m_warnings_first = new wxCheckBox(panel, wxID_ANY, "Add warnings before tips (layout order must not change)");
        row("", m_warnings_first);

        m_progress = new wxCheckBox(panel, wxID_ANY, "Custom content: progress bar");
        row("Content", m_progress);

        m_dsa = new wxCheckBox(panel, wxID_ANY, "Show \"don't show again\"");
        m_dsa->SetValue(true);
        row("Checkbox", m_dsa);

        m_dsa_label = new wxTextCtrl(panel, wxID_ANY, utf8(u8"不再提醒"));
        row("Checkbox label", m_dsa_label);

        m_secondary = new wxSpinCtrl(panel, wxID_ANY, "1", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 3, 1);
        row("Secondary buttons", m_secondary);

        m_primary = new wxChoice(panel, wxID_ANY);
        m_primary->Append({"None", "Primary", "Danger"});
        m_primary->SetSelection(1);
        row("Recommended button", m_primary);

        m_primary_first = new wxCheckBox(panel, wxID_ANY, "Add recommended button first (must still end up rightmost)");
        row("", m_primary_first);

        m_dark = new wxCheckBox(panel, wxID_ANY, "Dark mode (takes effect for newly opened dialogs)");
        m_dark->SetValue(wxGetApp().dark_mode());
        row("Theme", m_dark);

        auto *presets = new wxBoxSizer(wxHORIZONTAL);
        auto  preset  = [&](const wxString &label, auto fn) {
            auto *btn = new wxButton(panel, wxID_ANY, label);
            btn->Bind(wxEVT_BUTTON, [this, fn](wxCommandEvent &) { fn(); });
            presets->Add(btn, 0, wxRIGHT, 6);
        };
        preset("Preset: template", [this]() { apply_preset(2, 1, 1, 1, false, true, 1, 1); });
        preset("Preset: negative", [this]() { apply_preset(1, 0, 0, 0, false, false, 1, 2); });
        preset("Preset: progress", [this]() { apply_preset(0, 0, 0, 0, true, false, 1, 0); });
        preset("Preset: plain OK", [this]() { apply_preset(3, 0, 0, 0, false, false, 0, 1); });

        auto *actions = new wxBoxSizer(wxHORIZONTAL);
        auto *modal   = new wxButton(panel, wxID_ANY, "Show modal");
        auto *live    = new wxButton(panel, wxID_ANY, "Show modeless + live updates");
        actions->Add(modal, 0, wxRIGHT, 6);
        actions->Add(live, 0);
        modal->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { show_modal(); });
        live->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { show_modeless(); });

        m_log = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxSize(-1, 140), wxTE_MULTILINE | wxTE_READONLY);

        auto *main = new wxBoxSizer(wxVERTICAL);
        main->Add(grid, 0, wxEXPAND | wxALL, 12);
        main->Add(presets, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);
        main->Add(actions, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);
        main->Add(m_log, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
        panel->SetSizer(main);
        auto *frame_sizer = new wxBoxSizer(wxVERTICAL);
        frame_sizer->Add(panel, 1, wxEXPAND);
        SetSizerAndFit(frame_sizer);
        SetSize(wxSize(FromDIP(620), GetSize().y));
        Centre();

        m_live_timer.Bind(wxEVT_TIMER, [this](wxTimerEvent &) { on_live_timer(); });
    }

private:
    void apply_preset(int message, int image, int tips, int warnings, bool progress, bool dsa, int secondary, int primary)
    {
        m_message->SetSelection(message);
        m_image->SetSelection(image);
        m_tips->SetValue(tips);
        m_warnings->SetValue(warnings);
        m_progress->SetValue(progress);
        m_dsa->SetValue(dsa);
        m_secondary->SetValue(secondary);
        m_primary->SetSelection(primary);
        if (message == 1) {
            m_title->SetValue("Cancel print");
        }
        else if (progress) {
            m_title->SetValue("Repair model object");
        }
        else {
            m_title->SetValue(utf8(u8"弹窗标题"));
        }
    }

    void apply_theme()
    {
        const bool dark = m_dark->GetValue();
        wxGetApp().app_config->set("dark_color_mode", dark ? "1" : "0");
        wxGetApp().init_label_colours();
        wxGetApp().Update_dark_mode_flag();
#ifdef _WIN32
        NppDarkMode::SetDarkMode(dark);
#endif
    }

    std::unique_ptr<AlertDialog> build()
    {
        apply_theme();

        wxString message;
        switch (m_message->GetSelection()) {
        case 0: break;
        case 4: message = very_long_message(); break;
        default: message = utf8(MESSAGES[m_message->GetSelection()]); break;
        }
        auto dlg = std::make_unique<AlertDialog>(this, m_title->GetValue(), message);

        switch (m_image->GetSelection()) {
        case 1: dlg->SetImage(placeholder(360, 240)); break;
        case 2: dlg->SetImage(placeholder(90, 60)); break;
        case 3: dlg->SetImage(placeholder(400, 200)); break;
        case 4: dlg->SetImage(placeholder(120, 240)); break;
        default: break;
        }

        auto add_tips = [&]() {
            for (int i = 0; i < m_tips->GetValue(); ++i)
                dlg->AddTip(utf8(TIPS[i]));
        };
        auto add_warnings = [&]() {
            for (int i = 0; i < m_warnings->GetValue(); ++i)
                dlg->AddWarning(utf8(WARNINGS[i]));
        };
        if (m_warnings_first->GetValue()) {
            add_warnings();
            add_tips();
        } else {
            add_tips();
            add_warnings();
        }

        if (m_progress->GetValue())
            dlg->AddContent(create_progress(dlg.get()));

        if (m_dsa->GetValue())
            dlg->ShowDontShowAgain(m_dsa_label->GetValue());

        auto add_recommended = [&]() {
            if (m_primary->GetSelection() == 1)
                dlg->AddButton(wxID_OK, utf8(u8"主要行动点"), AlertButtonRole::Primary);
            else if (m_primary->GetSelection() == 2)
                dlg->AddButton(wxID_OK, "Cancel print", AlertButtonRole::Danger);
        };
        if (m_primary_first->GetValue())
            add_recommended();
        for (int i = 0; i < m_secondary->GetValue(); ++i)
            dlg->AddButton(i == 0 ? wxID_CANCEL : wxID_HIGHEST + i, SECONDARY_LABELS[i]);
        if (!m_primary_first->GetValue())
            add_recommended();
        return dlg;
    }

    // Mirrors the "repair model object" design: caption, status line and bar
    wxWindow *create_progress(wxWindow *parent)
    {
        auto *panel = new wxPanel(parent);
        panel->SetBackgroundColour(StateColor::darkModeColorFor(ThemeColor::White));
        auto *caption = new Label(panel, Label::Body_13, "Object: model.stl\nRepairing through the Windows service");
        caption->SetForegroundColour(StateColor::darkModeColorFor(ThemeColor::TextMuted));
        auto *status = new wxBoxSizer(wxHORIZONTAL);
        auto *state  = new Label(panel, Label::Body_13, "Repairing");
        auto *pct    = new Label(panel, Label::Body_13, "60%");
        status->Add(state, 1);
        status->Add(pct, 0);
        auto *gauge = new wxGauge(panel, wxID_ANY, 100, wxDefaultPosition, wxSize(-1, parent->FromDIP(6)));
        gauge->SetValue(60);
        auto *sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(caption, 0, wxBOTTOM, parent->FromDIP(16));
        sizer->Add(status, 0, wxEXPAND | wxBOTTOM, parent->FromDIP(8));
        sizer->Add(gauge, 0, wxEXPAND);
        panel->SetSizer(sizer);
        return panel;
    }

    void show_modal()
    {
        auto       dlg    = build();
        const int  result = dlg->ShowModal();
        log(wxString::Format("ShowModal() = %s, don't show again = %s", id_name(result),
                             dlg->IsDontShowAgainChecked() ? "checked" : "unchecked"));
    }

    // Exercises setters on a visible dialog: each step must re-layout in place
    void show_modeless()
    {
        if (m_live && !m_live->IsBeingDeleted()) {
            m_live->Raise();
            return;
        }
        AlertDialog *dlg = build().release();
        dlg->Bind(wxEVT_CLOSE_WINDOW, [this, dlg](wxCloseEvent &) {
            log(wxString::Format("modeless closed, don't show again = %s", dlg->IsDontShowAgainChecked() ? "checked" : "unchecked"));
            m_live_timer.Stop();
            dlg->Destroy();
        });
        m_live      = wxWindowPtr<AlertDialog>(dlg);
        m_live_step = 0;
        dlg->Show();
        log("modeless shown; a tip, a warning and a button are added over the next 3 seconds");
        m_live_timer.Start(1000);
    }

    void on_live_timer()
    {
        if (!m_live || m_live->IsBeingDeleted() || ++m_live_step > 3) {
            m_live_timer.Stop();
            return;
        }
        if (m_live_step == 1)
            m_live->AddTip("Tips: added while the dialog is visible.");
        else if (m_live_step == 2)
            m_live->AddWarning("Warning added while the dialog is visible.");
        else
            m_live->AddButton(wxID_APPLY, "Added later");
        log(wxString::Format("live update %d applied", m_live_step));
    }

    static wxString id_name(int id)
    {
        switch (id) {
        case wxID_OK: return "wxID_OK";
        case wxID_CANCEL: return "wxID_CANCEL";
        case wxID_APPLY: return "wxID_APPLY";
        default: return wxString::Format("%d", id);
        }
    }

    void log(const wxString &line) { m_log->AppendText(line + "\n"); }

    wxTextCtrl *m_title{nullptr};
    wxChoice   *m_message{nullptr};
    wxChoice   *m_image{nullptr};
    wxSpinCtrl *m_tips{nullptr};
    wxSpinCtrl *m_warnings{nullptr};
    wxCheckBox *m_warnings_first{nullptr};
    wxCheckBox *m_progress{nullptr};
    wxCheckBox *m_dsa{nullptr};
    wxTextCtrl *m_dsa_label{nullptr};
    wxSpinCtrl *m_secondary{nullptr};
    wxChoice   *m_primary{nullptr};
    wxCheckBox *m_primary_first{nullptr};
    wxCheckBox *m_dark{nullptr};
    wxTextCtrl *m_log{nullptr};

    wxTimer                   m_live_timer;
    wxWindowPtr<AlertDialog> m_live;
    int                       m_live_step{0};
};

// Hosts the real GUI_App so dialogs see the same fonts, colours and dark-mode state as the
// slicer, but skips the slicer start-up (presets, network, main frame)
class GalleryApp : public GUI_App
{
public:
    bool OnInit() override
    {
        wxInitAllImageHandlers();
        Label::initSysFont(app_config->get_language_code(), true);
        init_fonts();
        init_label_colours();
#ifdef _WIN32
        NppDarkMode::InitDarkMode(dark_mode(), false);
#endif
        auto *frame = new GalleryFrame();
        frame->Show();
        SetTopWindow(frame);
        return true;
    }

    // GUI_App::OnExit tears down services this app never started
    int OnExit() override { return wxApp::OnExit(); }
};

} // namespace

int main(int argc, char **argv)
{
#ifdef _WIN32
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#endif
    // Own data dir so the user's slicer settings are never read or written
    const boost::filesystem::path data_dir = boost::filesystem::temp_directory_path() / "alert_dialog_gallery";
    boost::filesystem::create_directories(data_dir / "log");
    set_data_dir(data_dir.string());

    const boost::filesystem::path resources = GALLERY_RESOURCES_DIR;
    set_resources_dir(resources.string());
    set_var_dir((resources / "images").string());
    set_local_dir((resources / "i18n").string());

    wxApp::SetInstance(new GalleryApp());
    return wxEntry(argc, argv);
}
