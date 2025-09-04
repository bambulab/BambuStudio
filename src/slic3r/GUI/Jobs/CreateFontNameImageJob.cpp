#include "CreateFontNameImageJob.hpp"

// rasterization of ExPoly
#include "libslic3r/SLA/AGGRaster.hpp"

#include "slic3r/Utils/WxFontUtils.hpp"
#include "slic3r/GUI/3DScene.hpp" // ::glsafe

// ability to request new frame after finish rendering
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"

#include "wx/fontenum.h"

#include <boost/log/trivial.hpp>

using namespace Slic3r;
using namespace Slic3r::GUI;

const std::string CreateFontImageJob::default_text = "AaBbCc123";

CreateFontImageJob::CreateFontImageJob(FontImageData &&input)
    : m_input(std::move(input))
{
    assert(wxFontEnumerator::IsValidFacename(m_input.font_name));
    assert(m_input.gray_level > 0 && m_input.gray_level < 255);
    assert(m_input.texture_id != 0);
}

void CreateFontImageJob::process(Ctl &ctl)
{
    auto font_file_with_cache = Slic3r::GUI::BackupFonts::gener_font_with_cache(m_input.font_name, m_input.encoding);
    if (!font_file_with_cache.has_value()) {
        return;
    }
    // use only first line of text
    std::string& text = m_input.text;
    if (text.empty())
        text = default_text; // copy

    size_t enter_pos = text.find('\n');
    if (enter_pos < text.size()) {
        // text start with enter
        if (enter_pos == 0)
            return;
        // exist enter, soo delete all after enter
        text = text.substr(0, enter_pos);
    }

    std::function<bool()> was_canceled = [&ctl, cancel = m_input.cancel]() -> bool {
        if (ctl.was_canceled()) return true;
        if (cancel->load()) return true;
        return false;
    };
    auto ft_fn = []() {
        return Slic3r::GUI::BackupFonts::backup_fonts;
    };
    FontProp fp; // create default font parameters
    auto &ff  = *font_file_with_cache.font_file;
    double standard_scale  = get_text_shape_scale(fp, ff);
    bool        support_backup_fonts = GUI::wxGetApp().app_config->get_bool("support_backup_fonts");
    EmbossShape emboss_shape;
    ExPolygons  shapes           = support_backup_fonts ? Emboss::text2shapes(emboss_shape, font_file_with_cache, text.c_str(), fp, standard_scale, was_canceled, ft_fn) :
                                                          Emboss::text2shapes(emboss_shape, font_file_with_cache, text.c_str(), fp, standard_scale, was_canceled);
    m_input.generate_origin_text = true;
    if (shapes.empty()) {// select some character from font e.g. default text
        m_input.generate_origin_text = false;
        shapes                       = Emboss::text2shapes(emboss_shape, font_file_with_cache, default_text.c_str(), fp, standard_scale, was_canceled, ft_fn);
    }

    if (shapes.empty()) {
        m_input.cancel->store(true);
        return;
    }

    // normalize height of font
    BoundingBox bounding_box;
    for (const ExPolygon &shape : shapes)
        bounding_box.merge(BoundingBox(shape.contour.points));
    if (bounding_box.size().x() < 1 || bounding_box.size().y() < 1) {
        m_input.cancel->store(true);
        return;
    }
    double scale = m_input.size.y() / (double) bounding_box.size().y();
    BoundingBoxf bb2(bounding_box.min.cast<double>(),
                     bounding_box.max.cast<double>());
    bb2.scale(scale);
    Vec2d size_f = bb2.size();
    m_tex_size = Point(std::ceil(size_f.x()), std::ceil(size_f.y()));
    // crop image width
    if (m_tex_size.x() > m_input.size.x())
        m_tex_size.x() = m_input.size.x();
    if (m_tex_size.y() > m_input.size.y())
        m_tex_size.y() = m_input.size.y();

    // Set up result
    unsigned bit_count = 4; // RGBA
    m_result = std::vector<unsigned char>(m_tex_size.x() * m_tex_size.y() * bit_count, {255});

    sla::Resolution resolution(m_tex_size.x(), m_tex_size.y());
    double pixel_dim = SCALING_FACTOR / scale;
    sla::PixelDim dim(pixel_dim, pixel_dim);
    double gamma = 1.;
    std::unique_ptr<sla::RasterBase> raster = sla::create_raster_grayscale_aa(resolution, dim, gamma);
    for (ExPolygon &shape : shapes)
        shape.translate(-bounding_box.min);
    for (const ExPolygon &shape : shapes)
        raster->draw(shape);

    // copy rastered data to pixels
    sla::RasterEncoder encoder =
        [&pix = m_result, w = m_tex_size.x(), h = m_tex_size.y(),
         gray_level = m_input.gray_level]
    (const void *ptr, size_t width, size_t height, size_t num_components) {
        size_t size {static_cast<size_t>(w*h)};
        const unsigned char *ptr2 = (const unsigned char *) ptr;
        for (size_t x = 0; x < width; ++x)
            for (size_t y = 0; y < height; ++y) {
                size_t index = y*w + x;
                assert(index < size);
                if (index >= size) continue;
                pix[3+4*index] = ptr2[y * width + x] / gray_level;
            }
        return sla::EncodedRaster();
    };
    raster->encode(encoder);
}

