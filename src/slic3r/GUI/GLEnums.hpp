#ifndef slic3r_GUI_GLEnums_hpp_
#define slic3r_GUI_GLEnums_hpp_
namespace Slic3r {
namespace GUI {

    enum class EPickingEffect
    {
        Disabled,
        StencilOutline,
        Silhouette
    };

    enum class ERenderPipelineStage
    {
        Normal,
        Silhouette
    };

} // namespace Slic3r
} // namespace GUI

#endif // slic3r_GUI_GLEnums_hpp_