// Include GLGizmoBase.hpp before I18N.hpp as it includes some libigl code, which overrides our localization "L" macro.
#include "GLGizmoText.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/Gizmos/GLGizmosCommon.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/format.hpp"

#include "libslic3r/Geometry/ConvexHull.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Timer.hpp"
#include "libslic3r/Shape/TextShape.hpp"
#include "slic3r/Utils/WxFontUtils.hpp"
#include "slic3r/GUI/Jobs/CreateFontNameImageJob.hpp"
#include "slic3r/GUI/Jobs/NotificationProgressIndicator.hpp"

#include <wx/font.h>
#include <wx/fontutil.h>
#include <wx/fontdlg.h>
#include <wx/fontenum.h>
#include <wx/display.h> // detection of change DPI
#include <wx/hashmap.h>
#include <wx/utils.h>

#include <numeric>
#include <codecvt>
#include <boost/log/trivial.hpp>

#include <GL/glew.h>

#include "imgui/imgui_stdlib.h" // using std::string for inputs
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui/imgui_internal.h>
#include "libslic3r/SVG.hpp"
#include <codecvt>


using namespace Slic3r;
using namespace Slic3r::GUI;
using namespace Slic3r::GUI::Emboss;
using namespace Slic3r::Emboss;
static std::size_t hash_value(wxString const &s)
{
    boost::hash<std::string> hasher;
    return hasher(s.ToStdString());
}

// increase number when change struct FacenamesSerializer
constexpr std::uint32_t FACENAMES_VERSION = 1;
struct FacenamesSerializer
{
    // hash number for unsorted vector of installed font into system
    size_t hash = 0;
    // assumption that is loadable
    std::vector<wxString> good;
    // Can't load for some reason
    std::vector<wxString> bad;
};
template<class Archive> void save(Archive &archive, wxString const &d)
{
    auto data = into_u8(d);
    archive(data);
}
template<class Archive> void load(Archive &archive, wxString &d)
{
    std::string s;
    archive(s);
    d = from_u8(s);
}
template<class Archive> void serialize(Archive &ar, FacenamesSerializer &t, const std::uint32_t version)
{
    // When performing a load, the version associated with the class
    // is whatever it was when that data was originally serialized
    // When we save, we'll use the version that is defined in the macro
    if (version != FACENAMES_VERSION) {
        throw Slic3r::IOError("Version of hints.cereal is higher than current version.");
        return;
    }
    ar(t.hash, t.good, t.bad);
}
CEREAL_CLASS_VERSION(FacenamesSerializer, FACENAMES_VERSION); // register class version
namespace Slic3r {
namespace GUI {
namespace Text {
template<typename T> struct Limit
{
    // Limitation for view slider range in GUI
    MinMax<T> gui;
    // Real limits for setting exacts values
    MinMax<T> values;
};
static const struct Limits
{
    MinMax<double> emboss{0.01, 1e4};                    // in mm
    MinMax<float>  size_in_mm{1.0f, 1000.f};             // in mm
    Limit<float>   boldness{{-.1f, .1f}, {-5e5f, 5e5f}}; // in font points
    Limit<float>   skew{{-1.f, 1.f}, {-100.f, 100.f}};   // ration without unit
    MinMax<int>    char_gap{-20000, 20000};              // in font points
    MinMax<int>    line_gap{-20000, 20000};              // in font points
    // distance text object from surface
    MinMax<float> angle{-180.f, 180.f}; // in degrees
} limits;
enum class IconType : unsigned {
    rename = 0,
    warning,
    undo,
    save,
    add,
    erase,
    /*
    italic,
    unitalic,
    bold,
    unbold,
    system_selector,
    open_file,
    lock,
    lock_bold,
    unlock,
    unlock_bold,
    align_horizontal_left,
    align_horizontal_center,
    align_horizontal_right,
    align_vertical_top,
    align_vertical_center,
    align_vertical_bottom,*/
    // automatic calc of icon's count
    _count
};
// Define rendered version of icon
enum class IconState : unsigned { activable = 0, hovered /*1*/, disabled /*2*/ };
// selector for icon by enum
const IconManager::Icon &get_icon(const IconManager::VIcons &icons, IconType type, IconState state);

struct CurGuiCfg
{
    // Detect invalid config values when change monitor DPI
    double screen_scale;
    bool   dark_mode = false;

    // Zero means it is calculated in init function
    float        height_of_volume_type_selector = 0.f;
    float        input_width                    = 0.f;
    float        delete_pos_x                   = 0.f;
    float        max_style_name_width           = 0.f;
    unsigned int icon_width                     = 0;
    float        max_tooltip_width              = 0.f;

    // maximal width and height of style image
    Vec2i32 max_style_image_size = Vec2i32(0, 0);

    float indent                = 0.f;
    float input_offset          = 0.f;
    float advanced_input_offset = 0.f;
    float lock_offset           = 0.f;

    ImVec2 text_size;

    // maximal size of face name image
    Vec2i32 face_name_size             = Vec2i32(0, 0);
    float   face_name_texture_offset_x = 0.f;

    // maximal texture generate jobs running at once
    unsigned int max_count_opened_font_files = 10;

    // Only translations needed for calc GUI size
    struct Translations
    {
        std::string font;
        std::string height;
        std::string depth;

        // advanced
        std::string use_surface;
        std::string per_glyph;
        std::string alignment;
        std::string char_gap;
        std::string line_gap;
        std::string boldness;
        std::string skew_ration;
        std::string from_surface;
        std::string rotation;
        std::string keep_up;
        std::string collection;
    };
    Translations translations;
};
CurGuiCfg create_gui_configuration();
}
using namespace Text;
IconManager::VIcons init_text_icons(IconManager &mng, const CurGuiCfg &cfg)
{
    mng.release();

    ImVec2 size(cfg.icon_width, cfg.icon_width);
    // icon order has to match the enum IconType
    std::vector<std::string> filenames{
        "edit_button.svg",
        "obj_warning.svg",  // exclamation // ORCA: use obj_warning instead exclamation. exclamation is not compatible with low res
        "text_undo.svg",         // reset_value
        "text_save.svg",         // save
        "add_copies.svg",
        "delete2.svg",
        //"text_refresh.svg",      // refresh
        //"text_open.svg",         // changhe_file
        //"text_bake.svg",         // bake

        //"text_obj_warning.svg",  // exclamation // ORCA: use obj_warning instead exclamation. exclamation is not compatible with low res
        //"text_lock_closed.svg",  // lock
        //"text_lock_open.svg",    // unlock
        //"text_reflection_x.svg", // reflection_x
        //"text_reflection_y.svg", // reflection_y
    };

    assert(filenames.size() == static_cast<size_t>(IconType::_count));
    std::string path = resources_dir() + "/images/";
    for (std::string &filename : filenames)
        filename = path + filename;

    auto type = IconManager::RasterType::color_wite_gray;
    return mng.init(filenames, size, type);
}
bool is_text_empty(std::string_view text) { return text.empty() || text.find_first_not_of(" \n\t\r") == std::string::npos; }
struct GLGizmoText::GuiCfg : public Text::CurGuiCfg
{};

static const wxColour FONT_TEXTURE_BG         = wxColour(0, 0, 0, 0);
static const wxColour FONT_TEXTURE_FG = *wxWHITE;
static const int FONT_SIZE = 12;
static const float SELECTABLE_INNER_OFFSET = 8.0f;

const std::array<float, 4>  TEXT_GRABBER_COLOR      = {1.0, 1.0, 0.0, 1.0};
const std::array<float, 4>  TEXT_GRABBER_HOVER_COLOR = {0.7, 0.7, 0.0, 1.0};
#ifdef DEBUG_TEXT
std::string                 formatFloat(float val)
{
    std::stringstream ss;
    ss << std::fixed << std::setprecision(2) << val;
    return ss.str();
}
#endif

class Line_3D
{
public:
    Line_3D(Vec3d i_a, Vec3d i_b) : a(i_a), b(i_b) {}

    double length() { return (b - a).cast<double>().norm(); }

    Vec3d vector()
    {
        Vec3d new_vec = b - a;
        new_vec.normalize();
        return new_vec;
    }

    void reverse() { std::swap(this->a, this->b); }

    Vec3d a;
    Vec3d b;
};

class Polygon_3D
{
public:
    Polygon_3D(const std::vector<Vec3d> &points) : m_points(points) {}

    std::vector<Line_3D> get_lines()
    {
        std::vector<Line_3D> lines;
        lines.reserve(m_points.size());
        if (m_points.size() > 2) {
            for (int i = 0; i < m_points.size() - 1; ++i) { lines.push_back(Line_3D(m_points[i], m_points[i + 1])); }
            lines.push_back(Line_3D(m_points.back(), m_points.front()));
        }
        return lines;
    }
    std::vector<Vec3d> m_points;
};

// for debug
void export_regions_to_svg(const Point &point, const Polygons &polylines)
{
    std::string path = "D:/svg_profiles/text_poly.svg";
    //BoundingBox bbox = get_extents(polylines);
    SVG svg(path.c_str());
    svg.draw(polylines, "green");
    svg.draw(point, "red", 5e6);
}

int preNUm(unsigned char byte)
{
    unsigned char mask = 0x80;
    int           num  = 0;
    for (int i = 0; i < 8; i++) {
        if ((byte & mask) == mask) {
            mask = mask >> 1;
            num++;
        } else {
            break;
        }
    }
    return num;
}

// https://www.jianshu.com/p/a83d398e3606
bool get_utf8_sub_strings(char *data, int len, std::vector<std::string> &out_strs)
{
    out_strs.clear();
    std::string str = std::string(data);

    int num = 0;
    int i   = 0;
    while (i < len) {
        if ((data[i] & 0x80) == 0x00) {
            out_strs.emplace_back(str.substr(i, 1));
            i++;
            continue;
        } else if ((num = preNUm(data[i])) > 2) {
            int start = i;
            i++;
            for (int j = 0; j < num - 1; j++) {
                if ((data[i] & 0xc0) != 0x80) { return false; }
                i++;
            }
            out_strs.emplace_back(str.substr(start, i - start));
        } else {
            return false;
        }
    }
    return true;
}
bool draw_button(const IconManager::VIcons &icons, IconType type, bool disable = false);
struct FaceName
{
    wxString    wx_name;
    std::string name_truncated = "";
    size_t      texture_index  = 0;
    // State for generation of texture
    // when start generate create share pointers
    std::shared_ptr<std::atomic<bool>> cancel = nullptr;
    // R/W only on main thread - finalize of job
    std::shared_ptr<bool> is_created = nullptr;
};
// Implementation of forwarded struct
// Keep sorted list of loadable face names
struct CurFacenames
{
    // flag to keep need of enumeration fonts from OS
    // false .. wants new enumeration check by Hash
    // true  .. already enumerated(During opened combo box)
    bool is_init = false;

    bool has_truncated_names = false;

    // data of can_load() faces
    std::vector<FaceName>    faces       = {};
    std::vector<std::string> faces_names = {};
    // Sorter set of Non valid face names in OS
    std::vector<wxString> bad = {};

    // Configuration of font encoding
    static const wxFontEncoding encoding = wxFontEncoding::wxFONTENCODING_SYSTEM;

    // Identify if preview texture exists
    GLuint texture_id = 0;

    // protection for open too much font files together
    // Gtk:ERROR:../../../../gtk/gtkiconhelper.c:494:ensure_surface_for_gicon: assertion failed (error == NULL): Failed to load
    // /usr/share/icons/Yaru/48x48/status/image-missing.png: Error opening file /usr/share/icons/Yaru/48x48/status/image-missing.png: Too
    // many open files (g-io-error-quark, 31) This variable must exist until no CreateFontImageJob is running
    unsigned int count_opened_font_files = 0;

    // Configuration for texture height
    const int count_cached_textures = 32;

    // index for new generated texture index(must be lower than count_cached_textures)
    size_t texture_index = 0;

    // hash created from enumerated font from OS
    // check when new font was installed
    size_t hash = 0;

