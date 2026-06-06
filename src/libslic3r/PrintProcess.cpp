#include "Print.hpp"

#include "Brim.hpp"
#include "ClipperUtils.hpp"
#include "GCode.hpp"
#include "GCode/ConflictChecker.hpp"
#include "I18N.hpp"
#include "ShortestPath.hpp"
#include "ShortestPathExtras.hpp"
#include "Thread.hpp"
#include "Time.hpp"
#include "Utils.hpp"

#include <chrono>
#include <map>
#include <optional>

#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#define L(s) Slic3r::I18N::translate(s)

namespace Slic3r {

static std::map<ObjectID, unsigned int> get_object_extruder_map(const Print &print)
{
    std::map<ObjectID, unsigned int> object_extruder_map;
    for (const PrintObject *object : print.objects()) {
        if (object->object_first_layer_wall_extruders.empty()) {
            unsigned int object_first_layer_first_extruder = print.config().filament_diameter.size();
            auto         first_layer_regions                = object->layers().front()->regions();
            if (! first_layer_regions.empty()) {
                for (const LayerRegion *region_ptr : first_layer_regions) {
                    if (region_ptr->has_extrusions())
                        object_first_layer_first_extruder =
                            std::min(object_first_layer_first_extruder, region_ptr->region().extruder(frExternalPerimeter));
                }
            }
            object_extruder_map.insert(std::make_pair(object->id(), object_first_layer_first_extruder));
        } else {
            object_extruder_map.insert(std::make_pair(object->id(), object->object_first_layer_wall_extruders.front()));
        }
    }
    return object_extruder_map;
}

void Print::process(std::unordered_map<std::string, long long> *slice_time, bool use_cache)
{
    long long start_time = 0, end_time = 0;
    if (slice_time) {
        (*slice_time)[TIME_USING_CACHE]      = 0;
        (*slice_time)[TIME_MAKE_PERIMETERS]  = 0;
        (*slice_time)[TIME_INFILL]           = 0;
        (*slice_time)[TIME_GENERATE_SUPPORT] = 0;
    }

    name_tbb_thread_pool_threads_set_locale();

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": this=%1%, enter, use_cache=%2%, object size=%3%") % this % use_cache % m_objects.size();
    if (m_objects.empty())
        return;

    prepare_shared_slicing_context(use_cache);
    BOOST_LOG_TRIVIAL(info) << "Starting the slicing process." << log_memory_info();
    process_perimeters_stage(slice_time);

    if (slice_time)
        start_time = (long long) Slic3r::Utils::get_current_milliseconds_time_utc();

    for (PrintObject *obj : m_objects) {
        if (m_reslicing_objects.count(obj) != 0) {
            obj->infill();
        } else {
            if (obj->set_started(posPrepareInfill))
                obj->set_done(posPrepareInfill);
            if (obj->set_started(posInfill))
                obj->set_done(posInfill);
        }
    }

    if (slice_time) {
        end_time = (long long) Slic3r::Utils::get_current_milliseconds_time_utc();
        (*slice_time)[TIME_INFILL] += end_time - start_time;
    }

    for (PrintObject *obj : m_objects) {
        if (m_reslicing_objects.count(obj) != 0) {
            obj->ironing();
        } else {
            if (obj->set_started(posIroning))
                obj->set_done(posIroning);
        }
    }

    if (slice_time)
        start_time = (long long) Slic3r::Utils::get_current_milliseconds_time_utc();

    tbb::parallel_for(tbb::blocked_range<int>(0, int(m_objects.size())),
        [this](const tbb::blocked_range<int> &range) {
            for (int i = range.begin(); i < range.end(); ++i) {
                PrintObject *obj = m_objects[i];
                if (m_reslicing_objects.count(obj) != 0) {
                    obj->generate_support_material();
                } else {
                    if (obj->set_started(posSupportMaterial))
                        obj->set_done(posSupportMaterial);
                }
            }
        });

    if (slice_time) {
        end_time = (long long) Slic3r::Utils::get_current_milliseconds_time_utc();
        (*slice_time)[TIME_GENERATE_SUPPORT] += end_time - start_time;
    }

    for (PrintObject *obj : m_objects) {
        if (m_reslicing_objects.count(obj) != 0) {
            obj->detect_overhangs_for_lift();
        } else {
            if (obj->set_started(posDetectOverhangsForLift))
                obj->set_done(posDetectOverhangsForLift);
        }
    }

    for (PrintObject *obj : m_objects) {
        if (m_reslicing_objects.count(obj) == 0) {
            obj->copy_layers_from_shared_object();
            obj->copy_layers_overhang_from_shared_object();
        }
    }

