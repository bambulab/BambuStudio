#include "DeviceWebPage.hpp"

namespace Slic3r { namespace GUI {

DeviceWebPage::DeviceWebPage(wxWindow *parent)
    : DeviceWebHost(parent, DeviceWebHostMode::FilamentManager, "/filament_manager")
{
}

}}
