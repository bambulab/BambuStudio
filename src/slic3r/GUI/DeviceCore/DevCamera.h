#pragma once
#include <memory>
#include <nlohmann/json.hpp>

namespace Slic3r {

class MachineObject;

// Printer camera module. Data source: print.ipcam
class DevCamera
{
public:
    // print.ipcam.ipcam_dev
    enum IpcamReadyState : int {
        IPCAM_NOT_PRESENT = 0,
        IPCAM_READY       = 1,
        IPCAM_RELEASED    = 2, // destroyed by printer due to limited resources
    };

    static std::shared_ptr<DevCamera> Create(MachineObject *obj) { return std::shared_ptr<DevCamera>(new DevCamera(obj)); }

public: // getter
    IpcamReadyState GetIpcamReadyState() const { return m_ipcam_ready_state; }
    bool            IsIpcamReady() const { return m_ipcam_ready_state == IPCAM_READY; }

public: // setter
    void ParseIpcam(const nlohmann::json &ipcam);

protected:
    DevCamera(MachineObject *obj) : m_owner(obj) {}

private:
    MachineObject  *m_owner = nullptr;
    IpcamReadyState m_ipcam_ready_state = IPCAM_READY;
};

} // namespace Slic3r
