#ifndef BBS_3MF_hpp_
#define BBS_3MF_hpp_

namespace Slic3r {
class Model;
class DynamicPrintConfig;
struct ThumbnailData;

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
    bool locked;
};

typedef std::vector<PlateData*> PlateDataPtrs;

typedef std::map<int, PlateData*> PlateDataMaps;

//BBS: add plate data list related logic
// Load the content of a 3mf file into the given model and preset bundle.
extern bool load_bbs_3mf(const char* path, DynamicPrintConfig* config, ConfigSubstitutionContext* config_substitutions, Model* model, PlateDataPtrs* plate_data_list, bool check_version, bool* is_bbl_3mf);

//BBS: add plate data list related logic
// Save the given model and the config data contained in the given Print into a 3mf file.
// The model could be modified during the export process if meshes are not repaired or have no shared vertices
extern bool store_bbs_3mf(const char* path, Model* model, PlateDataPtrs& plate_data_list, const DynamicPrintConfig* config, bool fullpath_sources, const std::vector<ThumbnailData*>& thumbnail_data, bool zip64 = true);

extern void release_PlateData_list(PlateDataPtrs& plate_data_list);
} // namespace Slic3r

#endif /* BBS_3MF_hpp_ */
