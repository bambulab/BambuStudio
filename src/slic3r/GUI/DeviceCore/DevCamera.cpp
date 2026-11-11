#include "DevCamera.h"

#include "libslic3r/Log.hpp"

namespace Slic3r {

static DevCamera::IpcamReadyState _parse_ipcam_ready_state(const std::string &dev)
{
    if (dev == "1")
        return DevCamera::IPCAM_READY;
    if (dev == "2")
        return DevCamera::IPCAM_RELEASED;
    return DevCamera::IPCAM_NOT_PRESENT;
}

void DevCamera::ParseIpcam(const nlohmann::json &ipcam)
{
    if (ipcam.contains("ipcam_dev") && ipcam["ipcam_dev"].is_string()) {
        const std::string dev       = ipcam["ipcam_dev"].get<std::string>();
        IpcamReadyState   new_state = _parse_ipcam_ready_state(dev);
        if (new_state != m_ipcam_ready_state) {
            m_ipcam_ready_state = new_state;
        }
    }
}

} // namespace Slic3r
