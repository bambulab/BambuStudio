#ifndef slic3r_wgtFilaManagerColorType_h_
#define slic3r_wgtFilaManagerColorType_h_

#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/EncodedFilament.hpp"

#include <cstddef>

namespace Slic3r { namespace GUI {

// Filament Manager's canonical color type matches the AMS/cloud/web wire
// semantics: 0=gradient, 1=multicolor, 2=single. DevFilaColorType's legacy
// enumerator names are misleading, so never switch on those names here.
enum class FilaManagerColorType : int
{
    Gradient   = 0,
    MultiColor = 1,
    Single     = 2,
};

inline FilaManagerColorType fallback_fila_manager_color_type(std::size_t color_count)
{
    return color_count > 1 ? FilaManagerColorType::MultiColor : FilaManagerColorType::Single;
}

inline FilaManagerColorType normalize_fila_manager_color_type(int color_type, std::size_t color_count = 1)
{
    switch (color_type) {
        case 0: return FilaManagerColorType::Gradient;
        case 1: return FilaManagerColorType::MultiColor;
        case 2: return FilaManagerColorType::Single;
        default: return fallback_fila_manager_color_type(color_count);
    }
}

inline int to_fila_manager_color_type_int(FilaManagerColorType color_type)
{
    return static_cast<int>(color_type);
}

inline int from_ams_color_type(DevFilaColorType ctype, std::size_t color_count = 1)
{
    switch (static_cast<int>(ctype)) {
        case 0: return 0;
        case 1: return 1;
        case 2: return 2;
    }
    return to_fila_manager_color_type_int(fallback_fila_manager_color_type(color_count));
}

inline FilamentColor::ColorType to_filament_color_type(int spool_color_type, std::size_t color_count = 1)
{
    const FilaManagerColorType normalized = normalize_fila_manager_color_type(spool_color_type, color_count);
    switch (normalized) {
        case FilaManagerColorType::Gradient:   return FilamentColor::ColorType::GRADIENT_CLR;
        case FilaManagerColorType::MultiColor: return FilamentColor::ColorType::MULTI_CLR;
        case FilaManagerColorType::Single:     return FilamentColor::ColorType::SINGLE_CLR;
    }
    return FilamentColor::ColorType::SINGLE_CLR;
}

inline int from_filament_color_type(FilamentColor::ColorType t)
{
    switch (t) {
        case FilamentColor::ColorType::GRADIENT_CLR: return 0;
        case FilamentColor::ColorType::MULTI_CLR:    return 1;
        default:                                     return 2;
    }
}

}} // namespace Slic3r::GUI

#endif // slic3r_wgtFilaManagerColorType_h_
