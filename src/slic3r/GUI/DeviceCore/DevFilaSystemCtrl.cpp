#include <algorithm>
#include <cctype>

#include <nlohmann/json.hpp>
#include "DevFilaSystem.h"

#include "slic3r/GUI/DeviceManager.hpp"// TODO: remove this include
#include "DevFilamentWeight.h"
#include "DevUtil.h"
#include "libslic3r/Log.hpp"

using namespace nlohmann;
namespace Slic3r
{

int DevFilaSystem::CtrlAmsReset() const
{
    json jj_command;
    jj_command["print"]["command"] = "ams_reset";
    jj_command["print"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
    return m_owner->publish_json(jj_command);
}

// Mirror the committed weight into the local tray so the AMS view and the device page
// render it on the next timer tick. Editing a slot hold-guards the tray for
// HOLD_COUNT_MAX push cycles, which otherwise swallows the remain that firmware echoes
// back. `remain` is the percentage both views actually render, so it has to move
// together with the gram values; firmware truncates it, and matching that keeps the
// optimistic value identical to the one the next push reports.
static void ApplyLocalFilamentWeight(DevFilaSystem* fila_system, int ams_id, int slot_id, int total, int remain)
{
    if (!fila_system || total <= 0 || remain < 0) {
        return;
    }

    DevAmsTray*       tray       = nullptr;
    const std::string ams_id_str = std::to_string(ams_id);
    if (fila_system->IsVirtualTrayId(ams_id_str)) {
        tray = fila_system->GetVirtualTrayPtr(ams_id_str);
    } else if (auto* ams_unit = fila_system->GetAmsById(ams_id_str)) {
        tray = ams_unit->GetTray(std::to_string(slot_id));
    }

    if (tray) {
        int remain_percent = remain * 100 / total;
        if (remain_percent > 100) { remain_percent = 100; }

        tray->weight   = std::to_string(total);
        tray->remain_g = remain;
        tray->remain   = remain_percent;
    }
}

int DevFilaSystem::CtrlSetFilamentWeight(int ams_id, int slot_id, int total, int remain)
{
    const auto validation = DevFilamentWeight::Validate(total, remain);
    if (!validation.valid) {
        BOOST_LOG_TRIVIAL(warning) << "invalid filament weight, total=" << total << ", remain=" << remain;
        return -1;
    }

    json jj_command;
    jj_command["print"]["command"] = "set_filament_weight";
    jj_command["print"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
    jj_command["print"]["ams_id"] = ams_id;
    jj_command["print"]["slot_id"] = slot_id;
    jj_command["print"]["total"] = total;
    jj_command["print"]["remain"] = remain;
    jj_command["print"]["src_id"] = 1;

    const int rtn = m_owner->publish_json(jj_command);
    if (rtn == 0) {
        ApplyLocalFilamentWeight(this, ams_id, slot_id, total, remain);
    }

    return rtn;
}

// Command replies carry a "result" string alongside errno; anything but "success" means
// the printer refused the command.
static bool IsReplySuccessful(const std::optional<json>& reply)
{
    if (!reply || !reply->contains("result") || !(*reply)["result"].is_string()) {
        return false;
    }

    std::string result = (*reply)["result"].get<std::string>();
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return result == "success";
}

void DevFilaSystem::CtrlSetFilamentWeightAfter(const std::shared_ptr<DevCtrlToken>& token, int ams_id, int slot_id, int total, int remain)
{
    if (!token) {
        CtrlSetFilamentWeight(ams_id, slot_id, total, remain);
        return;
    }

    m_pending_weight_tokens.push_back(token);

    // The token reaches exactly one terminal state, so this runs at most once. It is
    // dispatched via wxTheApp->CallAfter() guarded by a weak_ptr, and the tokens die with
    // this DevFilaSystem, so `this` cannot dangle here.
    auto on_settled = [this, ams_id, slot_id, total, remain](const std::shared_ptr<DevCtrlToken>& settled, DevCtrlTokenStatus status) {
        m_pending_weight_tokens.erase(std::remove(m_pending_weight_tokens.begin(), m_pending_weight_tokens.end(), settled), m_pending_weight_tokens.end());

        if (status != DevCtrlTokenStatus::ReplyReceived || !IsReplySuccessful(settled->GetReply())) {
            BOOST_LOG_TRIVIAL(warning) << "CtrlSetFilamentWeightAfter: ams_filament_setting did not succeed, dropping set_filament_weight for "
                                       << KV(log_key::ams_id, ams_id) << KV(log_key::slot_id, slot_id);
            return;
        }

        CtrlSetFilamentWeight(ams_id, slot_id, total, remain);
    };

    token->SetCallback(DevCtrlTokenStatus::ReplyReceived, on_settled);
    token->SetCallback(DevCtrlTokenStatus::SendFailed, on_settled);
    token->SetCallback(DevCtrlTokenStatus::TimedOut, on_settled);
}

}