    // filtration pattern
    // std::string       search = "";
    // std::vector<bool> hide; // result of filtration
};
struct GLGizmoText::Facenames : public CurFacenames
{};

bool                    store(const CurFacenames &facenames);
bool                    load(CurFacenames &facenames);
void                    init_face_names(CurFacenames &face_names);
void                    init_truncated_names(CurFacenames &face_names, float max_width);
std::optional<wxString> get_installed_face_name(const std::optional<std::string> &face_name_opt, CurFacenames &face_names);
void                    draw_font_preview(FaceName &face, const std::string &text, CurFacenames &faces, const CurGuiCfg &cfg, bool is_visible);
void                    init_text_lines(TextLinesModel &text_lines, const Selection &selection, /* const*/ StyleManager &style_manager, unsigned count_lines = 0);
class TextDataBase : public DataBase
{
public:
    TextDataBase(DataBase &&parent, const FontFileWithCache &font_file, TextConfiguration &&text_configuration, const EmbossProjection &projection)
        : DataBase(std::move(parent)), m_font_file(font_file) /* copy */, m_text_configuration(std::move(text_configuration))
    {
        assert(m_font_file.has_value());
        shape.projection = projection; // copy

        const FontProp &fp = m_text_configuration.style.prop;
        const FontFile &ff = *m_font_file.font_file;
        shape.scale        = get_text_shape_scale(fp, ff);
    }
    // Create shape from text + font configuration
    EmbossShape &create_shape() override;
    void         write(ModelVolume &volume) const override;
    /// <summary>
    /// Used only with text for embossing per glyph.
    /// Create text lines only for new added volume to object
    /// otherwise textline is already setted before
    /// </summary>
    /// <param name="tr">Embossed volume final transformation in object</param>
    /// <param name="vols">Volumes to be sliced to text lines</param>
    /// <returns>True on succes otherwise False(Per glyph shoud be disabled)</returns>
    //bool create_text_lines(const Transform3d &tr, const ModelVolumePtrs &vols) override;

private:
    //  Keep pointer on Data of font (glyph shapes)
    FontFileWithCache m_font_file;
    // font item is not used for create object
    TextConfiguration m_text_configuration;
};

void TextDataBase::write(ModelVolume &volume) const
{
    //DataBase::write(volume);
    volume.set_text_configuration(m_text_configuration);// volume.text_configuration = m_text_configuration; // copy
    // Fix for object: stored attribute that volume is embossed per glyph when it is object
    if (m_text_configuration.style.prop.per_glyph && volume.is_the_only_one_part()) {
        volume.get_text_configuration()->style.prop.per_glyph = false;
    }
}
///////////////////////
/// GLGizmoText start

GLGizmoText::GLGizmoText(GLCanvas3D& parent, unsigned int sprite_id)
    : GLGizmoBase(parent, sprite_id),
    m_face_names(std::make_unique<Facenames>()),
    m_style_manager(m_imgui->get_glyph_ranges(), create_default_styles),
    m_gui_cfg(nullptr), m_rotate_gizmo(parent, GLGizmoRotate::Axis::Z) // grab id = 2 (Z axis)
{
    m_rotate_gizmo.set_group_id(0);
    m_rotate_gizmo.set_force_local_coordinate(true);
}

GLGizmoText::~GLGizmoText()
{
    if (m_thread.joinable())
        m_thread.join();

    for (int i = 0; i < m_textures.size(); i++) {
        if (m_textures[i].texture != nullptr)
            delete m_textures[i].texture;
    }
}

bool GLGizmoText::on_init()
{
    m_rotate_gizmo.init();
    ColorRGBA gray_color(.6f, .6f, .6f, .3f);
    m_rotate_gizmo.set_highlight_color(gray_color.get_data());
    // Set rotation gizmo upwardrotate
    m_rotate_gizmo.set_angle(PI / 2);

    m_init_texture     = false;
    m_style_manager.init(wxGetApp().app_config);
    Emboss::StyleManager::Style &style          = m_style_manager.get_style();
    std::optional<wxString>      installed_name = get_installed_face_name(style.prop.face_name, *m_face_names);
    //m_avail_font_names = init_face_names();//todo

    //m_thread = std::thread(&GLGizmoText::update_font_status, this);
    //m_avail_font_names = init_occt_fonts();
    //update_font_texture();
    m_scale = m_imgui->get_font_size();
    m_shortcut_key = WXK_CONTROL_T;

    reset_text_info();

    m_desc["rotate_text_caption"] = _L("Shift + Mouse move up or down");
    m_desc["rotate_text"]         = _L("Rotate text");

    return true;
}

void GLGizmoText::update_font_texture()
{
    m_font_names.clear();
    for (int i = 0; i < m_textures.size(); i++) {
        if (m_textures[i].texture != nullptr)
            delete m_textures[i].texture;
    }
    m_combo_width = 0.0f;
    m_combo_height = 0.0f;
    m_textures.clear();
    m_textures.reserve(m_avail_font_names.size());
    for (int i = 0; i < m_avail_font_names.size(); i++)
    {
        GLTexture* texture = new GLTexture();
        auto face = wxString::FromUTF8(m_avail_font_names[i]);
        auto retina_scale = m_parent.get_scale();
        wxFont font { (int)round(retina_scale * FONT_SIZE), wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, face };
        int w, h, hl;
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_font_status[i] && texture->generate_texture_from_text(m_avail_font_names[i], font, w, h, hl, FONT_TEXTURE_BG, FONT_TEXTURE_FG)) {
            //if (h < m_imgui->scaled(2.f)) {
                TextureInfo info;
                info.texture = texture;
                info.w = w;
                info.h = h;
                info.hl = hl;
                info.font_name = m_avail_font_names[i];
                m_textures.push_back(info);
                m_combo_width = std::max(m_combo_width, static_cast<float>(texture->m_original_width));
                m_font_names.push_back(info.font_name);
            //}
        }
    }
    m_combo_height = m_imgui->scaled(32.f / 15.f);
}

bool GLGizmoText::is_mesh_point_clipped(const Vec3d &point, const Transform3d &trafo) const
{
    if (m_c->object_clipper()->get_position() == 0.)
        return false;

    auto  sel_info          = m_c->selection_info();
    Vec3d transformed_point = trafo * point;
    transformed_point(2) += sel_info->get_sla_shift();
    return m_c->object_clipper()->get_clipping_plane()->is_point_clipped(transformed_point);
}

BoundingBoxf3 GLGizmoText::bounding_box() const
{
    BoundingBoxf3                 ret;
    const Selection &             selection = m_parent.get_selection();
    const Selection::IndicesList &idxs      = selection.get_volume_idxs();
    for (unsigned int i : idxs) {
        const GLVolume *volume = selection.get_volume(i);
        if (!volume->is_modifier)
            ret.merge(volume->transformed_convex_hull_bounding_box());
    }
    return ret;
}

EmbossStyles GLGizmoText::create_default_styles()
{
    wxFontEnumerator::InvalidateCache();
    wxArrayString facenames = wxFontEnumerator::GetFacenames(CurFacenames::encoding);

    wxFont wx_font_normal = *wxNORMAL_FONT;
#ifdef __APPLE__
    // Set normal font to helvetica when possible
    for (const wxString &facename : facenames) {
        if (facename.IsSameAs("Helvetica")) {
            wx_font_normal = wxFont(wxFontInfo().FaceName(facename).Encoding(Facenames::encoding));
            break;
        }
    }
#endif // __APPLE__

    // https://docs.wxwidgets.org/3.0/classwx_font.html
    // Predefined objects/pointers: wxNullFont, wxNORMAL_FONT, wxSMALL_FONT, wxITALIC_FONT, wxSWISS_FONT
    EmbossStyles styles = {
        WxFontUtils::create_emboss_style(wxFont(10, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD), _u8L("MODERN")),//v2.0 version
        WxFontUtils::create_emboss_style(wxFont(10, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD), _u8L("OLD")),//for 1.10 and 1.9 and old version
        WxFontUtils::create_emboss_style(wx_font_normal, _u8L("NORMAL")), // wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT)
        WxFontUtils::create_emboss_style(*wxSMALL_FONT, _u8L("SMALL")),   // A font using the wxFONTFAMILY_SWISS family and 2 points smaller than wxNORMAL_FONT.
        WxFontUtils::create_emboss_style(*wxITALIC_FONT,
                                         _u8L("ITALIC")), // A font using the wxFONTFAMILY_ROMAN family and wxFONTSTYLE_ITALIC style and of the same size of wxNORMAL_FONT.
        WxFontUtils::create_emboss_style(*wxSWISS_FONT, _u8L("SWISS")), // A font identic to wxNORMAL_FONT except for the family used which is wxFONTFAMILY_SWISS.
    };

    // Not all predefined font for wx must be valid TTF, but at least one style must be loadable
    styles.erase(std::remove_if(styles.begin(), styles.end(),
                                [](const EmbossStyle &style) {
                                    wxFont wx_font = WxFontUtils::create_wxFont(style);

                                    // check that face name is setabled
                                    if (style.prop.face_name.has_value()) {
                                        wxString face_name = wxString::FromUTF8(style.prop.face_name->c_str());
                                        wxFont   wx_font_temp;
                                        if (!wx_font_temp.SetFaceName(face_name))
                                            return true;
                                    }

                                    // Check that exsit valid TrueType Font for wx font
                                    return WxFontUtils::create_font_file(wx_font) == nullptr;
                                }),
                 styles.end());

    // exist some valid style?
    if (!styles.empty())
        return styles;

    // No valid style in defult list
    // at least one style must contain loadable font
    wxFont wx_font;
    for (const wxString &face : facenames) {
        wx_font = wxFont(face);
        if (WxFontUtils::create_font_file(wx_font) != nullptr)
            break;
        wx_font = wxFont(); // NotOk
    }

    if (wx_font.IsOk()) {
        // use first alphabetic sorted installed font
        styles.push_back(WxFontUtils::create_emboss_style(wx_font, _u8L("First font")));
    } else {
        // On current OS is not installed any correct TTF font
        // use font packed with Slic3r
        std::string font_path = Slic3r::resources_dir() + "/fonts/NotoSans-Regular.ttf";
        styles.push_back(EmbossStyle{_u8L("Default font"), font_path, EmbossStyle::Type::file_path});
    }
    return styles;
}

bool GLGizmoText::select_facename(const wxString &facename, bool update_text)
{
    if (!wxFontEnumerator::IsValidFacename(facename))
        return false;
    // Select font
    wxFont wx_font(wxFontInfo().FaceName(facename).Encoding(CurFacenames::encoding));
    if (!wx_font.IsOk())
        return false;
#ifdef USE_PIXEL_SIZE_IN_WX_FONT
    // wx font could change source file by size of font
    int point_size = static_cast<int>(m_style_manager.get_font_prop().size_in_mm);
    wx_font.SetPointSize(point_size);
#endif // USE_PIXEL_SIZE_IN_WX_FONT
    if (!m_style_manager.set_wx_font(wx_font))
        return false;
    if (update_text) {
        process();
    }
    return true;
}

bool GLGizmoText::gizmo_event(SLAGizmoEventType action, const Vec2d &mouse_position, bool shift_down, bool alt_down, bool control_down)
{
    std::string text = std::string(m_text);
    if (text.empty())
        return true;


    const Selection &selection = m_parent.get_selection();
    auto mo                    = selection.get_model()->objects[m_object_idx];
    if (mo == nullptr)
        return true;

    const ModelInstance *mi        = mo->instances[selection.get_instance_idx()];
    const Camera &       camera    = wxGetApp().plater()->get_camera();

    if (action == SLAGizmoEventType::Moving) {
        if (shift_down && !alt_down && !control_down) {
            float angle = m_rotate_angle + 0.5 * (m_mouse_position - mouse_position).y();
            if (angle == 0)
                return true;

            while (angle < 0)
                angle += 360;

            while (angle >= 360)
                angle -= 360;

            m_rotate_angle = angle;
            m_shift_down   = true;
            m_need_update_text = true;
        } else {
            m_shift_down     = false;
            m_origin_mouse_position = mouse_position;
        }
        m_mouse_position = mouse_position;
    }
    else if (action == SLAGizmoEventType::LeftDown) {
        if (!selection.is_empty() && get_hover_id() != -1) {
            start_dragging();
            return true;
        }

        if (m_is_modify)
            return true;

        Plater *plater = wxGetApp().plater();
        if (!plater || m_thickness <= 0)
            return true;

        ModelObject *model_object = selection.get_model()->objects[m_object_idx];
        if (m_preview_text_volume_id > 0) {
            model_object->delete_volume(m_preview_text_volume_id);
            plater->update();
            m_preview_text_volume_id = -1;
        }

        // Precalculate transformations of individual meshes.
        std::vector<Transform3d> trafo_matrices;
        for (const ModelVolume *mv : mo->volumes) {
            if (mv->is_model_part()) { trafo_matrices.emplace_back(mi->get_transformation().get_matrix() * mv->get_matrix()); }
        }

        Vec3f  normal                       = Vec3f::Zero();
        Vec3f  hit                          = Vec3f::Zero();
        size_t facet                        = 0;
        Vec3f  closest_hit                  = Vec3f::Zero();
        Vec3f  closest_normal               = Vec3f::Zero();
        double closest_hit_squared_distance = std::numeric_limits<double>::max();
        int    closest_hit_mesh_id          = -1;

        // Cast a ray on all meshes, pick the closest hit and save it for the respective mesh
        for (int mesh_id = 0; mesh_id < int(trafo_matrices.size()); ++mesh_id) {
            MeshRaycaster mesh_raycaster = MeshRaycaster(mo->volumes[mesh_id]->mesh());

            if (mesh_raycaster.unproject_on_mesh(mouse_position, trafo_matrices[mesh_id], camera, hit, normal,
                                                                           m_c->object_clipper()->get_clipping_plane(), &facet)) {
                // In case this hit is clipped, skip it.
                if (is_mesh_point_clipped(hit.cast<double>(), trafo_matrices[mesh_id]))
                    continue;

                // Is this hit the closest to the camera so far?
                double hit_squared_distance = (camera.get_position() - trafo_matrices[mesh_id] * hit.cast<double>()).squaredNorm();
                if (hit_squared_distance < closest_hit_squared_distance) {
                    closest_hit_squared_distance = hit_squared_distance;
                    closest_hit_mesh_id          = mesh_id;
                    closest_hit                  = hit;
                    closest_normal               = normal;
                }
            }
        }

        if (closest_hit == Vec3f::Zero() && closest_normal == Vec3f::Zero())
            return true;

        m_rr = {mouse_position, closest_hit_mesh_id, closest_hit, closest_normal};//left down

        generate_text_volume(false);
        m_is_modify = true;
        plater->update();
    }

    return true;
}

void GLGizmoText::on_set_state()
{
    m_rotate_gizmo.set_state(GLGizmoBase::m_state);
    if (m_state == EState::On) {
        m_last_text_mv = nullptr;
        m_need_fix     = false;
        m_show_text_normal_reset_tip = false;
        m_use_current_pose           = true;
        load_init_text();
        if (m_last_text_mv) {
            m_reedit_text = true;
            m_need_fix    = true;
            m_load_text_tran_in_object = m_text_tran_in_object;
            if (m_really_use_surface_calc) {
                m_show_warning_regenerated = true;
                m_need_fix                 = false;
                use_fix_normal_position();
            } else if (m_fix_old_tran_flag && m_font_version == "") {
                m_show_warning_old_tran = false;
                auto  offset     = m_text_tran_in_object.get_offset();
                auto  rotation   = m_text_tran_in_object.get_rotation();
                float eps        = 0.01f;
                int   count      = 0;
                bool  has_rotation      = rotation.norm() > eps;
                count += has_rotation ? 1 : 0;
                auto scaling_factor = m_text_tran_in_object.get_scaling_factor();
                bool  has_scale         = (scaling_factor - Vec3d(1, 1, 1)).norm() > eps;
                count += has_scale ? 1 : 0;
                auto mirror     = m_text_tran_in_object.get_mirror();
                bool has_mirror = (mirror - Vec3d(1, 1, 1)).norm() > eps;
                count += has_mirror ? 1 : 0;

                Geometry::Transformation expert_text_tran_in_world;
                generate_text_tran_in_world(m_fix_text_normal_in_world.cast<double>(), m_text_position_in_world, m_rotate_angle, expert_text_tran_in_world);
                auto                     temp_expert_text_tran_in_object = m_object_cs_to_world_tran.inverse() * expert_text_tran_in_world.get_matrix();
                Geometry::Transformation expert_text_tran_in_object(temp_expert_text_tran_in_object);
                expert_text_tran_in_object.set_offset(m_text_tran_in_object.get_offset());

                if (count >= 2) {
                    m_show_warning_old_tran = true;
                }
                if (m_is_version1_10_xoy) {
                    auto rotate_tran = Geometry::assemble_transform(Vec3d::Zero(), {0.5 * M_PI, 0.0, 0.0});
                    m_load_text_tran_in_object.set_from_transform(m_load_text_tran_in_object.get_matrix() * rotate_tran);
                    m_load_text_tran_in_object.set_offset(m_load_text_tran_in_object.get_offset() + Vec3d(0, 1.65, 0)); // for size 16
                    return;
                }
                //go on
                if (has_rotation && m_show_warning_old_tran == false) {
                    m_show_warning_lost_rotate = true;
                    if (m_is_version1_9_xoz) {
                        expert_text_tran_in_object.set_rotation(rotation);
                    } else {
                        m_need_fix = false;
                    }
                    use_fix_normal_position();
                }
                //not need set set_rotation//has_rotation
                if (has_scale) {
                    expert_text_tran_in_object.set_scaling_factor(scaling_factor);
                }
                if (has_mirror) {
                    expert_text_tran_in_object.set_mirror(mirror);
                }
                m_load_text_tran_in_object.set_from_transform(expert_text_tran_in_object.get_matrix());
                if (m_is_version1_9_xoz) {
                    m_load_text_tran_in_object.set_offset(m_load_text_tran_in_object.get_offset());
                }
            }
        }
    }
    else if (m_state == EState::Off) {
        ImGui::FocusWindow(nullptr);//exit cursor
        m_reedit_text     = false;
        m_fix_old_tran_flag = false;
        m_warning_font      = false;
        m_warning_cur_font_not_support_part_text = true;
        close_warning_flag_after_close_or_drag();
        reset_text_info();
        delete_temp_preview_text_volume();
        m_parent.use_slope(false);
        m_parent.toggle_model_objects_visibility(true);
    }
}

void GLGizmoText::load_old_font() {
    const int old_font_index = 1;
    const StyleManager::Style &style          = m_style_manager.get_styles()[old_font_index];
    // create copy to be able do fix transformation only when successfully load style
    if (m_style_manager.load_style(old_font_index)) {
        if (m_curr_font_idx >= 0) {
            m_warning_font = true;
        }
        m_style_manager.get_font_prop().size_in_mm = m_font_size / 1.09;

        wxString font_name("Arial");
        select_facename(font_name,true);//include process()
    }
}

void GLGizmoText::draw_style_list(float caption_size)
{
    if (!m_style_manager.is_active_font())
        return;

    const StyleManager::Style *stored_style = nullptr;
    bool                       is_stored    = m_style_manager.exist_stored_style();
    if (is_stored)
        stored_style = m_style_manager.get_stored_style();
    const StyleManager::Style &current_style = m_style_manager.get_style();
    /*wxString                   path0((*stored_style).path.c_str(), wxConvUTF8);
    wxString                   path1(current_style.path.c_str(), wxConvUTF8);*/
    bool                       is_changed    = (stored_style) ? !(*stored_style == current_style) : true;
    bool                       is_modified   = is_stored && is_changed;
    const float &max_style_name_width = m_gui_cfg->max_style_name_width;
    std::string &trunc_name           = m_style_manager.get_truncated_name();
    if (trunc_name.empty()) {
        // generate trunc name
        std::string current_name = current_style.name;
        ImGuiWrapper::escape_double_hash(current_name);
        trunc_name = ImGuiWrapper::trunc(current_name, max_style_name_width);
    }
    ImGui::AlignTextToFramePadding();
    std::string title = _u8L("Style");
    if (m_style_manager.exist_stored_style())
        ImGui::Text("%s", title.c_str());
    else
        ImGui::TextColored(ImGuiWrapper::COL_BAMBU, "%s", title.c_str());
    ImGui::SameLine(caption_size);
    ImGui::PushItemWidth(m_gui_cfg->input_width);
    auto add_text_modify = [&is_modified](const std::string &name) {
        if (!is_modified)
            return name;
        return name + Preset::suffix_modified();
    };
    std::optional<size_t> selected_style_index;
    std::string           tooltip = "";
    ImGuiWrapper::push_combo_style(m_parent.get_scale());
    if (ImGui::BBLBeginCombo("##style_selector", add_text_modify(trunc_name).c_str())) {
        m_style_manager.init_style_images(m_gui_cfg->max_style_image_size, m_text);
        m_style_manager.init_trunc_names(max_style_name_width);
        std::optional<std::pair<size_t, size_t>> swap_indexes;
        const StyleManager::Styles &             styles = m_style_manager.get_styles();
        for (const StyleManager::Style &style : styles) {
            size_t             index             = &style - &styles.front();
            const std::string &actual_style_name = style.name;
            ImGui::PushID(actual_style_name.c_str());
            bool is_selected = (index == m_style_manager.get_style_index());

            float                                          select_height = static_cast<float>(m_gui_cfg->max_style_image_size.y());
            ImVec2                                         select_size(0.f, select_height); // 0,0 --> calculate in draw
            const std::optional<StyleManager::StyleImage> &img = style.image;
            // allow click delete button
            ImGuiSelectableFlags_ flags = ImGuiSelectableFlags_AllowItemOverlap;
            if (ImGui::BBLSelectable(style.truncated_name.c_str(), is_selected, flags, select_size)) {
                selected_style_index = index;
            } else if (ImGui::IsItemHovered())
                tooltip = actual_style_name;

            // reorder items
            if (ImGui::IsItemActive() && !ImGui::IsItemHovered()) {
                if (ImGui::GetMouseDragDelta(0).y < 0.f) {
                    if (index > 0) swap_indexes = {index, index - 1};
                } else if ((index + 1) < styles.size())
                    swap_indexes = {index, index + 1};
                if (swap_indexes.has_value()) ImGui::ResetMouseDragDelta();
            }

            // draw style name
            if (img.has_value()) {
                ImGui::SameLine(max_style_name_width);
                ImVec4 tint_color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
                ImGui::Image(img->texture_id, img->tex_size, img->uv0, img->uv1, tint_color);
            }

            ImGui::PopID();
        }
        if (swap_indexes.has_value())
            m_style_manager.swap(swap_indexes->first, swap_indexes->second);
        ImGui::EndCombo();
    } else {
        // do not keep in memory style images when no combo box open
        m_style_manager.free_style_images();
        if (ImGui::IsItemHovered()) {
            std::string style_name = add_text_modify(current_style.name);
            tooltip = is_modified ? GUI::format(_L("Modified style \"%1%\""), current_style.name) : GUI::format(_L("Current style is \"%1%\""), current_style.name);
        }
    }
    ImGuiWrapper::pop_combo_style();
    if (!tooltip.empty())
        m_imgui->tooltip(tooltip, m_gui_cfg->max_tooltip_width);

    // Check whether user wants lose actual style modification
    if (selected_style_index.has_value() && is_modified) {
        const std::string &style_name = m_style_manager.get_styles()[*selected_style_index].name;
        wxString      message = GUI::format_wxstr(_L("Changing style to \"%1%\" will discard current style modification.\n\nWould you like to continue anyway?"), style_name);
        MessageDialog not_loaded_style_message(nullptr, message, _L("Warning"), wxICON_WARNING | wxYES | wxNO);
        if (not_loaded_style_message.ShowModal() != wxID_YES) selected_style_index.reset();
    }

    // selected style from combo box
    if (selected_style_index.has_value()) {
        const StyleManager::Style &style = m_style_manager.get_styles()[*selected_style_index];
        // create copy to be able do fix transformation only when successfully load style
        StyleManager::Style cur_s = current_style; // copy
        StyleManager::Style new_s = style;         // copy
        if (m_style_manager.load_style(*selected_style_index)) {
            process(true);//, fix_transformation(cur_s, new_s, m_parent)//todo
        } else {
            wxString      title   = _L("Not valid style.");
            wxString      message = GUI::format_wxstr(_L("Style \"%1%\" can't be used and will be removed from a list."), style.name);
            MessageDialog not_loaded_style_message(nullptr, message, title, wxOK);
            not_loaded_style_message.ShowModal();
            m_style_manager.erase(*selected_style_index);
        }
    }

    /*ImGui::SameLine();
    draw_style_rename_button();*/

    ImGui::SameLine();
    draw_style_save_button(is_modified);

    ImGui::SameLine();
    draw_style_add_button();

    // delete button
    ImGui::SameLine();
    draw_delete_style_button();
}

void GLGizmoText::draw_style_save_button(bool is_modified)
{
    if (draw_button(m_icons, IconType::save, !is_modified)) {
        // save styles to app config
        m_style_manager.store_styles_to_app_config();
    } else if (ImGui::IsItemHovered()) {
        std::string tooltip;
        if (!m_style_manager.exist_stored_style()) {
            tooltip = _u8L("First Add style to list.");
        } else if (is_modified) {
            tooltip = GUI::format(_L("Save %1% style"), m_style_manager.get_style().name);
        } else {
            tooltip = _u8L("No changes to save or text volume ie empty.");
        }
        m_imgui->tooltip(tooltip, m_gui_cfg->max_tooltip_width);
    }
}

void GLGizmoText::draw_style_save_as_popup()
{
    ImGuiWrapper::text_colored(ImGuiWrapper::COL_ORANGE_DARK, _u8L("New name of style") + ": ");
    //use name inside of volume configuration as temporary new name

    std::string&  new_name     = m_style_new_name; // text_volume->get_text_configuration()->style.name;//BBS modify
    bool         is_unique    = m_style_manager.is_unique_style_name(new_name);
    bool         allow_change = false;
    if (new_name.empty()) {
        ImGuiWrapper::text_colored(ImGuiWrapper::COL_ORANGE_DARK, _u8L("Name can't be empty."));
    } else if (!is_unique) {
        ImGuiWrapper::text_colored(ImGuiWrapper::COL_ORANGE_DARK, _u8L("Name has to be unique."));
    } else {
        ImGui::NewLine();
        allow_change = true;
    }

    bool                save_style = false;
    ImGuiInputTextFlags flags      = ImGuiInputTextFlags_EnterReturnsTrue;
    if (ImGui::InputText("##save as style", &new_name, flags))
        save_style = true;
    if (m_imgui->button(_L("OK"), ImVec2(0.f, 0.f), allow_change))
        save_style = true;

    ImGui::SameLine();
    if (ImGui::Button(_u8L("Cancel").c_str())) {
        // write original name to volume TextConfiguration
        //new_name = m_style_manager.get_style().name;
        ImGui::CloseCurrentPopup();
    }

    if (save_style && allow_change) {
        m_style_manager.add_style(new_name);
        m_style_manager.store_styles_to_app_config();
        ImGui::CloseCurrentPopup();
    }
}

void GLGizmoText::draw_style_add_button()
{
    bool only_add_style = !m_style_manager.exist_stored_style();
    bool can_add        = true;
    //auto text_volume    = m_last_text_mv; // m_volume
    if (only_add_style)//&& text_volume->get_text_configuration().has_value() && text_volume->get_text_configuration()->style.type != WxFontUtils::get_current_type()
        can_add = false;

    std::string title    = _u8L("Save as new style");
    const char *popup_id = title.c_str();
    // save as new style
    ImGui::SameLine();
    if (draw_button(m_icons, IconType::add, !can_add)) {
        if (!m_style_manager.exist_stored_style()) {
            m_style_manager.store_styles_to_app_config(wxGetApp().app_config);
        } else {
            ImGui::OpenPopup(popup_id);
        }
    } else if (ImGui::IsItemHovered()) {
        if (!can_add) {
            m_imgui->tooltip(_u8L("Only valid font can be added to style."), m_gui_cfg->max_tooltip_width);
        } else if (only_add_style) {
            m_imgui->tooltip(_u8L("Add style to my list."), m_gui_cfg->max_tooltip_width);
        } else {
            m_imgui->tooltip(_u8L("Save as new style."), m_gui_cfg->max_tooltip_width);
        }
    }

    if (ImGui::BeginPopupModal(popup_id, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        m_imgui->disable_background_fadeout_animation();
        draw_style_save_as_popup();
        ImGui::EndPopup();
    }
}

void GLGizmoText::draw_delete_style_button()
{
    bool is_stored  = m_style_manager.exist_stored_style();
    bool is_last    = m_style_manager.get_styles().size() == 1;
    bool can_delete = is_stored && !is_last;

    if (draw_button(m_icons, IconType::erase, !can_delete)) {
        std::string style_name       = m_style_manager.get_style().name; // copy
        wxString    dialog_title     = _L("Remove style");
        size_t      next_style_index = std::numeric_limits<size_t>::max();
        Plater *    plater           = wxGetApp().plater();
        bool        exist_change     = false;
        while (true) {
            // NOTE: can't use previous loaded activ index -> erase could change index
            size_t active_index = m_style_manager.get_style_index();
            next_style_index    = (active_index > 0) ? active_index - 1 : active_index + 1;

            if (next_style_index >= m_style_manager.get_styles().size()) {
                MessageDialog msg(plater, _L("Can't remove the last existing style."), dialog_title, wxICON_ERROR | wxOK);
                msg.ShowModal();
                break;
            }

            // IMPROVE: add function can_load?
            // clean unactivable styles
            if (!m_style_manager.load_style(next_style_index)) {
                m_style_manager.erase(next_style_index);
                exist_change = true;
                continue;
            }

            wxString      message = GUI::format_wxstr(_L("Are you sure you want to permanently remove the \"%1%\" style?"), style_name);
            MessageDialog msg(plater, message, dialog_title, wxICON_WARNING | wxYES | wxNO);
            if (msg.ShowModal() == wxID_YES) {
                // delete style
                m_style_manager.erase(active_index);
                exist_change = true;
                process();
            } else {
                // load back style
                m_style_manager.load_style(active_index);
            }
            break;
        }
        if (exist_change) m_style_manager.store_styles_to_app_config(wxGetApp().app_config);
    }

    if (ImGui::IsItemHovered()) {
        const std::string &style_name = m_style_manager.get_style().name;
        std::string        tooltip;
        if (can_delete)
            tooltip = GUI::format(_L("Delete \"%1%\" style."), style_name);
        else if (is_last)
            tooltip = GUI::format(_L("Can't delete \"%1%\". It is last style."), style_name);
        else /*if(!is_stored)*/
            tooltip = GUI::format(_L("Can't delete temporary style \"%1%\"."), style_name);
        m_imgui->tooltip(tooltip, m_gui_cfg->max_tooltip_width);
    }
}

void GLGizmoText::update_boldness()
{
    std::optional<float> &boldness = m_style_manager.get_font_prop().boldness;
    if (m_bold) {
        set_default_boldness(boldness);
    } else {
        boldness = 0.0f;
    }
}

void GLGizmoText::update_italic()
{
    std::optional<float> &skew = m_style_manager.get_font_prop().skew;
    if (m_italic) {
        skew = 0.2f;
    } else {
        skew = 0.0f;
    }
}

void GLGizmoText::set_default_boldness(std::optional<float> &boldness)
{
    const FontFile &      ff        = *m_style_manager.get_font_file_with_cache().font_file;
    const FontProp &      fp        = m_style_manager.get_font_prop();
    const FontFile::Info &font_info = get_font_info(ff, fp);
    boldness                        = font_info.ascent / 4.f;
}

std::unique_ptr<Emboss::DataBase> GLGizmoText::create_emboss_data_base(
    const std::string &text, Emboss::StyleManager &style_manager, const Selection &selection, ModelVolumeType type, std::shared_ptr<std::atomic<bool>> &cancel)
{
    // create volume_name
    std::string volume_name = text; // copy
    // contain_enter?
    if (volume_name.find('\n') != std::string::npos)
        // change enters to space
        std::replace(volume_name.begin(), volume_name.end(), '\n', ' ');

    if (!style_manager.is_active_font()) {
        style_manager.load_valid_style();
        assert(style_manager.is_active_font());
        if (!style_manager.is_active_font()) return {}; // no active font in style, should never happend !!!
    }

    StyleManager::Style &style = style_manager.get_style(); // copy
    // actualize font path - during changes in gui it could be corrupted
    // volume must store valid path
    assert(style_manager.get_wx_font().IsOk());
    assert(style.path.compare(WxFontUtils::store_wxFont(style_manager.get_wx_font())) == 0);

    bool is_outside = (type == ModelVolumeType::MODEL_PART);

    // Cancel previous Job, when it is in process
    // worker.cancel(); --> Use less in this case I want cancel only previous EmbossJob no other jobs
    // Cancel only EmbossUpdateJob no others
    if (cancel != nullptr)
        cancel->store(true);
    // create new shared ptr to cancel new job
    cancel = std::make_shared<std::atomic<bool>>(false);

    DataBase base(volume_name, cancel);
    style.projection.depth = m_thickness; // BBS add
    style.projection.embeded_depth = m_embeded_depth; // BBS add
    base.is_outside   = is_outside;
   // base.text_lines   = text_lines.get_lines();
    base.from_surface = style.distance;

    FontFileWithCache &font = style_manager.get_font_file_with_cache();
    TextConfiguration  tc{static_cast<EmbossStyle>(style), text};
    return std::make_unique<TextDataBase>(std::move(base), font, std::move(tc), style.projection);
}
//#define TEST_TEXT_JOB
bool GLGizmoText::process(bool make_snapshot, std::optional<Transform3d> volume_transformation, bool update_text)
{
    auto text_volume = m_last_text_mv;//m_volume
    //if (text_volume == nullptr) // m_volume
    //    return false;
    // without text there is nothing to emboss
    if (is_text_empty(m_text))
        return false;
    // exist loaded font file?
    if (!m_style_manager.is_active_font())
        return false;
    update_boldness();
    const Selection &selection = m_parent.get_selection();
    DataBasePtr      base      = create_emboss_data_base(m_text, m_style_manager, selection, text_volume == nullptr ? ModelVolumeType::MODEL_PART : text_volume->type(),
                                               m_job_cancel); // m_text_lines
    m_data_update = {std::move(base), text_volume == nullptr ? -1 : text_volume->id(), make_snapshot};
#ifdef TEST_TEXT_JOB
    auto             job       = std::make_unique<GenerateTextJob>(std::move(data));
    auto &           worker    = wxGetApp().plater()->get_ui_job_worker();
    queue_job(worker, std::move(job));
#else
    create_all_char_mesh(*m_data_update.base, m_chars_mesh_result);
    if (update_text) {
        m_need_update_text = true;
    }
#endif
    //if (!start_update_volume(std::move(data), *m_volume, selection, m_raycast_manager))
     //   return false;
    // notification is removed befor object is changed by job
    //remove_notification_not_valid_font();
    return true;
}

void GLGizmoText::check_text_type(bool is_surface_text, bool is_keep_horizontal)
{
    if (is_surface_text && is_keep_horizontal) {
        m_text_type = TextType::SURFACE_HORIZONAL;
    } else if (is_surface_text) {
        m_text_type = TextType::SURFACE;
    } else if (is_keep_horizontal) {
        m_text_type = TextType::HORIZONAL;
    } else {
        m_text_type = TextType::HORIZONAL;
    }
}

void GLGizmoText::generate_text_tran_in_world(const Vec3d &text_normal_in_world, const Vec3d &text_position_in_world,float rotate_degree, Geometry::Transformation &cur_tran)
{
    Vec3d  temp_normal        = text_normal_in_world;
    Vec3d  cut_plane_in_world = Vec3d::UnitY();
    double epson              = 1e-6;
    if (!(abs(temp_normal.x()) <= epson && abs(temp_normal.y()) <= epson && abs(temp_normal.z()) > epson)) { // temp_normal != Vec3d::UnitZ()
        Vec3d v_plane      = temp_normal.cross(Vec3d::UnitZ());
        cut_plane_in_world = v_plane.cross(temp_normal);
    }
    Vec3d z_dir       = text_normal_in_world;
    Vec3d y_dir       = cut_plane_in_world;
    Vec3d x_dir_world = y_dir.cross(z_dir);
    if (m_text_type == TextType::SURFACE_HORIZONAL && text_normal_in_world != Vec3d::UnitZ()) {
        y_dir                       = Vec3d::UnitZ();
        x_dir_world                 = y_dir.cross(z_dir);
        z_dir                       = x_dir_world.cross(y_dir);
    }
    auto temp_tran = Geometry::generate_transform(x_dir_world, y_dir, z_dir, text_position_in_world);
    Geometry::Transformation rotate_trans;
    rotate_trans.set_rotation(Vec3d(0, 0, Geometry::deg2rad(rotate_degree))); // m_rotate_angle
    cur_tran.set_matrix(temp_tran.get_matrix() * rotate_trans.get_matrix());
}

std::string GLGizmoText::get_icon_filename(bool b_dark_mode) const
{
    return b_dark_mode ? "toolbar_text_dark.svg" : "toolbar_text.svg";
}

void GLGizmoText::use_fix_normal_position()
{
    m_text_normal_in_world   = m_fix_text_normal_in_world;
    m_text_position_in_world = m_fix_text_position_in_world;
}

void GLGizmoText::load_init_text()
{
    Plater *plater = wxGetApp().plater();
    if (m_parent.get_selection().is_single_volume() || m_parent.get_selection().is_single_modifier()) {
        ModelVolume *model_volume = m_parent.get_selection().get_selected_single_volume(m_object_idx, m_volume_idx);
        if (model_volume) {
            TextInfo text_info = model_volume->get_text_info();
            if (!text_info.m_text.empty()) {
                if (m_last_text_mv == model_volume) {
                    m_last_text_mv = model_volume;
                    return;
                }
                if (plater) {
                    plater->take_snapshot("enter Text");
                }
                auto box = model_volume->get_mesh_shared_ptr()->bounding_box();
                auto box_size = box.size();
                auto valid_z = text_info.m_embeded_depth + text_info.m_thickness;
                m_last_text_mv = model_volume;
                load_from_text_info(text_info);
                auto text_height          = get_text_height(text_info.m_text); // old text
                m_really_use_surface_calc = true;
                int index                 = -1;
                for (size_t i = 0; i < 3; i++) {
                    if (abs(box_size[i] - valid_z) < 0.1) {
                        m_really_use_surface_calc = false;
                        index                     = i;
                        break;
                    }
                }
                if (abs(box.size()[1] - text_height) > 0.1) {
                    m_fix_old_tran_flag = true;
                }

                m_text_volume_tran = model_volume->get_matrix();
                m_text_tran_in_object.set_matrix(m_text_volume_tran);
                int                  temp_object_idx;
                auto                 mo         = m_parent.get_selection().get_selected_single_object(temp_object_idx);
                const ModelInstance *mi         = mo->instances[m_parent.get_selection().get_instance_idx()];
                m_object_cs_to_world_tran       = mi->get_transformation().get_matrix();
                auto world_tran                 = m_object_cs_to_world_tran * m_text_volume_tran;
                m_text_tran_in_world.set_matrix(world_tran);
                m_text_position_in_world = m_text_tran_in_world.get_offset();
                m_text_normal_in_world   = m_text_tran_in_world.get_matrix().linear().col(2).cast<float>();
                {
                    TriangleMesh text_attach_mesh(mo->volumes[m_rr.mesh_id]->mesh());
                    text_attach_mesh.transform(mo->volumes[m_rr.mesh_id]->get_matrix());
                    MeshRaycaster temp_ray_caster(text_attach_mesh);
                    Vec3f local_center = m_text_tran_in_object.get_offset().cast<float>();//(m_text_tran_in_object.get_matrix() * box.center()).cast<float>(); //
                    Vec3f         temp_normal;
                    Vec3f         closest_pt                    = temp_ray_caster.get_closest_point(local_center, &temp_normal);
                    m_fix_text_position_in_world = m_object_cs_to_world_tran * closest_pt.cast<double>();
                    m_fix_text_normal_in_world   = (mi->get_transformation().get_matrix_no_offset().cast<float>() * temp_normal).normalized();
                    int   face_id;
                    Vec3f direction = m_text_tran_in_world.get_matrix().linear().col(2).cast<float>();
                    if (index == 2 && abs(box_size[1] - text_height) < 0.1) {
                        m_is_version1_9_xoz = true;
                        m_fix_old_tran_flag = true;
                    }
                    if (index == 1 && abs(box_size[2] - text_height) < 0.1) { // for 1.10 version, xoy plane cut,just fix
                        m_is_version1_10_xoy = true;
                        direction = m_text_tran_in_world.get_matrix().linear().col(1).cast<float>();
                        if (!temp_ray_caster.get_closest_point_and_normal(local_center, direction, &closest_pt, &temp_normal, &face_id)) {
                            m_show_warning_error_mesh = true;
                        }
                    }
                    else if (!temp_ray_caster.get_closest_point_and_normal(local_center, -direction, &closest_pt, &temp_normal, &face_id)) {
                        if (!temp_ray_caster.get_closest_point_and_normal(local_center, direction, &closest_pt, &temp_normal, &face_id)) {
                            m_show_warning_error_mesh = true;
                        }
                    }
                }
                // m_rr.mesh_id
                if (!m_font_name.empty()) {//font version 1.10 and before
                    m_need_update_text = false;
                }
                m_is_modify        = true;
            }
        }
    }
}

void  GLGizmoText::data_changed(bool is_serializing) { load_init_text(); }

void GLGizmoText::on_set_hover_id()
{
    m_rotate_gizmo.set_hover_id(m_hover_id);
}

void GLGizmoText::on_enable_grabber(unsigned int id) {
    m_rotate_gizmo.enable_grabber(0);
}

void GLGizmoText::on_disable_grabber(unsigned int id) {
    m_rotate_gizmo.disable_grabber(0); }

bool GLGizmoText::on_mouse(const wxMouseEvent &mouse_event)
{
    if (m_last_text_mv == nullptr)
        return false;
    if (m_hover_id != m_move_cube_id) {
        if (on_mouse_for_rotation(mouse_event))
            return true;
    }
    return false;
}

bool GLGizmoText::on_mouse_for_rotation(const wxMouseEvent &mouse_event)
{
    if (mouse_event.Moving())
        return false;

    bool used = use_grabbers(mouse_event);
    if (!m_dragging)
        return used;

    if (mouse_event.Dragging()){//m_angle means current angle
        // Vec3d rotation = Vec3d::Zero();
        // rotation[2] = m_rotate_gizmo.get_angle();
        ////Geometry::rotation_transform(rotation);
        // do_local_z_rotate(m_parent.get_selection(), m_rotate_gizmo.get_angle());
        //// dragging_rotate_gizmo(m_rotate_gizmo.get_angle(), m_angle, m_rotate_start_angle, m_parent.get_selection());
        dragging_rotate_gizmo(m_rotate_gizmo.get_angle(), m_angle, m_rotate_start_angle, m_parent.get_selection());
    }

    return used;
}

CommonGizmosDataID GLGizmoText::on_get_requirements() const
{
    return CommonGizmosDataID(
          int(CommonGizmosDataID::SelectionInfo)
        | int(CommonGizmosDataID::InstancesHider)
        | int(CommonGizmosDataID::Raycaster)
        | int(CommonGizmosDataID::ObjectClipper));
}

std::string GLGizmoText::on_get_name() const
{
    if (!on_is_activable() && m_state == EState::Off) {
        return _u8L("Text shape") + ":\n" + _u8L("Please select single object.");
    } else {
        return _u8L("Text shape");
    }
}

bool GLGizmoText::on_is_activable() const
{
    // This is assumed in GLCanvas3D::do_rotate, do not change this
    // without updating that function too.
    if (m_parent.get_selection().is_single_full_instance())
        return true;

    int obejct_idx, volume_idx;
    ModelVolume *model_volume = m_parent.get_selection().get_selected_single_volume(obejct_idx, volume_idx);
    if (model_volume)
        return !model_volume->get_text_info().m_text.empty();

    return false;
}

void GLGizmoText::on_render()
{
    glsafe(::glClear(GL_DEPTH_BUFFER_BIT));
    glsafe(::glEnable(GL_DEPTH_TEST));

    std::string text = std::string(m_text);
    if (text.empty()) {
        delete_temp_preview_text_volume();
        return;
    }

    ModelObject *mo = nullptr;
    const Selection &selection = m_parent.get_selection();
    mo = selection.get_model()->objects[m_object_idx];

    if (mo == nullptr) {
        BOOST_LOG_TRIVIAL(info) << boost::format("Text: selected object is null");
        return;
    }

    const Camera& camera = wxGetApp().plater()->get_camera();
    const auto& projection_matrix = camera.get_projection_matrix();
    const auto& view_matrix = camera.get_view_matrix();
    // First check that the mouse pointer is on an object.
    const ModelInstance *mi        = mo->instances[selection.get_instance_idx()];
    Plater *plater = wxGetApp().plater();
    if (!plater)
        return;
#ifdef DEBUG_TEXT
    if (m_text_normal_in_world.norm() > 0.1) { // debug
        Geometry::Transformation tran(m_text_volume_tran);
        if (tran.get_offset().norm() > 1) {
            auto text_volume_tran_world = mi->get_transformation().get_matrix() * m_text_volume_tran;
            render_cross_mark(text_volume_tran_world, Vec3f::Zero());
        }

<<<<<<< HEAD   (d79b7b ENH: update the log trivial for parse_json)
        render_cross_mark(m_text_tran_in_world, Vec3f::Zero());
=======
        render_cross_mark(m_text_tran_in_world.get_matrix(), Vec3f::Zero(), true);
>>>>>>> CHANGE (0e84eb NEW:Transplant new font load method)

        glsafe(::glLineWidth(2.0f));
        ::glBegin(GL_LINES);
        glsafe(::glColor3f(1.0f, 0.0f, 0.0f));

        for (size_t i = 1; i < m_cut_points_in_world.size(); i++) {//draw points
            auto target0 = m_cut_points_in_world[i - 1].cast<float>();
            auto target1 = m_cut_points_in_world[i].cast<float>();
            glsafe(::glVertex3f(target0(0), target0(1), target0(2)));
            glsafe(::glVertex3f(target1(0), target1(1), target1(2)));
        }
        glsafe(::glEnd());
    }
#endif
    //if (is_rotate_by_grabbers || (!is_surface_dragging && !is_parent_dragging)) {
    if (m_last_text_mv) {
        if (m_hover_id == m_move_cube_id && m_dragging) {
        } else if (!m_use_current_pose) {
            m_rotate_gizmo.render();
        }
    }
    //}
    if (!m_is_modify || m_shift_down) {//for temp text
        // Precalculate transformations of individual meshes.
        std::vector<Transform3d> trafo_matrices;
        for (const ModelVolume *mv : mo->volumes) {
            if (mv->is_model_part())
                trafo_matrices.emplace_back(mi->get_transformation().get_matrix() * mv->get_matrix());
        }
        // Raycast and return if there's no hit.
        Vec2d mouse_pos;
        if (m_shift_down) {
            if (m_is_modify)
                mouse_pos = m_rr.mouse_position;
            else
                mouse_pos = m_origin_mouse_position;
        }
        else {
            mouse_pos = m_parent.get_local_mouse_position();
        }

        bool position_changed = update_raycast_cache(mouse_pos, camera, trafo_matrices);

        if (m_rr.mesh_id == -1) {
            delete_temp_preview_text_volume();
            return;
        }

        if (!position_changed && !m_need_update_text && !m_shift_down)
            return;
        update_text_pos_normal();
    }

    if (m_is_modify) {
        update_text_pos_normal();
        Geometry::Transformation tran;//= m_text_tran_in_world;
        {
            double   phi;
            Vec3d    rotation_axis;
            Matrix3d rotation_matrix;
            Geometry::rotation_from_two_vectors(Vec3d::UnitZ(), m_text_normal_in_world.cast<double>(), rotation_axis, phi, &rotation_matrix);
            tran.set_matrix((Transform3d) rotation_matrix);
        }
        tran.set_offset(m_text_position_in_world);
        bool                     hover = (m_hover_id == m_move_cube_id);
        std::array<float, 4>     render_color;
        if (hover) {
            render_color = TEXT_GRABBER_HOVER_COLOR;
        } else
            render_color = TEXT_GRABBER_COLOR;
        float fullsize            = get_grabber_size();
        m_move_grabber.center     = tran.get_offset();
        Transform3d rotate_matrix = tran.get_rotation_matrix();
        Transform3d cube_mat      = Geometry::translation_transform(m_move_grabber.center) * rotate_matrix * Geometry::scale_transform(fullsize);
        m_move_grabber.set_model_matrix(cube_mat);
        render_glmodel(m_move_grabber.get_cube(), render_color, view_matrix * cube_mat, projection_matrix);
    }

    delete_temp_preview_text_volume();

    if (m_is_modify && !m_need_update_text)
        return;

    generate_text_volume();
    plater->update();
}

void GLGizmoText::on_render_for_picking()
{
    glsafe(::glDisable(GL_DEPTH_TEST));
    if (!m_use_current_pose) {
        m_rotate_gizmo.render_for_picking();
    }
    const auto& shader = wxGetApp().get_shader("flat");
    if (shader == nullptr)
        return;
    wxGetApp().bind_shader(shader);
    int          obejct_idx, volume_idx;
    ModelVolume *model_volume = m_parent.get_selection().get_selected_single_volume(obejct_idx, volume_idx);
    if (model_volume && !model_volume->get_text_info().m_text.empty()) {
        const Selection &selection = m_parent.get_selection();
        auto             mo        = selection.get_model()->objects[m_object_idx];
        if (mo == nullptr)
            return;
        auto color              = picking_color_component(m_move_cube_id);
        m_move_grabber.color[0] = color[0];
        m_move_grabber.color[1] = color[1];
        m_move_grabber.color[2] = color[2];
        m_move_grabber.color[3] = color[3];
        m_move_grabber.render_for_picking();
    }
    wxGetApp().unbind_shader();
}

void GLGizmoText::on_start_dragging()
{
    if (m_hover_id < 0) { return; }
    if (m_hover_id == m_move_cube_id) {
    } else {
        m_rotate_gizmo.start_dragging();
        m_angle = m_rotate_angle * M_PI / 180.0;
    }
}

void GLGizmoText::on_stop_dragging()
{
    if (m_hover_id == m_move_cube_id) {
    } else {
        m_rotate_gizmo.stop_dragging();
        // TODO: when start second rotatiton previous rotation rotate draggers
        // This is fast fix for second try to rotate
        // When fixing, move grabber above text (not on side)
        m_rotate_gizmo.set_angle(PI / 2);

        // apply rotation
        // TRN This is an item label in the undo-redo stack.
        m_parent.do_rotate(rotation_snapshot_name);
        m_rotate_start_angle.reset();
        //volume_transformation_changed();//todo
        // recalculate for surface cut
        auto text_volume = m_last_text_mv;
        if (text_volume != nullptr ) {//&& text_volume->emboss_shape.has_value() && text_volume->emboss_shape->projection.use_surface
            if (*m_angle < 0) {
                *m_angle += M_PI * 2;
            }
            m_rotate_angle = *m_angle / M_PI * 180.0;
            m_need_update_text = true;
        }
    }
}

void GLGizmoText::on_update(const UpdateData &data)
{
    if (m_hover_id == 0) {
        m_rotate_gizmo.update(data);
        return;
    }
    Vec2d              mouse_pos = Vec2d(data.mouse_pos.x(), data.mouse_pos.y());

    const Selection &selection = m_parent.get_selection();
    auto mo                         = selection.get_model()->objects[m_object_idx];
    if (mo == nullptr)
        return;

    const ModelInstance *mi        = mo->instances[selection.get_instance_idx()];
    const Camera &       camera    = wxGetApp().plater()->get_camera();

    std::vector<Transform3d> trafo_matrices;
    for (const ModelVolume *mv : mo->volumes) {
        if (mv->is_model_part()) { trafo_matrices.emplace_back(mi->get_transformation().get_matrix() * mv->get_matrix()); }
    }

    Vec3f  normal                       = Vec3f::Zero();
    Vec3f  hit                          = Vec3f::Zero();
    size_t facet                        = 0;
    Vec3f  closest_hit                  = Vec3f::Zero();
    Vec3f  closest_normal               = Vec3f::Zero();
    double closest_hit_squared_distance = std::numeric_limits<double>::max();
    int    closest_hit_mesh_id          = -1;

    // Cast a ray on all meshes, pick the closest hit and save it for the respective mesh
    for (int mesh_id = 0; mesh_id < int(trafo_matrices.size()); ++mesh_id) {
        if (mesh_id == m_volume_idx)
            continue;

        MeshRaycaster mesh_raycaster = MeshRaycaster(mo->volumes[mesh_id]->mesh());

        if (mesh_raycaster.unproject_on_mesh(mouse_pos, trafo_matrices[mesh_id], camera, hit, normal, m_c->object_clipper()->get_clipping_plane(),
                                                                       &facet)) {
            // In case this hit is clipped, skip it.
            if (is_mesh_point_clipped(hit.cast<double>(), trafo_matrices[mesh_id])) continue;

            // Is this hit the closest to the camera so far?
            double hit_squared_distance = (camera.get_position() - trafo_matrices[mesh_id] * hit.cast<double>()).squaredNorm();
            if (hit_squared_distance < closest_hit_squared_distance) {
                closest_hit_squared_distance = hit_squared_distance;
                closest_hit_mesh_id          = mesh_id;
                closest_hit                  = hit;
                closest_normal               = normal;
            }
        }
    }

    if (closest_hit == Vec3f::Zero() && closest_normal == Vec3f::Zero()) return;

    if (closest_hit_mesh_id != -1) {
        m_rr = {mouse_pos, closest_hit_mesh_id, closest_hit, closest_normal};//on drag
        m_need_fix         = false;
        close_warning_flag_after_close_or_drag();
        m_need_update_text = true;
    }
}

void GLGizmoText::push_button_style(bool pressed) {
    if (m_is_dark_mode) {
        if (pressed) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(43 / 255.f, 64 / 255.f, 54 / 255.f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(43 / 255.f, 64 / 255.f, 54 / 255.f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(43 / 255.f, 64 / 255.f, 54 / 255.f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.f, 174 / 255.f, 66 / 255.f, 1.f));
        }
        else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(45.f / 255.f, 45.f / 255.f, 49.f / 255.f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(84 / 255.f, 84 / 255.f, 90 / 255.f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(84 / 255.f, 84 / 255.f, 90 / 255.f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(45.f / 255.f, 45.f / 255.f, 49.f / 255.f, 1.f));
        }
    }
    else {
        if (pressed) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(219 / 255.f, 253 / 255.f, 231 / 255.f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(219 / 255.f, 253 / 255.f, 231 / 255.f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(219 / 255.f, 253 / 255.f, 231 / 255.f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.f, 174 / 255.f, 66 / 255.f, 1.f));
        }
        else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.f, 1.f, 1.f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(238 / 255.f, 238 / 255.f, 238 / 255.f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(238 / 255.f, 238 / 255.f, 238 / 255.f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.f, 1.f, 1.f, 1.f));
        }

    }
}

void GLGizmoText::pop_button_style() {
    ImGui::PopStyleColor(4);
}

void GLGizmoText::push_combo_style(const float scale) {
    if (m_is_dark_mode) {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 1.0f * scale);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f * scale);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGuiWrapper::COL_WINDOW_BG_DARK);
        ImGui::PushStyleColor(ImGuiCol_BorderActive, ImVec4(0.00f, 0.68f, 0.26f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.00f, 0.68f, 0.26f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.00f, 0.68f, 0.26f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.00f, 0.68f, 0.26f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImGuiWrapper::COL_WINDOW_BG_DARK);
        ImGui::PushStyleColor(ImGuiCol_Button, { 1.00f, 1.00f, 1.00f, 0.0f });
    }
    else {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 1.0f * scale);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f * scale);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGuiWrapper::COL_WINDOW_BG);
        ImGui::PushStyleColor(ImGuiCol_BorderActive, ImVec4(0.00f, 0.68f, 0.26f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.00f, 0.68f, 0.26f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.00f, 0.68f, 0.26f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.00f, 0.68f, 0.26f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImGuiWrapper::COL_WINDOW_BG);
        ImGui::PushStyleColor(ImGuiCol_Button, { 1.00f, 1.00f, 1.00f, 0.0f });
    }
}

