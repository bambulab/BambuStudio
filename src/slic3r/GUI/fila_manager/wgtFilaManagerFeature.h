#ifndef slic3r_GUI_wgtFilaManagerFeature_h
#define slic3r_GUI_wgtFilaManagerFeature_h

#include <string>

namespace Slic3r { namespace GUI {

constexpr const char* FilaManagerEnabledConfigKey = "studio_enable_fila_manager";

bool is_fila_manager_disabled_by_config(const std::string& enabled_value, bool is_macos);

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_wgtFilaManagerFeature_h
