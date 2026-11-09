/*****************************************************************//**
 * \file   ViewModelActions.cpp
 * \brief  confirm / close of the Select Filament dialog.
 *
 * A rejected confirm leaves the dialog open and answers with a non-zero
 * error_code; the page shows nothing, matching the native dialog where
 * Confirm without a valid selection is a no-op.
 *********************************************************************/
#include "ViewModelActions.hpp"
#include "ViewModel.hpp"
#include "Schema.hpp"

#include "slic3r/GUI/GUI_App.hpp"

#include <string>

#include <wx/defs.h>
#include "libslic3r/Log.hpp"

namespace Slic3r { namespace GUI {

namespace FS  = FilamentSelectSchema;
namespace FSC = FilamentSelectCommon;

namespace {

constexpr int kRejected = 1;

} // namespace

nlohmann::json FilamentSelectActions::Handle(FilamentSelectVM&     vm,
                                             const std::string&    action,
                                             const nlohmann::json& payload)
{
    if (action == "confirm") return Confirm(vm, payload);
    if (action == "close")   return Close(vm);

    BOOST_LOG_TRIVIAL(warning) << "unknown filament select action " << action;
    return vm.MakeResponse(vm.GetModule(), "action", action, kRejected, "unknown action");
}

nlohmann::json FilamentSelectActions::Confirm(FilamentSelectVM& vm, const nlohmann::json& payload)
{
    auto reject = [&vm](const char* reason) {
        BOOST_LOG_TRIVIAL(warning) << "filament select confirm rejected: " << reason;
        return vm.MakeResponse(vm.GetModule(), "action", "confirm", kRejected, reason);
    };

    if (!vm.m_opened || !vm.m_result) return reject("dialog not opened");

    const std::string tab = payload.value(FS::keys::confirm::tab, std::string());

    if (tab == FS::values::tab::manager) {
        std::size_t index = 0;
        const std::string row_key = payload.value(FS::keys::confirm::row_key, std::string());
        if (!FilamentSelectBuilder::ParseRowKey(row_key, index) || index >= vm.m_snapshot.rows.rows.size())
            return reject("no spool selected");

        const FSC::SpoolRowEntry& entry = vm.m_snapshot.rows.rows[index];
        if (!entry.selectable()) return reject("spool not selectable");

        // The list is a snapshot: the spool may have been deleted, or mounted in
        // an AMS elsewhere, since the dialog opened.
        auto* store = wxGetApp().fila_manager_store();
        const FilamentSpool* live = store ? store->get_spool(entry.spool.spool_id) : nullptr;
        if (!live) return reject("spool no longer exists");
        if (live->in_printer) return reject("spool already loaded in printer");

        FilamentSelectResult result;
        result.spool_id   = live->spool_id;
        result.color_type = live->color_type;
        result.colors     = live->colors;
        result.color_code = live->color_code;
        *vm.m_result = std::move(result);

        BOOST_LOG_TRIVIAL(info) << "filament select confirmed a Filament Manager spool";
        vm.RequestClose(wxID_OK);
        return vm.MakeResponse(vm.GetModule(), "action", "confirm", 0);
    }

    if (tab == FS::values::tab::presets) {
        const wxString alias = wxString::FromUTF8(payload.value(FS::keys::confirm::alias, std::string()));
        if (alias.IsEmpty() || vm.m_snapshot.filament_items.Index(alias) == wxNOT_FOUND)
            return reject("no preset selected");

        FilamentSelectResult result;
        result.preset_alias = alias;
        *vm.m_result = std::move(result);

        BOOST_LOG_TRIVIAL(info) << "filament select confirmed a system preset";
        vm.RequestClose(wxID_OK);
        return vm.MakeResponse(vm.GetModule(), "action", "confirm", 0);
    }

    return reject("unknown tab");
}

nlohmann::json FilamentSelectActions::Close(FilamentSelectVM& vm)
{
    vm.RequestClose(wxID_CANCEL);
    return vm.MakeResponse(vm.GetModule(), "action", "close", 0);
}

}} // namespace Slic3r::GUI
