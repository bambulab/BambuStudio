#include "ExtraCameraPanel.hpp"

#include <nlohmann/json.hpp>

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/listbox.h>
#include <wx/sizer.h>
#include <wx/textdlg.h>
#include <wx/webview.h>

#include "GUI_App.hpp"
#include "I18N.hpp"
#include "Widgets/WebView.hpp"
#include "libslic3r/AppConfig.hpp"

namespace Slic3r {
namespace GUI {

namespace {

const char *CFG_LIST     = "extra_cameras";          // JSON array of {name, url}
const char *CFG_SELECTED = "extra_camera_selected";  // index into that array

wxString to_wx(const std::string &s) { return wxString::FromUTF8(s.c_str()); }
std::string to_std(const wxString &s) { return std::string(s.utf8_str()); }

// Users will type "192.168.1.50:8080/stream"; make that loadable.
std::string normalize_url(std::string url)
{
    const auto first = url.find_first_not_of(" \t");
    const auto last  = url.find_last_not_of(" \t");
    if (first == std::string::npos)
        return {};
    url = url.substr(first, last - first + 1);
    if (url.find("://") == std::string::npos)
        url = "http://" + url;
    return url;
}

} // namespace

ExtraCameraPanel::ExtraCameraPanel(wxWindow *parent)
    : wxPanel(parent, wxID_ANY)
{
    auto *top = new wxBoxSizer(wxHORIZONTAL);
    m_choice  = new wxChoice(this, wxID_ANY);
    m_reload  = new wxButton(this, wxID_ANY, _L("Reload"));
    m_manage  = new wxButton(this, wxID_ANY, _L("Manage..."));
    top->Add(m_choice, 1, wxALL | wxALIGN_CENTER_VERTICAL, 6);
    top->Add(m_reload, 0, wxTOP | wxBOTTOM | wxALIGN_CENTER_VERTICAL, 6);
    top->Add(m_manage, 0, wxALL | wxALIGN_CENTER_VERTICAL, 6);

    // Go through Bambu's helper rather than wxWebView::New: on Windows every
    // WebView2 in the process must share one runtime + user-data folder, and the
    // helper also provides the no-backend placeholder, dark-mode background and
    // devtools gating. Its navigation logs redact credentials, queries and IPs.
    m_web = WebView::CreateWebView(this, "about:blank", "ExtraCamera");

    auto *main = new wxBoxSizer(wxVERTICAL);
    main->Add(top, 0, wxEXPAND);
    main->Add(m_web, 1, wxEXPAND);
    SetSizer(main);

    m_choice->Bind(wxEVT_CHOICE, [this](wxCommandEvent &) {
        m_selected = m_choice->GetSelection();
        wxGetApp().app_config->set(CFG_SELECTED, std::to_string(m_selected));
        if (m_active)
            load_selected();
    });
    m_reload->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
        m_loaded_url.clear();
        if (m_active)
            load_selected();
    });
    m_manage->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { on_manage(); });

    // Visibility is driven by MonitorPanel::update_extra_camera_active(), not
    // wxEVT_SHOW: children get no show event when the Device tab itself is hidden.

    load_entries();
    refresh_choice();
}

void ExtraCameraPanel::set_active(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    if (m_active)
        load_selected();
    else
        unload();
}

void ExtraCameraPanel::load_entries()
{
    m_entries.clear();
    auto *cfg = wxGetApp().app_config;

    try {
        const std::string raw = cfg->get(CFG_LIST);
        if (!raw.empty()) {
            auto j = nlohmann::json::parse(raw);
            for (const auto &item : j)
                m_entries.push_back({item.value("name", std::string()), item.value("url", std::string())});
        }
    } catch (...) {
        m_entries.clear(); // corrupt value in the config: start fresh
    }

    try {
        const std::string sel = cfg->get(CFG_SELECTED);
        m_selected = sel.empty() ? 0 : std::stoi(sel);
    } catch (...) {
        m_selected = 0;
    }
    if (m_selected < 0 || m_selected >= (int) m_entries.size())
        m_selected = 0;
}

void ExtraCameraPanel::save_entries()
{
    nlohmann::json j = nlohmann::json::array();
    for (const auto &e : m_entries)
        j.push_back({{"name", e.name}, {"url", e.url}});

    auto *cfg = wxGetApp().app_config;
    cfg->set(CFG_LIST, j.dump());
    cfg->set(CFG_SELECTED, std::to_string(m_selected));
    cfg->save();
}

