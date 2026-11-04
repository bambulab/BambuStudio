// src/slic3r/GUI/DeviceWeb/ViewModels/PrintQueue/PrintQueueVM.hpp
#ifndef PRINTQUEUEVM_HPP
#define PRINTQUEUEVM_HPP

#include <functional>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "slic3r/GUI/DeviceWeb/IViewModel.hpp"

namespace Slic3r { namespace GUI {

// Bridges the print queue web page to PrintQueueManager. Commands are
// acknowledged first and run off the UI thread; the page always redraws from
// a pushed `queue/list` snapshot, never from polling.
class PrintQueueVM : public IViewModel
{
public:
    PrintQueueVM();
    ~PrintQueueVM() override;

    std::string GetModule() const override { return "print_queue"; }

    nlohmann::json OnCommand(const std::string& submod,
                             const std::string& action,
                             const nlohmann::json& payload) override;

    void ReportState(const std::string& submod, const std::string& action) override;

    void SetDeviceId(const std::string& device_id);
    const std::string& GetDeviceId() const { return m_device_id; }

private:
    using QueueTask = std::function<int(std::string&)>;

    nlohmann::json MakeResp(const std::string& submod,
                            const std::string& action,
                            int code,
                            const std::string& msg = "",
                            const nlohmann::json& payload = nlohmann::json::object());

    nlohmann::json HandleProject(const std::string& action, const nlohmann::json& payload);
    nlohmann::json HandlePlate(const std::string& action, const nlohmann::json& payload);

    // Pass an empty task to only refresh.
    void RunAsync(QueueTask task);
    void PublishQueue(int result, const std::string& response_body);

    static nlohmann::json BuildProjects(const std::string& response_body);

    std::string m_device_id;
    // Invalidated in the destructor so queued CallAfter lambdas become no-ops.
    std::shared_ptr<int> m_alive;
};

}} // namespace Slic3r::GUI

#endif // PRINTQUEUEVM_HPP
