#ifndef DEVICEWEBPAGE_H
#define DEVICEWEBPAGE_H


#include "DeviceWebHost.hpp"

namespace Slic3r {
namespace GUI {


class DeviceWebPage: public DeviceWebHost {
public:
    DeviceWebPage(wxWindow *parent);
    ~DeviceWebPage() override = default;
};

} // GUI
} // Slic3r


#endif // DEVICEWEBPAGE_H