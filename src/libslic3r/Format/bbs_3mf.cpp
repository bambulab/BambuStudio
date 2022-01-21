#include "../libslic3r.h"
#include "../Exception.hpp"
#include "../Model.hpp"
#include "../Preset.hpp"
#include "../Utils.hpp"
#include "../LocalesUtils.hpp"
#include "../GCode.hpp"
#include "../Geometry.hpp"
#include "../GCode/ThumbnailData.hpp"
#include "../Semver.hpp"
#include "../Time.hpp"

#include "../I18N.hpp"

#include "bbs_3mf.hpp"

#include <limits>
#include <stdexcept>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/nowide/cstdio.hpp>
#include <boost/spirit/include/karma.hpp>
#include <boost/spirit/include/qi_int.hpp>
#include <boost/log/trivial.hpp>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/foreach.hpp>
namespace pt = boost::property_tree;

#include <expat.h>
#include <Eigen/Dense>
#include "miniz_extension.hpp"

#include <fast_float/fast_float.h>

// Slightly faster than sprintf("%.9g"), but there is an issue with the karma floating point formatter,
// https://github.com/boostorg/spirit/pull/586
// where the exported string is one digit shorter than it should be to guarantee lossless round trip.
// The code is left here for the ocasion boost guys improve.
#define EXPORT_3MF_USE_SPIRIT_KARMA_FP 0

// VERSION NUMBERS
// 0 : .3mf, files saved by older slic3r or other applications. No version definition in them.
// 1 : Introduction of 3mf versioning. No other change in data saved into 3mf files.
// 2 : Volumes' matrices and source data added to Metadata/Slic3r_PE_model.config file, meshes transformed back to their coordinate system on loading.
// WARNING !! -> the version number has been rolled back to 1
//               the next change should use 3
const unsigned int VERSION_BBS_3MF = 1;
// Allow loading version 2 file as well.
const unsigned int VERSION_BBS_3MF_COMPATIBLE = 2;
const char* BBS_3MF_VERSION = "bamboo_slicer:Version3mf"; // definition of the metadata name saved into .model file
const char* BBS_PRUSA_VERSION = "slic3rpe:Version3mf"; //compatible with prusa currently
// Painting gizmos data version numbers
// 0 : 3MF files saved by older BambuSlicer or the painting gizmo wasn't used. No version definition in them.
// 1 : Introduction of painting gizmos data versioning. No other changes in painting gizmos data.
const unsigned int FDM_SUPPORTS_PAINTING_VERSION = 1;
const unsigned int SEAM_PAINTING_VERSION         = 1;
const unsigned int MM_PAINTING_VERSION           = 1;

const std::string SLIC3RPE_FDM_SUPPORTS_PAINTING_VERSION = "slic3rpe:FdmSupportsPaintingVersion";
const std::string SLIC3RPE_SEAM_PAINTING_VERSION         = "slic3rpe:SeamPaintingVersion";
const std::string SLIC3RPE_MM_PAINTING_VERSION           = "slic3rpe:MmPaintingVersion";

const std::string MODEL_FOLDER = "3D/";
const std::string MODEL_EXTENSION = ".model";
const std::string MODEL_FILE = "3D/3dmodel.model"; // << this is the only format of the string which works with CURA
//BBS: add metadata_folder
const std::string METADATA_DIR = "Metadata/";
const std::string GCODE_EXTENSION = ".gcode";
const std::string CONTENT_TYPES_FILE = "[Content_Types].xml";
const std::string RELATIONSHIPS_FILE = "_rels/.rels";
const std::string THUMBNAIL_FILE = "Metadata/thumbnail";
const std::string PRINT_CONFIG_FILE = "Metadata/Slic3r_PE.config";
const std::string MODEL_CONFIG_FILE = "Metadata/Slic3r_PE_model.config";
const std::string BBS_PRINT_CONFIG_FILE = "Metadata/print_profile.config";
const std::string BBS_MODEL_CONFIG_FILE = "Metadata/model_settings.config";
const std::string SLICE_INFO_CONFIG_FILE = "Metadata/slice_info.config";
const std::string LAYER_HEIGHTS_PROFILE_FILE = "Metadata/Slic3r_PE_layer_heights_profile.txt";
const std::string LAYER_CONFIG_RANGES_FILE = "Metadata/Prusa_Slicer_layer_config_ranges.xml";
const std::string SLA_SUPPORT_POINTS_FILE = "Metadata/Slic3r_PE_sla_support_points.txt";
const std::string SLA_DRAIN_HOLES_FILE = "Metadata/Slic3r_PE_sla_drain_holes.txt";
const std::string CUSTOM_GCODE_PER_PRINT_Z_FILE = "Metadata/Prusa_Slicer_custom_gcode_per_print_z.xml";
const std::string AUXILIARY_DIR = "Auxiliaries/";
const std::string PROJECT_EMBEDDED_PRINT_PRESETS_FILE = "Metadata/print_setting_";
const std::string PROJECT_EMBEDDED_FILAMENT_PRESETS_FILE = "Metadata/filament_setting_";
const std::string PROJECT_EMBEDDED_PRINTER_PRESETS_FILE = "Metadata/printer_setting_";


const unsigned int AUXILIARY_STR_LEN = 12;
const unsigned int METADATA_STR_LEN = 9;


static constexpr const char* MODEL_TAG = "model";
static constexpr const char* RESOURCES_TAG = "resources";
static constexpr const char* OBJECT_TAG = "object";
static constexpr const char* MESH_TAG = "mesh";
static constexpr const char* VERTICES_TAG = "vertices";
static constexpr const char* VERTEX_TAG = "vertex";
static constexpr const char* TRIANGLES_TAG = "triangles";
static constexpr const char* TRIANGLE_TAG = "triangle";
static constexpr const char* COMPONENTS_TAG = "components";
static constexpr const char* COMPONENT_TAG = "component";
static constexpr const char* BUILD_TAG = "build";
static constexpr const char* ITEM_TAG = "item";
static constexpr const char* METADATA_TAG = "metadata";

static constexpr const char* CONFIG_TAG = "config";
static constexpr const char* VOLUME_TAG = "volume";
static constexpr const char* PART_TAG = "part";
static constexpr const char* PLATE_TAG = "plate";
static constexpr const char* INSTANCE_TAG = "model_instance";
//BBS
static constexpr const char* ASSEMBLE_TAG = "assemble";
static constexpr const char* ASSEMBLE_ITEM_TAG = "assemble_item";
static constexpr const char* SLICE_HEADER_TAG = "header";
static constexpr const char* SLICE_HEADER_ITEM_TAG = "header_item";


static constexpr const char* UNIT_ATTR = "unit";
static constexpr const char* NAME_ATTR = "name";
static constexpr const char* TYPE_ATTR = "type";
static constexpr const char* ID_ATTR = "id";
static constexpr const char* X_ATTR = "x";
static constexpr const char* Y_ATTR = "y";
static constexpr const char* Z_ATTR = "z";
static constexpr const char* V1_ATTR = "v1";
static constexpr const char* V2_ATTR = "v2";
static constexpr const char* V3_ATTR = "v3";
static constexpr const char* OBJECTID_ATTR = "objectid";
static constexpr const char* TRANSFORM_ATTR = "transform";
// BBS
static constexpr const char* OFFSET_ATTR = "offset";
static constexpr const char* PRINTABLE_ATTR = "printable";
static constexpr const char* INSTANCESCOUNT_ATTR = "instances_count";
static constexpr const char* CUSTOM_SUPPORTS_ATTR = "slic3rpe:custom_supports";
static constexpr const char* CUSTOM_SEAM_ATTR = "slic3rpe:custom_seam";
static constexpr const char* MMU_SEGMENTATION_ATTR = "slic3rpe:mmu_segmentation";

static constexpr const char* KEY_ATTR = "key";
static constexpr const char* VALUE_ATTR = "value";
static constexpr const char* FIRST_TRIANGLE_ID_ATTR = "firstid";
static constexpr const char* LAST_TRIANGLE_ID_ATTR = "lastid";
static constexpr const char* SUBTYPE_ATTR = "subtype";
static constexpr const char* LOCK_ATTR = "locked";
static constexpr const char* OBJECT_ID_ATTR = "object_id";
static constexpr const char* INSTANCEID_ATTR = "instance_id";
static constexpr const char* PLATERID_ATTR = "plater_id";
static constexpr const char* PLATE_IDX_ATTR = "index";
static constexpr const char* SLICE_PREDICTION_ATTR = "prediction";
static constexpr const char* SLICE_WEIGHT_ATTR = "weight";
static constexpr const char* OUTSIDE_ATTR = "outside";

static constexpr const char* OBJECT_TYPE = "object";
static constexpr const char* VOLUME_TYPE = "volume";
static constexpr const char* PART_TYPE = "part";

static constexpr const char* NAME_KEY = "name";
static constexpr const char* MODIFIER_KEY = "modifier";
static constexpr const char* VOLUME_TYPE_KEY = "volume_type";
static constexpr const char* PART_TYPE_KEY = "part_type";
static constexpr const char* MATRIX_KEY = "matrix";
static constexpr const char* SOURCE_FILE_KEY = "source_file";
static constexpr const char* SOURCE_OBJECT_ID_KEY = "source_object_id";
static constexpr const char* SOURCE_VOLUME_ID_KEY = "source_volume_id";
static constexpr const char* SOURCE_OFFSET_X_KEY = "source_offset_x";
static constexpr const char* SOURCE_OFFSET_Y_KEY = "source_offset_y";
static constexpr const char* SOURCE_OFFSET_Z_KEY = "source_offset_z";
static constexpr const char* SOURCE_IN_INCHES    = "source_in_inches";
static constexpr const char* SOURCE_IN_METERS    = "source_in_meters";

static constexpr const char* MESH_STAT_EDGES_FIXED          = "edges_fixed";
static constexpr const char* MESH_STAT_DEGENERATED_FACETS   = "degenerate_facets";
static constexpr const char* MESH_STAT_FACETS_REMOVED       = "facets_removed";
static constexpr const char* MESH_STAT_FACETS_RESERVED      = "facets_reversed";
static constexpr const char* MESH_STAT_BACKWARDS_EDGES      = "backwards_edges";


const unsigned int BBS_VALID_OBJECT_TYPES_COUNT = 1;
const char* BBS_VALID_OBJECT_TYPES[] =
{
    "model"
};

const char* BBS_INVALID_OBJECT_TYPES[] =
{
    "solidsupport",
    "support",
    "surface",
    "other"
};

class version_error : public Slic3r::FileIOError
{
public:
    version_error(const std::string& what_arg) : Slic3r::FileIOError(what_arg) {}
    version_error(const char* what_arg) : Slic3r::FileIOError(what_arg) {}
};

const char* bbs_get_attribute_value_charptr(const char** attributes, unsigned int attributes_size, const char* attribute_key)
{
    if ((attributes == nullptr) || (attributes_size == 0) || (attributes_size % 2 != 0) || (attribute_key == nullptr))
        return nullptr;

    for (unsigned int a = 0; a < attributes_size; a += 2) {
        if (::strcmp(attributes[a], attribute_key) == 0)
            return attributes[a + 1];
    }

    return nullptr;
}

std::string bbs_get_attribute_value_string(const char** attributes, unsigned int attributes_size, const char* attribute_key)
{
    const char* text = bbs_get_attribute_value_charptr(attributes, attributes_size, attribute_key);
    return (text != nullptr) ? text : "";
}

float bbs_get_attribute_value_float(const char** attributes, unsigned int attributes_size, const char* attribute_key)
{
    float value = 0.0f;
    if (const char *text = bbs_get_attribute_value_charptr(attributes, attributes_size, attribute_key); text != nullptr)
        fast_float::from_chars(text, text + strlen(text), value);
    return value;
}

int bbs_get_attribute_value_int(const char** attributes, unsigned int attributes_size, const char* attribute_key)
{
    int value = 0;
    if (const char *text = bbs_get_attribute_value_charptr(attributes, attributes_size, attribute_key); text != nullptr)
        boost::spirit::qi::parse(text, text + strlen(text), boost::spirit::qi::int_, value);
    return value;
}

bool bbs_get_attribute_value_bool(const char** attributes, unsigned int attributes_size, const char* attribute_key)
{
    const char* text = bbs_get_attribute_value_charptr(attributes, attributes_size, attribute_key);
    return (text != nullptr) ? (bool)::atoi(text) : true;
}

Slic3r::Transform3d bbs_get_transform_from_3mf_specs_string(const std::string& mat_str)
{
    // check: https://3mf.io/3d-manufacturing-format/ or https://github.com/3MFConsortium/spec_core/blob/master/3MF%20Core%20Specification.md
    // to see how matrices are stored inside 3mf according to specifications
    Slic3r::Transform3d ret = Slic3r::Transform3d::Identity();

    if (mat_str.empty())
        // empty string means default identity matrix
        return ret;

    std::vector<std::string> mat_elements_str;
    boost::split(mat_elements_str, mat_str, boost::is_any_of(" "), boost::token_compress_on);

    unsigned int size = (unsigned int)mat_elements_str.size();
    if (size != 12)
        // invalid data, return identity matrix
        return ret;

    unsigned int i = 0;
    // matrices are stored into 3mf files as 4x3
    // we need to transpose them
    for (unsigned int c = 0; c < 4; ++c) {
        for (unsigned int r = 0; r < 3; ++r) {
            ret(r, c) = ::atof(mat_elements_str[i++].c_str());
        }
    }
    return ret;
}

Slic3r::Vec3d bbs_get_offset_from_3mf_specs_string(const std::string& vec_str)
{
    Slic3r::Vec3d ofs2ass(0, 0, 0);

    if (vec_str.empty())
        // empty string means default zero offset
        return ofs2ass;

    std::vector<std::string> vec_elements_str;
    boost::split(vec_elements_str, vec_str, boost::is_any_of(" "), boost::token_compress_on);

    unsigned int size = (unsigned int)vec_elements_str.size();
    if (size != 3)
        // invalid data, return zero offset
        return ofs2ass;

    for (unsigned int i = 0; i < 3; i++) {
        ofs2ass(i) = ::atof(vec_elements_str[i].c_str());
    }

    return ofs2ass;
}

float bbs_get_unit_factor(const std::string& unit)
{
    const char* text = unit.c_str();

    if (::strcmp(text, "micron") == 0)
        return 0.001f;
    else if (::strcmp(text, "centimeter") == 0)
        return 10.0f;
    else if (::strcmp(text, "inch") == 0)
        return 25.4f;
    else if (::strcmp(text, "foot") == 0)
        return 304.8f;
    else if (::strcmp(text, "meter") == 0)
        return 1000.0f;
    else
        // default "millimeters" (see specification)
        return 1.0f;
}

bool bbs_is_valid_object_type(const std::string& type)
{
    // if the type is empty defaults to "model" (see specification)
    if (type.empty())
        return true;

    for (unsigned int i = 0; i < BBS_VALID_OBJECT_TYPES_COUNT; ++i) {
        if (::strcmp(type.c_str(), BBS_VALID_OBJECT_TYPES[i]) == 0)
            return true;
    }

    return false;
}

namespace Slic3r {

//! macro used to mark string used at localization,
//! return same string
#define L(s) (s)
#define _(s) Slic3r::I18N::translate(s)

    // Base class with error messages management
    class _BBS_3MF_Base
    {
        std::vector<std::string> m_errors;

    protected:
        void add_error(const std::string& error) { m_errors.push_back(error); }
        void clear_errors() { m_errors.clear(); }

    public:
        void log_errors()
        {
            for (const std::string& error : m_errors)
                BOOST_LOG_TRIVIAL(error) << error;
        }
    };

    class _BBS_3MF_Importer : public _BBS_3MF_Base
    {
        struct Component
        {
            int object_id;
            Transform3d transform;

            explicit Component(int object_id)
                : object_id(object_id)
                , transform(Transform3d::Identity())
            {
            }

            Component(int object_id, const Transform3d& transform)
                : object_id(object_id)
                , transform(transform)
            {
            }
        };

        typedef std::vector<Component> ComponentsList;

        struct Geometry
        {
            std::vector<Vec3f> vertices;
            std::vector<Vec3i> triangles;
            std::vector<std::string> custom_supports;
            std::vector<std::string> custom_seam;
            std::vector<std::string> mmu_segmentation;

            bool empty() { return vertices.empty() || triangles.empty(); }

            // backup & restore
            void swap(Geometry& o) {
                std::swap(vertices, o.vertices);
                std::swap(triangles, o.triangles);
                std::swap(custom_supports, o.custom_supports);
                std::swap(custom_seam, o.custom_seam);
            }

            void reset() {
                vertices.clear();
                triangles.clear();
                custom_supports.clear();
                custom_seam.clear();
                mmu_segmentation.clear();
            }
        };

        struct CurrentObject
        {
            // ID of the object inside the 3MF file, 1 based.
            int id;
            // Index of the ModelObject in its respective Model, zero based.
            int model_object_idx;
            Geometry geometry;
            ModelObject* object;
            ComponentsList components;

            CurrentObject() { reset(); }

            void reset() {
                id = -1;
                model_object_idx = -1;
                geometry.reset();
                object = nullptr;
                components.clear();
            }
        };

        struct CurrentConfig
        {
            int object_id;
            int volume_id;
        };

        struct CurrentInstance
        {
            int object_id;
            int instance_id;
        };

        struct Instance
        {
            ModelInstance* instance;
            Transform3d transform;

            Instance(ModelInstance* instance, const Transform3d& transform)
                : instance(instance)
                , transform(transform)
            {
            }
        };

        struct Metadata
        {
            std::string key;
            std::string value;

            Metadata(const std::string& key, const std::string& value)
                : key(key)
                , value(value)
            {
            }
        };

        typedef std::vector<Metadata> MetadataList;

        struct ObjectMetadata
        {
            struct VolumeMetadata
            {
                unsigned int first_triangle_id;
                unsigned int last_triangle_id;
                MetadataList metadata;
                RepairedMeshErrors mesh_stats;

                VolumeMetadata(unsigned int first_triangle_id, unsigned int last_triangle_id)
                    : first_triangle_id(first_triangle_id)
                    , last_triangle_id(last_triangle_id)
                {
                }
            };

            typedef std::vector<VolumeMetadata> VolumeMetadataList;

            MetadataList metadata;
            VolumeMetadataList volumes;
        };

        // Map from a 1 based 3MF object ID to a 0 based ModelObject index inside m_model->objects.
        typedef std::map<int, int> IdToModelObjectMap;
        typedef std::map<int, ComponentsList> IdToAliasesMap;
        typedef std::vector<Instance> InstancesList;
        typedef std::map<int, ObjectMetadata> IdToMetadataMap;
        typedef std::map<int, Geometry> IdToGeometryMap;
        typedef std::map<int, std::vector<coordf_t>> IdToLayerHeightsProfileMap;
        typedef std::map<int, t_layer_config_ranges> IdToLayerConfigRangesMap;
        typedef std::map<int, std::vector<sla::SupportPoint>> IdToSlaSupportPointsMap;
        typedef std::map<int, std::vector<sla::DrainHole>> IdToSlaDrainHolesMap;

        // Version of the 3mf file
        unsigned int m_version;
        bool m_check_version;
        bool m_load_aux;
        // backup & restore
        bool m_load_restore;
        // bool m_mesh_only; load only mesh from origin 3mf, currently not work

        // Semantic version of BambuSlicer, that generated this 3MF.
        boost::optional<Semver> m_bambuslicer_generator_version;
        unsigned int m_fdm_supports_painting_version = 0;
        unsigned int m_seam_painting_version         = 0;
        unsigned int m_mm_painting_version           = 0;

        XML_Parser m_xml_parser;
        // Error code returned by the application side of the parser. In that case the expat may not reliably deliver the error state
        // after returning from XML_Parse() function, thus we keep the error state here.
        bool m_parse_error { false };
        std::string m_parse_error_message;
        Model* m_model;
        float m_unit_factor;
        CurrentObject m_curr_object;
        IdToModelObjectMap m_objects;
        IdToAliasesMap m_objects_aliases;
        InstancesList m_instances;
        IdToGeometryMap m_geometries;
        IdToGeometryMap m_orig_geometries; // backup & restore
        CurrentConfig m_curr_config;
        IdToMetadataMap m_objects_metadata;
        IdToLayerHeightsProfileMap m_layer_heights_profiles;
        IdToLayerConfigRangesMap m_layer_config_ranges;
        IdToSlaSupportPointsMap m_sla_support_points;
        IdToSlaDrainHolesMap    m_sla_drain_holes;
        std::map<unsigned int, size_t> m_object_id_map; // backup & restore
        std::map<unsigned int, std::string> m_plate_id_map; // backup & restore
        std::string m_curr_metadata_name;
        std::string m_curr_characters;
        std::string m_name;

        //BBS: plater related structures
        bool m_is_bbl_3mf { false };
        bool m_parsing_slice_info { false };
        PlateDataMaps m_plater_data;
        PlateData* m_curr_plater;
        CurrentInstance m_curr_instance;
        std::vector<std::string> m_gcode_files;

    public:
        _BBS_3MF_Importer();
        ~_BBS_3MF_Importer();

        //BBS: add plate data related logic
        // add backup & restore logic
        bool load_model_from_file(const std::string& filename, Model& model, PlateDataPtrs& plate_data_list, std::vector<Preset*>& project_presets, DynamicPrintConfig& config, ConfigSubstitutionContext& config_substitutions, bool check_version, bool& is_bbl_3mf, bool load_aux, bool load_restore, Import3mfProgressFn proFn = nullptr);
        unsigned int version() const { return m_version; }

    private:
        void _destroy_xml_parser();
        void _stop_xml_parser(const std::string& msg = std::string());

        bool        parse_error()         const { return m_parse_error; }
        const char* parse_error_message() const {
            return m_parse_error ?
                // The error was signalled by the user code, not the expat parser.
                (m_parse_error_message.empty() ? "Invalid 3MF format" : m_parse_error_message.c_str()) :
                // The error was signalled by the expat parser.
                XML_ErrorString(XML_GetErrorCode(m_xml_parser));
        }

