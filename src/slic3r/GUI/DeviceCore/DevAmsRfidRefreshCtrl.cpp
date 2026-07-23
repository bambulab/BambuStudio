#include "DevAmsRfidRefreshCtrl.h"

#include "DevFilaSystem.h"
#include "DevNozzleRack.h"
#include "DevNozzleSystem.h"

#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/GUI_App.hpp"

#include <algorithm>
#include <cctype>

namespace Slic3r {

std::shared_ptr<DevAmsRfidRefreshCtrl> DevAmsRfidRefreshCtrl::Create(DevFilaSystem *owner)
{
    return std::shared_ptr<DevAmsRfidRefreshCtrl>(new DevAmsRfidRefreshCtrl(owner));
}

DevAmsRfidRefreshCtrl::DevAmsRfidRefreshCtrl(DevFilaSystem *owner) : m_owner(owner) {}

DevAmsRfidRefreshCtrl::~DevAmsRfidRefreshCtrl()
{
    Detach();
}

bool DevAmsRfidRefreshCtrl::CanRefresh() const
{
    MachineObject *obj = m_owner ? m_owner->GetOwner() : nullptr;
    return obj && !obj->is_in_printing() && !obj->is_in_upgrading();
}

std::optional<int> DevAmsRfidRefreshCtrl::CollectTargetAmsId() const
{
    if (!m_owner)
        return std::nullopt;

    for (const auto &[_, ams] : m_owner->GetAmsList()) {
        if (ams && ams->GetAmsType() == DevAmsType::ZMULTI_B && ams->IsExist() &&
            ams->SupportsFilamentTabAutoReadRfid())
            return ams->GetAmdIdInt();
    }
    return std::nullopt;
}

int DevAmsRfidRefreshCtrl::PublishAutoRefresh(int ams_id)
{
    MachineObject *obj = m_owner ? m_owner->GetOwner() : nullptr;
    if (!obj)
        return -1;

    nlohmann::json command;
    command["print"]["command"]     = "ams_get_slots_rfid";
    command["print"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
    command["print"]["ams_id"]      = ams_id;
    command["print"]["mode"]        = 0;
    return obj->publish_json(command);
}

std::shared_ptr<DevCtrlToken> DevAmsRfidRefreshCtrl::PublishProbe(int ams_id)
{
    MachineObject *obj = m_owner ? m_owner->GetOwner() : nullptr;
    if (!obj)
        return nullptr;

    const std::string sequence_id = std::to_string(MachineObject::m_sequence_id++);
    nlohmann::json command;
    command["print"]["command"]     = "ams_get_slots_rfid";
    command["print"]["sequence_id"] = sequence_id;
    command["print"]["ams_id"]      = ams_id;
    command["print"]["mode"]        = 0;
    command["print"]["try"]         = true;

    // The token manager owns the lifecycle and matches the reply by sequence_id.
    // The caller wires its own callbacks and parses the reply via ParseProbeReply().
    const auto token = obj->GetCtrlTokenMgr().Build(sequence_id);
    if (!token)
        return nullptr;

    token->SetSendResult(obj->publish_json(command));
    return token;
}

int DevAmsRfidRefreshCtrl::TriggerRefresh()
{
    if (!m_owner || !m_owner->GetAmsSystemSetting().IsFilamentTabAutoReadRfidEnabled() || !CanRefresh())
        return 0;

    const auto ams_id = CollectTargetAmsId();
    return ams_id && PublishAutoRefresh(*ams_id) == 0 ? 1 : 0;
}

std::shared_ptr<DevCtrlToken> DevAmsRfidRefreshCtrl::ProbeRefreshNeed()
{
    if (!CanRefresh())
        return nullptr;

    const auto ams_id = CollectTargetAmsId();
    return ams_id ? PublishProbe(*ams_id) : nullptr;
}

void DevAmsRfidRefreshCtrl::Detach()
{
    // Token lifecycle is owned by MachineObject; only drop the back-pointer here.
    m_owner = nullptr;
}

DevAmsRfidProbeStatus DevAmsRfidRefreshCtrl::ParseProbeReply(const nlohmann::json &reply)
{
    if (reply.contains("try") && reply.contains("result")) {
        const auto& value = reply["result"];
        if (value.is_string()) {
            std::string text = value.get<std::string>();
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            if (text == "success")
                return DevAmsRfidProbeStatus::NeedRefresh;
            if (text == "fail")
                return DevAmsRfidProbeStatus::NoRefreshNeeded;
        }
    }

    return DevAmsRfidProbeStatus::Unavailable;
}

} // namespace Slic3r
