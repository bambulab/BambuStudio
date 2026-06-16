#include "Print.hpp"

#include "I18N.hpp"
#include "Thread.hpp"
#include "Time.hpp"
#include "Utils.hpp"

#include <set>

#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

#define L(s) Slic3r::I18N::translate(s)

namespace Slic3r {

namespace {

bool is_print_object_the_same(const PrintObject *object1, const PrintObject *object2)
{
    if (object1->trafo().matrix() != object2->trafo().matrix())
        return false;

    const ModelObject *model_obj1 = object1->model_object();
    const ModelObject *model_obj2 = object2->model_object();
    if (model_obj1->volumes.size() != model_obj2->volumes.size())
        return false;

    bool has_extruder1 = model_obj1->config.has("extruder");
    bool has_extruder2 = model_obj2->config.has("extruder");
    if ((has_extruder1 != has_extruder2) || (has_extruder1 && model_obj1->config.extruder() != model_obj2->config.extruder()))
        return false;

    for (int index = 0; index < model_obj1->volumes.size(); ++index) {
        const ModelVolume &model_volume1 = *model_obj1->volumes[index];
        const ModelVolume &model_volume2 = *model_obj2->volumes[index];
        if (model_volume1.type() != model_volume2.type())
            return false;
        if (model_volume1.mesh_ptr() != model_volume2.mesh_ptr())
            return false;
        if (! (model_volume1.get_transformation() == model_volume2.get_transformation()))
            return false;

        has_extruder1 = model_volume1.config.has("extruder");
        has_extruder2 = model_volume2.config.has("extruder");
        if ((has_extruder1 != has_extruder2) || (has_extruder1 && model_volume1.config.extruder() != model_volume2.config.extruder()))
            return false;
        if (! model_volume1.supported_facets.equals(model_volume2.supported_facets))
            return false;
        if (! model_volume1.fuzzy_skin_facets.equals(model_volume2.fuzzy_skin_facets))
            return false;
        if (! model_volume1.seam_facets.equals(model_volume2.seam_facets))
            return false;
        if (! model_volume1.mmu_segmentation_facets.equals(model_volume2.mmu_segmentation_facets))
            return false;
        if (model_volume1.config.get() != model_volume2.config.get())
            return false;
    }

    if (model_obj1->layer_config_ranges != model_obj2->layer_config_ranges)
        return false;
    if (model_obj1->layer_height_profile.get() != model_obj2->layer_height_profile.get())
        return false;
    if (model_obj1->config.get() != model_obj2->config.get())
        return false;
    return true;
}

} // namespace

void Print::prepare_shared_slicing_context(bool use_cache)
{
    for (PrintObject *obj : m_objects)
        obj->clear_shared_object();

    m_reslicing_objects.clear();

    if (! use_cache) {
        std::set<PrintObject *> slicing_objects;
        for (PrintObject *obj : m_objects) {
            for (PrintObject *slicing_obj : slicing_objects) {
                if (is_print_object_the_same(obj, slicing_obj)) {
                    obj->set_shared_object(slicing_obj);
                    break;
                }
            }
            if (! obj->get_shared_object()) {
                slicing_objects.insert(obj);
                m_reslicing_objects.insert(obj);
            }
        }
    } else {
        std::set<PrintObject *> slicing_objects;
        for (PrintObject *obj : m_objects) {
            if (obj->layer_count() > 0)
                slicing_objects.insert(obj);
        }
        for (PrintObject *obj : m_objects) {
            if (slicing_objects.count(obj) != 0)
                continue;

            bool found_shared = false;
            for (PrintObject *slicing_obj : slicing_objects) {
                if (is_print_object_the_same(obj, slicing_obj)) {
                    obj->set_shared_object(slicing_obj);
                    found_shared = true;
                    break;
                }
            }
            if (! found_shared) {
                BOOST_LOG_TRIVIAL(warning)
                    << boost::format("Also can not find the shared object, identify_id %1%, maybe shared object is skipped")
                           % obj->model_object()->instances[0]->loaded_id;
                slicing_objects.insert(obj);
                m_reslicing_objects.insert(obj);
            }
        }
    }

    BOOST_LOG_TRIVIAL(info)
        << __FUNCTION__ << boost::format(": total object counts %1% in current print, need to slice %2%") % m_objects.size() % m_reslicing_objects.size();
}

void Print::process_perimeters_stage(std::unordered_map<std::string, long long> *slice_time)
{
    long long start_time = 0;
    long long end_time   = 0;
    const AutoContourHolesCompensationParams auto_contour_holes_compensation_params = AutoContourHolesCompensationParams(m_config);

    if (slice_time)
        start_time = (long long) Slic3r::Utils::get_current_milliseconds_time_utc();

    for (PrintObject *obj : m_objects) {
        if (m_reslicing_objects.count(obj) != 0) {
            obj->set_auto_circle_compenstaion_params(auto_contour_holes_compensation_params);
            obj->make_perimeters();
        } else {
            if (obj->set_started(posSlice))
                obj->set_done(posSlice);
            if (obj->set_started(posPerimeters))
                obj->set_done(posPerimeters);
        }
    }

    if (slice_time) {
        end_time = (long long) Slic3r::Utils::get_current_milliseconds_time_utc();
        (*slice_time)[TIME_MAKE_PERIMETERS] += end_time - start_time;
    }
}

void Print::process_perimeters(std::unordered_map<std::string, long long> *slice_time, bool use_cache)
{
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

    for (PrintObject *obj : m_objects) {
        if (m_reslicing_objects.count(obj) == 0)
            obj->copy_layers_from_shared_object();
    }
}

} // namespace Slic3r
