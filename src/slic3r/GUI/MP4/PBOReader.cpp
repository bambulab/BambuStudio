#include "PBOReader.hpp"
#include "slic3r/GUI/3DScene.hpp"  // glsafe
#include <GL/glew.h>

namespace Slic3r {
namespace GUI {

PBOReader::PBOReader() = default;

PBOReader::~PBOReader()
{
    release();
}

void PBOReader::release()
{
    if (m_pbos[0]) {
        glsafe(::glDeleteBuffers(2, m_pbos));
        m_pbos[0] = m_pbos[1] = 0;
    }
    m_frame_count = 0;
    m_index = 0;
}

void PBOReader::resize(uint32_t width, uint32_t height, uint32_t channels)
{
    if (width == m_width && height == m_height && channels == m_channels && m_pbos[0] != 0)
        return;

    release();

    m_width    = width;
    m_height   = height;
    m_channels = channels;

    const GLsizeiptr buf_size = static_cast<GLsizeiptr>(byte_size());

    glsafe(::glGenBuffers(2, m_pbos));
    for (int i = 0; i < 2; ++i) {
        glsafe(::glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbos[i]));
        glsafe(::glBufferData(GL_PIXEL_PACK_BUFFER, buf_size, nullptr, GL_STREAM_READ));
    }
    glsafe(::glBindBuffer(GL_PIXEL_PACK_BUFFER, 0));
}

void PBOReader::begin_read()
{
    if (!m_pbos[0])
        return;

    glsafe(::glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbos[m_index]));
    glsafe(::glReadPixels(0, 0, m_width, m_height, GL_RGBA, GL_UNSIGNED_BYTE, nullptr));
    glsafe(::glBindBuffer(GL_PIXEL_PACK_BUFFER, 0));

    // Flip index so next begin_read writes the other PBO
    m_index = 1 - m_index;
    ++m_frame_count;
}

const uint8_t* PBOReader::map_previous()
{
    if (m_frame_count < 2)
        return nullptr;

    // begin_read() wrote PBO[X] then flipped m_index to 1-X.
    // So the last-written PBO is at index (1 - m_index).
    const int prev = 1 - m_index;
    glsafe(::glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbos[prev]));
    const uint8_t* ptr = static_cast<const uint8_t*>(::glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY));
    return ptr;
}

void PBOReader::unmap()
{
    ::glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    glsafe(::glBindBuffer(GL_PIXEL_PACK_BUFFER, 0));
}

} // namespace GUI
} // namespace Slic3r
