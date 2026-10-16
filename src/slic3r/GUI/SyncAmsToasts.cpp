#include "SyncAmsToasts.hpp"

#include "GUI_App.hpp"
#include "I18N.hpp"
#include "MainFrame.hpp"
#include "Plater.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/Label.hpp"
#include "libslic3r/PresetBundle.hpp"

namespace Slic3r::GUI {

SyncNozzleAndAmsDialog::SyncNozzleAndAmsDialog(InputInfo &input_info)
    : BaseTransparentDPIFrame(static_cast<wxWindow *>(wxGetApp().mainframe),
                              320,
                              input_info.dialog_pos,
                              90,
                              _L("Successfully synchronized Printer information."),
                              _L("Continue to sync filaments"),
                              _CTX(L_CONTEXT("Cancel", "Sync_Nozzle_AMS"), "Sync_Nozzle_AMS"),
                              DisappearanceMode::TimedDisappearance)
    , m_input_info(input_info)
{}

SyncNozzleAndAmsDialog::~SyncNozzleAndAmsDialog() {}

void SyncNozzleAndAmsDialog::deal_ok()
{
    on_hide();
    wxGetApp().plater()->sidebar().sync_ams_list(true);
}

void SyncNozzleAndAmsDialog::deal_cancel() { on_hide(); }

void SyncNozzleAndAmsDialog::update_info(InputInfo &info)
{
    m_input_info = info;
    restart();
    SetPosition(m_input_info.dialog_pos);
}

FinishSyncAmsDialog::FinishSyncAmsDialog(InputInfo &input_info)
    : BaseTransparentDPIFrame(static_cast<wxWindow *>(wxGetApp().mainframe),
                              310,
                              input_info.dialog_pos,
                              68,
                              _L("Successfully synchronized color and type of filament from printer."),
                              _CTX(L_CONTEXT("OK", "FinishSyncAms"), "FinishSyncAms"),
                              "",
                              DisappearanceMode::TimedDisappearance)
    , m_input_info(input_info)
{
    m_button_cancel->Hide();
}

FinishSyncAmsDialog::~FinishSyncAmsDialog() {}

void FinishSyncAmsDialog::deal_ok() { on_hide(); }

void FinishSyncAmsDialog::set_custom_message(const wxString &message)
{
    if (!m_finish_text) return;
    m_finish_text->SetLabel(message);
    Layout();
    Fit();
}

void FinishSyncAmsDialog::update_info(InputInfo &info)
{
    m_input_info = info;
    restart();
    SetPosition(m_input_info.dialog_pos);
}

} // namespace Slic3r::GUI
