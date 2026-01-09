#ifndef slic3r_Format_OBJ_hpp_
#define slic3r_Format_OBJ_hpp_
#include "libslic3r/Color.hpp"
#include "objparser.hpp"
#include <unordered_map>
namespace Slic3r {

class TriangleMesh;
class Model;
class ModelObject;

// TriangleColorData structure：Stores the color binding information for each triangle in 3MF
struct TriangleColor {
    int pid{ -1 };           // Color group ID(property ID)
    int indices[3]{ -1, -1, -1 };  // The color indices of the three vertices (p1, p2, p3)
};

// Volume color information: Stores the triangle color binding data for each volume.
struct VolumeColorInfo {
    int pid{ -1 };
    int pindex{ -1 };
    std::vector<TriangleColor> triangle_colors;  // Color binding for each triangle
};

// Load an OBJ file into a provided model.
struct ObjInfo {
    std::vector<RGBA> vertex_colors;
    std::vector<RGBA> face_colors;
    std::vector<RGBA> mtl_colors;
    std::vector<std::string>   mtl_color_names;
    std::vector<ObjParser::ObjUseMtl>      usemtls; // for origin render
    bool              first_time_using_makerlab{false};
    bool              is_single_mtl{false};
    std::string       lost_material_name{""};
    std::vector<std::array<Vec2f,3>> uvs;
    std::string        obj_dircetory;
    std::map<std::string,bool>  pngs;
    std::unordered_map<int, std::string> uv_map_pngs;
    bool              has_uv_png{false};

    std::string ml_region;
    std::string	ml_name;
    std::string ml_id;
};
struct ObjDialogInOut
{ // input:colors array
    std::vector<RGBA> input_colors;
    std::vector<ObjParser::ObjUseMtl> usemtls; // for origin render
    bool              is_single_color{false};
    // colors array output:
    std::vector<unsigned char> filament_ids;
    unsigned char              first_extruder_id{1};
    bool                       deal_vertex_color;
    std::unordered_map<int, VolumeColorInfo> volume_colors;// Used when FormatType is Standard3mf
    std::unordered_map<int, std::vector<RGBA>> color_group_map; // Used when FormatType is Standard3mf
    Model *                    model{nullptr};
    std::vector<RGBA>          mtl_colors;
    std::vector<std::string>   mtl_color_names;
    bool                       first_time_using_makerlab{false};
    // ml
    std::string ml_region;
    std::string ml_name;
    std::string ml_id;
    std::string lost_material_name{""};
    //enum
    enum class FormatType {
        Obj,
        Standard3mf
    };
    FormatType input_type{FormatType::Obj};
    bool       exist_color_error{false};
    bool       exist_texture_error{false};
};
typedef std::function<void(ObjDialogInOut &in_out)> ObjImportColorFn;
extern bool load_obj(const char *path, TriangleMesh *mesh, ObjInfo &vertex_colors, std::string &message, bool gamma_correct =false);
extern bool load_obj(const char *path, Model *model, ObjInfo &vertex_colors, std::string &message, const char *object_name = nullptr, bool gamma_correct =false);

extern bool store_obj(const char *path, TriangleMesh *mesh);
extern bool store_obj(const char *path, ModelObject *model);
extern bool store_obj(const char *path, Model *model);

}; // namespace Slic3r

#endif /* slic3r_Format_OBJ_hpp_ */