void GLGizmoText::pop_combo_style()
{
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(7);
}

// BBS
void GLGizmoText::on_render_input_window(float x, float y, float bottom_limit)
{
    /*if (!m_init_texture) {
        update_font_texture();
        m_init_texture = true;
    }

    if (m_imgui->get_font_size() != m_scale) {
        m_scale = m_imgui->get_font_size();
        update_font_texture();
    }
    if (m_textures.size() == 0) {
        BOOST_LOG_TRIVIAL(info) << "GLGizmoText has no texture";
        return;
    }*/

    const Selection &selection = m_parent.get_selection();
    if (selection.is_single_full_instance() || selection.is_single_full_object()) {
        const GLVolume * gl_volume = selection.get_volume(*selection.get_volume_idxs().begin());
        int object_idx = gl_volume->object_idx();
        if (object_idx != m_object_idx || (object_idx == m_object_idx && m_volume_idx != -1)) {
            m_object_idx = object_idx;
            m_volume_idx = -1;
            reset_text_info();
        }
    } else if (selection.is_single_volume() || selection.is_single_modifier()) {
        int object_idx, volume_idx;
        ModelVolume *model_volume = m_parent.get_selection().get_selected_single_volume(object_idx, volume_idx);
        if ((object_idx != m_object_idx || (object_idx == m_object_idx && volume_idx != m_volume_idx))
            && model_volume) {
            m_last_text_mv     = model_volume;
            TextInfo text_info = model_volume->get_text_info();
            load_from_text_info(text_info);//mouse click down
            m_is_modify = true;
            m_volume_idx = volume_idx;
            m_object_idx = object_idx;
        }
    }

    const float win_h = ImGui::GetWindowHeight();
    y = std::min(y, bottom_limit - win_h);
    GizmoImguiSetNextWIndowPos(x, y, ImGuiCond_Always, 0.0f, 0.0f);

    static float last_y = 0.0f;
    static float last_h = 0.0f;

    const float currt_scale = m_parent.get_scale();
    ImGuiWrapper::push_toolbar_style(currt_scale);
    double screen_scale = wxDisplay(wxGetApp().plater()).GetScaleFactor();
    // Configuration creation
    if (m_gui_cfg == nullptr ||                    // Exist configuration - first run
        m_gui_cfg->screen_scale != screen_scale || // change of DPI
        m_gui_cfg->dark_mode != m_is_dark_mode     // change of dark mode
    ) {
        // Create cache for gui offsets
        auto cfg     = Text::create_gui_configuration();
        cfg.screen_scale = screen_scale;
        cfg.dark_mode    = m_is_dark_mode;

        GuiCfg gui_cfg{std::move(cfg)};
        m_gui_cfg = std::make_unique<GuiCfg>(std::move(gui_cfg));

        // change resolution regenerate icons
        m_icons = init_text_icons(m_icon_manager, *m_gui_cfg);//init_icons();//todo
        m_style_manager.clear_imgui_font();
    }


    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0,5.0) * currt_scale);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 4.0f * currt_scale);
    GizmoImguiBegin("Text", ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
#ifdef DEBUG_TEXT
    std::string world_hit = "world hit x:" + formatFloat(m_text_position_in_world[0]) + " y:" + formatFloat(m_text_position_in_world[1]) +
                            " z:" + formatFloat(m_text_position_in_world[2]);
    std::string hit = "local hit x:" + formatFloat(m_rr.hit[0]) + " y:" + formatFloat(m_rr.hit[1]) + " z:" + formatFloat(m_rr.hit[2]);
    std::string normal = "normal x:" + formatFloat(m_rr.normal[0]) + " y:" + formatFloat(m_rr.normal[1]) + " z:" + formatFloat(m_rr.normal[2]);
    auto cut_dir = "cut_dir x:" + formatFloat(m_cut_plane_dir_in_world[0]) + " y:" + formatFloat(m_cut_plane_dir_in_world[1]) + " z:" + formatFloat(m_cut_plane_dir_in_world[2]);
    auto fix_position_str = "fix position:" + formatFloat(m_fix_text_position_in_world[0]) + " y:" + formatFloat(m_fix_text_position_in_world[1]) +
                            " z:" + formatFloat(m_fix_text_position_in_world[2]);
    auto fix_normal_str = "fix normal:" + formatFloat(m_fix_text_normal_in_world[0]) + " y:" + formatFloat(m_fix_text_normal_in_world[1]) +
                          " z:" + formatFloat(m_fix_text_normal_in_world[2]);
    m_imgui->text(world_hit);
    m_imgui->text(hit);
    m_imgui->text(normal);
    m_imgui->text(cut_dir);
    m_imgui->text(fix_position_str);
    m_imgui->text(fix_normal_str);
#endif
    float space_size = m_imgui->get_style_scaling() * 8;
    float font_cap = m_imgui->calc_text_size(_L("Font")).x;
    float size_cap = m_imgui->calc_text_size(_L("Size")).x;
    float thickness_cap = m_imgui->calc_text_size(_L("Thickness")).x;
    float input_cap = m_imgui->calc_text_size(_L("Input text")).x;
    float depth_cap = m_imgui->calc_text_size(_L("Embeded")).x;
    float caption_size  = std::max(std::max(font_cap, size_cap), std::max(depth_cap, input_cap)) + space_size + ImGui::GetStyle().WindowPadding.x;

    float input_text_size = m_imgui->scaled(10.0f);
    float button_size = ImGui::GetFrameHeight();

    ImVec2 selectable_size(std::max((input_text_size + ImGui::GetFrameHeight() * 2), m_combo_width + SELECTABLE_INNER_OFFSET * currt_scale), m_combo_height);
    float list_width = selectable_size.x + ImGui::GetStyle().ScrollbarSize + 2 * currt_scale;

    float input_size = list_width - button_size * 2 - ImGui::GetStyle().ItemSpacing.x * 4;

    ImTextureID normal_B = m_parent.get_gizmos_manager().get_icon_texture_id(GLGizmosManager::MENU_ICON_NAME::IC_TEXT_B);
    ImTextureID normal_T = m_parent.get_gizmos_manager().get_icon_texture_id(GLGizmosManager::MENU_ICON_NAME::IC_TEXT_T);
    ImTextureID normal_B_dark = m_parent.get_gizmos_manager().get_icon_texture_id(GLGizmosManager::MENU_ICON_NAME::IC_TEXT_B_DARK);
    ImTextureID normal_T_dark = m_parent.get_gizmos_manager().get_icon_texture_id(GLGizmosManager::MENU_ICON_NAME::IC_TEXT_T_DARK);

    // adjust window position to avoid overlap the view toolbar
    if (last_h != win_h || last_y != y) {
        // ask canvas for another frame to render the window in the correct position
        m_imgui->set_requires_extra_frame();
        if (last_h != win_h)
            last_h = win_h;
        if (last_y != y)
            last_y = y;
    }

    if (m_gui_cfg && (int) m_gui_cfg->input_offset != (int) caption_size) {
        m_gui_cfg->input_offset = caption_size;
    }
    ImGui::AlignTextToFramePadding();
    m_imgui->text(_L("Input text"));
    ImGui::SameLine(caption_size);
    ImGui::PushItemWidth(2 * m_gui_cfg->input_width);
    draw_text_input(caption_size);
    ImGui::AlignTextToFramePadding();
    m_imgui->text(_L("Font"));
    ImGui::SameLine(caption_size);
    ImGui::PushItemWidth(2 * m_gui_cfg->input_width);
    draw_font_list();

    draw_height();
    /*ImGui::AlignTextToFramePadding();
    m_imgui->text(_L("Size"));
    ImGui::SameLine(caption_size);
    ImGui::PushItemWidth(input_size);
    if (ImGui::InputFloat("###font_size", &m_font_size, 0.0f, 0.0f, "%.2f")) {
        limit_value(m_font_size, m_font_size_min, m_font_size_max);
        m_need_update_text = true;
    }*/
    ImGui::SameLine();

    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f * currt_scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {1.0f * currt_scale, 1.0f * currt_scale });
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f * currt_scale);
    push_button_style(m_bold);
    bool exist_change = false;
    if (ImGui::ImageButton(m_is_dark_mode ? normal_B_dark : normal_B, {button_size - 2 * ImGui::GetStyle().FramePadding.x, button_size - 2 * ImGui::GetStyle().FramePadding.y})) {
        m_bold = !m_bold;
        //update_boldness(); in process();
        exist_change = true;
    }
    pop_button_style();
    auto temp_input_width = 100;
    auto cur_temp_x = caption_size * 2 + temp_input_width + 5 * space_size;
    ImGui::SameLine(cur_temp_x - 5 * space_size);
    ImGui::PushItemWidth(button_size);
    push_button_style(m_italic);
    if (ImGui::ImageButton(m_is_dark_mode ? normal_T_dark : normal_T, {button_size - 2 * ImGui::GetStyle().FramePadding.x, button_size - 2 * ImGui::GetStyle().FramePadding.y})) {
        m_italic = !m_italic;
        update_italic();
        exist_change               = true;
    }
    if (exist_change) {
        m_style_manager.clear_glyphs_cache();
        process();
    }
    pop_button_style();
    ImGui::PopStyleVar(3);

    ImGui::AlignTextToFramePadding();
    m_imgui->text(_L("Thickness"));
    ImGui::SameLine(caption_size);
    ImGui::PushItemWidth(temp_input_width); // 2 * m_gui_cfg->input_width
    float old_value = m_thickness;
    ImGui::InputFloat("###text_thickness", &m_thickness, 0.0f, 0.0f, "%.2f");
    m_thickness = ImClamp(m_thickness, m_thickness_min, m_thickness_max);
    if (old_value != m_thickness) {
        process();
    }

    ImGui::SameLine(caption_size + space_size * 2 + temp_input_width);
    ImGui::PushItemWidth(caption_size);
    m_imgui->text(_L("Embeded depth"));

    ImGui::SameLine(cur_temp_x);
    auto full_width = caption_size + 2 * m_gui_cfg->input_width;
    auto valid_width = full_width - cur_temp_x;
    ImGui::PushItemWidth(valid_width);
    old_value = m_embeded_depth;
    if (ImGui::InputFloat("###text_embeded_depth", &m_embeded_depth, 0.0f, 0.0f, "%.2f")) {
        limit_value(m_embeded_depth, 0.0f, m_embeded_depth_max);
    }
    if (old_value != m_embeded_depth) {
        process();
    }

    const float slider_icon_width = m_imgui->get_slider_icon_size().x;
    const float slider_width      = list_width - 1.5 * slider_icon_width - space_size;
    const float drag_left_width   = caption_size + slider_width + space_size;

    ImGui::AlignTextToFramePadding();
    m_imgui->text(_L("Text Gap"));
    ImGui::SameLine(caption_size);
    ImGui::PushItemWidth(slider_width);
    if (m_imgui->bbl_slider_float_style("##text_gap", &m_text_gap, -100, 100, "%.2f", 1.0f, true))
        m_need_update_text = true;
    ImGui::SameLine(drag_left_width);
    ImGui::PushItemWidth(1.5 * slider_icon_width);
    if (ImGui::BBLDragFloat("##text_gap_input", &m_text_gap, 0.05f, 0.0f, 0.0f, "%.2f"))
        m_need_update_text = true;

    m_imgui->disabled_begin(m_use_current_pose);
    ImGui::AlignTextToFramePadding();
    m_imgui->text(_L("Angle"));
    ImGui::SameLine(caption_size);
    ImGui::PushItemWidth(slider_width);
    if (m_imgui->bbl_slider_float_style("##angle", &m_rotate_angle, 0, 360, "%.2f", 1.0f, true))
        m_need_update_text = true;
    ImGui::SameLine(drag_left_width);
    ImGui::PushItemWidth(1.5 * slider_icon_width);
    if (ImGui::BBLDragFloat("##angle_input", &m_rotate_angle, 0.05f, 0.0f, 0.0f, "%.2f"))
        m_need_update_text = true;
    m_imgui->disabled_end();

    draw_style_list(caption_size);
    //warnning
    std::string text = m_text;
    if (m_warning_font) {
        m_imgui->warning_text_wrapped(_L("Warning:Due to font upgrades,previous font is replaced with Arial font, and recommend you to modify the font."), full_width);
        m_parent.request_extra_frame();
    }
    if (m_warning_cur_font_not_support_part_text) {
        m_imgui->warning_text(_L("Warning:cur font not support partial Text,please change font."));
    }
    if (m_show_warning_text_create_fail) {
        m_imgui->warning_text(_L("Warning:create text fail."));
    }
    if (m_show_text_normal_error) {
        m_imgui->warning_text(_L("Warning:text normal is error."));
    }
    if (m_show_text_normal_reset_tip) {
        m_imgui->warning_text(_L("Warning:text normal has been reset."));
    }
    if (m_show_warning_regenerated) {
        m_imgui->warning_text_wrapped(_L("Warning:Because current text does indeed use surround algorithm,if continue to edit, text has to regenerated according to new location."),
                              full_width);
        m_parent.request_extra_frame();
    }
    if (m_show_warning_old_tran) {
        m_imgui->warning_text_wrapped(_L("Warning:old matrix has at least two parameters: mirroring, scaling, and rotation. If you continue editing, it may not be correct. Please "
                                 "dragging text or cancel using current pose, save and reedit again."),
                              full_width);
        m_parent.request_extra_frame();
    }
    if (m_show_warning_error_mesh) {
        m_imgui->warning_text_wrapped(
            _L("Error:Detecting an incorrect mesh id or an unknown error, regenerating text may result in incorrect outcomes.Please drag text,save it then reedit it again."),
            full_width);
        m_parent.request_extra_frame();
    }
    if (m_show_warning_lost_rotate) {
        m_imgui->warning_text_wrapped(
            _L("Warning:Due to functional upgrade, rotation information cannot be restored. Please drag or modify text,save it and reedit it will ok."),
                              full_width);
        m_parent.request_extra_frame();
    }
    ImGui::Separator();
    if (m_need_fix && m_text_type > TextType::HORIZONAL) {
        ImGui::AlignTextToFramePadding();
        ImGui::SameLine(caption_size);
        ImGui::PushItemWidth(list_width);
        ImGui::AlignTextToFramePadding();
        if (m_imgui->bbl_checkbox(_L("Use opened text pose"), m_use_current_pose)) {
            m_need_update_text = true;
        }
    }
    else {
        m_use_current_pose = false;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 10.0f));
    float get_cur_y = ImGui::GetContentRegionMax().y + ImGui::GetFrameHeight() + y;
    show_tooltip_information(x, get_cur_y);

    float f_scale = m_parent.get_main_toolbar_scale();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 4.0f * f_scale));

    ImGui::SameLine(caption_size);
    ImGui::AlignTextToFramePadding();
    auto is_surface_text = m_text_type == TextType::SURFACE || m_text_type == TextType::SURFACE_HORIZONAL;
    if (m_imgui->bbl_checkbox(_L("Surface"), is_surface_text)) {
        m_need_update_text = true;
    }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    auto is_keep_horizontal = m_text_type == TextType::HORIZONAL || m_text_type == TextType::SURFACE_HORIZONAL;
    if (m_imgui->bbl_checkbox(_L("Horizontal text"), is_keep_horizontal)) {
        m_need_update_text = true;
        if (is_surface_text && is_keep_horizontal == false) {
            update_text_normal_in_world();
        }
    }
    check_text_type(is_surface_text, is_keep_horizontal);

    //ImGui::SameLine();
    //ImGui::AlignTextToFramePadding();
    //m_imgui->text(_L("Status:"));
    //float status_cap = m_imgui->calc_text_size(_L("Status:")).x + space_size + ImGui::GetStyle().WindowPadding.x;
    //ImGui::SameLine();
    //m_imgui->text(m_is_modify ? _L("Modify") : _L("Add"));

    ImGui::PopStyleVar(2);