        //BBS: add plate data related logic
        // add backup & restore logic
        bool _load_model_from_file(std::string filename, Model& model, PlateDataPtrs& plate_data_list, std::vector<Preset*>& project_presets, DynamicPrintConfig& config, ConfigSubstitutionContext& config_substitutions, Import3mfProgressFn proFn = nullptr);
        bool _extract_model_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat);
        bool _extract_model_from_file(std::string const& file); // mesh only file -- backup & restore logic
        void _extract_layer_heights_profile_config_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat);
        void _extract_layer_config_ranges_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat, ConfigSubstitutionContext& config_substitutions);
        void _extract_sla_support_points_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat);
        void _extract_sla_drain_holes_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat);

        void _extract_custom_gcode_per_print_z_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat);

        void _extract_print_config_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat, DynamicPrintConfig& config, ConfigSubstitutionContext& subs_context, const std::string& archive_filename);
        bool _extract_model_config_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat, Model& model);
        //BBS: extract project embedded presets
        void _extract_project_embedded_presets_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat, std::vector<Preset*>&project_presets, Model& model, Preset::Type type);

        void _extract_auxiliary_file_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat, Model& model);
        void _extract_gcode_file_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat, Model& model, std::string& name);
        bool _extract_slice_info_config_file_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat, Model& model);

        // handlers to parse the .model file
        void _handle_start_model_xml_element(const char* name, const char** attributes);
        void _handle_end_model_xml_element(const char* name);
        void _handle_model_xml_characters(const XML_Char* s, int len);

        // handlers to parse the MODEL_CONFIG_FILE file
        void _handle_start_config_xml_element(const char* name, const char** attributes);
        void _handle_end_config_xml_element(const char* name);

        bool _handle_start_model(const char** attributes, unsigned int num_attributes);
        bool _handle_end_model();

        bool _handle_start_resources(const char** attributes, unsigned int num_attributes);
        bool _handle_end_resources();

        bool _handle_start_object(const char** attributes, unsigned int num_attributes);
        bool _handle_end_object();

        bool _handle_start_mesh(const char** attributes, unsigned int num_attributes);
        bool _handle_end_mesh();

        bool _handle_start_vertices(const char** attributes, unsigned int num_attributes);
        bool _handle_end_vertices();

        bool _handle_start_vertex(const char** attributes, unsigned int num_attributes);
        bool _handle_end_vertex();

        bool _handle_start_triangles(const char** attributes, unsigned int num_attributes);
        bool _handle_end_triangles();

        bool _handle_start_triangle(const char** attributes, unsigned int num_attributes);
        bool _handle_end_triangle();

        bool _handle_start_components(const char** attributes, unsigned int num_attributes);
        bool _handle_end_components();

        bool _handle_start_component(const char** attributes, unsigned int num_attributes);
        bool _handle_end_component();

        bool _handle_start_build(const char** attributes, unsigned int num_attributes);
        bool _handle_end_build();

        bool _handle_start_item(const char** attributes, unsigned int num_attributes);
        bool _handle_end_item();

        bool _handle_start_metadata(const char** attributes, unsigned int num_attributes);
        bool _handle_end_metadata();

        bool _create_object_instance(int object_id, const Transform3d& transform, const bool printable, unsigned int recur_counter);

        void _apply_transform(ModelInstance& instance, const Transform3d& transform);

        bool _handle_start_config(const char** attributes, unsigned int num_attributes);
        bool _handle_end_config();

        bool _handle_start_config_object(const char** attributes, unsigned int num_attributes);
        bool _handle_end_config_object();

        bool _handle_start_config_volume(const char** attributes, unsigned int num_attributes);
        bool _handle_start_config_volume_mesh(const char** attributes, unsigned int num_attributes);
        bool _handle_end_config_volume();
        bool _handle_end_config_volume_mesh();

        bool _handle_start_config_metadata(const char** attributes, unsigned int num_attributes);
        bool _handle_end_config_metadata();

        //BBS: add plater config parse functions
        bool _handle_start_config_plater(const char** attributes, unsigned int num_attributes);
        bool _handle_end_config_plater();

        bool _handle_start_config_plater_instance(const char** attributes, unsigned int num_attributes);
        bool _handle_end_config_plater_instance();

        bool _handle_start_assemble(const char** attributes, unsigned int num_attributes);
        bool _handle_end_assemble();

        bool _handle_start_assemble_item(const char** attributes, unsigned int num_attributes);
        bool _handle_end_assemble_item();

        bool _generate_volumes(ModelObject& object, const Geometry& geometry, const ObjectMetadata::VolumeMetadataList& volumes, ConfigSubstitutionContext& config_substitutions);

        // callbacks to parse the .model file
        static void XMLCALL _handle_start_model_xml_element(void* userData, const char* name, const char** attributes);
        static void XMLCALL _handle_end_model_xml_element(void* userData, const char* name);
        static void XMLCALL _handle_model_xml_characters(void* userData, const XML_Char* s, int len);

        // callbacks to parse the MODEL_CONFIG_FILE file
        static void XMLCALL _handle_start_config_xml_element(void* userData, const char* name, const char** attributes);
        static void XMLCALL _handle_end_config_xml_element(void* userData, const char* name);
    };

    _BBS_3MF_Importer::_BBS_3MF_Importer()
        : m_version(0)
        , m_check_version(false)
        , m_xml_parser(nullptr)
        , m_model(nullptr)   
        , m_unit_factor(1.0f)
        , m_curr_metadata_name("")
        , m_curr_characters("")
        , m_name("")
        , m_curr_plater(nullptr)
    {
    }

    _BBS_3MF_Importer::~_BBS_3MF_Importer()
    {
        _destroy_xml_parser();

        std::map<int, PlateData*>::iterator it = m_plater_data.begin();
        while (it != m_plater_data.end())
        {
            delete it->second;
            it++;
        }
        m_plater_data.clear();

        m_gcode_files.clear();
    }

    //BBS: add plate data related logic
        // add backup & restore logic
    bool _BBS_3MF_Importer::load_model_from_file(const std::string& filename, Model& model, PlateDataPtrs& plate_data_list, std::vector<Preset*>& project_presets, DynamicPrintConfig& config, ConfigSubstitutionContext& config_substitutions, bool check_version, bool& is_bbl_3mf, bool load_aux, bool load_restore, Import3mfProgressFn proFn)
    {
        m_version = 0;
        m_fdm_supports_painting_version = 0;
        m_seam_painting_version = 0;
        m_mm_painting_version = 0;
        m_check_version = check_version;
        //BBS: auxiliary data
        m_load_aux = load_aux;
        m_load_restore = load_restore;
        // m_mesh_only = false;
        m_model = &model;
        m_unit_factor = 1.0f;
        m_curr_object.reset();
        m_objects.clear();
        m_objects_aliases.clear();
        m_instances.clear();
        m_geometries.clear();
        m_curr_config.object_id = -1;
        m_curr_config.volume_id = -1;
        m_objects_metadata.clear();
        m_layer_heights_profiles.clear();
        m_layer_config_ranges.clear();
        m_sla_support_points.clear();
        m_curr_metadata_name.clear();
        m_curr_characters.clear();
        //BBS: plater data init
        m_plater_data.clear();
        m_curr_instance.object_id = -1;
        m_curr_instance.instance_id = -1;
        m_gcode_files.clear();
        clear_errors();

        // restore
        if (load_restore) {
            model.set_backup_path(filename.substr(0, filename.size() - 5));
            boost::filesystem::save_string_file(
                model.get_backup_path() + "/lock.txt",
                boost::lexical_cast<std::string>(get_current_pid()));
        }
        bool result = _load_model_from_file(filename, model, plate_data_list, project_presets, config, config_substitutions, proFn);
        is_bbl_3mf = m_is_bbl_3mf;
        // save for restore
        if (result && load_aux && !load_restore) {
            boost::filesystem::save_string_file(model.get_backup_path() + "/origin.txt", filename);
        }
        if (load_restore && !result) // not clear failed backup data for later analyze
            model.set_backup_path("");
        return result;
    }

    void _BBS_3MF_Importer::_destroy_xml_parser()
    {
        if (m_xml_parser != nullptr) {
            XML_ParserFree(m_xml_parser);
            m_xml_parser = nullptr;
        }
    }

    void _BBS_3MF_Importer::_stop_xml_parser(const std::string &msg)
    {
        assert(! m_parse_error);
        assert(m_parse_error_message.empty());
        assert(m_xml_parser != nullptr);
        m_parse_error = true;
        m_parse_error_message = msg;
        XML_StopParser(m_xml_parser, false);
    }

    //BBS: add plate data related logic
    bool _BBS_3MF_Importer::_load_model_from_file(std::string filename, Model& model, PlateDataPtrs& plate_data_list, std::vector<Preset*>& project_presets, DynamicPrintConfig& config, ConfigSubstitutionContext& config_substitutions, Import3mfProgressFn proFn)
    {
        bool cb_cancel = false;
        //BBS progress point
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ":" << __LINE__ << boost::format("import 3mf IMPORT_STAGE_RESTORE\n");
        if (proFn) {
            proFn(IMPORT_STAGE_RESTORE, 0, 1, cb_cancel);
            if (cb_cancel)
                return false;
        }

        // prepare restore
        if (m_load_restore) {
            std::string objectmapfile = model.get_backup_path() + "/object_map.txt";
            {
                std::ifstream ifs(encode_path(objectmapfile.c_str()));
                unsigned int id1;
                size_t id2;
                while (ifs >> id1 >> id2) {
                    BOOST_LOG_TRIVIAL(info) << "load object_map: " << id1 << " -> " << id2;
                    m_object_id_map.insert(std::make_pair(id1, id2));
                }
            }
            std::string platemapfile = model.get_backup_path() + "/plate_map.txt";
            {
                std::ifstream ifs(encode_path(platemapfile.c_str()));
                unsigned int id1;
                std::string id2;
                while (ifs >> id1 >> id2) {
                    BOOST_LOG_TRIVIAL(info) << "load plate_map: " << id1 << " -> " << id2;
                    m_plate_id_map.insert(std::make_pair(id1, id2));
                }
            }
            std::string originfile;
            try {
                if (boost::filesystem::exists(model.get_backup_path() + "/origin.txt"))
                    boost::filesystem::load_string_file(model.get_backup_path() + "/origin.txt", originfile);
            }
            catch (...) {
            }
            if (!originfile.empty()) {
                // m_mesh_only = true;
                // m_load_restore = false;
                // preload from origin file, pending params are not hit
                // _load_model_from_file(originfile, model, plate_data_list, config, config_substitutions);
                // m_load_restore = true;
                // m_mesh_only = false;
            }
        }

        //BBS progress point
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ":" << __LINE__ << boost::format("import 3mf IMPORT_STAGE_OPEN\n");
        if (proFn) {
            proFn(IMPORT_STAGE_OPEN, 0, 1, cb_cancel);
            if (cb_cancel)
                return false;
        }

        mz_zip_archive archive;
        mz_zip_zero_struct(&archive);

        if (!open_zip_reader(&archive, filename)) {
            add_error("Unable to open the file");
            return false;
        }

        mz_uint num_entries = mz_zip_reader_get_num_files(&archive);

        mz_zip_archive_file_stat stat;

        m_name = boost::filesystem::path(filename).stem().string();


        // we first loop the entries to read from the archive the .model file only, in order to extract the version from it
        for (mz_uint i = 0; i < num_entries; ++i) {
            if (mz_zip_reader_file_stat(&archive, i, &stat)) {
                
                //BBS progress point
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ":" << __LINE__ << boost::format("import 3mf IMPORT_STAGE_READ_FILES\n");
                if (proFn) {
                    proFn(IMPORT_STAGE_READ_FILES, i, num_entries, cb_cancel);
                    if (cb_cancel)
                        return false;
                }

                std::string name(stat.m_filename);
                std::replace(name.begin(), name.end(), '\\', '/');

                if (boost::algorithm::istarts_with(name, MODEL_FOLDER) && boost::algorithm::iends_with(name, MODEL_EXTENSION)) {
                    try
                    {
                        // valid model name -> extract model
                        if (!_extract_model_from_archive(archive, stat)) {
                            close_zip_reader(&archive);
                            add_error("Archive does not contain a valid model");
                            return false;
                        }
                    }
                    catch (const std::exception& e)
                    {
                        // ensure the zip archive is closed and rethrow the exception
                        close_zip_reader(&archive);
                        add_error(e.what());
                        return false;
                    }
                }
            }
        }

        // only load for mesh, finish here
        // if (m_mesh_only) {
        //     close_zip_reader(&archive);
        //     m_objects.clear();
        //    
        //     return true;
        // }

        // we then loop again the entries to read other files stored in the archive
        for (mz_uint i = 0; i < num_entries; ++i) {
            if (mz_zip_reader_file_stat(&archive, i, &stat)) {

                //BBS progress point
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ":" << __LINE__ << boost::format("import 3mf IMPORT_STAGE_EXTRACT\n");
                if (proFn) {
                    proFn(IMPORT_STAGE_EXTRACT, i, num_entries, cb_cancel);
                    if (cb_cancel)
                        return false;
                }

                std::string name(stat.m_filename);
                std::replace(name.begin(), name.end(), '\\', '/');

                //BBS: disable adaptive layer height related file in 3MF
                /* if (boost::algorithm::iequals(name, LAYER_HEIGHTS_PROFILE_FILE)) {
                    // extract slic3r layer heights profile file
                    _extract_layer_heights_profile_config_from_archive(archive, stat);
                }
                else */
                if (boost::algorithm::iequals(name, LAYER_CONFIG_RANGES_FILE)) {
                    // extract slic3r layer config ranges file
                    _extract_layer_config_ranges_from_archive(archive, stat, config_substitutions);
                }
                //BBS: disable SLA related files currently
                /*else if (boost::algorithm::iequals(name, SLA_SUPPORT_POINTS_FILE)) {
                    // extract sla support points file
                    _extract_sla_support_points_from_archive(archive, stat);
                }
                else if (boost::algorithm::iequals(name, SLA_DRAIN_HOLES_FILE)) {
                    // extract sla support points file
                    _extract_sla_drain_holes_from_archive(archive, stat);
                }*/
                else if ((boost::algorithm::iequals(name, PRINT_CONFIG_FILE))||(boost::algorithm::iequals(name, BBS_PRINT_CONFIG_FILE))) {
                    // extract slic3r print config file
                    _extract_print_config_from_archive(archive, stat, config, config_substitutions, filename);
                }
                //BBS: project embedded presets
                else if (boost::algorithm::istarts_with(name, PROJECT_EMBEDDED_PRINT_PRESETS_FILE)) {
                    // extract slic3r layer config ranges file
                    _extract_project_embedded_presets_from_archive(archive, stat, project_presets, model, Preset::TYPE_PRINT);
                }
                else if (boost::algorithm::istarts_with(name, PROJECT_EMBEDDED_FILAMENT_PRESETS_FILE)) {
                    // extract slic3r layer config ranges file
                    _extract_project_embedded_presets_from_archive(archive, stat, project_presets, model, Preset::TYPE_FILAMENT);
                }
                else if (boost::algorithm::istarts_with(name, PROJECT_EMBEDDED_PRINTER_PRESETS_FILE)) {
                    // extract slic3r layer config ranges file
                    _extract_project_embedded_presets_from_archive(archive, stat, project_presets, model, Preset::TYPE_PRINTER);
                }
                else if (boost::algorithm::iequals(name, CUSTOM_GCODE_PER_PRINT_Z_FILE)) {
                    // extract slic3r layer config ranges file
                    _extract_custom_gcode_per_print_z_from_archive(archive, stat);
                }
                else if ((boost::algorithm::iequals(name, MODEL_CONFIG_FILE))||(boost::algorithm::iequals(name, BBS_MODEL_CONFIG_FILE))) {
                    // extract slic3r model config file
                    if (!_extract_model_config_from_archive(archive, stat, model)) {
                        close_zip_reader(&archive);
                        add_error("Archive does not contain a valid model config");
                        return false;
                    }
                }
                else if (boost::algorithm::iequals(name, SLICE_INFO_CONFIG_FILE)) {
                    m_parsing_slice_info = true;
                    //extract slice info from archive
                    _extract_slice_info_config_file_from_archive(archive, stat, model);
                    m_parsing_slice_info = false;
                }
                else if (boost::algorithm::istarts_with(name, AUXILIARY_DIR)) {
                    // extract auxiliary directory to temp directory, do nothing for restore
                    if (m_load_aux && !m_load_restore)
                        _extract_auxiliary_file_from_archive(archive, stat, model);
                }
                else if (boost::algorithm::istarts_with(name, METADATA_DIR) && boost::algorithm::iends_with(name, GCODE_EXTENSION)) {
                    //load gcode files
                    _extract_gcode_file_from_archive(archive, stat, model, name);
                }
            }
        }

        close_zip_reader(&archive);

        if (m_version == 0) {
            // if the 3mf was not produced by BambuSlicer and there is more than one instance,
            // split the object in as many objects as instances
            size_t curr_models_count = m_model->objects.size();
            size_t i = 0;
            while (i < curr_models_count) {
                ModelObject* model_object = m_model->objects[i];
                if (model_object->instances.size() > 1) {
                    // select the geometry associated with the original model object
                    const Geometry* geometry = nullptr;
                    int object_idx = 0;
                    for (const IdToModelObjectMap::value_type& object : m_objects) {
                        if (object.second == int(i)) {
                            IdToGeometryMap::const_iterator obj_geometry = m_geometries.find(object.first);
                            if (obj_geometry == m_geometries.end()) {
                                add_error("Unable to find object geometry");
                                return false;
                            }

                            //BBS progress point
                            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ":" << __LINE__ << boost::format("import 3mf IMPORT_STAGE_LOADING_OBJECTS\n");
                            if (proFn) {
                                proFn(IMPORT_STAGE_LOADING_OBJECTS, object_idx, m_objects.size(), cb_cancel);
                                if (cb_cancel)
                                    return false;
                            }
                            object_idx++;

                            geometry = &obj_geometry->second;
                            break;
                        }
                    }

                    if (geometry == nullptr) {
                        add_error("Unable to find object geometry");
                        return false;
                    }

                    // use the geometry to create the volumes in the new model objects
                    ObjectMetadata::VolumeMetadataList volumes(1, { 0, (unsigned int)geometry->triangles.size() - 1 });

                    // for each instance after the 1st, create a new model object containing only that instance
                    // and copy into it the geometry
                    while (model_object->instances.size() > 1) {
                        ModelObject* new_model_object = m_model->add_object(*model_object);
                        new_model_object->clear_instances();
                        new_model_object->add_instance(*model_object->instances.back());
                        model_object->delete_last_instance();
                        if (!_generate_volumes(*new_model_object, *geometry, volumes, config_substitutions))
                            return false;
                    }
                }
                ++i;
            }
        }

        for (const IdToModelObjectMap::value_type& object : m_objects) {
            if (object.second >= int(m_model->objects.size())) {
                add_error("Unable to find object");
                return false;
            }
            ModelObject* model_object = m_model->objects[object.second];
            IdToGeometryMap::const_iterator obj_geometry = m_geometries.find(object.first);
            if (obj_geometry == m_geometries.end()) {
                add_error("Unable to find object geometry");
                return false;
            }

            // m_layer_heights_profiles are indexed by a 1 based model object index.
            IdToLayerHeightsProfileMap::iterator obj_layer_heights_profile = m_layer_heights_profiles.find(object.second + 1);
            if (obj_layer_heights_profile != m_layer_heights_profiles.end())
                model_object->layer_height_profile.set(std::move(obj_layer_heights_profile->second));

            // m_layer_config_ranges are indexed by a 1 based model object index.
            IdToLayerConfigRangesMap::iterator obj_layer_config_ranges = m_layer_config_ranges.find(object.second + 1);
            if (obj_layer_config_ranges != m_layer_config_ranges.end())
                model_object->layer_config_ranges = std::move(obj_layer_config_ranges->second);

            // m_sla_support_points are indexed by a 1 based model object index.
            IdToSlaSupportPointsMap::iterator obj_sla_support_points = m_sla_support_points.find(object.second + 1);
            if (obj_sla_support_points != m_sla_support_points.end() && !obj_sla_support_points->second.empty()) {
                model_object->sla_support_points = std::move(obj_sla_support_points->second);
                model_object->sla_points_status = sla::PointsStatus::UserModified;
            }

            IdToSlaDrainHolesMap::iterator obj_drain_holes = m_sla_drain_holes.find(object.second + 1);
            if (obj_drain_holes != m_sla_drain_holes.end() && !obj_drain_holes->second.empty()) {
                model_object->sla_drain_holes = std::move(obj_drain_holes->second);
            }

            ObjectMetadata::VolumeMetadataList volumes;
            ObjectMetadata::VolumeMetadataList* volumes_ptr = nullptr;

            IdToMetadataMap::iterator obj_metadata = m_objects_metadata.find(object.first);
            if (obj_metadata != m_objects_metadata.end()) {
                // config data has been found, this model was saved using slic3r pe

                // apply object's name and config data
                for (const Metadata& metadata : obj_metadata->second.metadata) {
                    if (metadata.key == "name")
                        model_object->name = metadata.value;
                    //BBS: add module name
                    else if (metadata.key == "module")
                        model_object->module_name = metadata.value;
                    else
                        model_object->config.set_deserialize(metadata.key, metadata.value, config_substitutions);
                }

                // select object's detected volumes
                volumes_ptr = &obj_metadata->second.volumes;
            }
            else {
                // config data not found, this model was not saved using slic3r pe

                // add the entire geometry as the single volume to generate
                volumes.emplace_back(0, (int)obj_geometry->second.triangles.size() - 1);

                // select as volumes
                volumes_ptr = &volumes;
            }

            if (!_generate_volumes(*model_object, obj_geometry->second, *volumes_ptr, config_substitutions))
                return false;
        }

        int object_idx = 0;
        for (ModelObject* o : model.objects) {
            int volume_idx = 0;
            for (ModelVolume* v : o->volumes) {
                if (v->source.input_file.empty() && v->type() == ModelVolumeType::MODEL_PART) {
                    v->source.input_file = filename;
                    if (v->source.volume_idx == -1)
                        v->source.volume_idx = volume_idx;
                    if (v->source.object_idx == -1)
                        v->source.object_idx = object_idx;
                }
                ++volume_idx;
            }
            ++object_idx;
        }

//        // fixes the min z of the model if negative
//        model.adjust_min_z();

        //BBS progress point
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ":" << __LINE__ << boost::format("import 3mf IMPORT_STAGE_LOADING_PLATES\n");
        if (proFn) {
            proFn(IMPORT_STAGE_LOADING_PLATES, 0, 1, cb_cancel);
            if (cb_cancel)
                return false;
        }

        //BBS: load the plate info into plate_data_list
        std::map<int, PlateData*>::iterator it = m_plater_data.begin();
        plate_data_list.clear();
        plate_data_list.reserve(m_plater_data.size());
        for (unsigned int i = 0; i < m_plater_data.size(); i++)
        {
            PlateData* plate = new PlateData();
            plate_data_list.push_back(plate);
        }
        auto find_gcode_name = [this](int plate_index) -> int {
                for (int index = 0; index < m_gcode_files.size(); index ++)
                {
                    std::string &file_name = m_gcode_files[index];
                    std::string sub_name  = "plate_" + std::to_string(plate_index) + ".gcode";
                    if (file_name.find(sub_name) != std::string::npos) {
                        return index;
                    }
                }
                return -1;
            };
        while (it != m_plater_data.end())
        {
            if (it->first > m_plater_data.size())
            {
                add_error("invalid plate index");
                return false;
            }
            plate_data_list[it->first-1]->locked = it->second->locked;
            plate_data_list[it->first-1]->plate_index = it->second->plate_index-1;
            plate_data_list[it->first-1]->objects_and_instances = it->second->objects_and_instances;
            plate_data_list[it->first-1]->gcode_prediction = it->second->gcode_prediction;
            plate_data_list[it->first-1]->gcode_weight = it->second->gcode_weight;
            plate_data_list[it->first-1]->toolpath_outside = it->second->toolpath_outside;
            int gcode_index = find_gcode_name(it->first);
            if (gcode_index != -1) {
                plate_data_list[it->first-1]->gcode_file = m_gcode_files[gcode_index];
            }
            else if (m_plate_id_map.find(it->first) != m_plate_id_map.end()) {
                std::string gcode_file = model.get_backup_path() + "/" + m_plate_id_map[it->first] + ".gcode";
                if (boost::filesystem::exists(gcode_file))
                    plate_data_list[it->first - 1]->gcode_file = gcode_file;
            }
            it++;
        }

        // rename mesh files to new id, two pass to resolve conflict
        for (const IdToModelObjectMap::value_type& object : m_objects) {
            ModelObject* model_object = m_model->objects[object.second];
            size_t & id = m_object_id_map[object.first];
            std::string path2 = m_model->get_backup_path() + "/mesh_" + boost::lexical_cast<std::string>(id) + ".xml";
            id = model_object->id().id;
            std::string path = m_model->get_backup_path() + "/mesh2_" + boost::lexical_cast<std::string>(id) + ".xml";
            if (boost::filesystem::exists(path2) && !boost::filesystem::exists(path)) {
                boost::filesystem::rename(path2, path);
            }
        }
        for (const IdToModelObjectMap::value_type& object : m_objects) {
            ModelObject* model_object = m_model->objects[object.second];
            size_t id = model_object->id().id;
            std::string path = m_model->get_backup_path() + "/mesh_" + boost::lexical_cast<std::string>(id) + ".xml";
            std::string path2 = m_model->get_backup_path() + "/mesh2_" + boost::lexical_cast<std::string>(id) + ".xml";
            if (boost::filesystem::exists(path2) && !boost::filesystem::exists(path)) {
                boost::filesystem::rename(path2, path);
            }
        }

        //BBS progress point
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ":" << __LINE__ << boost::format("import 3mf IMPORT_STAGE_FINISH\n");
        if (proFn) {
            proFn(IMPORT_STAGE_FINISH, 0, 1, cb_cancel);
            if (cb_cancel)
                return false;
        }

        return true;
    }

    bool _BBS_3MF_Importer::_extract_model_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat)
    {
        if (stat.m_uncomp_size == 0) {
            add_error("Found invalid size");
            return false;
        }

        _destroy_xml_parser();

        m_xml_parser = XML_ParserCreate(nullptr);
        if (m_xml_parser == nullptr) {
            add_error("Unable to create parser");
            return false;
        }

        XML_SetUserData(m_xml_parser, (void*)this);
        XML_SetElementHandler(m_xml_parser, _BBS_3MF_Importer::_handle_start_model_xml_element, _BBS_3MF_Importer::_handle_end_model_xml_element);
        XML_SetCharacterDataHandler(m_xml_parser, _BBS_3MF_Importer::_handle_model_xml_characters);

        struct CallbackData
        {
            XML_Parser& parser;
            _BBS_3MF_Importer& importer;
            const mz_zip_archive_file_stat& stat;

            CallbackData(XML_Parser& parser, _BBS_3MF_Importer& importer, const mz_zip_archive_file_stat& stat) : parser(parser), importer(importer), stat(stat) {}
        };

        CallbackData data(m_xml_parser, *this, stat);

        mz_bool res = 0;

        try
        {
            res = mz_zip_reader_extract_file_to_callback(&archive, stat.m_filename, [](void* pOpaque, mz_uint64 file_ofs, const void* pBuf, size_t n)->size_t {
                CallbackData* data = (CallbackData*)pOpaque;
                if (!XML_Parse(data->parser, (const char*)pBuf, (int)n, (file_ofs + n == data->stat.m_uncomp_size) ? 1 : 0) || data->importer.parse_error()) {
                    char error_buf[1024];
                    ::sprintf(error_buf, "Error (%s) while parsing '%s' at line %d", data->importer.parse_error_message(), data->stat.m_filename, (int)XML_GetCurrentLineNumber(data->parser));
                    throw Slic3r::FileIOError(error_buf);
                }

                return n;
                }, &data, 0);
        }
        catch (const version_error& e)
        {
            // rethrow the exception
            throw Slic3r::FileIOError(e.what());
        }
        catch (std::exception& e)
        {
            add_error(e.what());
            return false;
        }

        if (res == 0) {
            add_error("Error while extracting model data from zip archive");
            return false;
        }

        return true;
    }
    
    // load mesh only file
    bool _BBS_3MF_Importer::_extract_model_from_file(std::string const & file)
    {
        auto xml_parser = m_xml_parser;
        m_xml_parser = XML_ParserCreate(nullptr);
        if (m_xml_parser == nullptr) {
            add_error("Unable to create parser");
            m_xml_parser = xml_parser;
            return false;
        }

        XML_SetUserData(m_xml_parser, (void*)this);
        XML_SetElementHandler(m_xml_parser, _BBS_3MF_Importer::_handle_start_model_xml_element, _BBS_3MF_Importer::_handle_end_model_xml_element);
        XML_SetCharacterDataHandler(m_xml_parser, _BBS_3MF_Importer::_handle_model_xml_characters);

        try
        {
            std::ifstream ifs(encode_path(file.c_str()));
            if (!ifs) {
                char error_buf[1024];
                ::sprintf(error_buf, "Error while opening '%s", file.c_str());
                throw Slic3r::FileIOError(error_buf);
            }
            std::string data(4096, char(0));
            while (ifs) {
                ifs.read(&data.front(), 4096);
                if (!XML_Parse(m_xml_parser, &data.front(), (int)ifs.gcount(), ifs.eof() || parse_error())) {
                    char error_buf[1024];
                    ::sprintf(error_buf, "Error (%s) while parsing '%s' at line %d", parse_error_message(), file.c_str(), (int)XML_GetCurrentLineNumber(xml_parser));
                    throw Slic3r::FileIOError(error_buf);
                }
            }
            XML_ParserFree(m_xml_parser);
            m_xml_parser = xml_parser;
        }
        catch (const version_error& e)
        {
            // rethrow the exception
            XML_ParserFree(m_xml_parser);
            m_xml_parser = xml_parser;
            throw Slic3r::FileIOError(e.what());
        }
        catch (std::exception& e)
        {
            XML_ParserFree(m_xml_parser);
            m_xml_parser = xml_parser;
            add_error(e.what());
            return false;
        }

        return true;
    }

    void _BBS_3MF_Importer::_extract_print_config_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat, DynamicPrintConfig& config, ConfigSubstitutionContext& config_substitutions, const std::string& archive_filename)
    {
        if (stat.m_uncomp_size > 0) {
            std::string buffer((size_t)stat.m_uncomp_size, 0);
            mz_bool res = mz_zip_reader_extract_file_to_mem(&archive, stat.m_filename, (void*)buffer.data(), (size_t)stat.m_uncomp_size, 0);
            if (res == 0) {
                add_error("Error while reading config data to buffer");
                return;
            }
            ConfigBase::load_from_gcode_string_legacy(config, buffer.data(), config_substitutions);
        }
    }

    //BBS: extract project embedded presets
    void _BBS_3MF_Importer::_extract_project_embedded_presets_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat, std::vector<Preset*>&project_presets, Model& model, Preset::Type type)
    {
        if (stat.m_uncomp_size > 0) {
            const std::string& temp_path = model.get_backup_path();
            /*std::string src_file = decode_path(stat.m_filename);
            std::size_t found = src_file.find(METADATA_DIR);
            if (found != std::string::npos)
                src_file = src_file.substr(found + METADATA_STR_LEN);
            else
                return;*/
            std::string dest_file = temp_path + std::string("/") + "_temp_2.config";;
            std::string dest_zip_file = encode_path(dest_file.c_str());
            mz_bool res = mz_zip_reader_extract_file_to_file(&archive, stat.m_filename, dest_zip_file.c_str(), 0);
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", extract  %1% from 3mf %2%, ret %3%\n") % dest_file % stat.m_filename % res;
            if (res == 0) {
                add_error("Error while extract auxiliary file to file");
                return;
            }
            //load presets
            DynamicPrintConfig config;
            ConfigSubstitutions config_substitutions = config.load_from_ini(dest_file, Enable);
            ConfigOptionString* print_name;
            ConfigOptionStrings* filament_names;
            std::string preset_name;
            if (type == Preset::TYPE_PRINT) {
                print_name = dynamic_cast < ConfigOptionString* > (config.option("print_settings_id"));
                if (!print_name) {
                    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(", can not found print_settings_id from  %1%\n") % dest_file;
                    //skip this file
                    return;
                }
                preset_name = print_name->value;
            }
            else if (type == Preset::TYPE_FILAMENT) {
                filament_names = dynamic_cast < ConfigOptionStrings* > (config.option("filament_settings_id"));
                if (!filament_names) {
                    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(", can not found filament_settings_id from  %1%\n") % dest_file;
                    //skip this file
                    return;
                }
                preset_name = filament_names->values[0];
            }
            else if (type == Preset::TYPE_PRINTER) {
                print_name = dynamic_cast < ConfigOptionString* > (config.option("printer_settings_id"));
                if (!print_name) {
                    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(", can not found printer_settings_id from  %1%\n") % dest_file;
                    //skip this file
                    return;
                }
                preset_name = print_name->value;
            }
            else {
                BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(", invalid type  %1% from file %2%\n")% Preset::get_type_string(type) % dest_file;
                //skip this file
                return;
            }

            Preset *preset = new Preset(type, preset_name, false);
            preset->file = dest_file;
            preset->config = std::move(config);
            preset->loaded = true;
            preset->is_project_embedded = true;
            preset->is_external = true;
            preset->is_dirty = false;
            /*for (int i = 0; i < config_substitutions.size(); i++)
            {
                //ConfigSubstitution config_substitution;
                //config_substitution.opt_def   = optdef;
                //config_substitution.old_value = value;
                //config_substitution.new_value = ConfigOptionUniquePtr(opt->clone());
                preset->loading_substitutions.emplace_back(std::move(config_substitutions[i]));
            }*/
            if (!config_substitutions.empty()) {
                preset->loading_substitutions = new ConfigSubstitutions();
                *(preset->loading_substitutions) = std::move(config_substitutions);
            }

            project_presets.push_back(preset);
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", create one project embedded preset: %1% from %2%, type %3%\n") % preset_name % dest_file %Preset::get_type_string(type);
        }
    }

    void _BBS_3MF_Importer::_extract_auxiliary_file_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat, Model& model)
    {
        if (stat.m_uncomp_size > 0) {
            std::string dest_file;
            std::string src_file = decode_path(stat.m_filename);
            std::string temp_path = model.get_auxiliary_file_temp_path();
            //aux directory from model
            boost::filesystem::path dir = boost::filesystem::path(temp_path);
            if (!boost::filesystem::exists(dir))
            {
                boost::filesystem::create_directory(dir);
            }
            std::size_t found = src_file.find(AUXILIARY_DIR);
            if (found != std::string::npos)
                src_file = src_file.substr(found + AUXILIARY_STR_LEN);
            else
                return;
            if (src_file.find('/') != std::string::npos)
            {
                boost::filesystem::path src_path = boost::filesystem::path(src_file);
                boost::filesystem::path parent_path = src_path.parent_path();
                std::string temp_path = dir.string() + std::string("/") + parent_path.string();
                boost::filesystem::path parent_full_path =  boost::filesystem::path(temp_path);
                if (!boost::filesystem::exists(parent_full_path))
                    boost::filesystem::create_directory(parent_full_path);
            }
            dest_file = dir.string() + std::string("/") + src_file;
            std::string dest_zip_file = encode_path(dest_file.c_str());
            //std::string src_zip_file = encode_path(stat.m_filename);
            mz_bool res = mz_zip_reader_extract_file_to_file(&archive, stat.m_filename, dest_zip_file.c_str(), 0);
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", extract  %1% from 3mf %2%, ret %3%\n") % dest_file % stat.m_filename % res;
            if (res == 0) {
                add_error("Error while extract auxiliary file to file");
                return;
            }
        }
    }

    //BBS: extract gcode file name from archive
    void _BBS_3MF_Importer::_extract_gcode_file_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat, Model& model, std::string& name)
    {
        if (stat.m_uncomp_size > 0) {
            std::string dest_file;
            std::string src_file = decode_path(stat.m_filename);
            // BBS: use backup path
            const std::string &temp_path = model.get_backup_path();
            //aux directory from model
            boost::filesystem::path dir = boost::filesystem::path(temp_path);
            std::size_t found = src_file.find(METADATA_DIR);
            if (found != std::string::npos)
                src_file = src_file.substr(found + METADATA_STR_LEN);
            else
                return;

            dest_file = dir.string() + std::string("/") + src_file;
            std::string dest_zip_file = encode_path(dest_file.c_str());
            mz_bool res = mz_zip_reader_extract_file_to_file(&archive, stat.m_filename, dest_zip_file.c_str(), 0);
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", extract  %1% from 3mf %2%, ret %3%\n") % dest_file % stat.m_filename % res;
            if (res == 0) {
                add_error("Error while extract gcode file to temp directory");
                return;
            }
            m_gcode_files.push_back(dest_file.c_str());
        }

        return ;
    }

    void _BBS_3MF_Importer::_extract_layer_heights_profile_config_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat)
    {
        if (stat.m_uncomp_size > 0) {
            std::string buffer((size_t)stat.m_uncomp_size, 0);
            mz_bool res = mz_zip_reader_extract_file_to_mem(&archive, stat.m_filename, (void*)buffer.data(), (size_t)stat.m_uncomp_size, 0);
            if (res == 0) {
                add_error("Error while reading layer heights profile data to buffer");
                return;
            }

            if (buffer.back() == '\n')
                buffer.pop_back();

            std::vector<std::string> objects;
            boost::split(objects, buffer, boost::is_any_of("\n"), boost::token_compress_off);

            for (const std::string& object : objects)             {
                std::vector<std::string> object_data;
                boost::split(object_data, object, boost::is_any_of("|"), boost::token_compress_off);
                if (object_data.size() != 2) {
                    add_error("Error while reading object data");
                    continue;
                }

                std::vector<std::string> object_data_id;
                boost::split(object_data_id, object_data[0], boost::is_any_of("="), boost::token_compress_off);
                if (object_data_id.size() != 2) {
                    add_error("Error while reading object id");
                    continue;
                }

                int object_id = std::atoi(object_data_id[1].c_str());
                if (object_id == 0) {
                    add_error("Found invalid object id");
                    continue;
                }

                IdToLayerHeightsProfileMap::iterator object_item = m_layer_heights_profiles.find(object_id);
                if (object_item != m_layer_heights_profiles.end()) {
                    add_error("Found duplicated layer heights profile");
                    continue;
                }

                std::vector<std::string> object_data_profile;
                boost::split(object_data_profile, object_data[1], boost::is_any_of(";"), boost::token_compress_off);
                if (object_data_profile.size() <= 4 || object_data_profile.size() % 2 != 0) {
                    add_error("Found invalid layer heights profile");
                    continue;
                }

                std::vector<coordf_t> profile;
                profile.reserve(object_data_profile.size());

                for (const std::string& value : object_data_profile) {
                    profile.push_back((coordf_t)std::atof(value.c_str()));
                }

                m_layer_heights_profiles.insert({ object_id, profile });
            }
        }
    }

    void _BBS_3MF_Importer::_extract_layer_config_ranges_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat, ConfigSubstitutionContext& config_substitutions)
    {
        if (stat.m_uncomp_size > 0) {
            std::string buffer((size_t)stat.m_uncomp_size, 0);
            mz_bool res = mz_zip_reader_extract_file_to_mem(&archive, stat.m_filename, (void*)buffer.data(), (size_t)stat.m_uncomp_size, 0);
            if (res == 0) {
                add_error("Error while reading layer config ranges data to buffer");
                return;
            }

            std::istringstream iss(buffer); // wrap returned xml to istringstream
            pt::ptree objects_tree;
            pt::read_xml(iss, objects_tree);

            for (const auto& object : objects_tree.get_child("objects")) {
                pt::ptree object_tree = object.second;
                int obj_idx = object_tree.get<int>("<xmlattr>.id", -1);
                if (obj_idx <= 0) {
                    add_error("Found invalid object id");
                    continue;
                }

                IdToLayerConfigRangesMap::iterator object_item = m_layer_config_ranges.find(obj_idx);
                if (object_item != m_layer_config_ranges.end()) {
                    add_error("Found duplicated layer config range");
                    continue;
                }

                t_layer_config_ranges config_ranges;

                for (const auto& range : object_tree) {
                    if (range.first != "range")
                        continue;
                    pt::ptree range_tree = range.second;
                    double min_z = range_tree.get<double>("<xmlattr>.min_z");
                    double max_z = range_tree.get<double>("<xmlattr>.max_z");

                    // get Z range information
                    DynamicPrintConfig config;

                    for (const auto& option : range_tree) {
                        if (option.first != "option")
                            continue;
                        std::string opt_key = option.second.get<std::string>("<xmlattr>.opt_key");
                        std::string value = option.second.data();

                        config.set_deserialize(opt_key, value, config_substitutions);
                    }

                    config_ranges[{ min_z, max_z }].assign_config(std::move(config));
                }

                if (!config_ranges.empty())
                    m_layer_config_ranges.insert({ obj_idx, std::move(config_ranges) });
            }
        }
    }

    void _BBS_3MF_Importer::_extract_sla_support_points_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat)
    {
        if (stat.m_uncomp_size > 0) {
            std::string buffer((size_t)stat.m_uncomp_size, 0);
            mz_bool res = mz_zip_reader_extract_file_to_mem(&archive, stat.m_filename, (void*)buffer.data(), (size_t)stat.m_uncomp_size, 0);
            if (res == 0) {
                add_error("Error while reading sla support points data to buffer");
                return;
            }

            if (buffer.back() == '\n')
                buffer.pop_back();

            std::vector<std::string> objects;
            boost::split(objects, buffer, boost::is_any_of("\n"), boost::token_compress_off);

            // Info on format versioning - see 3mf.hpp
            int version = 0;
            std::string key("support_points_format_version=");
            if (!objects.empty() && objects[0].find(key) != std::string::npos) {
                objects[0].erase(objects[0].begin(), objects[0].begin() + long(key.size())); // removes the string
                version = std::stoi(objects[0]);
                objects.erase(objects.begin()); // pop the header
            }

            for (const std::string& object : objects) {
                std::vector<std::string> object_data;
                boost::split(object_data, object, boost::is_any_of("|"), boost::token_compress_off);

                if (object_data.size() != 2) {
                    add_error("Error while reading object data");
                    continue;
                }

                std::vector<std::string> object_data_id;
                boost::split(object_data_id, object_data[0], boost::is_any_of("="), boost::token_compress_off);
                if (object_data_id.size() != 2) {
                    add_error("Error while reading object id");
                    continue;
                }

                int object_id = std::atoi(object_data_id[1].c_str());
                if (object_id == 0) {
                    add_error("Found invalid object id");
                    continue;
                }

                IdToSlaSupportPointsMap::iterator object_item = m_sla_support_points.find(object_id);
                if (object_item != m_sla_support_points.end()) {
                    add_error("Found duplicated SLA support points");
                    continue;
                }

                std::vector<std::string> object_data_points;
                boost::split(object_data_points, object_data[1], boost::is_any_of(" "), boost::token_compress_off);

                std::vector<sla::SupportPoint> sla_support_points;

                if (version == 0) {
                    for (unsigned int i=0; i<object_data_points.size(); i+=3)
                    sla_support_points.emplace_back(float(std::atof(object_data_points[i+0].c_str())),
                                                    float(std::atof(object_data_points[i+1].c_str())),
													float(std::atof(object_data_points[i+2].c_str())),
                                                    0.4f,
                                                    false);
                }
                if (version == 1) {
                    for (unsigned int i=0; i<object_data_points.size(); i+=5)
                    sla_support_points.emplace_back(float(std::atof(object_data_points[i+0].c_str())),
                                                    float(std::atof(object_data_points[i+1].c_str())),
                                                    float(std::atof(object_data_points[i+2].c_str())),
                                                    float(std::atof(object_data_points[i+3].c_str())),
													//FIXME storing boolean as 0 / 1 and importing it as float.
                                                    std::abs(std::atof(object_data_points[i+4].c_str()) - 1.) < EPSILON);
                }

                if (!sla_support_points.empty())
                    m_sla_support_points.insert({ object_id, sla_support_points });
            }
        }
    }
    
    void _BBS_3MF_Importer::_extract_sla_drain_holes_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat)
    {
        if (stat.m_uncomp_size > 0) {
            std::string buffer(size_t(stat.m_uncomp_size), 0);
            mz_bool res = mz_zip_reader_extract_file_to_mem(&archive, stat.m_filename, (void*)buffer.data(), (size_t)stat.m_uncomp_size, 0);
            if (res == 0) {
                add_error("Error while reading sla support points data to buffer");
                return;
            }
            
            if (buffer.back() == '\n')
                buffer.pop_back();
            
            std::vector<std::string> objects;
            boost::split(objects, buffer, boost::is_any_of("\n"), boost::token_compress_off);
            
            // Info on format versioning - see 3mf.hpp
            int version = 0;
            std::string key("drain_holes_format_version=");
            if (!objects.empty() && objects[0].find(key) != std::string::npos) {
                objects[0].erase(objects[0].begin(), objects[0].begin() + long(key.size())); // removes the string
                version = std::stoi(objects[0]);
                objects.erase(objects.begin()); // pop the header
            }
            
            for (const std::string& object : objects) {
                std::vector<std::string> object_data;
                boost::split(object_data, object, boost::is_any_of("|"), boost::token_compress_off);
                
                if (object_data.size() != 2) {
                    add_error("Error while reading object data");
                    continue;
                }
                
                std::vector<std::string> object_data_id;
                boost::split(object_data_id, object_data[0], boost::is_any_of("="), boost::token_compress_off);
                if (object_data_id.size() != 2) {
                    add_error("Error while reading object id");
                    continue;
                }
                
                int object_id = std::atoi(object_data_id[1].c_str());
                if (object_id == 0) {
                    add_error("Found invalid object id");
                    continue;
                }
                
                IdToSlaDrainHolesMap::iterator object_item = m_sla_drain_holes.find(object_id);
                if (object_item != m_sla_drain_holes.end()) {
                    add_error("Found duplicated SLA drain holes");
                    continue;
                }
                
                std::vector<std::string> object_data_points;
                boost::split(object_data_points, object_data[1], boost::is_any_of(" "), boost::token_compress_off);
                
                sla::DrainHoles sla_drain_holes;

                if (version == 1) {
                    for (unsigned int i=0; i<object_data_points.size(); i+=8)
                        sla_drain_holes.emplace_back(Vec3f{float(std::atof(object_data_points[i+0].c_str())),
                                                      float(std::atof(object_data_points[i+1].c_str())),
                                                      float(std::atof(object_data_points[i+2].c_str()))},
                                                     Vec3f{float(std::atof(object_data_points[i+3].c_str())),
                                                      float(std::atof(object_data_points[i+4].c_str())),
                                                      float(std::atof(object_data_points[i+5].c_str()))},
                                                      float(std::atof(object_data_points[i+6].c_str())),
                                                      float(std::atof(object_data_points[i+7].c_str())));
                }

                // The holes are saved elevated above the mesh and deeper (bad idea indeed).
                // This is retained for compatibility.
                // Place the hole to the mesh and make it shallower to compensate.
                // The offset is 1 mm above the mesh.
                for (sla::DrainHole& hole : sla_drain_holes) {
                    hole.pos += hole.normal.normalized();
                    hole.height -= 1.f;
                }
                
                if (!sla_drain_holes.empty())
                    m_sla_drain_holes.insert({ object_id, sla_drain_holes });
            }
        }
    }

    bool _BBS_3MF_Importer::_extract_model_config_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat, Model& model)
    {
        if (stat.m_uncomp_size == 0) {
            add_error("Found invalid size");
            return false;
        }

        _destroy_xml_parser();

        m_xml_parser = XML_ParserCreate(nullptr);
        if (m_xml_parser == nullptr) {
            add_error("Unable to create parser");
            return false;
        }

        XML_SetUserData(m_xml_parser, (void*)this);
        XML_SetElementHandler(m_xml_parser, _BBS_3MF_Importer::_handle_start_config_xml_element, _BBS_3MF_Importer::_handle_end_config_xml_element);

        void* parser_buffer = XML_GetBuffer(m_xml_parser, (int)stat.m_uncomp_size);
        if (parser_buffer == nullptr) {
            add_error("Unable to create buffer");
            return false;
        }

        mz_bool res = mz_zip_reader_extract_file_to_mem(&archive, stat.m_filename, parser_buffer, (size_t)stat.m_uncomp_size, 0);
        if (res == 0) {
            add_error("Error while reading config data to buffer");
            return false;
        }

        if (!XML_ParseBuffer(m_xml_parser, (int)stat.m_uncomp_size, 1)) {
            char error_buf[1024];
            ::sprintf(error_buf, "Error (%s) while parsing xml file at line %d", XML_ErrorString(XML_GetErrorCode(m_xml_parser)), (int)XML_GetCurrentLineNumber(m_xml_parser));
            add_error(error_buf);
            return false;
        }

        return true;
    }

    void _BBS_3MF_Importer::_extract_custom_gcode_per_print_z_from_archive(::mz_zip_archive &archive, const mz_zip_archive_file_stat &stat)
    {
        if (stat.m_uncomp_size > 0) {
            std::string buffer((size_t)stat.m_uncomp_size, 0);
            mz_bool res = mz_zip_reader_extract_file_to_mem(&archive, stat.m_filename, (void*)buffer.data(), (size_t)stat.m_uncomp_size, 0);
            if (res == 0) {
                add_error("Error while reading custom Gcodes per height data to buffer");
                return;
            }

            std::istringstream iss(buffer); // wrap returned xml to istringstream
            pt::ptree main_tree;
            pt::read_xml(iss, main_tree);

            if (main_tree.front().first != "custom_gcodes_per_print_z")
                return;
            pt::ptree code_tree = main_tree.front().second;

            m_model->custom_gcode_per_print_z.gcodes.clear();

            for (const auto& code : code_tree) {
                if (code.first == "mode") {
                    pt::ptree tree = code.second;
                    std::string mode = tree.get<std::string>("<xmlattr>.value");
                    m_model->custom_gcode_per_print_z.mode = mode == CustomGCode::SingleExtruderMode ? CustomGCode::Mode::SingleExtruder :
                                                             mode == CustomGCode::MultiAsSingleMode  ? CustomGCode::Mode::MultiAsSingle  :
                                                             CustomGCode::Mode::MultiExtruder;
                }
                if (code.first != "code")
                    continue;

                pt::ptree tree = code.second;
                double print_z          = tree.get<double>      ("<xmlattr>.print_z" );
                int extruder            = tree.get<int>         ("<xmlattr>.extruder");
                std::string color       = tree.get<std::string> ("<xmlattr>.color"   );

                CustomGCode::Type   type;
                std::string         extra;
                pt::ptree attr_tree = tree.find("<xmlattr>")->second;
                if (attr_tree.find("type") == attr_tree.not_found()) {
                    // It means that data was saved in old version (2.2.0 and older) of BambuSlicer
                    // read old data ... 
                    std::string gcode       = tree.get<std::string> ("<xmlattr>.gcode");
                    // ... and interpret them to the new data
                    type  = gcode == "M600"           ? CustomGCode::ColorChange : 
                            gcode == "M601"           ? CustomGCode::PausePrint  :   
                            gcode == "tool_change"    ? CustomGCode::ToolChange  :   CustomGCode::Custom;
                    extra = type == CustomGCode::PausePrint ? color :
                            type == CustomGCode::Custom     ? gcode : "";
                }
                else {
                    type  = static_cast<CustomGCode::Type>(tree.get<int>("<xmlattr>.type"));
                    extra = tree.get<std::string>("<xmlattr>.extra");
                }
                m_model->custom_gcode_per_print_z.gcodes.push_back(CustomGCode::Item{print_z, type, extruder, color, extra}) ;
            }
        }
    }

    bool _BBS_3MF_Importer::_extract_slice_info_config_file_from_archive(mz_zip_archive& archive, const mz_zip_archive_file_stat& stat, Model& model)
    {
        if (stat.m_uncomp_size == 0) {
            add_error("Found invalid size");
            return false;
        }

        _destroy_xml_parser();

        m_xml_parser = XML_ParserCreate(nullptr);
        if (m_xml_parser == nullptr) {
            add_error("Unable to create parser");
            return false;
        }

        XML_SetUserData(m_xml_parser, (void*)this);
        XML_SetElementHandler(m_xml_parser, _BBS_3MF_Importer::_handle_start_config_xml_element, _BBS_3MF_Importer::_handle_end_config_xml_element);

        void* parser_buffer = XML_GetBuffer(m_xml_parser, (int)stat.m_uncomp_size);
        if (parser_buffer == nullptr) {
            add_error("Unable to create buffer");
            return false;
        }

        mz_bool res = mz_zip_reader_extract_file_to_mem(&archive, stat.m_filename, parser_buffer, (size_t)stat.m_uncomp_size, 0);
        if (res == 0) {
            add_error("Error while reading config data to buffer");
            return false;
        }

        if (!XML_ParseBuffer(m_xml_parser, (int)stat.m_uncomp_size, 1)) {
            char error_buf[1024];
            ::sprintf(error_buf, "Error (%s) while parsing xml file at line %d", XML_ErrorString(XML_GetErrorCode(m_xml_parser)), (int)XML_GetCurrentLineNumber(m_xml_parser));
            add_error(error_buf);
            return false;
        }

        return true;
    }

    void _BBS_3MF_Importer::_handle_start_model_xml_element(const char* name, const char** attributes)
    {
        if (m_xml_parser == nullptr)
            return;

        bool res = true;
        unsigned int num_attributes = (unsigned int)XML_GetSpecifiedAttributeCount(m_xml_parser);

        if (::strcmp(MODEL_TAG, name) == 0)
            res = _handle_start_model(attributes, num_attributes);
        else if (::strcmp(RESOURCES_TAG, name) == 0)
            res = _handle_start_resources(attributes, num_attributes);
        else if (::strcmp(OBJECT_TAG, name) == 0)
            res = _handle_start_object(attributes, num_attributes);
        else if (::strcmp(MESH_TAG, name) == 0)
            res = _handle_start_mesh(attributes, num_attributes);
        else if (::strcmp(VERTICES_TAG, name) == 0)
            res = _handle_start_vertices(attributes, num_attributes);
        else if (::strcmp(VERTEX_TAG, name) == 0)
            res = _handle_start_vertex(attributes, num_attributes);
        else if (::strcmp(TRIANGLES_TAG, name) == 0)
            res = _handle_start_triangles(attributes, num_attributes);
        else if (::strcmp(TRIANGLE_TAG, name) == 0)
            res = _handle_start_triangle(attributes, num_attributes);
        //else if (m_mesh_only)
        //    res = true;
        else if (::strcmp(COMPONENTS_TAG, name) == 0)
            res = _handle_start_components(attributes, num_attributes);
        else if (::strcmp(COMPONENT_TAG, name) == 0)
            res = _handle_start_component(attributes, num_attributes);
        else if (::strcmp(BUILD_TAG, name) == 0)
            res = _handle_start_build(attributes, num_attributes);
        else if (::strcmp(ITEM_TAG, name) == 0)
            res = _handle_start_item(attributes, num_attributes);
        else if (::strcmp(METADATA_TAG, name) == 0)
            res = _handle_start_metadata(attributes, num_attributes);

        if (!res)
            _stop_xml_parser();
    }

    void _BBS_3MF_Importer::_handle_end_model_xml_element(const char* name)
    {
        if (m_xml_parser == nullptr)
            return;

        bool res = true;

        if (::strcmp(MODEL_TAG, name) == 0)
            res = _handle_end_model();
        else if (::strcmp(RESOURCES_TAG, name) == 0)
            res = _handle_end_resources();
        else if (::strcmp(OBJECT_TAG, name) == 0)
            res = _handle_end_object();
        else if (::strcmp(MESH_TAG, name) == 0)
            res = _handle_end_mesh();
        else if (::strcmp(VERTICES_TAG, name) == 0)
            res = _handle_end_vertices();
        else if (::strcmp(VERTEX_TAG, name) == 0)
            res = _handle_end_vertex();
        else if (::strcmp(TRIANGLES_TAG, name) == 0)
            res = _handle_end_triangles();
        else if (::strcmp(TRIANGLE_TAG, name) == 0)
            res = _handle_end_triangle();
        // else if (m_mesh_only)
        //     res = true;
        else if (::strcmp(COMPONENTS_TAG, name) == 0)
            res = _handle_end_components();
        else if (::strcmp(COMPONENT_TAG, name) == 0)
            res = _handle_end_component();
        else if (::strcmp(BUILD_TAG, name) == 0)
            res = _handle_end_build();
        else if (::strcmp(ITEM_TAG, name) == 0)
            res = _handle_end_item();
        else if (::strcmp(METADATA_TAG, name) == 0)
            res = _handle_end_metadata();

        if (!res)
            _stop_xml_parser();
    }

    void _BBS_3MF_Importer::_handle_model_xml_characters(const XML_Char* s, int len)
    {
        // if (!m_mesh_only)
        m_curr_characters.append(s, len);
    }

    void _BBS_3MF_Importer::_handle_start_config_xml_element(const char* name, const char** attributes)
    {
        if (m_xml_parser == nullptr)
            return;

        bool res = true;
        unsigned int num_attributes = (unsigned int)XML_GetSpecifiedAttributeCount(m_xml_parser);

        if (::strcmp(CONFIG_TAG, name) == 0)
            res = _handle_start_config(attributes, num_attributes);
        else if (::strcmp(OBJECT_TAG, name) == 0)
            res = _handle_start_config_object(attributes, num_attributes);
        else if (::strcmp(VOLUME_TAG, name) == 0)
            res = _handle_start_config_volume(attributes, num_attributes);
        else if (::strcmp(PART_TAG, name) == 0)
            res = _handle_start_config_volume(attributes, num_attributes);
        else if (::strcmp(MESH_TAG, name) == 0)
            res = _handle_start_config_volume_mesh(attributes, num_attributes);
        else if (::strcmp(METADATA_TAG, name) == 0)
            res = _handle_start_config_metadata(attributes, num_attributes);
        else if (::strcmp(PLATE_TAG, name) == 0)
            res = _handle_start_config_plater(attributes, num_attributes);
        else if (::strcmp(INSTANCE_TAG, name) == 0)
            res = _handle_start_config_plater_instance(attributes, num_attributes);
        else if (::strcmp(ASSEMBLE_TAG, name) == 0)
            res = _handle_start_assemble(attributes, num_attributes);
        else if (::strcmp(ASSEMBLE_ITEM_TAG, name) == 0)
            res = _handle_start_assemble_item(attributes, num_attributes);

        if (!res)
            _stop_xml_parser();
    }

    void _BBS_3MF_Importer::_handle_end_config_xml_element(const char* name)
    {
        if (m_xml_parser == nullptr)
            return;

        bool res = true;

        if (::strcmp(CONFIG_TAG, name) == 0)
            res = _handle_end_config();
        else if (::strcmp(OBJECT_TAG, name) == 0)
            res = _handle_end_config_object();
        else if (::strcmp(VOLUME_TAG, name) == 0)
            res = _handle_end_config_volume();
        else if (::strcmp(PART_TAG, name) == 0)
            res = _handle_end_config_volume();
        else if (::strcmp(MESH_TAG, name) == 0)
            res = _handle_end_config_volume_mesh();
        else if (::strcmp(METADATA_TAG, name) == 0)
            res = _handle_end_config_metadata();
        else if (::strcmp(PLATE_TAG, name) == 0)
            res = _handle_end_config_plater();
        else if (::strcmp(INSTANCE_TAG, name) == 0)
            res = _handle_end_config_plater_instance();
        else if (::strcmp(ASSEMBLE_TAG, name) == 0)
            res = _handle_end_assemble();
        else if (::strcmp(ASSEMBLE_ITEM_TAG, name) == 0)
            res = _handle_end_assemble_item();

        if (!res)
            _stop_xml_parser();
    }

    bool _BBS_3MF_Importer::_handle_start_model(const char** attributes, unsigned int num_attributes)
    {
        m_unit_factor = bbs_get_unit_factor(bbs_get_attribute_value_string(attributes, num_attributes, UNIT_ATTR));
        return true;
    }

    bool _BBS_3MF_Importer::_handle_end_model()
    {
        // deletes all non-built or non-instanced objects
        for (const IdToModelObjectMap::value_type& object : m_objects) {
            if (object.second >= int(m_model->objects.size())) {
                add_error("Unable to find object");
                return false;
            }
            ModelObject *model_object = m_model->objects[object.second];
            if (model_object != nullptr && model_object->instances.size() == 0)
                m_model->delete_object(model_object);
        }

        if (m_version == 0) {
            // if the 3mf was not produced by BambuSlicer and there is only one object,
            // set the object name to match the filename
            if (m_model->objects.size() == 1)
                m_model->objects.front()->name = m_name;
        }

        // applies instances' matrices
        for (Instance& instance : m_instances) {
            if (instance.instance != nullptr && instance.instance->get_object() != nullptr)
                // apply the transform to the instance
                _apply_transform(*instance.instance, instance.transform);
        }

        return true;
    }

    bool _BBS_3MF_Importer::_handle_start_resources(const char** attributes, unsigned int num_attributes)
    {
        // do nothing
        return true;
    }

    bool _BBS_3MF_Importer::_handle_end_resources()
    {
        // do nothing
        return true;
    }

    bool _BBS_3MF_Importer::_handle_start_object(const char** attributes, unsigned int num_attributes)
    {
        // reset current data
        m_curr_object.reset();

        if (bbs_is_valid_object_type(bbs_get_attribute_value_string(attributes, num_attributes, TYPE_ATTR))) {
            // if (m_mesh_only) {
            //     m_curr_object.id = bbs_get_attribute_value_int(attributes, num_attributes, ID_ATTR);
            //     return true;
            // }
            // create new object (it may be removed later if no instances are generated from it)
            m_curr_object.model_object_idx = (int)m_model->objects.size();
            m_curr_object.object = m_model->add_object();
            if (m_curr_object.object == nullptr) {
                add_error("Unable to create object");
                return false;
            }

            // set object data
            m_curr_object.object->name = bbs_get_attribute_value_string(attributes, num_attributes, NAME_ATTR);
            if (m_curr_object.object->name.empty())
                m_curr_object.object->name = m_name + "_" + std::to_string(m_model->objects.size());

            m_curr_object.id = bbs_get_attribute_value_int(attributes, num_attributes, ID_ATTR);
        }

        return true;
    }

    bool _BBS_3MF_Importer::_handle_end_object()
    {
        // if (m_mesh_only) {
        //     if (m_curr_object.geometry.empty()) {
        //         return false;
        //     }
        //     m_orig_geometries.insert({ m_curr_object.id, std::move(m_curr_object.geometry) });
        //     return true;
       //  }
        if (m_curr_object.object != nullptr) {
            if (m_curr_object.geometry.empty() && m_load_restore) {
                // load mesh from split file
                auto it = m_object_id_map.find(m_curr_object.id);
                if (it != m_object_id_map.end()) {
                    std::string file(m_model->get_backup_path() + "/mesh_" + boost::lexical_cast<std::string>(it->second) + ".xml");
                    // load into m_curr_object.geometry
                    _extract_model_from_file(file);
                } else {
                    add_error("not found mesh object " + boost::lexical_cast<std::string>(m_curr_object.id) + " in object_map");
                }
                // use mesh from origin project file
                // TODO: id match
                // m_curr_object.geometry.swap(m_orig_geometries[m_curr_object.id]);
                // m_orig_geometries.erase(m_curr_object.id);
            }

            if (m_curr_object.geometry.empty()) {
                // no geometry defined
                // remove the object from the model
                m_model->delete_object(m_curr_object.object);

                if (m_curr_object.components.empty()) {
                    // no components defined -> invalid object, delete it
                    IdToModelObjectMap::iterator object_item = m_objects.find(m_curr_object.id);
                    if (object_item != m_objects.end())
                        m_objects.erase(object_item);

                    IdToAliasesMap::iterator alias_item = m_objects_aliases.find(m_curr_object.id);
                    if (alias_item != m_objects_aliases.end())
                        m_objects_aliases.erase(alias_item);
                }
                else
                    // adds components to aliases
                    m_objects_aliases.insert({ m_curr_object.id, m_curr_object.components });
            }
            else {
                // geometry defined, store it for later use
                m_geometries.insert({ m_curr_object.id, std::move(m_curr_object.geometry) });

                // stores the object for later use
                if (m_objects.find(m_curr_object.id) == m_objects.end()) {
                    m_objects.insert({ m_curr_object.id, m_curr_object.model_object_idx });
                    m_objects_aliases.insert({ m_curr_object.id, { 1, Component(m_curr_object.id) } }); // aliases itself
                }
                else {
                    add_error("Found object with duplicate id");
                    return false;
                }
            }
        }

        return true;
    }

    bool _BBS_3MF_Importer::_handle_start_mesh(const char** attributes, unsigned int num_attributes)
    {
        // reset current geometry
        m_curr_object.geometry.reset();
        return true;
    }

    bool _BBS_3MF_Importer::_handle_end_mesh()
    {
        // do nothing
        return true;
    }

    bool _BBS_3MF_Importer::_handle_start_vertices(const char** attributes, unsigned int num_attributes)
    {
        // reset current vertices
        m_curr_object.geometry.vertices.clear();
        return true;
    }

    bool _BBS_3MF_Importer::_handle_end_vertices()
    {
        // do nothing
        return true;
    }

    bool _BBS_3MF_Importer::_handle_start_vertex(const char** attributes, unsigned int num_attributes)
    {
        // appends the vertex coordinates
        // missing values are set equal to ZERO
        m_curr_object.geometry.vertices.emplace_back(
            m_unit_factor * bbs_get_attribute_value_float(attributes, num_attributes, X_ATTR),
            m_unit_factor * bbs_get_attribute_value_float(attributes, num_attributes, Y_ATTR),
            m_unit_factor * bbs_get_attribute_value_float(attributes, num_attributes, Z_ATTR));
        return true;
    }

    bool _BBS_3MF_Importer::_handle_end_vertex()
    {
        // do nothing
        return true;
    }

    bool _BBS_3MF_Importer::_handle_start_triangles(const char** attributes, unsigned int num_attributes)
    {
        // reset current triangles
        m_curr_object.geometry.triangles.clear();
        return true;
    }

    bool _BBS_3MF_Importer::_handle_end_triangles()
    {
        // do nothing
        return true;
    }

    bool _BBS_3MF_Importer::_handle_start_triangle(const char** attributes, unsigned int num_attributes)
    {
        // we are ignoring the following attributes:
        // p1
        // p2
        // p3
        // pid
        // see specifications

        // appends the triangle's vertices indices
        // missing values are set equal to ZERO
        m_curr_object.geometry.triangles.emplace_back(
            bbs_get_attribute_value_int(attributes, num_attributes, V1_ATTR),
            bbs_get_attribute_value_int(attributes, num_attributes, V2_ATTR),
            bbs_get_attribute_value_int(attributes, num_attributes, V3_ATTR));

        m_curr_object.geometry.custom_supports.push_back(bbs_get_attribute_value_string(attributes, num_attributes, CUSTOM_SUPPORTS_ATTR));
        m_curr_object.geometry.custom_seam.push_back(bbs_get_attribute_value_string(attributes, num_attributes, CUSTOM_SEAM_ATTR));
        m_curr_object.geometry.mmu_segmentation.push_back(bbs_get_attribute_value_string(attributes, num_attributes, MMU_SEGMENTATION_ATTR));
        return true;
    }

    bool _BBS_3MF_Importer::_handle_end_triangle()
    {
        // do nothing
        return true;
    }

    bool _BBS_3MF_Importer::_handle_start_components(const char** attributes, unsigned int num_attributes)
    {
        // reset current components
        m_curr_object.components.clear();
        return true;
    }

    bool _BBS_3MF_Importer::_handle_end_components()
    {
        // do nothing
        return true;
    }

    bool _BBS_3MF_Importer::_handle_start_component(const char** attributes, unsigned int num_attributes)
    {
        int object_id = bbs_get_attribute_value_int(attributes, num_attributes, OBJECTID_ATTR);
        Transform3d transform = bbs_get_transform_from_3mf_specs_string(bbs_get_attribute_value_string(attributes, num_attributes, TRANSFORM_ATTR));

        IdToModelObjectMap::iterator object_item = m_objects.find(object_id);
        if (object_item == m_objects.end()) {
            IdToAliasesMap::iterator alias_item = m_objects_aliases.find(object_id);
            if (alias_item == m_objects_aliases.end()) {
                add_error("Found component with invalid object id");
                return false;
            }
        }

        m_curr_object.components.emplace_back(object_id, transform);

        return true;
    }

    bool _BBS_3MF_Importer::_handle_end_component()
    {
        // do nothing
        return true;
    }

    bool _BBS_3MF_Importer::_handle_start_build(const char** attributes, unsigned int num_attributes)
    {
        // do nothing
        return true;
    }

    bool _BBS_3MF_Importer::_handle_end_build()
    {
        // do nothing
        return true;
    }

    bool _BBS_3MF_Importer::_handle_start_item(const char** attributes, unsigned int num_attributes)
    {
        // we are ignoring the following attributes
        // thumbnail
        // partnumber
        // pid
        // pindex
        // see specifications

        int object_id = bbs_get_attribute_value_int(attributes, num_attributes, OBJECTID_ATTR);
        Transform3d transform = bbs_get_transform_from_3mf_specs_string(bbs_get_attribute_value_string(attributes, num_attributes, TRANSFORM_ATTR));
        int printable = bbs_get_attribute_value_bool(attributes, num_attributes, PRINTABLE_ATTR);

        return _create_object_instance(object_id, transform, printable, 1);
    }

    bool _BBS_3MF_Importer::_handle_end_item()
    {
        // do nothing
        return true;
    }

    bool _BBS_3MF_Importer::_handle_start_metadata(const char** attributes, unsigned int num_attributes)
    {
        m_curr_characters.clear();

        std::string name = bbs_get_attribute_value_string(attributes, num_attributes, NAME_ATTR);
        if (!name.empty())
            m_curr_metadata_name = name;

        return true;
    }

    inline static void check_painting_version(unsigned int loaded_version, unsigned int highest_supported_version, const std::string &error_msg)
    {
        if (loaded_version > highest_supported_version)
            throw version_error(error_msg);
    }

    bool _BBS_3MF_Importer::_handle_end_metadata()
    {
        if ((m_curr_metadata_name == BBS_3MF_VERSION)||(m_curr_metadata_name == BBS_PRUSA_VERSION)) {
            if (m_curr_metadata_name == BBS_3MF_VERSION)
                m_is_bbl_3mf = true;
            m_version = (unsigned int)atoi(m_curr_characters.c_str());
            if (m_check_version && (m_version > VERSION_BBS_3MF_COMPATIBLE)) {
                // std::string msg = _(L("The selected 3mf file has been saved with a newer version of " + std::string(SLIC3R_APP_NAME) + " and is not compatible."));
                // throw version_error(msg.c_str());
                const std::string msg = (boost::format(_(L("The selected 3mf file has been saved with a newer version of %1% and is not compatible."))) % std::string(SLIC3R_APP_NAME)).str();
                throw version_error(msg);
            }
        } else if (m_curr_metadata_name == "Application") {
            // Generator application of the 3MF.
            // SLIC3R_APP_KEY - SLIC3R_VERSION
            if (boost::starts_with(m_curr_characters, "BambuSlicer-"))
                m_bambuslicer_generator_version = Semver::parse(m_curr_characters.substr(12));
        } else if (m_curr_metadata_name == SLIC3RPE_FDM_SUPPORTS_PAINTING_VERSION) {
            m_fdm_supports_painting_version = (unsigned int) atoi(m_curr_characters.c_str());
            check_painting_version(m_fdm_supports_painting_version, FDM_SUPPORTS_PAINTING_VERSION,
                _(L("The selected 3MF contains FDM supports painted object using a newer version of BambuSlicer and is not compatible.")));
        } else if (m_curr_metadata_name == SLIC3RPE_SEAM_PAINTING_VERSION) {
            m_seam_painting_version = (unsigned int) atoi(m_curr_characters.c_str());
            check_painting_version(m_seam_painting_version, SEAM_PAINTING_VERSION,
                _(L("The selected 3MF contains seam painted object using a newer version of BambuSlicer and is not compatible.")));
        } else if (m_curr_metadata_name == SLIC3RPE_MM_PAINTING_VERSION) {
            m_mm_painting_version = (unsigned int) atoi(m_curr_characters.c_str());
            check_painting_version(m_mm_painting_version, MM_PAINTING_VERSION,
                _(L("The selected 3MF contains multi-material painted object using a newer version of BambuSlicer and is not compatible.")));
        }

        return true;
    }

    bool _BBS_3MF_Importer::_create_object_instance(int object_id, const Transform3d& transform, const bool printable, unsigned int recur_counter)
    {
        static const unsigned int MAX_RECURSIONS = 10;

        // escape from circular aliasing
        if (recur_counter > MAX_RECURSIONS) {
            add_error("Too many recursions");
            return false;
        }

        IdToAliasesMap::iterator it = m_objects_aliases.find(object_id);
        if (it == m_objects_aliases.end()) {
            add_error("Found item with invalid object id " + std::to_string(object_id));
            return false;
        }

        if (it->second.size() == 1 && it->second[0].object_id == object_id) {
            // aliasing to itself

            IdToModelObjectMap::iterator object_item = m_objects.find(object_id);
            if (object_item == m_objects.end() || object_item->second == -1) {
                add_error("Found invalid object");
                return false;
            }
            else {
                ModelInstance* instance = m_model->objects[object_item->second]->add_instance();
                if (instance == nullptr) {
                    add_error("Unable to add object instance");
                    return false;
                }
                instance->printable = printable;

                m_instances.emplace_back(instance, transform);
            }
        }
        else {
            // recursively process nested components
            for (const Component& component : it->second) {
                if (!_create_object_instance(component.object_id, transform * component.transform, printable, recur_counter + 1))
                    return false;
            }
        }

        return true;
    }

    void _BBS_3MF_Importer::_apply_transform(ModelInstance& instance, const Transform3d& transform)
    {
        Slic3r::Geometry::Transformation t(transform);
        // invalid scale value, return
        if (!t.get_scaling_factor().all())
            return;

        instance.set_transformation(t);
    }

    bool _BBS_3MF_Importer::_handle_start_config(const char** attributes, unsigned int num_attributes)
    {
        // do nothing
        return true;
    }

    bool _BBS_3MF_Importer::_handle_end_config()
    {
        // do nothing
        return true;
    }

    bool _BBS_3MF_Importer::_handle_start_config_object(const char** attributes, unsigned int num_attributes)
    {
        int object_id = bbs_get_attribute_value_int(attributes, num_attributes, ID_ATTR);
        IdToMetadataMap::iterator object_item = m_objects_metadata.find(object_id);
        if (object_item != m_objects_metadata.end()) {
            add_error("Found duplicated object id");
            return false;
        }

        // Added because of github #3435, currently not used by BambuSlicer
        // int instances_count_id = bbs_get_attribute_value_int(attributes, num_attributes, INSTANCESCOUNT_ATTR);

        m_objects_metadata.insert({ object_id, ObjectMetadata() });
        m_curr_config.object_id = object_id;
        return true;
    }

    bool _BBS_3MF_Importer::_handle_end_config_object()
    {
        // do nothing
        return true;
    }

    bool _BBS_3MF_Importer::_handle_start_config_volume(const char** attributes, unsigned int num_attributes)
    {
        IdToMetadataMap::iterator object = m_objects_metadata.find(m_curr_config.object_id);
        if (object == m_objects_metadata.end()) {
            add_error("Cannot assign volume to a valid object");
            return false;
        }

        m_curr_config.volume_id = (int)object->second.volumes.size();

        unsigned int first_triangle_id = (unsigned int)bbs_get_attribute_value_int(attributes, num_attributes, FIRST_TRIANGLE_ID_ATTR);
        unsigned int last_triangle_id = (unsigned int)bbs_get_attribute_value_int(attributes, num_attributes, LAST_TRIANGLE_ID_ATTR);

        object->second.volumes.emplace_back(first_triangle_id, last_triangle_id);
        return true;
    }

    bool _BBS_3MF_Importer::_handle_start_config_volume_mesh(const char** attributes, unsigned int num_attributes)
    {
        IdToMetadataMap::iterator object = m_objects_metadata.find(m_curr_config.object_id);
        if (object == m_objects_metadata.end()) {
            add_error("Cannot assign volume mesh to a valid object");
            return false;
        }
        if (object->second.volumes.empty()) {
            add_error("Cannot assign mesh to a valid olume");
            return false;
        }

        ObjectMetadata::VolumeMetadata& volume = object->second.volumes.back();

        int edges_fixed         = bbs_get_attribute_value_int(attributes, num_attributes, MESH_STAT_EDGES_FIXED       );
        int degenerate_facets   = bbs_get_attribute_value_int(attributes, num_attributes, MESH_STAT_DEGENERATED_FACETS);
        int facets_removed      = bbs_get_attribute_value_int(attributes, num_attributes, MESH_STAT_FACETS_REMOVED    );
        int facets_reversed     = bbs_get_attribute_value_int(attributes, num_attributes, MESH_STAT_FACETS_RESERVED   );
        int backwards_edges     = bbs_get_attribute_value_int(attributes, num_attributes, MESH_STAT_BACKWARDS_EDGES   );

        volume.mesh_stats = { edges_fixed, degenerate_facets, facets_removed, facets_reversed, backwards_edges };

        return true;
    }

    bool _BBS_3MF_Importer::_handle_end_config_volume()
    {
        // do nothing
        return true;
    }

    bool _BBS_3MF_Importer::_handle_end_config_volume_mesh()
    {
        // do nothing
        return true;
    }

    bool _BBS_3MF_Importer::_handle_start_config_metadata(const char** attributes, unsigned int num_attributes)
    {
        std::string type = bbs_get_attribute_value_string(attributes, num_attributes, TYPE_ATTR);
        std::string key = bbs_get_attribute_value_string(attributes, num_attributes, KEY_ATTR);
        std::string value = bbs_get_attribute_value_string(attributes, num_attributes, VALUE_ATTR);

        if ((m_curr_plater == nullptr)&&!m_parsing_slice_info)
        {
            IdToMetadataMap::iterator object = m_objects_metadata.find(m_curr_config.object_id);
            if (object == m_objects_metadata.end()) {
                add_error("Cannot assign metadata to valid object id");
                return false;
            }
            if (type == OBJECT_TYPE)
                object->second.metadata.emplace_back(key, value);
            else if (type == VOLUME_TYPE) {
                if (size_t(m_curr_config.volume_id) < object->second.volumes.size())
                    object->second.volumes[m_curr_config.volume_id].metadata.emplace_back(key, value);
            }
            else if (type == PART_TYPE) {
                if (size_t(m_curr_config.volume_id) < object->second.volumes.size())
                    object->second.volumes[m_curr_config.volume_id].metadata.emplace_back(key, value);
            }
            else {
                add_error("Found invalid metadata type");
                return false;
            }
        }
        else
        {
            //plater
            if (key == PLATERID_ATTR)
            {
                m_curr_plater->plate_index = atoi(value.c_str());
            }
            else if (key == LOCK_ATTR)
            {
                std::istringstream(value) >> std::boolalpha >> m_curr_plater->locked;
            }
            else if (key == INSTANCEID_ATTR)
            {
                m_curr_instance.instance_id = atoi(value.c_str());
            }
            else if (key == OBJECT_ID_ATTR)
            {
                int obj_id = atoi(value.c_str());
                IdToModelObjectMap::iterator object_item = m_objects.find(obj_id);
                if (object_item != m_objects.end())
                    m_curr_instance.object_id = object_item->second;
                else
                    m_curr_instance.object_id = -1;
            }
            else if (key == PLATE_IDX_ATTR)
            {
                int plate_index = atoi(value.c_str());
                std::map<int, PlateData*>::iterator it = m_plater_data.find(plate_index);
                if (it != m_plater_data.end())
                    m_curr_plater = it->second;
            }
            else if (key == SLICE_PREDICTION_ATTR)
            {
                if (m_curr_plater)
                    m_curr_plater->gcode_prediction = value;
            }
            else if (key == SLICE_WEIGHT_ATTR)
            {
                if (m_curr_plater)
                    m_curr_plater->gcode_weight = value;
            }
            else if (key == OUTSIDE_ATTR)
            {
                if (m_curr_plater)
                    std::istringstream(value) >> std::boolalpha >> m_curr_plater->toolpath_outside;
            }
        }

        return true;
    }

    bool _BBS_3MF_Importer::_handle_end_config_metadata()
    {
        // do nothing
        return true;
    }

    bool _BBS_3MF_Importer::_handle_start_config_plater(const char** attributes, unsigned int num_attributes)
    {
        if (!m_parsing_slice_info) {
            m_curr_plater = new PlateData();
        }
 
        return true;
    }

    bool _BBS_3MF_Importer::_handle_end_config_plater()
    {
        if (!m_curr_plater)
        {
            add_error("don't find plater created before");
            return false;
        }
        m_plater_data.emplace(m_curr_plater->plate_index, m_curr_plater);
        m_curr_plater = nullptr;
        return true;
    }

    bool _BBS_3MF_Importer::_handle_start_config_plater_instance(const char** attributes, unsigned int num_attributes)
    {
        if (!m_curr_plater)
        {
            add_error("don't find plater created before");
            return false;
        }

        //do nothing
        return true;
    }

    bool _BBS_3MF_Importer::_handle_end_config_plater_instance()
    {
        if (!m_curr_plater)
        {
            add_error("don't find plater created before");
            return false;
        }
        if ((m_curr_instance.object_id == -1) || (m_curr_instance.object_id == -1))
        {
            add_error("invalid object id/instance id");
            return false;
        }

        m_curr_plater->objects_and_instances.emplace_back(m_curr_instance.object_id, m_curr_instance.instance_id);
        m_curr_instance.object_id = m_curr_instance.instance_id = -1;
        return true;
    }

    bool _BBS_3MF_Importer::_handle_start_assemble(const char** attributes, unsigned int num_attributes)
    {
        return true;
    }

    bool _BBS_3MF_Importer::_handle_end_assemble()
    {
        //do nothing
        return true;
    }

    bool _BBS_3MF_Importer::_handle_start_assemble_item(const char** attributes, unsigned int num_attributes)
    {
        int object_id = bbs_get_attribute_value_int(attributes, num_attributes, OBJECT_ID_ATTR);
        int instance_id = bbs_get_attribute_value_int(attributes, num_attributes, INSTANCEID_ATTR) - object_id;

        IdToModelObjectMap::iterator object_item = m_objects.find(object_id);
        if (object_item != m_objects.end())
            object_id = object_item->second;
        Transform3d transform = bbs_get_transform_from_3mf_specs_string(bbs_get_attribute_value_string(attributes, num_attributes, TRANSFORM_ATTR));
        Vec3d ofs2ass = bbs_get_offset_from_3mf_specs_string(bbs_get_attribute_value_string(attributes, num_attributes, OFFSET_ATTR));
        if (object_id < m_model->objects.size()) {
            if (instance_id < m_model->objects[object_id]->instances.size()) {
                m_model->objects[object_id]->instances[instance_id]->set_assemble_from_transform(transform);
                m_model->objects[object_id]->instances[instance_id]->set_offset_to_assembly(ofs2ass);
            }
        }
        return true;
    }

    bool _BBS_3MF_Importer::_handle_end_assemble_item()
    {
        return true;
    }

    bool _BBS_3MF_Importer::_generate_volumes(ModelObject& object, const Geometry& geometry, const ObjectMetadata::VolumeMetadataList& volumes, ConfigSubstitutionContext& config_substitutions)
    {
        if (!object.volumes.empty()) {
            add_error("Found invalid volumes count");
            return false;
        }

        unsigned int geo_tri_count = (unsigned int)geometry.triangles.size();
        unsigned int renamed_volumes_count = 0;

        for (const ObjectMetadata::VolumeMetadata& volume_data : volumes) {
            if (geo_tri_count <= volume_data.first_triangle_id || geo_tri_count <= volume_data.last_triangle_id || volume_data.last_triangle_id < volume_data.first_triangle_id) {
                add_error("Found invalid triangle id");
                return false;
            }

            Transform3d volume_matrix_to_object = Transform3d::Identity();
            bool        has_transform 		    = false;
            // extract the volume transformation from the volume's metadata, if present
            for (const Metadata& metadata : volume_data.metadata) {
                if (metadata.key == MATRIX_KEY) {
                    volume_matrix_to_object = Slic3r::Geometry::transform3d_from_string(metadata.value);
                    has_transform 			= ! volume_matrix_to_object.isApprox(Transform3d::Identity(), 1e-10);
                    break;
                }
            }

            // splits volume out of imported geometry
            indexed_triangle_set its;
            its.indices.assign(geometry.triangles.begin() + volume_data.first_triangle_id, geometry.triangles.begin() + volume_data.last_triangle_id + 1);
            const size_t triangles_count = its.indices.size();
            if (triangles_count == 0) {
                add_error("An empty triangle mesh found");
                return false;
            }

            {
                int min_id = its.indices.front()[0];
                int max_id = min_id;
                for (const Vec3i& face : its.indices) {
                    for (const int tri_id : face) {
                        if (tri_id < 0 || tri_id >= int(geometry.vertices.size())) {
                            add_error("Found invalid vertex id");
                            return false;
                        }
                        min_id = std::min(min_id, tri_id);
                        max_id = std::max(max_id, tri_id);
                    }
                }
                its.vertices.assign(geometry.vertices.begin() + min_id, geometry.vertices.begin() + max_id + 1);

                // rebase indices to the current vertices list
                for (Vec3i& face : its.indices)
                    for (int& tri_id : face)
                        tri_id -= min_id;
            }

            if (m_bambuslicer_generator_version && 
                *m_bambuslicer_generator_version >= *Semver::parse("2.4.0-alpha1") &&
                *m_bambuslicer_generator_version < *Semver::parse("2.4.0-alpha3"))
                // BambuSlicer 2.4.0-alpha2 contained a bug, where all vertices of a single object were saved for each volume the object contained.
                // Remove the vertices, that are not referenced by any face.
                its_compactify_vertices(its, true);

            TriangleMesh triangle_mesh(std::move(its), volume_data.mesh_stats);

            if (m_version == 0) {
                // if the 3mf was not produced by BambuSlicer and there is only one instance,
                // bake the transformation into the geometry to allow the reload from disk command
                // to work properly
                if (object.instances.size() == 1) {
                    triangle_mesh.transform(object.instances.front()->get_transformation().get_matrix(), false);
                    object.instances.front()->set_transformation(Slic3r::Geometry::Transformation());
                    //FIXME do the mesh fixing?
                }
            }
            if (triangle_mesh.volume() < 0)
                triangle_mesh.flip_triangles();

			ModelVolume* volume = object.add_volume(std::move(triangle_mesh));
            // stores the volume matrix taken from the metadata, if present
            if (has_transform)
                volume->source.transform = Slic3r::Geometry::Transformation(volume_matrix_to_object);
            volume->calculate_convex_hull();

            // recreate custom supports, seam and mmu segmentation from previously loaded attribute
            volume->supported_facets.reserve(triangles_count);
            volume->seam_facets.reserve(triangles_count);
            volume->mmu_segmentation_facets.reserve(triangles_count);
            for (size_t i=0; i<triangles_count; ++i) {
                size_t index = volume_data.first_triangle_id + i;
                assert(index < geometry.custom_supports.size());
                assert(index < geometry.custom_seam.size());
                assert(index < geometry.mmu_segmentation.size());
                if (! geometry.custom_supports[index].empty())
                    volume->supported_facets.set_triangle_from_string(i, geometry.custom_supports[index]);
                if (! geometry.custom_seam[index].empty())
                    volume->seam_facets.set_triangle_from_string(i, geometry.custom_seam[index]);
                if (! geometry.mmu_segmentation[index].empty())
                    volume->mmu_segmentation_facets.set_triangle_from_string(i, geometry.mmu_segmentation[index]);
            }
            volume->supported_facets.shrink_to_fit();
            volume->seam_facets.shrink_to_fit();
            volume->mmu_segmentation_facets.shrink_to_fit();

            // apply the remaining volume's metadata
            for (const Metadata& metadata : volume_data.metadata) {
                if (metadata.key == NAME_KEY)
                    volume->name = metadata.value;
                else if ((metadata.key == MODIFIER_KEY) && (metadata.value == "1"))
					volume->set_type(ModelVolumeType::PARAMETER_MODIFIER);
                else if (metadata.key == VOLUME_TYPE_KEY)
                    volume->set_type(ModelVolume::type_from_string(metadata.value));
                else if (metadata.key == SOURCE_FILE_KEY)
                    volume->source.input_file = metadata.value;
                else if (metadata.key == SOURCE_OBJECT_ID_KEY)
                    volume->source.object_idx = ::atoi(metadata.value.c_str());
                else if (metadata.key == SOURCE_VOLUME_ID_KEY)
                    volume->source.volume_idx = ::atoi(metadata.value.c_str());
                else if (metadata.key == SOURCE_OFFSET_X_KEY)
                    volume->source.mesh_offset(0) = ::atof(metadata.value.c_str());
                else if (metadata.key == SOURCE_OFFSET_Y_KEY)
                    volume->source.mesh_offset(1) = ::atof(metadata.value.c_str());
                else if (metadata.key == SOURCE_OFFSET_Z_KEY)
                    volume->source.mesh_offset(2) = ::atof(metadata.value.c_str());
                else if (metadata.key == SOURCE_IN_INCHES)
                    volume->source.is_converted_from_inches = metadata.value == "1";
                else if (metadata.key == SOURCE_IN_METERS)
                    volume->source.is_converted_from_meters = metadata.value == "1";
                else
                    volume->config.set_deserialize(metadata.key, metadata.value, config_substitutions);
            }

            // this may happen for 3mf saved by 3rd part softwares
            if (volume->name.empty()) {
                volume->name = object.name;
                if (renamed_volumes_count > 0)
                    volume->name += "_" + std::to_string(renamed_volumes_count + 1);
                ++renamed_volumes_count;
            }
        }

        return true;
    }

    void XMLCALL _BBS_3MF_Importer::_handle_start_model_xml_element(void* userData, const char* name, const char** attributes)
    {
        _BBS_3MF_Importer* importer = (_BBS_3MF_Importer*)userData;
        if (importer != nullptr)
            importer->_handle_start_model_xml_element(name, attributes);
    }

    void XMLCALL _BBS_3MF_Importer::_handle_end_model_xml_element(void* userData, const char* name)
    {
        _BBS_3MF_Importer* importer = (_BBS_3MF_Importer*)userData;
        if (importer != nullptr)
            importer->_handle_end_model_xml_element(name);
    }

    void XMLCALL _BBS_3MF_Importer::_handle_model_xml_characters(void* userData, const XML_Char* s, int len)
    {
        _BBS_3MF_Importer* importer = (_BBS_3MF_Importer*)userData;
        if (importer != nullptr)
            importer->_handle_model_xml_characters(s, len);
    }

    void XMLCALL _BBS_3MF_Importer::_handle_start_config_xml_element(void* userData, const char* name, const char** attributes)
    {
        _BBS_3MF_Importer* importer = (_BBS_3MF_Importer*)userData;
        if (importer != nullptr)
            importer->_handle_start_config_xml_element(name, attributes);
    }
    
    void XMLCALL _BBS_3MF_Importer::_handle_end_config_xml_element(void* userData, const char* name)
    {
        _BBS_3MF_Importer* importer = (_BBS_3MF_Importer*)userData;
        if (importer != nullptr)
            importer->_handle_end_config_xml_element(name);
    }

    class _BBS_3MF_Exporter : public _BBS_3MF_Base
    {
        struct BuildItem
        {
            unsigned int id;
            Transform3d transform;
            bool printable;

            BuildItem(unsigned int id, const Transform3d& transform, const bool printable)
                : id(id)
                , transform(transform)
                , printable(printable)
            {
            }
        };

        struct Offsets
        {
            unsigned int first_vertex_id;
            unsigned int first_triangle_id;
            unsigned int last_triangle_id;

            Offsets(unsigned int first_vertex_id)
                : first_vertex_id(first_vertex_id)
                , first_triangle_id(-1)
                , last_triangle_id(-1)
            {
            }
        };

        typedef std::map<const ModelVolume*, Offsets> VolumeToOffsetsMap;

        struct ObjectData
        {
            ModelObject* object;
            VolumeToOffsetsMap volumes_offsets;

            explicit ObjectData(ModelObject* object)
                : object(object)
            {
            }
        };

        typedef std::vector<BuildItem> BuildItemsList;
        typedef std::map<int, ObjectData> IdToObjectDataMap;

        bool m_fullpath_sources{ true };
        bool m_zip64 { true };
        bool m_skip_static{ false }; // not save mesh and other big static contents

    public:
        //BBS: add plate data related logic

        // add backup logic
        bool save_model_to_file(const std::string& filename, Model& model, PlateDataPtrs& plate_data_list, std::vector<Preset*>& project_presets, const DynamicPrintConfig* config, bool fullpath_sources, const std::vector<ThumbnailData*>& thumbnail_data, bool zip64, bool skip_static, Export3mfProgressFn proFn = nullptr, bool silence = false);
        // add backup logic
        bool save_object_mesh(const std::string& filename, ModelObject& object);

    private:
        //BBS: add plate data related logic
        bool _save_model_to_file(const std::string& filename, Model& model, PlateDataPtrs& plate_data_list, std::vector<Preset*>& project_presets, const DynamicPrintConfig* config, const std::vector<ThumbnailData*>& thumbnail_data, Export3mfProgressFn proFn);

        bool _add_content_types_file_to_archive(mz_zip_archive& archive);
        bool _add_thumbnail_file_to_archive(mz_zip_archive& archive, const ThumbnailData& thumbnail_data, int index);
        bool _add_relationships_file_to_archive(mz_zip_archive& archive);
        bool _add_model_file_to_archive(const std::string& filename, mz_zip_archive& archive, const Model& model, IdToObjectDataMap& objects_data, Export3mfProgressFn proFn = nullptr);
        bool _add_object_to_model_stream(mz_zip_writer_staged_context &context, unsigned int& object_id, ModelObject& object, BuildItemsList& build_items, VolumeToOffsetsMap& volumes_offsets);
        bool _add_mesh_to_object_stream(std::function<bool(std::string&, bool)> const& flush, ModelObject& object, VolumeToOffsetsMap& volumes_offsets);
        bool _add_build_to_model_stream(std::stringstream& stream, const BuildItemsList& build_items);
        bool _add_layer_height_profile_file_to_archive(mz_zip_archive& archive, Model& model);
        bool _add_layer_config_ranges_file_to_archive(mz_zip_archive& archive, Model& model);
        bool _add_sla_support_points_file_to_archive(mz_zip_archive& archive, Model& model);
        bool _add_sla_drain_holes_file_to_archive(mz_zip_archive& archive, Model& model);
        bool _add_print_config_file_to_archive(mz_zip_archive& archive, const DynamicPrintConfig &config);
        //BBS: add project embedded preset files
        bool _add_project_embedded_presets_to_archive(mz_zip_archive& archive, Model& model, std::vector<Preset*> project_presets);
        bool _add_model_config_file_to_archive(mz_zip_archive& archive, const Model& model, PlateDataPtrs& plate_data_list, const IdToObjectDataMap &objects_data);
        bool _add_slice_info_config_file_to_archive(mz_zip_archive& archive, const Model& model, PlateDataPtrs& plate_data_list);
        bool _add_gcode_file_to_archive(mz_zip_archive& archive, const Model& model, PlateDataPtrs& plate_data_list, Export3mfProgressFn proFn = nullptr);
        bool _add_custom_gcode_per_print_z_file_to_archive(mz_zip_archive& archive, Model& model, const DynamicPrintConfig* config);
        bool _add_auxiliary_dir_to_archive(mz_zip_archive& archive, const std::string& aux_dir);

        static int convert_instance_id_to_resource_id(const Model& model, int obj_id, int instance_id)
        {
            int resource_id = 1;

            for (int i = 0; i < obj_id; ++i)
            {
                resource_id += model.objects[i]->instances.size();
            }

            resource_id += instance_id;

            return resource_id;
        }
    };

    //BBS: add plate data related logic
    // add backup logic
    bool _BBS_3MF_Exporter::save_model_to_file(const std::string& filename, Model& model, PlateDataPtrs& plate_data_list, std::vector<Preset*>& project_presets, const DynamicPrintConfig* config, bool fullpath_sources, const std::vector<ThumbnailData*>& thumbnail_data, bool zip64, bool skip_static, Export3mfProgressFn proFn, bool silence)
    {
        clear_errors();
        m_fullpath_sources = fullpath_sources;
        m_zip64 = zip64;

        m_skip_static = skip_static;
        boost::system::error_code ec;
        boost::filesystem::remove(filename + ".tmp", ec);
        bool result = _save_model_to_file(filename + ".tmp", model,
                                            plate_data_list, project_presets, config,
                                            thumbnail_data, proFn);
        if (result) {
            boost::filesystem::rename(filename + ".tmp", filename, ec);
            if (!silence)
                boost::filesystem::save_string_file(model.get_backup_path() + "/origin.txt", filename);
        }
        return result;
    }

    // backup mesh-only
    bool _BBS_3MF_Exporter::save_object_mesh(const std::string& filename, ModelObject& object)
    {
        std::ofstream ofs(encode_path(filename.c_str()));
        auto flush = [&ofs](std::string& buf, bool force) -> bool {
            ofs.write(buf.c_str(), buf.size());
            if (force)
                ofs.flush();
            buf.clear();
            return !!ofs;
        };
        VolumeToOffsetsMap volumes_offsets;
        _add_mesh_to_object_stream(flush, object, volumes_offsets);
        return false;
    }

    //BBS: add plate data related logic
    bool _BBS_3MF_Exporter::_save_model_to_file(const std::string& filename, Model& model, PlateDataPtrs& plate_data_list, std::vector<Preset*>& project_presets, const DynamicPrintConfig* config, const std::vector<ThumbnailData*>& thumbnail_data, Export3mfProgressFn proFn)
    {
        mz_zip_archive archive;
        mz_zip_zero_struct(&archive);

        bool cb_cancel = false;

        //BBS progress point
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ":" <<__LINE__ << boost::format("export 3mf EXPORT_STAGE_OPEN_3MF\n");
        if (proFn) {
            proFn(EXPORT_STAGE_OPEN_3MF, 0, 1, cb_cancel);
            if (cb_cancel)
                return false;
        }

        if (!open_zip_writer(&archive, filename)) {
            add_error("Unable to open the file");
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ":" << __LINE__ << boost::format(", Unable to open the file\n");
            return false;
        }

        //BBS progress point
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ":" <<__LINE__ << boost::format("export 3mf EXPORT_STAGE_CONTENT_TYPES\n");
        if (proFn) {
            proFn(EXPORT_STAGE_CONTENT_TYPES, 0, 1, cb_cancel);
            if (cb_cancel)
                return false;
        }

        // Adds content types file ("[Content_Types].xml";).
        // The content of this file is the same for each BambuSlicer 3mf.
        if (!m_skip_static && !_add_content_types_file_to_archive(archive)) {
            close_zip_writer(&archive);
            boost::filesystem::remove(filename);
            return false;
        }

        //BBS progress point
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ":" <<__LINE__ << boost::format("export 3mf EXPORT_STAGE_CONTENT_TYPES\n");

        //BBS: add thumbnail for each plate
        if (!m_skip_static && thumbnail_data.size()>0) {
            // Adds the file Metadata/thumbnail.png.
            for (unsigned int index = 0; index < thumbnail_data.size(); index++)
            {
                if (proFn) {
                    proFn(EXPORT_STAGE_ADD_THUMBNAILS, index, thumbnail_data.size(), cb_cancel);
                    if (cb_cancel)
                        return false;
                }

                if (thumbnail_data[index]->is_valid())
                {
                    if (!_add_thumbnail_file_to_archive(archive, *thumbnail_data[index], index)) {
                        close_zip_writer(&archive);
                        boost::filesystem::remove(filename);
                        return false;
                    }
                }
            }
        }

        //BBS progress point
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ":" <<__LINE__ << boost::format("export 3mf EXPORT_STAGE_ADD_RELATIONS\n");
        if (proFn) {
            proFn(EXPORT_STAGE_ADD_RELATIONS, 0, 1, cb_cancel);
            if (cb_cancel)
                return false;
        }

        // Adds relationships file ("_rels/.rels"). 
        // The content of this file is the same for each BambuSlicer 3mf.
        // The relationshis file contains a reference to the geometry file "3D/3dmodel.model", the name was chosen to be compatible with CURA.
        if (!m_skip_static && !_add_relationships_file_to_archive(archive)) {
            close_zip_writer(&archive);
            boost::filesystem::remove(filename);
            return false;
        }

        //BBS progress point
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ":" <<__LINE__ << boost::format("export 3mf EXPORT_STAGE_ADD_MODELS\n");
        if (proFn) {
            proFn(EXPORT_STAGE_ADD_MODELS, 0, 1, cb_cancel);
            if (cb_cancel)
                return false;
        }

        // Adds model file ("3D/3dmodel.model").
        // This is the one and only file that contains all the geometry (vertices and triangles) of all ModelVolumes.
        IdToObjectDataMap objects_data;
        if (!_add_model_file_to_archive(filename, archive, model, objects_data, proFn)) {
            close_zip_writer(&archive);
            boost::filesystem::remove(filename);
            return false;
        }

        // Adds layer height profile file ("Metadata/Slic3r_PE_layer_heights_profile.txt").
        // All layer height profiles of all ModelObjects are stored here, indexed by 1 based index of the ModelObject in Model.
        // The index differes from the index of an object ID of an object instance of a 3MF file!
        // BBS: don't need to save layer_height_profile because we calculate when slicing every time.
        /*
        if (!_add_layer_height_profile_file_to_archive(archive, model)) {
            close_zip_writer(&archive);
            boost::filesystem::remove(filename);
            return false;
        }*/

        //BBS progress point
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ":" <<__LINE__ << boost::format("export 3mf EXPORT_STAGE_ADD_LAYER_RANGE\n");
        if (proFn) {
            proFn(EXPORT_STAGE_ADD_LAYER_RANGE, 0, 1, cb_cancel);
            if (cb_cancel)
                return false;
        }

        // Adds layer config ranges file ("Metadata/Slic3r_PE_layer_config_ranges.txt").
        // All layer height profiles of all ModelObjects are stored here, indexed by 1 based index of the ModelObject in Model.
        // The index differes from the index of an object ID of an object instance of a 3MF file!
        if (!_add_layer_config_ranges_file_to_archive(archive, model)) {
            close_zip_writer(&archive);
            boost::filesystem::remove(filename);
            return false;
        }

        //BBS progress point
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ":" <<__LINE__ << boost::format("export 3mf EXPORT_STAGE_ADD_SUPPORT\n");
        if (proFn) {
            proFn(EXPORT_STAGE_ADD_SUPPORT, 0, 1, cb_cancel);
            if (cb_cancel)
                return false;
        }

        // Adds sla support points file ("Metadata/Slic3r_PE_sla_support_points.txt").
        // All  sla support points of all ModelObjects are stored here, indexed by 1 based index of the ModelObject in Model.
        // The index differes from the index of an object ID of an object instance of a 3MF file!
        if (!_add_sla_support_points_file_to_archive(archive, model)) {
            close_zip_writer(&archive);
            boost::filesystem::remove(filename);
            return false;
        }
        
        if (!_add_sla_drain_holes_file_to_archive(archive, model)) {
            close_zip_writer(&archive);
            boost::filesystem::remove(filename);
            return false;
        }

        //BBS progress point
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ":" <<__LINE__ << boost::format("export 3mf EXPORT_STAGE_ADD_CUSTOM_GCODE\n");
        if (proFn) {
            proFn(EXPORT_STAGE_ADD_CUSTOM_GCODE, 0, 1, cb_cancel);
            if (cb_cancel)
                return false;
        }

        // Adds custom gcode per height file ("Metadata/Prusa_Slicer_custom_gcode_per_print_z.xml").
        // All custom gcode per height of whole Model are stored here
        if (!_add_custom_gcode_per_print_z_file_to_archive(archive, model, config)) {
            close_zip_writer(&archive);
            boost::filesystem::remove(filename);
            return false;
        }

        //BBS progress point
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ":" <<__LINE__ << boost::format("export 3mf EXPORT_STAGE_ADD_PRINT_CONFIG\n");
        if (proFn) {
            proFn(EXPORT_STAGE_ADD_PRINT_CONFIG, 0, 1, cb_cancel);
            if (cb_cancel)
                return false;
        }

        // Adds slic3r print config file ("Metadata/Slic3r_PE.config").
        // This file contains the content of FullPrintConfing / SLAFullPrintConfig.
        if (config != nullptr) {
            if (!_add_print_config_file_to_archive(archive, *config)) {
                close_zip_writer(&archive);
                boost::filesystem::remove(filename);
                return false;
            }
        }

        //BBS progress point
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ":" <<__LINE__ << boost::format("export 3mf EXPORT_STAGE_ADD_CONFIG_FILE\n");
        if (proFn) {
            proFn(EXPORT_STAGE_ADD_CONFIG_FILE, 0, 1, cb_cancel);
            if (cb_cancel)
                return false;
        }

        //BBS: add project config
        if (project_presets.size() > 0) {
            //BBS: add project embedded preset files
            _add_project_embedded_presets_to_archive(archive, model, project_presets);

            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ":" <<__LINE__ << boost::format("export 3mf EXPORT_STAGE_ADD_PROJECT_CONFIG\n");
            if (proFn) {
                proFn(EXPORT_STAGE_ADD_PROJECT_CONFIG, 0, 1, cb_cancel);
                if (cb_cancel)
                    return false;
            }
        }

        // Adds slic3r model config file ("Metadata/Slic3r_PE_model.config").
        // This file contains all the attributes of all ModelObjects and their ModelVolumes (names, parameter overrides).
        // As there is just a single Indexed Triangle Set data stored per ModelObject, offsets of volumes into their respective Indexed Triangle Set data
        // is stored here as well.
        if (!_add_model_config_file_to_archive(archive, model, plate_data_list, objects_data)) {
            close_zip_writer(&archive);
            boost::filesystem::remove(filename);
            return false;
        }

        //BBS progress point
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ":" <<__LINE__ << boost::format("export 3mf EXPORT_STAGE_ADD_SLICE_INFO\n");
        if (proFn) {
            proFn(EXPORT_STAGE_ADD_SLICE_INFO, 0, 1, cb_cancel);
            if (cb_cancel)
                return false;
        }

        // Adds sliced info of plate file ("Metadata/slice_info.config")
        // This file contains all sliced info of all plates
        if (!_add_slice_info_config_file_to_archive(archive, model, plate_data_list)) {
            close_zip_writer(&archive);
            boost::filesystem::remove(filename);
            return false;
        }

        //BBS progress point
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ":" <<__LINE__ << boost::format("export 3mf EXPORT_STAGE_ADD_GCODE\n");

        // Adds gcode files ("Metadata/plate_1.gcode, plate_2.gcode, ...)
        if (!m_skip_static && !_add_gcode_file_to_archive(archive, model, plate_data_list, proFn)) {
            close_zip_writer(&archive);
            boost::filesystem::remove(filename);
            return false;
        }

        //BBS progress point
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ":" <<__LINE__ << boost::format("export 3mf EXPORT_STAGE_ADD_AUXILIARIES\n");
        if (proFn) {
            proFn(EXPORT_STAGE_ADD_AUXILIARIES, 0, 1, cb_cancel);
            if (cb_cancel)
                return false;
        }

        // Adds gcode files ("Metadata/plate_1.gcode, plate_2.gcode, ...)
        if (!m_skip_static && !_add_auxiliary_dir_to_archive(archive, model.get_auxiliary_file_temp_path())) {
            close_zip_writer(&archive);
            boost::filesystem::remove(filename);
            return false;
        }

        // save plate_id_map
       if (m_skip_static) {
            std::ofstream ofs(encode_path((const_cast<Model &>(model).get_backup_path() + "/plate_map.txt").c_str()));
            int l = model.get_backup_path().length() + 1;
            for (auto i : plate_data_list) {
                if (!i->gcode_file.empty()) {
                    auto id = i->gcode_file.substr(l, i->gcode_file.length() - l - 6);
                    BOOST_LOG_TRIVIAL(info) << "save plate_map: " << (i->plate_index + 1) << " -> " << id;
                    ofs << (i->plate_index + 1) << " " << id << std::endl;
                }
            }
            ofs.flush();
        }

        if (!mz_zip_writer_finalize_archive(&archive)) {
            close_zip_writer(&archive);
            boost::filesystem::remove(filename);
            add_error("Unable to finalize the archive");
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ":" << __LINE__ << boost::format(", Unable to finalize the archive\n");
            return false;
        }

        //BBS progress point
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ":" <<__LINE__ << boost::format("export 3mf EXPORT_STAGE_FINISH\n");
        if (proFn) {
            proFn(EXPORT_STAGE_FINISH, 0, 1, cb_cancel);
            if (cb_cancel)
                return false;
        }

        close_zip_writer(&archive);

        return true;
    }

    bool _BBS_3MF_Exporter::_add_content_types_file_to_archive(mz_zip_archive& archive)
    {
        std::stringstream stream;
        stream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        stream << "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">\n";
        stream << " <Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>\n";
        stream << " <Default Extension=\"model\" ContentType=\"application/vnd.ms-package.3dmanufacturing-3dmodel+xml\"/>\n";
        stream << " <Default Extension=\"png\" ContentType=\"image/png\"/>\n";
        stream << "</Types>";

        std::string out = stream.str();

        if (!mz_zip_writer_add_mem(&archive, CONTENT_TYPES_FILE.c_str(), (const void*)out.data(), out.length(), MZ_DEFAULT_COMPRESSION)) {
            add_error("Unable to add content types file to archive");
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ":" << __LINE__ << boost::format(", Unable to add content types file to archive\n");
            return false;
        }

        return true;
    }

    bool _BBS_3MF_Exporter::_add_thumbnail_file_to_archive(mz_zip_archive& archive, const ThumbnailData& thumbnail_data, int index)
    {
        bool res = false;

        size_t png_size = 0;
        void* png_data = tdefl_write_image_to_png_file_in_memory_ex((const void*)thumbnail_data.pixels.data(), thumbnail_data.width, thumbnail_data.height, 4, &png_size, MZ_DEFAULT_LEVEL, 1);
        if (png_data != nullptr) {
            std::string thumbnail_name = (boost::format("Metadata/plate_%1%.png") % (index + 1)).str();
            res = mz_zip_writer_add_mem(&archive, thumbnail_name.c_str(), (const void*)png_data, png_size, MZ_DEFAULT_COMPRESSION);
            mz_free(png_data);
        }

        if (!res) {
            add_error("Unable to add thumbnail file to archive");
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ":" << __LINE__ << boost::format(", Unable to add thumbnail file to archive\n");
        }

        return res;
    }

    bool _BBS_3MF_Exporter::_add_relationships_file_to_archive(mz_zip_archive& archive)
    {
        std::stringstream stream;
        stream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        stream << "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n";
        stream << " <Relationship Target=\"/" << MODEL_FILE << "\" Id=\"rel-1\" Type=\"http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel\"/>\n";
        stream << " <Relationship Target=\"/" << THUMBNAIL_FILE << "\" Id=\"rel-2\" Type=\"http://schemas.openxmlformats.org/package/2006/relationships/metadata/thumbnail\"/>\n";
        stream << "</Relationships>";

        std::string out = stream.str();

        if (!mz_zip_writer_add_mem(&archive, RELATIONSHIPS_FILE.c_str(), (const void*)out.data(), out.length(), MZ_DEFAULT_COMPRESSION)) {
            add_error("Unable to add relationships file to archive");
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ":" << __LINE__ << boost::format(", Unable to add relationships file to archive\n");
            return false;
        }

        return true;
    }

    static void reset_stream(std::stringstream &stream)
    {
        stream.str("");
        stream.clear();
        // https://en.cppreference.com/w/cpp/types/numeric_limits/max_digits10
        // Conversion of a floating-point value to text and back is exact as long as at least max_digits10 were used (9 for float, 17 for double).
        // It is guaranteed to produce the same floating-point value, even though the intermediate text representation is not exact.
        // The default value of std::stream precision is 6 digits only!
        stream << std::setprecision(std::numeric_limits<float>::max_digits10);
    }

    bool _BBS_3MF_Exporter::_add_model_file_to_archive(const std::string& filename, mz_zip_archive& archive, const Model& model, IdToObjectDataMap& objects_data, Export3mfProgressFn proFn)
    {
        mz_zip_writer_staged_context context;
        if (!mz_zip_writer_add_staged_open(&archive, &context, MODEL_FILE.c_str(), 
            m_zip64 ? 
                // Maximum expected and allowed 3MF file size is 16GiB.
                // This switches the ZIP file to a 64bit mode, which adds a tiny bit of overhead to file records.
                (uint64_t(1) << 30) * 16 : 
                // Maximum expected 3MF file size is 4GB-1. This is a workaround for interoperability with Windows 10 3D model fixing API, see
                // GH issue #6193.
                (uint64_t(1) << 32) - 1,
            nullptr, nullptr, 0, MZ_DEFAULT_COMPRESSION, nullptr, 0, nullptr, 0)) {
            add_error("Unable to add model file to archive");
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ":" << __LINE__ << boost::format(", Unable to add model file to archive\n");
            return false;
        }

        {
            std::stringstream stream;
            reset_stream(stream);
            stream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
            stream << "<" << MODEL_TAG << " unit=\"millimeter\" xml:lang=\"en-US\" xmlns=\"http://schemas.microsoft.com/3dmanufacturing/core/2015/02\" xmlns:slic3rpe=\"http://schemas.slic3r.org/3mf/2017/06\">\n";
            stream << " <" << METADATA_TAG << " name=\"" << BBS_3MF_VERSION << "\">" << VERSION_BBS_3MF << "</" << METADATA_TAG << ">\n";

            if (model.is_fdm_support_painted())
                stream << " <" << METADATA_TAG << " name=\"" << SLIC3RPE_FDM_SUPPORTS_PAINTING_VERSION << "\">" << FDM_SUPPORTS_PAINTING_VERSION << "</" << METADATA_TAG << ">\n";

            if (model.is_seam_painted())
                stream << " <" << METADATA_TAG << " name=\"" << SLIC3RPE_SEAM_PAINTING_VERSION << "\">" << SEAM_PAINTING_VERSION << "</" << METADATA_TAG << ">\n";

            if (model.is_mm_painted())
                stream << " <" << METADATA_TAG << " name=\"" << SLIC3RPE_MM_PAINTING_VERSION << "\">" << MM_PAINTING_VERSION << "</" << METADATA_TAG << ">\n";

            std::string name = xml_escape(boost::filesystem::path(filename).stem().string());
            stream << " <" << METADATA_TAG << " name=\"Title\">" << name << "</" << METADATA_TAG << ">\n";
            stream << " <" << METADATA_TAG << " name=\"Designer\">" << "</" << METADATA_TAG << ">\n";
            stream << " <" << METADATA_TAG << " name=\"Description\">" << name << "</" << METADATA_TAG << ">\n";
            stream << " <" << METADATA_TAG << " name=\"Copyright\">" << "</" << METADATA_TAG << ">\n";
            stream << " <" << METADATA_TAG << " name=\"LicenseTerms\">" << "</" << METADATA_TAG << ">\n";
            stream << " <" << METADATA_TAG << " name=\"Rating\">" << "</" << METADATA_TAG << ">\n";
            std::string date = Slic3r::Utils::utc_timestamp(Slic3r::Utils::get_current_time_utc());
            // keep only the date part of the string
            date = date.substr(0, 10);
            stream << " <" << METADATA_TAG << " name=\"CreationDate\">" << date << "</" << METADATA_TAG << ">\n";
            stream << " <" << METADATA_TAG << " name=\"ModificationDate\">" << date << "</" << METADATA_TAG << ">\n";
            stream << " <" << METADATA_TAG << " name=\"Application\">" << SLIC3R_APP_KEY << "-" << SLIC3R_VERSION << "</" << METADATA_TAG << ">\n";
            stream << " <" << RESOURCES_TAG << ">\n";
            std::string buf = stream.str();
            if (! buf.empty() && ! mz_zip_writer_add_staged_data(&context, buf.data(), buf.size())) {
                add_error("Unable to add model file to archive");
                BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ":" << __LINE__ << boost::format(", Unable to add model file to archive\n");
                return false;
            }
        }

        // Instance transformations, indexed by the 3MF object ID (which is a linear serialization of all instances of all ModelObjects).
        BuildItemsList build_items;

        // The object_id here is a one based identifier of the first instance of a ModelObject in the 3MF file, where
        // all the object instances of all ModelObjects are stored and indexed in a 1 based linear fashion.
        // Therefore the list of object_ids here may not be continuous.
        unsigned int object_id = 1;

        std::map<unsigned int, size_t> object_id_map; // backup: collect id mapping
        bool cb_cancel = false;
        int obj_idx = 0;
        for (ModelObject* obj : model.objects) {

            if (proFn) {
                proFn(EXPORT_STAGE_ADD_GCODE, obj_idx, model.objects.size(), cb_cancel);
                if (cb_cancel)
                    return false;
                obj_idx++;
            }
            
            if (obj == nullptr)
                continue;
            object_id_map.insert(std::make_pair(object_id, obj->id().id));
            // Index of an object in the 3MF file corresponding to the 1st instance of a ModelObject.
            unsigned int curr_id = object_id;
            IdToObjectDataMap::iterator object_it = objects_data.insert({ curr_id, ObjectData(obj) }).first;
            // Store geometry of all ModelVolumes contained in a single ModelObject into a single 3MF indexed triangle set object.
            // object_it->second.volumes_offsets will contain the offsets of the ModelVolumes in that single indexed triangle set.
            // object_id will be increased to point to the 1st instance of the next ModelObject.
            if (!_add_object_to_model_stream(context, object_id, *obj, build_items, object_it->second.volumes_offsets)) {
                add_error("Unable to add object to archive");
                BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ":" << __LINE__ << boost::format(", Unable to add object to archive\n");
                mz_zip_writer_add_staged_finish(&context);
                return false;
            }
        }

        // save object_id_map
        if (m_skip_static) {
            std::ofstream ofs(encode_path((const_cast<Model &>(model).get_backup_path() + "/object_map.txt").c_str()));
            for (auto i : object_id_map) {
                BOOST_LOG_TRIVIAL(info) << "save object_map: " << i.first << " -> " << i.second;
                ofs << i.first << " " << i.second << std::endl;
            }
            ofs.flush();
        }

        {
            std::stringstream stream;
            reset_stream(stream);
            stream << " </" << RESOURCES_TAG << ">\n";

            // Store the transformations of all the ModelInstances of all ModelObjects, indexed in a linear fashion.
            if (!_add_build_to_model_stream(stream, build_items)) {
                add_error("Unable to add build to archive");
                BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ":" << __LINE__ << boost::format(", Unable to add build to archive\n");
                mz_zip_writer_add_staged_finish(&context);
                return false;
            }

            stream << "</" << MODEL_TAG << ">\n";
           
            std::string buf = stream.str();

            if ((! buf.empty() && ! mz_zip_writer_add_staged_data(&context, buf.data(), buf.size())) ||
                ! mz_zip_writer_add_staged_finish(&context)) {
                add_error("Unable to add model file to archive");
                BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ":" << __LINE__ << boost::format(", Unable to add model file to archive\n");
                return false;
            }
        }

        return true;
    }

    bool _BBS_3MF_Exporter::_add_object_to_model_stream(mz_zip_writer_staged_context &context, unsigned int& object_id, ModelObject& object, BuildItemsList& build_items, VolumeToOffsetsMap& volumes_offsets)
    {
        std::stringstream stream;
        reset_stream(stream);
        unsigned int id = 0;
        for (const ModelInstance* instance : object.instances) {
			assert(instance != nullptr);
            if (instance == nullptr)
                continue;

            unsigned int instance_id = object_id + id;
            stream << "  <" << OBJECT_TAG << " id=\"" << instance_id << "\" type=\"model\">\n";

            if (id == 0) {
                std::string buf = stream.str();
                reset_stream(stream);
                // backup: make _add_mesh_to_object_stream() reusable
                auto flush = [this, &context](std::string & buf, bool force = false) {
                    if ((force && !buf.empty()) || buf.size() >= 65536 * 16) {
                        if (!mz_zip_writer_add_staged_data(&context, buf.data(), buf.size())) {
                            add_error("Error during writing or compression");
                            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ":" << __LINE__ << boost::format(", Error during writing or compression\n");
                            return false;
                        }
                        buf.clear();
                    }
                    return true;
                };
                if ((! buf.empty() && ! mz_zip_writer_add_staged_data(&context, buf.data(), buf.size())) || 
                    ! _add_mesh_to_object_stream(flush, object, volumes_offsets)) {
                    add_error("Unable to add mesh to archive");
                    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ":" << __LINE__ << boost::format(", Unable to add mesh to archive\n");
                    return false;
                }
            }
            else {
                stream << "   <" << COMPONENTS_TAG << ">\n";
                stream << "    <" << COMPONENT_TAG << " objectid=\"" << object_id << "\"/>\n";
                stream << "   </" << COMPONENTS_TAG << ">\n";
            }

            Transform3d t = instance->get_matrix();
            // instance_id is just a 1 indexed index in build_items.
            assert(instance_id == build_items.size() + 1);
            build_items.emplace_back(instance_id, t, instance->printable);

            stream << "  </" << OBJECT_TAG << ">\n";

            ++id;
        }

        object_id += id;
        std::string buf = stream.str();
        return buf.empty() || mz_zip_writer_add_staged_data(&context, buf.data(), buf.size());
    }