    if (this->set_started(psWipeTower)) {
        {
            std::vector<std::set<int>> geometric_unprintables(m_config.nozzle_diameter.size());
            for (PrintObject *obj : m_objects) {
                std::vector<std::set<int>> obj_geometric_unprintables = obj->detect_extruder_geometric_unprintables();
                for (size_t idx = 0; idx < obj_geometric_unprintables.size(); ++ idx) {
                    if (idx < geometric_unprintables.size())
                        geometric_unprintables[idx].insert(obj_geometric_unprintables[idx].begin(), obj_geometric_unprintables[idx].end());
                }
            }
            this->set_geometric_unprintable_filaments(geometric_unprintables);
        }

        std::unordered_map<int, std::unordered_map<int, double>> filament_print_time;
        if (calib_params().mode == CalibMode::Calib_None) {
            for (PrintObject *obj : m_objects) {
                auto obj_filament_print_time = obj->calc_estimated_filament_print_time();
                for (auto [filament_idx, extruder_time] : obj_filament_print_time)
                    for (auto [extruder_idx, time] : extruder_time)
                        filament_print_time[filament_idx][extruder_idx] += time;
            }
        }
        this->set_filament_print_time(filament_print_time);

        m_wipe_tower_data.clear();
        m_tool_ordering.clear();
        m_sequential_print_data.reset();

        if (this->has_wipe_tower()) {
            m_nozzle_group_result.reset();
            this->_make_wipe_tower();
        } else if (! is_sequential_print()) {
            m_nozzle_group_result.reset();
            m_tool_ordering = ToolOrdering(*this, -1, false);
            m_tool_ordering.sort_and_build_data(*this, -1, false);
            if (m_tool_ordering.empty() || m_tool_ordering.last_extruder() == unsigned(-1))
                throw Slic3r::SlicingError("The print is empty. The model is not printable with current print settings.");
        }
        this->set_done(psWipeTower);
    }

    if (this->has_wipe_tower())
        m_fake_wipe_tower.set_pos({ m_config.wipe_tower_x.get_at(m_plate_index), m_config.wipe_tower_y.get_at(m_plate_index) });