#if 0
    ImGuiIO& io = ImGui::GetIO();
    ImFontAtlas* atlas = io.Fonts;
    ImVec4 tint_col = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    ImVec4 border_col = ImVec4(0.0f, 0.0f, 0.0f, 0.8f);
    m_imgui->text(wxString("") << atlas->TexWidth << " * " << atlas->TexHeight);
    ImGui::Image(atlas->TexID, ImVec2((float)atlas->TexWidth, (float)atlas->TexHeight), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), tint_col, border_col);
#endif

    GizmoImguiEnd();
    ImGui::PopStyleVar(2);
    ImGuiWrapper::pop_toolbar_style();
}

void GLGizmoText::show_tooltip_information(float x, float y)
{
    std::array<std::string, 1> info_array  = std::array<std::string, 1>{"rotate_text"};
    float                      caption_max = 0.f;
    for (const auto &t : info_array) { caption_max = std::max(caption_max, m_imgui->calc_text_size(m_desc[t + "_caption"]).x); }

    ImTextureID normal_id = m_parent.get_gizmos_manager().get_icon_texture_id(GLGizmosManager::MENU_ICON_NAME::IC_TOOLBAR_TOOLTIP);
    ImTextureID hover_id  = m_parent.get_gizmos_manager().get_icon_texture_id(GLGizmosManager::MENU_ICON_NAME::IC_TOOLBAR_TOOLTIP_HOVER);

    caption_max += m_imgui->calc_text_size(": ").x + 35.f;

    float  font_size   = ImGui::GetFontSize();
    ImVec2 button_size = ImVec2(font_size * 1.8, font_size * 1.3);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0, ImGui::GetStyle().FramePadding.y});
    ImGui::ImageButton3(normal_id, hover_id, button_size);

    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip2(ImVec2(x, y));
        auto draw_text_with_caption = [this, &caption_max](const wxString &caption, const wxString &text) {
            m_imgui->text_colored(ImGuiWrapper::COL_ACTIVE, caption);
            ImGui::SameLine(caption_max);
            m_imgui->text_colored(ImGuiWrapper::COL_WINDOW_BG, text);
        };

        for (const auto &t : info_array)
            draw_text_with_caption(m_desc.at(t + "_caption") + ": ", m_desc.at(t));
        ImGui::EndTooltip();
    }
    ImGui::PopStyleVar(2);
}