#if EXPORT_3MF_USE_SPIRIT_KARMA_FP
    template <typename Num>
    struct coordinate_policy_fixed : boost::spirit::karma::real_policies<Num>
    {
        static int floatfield(Num n) { return fmtflags::fixed; }
        // Number of decimal digits to maintain float accuracy when storing into a text file and parsing back.
        static unsigned precision(Num /* n */) { return std::numeric_limits<Num>::max_digits10 + 1; }
        // No trailing zeros, thus for fmtflags::fixed usually much less than max_digits10 decimal numbers will be produced.
        static bool trailing_zeros(Num /* n */) { return false; }
    };
    template <typename Num>
    struct coordinate_policy_scientific : coordinate_policy_fixed<Num>
    {
        static int floatfield(Num n) { return fmtflags::scientific; }
    };
    // Define a new generator type based on the new coordinate policy.
    using coordinate_type_fixed      = boost::spirit::karma::real_generator<float, coordinate_policy_fixed<float>>;
    using coordinate_type_scientific = boost::spirit::karma::real_generator<float, coordinate_policy_scientific<float>>;
#endif // EXPORT_3MF_USE_SPIRIT_KARMA_FP

    // backup: reuse by save_object_mesh, support skip mesh data
    bool _BBS_3MF_Exporter::_add_mesh_to_object_stream(std::function<bool(std::string &,bool)> const & flush, ModelObject& object, VolumeToOffsetsMap& volumes_offsets)
    {
        std::string output_buffer;

#if 0
        auto flush = [this, &output_buffer, &context](bool force = false) {
            if ((force && ! output_buffer.empty()) || output_buffer.size() >= 65536 * 16) {
                if (! mz_zip_writer_add_staged_data(&context, output_buffer.data(), output_buffer.size())) {
                    add_error("Error during writing or compression");
                    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ":" << __LINE__ << boost::format(", Error during writing or compression\n");
                    return false;
                }
                output_buffer.clear();
            }
            return true;
        };
#endif

        if (!m_skip_static) {
            output_buffer += "   <";
            output_buffer += MESH_TAG;
            output_buffer += ">\n    <";
            output_buffer += VERTICES_TAG;
            output_buffer += ">\n";
        }

        auto format_coordinate = [](float f, char *buf) -> char* {
            assert(is_decimal_separator_point());
#if EXPORT_3MF_USE_SPIRIT_KARMA_FP
            // Slightly faster than sprintf("%.9g"), but there is an issue with the karma floating point formatter,
            // https://github.com/boostorg/spirit/pull/586
            // where the exported string is one digit shorter than it should be to guarantee lossless round trip.
            // The code is left here for the ocasion boost guys improve.
            coordinate_type_fixed      const coordinate_fixed      = coordinate_type_fixed();
            coordinate_type_scientific const coordinate_scientific = coordinate_type_scientific();
            // Format "f" in a fixed format.
            char *ptr = buf;
            boost::spirit::karma::generate(ptr, coordinate_fixed, f);
            // Format "f" in a scientific format.
            char *ptr2 = ptr;
            boost::spirit::karma::generate(ptr2, coordinate_scientific, f);
            // Return end of the shorter string.
            auto len2 = ptr2 - ptr;
            if (ptr - buf > len2) {
                // Move the shorter scientific form to the front.
                memcpy(buf, ptr, len2);
                ptr = buf + len2;
            }
            // Return pointer to the end.
            return ptr;
#else
            // Round-trippable float, shortest possible.
            return buf + sprintf(buf, "%.9g", f);
#endif
        };

        char buf[256];
        unsigned int vertices_count = 0;
        for (ModelVolume* volume : object.volumes) {
            if (volume == nullptr)
                continue;

			//if (!volume->mesh().stats().repaired())
			//	throw Slic3r::FileIOError("store_3mf() requires repair()");

            volumes_offsets.insert({ volume, Offsets(vertices_count) });

            const indexed_triangle_set &its = volume->mesh().its;
            if (its.vertices.empty()) {
                add_error("Found invalid mesh");
                BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ":" << __LINE__ << boost::format(", Found invalid mesh\n");
                return false;
            }

            vertices_count += (int)its.vertices.size();

            if (!m_skip_static) {
                const Transform3d& matrix = volume->get_matrix();

                for (size_t i = 0; i < its.vertices.size(); ++i) {
                    Vec3f v = (matrix * its.vertices[i].cast<double>()).cast<float>();
                    char* ptr = buf;
                    boost::spirit::karma::generate(ptr, boost::spirit::lit("     <") << VERTEX_TAG << " x=\"");
                    ptr = format_coordinate(v.x(), ptr);
                    boost::spirit::karma::generate(ptr, "\" y=\"");
                    ptr = format_coordinate(v.y(), ptr);
                    boost::spirit::karma::generate(ptr, "\" z=\"");
                    ptr = format_coordinate(v.z(), ptr);
                    boost::spirit::karma::generate(ptr, "\"/>\n");
                    *ptr = '\0';
                    output_buffer += buf;
                    if (!flush(output_buffer, false))
                        return false;
                }
            }
        }

        if (!m_skip_static) {
            output_buffer += "    </";
            output_buffer += VERTICES_TAG;
            output_buffer += ">\n    <";
            output_buffer += TRIANGLES_TAG;
            output_buffer += ">\n";
        }

        unsigned int triangles_count = 0;
        for (ModelVolume* volume : object.volumes) {
            if (volume == nullptr)
                continue;

            bool is_left_handed = volume->is_left_handed();
            VolumeToOffsetsMap::iterator volume_it = volumes_offsets.find(volume);
            assert(volume_it != volumes_offsets.end());

            const indexed_triangle_set &its = volume->mesh().its;

            // updates triangle offsets
            volume_it->second.first_triangle_id = triangles_count;
            triangles_count += (int)its.indices.size();
            volume_it->second.last_triangle_id = triangles_count - 1;

            if (m_skip_static)
                continue;

            for (int i = 0; i < int(its.indices.size()); ++ i) {
                {
                    const Vec3i &idx = its.indices[i];
                    char *ptr = buf;
                    boost::spirit::karma::generate(ptr, boost::spirit::lit("     <") << TRIANGLE_TAG <<
                        " v1=\"" << boost::spirit::int_ <<
                        "\" v2=\"" << boost::spirit::int_ <<
                        "\" v3=\"" << boost::spirit::int_ << "\"",
                        idx[is_left_handed ? 2 : 0] + volume_it->second.first_vertex_id,
                        idx[1] + volume_it->second.first_vertex_id,
                        idx[is_left_handed ? 0 : 2] + volume_it->second.first_vertex_id);
                    *ptr = '\0';
                    output_buffer += buf;
                }

                std::string custom_supports_data_string = volume->supported_facets.get_triangle_as_string(i);
                if (! custom_supports_data_string.empty()) {
                    output_buffer += " ";
                    output_buffer += CUSTOM_SUPPORTS_ATTR;
                    output_buffer += "=\"";
                    output_buffer += custom_supports_data_string;
                    output_buffer += "\"";
                }

                std::string custom_seam_data_string = volume->seam_facets.get_triangle_as_string(i);
                if (! custom_seam_data_string.empty()) {
                    output_buffer += " ";
                    output_buffer += CUSTOM_SEAM_ATTR;
                    output_buffer += "=\"";
                    output_buffer += custom_seam_data_string;
                    output_buffer += "\"";
                }

                std::string mmu_painting_data_string = volume->mmu_segmentation_facets.get_triangle_as_string(i);
                if (! mmu_painting_data_string.empty()) {
                    output_buffer += " ";
                    output_buffer += MMU_SEGMENTATION_ATTR;
                    output_buffer += "=\"";
                    output_buffer += mmu_painting_data_string;
                    output_buffer += "\"";
                }

                output_buffer += "/>\n";

                if (! flush(output_buffer, false))
                    return false;
            }
        }

        if (!m_skip_static) {
            output_buffer += "    </";
            output_buffer += TRIANGLES_TAG;
            output_buffer += ">\n   </";
            output_buffer += MESH_TAG;
            output_buffer += ">\n";

            // Force flush.
            return flush(output_buffer, true);
        }
        else {
            return true;
        }
    }

    bool _BBS_3MF_Exporter::_add_build_to_model_stream(std::stringstream& stream, const BuildItemsList& build_items)
    {
        // This happens for empty projects
        if (build_items.size() == 0) {
            add_error("No build item found");
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ":" << __LINE__ << boost::format("No build item found\n");
            return true;
        }

        stream << " <" << BUILD_TAG << ">\n";

        for (const BuildItem& item : build_items) {
            stream << "  <" << ITEM_TAG << " " << OBJECTID_ATTR << "=\"" << item.id << "\" " << TRANSFORM_ATTR << "=\"";
            for (unsigned c = 0; c < 4; ++c) {
                for (unsigned r = 0; r < 3; ++r) {
                    stream << item.transform(r, c);
                    if (r != 2 || c != 3)
                        stream << " ";
                }
            }
            stream << "\" " << PRINTABLE_ATTR << "=\"" << item.printable << "\"/>\n";
        }

        stream << " </" << BUILD_TAG << ">\n";

        return true;
    }

    bool _BBS_3MF_Exporter::_add_layer_height_profile_file_to_archive(mz_zip_archive& archive, Model& model)
    {
        assert(is_decimal_separator_point());
        std::string out = "";
        char buffer[1024];

        unsigned int count = 0;
        for (const ModelObject* object : model.objects) {
            ++count;
            const std::vector<double>& layer_height_profile = object->layer_height_profile.get();
            if (layer_height_profile.size() >= 4 && layer_height_profile.size() % 2 == 0) {
                sprintf(buffer, "object_id=%d|", count);
                out += buffer;

                // Store the layer height profile as a single semicolon separated list.
                for (size_t i = 0; i < layer_height_profile.size(); ++i) {
                    sprintf(buffer, (i == 0) ? "%f" : ";%f", layer_height_profile[i]);
                    out += buffer;
                }
                
                out += "\n";
            }
        }

        if (!out.empty()) {
            if (!mz_zip_writer_add_mem(&archive, LAYER_HEIGHTS_PROFILE_FILE.c_str(), (const void*)out.data(), out.length(), MZ_DEFAULT_COMPRESSION)) {
                add_error("Unable to add layer heights profile file to archive");
                BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ":" << __LINE__ << boost::format("Unable to add layer heights profile file to archive\n");
                return false;
            }
        }

        return true;
    }

    bool _BBS_3MF_Exporter::_add_layer_config_ranges_file_to_archive(mz_zip_archive& archive, Model& model)
    {
        std::string out = "";
        pt::ptree tree;

        unsigned int object_cnt = 0;
        for (const ModelObject* object : model.objects) {
            object_cnt++;
            const t_layer_config_ranges& ranges = object->layer_config_ranges;
            if (!ranges.empty())
            {
                pt::ptree& obj_tree = tree.add("objects.object","");

                obj_tree.put("<xmlattr>.id", object_cnt);

                // Store the layer config ranges.
                for (const auto& range : ranges) {
                    pt::ptree& range_tree = obj_tree.add("range", "");

                    // store minX and maxZ
                    range_tree.put("<xmlattr>.min_z", range.first.first);
                    range_tree.put("<xmlattr>.max_z", range.first.second);

                    // store range configuration
                    const ModelConfig& config = range.second;
                    for (const std::string& opt_key : config.keys()) {
                        pt::ptree& opt_tree = range_tree.add("option", config.opt_serialize(opt_key));
                        opt_tree.put("<xmlattr>.opt_key", opt_key);
                    }
                }
            }
        }

        if (!tree.empty()) {
            std::ostringstream oss;
            pt::write_xml(oss, tree);
            out = oss.str();

            // Post processing("beautification") of the output string for a better preview
            boost::replace_all(out, "><object",      ">\n <object");
            boost::replace_all(out, "><range",       ">\n  <range");
            boost::replace_all(out, "><option",      ">\n   <option");
            boost::replace_all(out, "></range>",     ">\n  </range>");
            boost::replace_all(out, "></object>",    ">\n </object>");
            // OR just 
            boost::replace_all(out, "><",            ">\n<"); 
        }

        if (!out.empty()) {
            if (!mz_zip_writer_add_mem(&archive, LAYER_CONFIG_RANGES_FILE.c_str(), (const void*)out.data(), out.length(), MZ_DEFAULT_COMPRESSION)) {
                add_error("Unable to add layer heights profile file to archive");
                BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ":" << __LINE__ << boost::format("Unable to add layer heights profile file to archive\n");
                return false;
            }
        }

        return true;
    }

    bool _BBS_3MF_Exporter::_add_sla_support_points_file_to_archive(mz_zip_archive& archive, Model& model)
    {
        assert(is_decimal_separator_point());
        std::string out = "";
        char buffer[1024];

        unsigned int count = 0;
        for (const ModelObject* object : model.objects) {
            ++count;
            const std::vector<sla::SupportPoint>& sla_support_points = object->sla_support_points;
            if (!sla_support_points.empty()) {
                sprintf(buffer, "object_id=%d|", count);
                out += buffer;

                // Store the layer height profile as a single space separated list.
                for (size_t i = 0; i < sla_support_points.size(); ++i) {
                    sprintf(buffer, (i==0 ? "%f %f %f %f %f" : " %f %f %f %f %f"),  sla_support_points[i].pos(0), sla_support_points[i].pos(1), sla_support_points[i].pos(2), sla_support_points[i].head_front_radius, (float)sla_support_points[i].is_new_island);
                    out += buffer;
                }
                out += "\n";
            }
        }

        if (!out.empty()) {
            // Adds version header at the beginning:
            //out = std::string("support_points_format_version=") + std::to_string(support_points_format_version) + std::string("\n") + out;

            if (!mz_zip_writer_add_mem(&archive, SLA_SUPPORT_POINTS_FILE.c_str(), (const void*)out.data(), out.length(), MZ_DEFAULT_COMPRESSION)) {
                add_error("Unable to add sla support points file to archive");
                BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ":" << __LINE__ << boost::format("Unable to add sla support points file to archive\n");
                return false;
            }
        }
        return true;
    }
    
    bool _BBS_3MF_Exporter::_add_sla_drain_holes_file_to_archive(mz_zip_archive& archive, Model& model)
    {
        assert(is_decimal_separator_point());
        const char *const fmt = "object_id=%d|";
        std::string out;
        
        unsigned int count = 0;
        for (const ModelObject* object : model.objects) {
            ++count;
            sla::DrainHoles drain_holes = object->sla_drain_holes;

            // The holes were placed 1mm above the mesh in the first implementation.
            // This was a bad idea and the reference point was changed in 2.3 so
            // to be on the mesh exactly. The elevated position is still saved
            // in 3MFs for compatibility reasons.
            for (sla::DrainHole& hole : drain_holes) {
                hole.pos -= hole.normal.normalized();
                hole.height += 1.f;
            }

            if (!drain_holes.empty()) {
                out += string_printf(fmt, count);
                
                // Store the layer height profile as a single space separated list.
                for (size_t i = 0; i < drain_holes.size(); ++i)
                    out += string_printf((i == 0 ? "%f %f %f %f %f %f %f %f" : " %f %f %f %f %f %f %f %f"),
                                         drain_holes[i].pos(0),
                                         drain_holes[i].pos(1),
                                         drain_holes[i].pos(2),
                                         drain_holes[i].normal(0),
                                         drain_holes[i].normal(1),
                                         drain_holes[i].normal(2),
                                         drain_holes[i].radius,
                                         drain_holes[i].height);
                
                out += "\n";
            }
        }
        
        if (!out.empty()) {
            // Adds version header at the beginning:
            //out = std::string("drain_holes_format_version=") + std::to_string(drain_holes_format_version) + std::string("\n") + out;
            
            if (!mz_zip_writer_add_mem(&archive, SLA_DRAIN_HOLES_FILE.c_str(), static_cast<const void*>(out.data()), out.length(), mz_uint(MZ_DEFAULT_COMPRESSION))) {
                add_error("Unable to add sla support points file to archive");
                BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ":" << __LINE__ << boost::format("Unable to add sla support points file to archive\n");
                return false;
            }
        }
        return true;
    }

    bool _BBS_3MF_Exporter::_add_print_config_file_to_archive(mz_zip_archive& archive, const DynamicPrintConfig &config)
    {
        assert(is_decimal_separator_point());
        char buffer[1024];
        sprintf(buffer, "; %s\n\n", header_slic3r_generated().c_str());
        std::string out = buffer;

        for (const std::string &key : config.keys())
            if (key != "compatible_printers")
                out += "; " + key + " = " + config.opt_serialize(key) + "\n";

        if (!out.empty()) {
            if (!mz_zip_writer_add_mem(&archive, BBS_PRINT_CONFIG_FILE.c_str(), (const void*)out.data(), out.length(), MZ_DEFAULT_COMPRESSION)) {
                add_error("Unable to add print config file to archive");
                BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ":" << __LINE__ << boost::format("Unable to add print config file to archive\n");
                return false;
            }
        }

        return true;
    }

    //BBS: add project embedded preset files
    bool _BBS_3MF_Exporter::_add_project_embedded_presets_to_archive(mz_zip_archive& archive, Model& model, std::vector<Preset*> project_presets)
    {
        bool result = true;
        char buffer[1024];
        sprintf(buffer, "; %s\n\n", header_slic3r_generated().c_str());
        std::string out = buffer;
        int print_count = 0, filament_count = 0, printer_count = 0;
        const std::string& temp_path = model.get_backup_path();

        for (int i = 0; i < project_presets.size(); i++)
        {
            Preset* preset = project_presets[i];

            if (preset) {
                preset->file = temp_path + std::string("/") + "_temp_1.config";
                DynamicPrintConfig& config = preset->config;
                config.save(preset->file);

                std::string src_file = encode_path(preset->file.c_str());
                std::string dest_file;
                if (preset->type == Preset::TYPE_PRINT) {
                    dest_file = (boost::format(EMBEDDED_PRINT_FILE_FORMAT) % (print_count + 1)).str();
                    print_count++;
                }
                else if (preset->type == Preset::TYPE_FILAMENT) {
                    dest_file = (boost::format(EMBEDDED_FILAMENT_FILE_FORMAT) % (filament_count + 1)).str();
                    filament_count++;
                }
                else if (preset->type == Preset::TYPE_PRINTER) {
                    dest_file = (boost::format(EMBEDDED_PRINTER_FILE_FORMAT) % (printer_count + 1)).str();
                    printer_count++;
                }
                else
                    continue;

                result = mz_zip_writer_add_file(&archive, dest_file.c_str(), src_file.c_str(), "", 0, MZ_DEFAULT_COMPRESSION);
                if (!result) {
                    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ":" <<__LINE__ << boost::format(", Unable to add embedded preset %1% to archive %2%, type %3%\n")%preset->file %dest_file %Preset::get_type_string(preset->type);
                }
                else
                    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ":" <<__LINE__ << boost::format(", add embedded print preset %1% to archive %2%, type %3%\n")%preset->file %dest_file %Preset::get_type_string(preset->type);
            }
        }

        return true;
    }

    bool _BBS_3MF_Exporter::_add_model_config_file_to_archive(mz_zip_archive& archive, const Model& model, PlateDataPtrs& plate_data_list, const IdToObjectDataMap &objects_data)
    {
        std::stringstream stream;
        // Store mesh transformation in full precision, as the volumes are stored transformed and they need to be transformed back
        // when loaded as accurately as possible.
		stream << std::setprecision(std::numeric_limits<double>::max_digits10);
        stream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        stream << "<" << CONFIG_TAG << ">\n";

        for (const IdToObjectDataMap::value_type& obj_metadata : objects_data) {
            const ModelObject* obj = obj_metadata.second.object;
            if (obj != nullptr) {
                // Output of instances count added because of github #3435, currently not used by BambuSlicer
                stream << "  <"  << OBJECT_TAG << " " << ID_ATTR << "=\"" << obj_metadata.first << "\" " << INSTANCESCOUNT_ATTR << "=\"" << obj->instances.size() << "\">\n";

                // stores object's name
                if (!obj->name.empty())
                    stream << "    <" << METADATA_TAG << " " << TYPE_ATTR << "=\"" << OBJECT_TYPE << "\" " << KEY_ATTR << "=\"name\" " << VALUE_ATTR << "=\"" << xml_escape(obj->name) << "\"/>\n";

                //BBS: store object's module name
                if (!obj->module_name.empty())
                    stream << "    <" << METADATA_TAG << " " << TYPE_ATTR << "=\"" << OBJECT_TYPE << "\" " << KEY_ATTR << "=\"module\" " << VALUE_ATTR << "=\"" << xml_escape(obj->module_name) << "\"/>\n";

                // stores object's config data
                for (const std::string& key : obj->config.keys()) {
                    stream << "    <" << METADATA_TAG << " " << TYPE_ATTR << "=\"" << OBJECT_TYPE << "\" " << KEY_ATTR << "=\"" << key << "\" " << VALUE_ATTR << "=\"" << obj->config.opt_serialize(key) << "\"/>\n";
                }

                for (const ModelVolume* volume : obj_metadata.second.object->volumes) {
                    if (volume != nullptr) {
                        const VolumeToOffsetsMap& offsets = obj_metadata.second.volumes_offsets;
                        VolumeToOffsetsMap::const_iterator it = offsets.find(volume);
                        if (it != offsets.end()) {
                            // stores volume's offsets
                            stream << "    <" << VOLUME_TAG << " ";
                            stream << FIRST_TRIANGLE_ID_ATTR << "=\"" << it->second.first_triangle_id << "\" ";
                            stream << LAST_TRIANGLE_ID_ATTR << "=\"" << it->second.last_triangle_id << "\">\n";

                            // stores volume's id and subtype
                            /*std::string subtype;
                            switch (volume->type())
                            {
                            case ModelVolumeType::MODEL_PART:
                            default:
                                subtype = "normal";
                                break;
                            case ModelVolumeType::PARAMETER_MODIFIER:
                                subtype = "modifier";
                                break;
                            case ModelVolumeType::SUPPORT_BLOCKER:
                                subtype = "support_blocker";
                                break;
                            case ModelVolumeType::SUPPORT_ENFORCER:
                                subtype = "support_enforcer";
                                break;
                            }
                            stream << "    <" << PART_TAG << " " << ID_ATTR << "=\"" << it->second << "\" " << SUBTYPE_ATTR << "=\"" << subtype << "\">\n";*/

                            // stores volume's name
                            if (!volume->name.empty())
                                stream << "      <" << METADATA_TAG << " " << TYPE_ATTR << "=\"" << PART_TYPE << "\" " << KEY_ATTR << "=\"" << NAME_KEY << "\" " << VALUE_ATTR << "=\"" << xml_escape(volume->name) << "\"/>\n";
                                //stream << "      <" << METADATA_TAG << " " << KEY_ATTR << "=\"" << NAME_KEY << "\" " << VALUE_ATTR << "=\"" << xml_escape(volume->name) << "\"/>\n";

                            // stores volume's modifier field (legacy, to support old slicers)
                            if (volume->is_modifier())
                                stream << "      <" << METADATA_TAG << " " << TYPE_ATTR << "=\"" << PART_TYPE << "\" " << KEY_ATTR << "=\"" << MODIFIER_KEY << "\" " << VALUE_ATTR << "=\"1\"/>\n";
                            // stores volume's type (overrides the modifier field above)
                            stream << "      <" << METADATA_TAG << " " << TYPE_ATTR << "=\"" << PART_TYPE << "\" " << KEY_ATTR << "=\"" << PART_TYPE_KEY << "\" " <<
                                VALUE_ATTR << "=\"" << ModelVolume::type_to_string(volume->type()) << "\"/>\n";

                            // stores volume's local matrix
                            stream << "      <" << METADATA_TAG << " " << TYPE_ATTR << "=\"" << PART_TYPE << "\" " << KEY_ATTR << "=\"" << MATRIX_KEY << "\" " << VALUE_ATTR << "=\"";
                            Transform3d matrix = volume->get_matrix() * volume->source.transform.get_matrix();
                            for (int r = 0; r < 4; ++r) {
                                for (int c = 0; c < 4; ++c) {
                                    stream << matrix(r, c);
                                    if (r != 3 || c != 3)
                                        stream << " ";
                                }
                            }
                            stream << "\"/>\n";

                            // stores volume's source data
                            {
                                std::string input_file = xml_escape(m_fullpath_sources ? volume->source.input_file : boost::filesystem::path(volume->source.input_file).filename().string());
                                //std::string prefix = std::string("      <") + METADATA_TAG + " " + KEY_ATTR + "=\"";
                                std::string prefix = std::string("      <") + METADATA_TAG + " " + TYPE_ATTR + "=\"" + PART_TYPE + "\" " + KEY_ATTR + "=\"";
                                if (! volume->source.input_file.empty()) {
                                    stream << prefix << SOURCE_FILE_KEY      << "\" " << VALUE_ATTR << "=\"" << input_file << "\"/>\n";
                                    stream << prefix << SOURCE_OBJECT_ID_KEY << "\" " << VALUE_ATTR << "=\"" << volume->source.object_idx << "\"/>\n";
                                    stream << prefix << SOURCE_VOLUME_ID_KEY << "\" " << VALUE_ATTR << "=\"" << volume->source.volume_idx << "\"/>\n";
                                    stream << prefix << SOURCE_OFFSET_X_KEY  << "\" " << VALUE_ATTR << "=\"" << volume->source.mesh_offset(0) << "\"/>\n";
                                    stream << prefix << SOURCE_OFFSET_Y_KEY  << "\" " << VALUE_ATTR << "=\"" << volume->source.mesh_offset(1) << "\"/>\n";
                                    stream << prefix << SOURCE_OFFSET_Z_KEY  << "\" " << VALUE_ATTR << "=\"" << volume->source.mesh_offset(2) << "\"/>\n";
                                }
                                assert(! volume->source.is_converted_from_inches || ! volume->source.is_converted_from_meters);
                                if (volume->source.is_converted_from_inches)
                                    stream << prefix << SOURCE_IN_INCHES << "\" " << VALUE_ATTR << "=\"1\"/>\n";
                                else if (volume->source.is_converted_from_meters)
                                    stream << prefix << SOURCE_IN_METERS << "\" " << VALUE_ATTR << "=\"1\"/>\n";
                            }

                            // stores volume's config data
                            for (const std::string& key : volume->config.keys()) {
                                stream << "   <" << METADATA_TAG << " " << TYPE_ATTR << "=\"" << PART_TYPE << "\" " << KEY_ATTR << "=\"" << key << "\" " << VALUE_ATTR << "=\"" << volume->config.opt_serialize(key) << "\"/>\n";
                            }

                            // stores mesh's statistics
                            const RepairedMeshErrors& stats = volume->mesh().stats().repaired_errors;
                            stream << "   <" << MESH_TAG << " ";
                            stream << MESH_STAT_EDGES_FIXED        << "=\"" << stats.edges_fixed        << "\" ";
                            stream << MESH_STAT_DEGENERATED_FACETS << "=\"" << stats.degenerate_facets  << "\" ";
                            stream << MESH_STAT_FACETS_REMOVED     << "=\"" << stats.facets_removed     << "\" ";
                            stream << MESH_STAT_FACETS_RESERVED    << "=\"" << stats.facets_reversed    << "\" ";
                            stream << MESH_STAT_BACKWARDS_EDGES    << "=\"" << stats.backwards_edges    << "\"/>\n";

                            stream << "  </" << VOLUME_TAG << ">\n";
                        }
                    }
                }

                stream << " </" << OBJECT_TAG << ">\n";
            }
        }

        //BBS: store plate related logic
        for (unsigned int i = 0; i < (unsigned int)plate_data_list.size(); ++i)
        {
            PlateData* plate_data = plate_data_list[i];
            int instance_size = plate_data->objects_and_instances.size();

            if (plate_data != nullptr) {
                stream << "  <" << PLATE_TAG << ">\n";
                //plate index
                stream << "    <" << METADATA_TAG << " " << KEY_ATTR << "=\"" << PLATERID_ATTR << "\" " << VALUE_ATTR << "=\"" << i + 1 << "\"/>\n";
                stream << "    <" << METADATA_TAG << " " << KEY_ATTR << "=\"" << LOCK_ATTR << "\" " << VALUE_ATTR << "=\"" << std::boolalpha<< plate_data->locked<< "\"/>\n";
                if (instance_size > 0)
                {
                    for (unsigned int j = 0; j < instance_size; ++j)
                    {
                        stream << "    <" << INSTANCE_TAG << ">\n";
                        int obj_id = plate_data->objects_and_instances[j].first;
                        int inst_id = plate_data->objects_and_instances[j].second;

                        stream << "      <" << METADATA_TAG << " " << KEY_ATTR << "=\"" << OBJECT_ID_ATTR << "\" " << VALUE_ATTR << "=\"" << convert_instance_id_to_resource_id(model, obj_id, 0) << "\"/>\n";
                        stream << "      <" << METADATA_TAG << " " << KEY_ATTR << "=\"" << INSTANCEID_ATTR << "\" " << VALUE_ATTR << "=\"" << convert_instance_id_to_resource_id(model, obj_id, inst_id) << "\"/>\n";
                        stream << "    </" << INSTANCE_TAG << ">\n";
                    }
                }

                stream << "  </" << PLATE_TAG << ">\n"; 
            }
        }

        //BBS: store assemble related info
        stream << "  <" << ASSEMBLE_TAG << ">\n";
        for (const IdToObjectDataMap::value_type& obj_metadata : objects_data) {
            const ModelObject* obj = obj_metadata.second.object;
            if (obj != nullptr) {
                for (int instance_idx = 0; instance_idx < obj->instances.size(); ++instance_idx) {
                    if (obj->instances[instance_idx]->is_assemble_initialized()) {
                        stream << "   <" << ASSEMBLE_ITEM_TAG << " " << OBJECT_ID_ATTR << "=\"" << obj_metadata.first << "\" ";
                        stream << INSTANCEID_ATTR << "=\"" << obj_metadata.first + instance_idx << "\" " << TRANSFORM_ATTR << "=\"";
                            for (unsigned c = 0; c < 4; ++c) {
                                for (unsigned r = 0; r < 3; ++r) {
                                    const Transform3d assemble_trans = obj->instances[instance_idx]->get_assemble_transformation().get_matrix();
                                    stream << assemble_trans(r, c);
                                    if (r != 2 || c != 3)
                                        stream << " ";
                                }
                            }

                        stream << "\" ";

                        stream << OFFSET_ATTR << "=\"";
                        Vec3d ofs2ass = obj->instances[instance_idx]->get_offset_to_assembly();
                        stream << ofs2ass(0) << " " << ofs2ass(1) << " " << ofs2ass(2);
                    stream << "\" />\n";
                    }
                }
            }
        }
        stream << "  </" << ASSEMBLE_TAG << ">\n";


        stream << "</" << CONFIG_TAG << ">\n";

        std::string out = stream.str();

        if (!mz_zip_writer_add_mem(&archive, BBS_MODEL_CONFIG_FILE.c_str(), (const void*)out.data(), out.length(), MZ_DEFAULT_COMPRESSION)) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ":" << __LINE__ << boost::format("Unable to add model config file to archive\n");
            add_error("Unable to add model config file to archive");
            return false;
        }

        return true;
    }

    bool _BBS_3MF_Exporter::_add_slice_info_config_file_to_archive(mz_zip_archive& archive, const Model& model, PlateDataPtrs& plate_data_list)
    {
        std::stringstream stream;
        // Store mesh transformation in full precision, as the volumes are stored transformed and they need to be transformed back
        // when loaded as accurately as possible.
		stream << std::setprecision(std::numeric_limits<double>::max_digits10);
        stream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        stream << "<" << CONFIG_TAG << ">\n";

        // save slice header for debug
        stream << "  <" << SLICE_HEADER_TAG << ">\n";
        stream << "    <" << SLICE_HEADER_ITEM_TAG << " " << KEY_ATTR << "=\"" << "X-BBL-Client-Type"    << "\" " << VALUE_ATTR << "=\"" << "slicer" << "\"/>\n";
        stream << "    <" << SLICE_HEADER_ITEM_TAG << " " << KEY_ATTR << "=\"" << "X-BBL-Client-Version" << "\" " << VALUE_ATTR << "=\"" << convert_to_full_version(SLIC3R_RC_VERSION) << "\"/>\n";
        stream << "  </" << SLICE_HEADER_TAG << ">\n";

        for (unsigned int i = 0; i < (unsigned int)plate_data_list.size(); ++i)
        {
            PlateData* plate_data = plate_data_list[i];
            //int instance_size = plate_data->objects_and_instances.size();

            if (plate_data != nullptr && plate_data->is_sliced_valid) {
                stream << "  <" << PLATE_TAG << ">\n";
                //plate index
                stream << "    <" << METADATA_TAG << " " << KEY_ATTR << "=\"" << PLATE_IDX_ATTR        << "\" " << VALUE_ATTR << "=\"" << i + 1 << "\"/>\n";
                stream << "    <" << METADATA_TAG << " " << KEY_ATTR << "=\"" << SLICE_PREDICTION_ATTR << "\" " << VALUE_ATTR << "=\"" << plate_data->get_gcode_prediction_str() << "\"/>\n";
                stream << "    <" << METADATA_TAG << " " << KEY_ATTR << "=\"" << SLICE_WEIGHT_ATTR      << "\" " << VALUE_ATTR << "=\"" <<  plate_data->get_gcode_weight_str() << "\"/>\n";
                stream << "    <" << METADATA_TAG << " " << KEY_ATTR << "=\"" << OUTSIDE_ATTR      << "\" " << VALUE_ATTR << "=\"" << std::boolalpha<< plate_data->toolpath_outside << "\"/>\n";
                stream << "  </" << PLATE_TAG << ">\n";
            }
        }
        stream << "</" << CONFIG_TAG << ">\n";

        std::string out = stream.str();

        if (!mz_zip_writer_add_mem(&archive, SLICE_INFO_CONFIG_FILE.c_str(), (const void*)out.data(), out.length(), MZ_DEFAULT_COMPRESSION)) {
            add_error("Unable to add model config file to archive");
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ":" << __LINE__ << boost::format(", store  slice-info to 3mf,  length %1%, failed\n") % out.length();
            return false;
        }

        return true;
    }
