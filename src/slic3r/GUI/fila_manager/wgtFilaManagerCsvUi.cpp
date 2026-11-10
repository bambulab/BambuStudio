#include "wgtFilaManagerCsvUi.h"

#include <sstream>
#include <string>
#include <vector>

#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/filedlg.h>
#include <wx/radiobut.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/string.h>

#include <boost/nowide/fstream.hpp>

#include "wgtFilaManagerCsv.h"
#include "wgtFilaManagerStore.h"
#include "wgtFilaManagerCloudDispatcher.h"

#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/Utils/NetworkAgent.hpp"

namespace Slic3r { namespace GUI {

namespace {

std::vector<FilamentSpool> gather_inventory(wgtFilaManagerStore* store)
{
    std::vector<FilamentSpool> spools;
    if (!store) return spools;
    for (const std::string& id : store->all_spool_ids()) {
        if (const FilamentSpool* s = store->get_spool(id)) spools.push_back(*s);
    }
    return spools;
}

bool cloud_write_ready()
{
    NetworkAgent* agent = wxGetApp().getAgent();
    return agent && agent->is_user_login() && agent->is_server_connected();
}

enum class ExportScope { Cancelled, ImportableOnly, AllFields };

// Ask whether to export importable fields only or every field. `readonly_note`
// is reused as the CSV header suffix so the dialog and file read the same.
ExportScope ask_export_scope(wxWindow* parent, const wxString& readonly_note)
{
    wxDialog dlg(parent, wxID_ANY, _L("Export Filament Library"));
    auto* top = new wxBoxSizer(wxVERTICAL);

    top->Add(new wxStaticText(&dlg, wxID_ANY, _L("Which fields do you want to export?")),
             0, wxLEFT | wxRIGHT | wxTOP, 16);

    auto* rb_importable = new wxRadioButton(&dlg, wxID_ANY, _L("Importable fields only"),
                                            wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
    auto* rb_all = new wxRadioButton(
        &dlg, wxID_ANY,
        wxString::Format(_L("All fields (fields marked \"%s\" cannot be imported back)"), readonly_note));
    rb_importable->SetValue(true);
    top->Add(rb_importable, 0, wxLEFT | wxRIGHT | wxTOP, 16);
    top->Add(rb_all,        0, wxLEFT | wxRIGHT | wxTOP, 8);

    if (wxSizer* btns = dlg.CreateButtonSizer(wxOK | wxCANCEL))
        top->Add(btns, 0, wxEXPAND | wxALL, 16);

    dlg.SetSizerAndFit(top);
    dlg.CentreOnParent();
    if (dlg.ShowModal() != wxID_OK) return ExportScope::Cancelled;
    return rb_all->GetValue() ? ExportScope::AllFields : ExportScope::ImportableOnly;
}

// Human-readable preview of the plan; lists every delete and caps the rest so a
// large file doesn't produce a wall of text.
wxString describe_plan(const ImportPlan& plan)
{
    wxString msg;
    msg << wxString::Format(_L("Create %d, update %d, delete %d."),
                            (int) plan.creates.size(),
                            (int) plan.updates.size(),
                            (int) plan.deletes.size());

    if (!plan.deletes.empty()) {
        msg << "\n\n" << _L("These spools will be deleted:") << "\n";
        const size_t cap = 12;
        for (size_t i = 0; i < plan.deletes.size() && i < cap; ++i)
            msg << "  - " << from_u8(plan.deletes[i].label) << "\n";
        if (plan.deletes.size() > cap)
            msg << wxString::Format(_L("  ... and %d more"), (int) (plan.deletes.size() - cap)) << "\n";
    }

    if (!plan.errors.empty()) {
        msg << "\n" << wxString::Format(_L("%d row(s) will be skipped:"), (int) plan.errors.size()) << "\n";
        const size_t cap = 12;
        for (size_t i = 0; i < plan.errors.size() && i < cap; ++i)
            msg << "  - " << wxString::Format(_L("line %d: "), plan.errors[i].row)
                << from_u8(plan.errors[i].reason) << "\n";
        if (plan.errors.size() > cap)
            msg << wxString::Format(_L("  ... and %d more"), (int) (plan.errors.size() - cap)) << "\n";
    }
    return msg;
}

} // namespace

bool can_export_filament_library_csv()
{
    return wxGetApp().fila_manager_store() != nullptr;
}

bool can_import_filament_library_csv()
{
    return wxGetApp().fila_manager_store() != nullptr
        && wxGetApp().fila_manager_cloud_disp() != nullptr
        && cloud_write_ready();
}

void export_filament_library_csv(wxWindow* parent)
{
    wgtFilaManagerStore* store = wxGetApp().fila_manager_store();
    if (!store) {
        MessageDialog(parent, _L("Filament Manager is not ready yet."),
                      _L("Export Filament Library"), wxOK | wxICON_INFORMATION).ShowModal();
        return;
    }

    // Shown in the dialog and appended to each read-only CSV header.
    const wxString readonly_note = _L("export only");
    const ExportScope scope = ask_export_scope(parent, readonly_note);
    if (scope == ExportScope::Cancelled) return;

    const bool        include_readonly = scope == ExportScope::AllFields;
    const std::string suffix = include_readonly
        ? " (" + std::string(readonly_note.ToUTF8().data()) + ")"
        : std::string();
    const std::string csv = export_spools_to_csv(gather_inventory(store), include_readonly, suffix);

    wxFileDialog dlg(parent, _L("Export Filament Library"), wxEmptyString, "filament_library.csv",
                     "CSV files (*.csv)|*.csv", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() != wxID_OK) return;

    boost::nowide::ofstream ofs(dlg.GetPath().ToUTF8().data(), std::ios::binary);
    if (!ofs) {
        MessageDialog(parent, _L("Could not open the file for writing."),
                      _L("Export Filament Library"), wxOK | wxICON_ERROR).ShowModal();
        return;
    }
    ofs << csv;
    ofs.close();
}

void import_filament_library_csv(wxWindow* parent)
{
    wgtFilaManagerStore*           store = wxGetApp().fila_manager_store();
    wgtFilaManagerCloudDispatcher* disp  = wxGetApp().fila_manager_cloud_disp();
    if (!store || !disp || !cloud_write_ready()) {
        MessageDialog(parent,
                      _L("Importing writes to your cloud filament library. Sign in and connect, then try again."),
                      _L("Import Filament Library"), wxOK | wxICON_INFORMATION).ShowModal();
        return;
    }

    wxFileDialog dlg(parent, _L("Import Filament Library"), wxEmptyString, wxEmptyString,
                     "CSV files (*.csv)|*.csv", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() != wxID_OK) return;

    boost::nowide::ifstream ifs(dlg.GetPath().ToUTF8().data(), std::ios::binary);
    if (!ifs) {
        MessageDialog(parent, _L("Could not open the file for reading."),
                      _L("Import Filament Library"), wxOK | wxICON_ERROR).ShowModal();
        return;
    }
    std::stringstream buf;
    buf << ifs.rdbuf();

    const ParsedCsv  parsed = parse_csv(buf.str());
    const ImportPlan plan   = reconcile_import(parsed, gather_inventory(store));

    if (plan.empty_plan() && plan.errors.empty()) {
        const wxString msg = parsed.rows.empty()
            ? _L("Nothing to import: the file has no spool rows.")
            : _L("Nothing to import: every row already matches your library.");
        MessageDialog(parent, msg, _L("Import Filament Library"),
                      wxOK | wxICON_INFORMATION).ShowModal();
        return;
    }
    if (plan.empty_plan()) {
        // Only errors, nothing to apply.
        MessageDialog(parent, describe_plan(plan), _L("Import Filament Library"),
                      wxOK | wxICON_WARNING).ShowModal();
        return;
    }

    const long style = wxOK | wxCANCEL | (plan.deletes.empty() ? wxICON_INFORMATION : wxICON_WARNING);
    MessageDialog confirm(parent, describe_plan(plan), _L("Import Filament Library"), style);
    if (confirm.ShowModal() != wxID_OK) return;

    for (const nlohmann::json& obj : plan.creates)
        disp->enqueue_push_create(FilamentSpool::from_json(obj));
    for (const nlohmann::json& obj : plan.updates) {
        const std::string id = obj.value("spool_id", std::string());
        if (!id.empty()) disp->enqueue_push_update(id, obj);
    }
    if (!plan.deletes.empty()) {
        std::vector<std::string> ids;
        ids.reserve(plan.deletes.size());
        for (const PendingDelete& d : plan.deletes) ids.push_back(d.spool_id);
        disp->enqueue_push_delete(ids);
    }
    // No success dialog: the confirm above already stated the counts; the
    // inventory updates as the cloud sync lands.
}

}} // namespace Slic3r::GUI