bool GLGizmoText::set_height()
{
    float &value = m_style_manager.get_font_prop().size_in_mm;
    // size can't be zero or negative
    apply(value, Text::limits.size_in_mm);
    auto text_volume = m_last_text_mv;
    if (text_volume == nullptr) {
        return false;
    }
    // only different value need process
    //if (is_approx(value, text_volume->get_text_info().m_font_size_in_mm)) // m_volume->text_configuration->style.prop.size_in_mm
    //    return false;
   /* if (m_style_manager.get_font_prop().per_glyph)
        reinit_text_lines(m_text_lines.get_lines().size());*///todo

    return true;
}

void GLGizmoText::draw_text_input(int caption_width)
{
    bool allow_multi_line       = false;
    auto create_range_text_prep = [&mng = m_style_manager, &text = m_text, &exist_unknown = m_text_contain_unknown_glyph]() {
        auto &ff = mng.get_font_file_with_cache();
        assert(ff.has_value());
        const auto & cn         = mng.get_font_prop().collection_number;
        unsigned int font_index = (cn.has_value()) ? *cn : 0;
        return create_range_text(text, *ff.font_file, font_index, &exist_unknown);
    };

    double  scale      = m_scale_height.has_value() ? *m_scale_height : 1.;
    ImFont *imgui_font = m_style_manager.get_imgui_font();
    if (imgui_font == nullptr) {
        // try create new imgui font
        double screen_scale = wxDisplay(wxGetApp().plater()).GetScaleFactor();
        double imgui_scale  = scale * screen_scale;
        m_style_manager.create_imgui_font(create_range_text_prep(), imgui_scale);
        imgui_font = m_style_manager.get_imgui_font();
    }
    bool exist_font = imgui_font != nullptr && imgui_font->IsLoaded() && imgui_font->Scale > 0.f && imgui_font->ContainerAtlas != nullptr;
    // NOTE: Symbol fonts doesn't have atlas
    // when their glyph range is out of language character range
    if (exist_font){
        ImGui::PushFont(imgui_font);
    }

    // show warning about incorrectness view of font
    std::string warning_tool_tip;
    if (!exist_font) {
        warning_tool_tip = _u8L("The text cannot be written using the selected font. Please try choosing a different font.");
    } else {
        auto append_warning = [&warning_tool_tip](std::string t) {
            if (!warning_tool_tip.empty())
                warning_tool_tip += "\n";
            warning_tool_tip += t;
        };
        if (is_text_empty(m_text) && m_is_modify)  {//BBS modify
            append_warning(_u8L("Embossed text cannot contain only white spaces."));
        }
        if (m_text_contain_unknown_glyph) {
            append_warning(_u8L("Text contains character glyph (represented by '?') unknown by font.Please modify font or input."));
            m_warning_cur_font_not_support_part_text = true;
        }
        else {
            m_warning_cur_font_not_support_part_text = false;
        }
        const FontProp &prop = m_style_manager.get_font_prop();
       /* if (prop.skew.has_value())//BBS modify
            append_warning(_u8L("Text input doesn't show font skew."));
        if (prop.boldness.has_value())
            append_warning(_u8L("Text input doesn't show font boldness."));
        if (prop.line_gap.has_value())
            append_warning(_u8L("Text input doesn't show gap between lines."));*/
        auto &ff         = m_style_manager.get_font_file_with_cache();
        float imgui_size = StyleManager::get_imgui_font_size(prop, *ff.font_file, scale);
        if (imgui_size > StyleManager::max_imgui_font_size)
            append_warning(_u8L("Too tall, diminished font height inside text input."));
        if (imgui_size < StyleManager::min_imgui_font_size)
            append_warning(_u8L("Too small, enlarged font height inside text input."));
        bool is_multiline = m_text_lines.get_lines().size() > 1;
        if (is_multiline && (prop.align.first == FontProp::HorizontalAlign::center || prop.align.first == FontProp::HorizontalAlign::right))
            append_warning(_u8L("Text doesn't show current horizontal alignment."));
    }

    // flag for extend font ranges if neccessary
    // ranges can't be extend during font is activ(pushed)
    std::string               range_text;
    ImVec2                    input_size(2 * m_gui_cfg->input_width, m_gui_cfg->text_size.y); // 2 * m_gui_cfg->input_width - caption_width
    const ImGuiInputTextFlags flags = ImGuiInputTextFlags_AllowTabInput;// | ImGuiInputTextFlags_AutoSelectAll
    if (ImGui::InputTextMultiline("##Text", &m_text, input_size, flags)) {
        if (m_style_manager.get_font_prop().per_glyph) {
            unsigned count_lines = get_count_lines(m_text);
            //if (count_lines != m_text_lines.get_lines().size())
            //    // Necesarry to initialize count by given number (differ from stored in volume at the moment)
            //    reinit_text_lines(count_lines);
        }
        process();
        range_text = create_range_text_prep();
    }

    if (exist_font)
        ImGui::PopFont();

    // warning tooltip has to be with default font
    if (!warning_tool_tip.empty() && ( !allow_multi_line ||(allow_multi_line && ImGui::GetCurrentWindow()->DC.ChildWindows.Data))) {
        // Multiline input has hidden window for scrolling
        float scrollbar_width, scrollbar_height;
        const ImGuiStyle &style = ImGui::GetStyle();
        if (allow_multi_line) {
            const ImGuiWindow *input            = ImGui::GetCurrentWindow()->DC.ChildWindows.front();
            scrollbar_width  = (input->ScrollbarY) ? style.ScrollbarSize : 0.f;
            scrollbar_height = (input->ScrollbarX) ? style.ScrollbarSize : 0.f;
        } else {
            scrollbar_width  = 4;
            scrollbar_height = 2;
        }
        if (ImGui::IsItemHovered())
            m_imgui->tooltip(warning_tool_tip, m_gui_cfg->max_tooltip_width);

        ImVec2        cursor  = ImGui::GetCursorPos();
        float         width   = ImGui::GetContentRegionAvailWidth();
        const ImVec2 &padding = style.FramePadding;
        ImVec2        icon_pos(2 * m_gui_cfg->input_width, cursor.y - m_gui_cfg->icon_width - scrollbar_height - 2 * padding.y);// width - m_gui_cfg->icon_width - scrollbar_width + padding.x

        ImGui::SetCursorPos(icon_pos);
        draw(get_icon(m_icons, Text::IconType::warning, Text::IconState::hovered));
        ImGui::SetCursorPos(cursor);
    }

    // NOTE: must be after ImGui::font_pop()
    //          -> imgui_font has to be unused
    // IMPROVE: only extend not clear
    // Extend font ranges
    if (!range_text.empty() && !ImGuiWrapper::contain_all_glyphs(imgui_font, range_text))
        m_style_manager.clear_imgui_font();
}

void GLGizmoText::draw_font_list()
{
    ImGuiWrapper::push_combo_style(m_gui_cfg->screen_scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 4.0f * m_gui_cfg->screen_scale);

    wxString tooltip_name = "";

    // Set partial
    wxString actual_face_name;
    if (m_style_manager.is_active_font()) {
        const wxFont &wx_font = m_style_manager.get_wx_font();
        if (wx_font.IsOk()){
            actual_face_name = wx_font.GetFaceName();
            m_cur_font_name  = actual_face_name;
            m_font_name      = actual_face_name.utf8_string();
        }
    }
    // name of actual selected font
    const char *selected = (!actual_face_name.empty()) ? (const char *) actual_face_name.c_str() : " --- ";

    // Do not remove font face during enumeration
    // When deletation of font appear this variable is set
    std::optional<size_t> del_index;

    ImGui::SetNextItemWidth(2 * m_gui_cfg->input_width);
    std::vector<int> filtered_items_idx;
    bool             is_filtered = false;
    if (m_imgui->bbl_combo_with_filter("##Combo_Font", selected, m_face_names->faces_names, &filtered_items_idx, &is_filtered, m_imgui->scaled(32.f / 15.f))) {
        bool set_selection_focus = false;
        if (!m_face_names->is_init) {
            init_face_names(*m_face_names);
            set_selection_focus = true;
        }

        if (!m_face_names->has_truncated_names)
            init_truncated_names(*m_face_names, m_gui_cfg->input_width);

        if (m_face_names->texture_id == 0)
            init_font_name_texture();

        int show_items_count = is_filtered ? filtered_items_idx.size() : m_face_names->faces.size();

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0);

        for (int i = 0; i < show_items_count; i++) {
            int             idx          = is_filtered ? filtered_items_idx[i] : i;
            FaceName &      face         = m_face_names->faces[idx];
            const wxString &wx_face_name = face.wx_name;

            ImGui::PushID(idx);
            ScopeGuard           sg([]() { ImGui::PopID(); });
            bool                 is_selected = (actual_face_name == wx_face_name);
            ImVec2               selectable_size(0, m_imgui->scaled(32.f / 15.f));
            ImGuiSelectableFlags flags = 0;
            if (ImGui::BBLSelectable(face.name_truncated.c_str(), is_selected, flags, selectable_size)) {
                if (!select_facename(wx_face_name,true)) {
                    del_index = idx;
                    MessageDialog(wxGetApp().plater(), GUI::format_wxstr(_L("Font \"%1%\" can't be selected."), wx_face_name));
                } else {
                    ImGui::CloseCurrentPopup();
                }
            }
            // tooltip as full name of font face
            if (ImGui::IsItemHovered())
                tooltip_name = wx_face_name;

            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }

            // on first draw set focus on selected font
            if (set_selection_focus && is_selected)
                ImGui::SetScrollHereY();
            draw_font_preview(face, m_text, *m_face_names, *m_gui_cfg, ImGui::IsItemVisible());
        }

        ImGui::PopStyleVar(3);
        ImGui::EndListBox();
        ImGui::EndPopup();
    } else if (m_face_names->is_init) {
        // Just one after close combo box
        // free texture and set id to zero
        m_face_names->is_init = false;
        // cancel all process for generation of texture
        for (FaceName &face : m_face_names->faces)
            if (face.cancel != nullptr)
                face.cancel->store(true);
        glsafe(::glDeleteTextures(1, &m_face_names->texture_id));
        m_face_names->texture_id = 0;
    }

    // delete unloadable face name when try to use
    if (del_index.has_value()) {
        auto                   face = m_face_names->faces.begin() + (*del_index);
        std::vector<wxString> &bad  = m_face_names->bad;
        // sorted insert into bad fonts
        auto it = std::upper_bound(bad.begin(), bad.end(), face->wx_name);
        bad.insert(it, face->wx_name);
        m_face_names->faces.erase(face);
        m_face_names->faces_names.erase(m_face_names->faces_names.begin() + (*del_index));
        // update cached file
        store(*m_face_names);
    }

#ifdef ALLOW_ADD_FONT_BY_FILE
    ImGui::SameLine();
    // select font file by file browser
    if (draw_button(IconType::open_file)) {
        if (choose_true_type_file()) { process(); }
    } else if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Add file with font(.ttf, .ttc)");
#endif //  ALLOW_ADD_FONT_BY_FILE

#ifdef ALLOW_ADD_FONT_BY_OS_SELECTOR
    ImGui::SameLine();
    if (draw_button(IconType::system_selector)) {
        if (choose_font_by_wxdialog()) { process(); }
    } else if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Open dialog for choose from fonts.");
#endif //  ALLOW_ADD_FONT_BY_OS_SELECTOR

    ImGui::PopStyleVar(2);
    ImGuiWrapper::pop_combo_style();

    if (!tooltip_name.IsEmpty())
        m_imgui->tooltip(tooltip_name, m_gui_cfg->max_tooltip_width);
}