bool _BBS_3MF_Exporter::_add_gcode_file_to_archive(mz_zip_archive& archive, const Model& model, PlateDataPtrs& plate_data_list, Export3mfProgressFn proFn)
{
    bool result = true;
    bool cb_cancel = false;

    for (unsigned int i = 0; i < (unsigned int)plate_data_list.size(); ++i)
    {
        if (proFn) {
            proFn(EXPORT_STAGE_ADD_GCODE, i, plate_data_list.size(), cb_cancel);
            if (cb_cancel)
                return false;
        }

        PlateData* plate_data = plate_data_list[i];
        if (!plate_data->gcode_file.empty() && plate_data->is_sliced_valid) {
            std::string src_gcode_file = encode_path(plate_data->gcode_file.c_str());
            std::string gcode_in_3mf = (boost::format(GCODE_FILE_FORMAT) % (i + 1)).str();
            result = result & mz_zip_writer_add_file(&archive, gcode_in_3mf.c_str(), src_gcode_file.c_str(), "", 0, MZ_DEFAULT_LEVEL);
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ":" <<__LINE__ << boost::format(", store  %1% to 3mf %2%, result %3%\n") % src_gcode_file % gcode_in_3mf % result;
        }
        else {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ":" << __LINE__ << boost::format(", gcode_file = %1%, valid = %2%") % plate_data->gcode_file % plate_data->is_sliced_valid;
        }
    }
    return result;
}

