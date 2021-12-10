#ifndef BBS_3MF_hpp_
#define BBS_3MF_hpp_

#include <functional>

namespace Slic3r {
class Model;
struct ConfigSubstitutionContext;
class DynamicPrintConfig;
struct ThumbnailData;

#define GCODE_FILE_FORMAT     "Metadata/plate_%1%.gcode"

//BBS: define plate data list related structures
struct PlateData
{
    PlateData(int plate_id, std::set<std::pair<int, int>> &obj_to_inst_list, bool lock_state) : plate_index(plate_id), locked(lock_state)
    {
        objects_and_instances.clear();
        for (std::set<std::pair<int, int>>::iterator it = obj_to_inst_list.begin(); it != obj_to_inst_list.end(); ++it)
            objects_and_instances.emplace_back(it->first, it->second);
    }
    PlateData() : plate_index(-1), locked(false)
    {
        objects_and_instances.clear();
    }
    ~PlateData()
    {
        objects_and_instances.clear();
    }
    int plate_index;
    std::vector<std::pair<int, int>> objects_and_instances;
    std::string     gcode_file;
    std::string     thumbnail_file;
    std::string     gcode_prediction;
    std::string     gcode_weight;
    bool            is_sliced_valid = false;

    std::string get_gcode_prediction_str() {
        return gcode_prediction;
    }

    std::string get_gcode_weight_str() {
        return gcode_weight;
    }
    bool locked;
};


const int EXPORT_STAGE_OPEN_3MF         = 0;
const int EXPORT_STAGE_CONTENT_TYPES    = 1;
const int EXPORT_STAGE_ADD_THUMBNAILS   = 2;
const int EXPORT_STAGE_ADD_RELATIONS    = 3;
const int EXPORT_STAGE_ADD_MODELS       = 4;
const int EXPORT_STAGE_ADD_LAYER_RANGE  = 5;
const int EXPORT_STAGE_ADD_SUPPORT      = 6;
const int EXPORT_STAGE_ADD_CUSTOM_GCODE = 7;
const int EXPORT_STAGE_ADD_PRINT_CONFIG = 8;
const int EXPORT_STAGE_ADD_CONFIG_FILE  = 9;
const int EXPORT_STAGE_ADD_SLICE_INFO   = 10;
const int EXPORT_STAGE_ADD_GCODE        = 11;
const int EXPORT_STAGE_ADD_AUXILIARIES  = 12;
const int EXPORT_STAGE_FINISH           = 13;


//BBS export 3mf progress
typedef std::function<void(int export_stage, int current, int total, bool& cancel)> Export3mfProgressFn;

typedef std::vector<PlateData*> PlateDataPtrs;

typedef std::map<int, PlateData*> PlateDataMaps;

//BBS: add plate data list related logic
// Load the content of a 3mf file into the given model and preset bundle.
extern bool load_bbs_3mf(const char* path, DynamicPrintConfig* config, ConfigSubstitutionContext* config_substitutions, Model* model, PlateDataPtrs* plate_data_list, bool check_version, bool* is_bbl_3mf, bool load_aux);

//BBS: add plate data list related logic
// Save the given model and the config data contained in the given Print into a 3mf file.
// The model could be modified during the export process if meshes are not repaired or have no shared vertices
extern bool store_bbs_3mf(const char* path, Model* model, PlateDataPtrs& plate_data_list, const DynamicPrintConfig* config, bool fullpath_sources, const std::vector<ThumbnailData*>& thumbnail_data, bool zip64 = true, bool skip_static = false, Export3mfProgressFn proFn = nullptr);

extern void release_PlateData_list(PlateDataPtrs& plate_data_list);
} // namespace Slic3r

#endif /* BBS_3MF_hpp_ */
