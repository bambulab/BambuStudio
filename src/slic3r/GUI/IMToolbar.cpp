#include "IMToolbar.hpp"

#include "3DScene.hpp"
#include <GL/glew.h>
#include <imgui/imgui_internal.h>
#include <imgui/imgui.h>

#include "nanosvg/nanosvg.h"
#include "nanosvg/nanosvgrast.h"
#include "libslic3r/GCode/ThumbnailData.hpp"

namespace Slic3r {
namespace GUI {

bool IMToolbarItem::generate_texture()
{
    bool compress = false;
    GLint last_texture;
    unsigned m_image_texture{ 0 };
    unsigned char* pixels = (unsigned char*)(&image_data[0]);

    glsafe(::glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture));
    glsafe(::glGenTextures(1, &m_image_texture));
    glsafe(::glBindTexture(GL_TEXTURE_2D, m_image_texture));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    glsafe(::glPixelStorei(GL_UNPACK_ROW_LENGTH, 0));
    if (compress && GLEW_EXT_texture_compression_s3tc)
        glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, image_width, image_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels));
    else
        glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image_width, image_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels));

    // Store our identifier
    texture_id = (ImTextureID)(intptr_t)m_image_texture;

    // Restore state
    glsafe(::glBindTexture(GL_TEXTURE_2D, last_texture));

    return true;
}

void IMToolbar::del_all_item()
{
    for (int i = 0; i < m_items.size(); i++) {
        delete m_items[i];
        m_items[i] = nullptr;
    }
    m_items.clear();
}


// static 
static bool get_data_from_svg(const std::string& filename, unsigned int max_size_px, ThumbnailData &thumbnail_data)
{
    bool compression_enabled = false;

    NSVGimage* image = nsvgParseFromFile(filename.c_str(), "px", 96.0f);
    if (image == nullptr) {
        return false;
    }

    float scale = (float)max_size_px / std::max(image->width, image->height);

    thumbnail_data.width = (int)(scale * image->width);
    thumbnail_data.height = (int)(scale * image->height);

    int n_pixels = thumbnail_data.width * thumbnail_data.height;

    if (n_pixels <= 0) {
        nsvgDelete(image);
        return false;
    }

    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (rast == nullptr) {
        nsvgDelete(image);
        return false;
    }

    // creates the temporary buffer only once, with max size, and reuse it for all the levels, if generating mipmaps
    std::vector<unsigned char> data(n_pixels * 4, 0);
    thumbnail_data.pixels = data;
    nsvgRasterize(rast, image, 0, 0, scale, thumbnail_data.pixels.data(), thumbnail_data.width, thumbnail_data.height, thumbnail_data.width * 4);

    // we manually generate mipmaps because glGenerateMipmap() function is not reliable on all graphics cards
    int lod_w = thumbnail_data.width;
    int lod_h = thumbnail_data.height;
    GLint level = 0;
    while (lod_w > 1 || lod_h > 1) {
        ++level;

        lod_w = std::max(lod_w / 2, 1);
        lod_h = std::max(lod_h / 2, 1);
        scale /= 2.0f;

        data.resize(lod_w * lod_h * 4);

        nsvgRasterize(rast, image, 0, 0, scale, data.data(), lod_w, lod_h, lod_w * 4);
    }

    nsvgDeleteRasterizer(rast);
    nsvgDelete(image);

    return true;
}

bool IMReturnToolbar::init()
{
    bool compress = false;
    GLint last_texture;
    unsigned m_image_texture{ 0 };

    std::string path = resources_dir() + "/icons/";
    std::string file_name;

    file_name = path + "assemble_return.svg";

    ThumbnailData data;
    if (!get_data_from_svg(file_name, 20, data))
        return false;

    unsigned char* pixels = (unsigned char*)(&data.pixels[0]);
    glsafe(::glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture));
    glsafe(::glGenTextures(1, &m_image_texture));
    glsafe(::glBindTexture(GL_TEXTURE_2D, m_image_texture));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    glsafe(::glPixelStorei(GL_UNPACK_ROW_LENGTH, 0));
    if (compress && GLEW_EXT_texture_compression_s3tc)
        glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, data.width, data.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels));
    else
        glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, data.width, data.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels));

    // Store our identifier
    texture_id = (ImTextureID)(intptr_t)m_image_texture;

    // Restore state
    glsafe(::glBindTexture(GL_TEXTURE_2D, last_texture));

    return true;
}

} // namespace GUI
} // namespace Slic3r
