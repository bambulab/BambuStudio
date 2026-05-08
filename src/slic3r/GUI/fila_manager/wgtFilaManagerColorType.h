#ifndef slic3r_wgtFilaManagerColorType_h_
#define slic3r_wgtFilaManagerColorType_h_

#include "slic3r/GUI/EncodedFilament.hpp"

namespace Slic3r { namespace GUI {

// STUDIO-17977 / openspec 20260506耗材管理器AMS同步渐变多拼色 § 9.5:
// Bridge between two divergent ColorType enums that this codebase has
// historically maintained.
//
//   swagger / DevFilaColorType / FilamentSpool::color_type :
//       0 = gradient
//       1 = multicolor
//       2 = single colour
//
//   FilamentColor::ColorType (src/slic3r/GUI/EncodedFilament.hpp:75-80) :
//       SINGLE_CLR   = 0
//       MULTI_CLR    = 1
//       GRADIENT_CLR = 2
//
// A naive `static_cast<FilamentColor::ColorType>(spool_color_type)` would
// silently scramble these spaces (e.g. swagger 0 "gradient" would land on
// FilamentColor::ColorType::SINGLE_CLR). The two helpers below MUST be the
// only way the fila_manager / DeviceWeb layer crosses between the two
// enum spaces; design.md § 9.5 calls this out as a hard rule.

inline FilamentColor::ColorType to_filament_color_type(int spool_color_type)
{
    switch (spool_color_type) {
        case 0: return FilamentColor::ColorType::GRADIENT_CLR; // 渐变
        case 1: return FilamentColor::ColorType::MULTI_CLR;    // 拼色
        default: return FilamentColor::ColorType::SINGLE_CLR;  // 单色 (defensive)
    }
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