void GLGizmoText::draw_height(bool use_inch)
{
    float &            value            = m_style_manager.get_font_prop().size_in_mm;
    const EmbossStyle *stored_style     = m_style_manager.get_stored_style();
    const float *      stored           = (stored_style != nullptr) ? &stored_style->prop.size_in_mm : nullptr;
    const char *       size_format      = use_inch ? "%.2f in" : "%.1f mm";
    const std::string  revert_text_size = _u8L("Revert text size.");
    const std::string &name             = m_gui_cfg->translations.height;
    if (rev_input_mm(name, value, stored, revert_text_size, 0.1f, 1.f, size_format, use_inch, m_scale_height)) {
        if (set_height()) {
            process();
        }
    }
}

//template<>
bool imgui_input(const char *label, float *v, float step, float step_fast, const char *format, ImGuiInputTextFlags flags)
{
    return ImGui::InputFloat(label, v, step, step_fast, format, flags);
}
//template<>
bool imgui_input(const char *label, double *v, double step, double step_fast, const char *format, ImGuiInputTextFlags flags)
{
    return ImGui::InputDouble(label, v, step, step_fast, format, flags);
}

bool exist_change(const std::optional<float> &value, const std::optional<float> *default_value)
{
    if (default_value == nullptr) return false;
    return !is_approx(value, *default_value);
}

bool exist_change(const float &value, const float *default_value)
{
    if (default_value == nullptr) return false;
    return !is_approx(value, *default_value);
}

template<typename T, typename Draw>
bool GLGizmoText::revertible(const std::string &name, T &value, const T *default_value, const std::string &undo_tooltip, float undo_offset, Draw draw) const
{
    ImGui::AlignTextToFramePadding();
    bool changed = exist_change(value, default_value);
    if (changed || default_value == nullptr)
        ImGuiWrapper::text_colored(ImGuiWrapper::COL_BAMBU, name);
    else
        ImGuiWrapper::text(name);

    // render revert changes button
    if (changed) {
        ImGuiWindow *window = ImGui::GetCurrentWindow();
        float        prev_x = window->DC.CursorPosPrevLine.x;
        ImGui::SameLine(undo_offset); // change cursor postion
        if (draw_button(m_icons, IconType::undo)) {
            value = *default_value;

            // !! Fix to detect change of value after revert of float-slider
            m_imgui->get_last_slider_status().deactivated_after_edit = true;

            return true;
        } else if (ImGui::IsItemHovered())
            m_imgui->tooltip(undo_tooltip, m_gui_cfg->max_tooltip_width);
        window->DC.CursorPosPrevLine.x = prev_x; // set back previous position
    }
    return draw();
}

template<typename T>
bool GLGizmoText::rev_input(
    const std::string &name, T &value, const T *default_value, const std::string &undo_tooltip, T step, T step_fast, const char *format, ImGuiInputTextFlags flags) const
{
    // draw offseted input
    auto draw_offseted_input = [&offset = m_gui_cfg->input_offset, &width = m_gui_cfg->input_width, &name, &value, &step, &step_fast, format, flags]() {
        ImGui::SameLine(offset);
        ImGui::SetNextItemWidth(width);
        return imgui_input(("##" + name).c_str(), &value, step, step_fast, format, flags);
    };
    float undo_offset = m_gui_cfg->input_offset - m_gui_cfg->icon_width; // ImGui::GetStyle().WindowPadding.x;//todo
    return revertible(name, value, default_value, undo_tooltip, undo_offset, draw_offseted_input);
}

template<typename T>
bool GLGizmoText::rev_input_mm(const std::string &         name,
                                     T &                         value,
                                     const T *                   default_value_ptr,
                                     const std::string &         undo_tooltip,
                                     T                           step,
                                     T                           step_fast,
                                     const char *                format,
                                     bool                        use_inch,
                                     const std::optional<float> &scale) const
{
    // _variable which temporary keep value
    T value_ = value;
    T default_value_;
    if (use_inch) {
        // calc value in inch
        value_ *= GizmoObjectManipulation::mm_to_in;
        if (default_value_ptr) {
            default_value_    = GizmoObjectManipulation::mm_to_in * (*default_value_ptr);
            default_value_ptr = &default_value_;
        }
    }
    if (scale.has_value()) value_ *= *scale;
    bool use_correction = use_inch || scale.has_value();
    if (rev_input(name, use_correction ? value_ : value, default_value_ptr, undo_tooltip, step, step_fast, format)) {
        if (use_correction) {
            value = value_;
            if (use_inch) value *= GizmoObjectManipulation::in_to_mm;
            if (scale.has_value()) value /= *scale;
        }
        return true;
    }
    return false;
}

void GLGizmoText::draw_depth(bool use_inch) {}

