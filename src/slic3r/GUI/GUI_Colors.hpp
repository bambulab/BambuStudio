#ifndef slic3r_GUI_Colors_hpp_
#define slic3r_GUI_Colors_hpp_

#include "imgui/imgui.h"

enum RenderCol_ {
    RenderCol_3D_Background = 0,
    RenderCol_Plate_Unselected,
    RenderCol_Plate_Selected,
    RenderCol_Plate_Default,
    RenderCol_Plate_Line_Top,
    RenderCol_Plate_Line_Bottom,
    RenderCol_Model_Disable,
    RenderCol_Model_Unprintable,
    RenderCol_Model_Neutral,
    RenderCol_Part,
    RenderCol_Modifier,
    RenderCol_Negtive_Volume,
    RenderCol_Support_Enforcer,
    RenderCol_Support_Blocker,
    RenderCol_Axis_X,
    RenderCol_Axis_Y,
    RenderCol_Axis_Z,
    RenderCol_Grabber_X,
    RenderCol_Grabber_Y,
    RenderCol_Grabber_Z,
    RenderCol_Flatten_Plane,
    RenderCol_Flatten_Plane_Hover,
    RenderCol_Count,
};

typedef int RenderCol;

namespace Slic3r {

class RenderColor {
public:
    static ImVec4      colors[RenderCol_Count];
};
const char* GetRenderColName(RenderCol idx);
inline std::array<float, 4> GLColor(ImVec4 color) {
    return {color.x, color.y, color.z, color.w };
}

inline ImVec4 IMColor(std::array<float, 4> color) {
    return ImVec4(color[0], color[1], color[2], color[3]);
}

}

#endif
