#include "Actions.hpp"
#include "BlockCodeUtil.hpp"
#include "Builder.hpp"
#include "Schema.hpp"

#include "slic3r/GUI/DeviceCore/DevNozzleRack.h"
#include "slic3r/GUI/DeviceCore/DevNozzleSystem.h"
#include "slic3r/GUI/DeviceCore/DevManager.h"// get_selected_machine() needs the definition
#include "slic3r/GUI/DeviceManager.hpp"

#include "../ViewModel.hpp"
#include "../refill_info/Actions.hpp"// PromptAndRemoveSlot()

#include "slic3r/GUI/DeviceWeb/ViewModels/DevicePage/DevicePageDialogHelpers.h"
#include "slic3r/GUI/DeviceTab/wgtDeviceAutoRefillWebDialog.h"// CloseAutoRefillDialog()

// MainFrame.hpp is deliberately not included: its OpenCASCADE `Handle` macro collides with Handle()
#include "slic3r/GUI/GUI_App.hpp"

#include <algorithm>
#include "libslic3r/Log.hpp"

namespace Slic3r { namespace GUI {

namespace {

namespace SV = AutoRefillAddRefillFilamentsSchema::values;
namespace BlockCode = AutoRefillAddRefillFilamentsBlockCodeUtil;

/// Innermost first: ending the outer modal first would orphan the picker.
void close_auto_refill_dialogs()
{
    CloseAutoRefillFilamentsDialog(wxID_CANCEL);
    CloseAutoRefillDialog(wxID_CANCEL);
}

/// Runs a block-code dialog's follow-up on the UI thread. Most fixes happen on
/// the printer, so they only step out of the way. \return empty on success.
std::string run_follow_up(BlockCode::FollowUp follow_up, const DevAmsSlotId& slot, int display_index)
{
    switch (follow_up) {
    // Fixed on the printer, so no command: Studio must not edit or load for the user
    case BlockCode::FollowUp::GoToEditFilament:
    case BlockCode::FollowUp::GoToLoad:
    // Unsupported in Studio (docking calibration; enable_set_nozzle_info=false), labelled "on printer"
    case BlockCode::FollowUp::GoToCalibration:
    case BlockCode::FollowUp::GoToEditHotend:
        close_auto_refill_dialogs();
        return {};

    // The one fix Studio can perform itself
    case BlockCode::FollowUp::GoToRefreshHotend: {
        if (display_index <= 0) return "follow_up needs the hotend display_index";
        auto* dev_mgr = wxGetApp().getDeviceManager();
        MachineObject* obj = dev_mgr ? dev_mgr->get_selected_machine() : nullptr;
        if (!obj || !obj->GetNozzleSystem()) return "no selected machine";
        const auto rack = obj->GetNozzleSystem()->GetNozzleRack();
        if (!rack) return "printer has no nozzle rack";
        rack->CrtlRackReadNozzle(display_index - 1);
        return {};
    }

    // No HMS code in the reply, so this lands on the HMS list, not on this fault's entry
    case BlockCode::FollowUp::ViewHmsDetail:
        OpenHmsPanel();
        return {};

    // Only close the block-code dialog itself
    case BlockCode::FollowUp::None:
    case BlockCode::FollowUp::SelectAnyway:
        return {};

    // Unbinding belongs to the management page. Prompt from a fresh idle callback:
    // the picker's modal loop has to unwind before another modal may open.
    case BlockCode::FollowUp::GoToUnbind:
        CloseAutoRefillFilamentsDialog(wxID_CANCEL);
        wxGetApp().CallAfter([slot]() { AutoRefillRefillInfoActions::PromptAndRemoveSlot(slot); });
        return {};
    }
    return "unknown follow-up";
}

} // namespace

nlohmann::json AutoRefillAddRefillFilamentsActions::Reply(AutoRefillRefillInfoVM& vm,
                                                          const std::string&      action,
                                                          int                     code,
                                                          const std::string&      msg)
{
    return vm.MakeResponse(vm.GetModule(), "add_refill_filaments", action, code, msg,
                           AutoRefillAddRefillFilamentsBuilder::Build(vm.m_obj, vm.m_session_seq,
                                                                      vm.m_add_refill_query_payload,
                                                                      vm.m_add_refill_query_result ? &*vm.m_add_refill_query_result : nullptr));
}

nlohmann::json AutoRefillAddRefillFilamentsActions::Handle(AutoRefillRefillInfoVM& vm,
                                                           MachineObject*          obj,
                                                           const std::string&      action,
                                                           const nlohmann::json&   payload)
{
    if (action == SV::action::cancel) {
        vm.ClearAddRefillQuery();
        wxGetApp().CallAfter([]() { CloseAutoRefillFilamentsDialog(wxID_CANCEL); });
        return Reply(vm, action, 0, "");
    }

    // Only publishes; the slot list arrives as a `query_result` / `query_failed` report
    if (action == SV::action::query) {
        if (!vm.StartAddRefillQuery()) {
            return Reply(vm, action, 1, "publish failed");
        }
        return Reply(vm, action, 0, "");
    }

    // Sent after the first slot is picked; the device narrows the candidates around it
    if (action == SV::action::query_next) {
        const auto to = payload.find("to");
        if (to == payload.end() || !to->is_object()) {
            return Reply(vm, action, 1, "query_next needs the selected slot");
        }
        const DevAmsSlotId to_slot{to->value("ams_id", -1), to->value("slot_id", -1)};
        if (to_slot.first < 0 || to_slot.second < 0) {
            return Reply(vm, action, 1, "query_next got an invalid slot");
        }
        if (!vm.StartAddRefillQueryNext(to_slot, payload)) {
            return Reply(vm, action, 1, "publish failed");
        }
        return Reply(vm, action, 0, "");
    }

    // The page owns the dialog (copy is in the payload); the host only performs the jump
    if (action == SV::action::follow_up) {
        const auto block_code = static_cast<DevFilaRefillBlockCode>(
            payload.value("block_code", static_cast<int>(DevFilaRefillBlockCode::Available)));
        const BlockCode::FollowUp follow_up = BlockCode::Create(block_code).follow_up;
        const DevAmsSlotId slot{payload.value("ams_id", -1), payload.value("slot_id", -1)};
        const int display_index = payload.value("display_index", 0);

        // A modal opened inside the bridge request would re-enter the WebView2 handler
        const std::weak_ptr<bool> alive = vm.m_alive;
        wxGetApp().CallAfter([alive, follow_up, slot, display_index]() {
            if (const auto guard = alive.lock(); !guard || !*guard) return;
            const std::string failure = run_follow_up(follow_up, slot, display_index);
            if (!failure.empty()) {
                BOOST_LOG_TRIVIAL(warning) << "add_refill_filaments follow_up: " << failure;
            }
        });
        return Reply(vm, action, 0, "");
    }

    if (action == SV::action::dismiss_warning) {
        const auto block_code = static_cast<DevFilaRefillBlockCode>(
            payload.value("block_code", static_cast<int>(DevFilaRefillBlockCode::Available)));
        BlockCode::Suppress(block_code);
        return Reply(vm, action, 0, "");
    }

    // check before show confirm dialog
    if (action == SV::action::check_warning_suppressed) {
        const auto block_code = static_cast<DevFilaRefillBlockCode>(
            payload.value("block_code", static_cast<int>(DevFilaRefillBlockCode::Available)));
        return vm.MakeResponse(vm.GetModule(), "add_refill_filaments", action, 0, "",
                               nlohmann::json{{"suppressed", BlockCode::IsSuppressed(block_code)}});
    }

    if (action == SV::action::confirm) {
        std::vector<DevAmsSlotId> slots;
        const auto slots_it = payload.find("slots");
        if (slots_it != payload.end() && slots_it->is_array()) {
            for (const auto& item : *slots_it) {
                if (!item.is_object()) continue;
                slots.emplace_back(item.value("ams_id", -1), item.value("slot_id", -1));
            }
        }
        if (slots.empty()) {
            return Reply(vm, action, 1, "confirm needs at least one slot");
        }

        const std::string mode = payload.value("mode", std::string(SV::mode::create_group));
        bool published = false;
        if (mode == SV::mode::add_to_group) {
            const auto to = payload.find("to");
            if (to == payload.end() || !to->is_object()) {
                return Reply(vm, action, 1, "add_to_group needs a target slot");
            }
            const DevAmsSlotId group_member{to->value("ams_id", -1), to->value("slot_id", -1)};
            slots.erase(std::remove(slots.begin(), slots.end(), group_member), slots.end());
            if (slots.empty()) {
                return Reply(vm, action, 1, "add_to_group needs at least one new slot");
            }
            published = vm.StartAddRefillToGroup(group_member, slots);
        } else {
            published = vm.StartConfirmAddRefill(slots);
        }
        if (!published) {
            return Reply(vm, action, 1, "publish failed");
        }
        return Reply(vm, action, 0, "");
    }

    (void) obj;
    return Reply(vm, action, 1, "unknown add_refill_filaments action");
}

}} // namespace Slic3r::GUI
