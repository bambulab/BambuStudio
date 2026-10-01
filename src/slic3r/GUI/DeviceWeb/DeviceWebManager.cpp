// src/slic3r/GUI/DeviceWeb/DeviceWebManager.cpp
#include "DeviceWebManager.hpp"
#include "IViewModel.hpp"
#include "DeviceWebBridge.hpp"

#include <boost/log/trivial.hpp>

namespace Slic3r { namespace GUI {

void DeviceWebManager::Register(std::unique_ptr<IViewModel> vm)
{
    auto mod = vm->GetModule();
    BOOST_LOG_TRIVIAL(info) << "DeviceWebManager: register module '" << mod << "'";
    m_vms[mod] = std::move(vm);
}

std::optional<nlohmann::json> DeviceWebManager::Dispatch(const nlohmann::json& body)
{
    if (!body.contains("module") || !body.contains("submod") || !body.contains("action")) {
        BOOST_LOG_TRIVIAL(warning) << "DeviceWebManager: missing module/submod/action in body";
        return std::nullopt;
    }

    std::string mod    = body["module"].get<std::string>();
    std::string submod = body["submod"].get<std::string>();
    std::string action = body["action"].get<std::string>();

    auto it = m_vms.find(mod);
    if (it == m_vms.end()) {
        BOOST_LOG_TRIVIAL(warning) << "DeviceWebManager: unknown module '" << mod << "'";
        return std::nullopt;
    }

    nlohmann::json payload = body.contains("payload") ? body["payload"] : nlohmann::json::object();
    return it->second->OnCommand(submod, action, payload);
}

void DeviceWebManager::SetBridge(DeviceWebBridge* bridge)
{
    for (auto& [name, vm] : m_vms) {
        vm->SetBridge(bridge);
    }
}

void DeviceWebManager::NotifyColorChanged()
{
    for (auto& [name, vm] : m_vms) {
        vm->OnSysColorChanged();
    }
}

void DeviceWebManager::NotifyState(const std::string& module,
                                   const std::string& submod,
                                   const std::string& action)
{
    auto it = m_vms.find(module);
    if (it == m_vms.end()) {
        BOOST_LOG_TRIVIAL(warning) << "DeviceWebManager: cannot notify unknown module '" << module << "'";
        return;
    }

    it->second->ReportState(submod, action);
}

}} // namespace Slic3r::GUI