void GLGizmoText::init_font_name_texture()
{
    Timer t("init_font_name_texture");
    // check if already exists
    GLuint &id = m_face_names->texture_id;
    if (id != 0) return;
    // create texture for font
    GLenum target = GL_TEXTURE_2D;
    glsafe(::glGenTextures(1, &id));
    glsafe(::glBindTexture(target, id));
    glsafe(::glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
    glsafe(::glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
    const Vec2i32 &            size = m_gui_cfg->face_name_size;
    GLint                      w = size.x(), h = m_face_names->count_cached_textures * size.y();
    std::vector<unsigned char> data(4 * w * h, {0});
    const GLenum               format = GL_RGBA, type = GL_UNSIGNED_BYTE;
    const GLint                level = 0, internal_format = GL_RGBA, border = 0;
    glsafe(::glTexImage2D(target, level, internal_format, w, h, border, format, type, data.data()));

    // bind default texture
    GLuint no_texture_id = 0;
    glsafe(::glBindTexture(target, no_texture_id));

    // clear info about creation of texture - no one is initialized yet
    for (FaceName &face : m_face_names->faces) {
        face.cancel     = nullptr;
        face.is_created = nullptr;
    }
}

void GLGizmoText::reinit_text_lines(unsigned count_lines) {
    init_text_lines(m_text_lines, m_parent.get_selection(), m_style_manager, count_lines);
}

bool GLGizmoText::check(ModelVolumeType volume_type)
{
    return volume_type == ModelVolumeType::MODEL_PART || volume_type == ModelVolumeType::NEGATIVE_VOLUME || volume_type == ModelVolumeType::PARAMETER_MODIFIER;
}

bool GLGizmoText::init_create(ModelVolumeType volume_type) {  // check valid volume type
    if (!check(volume_type)) {
        BOOST_LOG_TRIVIAL(error) << "Can't create embossed volume with this type: " << (int) volume_type;
        return false;
    }
    if (!is_activable()) {
        BOOST_LOG_TRIVIAL(error) << "Can't create text. Gizmo is not activabled.";
        return false;
    }
    // Check can't be inside is_activable() cause crash
    // steps to reproduce: start App -> key 't' -> key 'delete'
    if (wxGetApp().obj_list()->has_selected_cut_object()) {
        BOOST_LOG_TRIVIAL(error) << "Can't create text on cut object";
        return false;
    }
    m_style_manager.discard_style_changes();
    assert(!m_text_lines.is_init());
    m_text_lines.reset(); // remove not current text lines
    // set default text
    m_text = _u8L("3D Text");
    return true;
}

void GLGizmoText::reset_text_info()
{
    m_font_name     = "";
    m_font_size     = m_style_manager.get_font_prop().size_in_mm;
    m_curr_font_idx = 0;
    m_bold          = m_style_manager.get_font_prop().boldness > 0;
    m_italic        = m_style_manager.get_font_prop().skew > 0;
    m_thickness     = 2.f;
    m_text          = "";
    m_embeded_depth   = 0.f;
    m_rotate_angle    = 0;
    m_text_gap        = 0.f;
    m_text_type       = TextType::SURFACE;
    m_rr              = RaycastResult();
    m_is_modify = false;
}

void GLGizmoText::update_text_pos_normal() {
    if (m_rr.mesh_id < 0) { return; }
    if (m_rr.normal.norm() < 0.1) { return; }
    const Selection &selection = m_parent.get_selection();
    auto mo                    = selection.get_model()->objects[m_object_idx];
    if (mo == nullptr) {
        BOOST_LOG_TRIVIAL(info) << boost::format("Text: selected object is null");
        return;
    }
    const ModelInstance *mi        = mo->instances[selection.get_instance_idx()];

    std::vector<Geometry::Transformation> w_matrices;
    std::vector<Geometry::Transformation> mv_trans;
    for (const ModelVolume *mv : mo->volumes) {
        if (mv->is_model_part()) {
            w_matrices.emplace_back(Geometry::Transformation(mi->get_transformation().get_matrix() * mv->get_matrix()));
            mv_trans.emplace_back(Geometry::Transformation(mv->get_matrix()));
        }
    }
#ifdef DEBUG_TEXT_VALUE
    m_rr.hit    = Vec3f(-0.58, -1.70, -12.8);
    m_rr.normal = Vec3f(0,0,-1);//just rotate cube
#endif
    m_text_position_in_world  = w_matrices[m_rr.mesh_id].get_matrix() * m_rr.hit.cast<double>();
    m_text_normal_in_world    = (w_matrices[m_rr.mesh_id].get_matrix_no_offset().cast<float>() * m_rr.normal).normalized();
}

float GLGizmoText::get_text_height(const std::string &text)//todo
{
    std::wstring_convert<std::codecvt_utf8<wchar_t>> str_cnv;
    std::wstring                                     ws = boost::nowide::widen(text);
    std::vector<std::string>                         alphas;
    for (auto w : ws) {
        alphas.push_back(str_cnv.to_bytes(w));
    }
    auto  texts  = alphas ;
    float max_height = 0.f;
    for (int i = 0; i < texts.size(); ++i) {
        std::string alpha;
        if (texts[i] == " ") {
            alpha = "i";
        } else {
            alpha = texts[i];
        }
        TextResult text_result;
        load_text_shape(alpha.c_str(), m_font_name.c_str(), m_font_size, m_thickness + m_embeded_depth, m_bold, m_italic, text_result);
        auto height = text_result.text_mesh.bounding_box().size()[1];
        if (max_height < height ){
            max_height = height;
        }
    }
    return max_height;
}

void GLGizmoText::close_warning_flag_after_close_or_drag()
{
    m_show_warning_text_create_fail = false;
    m_show_warning_regenerated = false;
    m_show_warning_error_mesh  = false;
    m_show_warning_old_tran    = false;
    m_show_warning_lost_rotate      = false;
    m_show_text_normal_error   = false;
    m_show_text_normal_reset_tip = false;
    m_show_warning_lost_rotate      = false;
    m_is_version1_10_xoy            = false;
    m_is_version1_9_xoz             = false;
}

void GLGizmoText::update_text_normal_in_world()
{
    int          temp_object_idx;
    auto         mo = m_parent.get_selection().get_selected_single_object(temp_object_idx);
    if (mo && m_rr.mesh_id >= 0) {
        const ModelInstance *mi = mo->instances[m_parent.get_selection().get_instance_idx()];
        TriangleMesh text_attach_mesh(mo->volumes[m_rr.mesh_id]->mesh());
        text_attach_mesh.transform(mo->volumes[m_rr.mesh_id]->get_matrix());
        MeshRaycaster temp_ray_caster(text_attach_mesh);
        Vec3f         local_center = m_text_tran_in_object.get_offset().cast<float>(); //(m_text_tran_in_object.get_matrix() * box.center()).cast<float>(); //
        Vec3f         temp_normal;
        Vec3f         closest_pt = temp_ray_caster.get_closest_point(local_center, &temp_normal);
        m_text_normal_in_world = (mi->get_transformation().get_matrix_no_offset().cast<float>() * temp_normal).normalized();
    }
    else {
        BOOST_LOG_TRIVIAL(info) << boost::format("error: update_text_normal_in_world");
    }
}

bool GLGizmoText::update_text_positions(const std::vector<std::string>& texts)
{
    if (m_chars_mesh_result.size() == 0) {
        m_position_points.clear();
        return false;
    }
    std::vector<double> text_lengths;
    for (int i = 0; i < texts.size(); ++i) {
        std::string alpha;
        if (texts[i] == " ") {
            alpha = "i";
        } else {
            alpha = texts[i];
        }
        /*TextResult text_result;
        load_text_shape(alpha.c_str(), m_font_name.c_str(), m_font_size, m_thickness + m_embeded_depth, m_bold, m_italic, text_result);
        double half_x_length = text_result.text_width / 2;*/
        auto box = m_chars_mesh_result[i].bounding_box();
        auto box_size      = box.size();
        auto half_x_length = box_size[0] / 2.0f;
        text_lengths.emplace_back(half_x_length + 1); // default
    }

    int text_num = m_chars_mesh_result.size();//FIX by BBS 20250109
    m_position_points.clear();
    m_normal_points.clear();

    const Selection &selection    = m_parent.get_selection();
    auto mo = selection.get_model()->objects[m_object_idx];
    if (mo == nullptr)
        return false;
    const ModelInstance *mi        = mo->instances[selection.get_instance_idx()];
    m_model_object_in_world_tran   = mi->get_transformation();
    // Precalculate transformations of individual meshes.
    std::vector<Transform3d> trafo_matrices;
    std::vector<Transform3d> rotate_trafo_matrices;
    for (const ModelVolume *mv : mo->volumes) {
        if (mv->is_model_part()) {
            trafo_matrices.emplace_back(mi->get_transformation().get_matrix() * mv->get_matrix());
            rotate_trafo_matrices.emplace_back(mi->get_transformation().get_matrix(true, false, true, true) * mv->get_matrix(true, false, true, true));
        }
    }

    if (m_rr.mesh_id == -1) {
        BOOST_LOG_TRIVIAL(info) << boost::format("Text: mrr_mesh_id is -1");
        return false;
    }
    // mouse_position_world may is error after user modified
    if (m_need_fix) {
        if (m_text_normal_in_world.norm() < 0.1) {
            m_show_text_normal_reset_tip = true;
        }
        use_fix_normal_position();
    }
    if (m_text_normal_in_world.norm() < 0.1) {
        m_show_text_normal_error = true;
        BOOST_LOG_TRIVIAL(info) << "m_text_normal_in_object is error";
        return false;
    }
    m_show_text_normal_error  = false;
    auto mouse_position_world = m_text_position_in_world.cast<double>();
    auto mouse_normal_world   = m_text_normal_in_world.cast<double>();

    TriangleMesh slice_meshs;
    int mesh_index = 0;
    int volume_index = 0;
    for (int i = 0; i < mo->volumes.size(); ++i) {
        // skip the editing text volume
        if (m_is_modify && m_volume_idx == i)
            continue;

        ModelVolume *mv = mo->volumes[i];
        if (mv->is_model_part()) {
            if (mesh_index == m_rr.mesh_id) {
                volume_index = i;
            }
            TriangleMesh vol_mesh(mv->mesh());
            vol_mesh.transform(mv->get_matrix());
            slice_meshs.merge(vol_mesh);
            mesh_index++;
        }
    }

    ModelVolume* volume = mo->volumes[volume_index];

    generate_text_tran_in_world(m_text_normal_in_world.cast<double>(), m_text_position_in_world, m_rotate_angle, m_text_tran_in_world);

    m_cut_plane_dir_in_world = m_text_tran_in_world.get_matrix().linear().col(1);
    m_text_normal_in_world   = m_text_tran_in_world.get_matrix().linear().col(2).cast<float>();
    // generate clip cs at click pos
    auto text_position_in_object = mi->get_transformation().get_matrix().inverse() * m_text_position_in_world.cast<double>();
    auto rotate_mat_inv          = mi->get_transformation().get_matrix_no_offset().inverse();

    auto text_tran_in_object = mi->get_transformation().get_matrix().inverse() * m_text_tran_in_world.get_matrix(); // Geometry::generate_transform(cs_x_dir, cs_y_dir, cs_z_dir, text_position_in_object); // todo modify by m_text_tran_in_world
    m_text_tran_in_object.set_matrix(text_tran_in_object);
    m_text_cs_to_world_tran = mi->get_transformation().get_matrix() * m_text_tran_in_object.get_matrix();
    auto rotate_tran          = Geometry::assemble_transform(Vec3d::Zero(), {-0.5 * M_PI, 0.0, 0.0});
    if (m_text_type == TextType::HORIZONAL || (m_need_fix && m_use_current_pose)) {
        m_position_points.resize(text_num);
        m_normal_points.resize(text_num);

        Vec3d pos_dir = m_cut_plane_dir_in_world.cross(mouse_normal_world);
        pos_dir.normalize();
        if (text_num % 2 == 1) {
            m_position_points[text_num / 2] = mouse_position_world;
            for (int i = 0; i < text_num / 2; ++i) {
                double left_gap = text_lengths[text_num / 2 - i - 1] + m_text_gap + text_lengths[text_num / 2 - i];
                if (left_gap < 0)
                    left_gap = 0;

                double right_gap = text_lengths[text_num / 2 + i + 1] + m_text_gap + text_lengths[text_num / 2 + i];
                if (right_gap < 0)
                    right_gap = 0;

                m_position_points[text_num / 2 - 1 - i] = m_position_points[text_num / 2 - i] - left_gap * pos_dir;
                m_position_points[text_num / 2 + 1 + i] = m_position_points[text_num / 2 + i] + right_gap * pos_dir;
            }
        } else {
            for (int i = 0; i < text_num / 2; ++i) {
                double left_gap = i == 0 ? (text_lengths[text_num / 2 - i - 1] + m_text_gap / 2) :
                    (text_lengths[text_num / 2 - i - 1] + m_text_gap + text_lengths[text_num / 2 - i]);
                if (left_gap < 0)
                    left_gap = 0;

                double right_gap = i == 0 ? (text_lengths[text_num / 2 + i] + m_text_gap / 2) :
                (text_lengths[text_num / 2 + i] + m_text_gap + text_lengths[text_num / 2 + i - 1]);
                if (right_gap < 0)
                    right_gap = 0;

                if (i == 0) {
                    m_position_points[text_num / 2 - 1 - i] = mouse_position_world - left_gap * pos_dir;
                    m_position_points[text_num / 2 + i]     = mouse_position_world + right_gap * pos_dir;
                    continue;
                }

                m_position_points[text_num / 2 - 1 - i] = m_position_points[text_num / 2 - i] - left_gap * pos_dir;
                m_position_points[text_num / 2 + i]     = m_position_points[text_num / 2 + i - 1] + right_gap * pos_dir;
            }
        }

        for (int i = 0; i < text_num; ++i) {
            m_normal_points[i] = mouse_normal_world;
        }

        return true;
    }

    MeshSlicingParams slicing_params;
    auto cut_tran    = (m_text_tran_in_object.get_matrix() * rotate_tran);
    slicing_params.trafo  = cut_tran.inverse();
    // for debug
    // its_write_obj(slice_meshs.its, "D:/debug_files/mesh.obj");
    // generate polygons
    const Polygons temp_polys = slice_mesh(slice_meshs.its, 0, slicing_params);
    Vec3d          scale_click_pt(scale_(0), scale_(0), 0);
    // for debug
    // export_regions_to_svg(Point(scale_pt.x(), scale_pt.y()), temp_polys);
    Polygons polys = union_(temp_polys);

    auto point_in_line_rectange = [](const Line &line, const Point &point, double& distance) {
        distance = line.distance_to(point);
        return distance < line.length() / 2;
    };

    int            index     = 0;
    double  min_distance = 1e12;
    Polygon        hit_ploy;
    for (const Polygon poly : polys) {
        if (poly.points.size() == 0)
            continue;

        Lines lines = poly.lines();
        for (int i = 0; i < lines.size(); ++i) {
            Line line = lines[i];
            double distance = min_distance;
            if (point_in_line_rectange(line, Point(scale_click_pt.x(), scale_click_pt.y()), distance)) {
                if (distance < min_distance) {
                    min_distance = distance;
                    index = i;
                    hit_ploy = poly;
                }
            }
        }
    }

    if (hit_ploy.points.size() == 0) {
        BOOST_LOG_TRIVIAL(info) << boost::format("Text: the hit polygon is null");
        return false;
    }

    m_cut_points_in_world.clear();
    m_cut_points_in_world.reserve(hit_ploy.points.size());
    for (int i = 0; i < hit_ploy.points.size(); ++i) {
        m_cut_points_in_world.emplace_back(m_text_cs_to_world_tran * rotate_tran * Vec3d(unscale_(hit_ploy.points[i].x()), unscale_(hit_ploy.points[i].y()), 0));
    }

    Polygon_3D new_polygon(m_cut_points_in_world);
    m_position_points.resize(text_num);
    if (text_num % 2 == 1) {
        m_position_points[text_num / 2] = Vec3d(mouse_position_world.x(), mouse_position_world.y(), mouse_position_world.z());

        std::vector<Line_3D>  lines       = new_polygon.get_lines();
        Line_3D   line        = lines[index];
        {
            int    index1      = index;
            double left_length = (mouse_position_world - line.a).cast<double>().norm();
            int    left_num    = text_num / 2;
            while (left_num > 0) {
                double gap_length = (text_lengths[left_num] + m_text_gap + text_lengths[left_num - 1]);
                if (gap_length < 0)
                    gap_length = 0;

                while (gap_length > left_length) {
                    gap_length -= left_length;
                    if (index1 == 0)
                        index1 = lines.size() - 1;
                    else
                        --index1;
                    left_length = lines[index1].length();
                }

                Vec3d direction = lines[index1].vector();
                direction.normalize();
                double distance_to_a = (left_length - gap_length);
                Line_3D   new_line      = lines[index1];

                double norm_value = direction.cast<double>().norm();
                double deta_x     = distance_to_a * direction.x() / norm_value;
                double deta_y     = distance_to_a * direction.y() / norm_value;
                double deta_z     = distance_to_a * direction.z() / norm_value;
                Vec3d  new_pos    = new_line.a + Vec3d(deta_x, deta_y, deta_z);
                left_num--;
                m_position_points[left_num] = new_pos;
                left_length                 = distance_to_a;
            }
        }

        {
            int    index2       = index;
            double right_length = (line.b - mouse_position_world).cast<double>().norm();
            int    right_num    = text_num / 2;
            while (right_num > 0) {
                double gap_length = (text_lengths[text_num - right_num] + m_text_gap + text_lengths[text_num - right_num - 1]);
                if (gap_length < 0)
                    gap_length = 0;

                while (gap_length > right_length) {
                    gap_length -= right_length;
                    if (index2 == lines.size() - 1)
                        index2 = 0;
                    else
                        ++index2;
                    right_length = lines[index2].length();
                }

                Line_3D line2 = lines[index2];
                line2.reverse();
                Vec3d direction = line2.vector();
                direction.normalize();
                double distance_to_b = (right_length - gap_length);
                Line_3D new_line         = lines[index2];

                double norm_value = direction.cast<double>().norm();
                double deta_x     = distance_to_b * direction.x() / norm_value;
                double deta_y     = distance_to_b * direction.y() / norm_value;
                double deta_z     = distance_to_b * direction.z() / norm_value;
                Vec3d new_pos = new_line.b + Vec3d(deta_x, deta_y, deta_z);
                m_position_points[text_num - right_num] = new_pos;
                right_length                            = distance_to_b;
                right_num--;
            }
        }
    }
    else {
        for (int i = 0; i < text_num / 2; ++i) {
            std::vector<Line_3D> lines = new_polygon.get_lines();
            Line_3D              line  = lines[index];
            {
                int    index1      = index;
                double left_length = (mouse_position_world - line.a).cast<double>().norm();
                int    left_num    = text_num / 2;
                for (int i = 0; i < text_num / 2; ++i) {
                    double gap_length = 0;
                    if (i == 0) {
                        gap_length = m_text_gap / 2 + text_lengths[text_num / 2 - 1 - i];
                    }
                    else {
                        gap_length = text_lengths[text_num / 2 - i] + m_text_gap + text_lengths[text_num / 2 - 1 - i];
                    }
                    if (gap_length < 0)
                        gap_length = 0;

                    while (gap_length > left_length) {
                        gap_length -= left_length;
                        if (index1 == 0)
                            index1 = lines.size() - 1;
                        else
                            --index1;
                        left_length = lines[index1].length();
                    }

                    Vec3d direction = lines[index1].vector();
                    direction.normalize();
                    double distance_to_a = (left_length - gap_length);
                    Line_3D   new_line      = lines[index1];

                    double norm_value = direction.cast<double>().norm();
                    double deta_x     = distance_to_a * direction.x() / norm_value;
                    double deta_y     = distance_to_a * direction.y() / norm_value;
                    double deta_z     = distance_to_a * direction.z() / norm_value;
                    Vec3d  new_pos    = new_line.a + Vec3d(deta_x, deta_y,deta_z);

                    m_position_points[text_num / 2 - 1 - i] = new_pos;
                    left_length                         = distance_to_a;
                }
            }

            {
                int    index2       = index;
                double right_length = (line.b - mouse_position_world).cast<double>().norm();
                int    right_num    = text_num / 2;
                double gap_length   = 0;
                for (int i = 0; i < text_num / 2; ++i) {
                    double gap_length = 0;
                    if (i == 0) {
                        gap_length = m_text_gap / 2 + text_lengths[text_num / 2 + i];
                    } else {
                        gap_length = text_lengths[text_num / 2 + i] + m_text_gap + text_lengths[text_num / 2 + i - 1];
                    }
                    if (gap_length < 0)
                        gap_length = 0;

                    while (gap_length > right_length) {
                        gap_length -= right_length;
                        if (index2 == lines.size() - 1)
                            index2 = 0;
                        else
                            ++index2;
                        right_length = lines[index2].length();
                    }

                    Line_3D line2 = lines[index2];
                    line2.reverse();
                    Vec3d direction = line2.vector();
                    direction.normalize();
                    double distance_to_b = (right_length - gap_length);
                    Line_3D   new_line      = lines[index2];

                    double norm_value                       = direction.cast<double>().norm();
                    double deta_x                           = distance_to_b * direction.x() / norm_value;
                    double deta_y                           = distance_to_b * direction.y() / norm_value;
                    double deta_z                           = distance_to_b * direction.z() / norm_value;
                    Vec3d  new_pos                          = new_line.b + Vec3d(deta_x, deta_y, deta_z);
                    m_position_points[text_num / 2 + i]     = new_pos;
                    right_length                            = distance_to_b;
                }
            }
        }
    }

    TriangleMesh mesh       = slice_meshs;
    std::vector<double> mesh_values(m_position_points.size(), 1e9);
    m_normal_points.resize(m_position_points.size());
    auto point_in_triangle_delete_area = [](const Vec3d &point, const Vec3d &point0, const Vec3d &point1, const Vec3d &point2) {
        Vec3d p0_p  = point - point0;
        Vec3d p0_p1 = point1 - point0;
        Vec3d p0_p2 = point2 - point0;
        Vec3d p_p0  = point0 - point;
        Vec3d p_p1  = point1 - point;
        Vec3d p_p2  = point2 - point;

        double s  = p0_p1.cross(p0_p2).norm();
        double s0 = p_p0.cross(p_p1).norm();
        double s1 = p_p1.cross(p_p2).norm();
        double s2 = p_p2.cross(p_p0).norm();

        return abs(s0 + s1 + s2 - s);
    };
    bool is_mirrored =m_model_object_in_world_tran.is_left_handed();
    for (int i = 0; i < m_position_points.size(); ++i) {
        for (auto indice : mesh.its.indices) {
            stl_vertex stl_point0 = mesh.its.vertices[indice[0]];
            stl_vertex stl_point1 = mesh.its.vertices[indice[1]];
            stl_vertex stl_point2 = mesh.its.vertices[indice[2]];

            Vec3d point0 = Vec3d(stl_point0[0], stl_point0[1], stl_point0[2]);
            Vec3d point1 = Vec3d(stl_point1[0], stl_point1[1], stl_point1[2]);
            Vec3d point2 = Vec3d(stl_point2[0], stl_point2[1], stl_point2[2]);

            point0 = mi->get_transformation().get_matrix() * point0;
            point1 = mi->get_transformation().get_matrix() * point1;
            point2 = mi->get_transformation().get_matrix() * point2;

            double abs_area = point_in_triangle_delete_area(m_position_points[i], point0, point1, point2);
            if (mesh_values[i] > abs_area) {
                mesh_values[i] = abs_area;

                Vec3d s1           = point1 - point0;
                Vec3d s2           = point2 - point0;
                m_normal_points[i] = s1.cross(s2);
                m_normal_points[i].normalize();
                if(is_mirrored){
                    m_normal_points[i] = -m_normal_points[i];
                }
            }
        }
    }
    return true;
}

TriangleMesh GLGizmoText::get_text_mesh(int i, const char *text_str, const Vec3d &position, const Vec3d &normal, const Vec3d &text_up_dir)
{
    if (m_chars_mesh_result.size() == 0) {
#ifdef _WIN32
        __debugbreak();
#endif
    }
    TriangleMesh mesh = m_chars_mesh_result[i];//m_cur_font_name
    auto         box  = mesh.bounding_box();
    mesh.translate(-box.center().x(), 0, 0);
    auto     box_look = mesh.bounding_box();
    double   phi;
    Vec3d    rotation_axis;
    Matrix3d rotation_matrix;
    Geometry::rotation_from_two_vectors(Vec3d::UnitZ(), normal, rotation_axis, phi, &rotation_matrix);
    mesh.rotate(phi, rotation_axis);

    auto project_on_plane = [](const Vec3d& dir, const Vec3d& plane_normal) -> Vec3d {
        return dir - (plane_normal.dot(dir) * plane_normal.dot(plane_normal)) * plane_normal;
    };

    Vec3d old_text_dir = Vec3d::UnitY();
    old_text_dir = rotation_matrix * old_text_dir;
    Vec3d new_text_dir = project_on_plane(text_up_dir, normal);
    new_text_dir.normalize();
    Geometry::rotation_from_two_vectors(old_text_dir, new_text_dir, rotation_axis, phi, &rotation_matrix);

    if (abs(phi - PI) < EPSILON)
        rotation_axis = normal;

    mesh.rotate(phi, rotation_axis);

    Vec3d offset = position - m_embeded_depth * normal;
    mesh.translate(offset.x(), offset.y(), offset.z());

    return mesh;//mesh in object cs
}

bool GLGizmoText::update_raycast_cache(const Vec2d &mouse_position, const Camera &camera, const std::vector<Transform3d> &trafo_matrices)
{
    if (m_rr.mouse_position == mouse_position) {
        return false;
    }

    if (m_is_modify)
        return false;

    Vec3f  normal                       = Vec3f::Zero();
    Vec3f  hit                          = Vec3f::Zero();
    size_t facet                        = 0;
    Vec3f  closest_hit                  = Vec3f::Zero();
    Vec3f  closest_nromal               = Vec3f::Zero();
    double closest_hit_squared_distance = std::numeric_limits<double>::max();
    int    closest_hit_mesh_id          = -1;

    // Cast a ray on all meshes, pick the closest hit and save it for the respective mesh
    for (int mesh_id = 0; mesh_id < int(trafo_matrices.size()); ++mesh_id) {
        if (m_preview_text_volume_id != -1 && mesh_id == int(trafo_matrices.size()) - 1)
            continue;

        if (m_c->raycaster()->raycasters()[mesh_id]->unproject_on_mesh(mouse_position, trafo_matrices[mesh_id], camera, hit, normal, m_c->object_clipper()->get_clipping_plane(),
                                                                       &facet)) {
            // In case this hit is clipped, skip it.
            if (is_mesh_point_clipped(hit.cast<double>(), trafo_matrices[mesh_id]))
                continue;

            double hit_squared_distance = (camera.get_position() - trafo_matrices[mesh_id] * hit.cast<double>()).squaredNorm();
            if (hit_squared_distance < closest_hit_squared_distance) {
                closest_hit_squared_distance = hit_squared_distance;
                closest_hit_mesh_id          = mesh_id;
                closest_hit                  = hit;
                closest_nromal               = normal;
            }
        }
    }

    m_rr = {mouse_position, closest_hit_mesh_id, closest_hit, closest_nromal};//update_raycast_cache berfor click down
    return true;
}

void GLGizmoText::generate_text_volume(bool is_temp)
{
    std::string text = std::string(m_text);
    if (text.empty())
        return;

    std::wstring_convert<std::codecvt_utf8<wchar_t>> str_cnv;
    std::wstring ws = boost::nowide::widen(m_text);
    std::vector<std::string> alphas;
    for (auto w : ws) {
        alphas.push_back(str_cnv.to_bytes(w));
    }

    update_text_positions(alphas);//update m_model_object_in_world_tran

    if (m_position_points.size() == 0)
        return;
    auto  inv_text_cs_in_object           = (m_model_object_in_world_tran.get_matrix() * m_text_tran_in_object.get_matrix()).inverse();
    auto         inv_text_cs_in_object_no_offset = (m_model_object_in_world_tran.get_matrix_no_offset() * m_text_tran_in_object.get_matrix_no_offset()).inverse();
    TriangleMesh mesh;
    for (int i = 0; i < m_position_points.size(); ++i) {
        auto         position      = inv_text_cs_in_object * m_position_points[i];
        auto         normal        = inv_text_cs_in_object_no_offset * m_normal_points[i];
        auto         cut_plane_dir = inv_text_cs_in_object_no_offset * m_cut_plane_dir_in_world;
        TriangleMesh sub_mesh      = get_text_mesh(i,alphas[i].c_str(), position, normal, cut_plane_dir);
        mesh.merge(sub_mesh);
    }
    if (mesh.empty())
        return;
    auto center = mesh.bounding_box().center();
    if (abs(mesh.bounding_box().size()[2] - (m_embeded_depth + m_thickness)) < 0.01) {
        mesh.translate(Vec3f(-center.x(), -center.y(), 0)); // align horizontal and vertical center
    }
    else {
        mesh.translate(Vec3f(0, -center.y(), 0)); // align vertical center
    }

    Plater *plater = wxGetApp().plater();
    if (!plater)
        return;

    TextInfo text_info = get_text_info();
    const Selection &selection    = m_parent.get_selection();
    ModelObject *    model_object = selection.get_model()->objects[m_object_idx];
    int             cur_volume_id;
    if (m_is_modify && m_need_update_text) {
        if (m_object_idx == -1 || m_volume_idx == -1) {
            BOOST_LOG_TRIVIAL(error) << boost::format("Text: selected object_idx = %1%, volume_idx = %2%") % m_object_idx % m_volume_idx;
            return;
        }
        if (!is_temp) {
            plater->take_snapshot("Modify Text");
        }
        text_info.m_font_version = CUR_FONT_VERSION;
        ModelVolume *    model_volume     = model_object->volumes[m_volume_idx];
        ModelVolume *    new_model_volume = model_object->add_volume(std::move(mesh),false);
        if (m_need_fix && // m_reedit_text//m_need_fix
            (m_text_type == TextType::HORIZONAL || (m_text_type > TextType::HORIZONAL && m_use_current_pose))) {
            new_model_volume->set_transformation(m_load_text_tran_in_object.get_matrix());
        }
        else {
            new_model_volume->set_transformation(m_text_tran_in_object.get_matrix());
        }
        new_model_volume->set_text_info(text_info);
        new_model_volume->name = model_volume->name;
        new_model_volume->set_type(model_volume->type());
        new_model_volume->config.apply(model_volume->config);
        std::swap(model_object->volumes[m_volume_idx], model_object->volumes.back());
        model_object->delete_volume(model_object->volumes.size() - 1);
        model_object->invalidate_bounding_box();
        plater->update();
        m_text_volume_tran = new_model_volume->get_matrix();
    } else {
        if (!is_temp && m_need_update_text)
            plater->take_snapshot("Add Text");
        ObjectList *obj_list = wxGetApp().obj_list();
        cur_volume_id                   = obj_list->add_text_part(mesh, "text_shape", text_info, m_text_tran_in_object.get_matrix(), is_temp);
        m_preview_text_volume_id       = is_temp ? cur_volume_id : -1;
        if (cur_volume_id >= 0) {
            m_show_warning_text_create_fail = false;
            ModelVolume *text_model_volume = model_object->volumes[cur_volume_id];
            m_text_volume_tran             = text_model_volume->get_matrix();
            m_data_update.base->write(*text_model_volume);//first save text_configuration
        }
        else {
            m_show_warning_text_create_fail = true;
        }
    }
    m_need_update_text = false;
    m_show_warning_lost_rotate = false;
}

void GLGizmoText::delete_temp_preview_text_volume()
{
    const Selection &selection = m_parent.get_selection();
    if (m_preview_text_volume_id > 0) {
        ModelObject *model_object = selection.get_model()->objects[m_object_idx];
        if (m_preview_text_volume_id < model_object->volumes.size()) {
            Plater *plater = wxGetApp().plater();
            if (!plater)
                return;

            model_object->delete_volume(m_preview_text_volume_id);

            plater->update();
        }
        m_preview_text_volume_id = -1;
    }
}

TextInfo GLGizmoText::get_text_info()
{
    TextInfo text_info;
    const wxFont &wx_font             = m_style_manager.get_wx_font();
    if (wx_font.IsOk()) {
        auto actual_face_name = wx_font.GetFaceName();
        m_cur_font_name  = actual_face_name;
        m_font_name      = actual_face_name.utf8_string();
    }
    text_info.m_font_name     = m_font_name;
    text_info.m_font_version  = m_font_version;
    m_font_size               = m_style_manager.get_font_prop().size_in_mm;
    text_info.m_font_size     = m_font_size;
    text_info.m_curr_font_idx = m_curr_font_idx;
    m_bold                    = m_style_manager.get_font_prop().boldness > 0;
    m_italic                  = m_style_manager.get_font_prop().skew > 0;
    text_info.m_bold          = m_bold;
    text_info.m_italic        = m_italic;

    text_info.m_thickness     = m_thickness;
    text_info.m_embeded_depth = m_embeded_depth;

    text_info.m_text          = m_text;
    text_info.m_rr.mesh_id    = m_rr.mesh_id;

    text_info.m_rotate_angle  = m_rotate_angle;
    text_info.m_text_gap      = m_text_gap;
    text_info.m_is_surface_text = m_text_type == TextType::SURFACE || m_text_type == TextType::SURFACE_HORIZONAL;
    text_info.m_keep_horizontal = m_text_type == TextType::HORIZONAL || m_text_type == TextType::SURFACE_HORIZONAL;
    return text_info;
}

void GLGizmoText::load_from_text_info(const TextInfo &text_info)
{
    m_font_name     = text_info.m_font_name;
    m_font_version = text_info.m_font_version;
    m_font_size     = text_info.m_font_size;
    // from other user's computer may exist case:font library size is different
    if (text_info.m_curr_font_idx < m_font_names.size()) {
        m_curr_font_idx = text_info.m_curr_font_idx;
    }
    else {
        m_curr_font_idx = 0;
    }
    m_bold          = text_info.m_bold;
    m_italic        = text_info.m_italic;
    m_thickness     = text_info.m_thickness;
    bool is_text_changed = m_text != text_info.m_text;
    m_text = text_info.m_text;
    m_rr.mesh_id    = text_info.m_rr.mesh_id;
    m_embeded_depth = text_info.m_embeded_depth;
    m_rotate_angle  = text_info.m_rotate_angle;
    m_text_gap      = text_info.m_text_gap;
    check_text_type(text_info.m_is_surface_text, text_info.m_keep_horizontal);

    bool is_compatible = m_font_version.empty();
    if (!m_font_version.empty()) {
        float version = std::atof(m_font_version.c_str());
        if (version < 1.1) {
            is_compatible = true;
        }
    }
    update_boldness();
    update_italic();
    if (is_compatible) { // compatible with older versions
        load_old_font();
    }
    else {
        m_style_manager.get_font_prop().size_in_mm = m_font_size;//m_font_size
        wxString font_name = wxString(m_font_name.c_str(), wxConvUTF8);
        select_facename(font_name,false);
    }
    if (is_text_changed) {
        process(true,std::nullopt,false);
    }
}

std::string concat(std::vector<wxString> data)
{
    std::stringstream ss;
    for (const auto &d : data) ss << d.c_str() << ", ";
    return ss.str();
}
boost::filesystem::path get_fontlist_cache_path() {
    return boost::filesystem::path(data_dir()) / "cache" / "fonts.cereal";
}
bool store(const CurFacenames &facenames)
{
    std::string             cache_path = get_fontlist_cache_path().string();
    boost::filesystem::path path(cache_path);
    if (!boost::filesystem::exists(path.parent_path())) { boost::filesystem::create_directory(path.parent_path()); }
    boost::nowide::ofstream       file(cache_path, std::ios::binary);
    ::cereal::BinaryOutputArchive archive(file);
    std::vector<wxString>         good;
    good.reserve(facenames.faces.size());
    for (const FaceName &face : facenames.faces) good.push_back(face.wx_name);
    FacenamesSerializer data = {facenames.hash, good, facenames.bad};

    assert(std::is_sorted(data.bad.begin(), data.bad.end()));
    assert(std::is_sorted(data.good.begin(), data.good.end()));
    try {
        archive(data);
    } catch (const std::exception &ex) {
        BOOST_LOG_TRIVIAL(error) << "Failed to write fontlist cache - " << cache_path << ex.what();
        return false;
    }
    return true;
}
bool load(CurFacenames &facenames)
{
    boost::filesystem::path path     = get_fontlist_cache_path();
    std::string             path_str = path.string();
    if (!boost::filesystem::exists(path)) {
        BOOST_LOG_TRIVIAL(warning) << "Fontlist cache - '" << path_str << "' does not exists.";
        return false;
    }
    boost::nowide::ifstream      file(path_str, std::ios::binary);
    ::cereal::BinaryInputArchive archive(file);

    FacenamesSerializer data;
    try {
        archive(data);
    } catch (const std::exception &ex) {
        BOOST_LOG_TRIVIAL(error) << "Failed to load fontlist cache - '" << path_str << "'. Exception: " << ex.what();
        return false;
    }

    assert(std::is_sorted(data.bad.begin(), data.bad.end()));
    assert(std::is_sorted(data.good.begin(), data.good.end()));

    facenames.hash = data.hash;
    facenames.faces.reserve(data.good.size());
    for (const wxString &face : data.good) facenames.faces.push_back({face});
    facenames.bad = data.bad;

    return true;
}

// validation lambda
bool is_valid_font(const wxString &name, wxFontEncoding encoding, std::vector<wxString> bad ) // face_names.encoding , face_names.bad
{
    if (name.empty())
        return false;

    // vertical font start with @, we will filter it out
    // Not sure if it is only in Windows so filtering is on all platforms
    if (name[0] == '@') return false;

    // previously detected bad font
    auto it = std::lower_bound(bad.begin(), bad.end(), name);
    if (it != bad.end() && *it == name) return false;

    wxFont wx_font(wxFontInfo().FaceName(name).Encoding(encoding));
    //*
    // Faster chech if wx_font is loadable but not 100%
    // names could contain not loadable font
    if (!WxFontUtils::can_load(wx_font)) return false;

    /*/
    // Slow copy of font files to try load font
    // After this all files are loadable
    auto font_file = WxFontUtils::create_font_file(wx_font);
    if (font_file == nullptr)
        return false; // can't create font file
    // */
    return true;
}

bool draw_button(const IconManager::VIcons &icons, IconType type, bool disable)
{
    return Slic3r::GUI::button(get_icon(icons, type, IconState::activable), get_icon(icons, type, IconState::hovered), get_icon(icons, type, IconState::disabled), disable);
}

void init_face_names(CurFacenames &face_names)
{
    Timer t("enumerate_fonts");
    if (face_names.is_init) return;
    face_names.is_init = true;

    // to reload fonts from system, when install new one
    wxFontEnumerator::InvalidateCache();

    // try load cache
    // Only not OS enumerated face has hash value 0
    if (face_names.hash == 0) {
        load(face_names);
        face_names.has_truncated_names = false;
    }

    using namespace std::chrono;
    steady_clock::time_point enumerate_start = steady_clock::now();
    ScopeGuard               sg([&enumerate_start, &face_names = face_names]() {
        steady_clock::time_point enumerate_end      = steady_clock::now();
        long long                enumerate_duration = duration_cast<milliseconds>(enumerate_end - enumerate_start).count();
        BOOST_LOG_TRIVIAL(info) << "OS enumerate " << face_names.faces.size() << " fonts "
                                << "(+ " << face_names.bad.size() << " can't load "
                                << "= " << face_names.faces.size() + face_names.bad.size() << " fonts) "
                                << "in " << enumerate_duration << " ms\n"
                                << concat(face_names.bad);
    });
    wxArrayString            facenames = wxFontEnumerator::GetFacenames(face_names.encoding);
    size_t                   hash      = boost::hash_range(facenames.begin(), facenames.end());
    // Zero value is used as uninitialized hash
    if (hash == 0) hash = 1;
    // check if it is same as last time
    if (face_names.hash == hash) {
        if (face_names.faces_names.size() == 0) {//FIX by bbs
            face_names.faces_names.clear();
            face_names.faces_names.reserve(face_names.faces.size());
            for (size_t i = 0; i < face_names.faces.size(); i++) {
                auto cur = face_names.faces[i].wx_name.ToUTF8().data();
                face_names.faces_names.push_back(cur);
            }
        }
        // no new installed font
        BOOST_LOG_TRIVIAL(info) << "Same FontNames hash, cache is used. "
                                << "For clear cache delete file: " << get_fontlist_cache_path().string();
        return;
    }

    BOOST_LOG_TRIVIAL(info) << ((face_names.hash == 0) ? "FontName list is generate from scratch." : "Hash are different. Only previous bad fonts are used and set again as bad");
    face_names.hash = hash;

    face_names.faces.clear();
    face_names.faces_names.clear();
    face_names.bad.clear();
    face_names.faces.reserve(facenames.size());
    face_names.faces_names.reserve(facenames.size());
    std::sort(facenames.begin(), facenames.end());
    for (const wxString& name : facenames) {
        if (is_valid_font(name, face_names.encoding, face_names.bad)) {
            face_names.faces.push_back({name});
            face_names.faces_names.push_back(name.utf8_string());
        } else {
            face_names.bad.push_back(name);
        }
    }
    assert(std::is_sorted(face_names.bad.begin(), face_names.bad.end()));
    face_names.has_truncated_names = false;
    store(face_names);
}
void init_truncated_names(CurFacenames& face_names, float max_width) {
    for (FaceName &face : face_names.faces) {
        std::string name_str(face.wx_name.ToUTF8().data());
        face.name_truncated = ImGuiWrapper::trunc(name_str, max_width);
    }
    face_names.has_truncated_names = true;
}

std::optional<wxString> get_installed_face_name(const std::optional<std::string>& face_name_opt, CurFacenames& face_names) {
    // Could exist OS without getter on face_name,
    // but it is able to restore font from descriptor
    // Soo default value must be TRUE
    if (!face_name_opt.has_value())
        return wxString();

    wxString face_name = wxString::FromUTF8(face_name_opt->c_str());

    // search in enumerated fonts
    // refresh list of installed font in the OS.
    init_face_names(face_names);
    face_names.is_init = false;

    auto                         cmp   = [](const FaceName &fn, const wxString &wx_name) { return fn.wx_name < wx_name; };
    const std::vector<FaceName> &faces = face_names.faces;
    // is font installed?
    if (auto it = std::lower_bound(faces.begin(), faces.end(), face_name, cmp); it != faces.end() && it->wx_name == face_name) return face_name;

    const std::vector<wxString> &bad    = face_names.bad;
    auto                         it_bad = std::lower_bound(bad.begin(), bad.end(), face_name);
    if (it_bad == bad.end() || *it_bad != face_name) {
        // check if wx allowed to set it up - another encoding of name
        wxFontEnumerator::InvalidateCache();
        wxFont wx_font_;                                                                          // temporary structure
        if (wx_font_.SetFaceName(face_name) && WxFontUtils::create_font_file(wx_font_) != nullptr // can load TTF file?
        ) {
            return wxString();
            // QUESTION: add this name to allowed faces?
            // Could create twin of font face name
            // When not add it will be hard to select it again when change font
        }
    }
    return {}; // not installed
}

void draw_font_preview(FaceName& face, const std::string& text, CurFacenames& faces, const CurGuiCfg& cfg, bool is_visible)
{
    // Limit for opened font files at one moment
    unsigned int &count_opened_fonts = faces.count_opened_font_files;
    // Size of texture
    ImVec2      size(cfg.face_name_size.x(), cfg.face_name_size.y());
    float       count_cached_textures_f = static_cast<float>(faces.count_cached_textures);
    std::string state_text;
    // uv0 and uv1 set to pixel 0,0 in texture
    ImVec2 uv0(0.f, 0.f), uv1(1.f / size.x, 1.f / size.y / count_cached_textures_f);
    if (face.is_created != nullptr) {
        // not created preview
        if (*face.is_created) {
            // Already created preview
            size_t texture_index = face.texture_index;
            uv0                  = ImVec2(0.f, texture_index / count_cached_textures_f);
            uv1                  = ImVec2(1.f, (texture_index + 1) / count_cached_textures_f);
        } else {
            // Not finished preview
            if (is_visible) {
                // when not canceled still loading
                state_text = (face.cancel->load()) ? " " + _u8L("No symbol") : " ... " + _u8L("Loading");
            } else {
                // not finished and not visible cancel job
                face.is_created = nullptr;
                face.cancel->store(true);
            }
        }
    } else if (is_visible && count_opened_fonts < cfg.max_count_opened_font_files) {
        ++count_opened_fonts;
        face.cancel     = std::make_shared<std::atomic_bool>(false);
        face.is_created = std::make_shared<bool>(false);

        const unsigned char gray_level = 5;
        // format type and level must match to texture data
        const GLenum format = GL_RGBA, type = GL_UNSIGNED_BYTE;
        const GLint  level = 0;
        // select next texture index
        size_t texture_index = (faces.texture_index + 1) % faces.count_cached_textures;

        // set previous cach as deleted
        for (FaceName &f : faces.faces)
            if (f.texture_index == texture_index) {
                if (f.cancel != nullptr) f.cancel->store(true);
                f.is_created = nullptr;
            }

        faces.texture_index = texture_index;
        face.texture_index  = texture_index;

        // render text to texture
        FontImageData data{
            text,           face.wx_name, faces.encoding, faces.texture_id, faces.texture_index, cfg.face_name_size, gray_level, format, type, level, &count_opened_fonts,
            face.cancel,    // copy
            face.is_created // copy
        };
        auto  job    = std::make_unique<CreateFontImageJob>(std::move(data));
        auto &worker = wxGetApp().plater()->get_ui_job_worker();
        queue_job(worker, std::move(job));
    } else {
        // cant start new thread at this moment so wait in queue
        state_text = " ... " + _u8L("In queue");
    }

    if (!state_text.empty()) {
        ImGui::SameLine(cfg.face_name_texture_offset_x);
        ImGui::Text("%s", state_text.c_str());
    }

    ImGui::SameLine(cfg.face_name_texture_offset_x -10);
    ImTextureID tex_id     = (void *) (intptr_t) faces.texture_id;
    ImVec4      tint_color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    ImGui::Image(tex_id, size, uv0, uv1, tint_color);
}

void init_text_lines(TextLinesModel &text_lines, const Selection &selection, /* const*/ StyleManager &style_manager, unsigned count_lines)
{
    text_lines.reset();
    if (selection.is_empty())
        return;
    const GLVolume *gl_volume_ptr = selection.get_first_volume();
    if (gl_volume_ptr == nullptr) return;
    const GLVolume &       gl_volume = *gl_volume_ptr;
    const ModelObjectPtrs &objects   = selection.get_model()->objects;
    const ModelVolume *    mv_ptr    = get_model_volume(gl_volume, objects);
    if (mv_ptr == nullptr) return;
    const ModelVolume &mv = *mv_ptr;
    if (mv.is_the_only_one_part()) return;

    const std::optional<EmbossShape> &es_opt = mv.emboss_shape;
    if (!es_opt.has_value()) return;
    const EmbossShape &es = *es_opt;

    //const std::optional<TextConfiguration> &tc_opt = mv.text_configuration;
    //if (!tc_opt.has_value()) return;
    //const TextConfiguration &tc = *tc_opt;

    //// calculate count lines when not set
    //if (count_lines == 0) {
    //    count_lines = get_count_lines(tc.text);
    //    if (count_lines == 0) return;
    //}

    //// prepare volumes to slice
    //ModelVolumePtrs volumes = prepare_volumes_to_slice(mv);

    //// For interactivity during drag over surface it must be from gl_volume not volume.
    //Transform3d mv_trafo = gl_volume.get_volume_transformation().get_matrix();
    //if (es.fix_3mf_tr.has_value())
    //    mv_trafo = mv_trafo * (es.fix_3mf_tr->inverse());
    //text_lines.init(mv_trafo, volumes, style_manager, count_lines);
}

const IconManager::Icon &Text::get_icon(const IconManager::VIcons &icons, Text::IconType type, Text::IconState state) {
    return *icons[(unsigned) type][(unsigned) state];
}
CurGuiCfg Text::create_gui_configuration()
{
    CurGuiCfg cfg; // initialize by default values;

    float             line_height              = ImGui::GetTextLineHeight();
    float             line_height_with_spacing = ImGui::GetTextLineHeightWithSpacing();
    float             space                    = line_height_with_spacing - line_height;
    const ImGuiStyle &style                    = ImGui::GetStyle();

    cfg.max_style_name_width = ImGui::CalcTextSize("Maximal font name, extended").x;

    cfg.icon_width = static_cast<unsigned int>(std::ceil(line_height));
    // make size pair number
    if (cfg.icon_width % 2 != 0) ++cfg.icon_width;

    cfg.delete_pos_x                    = cfg.max_style_name_width + space;
    const float count_line_of_text      = 3.f;
    cfg.text_size                       = ImVec2(-FLT_MIN, line_height_with_spacing * count_line_of_text);
    ImVec2      letter_m_size           = ImGui::CalcTextSize("M");
    const float count_letter_M_in_input = 12.f;
    cfg.input_width                     = letter_m_size.x * count_letter_M_in_input;
    CurGuiCfg::Translations &tr         = cfg.translations;

    // TRN - Input label. Be short as possible
    // Select look of letter shape
    tr.font = _u8L("Font");
    // TRN - Input label. Be short as possible
    // Height of one text line - Font Ascent
    tr.height = _u8L("Height");
    // TRN - Input label. Be short as possible
    // Size in emboss direction
    tr.depth = _u8L("Depth");

    float max_text_width = std::max({ImGui::CalcTextSize(tr.font.c_str()).x, ImGui::CalcTextSize(tr.height.c_str()).x, ImGui::CalcTextSize(tr.depth.c_str()).x});
    cfg.indent           = static_cast<float>(cfg.icon_width);
    cfg.input_offset     = style.WindowPadding.x + cfg.indent + max_text_width + space;

    // TRN - Input label. Be short as possible
    // Copy surface of model on surface of the embossed text
    tr.use_surface = _u8L("Use surface");
    // TRN - Input label. Be short as possible
    // Option to change projection on curved surface
    // for each character(glyph) in text separately
    tr.per_glyph = _u8L("Per glyph");
    // TRN - Input label. Be short as possible
    // Align Top|Middle|Bottom and Left|Center|Right
    tr.alignment = _u8L("Alignment");
    // TRN - Input label. Be short as possible
    tr.char_gap = _u8L("Char gap");
    // TRN - Input label. Be short as possible
    tr.line_gap = _u8L("Line gap");
    // TRN - Input label. Be short as possible
    tr.boldness = _u8L("Boldness");

    // TRN - Input label. Be short as possible
    // Like Font italic
    tr.skew_ration = _u8L("Skew ratio");

    // TRN - Input label. Be short as possible
    // Distance from model surface to be able
    // move text as part fully into not flat surface
    // move text as modifier fully out of not flat surface
    tr.from_surface = _u8L("From surface");

    // TRN - Input label. Be short as possible
    // Angle between Y axis and text line direction.
    tr.rotation = _u8L("Rotation");

    // TRN - Input label. Be short as possible
    // Keep vector from bottom to top of text aligned with printer Y axis
    tr.keep_up = _u8L("Keep up");

    // TRN - Input label. Be short as possible.
    // Some Font file contain multiple fonts inside and
    // this is numerical selector of font inside font collections
    tr.collection = _u8L("Collection");

    float max_advanced_text_width = std::max({ImGui::CalcTextSize(tr.use_surface.c_str()).x, ImGui::CalcTextSize(tr.per_glyph.c_str()).x,
                                              ImGui::CalcTextSize(tr.alignment.c_str()).x, ImGui::CalcTextSize(tr.char_gap.c_str()).x, ImGui::CalcTextSize(tr.line_gap.c_str()).x,
                                              ImGui::CalcTextSize(tr.boldness.c_str()).x, ImGui::CalcTextSize(tr.skew_ration.c_str()).x,
                                              ImGui::CalcTextSize(tr.from_surface.c_str()).x, ImGui::CalcTextSize(tr.rotation.c_str()).x + cfg.icon_width + 2 * space,
                                              ImGui::CalcTextSize(tr.keep_up.c_str()).x, ImGui::CalcTextSize(tr.collection.c_str()).x});
    cfg.advanced_input_offset     = max_advanced_text_width + 3 * space + cfg.indent;

    cfg.lock_offset = cfg.advanced_input_offset - (cfg.icon_width + space);
    // calculate window size
    float input_height     = line_height_with_spacing + 2 * style.FramePadding.y;
    float separator_height = 2 + style.FramePadding.y;

    // "Text is to object" + radio buttons
    cfg.height_of_volume_type_selector = separator_height + line_height_with_spacing + input_height;

    int max_style_image_width      = static_cast<int>(std::round(cfg.max_style_name_width / 2 - 2 * style.FramePadding.x));
    int max_style_image_height     = static_cast<int>(std::round(input_height));
    cfg.max_style_image_size       = Vec2i32(max_style_image_width, line_height);
    cfg.face_name_size             = Vec2i32(cfg.input_width, line_height_with_spacing);
    cfg.face_name_texture_offset_x = cfg.face_name_size.x() + space;

    cfg.max_tooltip_width = ImGui::GetFontSize() * 20.0f;

    return cfg;
}

EmbossShape &TextDataBase::create_shape()
{
    if (!shape.shapes_with_ids.empty())
        return shape;
    // create shape by configuration
    const char *    text         = m_text_configuration.text.c_str();
    std::wstring    text_w       = boost::nowide::widen(text);
    const FontProp &fp           = m_text_configuration.style.prop;
    auto            was_canceled = [&c = cancel]() { return c->load(); };

    shape.shapes_with_ids = text2vshapes(m_font_file, text_w, fp, was_canceled);
    return shape;
}
} // namespace GUI
} // namespace Slic3r
