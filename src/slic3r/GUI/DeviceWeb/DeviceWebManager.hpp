// src/slic3r/GUI/DeviceWeb/DeviceWebManager.hpp
#ifndef DEVICEWEBMANAGER_HPP
#define DEVICEWEBMANAGER_HPP

#include <string>
#include <unordered_map>
#include <memory>
#include <optional>
#include <nlohmann/json.hpp>

namespace Slic3r { namespace GUI {

class IViewModel;
class DeviceWebBridge;

class DeviceWebManager {
public:
    DeviceWebManager() = default;
    ~DeviceWebManager() = default;

    // Register a ViewModel. The manager takes ownership.
    void Register(std::unique_ptr<IViewModel> vm);

    // Route a web command to the matching ViewModel.
    // Returns the response body JSON, or nullopt if no module matched.
    std::optional<nlohmann::json> Dispatch(
        const nlohmann::json& body
    );

    // Set the SDK reference on all registered ViewModels.
    void SetBridge(DeviceWebBridge* bridge);

    // Notify all registered ViewModels that the system colour mode changed.
    void NotifyColorChanged();

    // Ask one module to push its current state to the frontend bridge.
    void NotifyState(const std::string& module,
                     const std::string& submod,
                     const std::string& action);

private:
    std::unordered_map<std::string, std::unique_ptr<IViewModel>> m_vms;
};

}} // namespace Slic3r::GUI

#endif // DEVICEWEBMANAGER_HPP
