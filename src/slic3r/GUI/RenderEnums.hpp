#ifndef slic3r_RenderEnums_hpp_
#define slic3r_RenderEnums_hpp_

namespace Slic3r {
    namespace GUI {

        enum class EMSAAType : uint8_t
        {
            Disabled,
            X2,
            X4,
            // desktop only
            X8,
            X16
        };

        enum class EPixelFormat : uint16_t
        {
            Unknow,
            R,
            RG,
            RGB,
            RGBA,
            DepthComponent,
            StencilIndex,
            DepthAndStencil
        };

        enum class EPixelDataType : uint16_t
        {
            Unknow,
            UByte,
            Byte,
            UShort,
            Short,
            UInt,
            Int,
            Float
        };

        enum class ETextureFormat : uint16_t
        {
            R8,
            R32F,
            RG32F,
            RGBA8,
            RGBA32F
        };

        enum class ESamplerType : uint8_t
        {
            Sampler2D,
            Sampler2DArray,
            SamplerBuffer,
        };

        enum class ESamplerFilterMode : uint8_t {
            Nearset = 0,
            Linear = 1
        };

        enum class EDrawPrimitiveType : uint8_t {
            Points,
            Triangles,
            TriangleStrip,
            TriangleFan,
            Lines,
            LineStrip,
            LineLoop
        };

    } // namespace GUI
} // namespace Slic3r

#endif // slic3r_RenderEnums_hpp_