void ExtraCameraPanel::refresh_choice()
{
    m_choice->Clear();
    for (const auto &e : m_entries)
        m_choice->Append(to_wx(e.name.empty() ? e.url : e.name));

    if (m_entries.empty()) {
        m_choice->Append(_L("(no cameras configured)"));
        m_choice->SetSelection(0);
        m_choice->Disable();
        m_reload->Disable();
    } else {
        if (m_selected < 0 || m_selected >= (int) m_entries.size())
            m_selected = 0;
        m_choice->SetSelection(m_selected);
        m_choice->Enable();
        m_reload->Enable();
    }
}

void ExtraCameraPanel::load_selected()
{
    if (m_entries.empty() || m_selected < 0 || m_selected >= (int) m_entries.size()) {
        unload();
        return;
    }
    const std::string &url = m_entries[m_selected].url;
    if (url == m_loaded_url)
        return;
    m_loaded_url = url;
    WebView::LoadUrl(m_web, to_wx(url));
}

void ExtraCameraPanel::unload()
{
    m_loaded_url.clear();
    WebView::LoadUrl(m_web, "about:blank");
}

void ExtraCameraPanel::on_manage()
{
    wxDialog dlg(this, wxID_ANY, _L("Manage cameras"), wxDefaultPosition, wxSize(460, 320));

    auto *list   = new wxListBox(&dlg, wxID_ANY);
    auto *add    = new wxButton(&dlg, wxID_ANY, _L("Add..."));
    auto *edit   = new wxButton(&dlg, wxID_ANY, _L("Edit..."));
    auto *remove = new wxButton(&dlg, wxID_ANY, _L("Remove"));
    auto *close  = new wxButton(&dlg, wxID_OK, _L("Close"));

    auto *buttons = new wxBoxSizer(wxVERTICAL);
    buttons->Add(add, 0, wxEXPAND | wxBOTTOM, 6);
    buttons->Add(edit, 0, wxEXPAND | wxBOTTOM, 6);
    buttons->Add(remove, 0, wxEXPAND | wxBOTTOM, 6);
    buttons->AddStretchSpacer(1);
    buttons->Add(close, 0, wxEXPAND);

    auto *row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(list, 1, wxEXPAND | wxRIGHT, 8);
    row->Add(buttons, 0, wxEXPAND);

    auto *outer = new wxBoxSizer(wxVERTICAL);
    outer->Add(row, 1, wxEXPAND | wxALL, 10);
    dlg.SetSizer(outer);

    auto fill = [&]() {
        list->Clear();
        for (const auto &e : m_entries)
            list->Append(to_wx(e.name + "   -   " + e.url));
    };

    // Ask for name + URL. Returns false if the user cancels or gives no URL.
    auto prompt = [&](Entry &e) -> bool {
        wxTextEntryDialog name_dlg(&dlg, _L("Name (e.g. \"Pi - enclosure\"):"), _L("Camera"), to_wx(e.name));
        if (name_dlg.ShowModal() != wxID_OK)
            return false;
        wxTextEntryDialog url_dlg(&dlg, _L("Stream URL (e.g. http://192.168.1.50:8080/stream):"), _L("Camera"),
                                  to_wx(e.url));
        if (url_dlg.ShowModal() != wxID_OK)
            return false;
        const std::string url = normalize_url(to_std(url_dlg.GetValue()));
        if (url.empty())
            return false;
        e.name = to_std(name_dlg.GetValue());
        e.url  = url;
        return true;
    };

    add->Bind(wxEVT_BUTTON, [&](wxCommandEvent &) {
        Entry e;
        if (prompt(e)) {
            m_entries.push_back(e);
            fill();
            list->SetSelection((int) m_entries.size() - 1);
        }
    });
    edit->Bind(wxEVT_BUTTON, [&](wxCommandEvent &) {
        int i = list->GetSelection();
        if (i == wxNOT_FOUND)
            return;
        Entry e = m_entries[i];
        if (prompt(e)) {
            m_entries[i] = e;
            fill();
            list->SetSelection(i);
        }
    });
    remove->Bind(wxEVT_BUTTON, [&](wxCommandEvent &) {
        int i = list->GetSelection();
        if (i == wxNOT_FOUND)
            return;
        m_entries.erase(m_entries.begin() + i);
        fill();
    });
    list->Bind(wxEVT_LISTBOX_DCLICK, [&](wxCommandEvent &) {
        wxCommandEvent ev(wxEVT_BUTTON, edit->GetId());
        edit->GetEventHandler()->ProcessEvent(ev);
    });

    fill();
    dlg.ShowModal();

    if (m_selected >= (int) m_entries.size())
        m_selected = 0;
    save_entries();
    refresh_choice();
    m_loaded_url.clear();
    if (m_active)
        load_selected();
}

} // namespace GUI
} // namespace Slic3r