bool _BBS_3MF_Exporter::_add_custom_gcode_per_print_z_file_to_archive( mz_zip_archive& archive, Model& model, const DynamicPrintConfig* config)
{
    std::string out = "";

    if (!model.custom_gcode_per_print_z.gcodes.empty()) {
        pt::ptree tree;
        pt::ptree& main_tree = tree.add("custom_gcodes_per_print_z", "");

        for (const CustomGCode::Item& code : model.custom_gcode_per_print_z.gcodes) {
            pt::ptree& code_tree = main_tree.add("code", "");

            // store data of custom_gcode_per_print_z
            code_tree.put("<xmlattr>.print_z"   , code.print_z  );
            code_tree.put("<xmlattr>.type"      , static_cast<int>(code.type));
            code_tree.put("<xmlattr>.extruder"  , code.extruder );
            code_tree.put("<xmlattr>.color"     , code.color    );
            code_tree.put("<xmlattr>.extra"     , code.extra    );

            // add gcode field data for the old version of the BambuSlicer
            std::string gcode = code.type == CustomGCode::ColorChange ? config->opt_string("color_change_gcode")    :
                                code.type == CustomGCode::PausePrint  ? config->opt_string("pause_print_gcode")     :
                                code.type == CustomGCode::Template    ? config->opt_string("template_custom_gcode") :
                                code.type == CustomGCode::ToolChange  ? "tool_change"   : code.extra; 
            code_tree.put("<xmlattr>.gcode"     , gcode   );
        }

        pt::ptree& mode_tree = main_tree.add("mode", "");
        // store mode of a custom_gcode_per_print_z 
        mode_tree.put("<xmlattr>.value", model.custom_gcode_per_print_z.mode == CustomGCode::Mode::SingleExtruder ? CustomGCode::SingleExtruderMode :
                                         model.custom_gcode_per_print_z.mode == CustomGCode::Mode::MultiAsSingle ?  CustomGCode::MultiAsSingleMode :
                                         CustomGCode::MultiExtruderMode);

        if (!tree.empty()) {
            std::ostringstream oss;
            boost::property_tree::write_xml(oss, tree);
            out = oss.str();

            // Post processing("beautification") of the output string
            boost::replace_all(out, "><", ">\n<");
        }
    } 

    if (!out.empty()) {
        if (!mz_zip_writer_add_mem(&archive, CUSTOM_GCODE_PER_PRINT_Z_FILE.c_str(), (const void*)out.data(), out.length(), MZ_DEFAULT_COMPRESSION)) {
            add_error("Unable to add custom Gcodes per print_z file to archive");
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ":" << __LINE__ << boost::format(", Unable to add custom Gcodes per print_z file to archive\n");
            return false;
        }
    }

    return true;
}