void CreateFontImageJob::finalize(bool canceled, std::exception_ptr &)
{
    if (m_input.count_opened_font_files)
        --(*m_input.count_opened_font_files);
    if (canceled || m_input.cancel->load()) return;

    *m_input.is_created = true;

    // Exist result bitmap with preview?
    // (not valid input. e.g. not loadable font)
    if (m_result.empty()) {
        // TODO: write text cannot load into texture
        m_result = std::vector<unsigned char>(m_tex_size.x() * m_tex_size.y() * 4, {255});
    }

    // upload texture on GPU
    const GLenum target = GL_TEXTURE_2D;
    glsafe(::glBindTexture(target, m_input.texture_id));

    GLsizei w = m_tex_size.x(), h = m_tex_size.y();
    GLint   xoffset = 0; // align to left
    auto texture_w = m_input.size.x();
    GLint yoffset = m_input.size.y() * m_input.index;
    // clear rest of texture
    std::vector<unsigned char> empty_data(texture_w * m_tex_size.y() * 4, {0});
    glsafe(::glTexSubImage2D(target, m_input.level, 0, yoffset, texture_w, h, m_input.format, m_input.type, empty_data.data()));
    if (m_input.generate_origin_text) { // valid texture
        glsafe(::glTexSubImage2D(target, m_input.level, xoffset, yoffset, w, h, m_input.format, m_input.type, m_result.data()));
    }
    // bind default texture
    GLuint no_texture_id = 0;
    glsafe(::glBindTexture(target, no_texture_id));

    // show rendered texture
    wxGetApp().plater()->canvas3D()->schedule_extra_frame(0);

    BOOST_LOG_TRIVIAL(info)
        << "Generate Preview font('" << m_input.font_name << "' id:" << m_input.index << ") "
        << "with text: '" << m_input.text << "' "
        << "texture_size " << m_input.size.x() << " x " << m_input.size.y();
}

std::vector<Slic3r::Emboss::FontFileWithCache> Slic3r::GUI::BackupFonts::backup_fonts;
void Slic3r::GUI::BackupFonts::generate_backup_fonts() {
    auto language = wxGetApp().app_config->get("language");
    auto custom_back_font_name = wxGetApp().app_config->get("custom_back_font_name");
    if (backup_fonts.empty()) {
         size_t             idx  = language.find('_');
         std::string        lang = (idx == std::string::npos) ? language : language.substr(0, idx);
         std::vector<wxString> font_names;
#ifdef _WIN32
         font_names.emplace_back(wxString(L"宋体"));//chinese confirm
         font_names.emplace_back(wxString::FromUTF8("MS Gothic")); // Japanese
         font_names.emplace_back(wxString::FromUTF8("NanumGothic")); // Korean
         font_names.emplace_back(wxString::FromUTF8("Arial")); // Arabic
#endif
#ifdef __APPLE__
         font_names.emplace_back(wxString::FromUTF8("Songti SC"));//chinese confirm
         font_names.emplace_back(wxString::FromUTF8("SimSong"));        // Japanese//mac special
         font_names.emplace_back(wxString::FromUTF8("Nanum Gothic"));           // Korean//mac need space
         font_names.emplace_back(wxString::FromUTF8("Arial")); // Arabic
#endif
#ifdef __linux__
         font_names.emplace_back(wxString(L"宋体"));         // chinese confirm
         font_names.emplace_back(wxString("AR PL UMing CN"));// chinese confirm
         font_names.emplace_back(wxString::FromUTF8("MS Gothic"));   // Japanese
         font_names.emplace_back(wxString::FromUTF8("NanumGothic")); // Korean
         font_names.emplace_back(wxString::FromUTF8("Arial"));       // Arabic
#endif
         if (!custom_back_font_name.empty()) {
             font_names.emplace_back(wxString::FromUTF8(custom_back_font_name));
         }
         for (int i = 0; i < font_names.size(); i++) {
             backup_fonts.emplace_back(gener_font_with_cache(font_names[i], wxFontEncoding::wxFONTENCODING_SYSTEM));
         }
    }
}

Slic3r::Emboss::FontFileWithCache Slic3r::GUI::BackupFonts::gener_font_with_cache(const wxString &font_name, const wxFontEncoding &encoding)
{
    Emboss::FontFileWithCache font_file_with_cache;
    if (!wxFontEnumerator::IsValidFacename(font_name))
        return font_file_with_cache;
    // Select font
    wxFont wx_font(wxFontInfo().FaceName(font_name).Encoding(encoding));
    if (!wx_font.IsOk()) return font_file_with_cache;
    std::unique_ptr<Emboss::FontFile> font_file = WxFontUtils::create_font_file(wx_font);
    if (font_file == nullptr)
        return font_file_with_cache;

    font_file_with_cache = Emboss::FontFileWithCache(std::move(font_file));
    return font_file_with_cache;
}

