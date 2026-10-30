#include "Actions.hpp"
#include "Builder.hpp"
#include "Schema.hpp"

#include "../ViewModel.hpp"

#include "slic3r/GUI/DeviceWeb/ViewModels/DevicePage/DevicePageDialogHelpers.h"
#include "slic3r/GUI/DeviceCore/DevCtrlToken.h"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevFilaAutoRefillMgr.h"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MsgDialog.hpp"

#include "libslic3r/Log.hpp"

#include <algorithm>
#include <memory>

namespace Slic3r { namespace GUI {

namespace {

namespace SK = AutoRefillRefillInfoSchema::keys;
namespace SV = AutoRefillRefillInfoSchema::values;

bool manual_edit_allowed(MachineObject* obj)
{
    return obj && obj->GetFilaSystem() && obj->GetFilaSystem()->GetAutoRefillMgr().IsSupportManualEdit();
}

/// \return the tray label shown in the removal prompt, e.g. "N5".
wxString resolve_slot_label(MachineObject* obj, const DevAmsSlotId& slot)
{
    const auto tray = obj->get_tray(slot.first, slot.second);
    if (!tray) return "--";

    const std::string name = tray->get_slot_name(obj);
    return name.empty() ? wxString("--") : wxString::FromUTF8(name);
}

/**
 * \brief Whether removing `slot` also dissolves its refill group.
 */
bool removal_dissolves_group(const DevFilaAutoRefillMgr& refill, const DevAmsSlotId& slot)
{
    const auto groups = refill.GetGroupsBySlot(slot);
    if (groups.groups.empty()) return false;

    const auto& slots = groups.groups.front().fila_refill_slots;
    return slots.size() <= 2;
}

/// Prompt copy for removing `slot`; `dissolves_group` also picks the toast.
wxString removal_prompt(MachineObject*              obj,
                        const DevFilaAutoRefillMgr& refill,
                        const DevAmsSlotId&         slot,
                        bool&                       dissolves_group)
{
    dissolves_group  = removal_dissolves_group(refill, slot);
    wxString message = wxString::Format(_L("Remove \"%s\" from this refill group?"),
                                        resolve_slot_label(obj, slot));
    if (dissolves_group) {
        message += "\n" + _L("The refill group will be automatically deleted after removal.");
    }
    return message;
}

} // namespace

void AutoRefillRefillInfoActions::PromptAndRemoveSlot(const DevAmsSlotId& slot)
{
    auto*          dev_mgr = wxGetApp().getDeviceManager();
    MachineObject* obj     = dev_mgr ? dev_mgr->get_selected_machine() : nullptr;
    if (!obj || !obj->GetFilaSystem()) return;

    auto&          refill    = obj->GetFilaSystem()->GetAutoRefillMgr();
    bool           dissolves = false;
    const wxString message   = removal_prompt(obj, refill, slot, dissolves);

    MessageDialog dlg(ResolveAutoRefillDialogParent(), message, _L("Tip"), wxOK | wxCANCEL | wxICON_WARNING);
    dlg.SetButtonLabel(wxID_OK, _L("Remove"));
    if (dlg.ShowModal() != wxID_OK) return;

    // Unwatched on purpose: the picker's view-model is gone by now, so there is
    // nowhere to report the acknowledgement to. The management page rebuilds
    // from RefillInfo on its refresh timer.
    refill.CtrlRemove(slot);
}

nlohmann::json AutoRefillRefillInfoActions::Reply(AutoRefillRefillInfoVM& vm,
                                                  const std::string&      action,
                                                  int                     code,
                                                  const std::string&      msg)
{
    return vm.MakeResponse(vm.GetModule(), "action", action, code, msg,
                           AutoRefillRefillInfoBuilder::Build(vm.m_obj, vm.m_session_seq));
}

nlohmann::json AutoRefillRefillInfoActions::Handle(AutoRefillRefillInfoVM& vm,
                                                   MachineObject*          obj,
                                                   const std::string&      action,
                                                   const nlohmann::json&   payload)
{
    if (action == SV::action::rendered) {
        return vm.MakeResponse(vm.GetModule(), "action", action, 0);
    }

    if (!obj || !obj->GetFilaSystem()) {
        return Reply(vm, action, 1, "no printer");
    }

    if (action == SV::action::enable_auto_refill) {
        if (!obj->GetFilaSystem()->GetAutoRefillMgr().IsSupported()) {
            return Reply(vm, action, 1, "auto refill not supported");
        }
        BOOST_LOG_TRIVIAL(info) << "enable auto refill from refill page";
        obj->command_ams_switch_filament(true);
        return Reply(vm, action, 0, "");
    }

    if (!manual_edit_allowed(obj)) {
        return Reply(vm, action, 1, "manual edit not supported");
    }

    auto& refill = obj->GetFilaSystem()->GetAutoRefillMgr();

    auto reply_ctrl = [&](const std::shared_ptr<DevCtrlToken>& token) {
        if (!token || token->GetStatus() == DevCtrlTokenStatus::SendFailed) {
            return Reply(vm, action, 1, "publish failed");
        }
        return Reply(vm, action, 0, "");
    };

    if (action == SV::action::open_add_group) {
        const std::weak_ptr<bool> alive = vm.m_alive;
        wxGetApp().CallAfter([vm_ptr = &vm, alive, obj, token = obj->get_token()]() {
            if (token.expired()) return;
            OpenAutoRefillFilamentsDialog(obj, AutoRefillFilamentsDialogMode::CreateGroup);
            const auto guard = alive.lock();
            if (guard && *guard) {
                auto* dev_mgr = wxGetApp().getDeviceManager();
                vm_ptr->Refresh(dev_mgr ? dev_mgr->get_selected_machine() : nullptr);
            }
        });
        return Reply(vm, action, 0, "");
    } else if (action == SV::action::open_add_filament) {
        if (!payload.contains(SK::command::ams_id) || !payload.contains(SK::command::slot_id)) {
            return Reply(vm, action, 1, "missing group head");
        }
        const DevAmsSlotId group_head{payload[SK::command::ams_id].get<int>(),
                                      payload[SK::command::slot_id].get<int>()};
        const std::weak_ptr<bool> alive = vm.m_alive;
        wxGetApp().CallAfter([vm_ptr = &vm, alive, obj, token = obj->get_token(), group_head]() {
            if (token.expired()) return;
            OpenAutoRefillFilamentsDialog(obj, AutoRefillFilamentsDialogMode::AddToGroup, group_head);
            const auto guard = alive.lock();
            if (guard && *guard) {
                auto* dev_mgr = wxGetApp().getDeviceManager();
                vm_ptr->Refresh(dev_mgr ? dev_mgr->get_selected_machine() : nullptr);
            }
        });
        return Reply(vm, action, 0, "");
    } else if (action == SV::action::query) {
        return reply_ctrl(refill.CtrlQuery());
    } else if (action == SV::action::query_next) {
        if (!payload.contains(SK::command::ams_id) || !payload.contains(SK::command::slot_id)) {
            return Reply(vm, action, 1, "missing ams_id/slot_id");
        }

        const DevAmsSlotId to{payload[SK::command::ams_id].get<int>(),  payload[SK::command::slot_id].get<int>()};
        return reply_ctrl(refill.CtrlQueryNext(to));
    } else if (action == SV::action::confirm_reset) {
        const std::string group_tag = payload.value(SK::command::group_tag, std::string());
        if (group_tag.empty()) {
            return Reply(vm, action, 1, "missing group_tag");
        }

        // A reset drops the manually added members only; the auto-generated
        // relations, and therefore the group itself, survive it.
        const wxString message =
            _L("This will delete all manually added filament in current group, please proceed with caution.");

        // Same constraint as confirm_remove: ShowModal() inside the WebView2
        // message callback would re-enter it. Answer first, prompt from idle,
        // push the decision back as a report.
        const std::weak_ptr<bool> alive = vm.m_alive;
        wxGetApp().CallAfter([vm_ptr = &vm, alive, group_tag, message]() {
            if (const auto guard = alive.lock(); !guard || !*guard) return;

            MessageDialog dlg(ResolveAutoRefillDialogParent(), message, _L("Reset Refill Group"), wxOK | wxCANCEL | wxICON_WARNING);
            dlg.SetButtonLabel(wxID_OK, _L("Reset"));
            const bool confirmed = dlg.ShowModal() == wxID_OK;

            // The host dialog may have been closed while the prompt was up.
            if (const auto guard = alive.lock(); !guard || !*guard) return;
            vm_ptr->FinishResetConfirmation(group_tag, confirmed);
        });
        return Reply(vm, action, 0, "");
    } else if (action == SV::action::reset) {
        const std::string group_tag = payload.value(SK::command::group_tag, std::string());
        if (group_tag.empty()) {
            return Reply(vm, action, 1, "missing group_tag");
        }
        if (!vm.StartResetGroup(group_tag)) {
            return Reply(vm, action, 1, "publish failed");
        }
        return Reply(vm, action, 0, "");
    } else if (action == SV::action::confirm_remove) {
        if (!payload.contains(SK::command::ams_id) || !payload.contains(SK::command::slot_id)) {
            return Reply(vm, action, 1, "missing ams_id/slot_id");
        }
        const DevAmsSlotId slot{payload[SK::command::ams_id].get<int>(),
                                payload[SK::command::slot_id].get<int>()};

        bool           group_removed = false;
        const wxString message       = removal_prompt(obj, refill, slot, group_removed);
        const std::string toast_key  = group_removed ? "Refill group removed successfully"
                                                     : "Removed successfully";

        // ShowModal() must not run here: this handler is called from the
        // WebView2 message callback, and a nested modal event loop re-enters it
        // and crashes. Answer the request first, then prompt from an idle
        // CallAfter and push the decision back as a report.
        const std::weak_ptr<bool> alive = vm.m_alive;
        wxGetApp().CallAfter([vm_ptr = &vm, alive, slot, message, toast_key]() {
            if (const auto guard = alive.lock(); !guard || !*guard) return;

            MessageDialog dlg(ResolveAutoRefillDialogParent(), message, _L("Tip"), wxOK | wxCANCEL | wxICON_WARNING);
            dlg.SetButtonLabel(wxID_OK, _L("Remove"));
            const bool confirmed = dlg.ShowModal() == wxID_OK;

            // The host dialog may have been closed while the prompt was up.
            if (const auto guard = alive.lock(); !guard || !*guard) return;
            vm_ptr->FinishRemoveConfirmation(slot, confirmed, toast_key);
        });
        return Reply(vm, action, 0, "");
    } else if (action == SV::action::remove) {
        if (!payload.contains(SK::command::ams_id) || !payload.contains(SK::command::slot_id)) {
            return Reply(vm, action, 1, "missing ams_id/slot_id");
        }
        const DevAmsSlotId slot{payload[SK::command::ams_id].get<int>(),
                                payload[SK::command::slot_id].get<int>()};
        if (!vm.StartRemoveSlot(slot)) {
            return Reply(vm, action, 1, "publish failed");
        }
        return Reply(vm, action, 0, "");
    }

    if (action == SV::action::edit) {
        if (!payload.contains(SK::command::ams_id) || !payload.contains(SK::command::slot_id) ||
            !payload.contains(SK::command::to_ams_id) || !payload.contains(SK::command::to_slot_id)) {
            return Reply(vm, action, 1, "missing slot/to slot");
        }
        const DevAmsSlotId slot{payload[SK::command::ams_id].get<int>(),
                                payload[SK::command::slot_id].get<int>()};
        const DevAmsSlotId member{payload[SK::command::to_ams_id].get<int>(),
                                  payload[SK::command::to_slot_id].get<int>()};
        const bool as_head = payload.value(SK::command::as_head, false);
        if (!vm.StartMoveSlot(slot, member, as_head)) {
            return Reply(vm, action, 1, "publish failed");
        }
        return Reply(vm, action, 0, "");
    }

    return Reply(vm, action, 1, "unknown auto refill action");
}

}} // namespace Slic3r::GUI
