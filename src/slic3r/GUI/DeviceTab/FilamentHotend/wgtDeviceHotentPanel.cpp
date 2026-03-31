#include "wgtDeviceHotentPanel.h"

#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"
#include "slic3r/GUI/DeviceCore/DevNozzleSystem.h"
#include "slic3r/GUI/DeviceCore/DevNozzleRack.h"

#include <wx/sizer.h>

namespace Slic3r::GUI {

static constexpr int kHotendSlotCount = 9;

wgtDeviceHotentPanel::wgtDeviceHotentPanel(wxWindow *parent) : wxPanel(parent, wxID_ANY) { CreateGui(); }

void wgtDeviceHotentPanel::UpdateByObj(Slic3r::MachineObject *obj_)
{
    m_obj = obj_;

    if (!m_obj) return;

    for (auto &[id, item] : m_hotend_items) {
        item->UpdateByObj(m_obj);
    }

    auto rack = m_obj->GetNozzleRack();
    // ── Reading state switch ────────────────────────────────────
    if (rack && rack->GetReadingCount() != 0) {
        m_progress_refresh->SetLabel(wxString::Format("(%d/%d)", rack->GetReadingIdx(), rack->GetReadingCount()));
        m_simple_book->SetSelection(1);
        if (!m_refresh_icon->IsPlaying()) {
            m_refresh_icon->Play();
        }
    } else {
        if (m_refresh_icon->IsPlaying()) {
            m_refresh_icon->Stop();
        }
        m_simple_book->SetSelection(0);
    }
}

void wgtDeviceHotentPanel::ReadAll()
{
    if (!m_obj) return;

    if (auto rack = m_obj->GetNozzleSystem()->GetNozzleRack()) {
        rack->CtrlRackReadAll(true);
    }
}

void wgtDeviceHotentPanel::CreateGui()
{
    SetBackgroundColour(*wxWHITE);

    m_simple_book = new wxSimplebook(this, wxID_ANY);

    // ── Page 0: Content page ──────────────────────────────────
    m_panel_content = new wxPanel(m_simple_book, wxID_ANY);
    m_panel_content->SetBackgroundColour(*wxWHITE);

    m_hotend_items.clear();
    for (int i = 0; i < kHotendSlotCount; ++i) {
        m_hotend_items[i] = new wgtDeviceHotendItem(m_panel_content, i + 1);
    }

    wxBoxSizer *row_top = new wxBoxSizer(wxHORIZONTAL);
    row_top->Add(m_hotend_items[0]);
    row_top->AddSpacer(FromDIP(8));
    row_top->Add(m_hotend_items[2]);
    row_top->AddSpacer(FromDIP(8));
    row_top->Add(m_hotend_items[4]);
    row_top->AddSpacer(FromDIP(8));
    row_top->Add(m_hotend_items[6]);

    wxBoxSizer *row_bot = new wxBoxSizer(wxHORIZONTAL);
    row_bot->Add(m_hotend_items[1]);
    row_bot->AddSpacer(FromDIP(8));
    row_bot->Add(m_hotend_items[3]);
    row_bot->AddSpacer(FromDIP(8));
    row_bot->Add(m_hotend_items[5]);
    row_bot->AddSpacer(FromDIP(8));
    row_bot->Add(m_hotend_items[7]);
    row_bot->AddSpacer(FromDIP(8));
    row_bot->Add(m_hotend_items[8]);

    wxBoxSizer *content_sizer = new wxBoxSizer(wxVERTICAL);
    content_sizer->Add(row_top, 0, wxTOP | wxLEFT | wxRIGHT, FromDIP(8));
    content_sizer->Add(row_bot, 0, wxALL, FromDIP(8));
    m_panel_content->SetSizer(content_sizer);

    // ── Page 1: Reading/refresh page ──────────────────────────
    m_panel_refresh = new wxPanel(m_simple_book, wxID_ANY);
    m_panel_refresh->SetBackgroundColour(*wxWHITE);
    CreateRefreshPage(m_panel_refresh);

    m_simple_book->AddPage(m_panel_content, "Content", true);
    m_simple_book->AddPage(m_panel_refresh, "Refresh", false);

    // ── Root sizer ───────────────────────────────────────────
    wxBoxSizer *root = new wxBoxSizer(wxVERTICAL);
    root->Add(m_simple_book, 1, wxEXPAND);

    SetSizer(root);
    Layout();
}

void wgtDeviceHotentPanel::CreateRefreshPage(wxPanel *parent)
{
    wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);

    std::vector<std::string> frames{"ams_rfid_1", "ams_rfid_2", "ams_rfid_3", "ams_rfid_4"};
    m_refresh_icon = new AnimaIcon(parent, wxID_ANY, frames, "refresh_printer", 100);
    m_refresh_icon->SetMinSize(wxSize(FromDIP(25), FromDIP(25)));

    wxBoxSizer *progress_sizer = new wxBoxSizer(wxHORIZONTAL);
    Label *progress_prefix = new Label(parent, _L("Reading") + " ");
    progress_prefix->SetBackgroundColour(*wxWHITE);
    m_progress_refresh = new Label(parent, "(0/0)");
    m_progress_refresh->SetFont(Label::Body_14);
    m_progress_refresh->SetBackgroundColour(*wxWHITE);
    m_progress_refresh->SetForegroundColour(*wxGREEN);
    Label *progress_suffix = new Label(parent, " ...");
    progress_suffix->SetBackgroundColour(*wxWHITE);
    progress_sizer->Add(progress_prefix, 0, wxLEFT);
    progress_sizer->Add(m_progress_refresh, 0, wxLEFT);
    progress_sizer->Add(progress_suffix, 0, wxLEFT);

    Label *tip = new Label(parent, _L("Please wait"));
    tip->SetBackgroundColour(*wxWHITE);

    sizer->Add(0, 0, 1, wxEXPAND);
    sizer->Add(m_refresh_icon, 0, wxALIGN_CENTER_HORIZONTAL);
    sizer->Add(progress_sizer, 0, wxALIGN_CENTER_HORIZONTAL);
    sizer->Add(tip, 0, wxALIGN_CENTER_HORIZONTAL);
    sizer->Add(0, 0, 1, wxEXPAND);

    parent->SetSizer(sizer);
}

} // namespace Slic3r::GUI