bool _BBS_3MF_Exporter::_add_auxiliary_dir_to_archive(mz_zip_archive& archive, const std::string& aux_dir)
{
    bool result = true;

    if (aux_dir.empty()) {
        //no accessory directories
        return result;
    }

    boost::filesystem::path dir = boost::filesystem::path(aux_dir);
    if (!boost::filesystem::exists(dir))
    {
        //no accessory directories
        return result;
    }

    //boost file access
    for (auto &dir_entry : boost::filesystem::directory_iterator(dir))
    {
        std::string src_file;
        std::string dst_in_3mf;
        if (boost::filesystem::is_directory(dir_entry.path()))
        {
            for (auto &subdir_entry : boost::filesystem::directory_iterator(dir_entry.path()))
            {
                if (boost::filesystem::is_regular_file(subdir_entry.path()))
                {
                    src_file = subdir_entry.path().string();
                    dst_in_3mf = std::string(AUXILIARY_DIR) + dir_entry.path().filename().string() + std::string("/") + subdir_entry.path().filename().string();

                    std::string dest_zip_file = encode_path(dst_in_3mf.c_str());
                    std::string src_zip_file = encode_path(src_file.c_str());

                    result = result & mz_zip_writer_add_file(&archive, dest_zip_file.c_str(), src_zip_file.c_str(), "", 0, MZ_DEFAULT_LEVEL);
                    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ":" <<__LINE__ << boost::format(", store  %1% to 3mf %2%, result %3%\n") % src_file % dst_in_3mf % result;
                }
            }
        }
        else if (boost::filesystem::is_regular_file(dir_entry.path()))
        {
            src_file = dir_entry.path().string();
            dst_in_3mf = std::string(AUXILIARY_DIR) + dir_entry.path().filename().string();

            std::string dest_zip_file = encode_path(dst_in_3mf.c_str());
            std::string src_zip_file = encode_path(src_file.c_str());

            result = result & mz_zip_writer_add_file(&archive, dest_zip_file.c_str(), src_zip_file.c_str(), "", 0, MZ_DEFAULT_LEVEL);
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ":" <<__LINE__ << boost::format(", store  %1% to 3mf %2%, result %3%\n") % src_file % dst_in_3mf % result;
        }
    }

    return result;
}