    if (this->set_started(psSkirtBrim)) {
        this->set_status(70, L("Generating skirt & brim"));

        if (slice_time)
            start_time = (long long) Slic3r::Utils::get_current_milliseconds_time_utc();

        m_skirt.clear();
        m_skirt_convex_hull.clear();
        m_first_layer_convex_hull.points.clear();
        for (PrintObject *object : m_objects)
            object->m_skirt.clear();

        const bool draft_shield = config().draft_shield != dsDisabled;
        if (this->has_skirt() && draft_shield)
            _make_skirt();

        ToolOrdering                                                       tool_ordering;
        bool                                                               has_wipe_tower = false;
        std::vector<const PrintInstance *>                                 print_object_instances_ordering;
        std::vector<std::pair<coordf_t, std::vector<GCode::LayerToPrint>>> layers_to_print = GCode::collect_layers_to_print(*this);
        std::vector<unsigned int>                                          printExtruders;

        if (is_sequential_print()) {
            m_sequential_print_data = ByObjectPrintData::build(this);

            std::vector<unsigned int> first_layer_filaments;
            std::vector<unsigned int> used_filaments;

            for (auto [object, ordering] : m_sequential_print_data->object_tool_ordering_map) {
                auto &layer_tools = ordering.layer_tools();
                if (layer_tools.empty())
                    continue;

                auto object_first_layer_filaments = layer_tools.front().extruders;
                first_layer_filaments.insert(first_layer_filaments.end(), object_first_layer_filaments.begin(), object_first_layer_filaments.end());
                used_filaments.insert(used_filaments.end(), ordering.all_extruders().begin(), ordering.all_extruders().end());
            }
            sort_remove_duplicates(first_layer_filaments);
            sort_remove_duplicates(used_filaments);

            printExtruders = first_layer_filaments;
            this->set_slice_used_filaments(first_layer_filaments, used_filaments);
            print_object_instances_ordering = chain_print_object_instances(*this);
        } else {
            tool_ordering = this->tool_ordering();
            tool_ordering.assign_custom_gcodes(*this);

            std::vector<unsigned int> first_layer_used_filaments;
            if (! tool_ordering.layer_tools().empty())
                first_layer_used_filaments = tool_ordering.layer_tools().front().extruders;

            this->set_slice_used_filaments(first_layer_used_filaments, tool_ordering.all_extruders());
            has_wipe_tower = this->has_wipe_tower() && tool_ordering.has_wipe_tower();
            print_object_instances_ordering = chain_print_object_instances(*this);
            append(printExtruders, tool_ordering.tools_for_layer(layers_to_print.front().first).extruders);
        }

        auto object_extruder_map = get_object_extruder_map(*this);
        {
            const LayerTools *first_lt = nullptr;
            if (! is_sequential_print() && ! tool_ordering.layer_tools().empty())
                first_lt = &tool_ordering.layer_tools().front();

            std::map<ObjectID, const PrintObject *> obj_by_id;
            if (m_sequential_print_data) {
                for (const PrintObject *obj : m_objects)
                    obj_by_id[obj->id()] = obj;
            }

            for (auto &[obj_id, ext_1based] : object_extruder_map) {
                if (ext_1based == 0)
                    continue;
                const LayerTools *lt = first_lt;
                if (! lt && m_sequential_print_data) {
                    auto oid_it = obj_by_id.find(obj_id);
                    if (oid_it != obj_by_id.end()) {
                        auto it = m_sequential_print_data->object_tool_ordering_map.find(oid_it->second);
                        if (it != m_sequential_print_data->object_tool_ordering_map.end() && ! it->second.layer_tools().empty())
                            lt = &it->second.layer_tools().front();
                    }
                }
                if (lt) {
                    auto it = lt->mixed_filament_resolution.find(ext_1based - 1);
                    if (it != lt->mixed_filament_resolution.end())
                        ext_1based = it->second + 1;
                }
            }
        }

        std::vector<std::pair<ObjectID, unsigned int>> objPrintVec;
        for (const PrintInstance *instance : print_object_instances_ordering) {
            const ObjectID &print_object_ID = instance->print_object->id();
            bool            existObject     = false;
            for (auto &objIDPair : objPrintVec) {
                if (print_object_ID == objIDPair.first)
                    existObject = true;
            }
            if (! existObject && object_extruder_map.find(print_object_ID) != object_extruder_map.end())
                objPrintVec.push_back(std::make_pair(print_object_ID, object_extruder_map.at(print_object_ID)));
        }

        m_brimMap.clear();
        m_supportBrimMap.clear();
        m_first_layer_convex_hull.points.clear();
        if (this->has_brim()) {
            Polygons islands_area;
            make_brim(*this, this->make_try_cancel(), islands_area, m_brimMap, m_supportBrimMap, objPrintVec, printExtruders);
            for (Polygon &poly_ex : islands_area)
                poly_ex.douglas_peucker(SCALED_RESOLUTION);
            for (Polygon &poly : union_(this->first_layer_islands(), islands_area))
                append(m_first_layer_convex_hull.points, std::move(poly.points));
        }

        if (has_skirt() && ! draft_shield) {
            assert(m_skirt.empty());
            _make_skirt();
        }

        this->finalize_first_layer_convex_hull();
        this->set_done(psSkirtBrim);

        if (slice_time) {
            end_time = (long long) Slic3r::Utils::get_current_milliseconds_time_utc();
            (*slice_time)[TIME_USING_CACHE] += end_time - start_time;
        }
    }

    for (PrintObject *obj : m_objects) {
        if (m_reslicing_objects.count(obj) != 0) {
            obj->simplify_extrusion_path();
        } else {
            if (obj->set_started(posSimplifyWall))
                obj->set_done(posSimplifyWall);
            if (obj->set_started(posSimplifyInfill))
                obj->set_done(posSimplifyInfill);
            if (obj->set_started(posSimplifySupportPath))
                obj->set_done(posSimplifySupportPath);
        }
    }

    for (PrintObject *obj : m_objects) {
        if (! obj->model_object()->layer_height_profile.empty())
            break;
    }
    if (! m_no_check) {
        using Clock = std::chrono::high_resolution_clock;
        auto startTime = Clock::now();
        std::optional<const FakeWipeTower *> wipe_tower_opt = {};
        if (this->has_wipe_tower()) {
            m_fake_wipe_tower.set_pos({ m_config.wipe_tower_x.get_at(m_plate_index), m_config.wipe_tower_y.get_at(m_plate_index) });
            wipe_tower_opt = std::make_optional<const FakeWipeTower *>(&m_fake_wipe_tower);
        }
        auto conflictRes = ConflictChecker::find_inter_of_lines_in_diff_objs(m_objects, wipe_tower_opt);
        auto endTime     = Clock::now();
        volatile double seconds = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count() / (double) 1000;
        BOOST_LOG_TRIVIAL(info) << "gcode path conflicts check takes " << seconds << " secs.";

        m_conflict_result = conflictRes;
        if (conflictRes.has_value())
            BOOST_LOG_TRIVIAL(error) << boost::format("gcode path conflicts found between %1% and %2%") % conflictRes.value()._objName1 % conflictRes.value()._objName2;
    }

    BOOST_LOG_TRIVIAL(info) << "Slicing process finished." << log_memory_info();
}

} // namespace Slic3r
