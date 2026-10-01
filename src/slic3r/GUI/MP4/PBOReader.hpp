#ifndef slic3r_PBOReader_hpp_
#define slic3r_PBOReader_hpp_

#include <cstdint>
#include <vector>

namespace Slic3r {
namespace GUI {

// Asynchronous pixel readback using double-buffered PBOs (ping-pong).
// Typical usage:
//   1. begin_read()   – issues glReadPixels into a PBO (async, no stall)
//   2. map_previous() – maps the *other* PBO that was filled end frame
//   3. unmap()        – unmaps so it can be reused next frame
class PBOReader
{
public:
    PBOReader();
    ~PBOReader();

    PBOReader(const PBOReader&) = delete;
    PBOReader& operator=(const PBOReader&) = delete;

    // (Re-)allocate PBOs when viewport size changes.
    void resize(uint32_t width, uint32_t height, uint32_t channels = 4);

    uint32_t width()    const { return m_width; }
    uint32_t height()   const { return m_height; }
    uint32_t channels() const { return m_channels; }
    size_t   byte_size() const { return static_cast<size_t>(m_width) * m_height * m_channels; }

    // Issue an async glReadPixels into the current PBO.  Call at end of frame.
    void begin_read();

    // Map the PBO that was written the *previous* frame.
    // Returns nullptr if nothing is ready yet (first frame) or on error.
    const uint8_t* map_previous();

    // Unmap after you have consumed the data.
    void unmap();

    bool is_initialized() const { return m_pbos[0] != 0; }

private:
    void release();

    uint32_t m_pbos[2]{0, 0};
    uint32_t m_width    = 0;
    uint32_t m_height   = 0;
    uint32_t m_channels = 4;
    int      m_index    = 0;   // toggles 0/1 each frame
    int      m_frame_count = 0;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_PBOReader_hpp_