// Perform conversions based on the config values available.
//FIXME provide a version of BambuSlicer that stored the project file (3MF).
static void handle_legacy_project_loaded(unsigned int version_project_file, DynamicPrintConfig& config)
{
    if (! config.has("brim_separation")) {
        if (auto *opt_elephant_foot   = config.option<ConfigOptionFloat>("elefant_foot_compensation", false); opt_elephant_foot) {
            // Conversion from older BambuSlicer which applied brim separation equal to elephant foot compensation.
            auto *opt_brim_separation = config.option<ConfigOptionFloat>("brim_separation", true);
            opt_brim_separation->value = opt_elephant_foot->value;
        }
    }
}

// backup backgroud thread to dispatch tasks and coperate with ui thread
class _BBS_Backup_Manager
{
public:
    static _BBS_Backup_Manager& get() {
        static _BBS_Backup_Manager m;
        return m;
    }

    void set_post_callback(std::function<void(int)> c) {
        boost::lock_guard lock(m_mutex);
        m_post_callback = c;
    }

    void run_ui_tasks() {
        std::deque<Task> tasks;
        {
            boost::lock_guard lock(m_mutex);
            std::swap(tasks, m_ui_tasks);
        }
        for (auto& t : tasks)
        {
            process_ui_task(t);
        }
    }

    void push_object_gaurd(ModelObject& object) {
        m_gaurd_objects.push_back(std::make_pair(&object, 0));
    }

    void pop_object_gaurd() {
        auto object = m_gaurd_objects.back();
        m_gaurd_objects.pop_back();
        if (object.second)
            add_object_mesh(*object.first);
    }

    void add_object_mesh(ModelObject& object, size_t originId = 0) {
        for (auto& g : m_gaurd_objects) {
            if (g.first == &object) {
                ++g.second;
                return;
            }
        }
        // clone object
        auto o = m_temp_model.add_object(object);
        push_task({ AddObject, object.id().id, object.get_model()->get_backup_path(), originId, o, originId == 0 ? size_t(1) : size_t(0) });
    }

    void remove_object_mesh(ModelObject& object) {
        push_task({ RemoveObject, object.id().id, object.get_model()->get_backup_path() });
    }

    void backup_soon() {
        boost::lock_guard lock(m_mutex);
        m_other_changes_backup = true;
        m_tasks.push_back({ Backup, 0, std::string(), ++m_task_seq });
        m_cond.notify_all();
    }

    void remove_backup(Model& model, bool removeAll) {
        BOOST_LOG_TRIVIAL(info)
            << "remove_backup " << model.get_backup_path() << ", " << removeAll;
        std::deque<Task>   canceled_tasks;
        boost::unique_lock lock(m_mutex);
        if (removeAll) {
            // running task may not be canceled
            for (auto & t : m_ui_tasks)
                canceled_tasks.push_back(t);
            for (auto & t : m_tasks)
                canceled_tasks.push_back(t);
            m_ui_tasks.clear();
            m_tasks.clear();
        }
        m_tasks.push_back({ RemoveBackup, removeAll, model.get_backup_path() });
        ++m_task_seq;
        m_other_changes = false;
        m_other_changes_backup = false;
        m_cond.notify_all();
        lock.unlock();
        for (auto& t : canceled_tasks) {
            process_ui_task(t, true);
        }
    }

    void set_interval(long n) {
        boost::lock_guard lock(m_mutex);
        m_next_backup -= boost::posix_time::seconds(m_interval);
        m_interval = n;
        m_next_backup += boost::posix_time::seconds(m_interval);
        m_cond.notify_all();
    }

    void put_other_changes()
    {
        BOOST_LOG_TRIVIAL(info) << "put_other_changes";
        m_other_changes        = true;
        m_other_changes_backup = true;
    }

    bool has_other_changes(bool backup)
    {
        return backup ? m_other_changes_backup : m_other_changes;
    }

private:
    enum TaskType {
        None, 
        Backup, // this task is working as response in ui thread
        AddObject,
        RemoveObject,
        RemoveBackup,
        Exit
    };
    struct Task {
        TaskType type;
        size_t id;
        std::string path;
        size_t id2;
        ModelObject* object;
        size_t delay; // delay sequence, only last task is delayed
        friend bool operator==(Task const& l, Task const& r) {
            return l.type == r.type && l.id == r.id;
        }
        std::string to_string() const {
            constexpr char const *type_names[] = {"None",
                                                  "Backup",
                                                  "AddObject",
                                                  "RemoveObject",
                                                  "RemoveBackup",
                                                  "Exit"};
            std::ostringstream os;
            os << "{ type:" << type_names[type] << ", id:" << id
               << ", path:" << path
               << ", id2:" << id2
               << ", object:" << (object ? object->id().id : 0) << ", delay:" << delay << "}";
            return os.str();
        }
    };

    struct timer {
        timer(char const * msg) : msg(msg), start(boost::posix_time::microsec_clock::universal_time()) { }
        ~timer() {
#ifdef __WIN32__
            auto end = boost::posix_time::microsec_clock::universal_time();
            int duration = (int)(end - start).total_milliseconds();
            char buf[20];
            OutputDebugStringA(msg);
            OutputDebugStringA(": ");
            OutputDebugStringA(itoa(duration, buf, 10));
            OutputDebugStringA("\n");
#endif
        }
        char const* msg;
        boost::posix_time::ptime start;
    };
private:
    _BBS_Backup_Manager() : m_thread(boost::ref(*this)) {
        m_next_backup = boost::get_system_time() + boost::posix_time::seconds(m_interval);
    }

    ~_BBS_Backup_Manager() {
        push_task({Exit});
        m_thread.join();
    }

    void push_task(Task const & t) {
        boost::unique_lock lock(m_mutex);
        if (t.delay && !m_tasks.empty() && m_tasks.back() == t) {
            auto t2 = m_tasks.back();
            m_tasks.back() = t;
            m_tasks.back().delay = t2.delay + 1;
            m_cond.notify_all();
            lock.unlock();
            process_ui_task(t2);
        }
        else {
            m_tasks.push_back(t);
            ++m_task_seq;
            m_cond.notify_all();
        }
    }

    void process_ui_task(Task& t, bool canceled = false) {
        BOOST_LOG_TRIVIAL(info) << "process_ui_task" << t.to_string();
        switch (t.type) {
            case Backup: {
                if (canceled)
                    break;
                std::function<void(int)> callback;
                boost::unique_lock lock(m_mutex);
                if (m_task_seq != t.id2) {
                    if (find(m_tasks.begin(), m_tasks.end(), Task{ Backup }) == m_tasks.end()) {
                        t.id2 = ++m_task_seq; // may has pending tasks, retry later
                        m_tasks.push_back(t);
                        m_cond.notify_all();
                    }
                    break;
                }
                callback = m_post_callback;
                lock.unlock();
                {
                    timer t("backup cost");
                    try {
                        callback(1);
                    } catch (...) {}
                }
                m_other_changes_backup = false;
                break;
            }
            case AddObject:
                m_temp_model.delete_object(t.object);
                break;
            case RemoveBackup:
                if (t.id) { // remove all
                    try {
                        boost::filesystem::remove(t.path + "/lock.txt");
                        boost::filesystem::remove_all(t.path);
                    } catch (...) {}
                }
                break;
        }
    }

    void process_task(Task& t) {
        BOOST_LOG_TRIVIAL(info) << "process_task" << t.to_string();
        switch (t.type) {
            case Backup:
                // do it in response
                break;
            case AddObject: {
                std::string path = t.path + "/mesh_" + boost::lexical_cast<std::string>(t.id) + ".xml";
                if (t.id2) {
                    std::string path2 = t.path + "/mesh_" + boost::lexical_cast<std::string>(t.id2) + ".xml";
                    if (boost::filesystem::exists(path2) && !boost::filesystem::exists(path)) {
                        boost::filesystem::rename(path2, path);
                        break;
                    }
                }
                {
                    timer tm(path.c_str());
                    _BBS_3MF_Exporter e;
                    e.save_object_mesh(path, *t.object);
                    // response to delete cloned object
                }
                break;
            }
            case RemoveObject: {
                boost::filesystem::remove(t.path + "/mesh_" + boost::lexical_cast<std::string>(t.id) + ".xml");
                t.type = None;
                break;
            }
            case RemoveBackup: {
                try {
                    boost::filesystem::remove(t.path + "/.3mf");
                }
                catch (...) {}
            }
        }
    }

public:
    void operator()() {
        boost::unique_lock lock(m_mutex);
        while (true)
        {
            while (m_tasks.empty()) {
                m_cond.timed_wait(lock, m_next_backup);
                if (m_interval > 0 && boost::get_system_time() > m_next_backup) {
                    m_tasks.push_back({ Backup, 0, std::string(), ++m_task_seq });
                    m_next_backup += boost::posix_time::seconds(m_interval);
                }
            }
            Task t = m_tasks.front();
            if (t.type == Exit) break;
            if (t.delay) {
                if (!delay_task(t, lock))
                    continue;
            }
            m_tasks.pop_front();
            auto callback = m_post_callback;
            lock.unlock();
            process_task(t);
            lock.lock();
            if (t.type > None) {
                m_ui_tasks.push_back(t);
                if (m_ui_tasks.size() == 1 && callback)
                    callback(0);
            }
        }
    }

    bool delay_task(Task& t, boost::unique_lock<boost::mutex> & lock) {
        // delay last task for 3 seconds after last modify
        auto now = boost::get_system_time();
        auto delay_expire = now + boost::posix_time::seconds(10); // must not delay over this time
        auto wait = now + boost::posix_time::seconds(3);
        while (true) {
            m_cond.timed_wait(lock, wait);
            if (m_tasks.size() != 1 || m_tasks.front().delay == t.delay)
                break;
            t.delay = m_tasks.front().delay;
            now = boost::get_system_time();
            if (now >= delay_expire)
                break;
            wait = now + boost::posix_time::seconds(3);
            if (wait > delay_expire)
                wait = delay_expire;
        };
        // task maybe canceled
        if (m_tasks.empty())
            return false;
        t = m_tasks.front();
        return true;
    }

private:
    boost::thread m_thread;
    boost::mutex m_mutex;
    boost::condition_variable m_cond;
    std::deque<Task> m_tasks;
    std::deque<Task> m_ui_tasks;
    size_t m_task_seq = 0;
    // param 0: should call run_ui_tasks
    // param 1: should backup current project
    std::function<void(int)> m_post_callback;
    long m_interval = 1 * 60;
    boost::system_time m_next_backup;
    Model m_temp_model; // visit only in main thread
    bool m_other_changes = false; // visit only in main thread
    bool m_other_changes_backup = false; // visit only in main thread
    std::vector<std::pair<ModelObject*, size_t>> m_gaurd_objects;
};


//BBS: add plate data list related logic
bool load_bbs_3mf(const char* path, DynamicPrintConfig* config, ConfigSubstitutionContext* config_substitutions, Model* model, PlateDataPtrs* plate_data_list, std::vector<Preset*>* project_presets, bool check_version, bool* is_bbl_3mf, bool load_aux, bool load_restore, Import3mfProgressFn proFn)
{
    if (path == nullptr || config == nullptr || model == nullptr)
        return false;

    // All import should use "C" locales for number formatting.
    CNumericLocalesSetter locales_setter;
    _BBS_3MF_Importer importer;
    bool res = importer.load_model_from_file(path, *model, *plate_data_list, *project_presets, *config, *config_substitutions, check_version, *is_bbl_3mf, load_aux, load_restore, proFn);
    importer.log_errors();
    handle_legacy_project_loaded(importer.version(), *config);
    return res;
}

//BBS: add plate data list related logic
bool store_bbs_3mf(const char* path, Model* model, PlateDataPtrs& plate_data_list, std::vector<Preset*>& project_presets, const DynamicPrintConfig* config, bool fullpath_sources, const std::vector<ThumbnailData*>& thumbnail_data, bool zip64, bool skip_static, Export3mfProgressFn proFn, bool silence)
{
    // All export should use "C" locales for number formatting.
    CNumericLocalesSetter locales_setter;

    if (path == nullptr || model == nullptr)
        return false;

    _BBS_3MF_Exporter exporter;
    bool res = exporter.save_model_to_file(path, *model, plate_data_list, project_presets, config, fullpath_sources, thumbnail_data, zip64, skip_static, proFn, silence);
    if (!res)
        exporter.log_errors();

    return res;
}

//BBS: release plate data list
void release_PlateData_list(PlateDataPtrs& plate_data_list)
{
    //clear
    for (unsigned int i = 0; i < plate_data_list.size(); i++)
    {
        delete plate_data_list[i];
    }
    plate_data_list.clear();

    return;
}

// backup interface

void save_object_mesh(ModelObject& object, size_t originId)
{
    if (!object.get_model() || !object.get_model()->is_need_backup())
        return;
    if (object.volumes.empty())
        return;
    _BBS_Backup_Manager::get().add_object_mesh(object, originId);
}

void delete_object_mesh(ModelObject& object)
{
    // not really remove
    // _BBS_Backup_Manager::get().remove_object_mesh(object);
}

void backup_soon()
{
    _BBS_Backup_Manager::get().backup_soon();
}

void remove_backup(Model& model, bool removeAll)
{
    _BBS_Backup_Manager::get().remove_backup(model, removeAll);
}

void set_backup_interval(long interval)
{
    _BBS_Backup_Manager::get().set_interval(interval);
}

void set_backup_callback(std::function<void(int)> callback)
{
    _BBS_Backup_Manager::get().set_post_callback(callback);
}

void run_backup_ui_tasks()
{
    _BBS_Backup_Manager::get().run_ui_tasks();
}

bool has_restore_data(std::string & path, std::string& origin)
{
    if (path.empty()) {
        origin = "<lock>";
        return false;
    }
    if (boost::filesystem::exists(path + "/lock.txt")) {
        std::string pid;
        boost::filesystem::load_string_file(path + "/lock.txt", pid);
        try {
            if (get_process_name(boost::lexical_cast<int>(pid)) ==
                get_process_name(0)) {
                origin = "<lock>";
                return false;
            }
        }
        catch (...) {
            return false;
        }
    }
    std::string file3mf = path + "/.3mf";
    if (!boost::filesystem::exists(file3mf))
        return false;
    if (!boost::filesystem::exists(path + "/object_map.txt"))
        return false;
    try {
        if (boost::filesystem::exists(path + "/origin.txt"))
            boost::filesystem::load_string_file(path + "/origin.txt", origin);
    }
    catch (...) {
    }
    path = file3mf;
    return true;
}

void put_other_changes()
{
    _BBS_Backup_Manager::get().put_other_changes();
}

bool has_other_changes(bool backup)
{
    return _BBS_Backup_Manager::get().has_other_changes(backup);
}

SaveObjectGaurd::SaveObjectGaurd(ModelObject& object)
{
    _BBS_Backup_Manager::get().push_object_gaurd(object);
}

SaveObjectGaurd::~SaveObjectGaurd()
{
    _BBS_Backup_Manager::get().pop_object_gaurd();
}

} // namespace Slic3r
