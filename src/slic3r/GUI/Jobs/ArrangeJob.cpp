#include "ArrangeJob.hpp"

#include "libslic3r/BuildVolume.hpp"
#include "libslic3r/SVG.hpp"
#include "libslic3r/MTUtils.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"

#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectManipulation.hpp"
#include "slic3r/GUI/NotificationManager.hpp"
#include "slic3r/GUI/format.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/NotificationManager.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"

#include "libnest2d/common.hpp"

#define SAVE_ARRANGE_POLY 0

namespace Slic3r { namespace GUI {
    using ArrangePolygon = arrangement::ArrangePolygon;

// Cache the wti info
class WipeTower: public GLCanvas3D::WipeTowerInfo {
public:
    explicit WipeTower(const GLCanvas3D::WipeTowerInfo &wti)
        : GLCanvas3D::WipeTowerInfo(wti)
    {}
    
    explicit WipeTower(GLCanvas3D::WipeTowerInfo &&wti)
        : GLCanvas3D::WipeTowerInfo(std::move(wti))
    {}

    void apply_arrange_result(const Vec2d& tr, double rotation, int item_id)
    {
        m_pos = unscaled(tr); m_rotation = rotation;
        apply_wipe_tower();
    }
    
    ArrangePolygon get_arrange_polygon() const
    {
        Polygon ap({
            {scaled(m_bb.min)},
            {scaled(m_bb.max.x()), scaled(m_bb.min.y())},
            {scaled(m_bb.max)},
            {scaled(m_bb.min.x()), scaled(m_bb.max.y())}
            });
        
        ArrangePolygon ret;
        ret.poly.contour = std::move(ap);
        ret.translation  = scaled(m_pos);
        ret.rotation     = m_rotation;
        ++ret.priority;

        return ret;
    }
};

static WipeTower get_wipe_tower(const Plater &plater)
{
    return WipeTower{plater.canvas3D()->get_wipe_tower_info()};
}

void ArrangeJob::clear_input()
{
    const Model &model = m_plater->model();
    
    size_t count = 0, cunprint = 0; // To know how much space to reserve
    for (auto obj : model.objects)
        for (auto mi : obj->instances)
            mi->printable ? count++ : cunprint++;
    
    m_selected.clear();
    m_unselected.clear();
    m_unprintable.clear();
    m_locked.clear();
    m_unarranged.clear();
    m_selected.reserve(count + 1 /* for optional wti */);
    m_unselected.reserve(count + 1 /* for optional wti */);
    m_unprintable.reserve(cunprint /* for optional wti */);
    m_locked.reserve(count + 1 /* for optional wti */);
    current_plate_index = 0;
}

ArrangePolygon ArrangeJob::prepare_arrange_polygon(void* model_instance)
{
    ModelInstance* instance = (ModelInstance*)model_instance;
    const Slic3r::DynamicPrintConfig& config = wxGetApp().preset_bundle->full_config();

    ArrangePolygon ap = get_arrange_poly(PtrWrapper{ instance }, m_plater);
    //BBS: add temperature information
    if (config.has("bed_temperature")) //get the bed temperature
        ap.bed_temp = config.opt_int("bed_temperature", ap.extrude_id - 1);
    if (config.has("temperature")) //get the print temperature
        ap.print_temp = config.opt_int("temperature", ap.extrude_id - 1);
    if (config.has("first_layer_bed_temperature")) //get the first_layer_bed_temperature
        ap.first_bed_temp = config.opt_int("first_layer_bed_temperature", ap.extrude_id - 1);
    if (config.has("first_layer_temperature")) //get the first_layer_temperature
        ap.first_print_temp = config.opt_int("first_layer_temperature", ap.extrude_id - 1);
    if (config.has("temperature_vitrification"))
        ap.vitrify_temp = config.opt_int("temperature_vitrification", ap.extrude_id - 1);

    ap.height = instance->get_object()->bounding_box().size().z();
    ap.name = instance->get_object()->name;
    return ap;
}

void ArrangeJob::prepare_selected() {
    PartPlateList& plate_list = m_plater->get_partplate_list();

    clear_input();
    
    Model &model = m_plater->model();
    //BBS: remove logic for unselected object
    //double stride = bed_stride_x(m_plater);
    
    std::vector<const Selection::InstanceIdxsList *>
            obj_sel(model.objects.size(), nullptr);
    
    for (auto &s : m_plater->get_selection().get_content())
        if (s.first < int(obj_sel.size()))
            obj_sel[size_t(s.first)] = &s.second;
    
    // Go through the objects and check if inside the selection
    for (size_t oidx = 0; oidx < model.objects.size(); ++oidx) {
        const Selection::InstanceIdxsList * instlist = obj_sel[oidx];
        ModelObject *mo = model.objects[oidx];
        
        std::vector<bool> inst_sel(mo->instances.size(), false);
        
        if (instlist)
            for (auto inst_id : *instlist)
                inst_sel[size_t(inst_id)] = true;
        
        for (size_t i = 0; i < inst_sel.size(); ++i) {
            ModelInstance * mi = mo->instances[i];
            ArrangePolygon&& ap = prepare_arrange_polygon(mo->instances[i]);
            //BBS: partplate_list preprocess
            //remove the locked plate's instances, neither in selected, nor in un-selected
            bool locked = plate_list.preprocess_arrange_polygon(oidx, i, ap, inst_sel[i]);
            if (!locked)
            {
                ArrangePolygons& cont = mo->instances[i]->printable ?
                    (inst_sel[i] ? m_selected :
                        m_unselected) :
                    m_unprintable;

                ap.itemid = cont.size();
                cont.emplace_back(std::move(ap));
            }
            else
            {
                //skip this object due to be locked in plate
                ap.itemid = m_locked.size();
                m_locked.emplace_back(std::move(ap));
                BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": skip locked instance, obj_id %1%, instance_id %2%") % oidx % i;
            }            
        }
    }

    if (auto wti = get_wipe_tower(*m_plater)) {
        ArrangePolygon &&ap = get_arrange_poly(wti, m_plater);

        auto &cont = m_plater->get_selection().is_wipe_tower() ? m_selected :
                                                                 m_unselected;
        cont.emplace_back(std::move(ap));
    }
    
    // If the selection was empty arrange everything
    if (m_selected.empty()) m_selected.swap(m_unselected);

    // The strides have to be removed from the fixed items. For the
    // arrangeable (selected) items bed_idx is ignored and the
    // translation is irrelevant.
    //BBS: remove logic for unselected object
    //for (auto &p : m_unselected) p.translation(X) -= p.bed_idx * stride;
}

arrangement::ArrangePolygon ArrangeJob::get_arrange_poly_(ModelInstance *mi)
{
    arrangement::ArrangePolygon ap = get_arrange_poly(mi, m_plater);

    auto setter = ap.setter;
    ap.setter = [this, setter, mi](const arrangement::ArrangePolygon &set_ap) {
        setter(set_ap);
        if (!set_ap.is_arranged())
            m_unarranged.emplace_back(mi);
    };

    return ap;
}

//BBS: prepare current part plate for arranging
void ArrangeJob::prepare_partplate() {
    clear_input();

    PartPlateList& plate_list = m_plater->get_partplate_list();
    PartPlate* plate = plate_list.get_curr_plate();
    current_plate_index = plate_list.get_curr_plate_index();
    assert(plate != nullptr);

    if (plate->empty())
    {
        //no instances on this plate
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": no instances in current plate!");

        return;
    }

    Model& model = m_plater->model();
    //BBS: remove logic for unselected object
    //double stride = bed_stride_x(m_plater);

    // Go through the objects and check if inside the selection
    for (size_t oidx = 0; oidx < model.objects.size(); ++oidx) 
    {
        ModelObject* mo = model.objects[oidx];
        for (size_t inst_idx = 0; inst_idx < mo->instances.size(); ++inst_idx)
        {
            bool in_plate = plate->contain_instance(oidx, inst_idx);
            ArrangePolygon&& ap = prepare_arrange_polygon(mo->instances[inst_idx]);

            ArrangePolygons& cont = mo->instances[inst_idx]->printable ?
                (in_plate ? m_selected : m_unselected) :
                m_unprintable;
            ap.itemid = cont.size();
            bool locked = plate_list.preprocess_arrange_polygon_other_locked(oidx, inst_idx, ap, in_plate);
            if (!locked)
            {
                cont.emplace_back(std::move(ap));
            }
            else
            {
                //skip this object due to be not in current plate, treated as locked
                m_locked.emplace_back(std::move(ap));
                BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": skip locked instance, obj_id %1%, instance_id %2%") % oidx % inst_idx;
            }
        }
    }


    //don't consider wipe_tower currently
    /*if (auto wti = get_wipe_tower(*m_plater)) {
        ArrangePolygon&& ap = get_arrange_poly(wti, m_plater);

        auto& cont = m_plater->get_selection().is_wipe_tower() ? m_selected :
            m_unselected;
        cont.emplace_back(std::move(ap));
    }*/

    // The strides have to be removed from the fixed items. For the
    // arrangeable (selected) items bed_idx is ignored and the
    // translation is irrelevant.
    //BBS: remove logic for unselected object
    //for (auto& p : m_unselected) p.translation(X) -= p.bed_idx * stride;
}

//BBS: add partplate logic
void ArrangeJob::prepare()
{
    wxGetApp().plater()->get_notification_manager()->push_notification(NotificationType::ArrangeOngoing, NotificationManager::NotificationLevel::RegularNotificationLevel, "Arranging the imported model...");

    int state = m_plater->get_prepare_state();
    if (state == Job::JobPrepareState::PREPARE_STATE_DEFAULT) {
        only_on_partplate = false;
        prepare_selected();
    }
    else if (state == Job::JobPrepareState::PREPARE_STATE_MENU) {
        only_on_partplate = true;   // only arrange items on current plate
        prepare_partplate();
    }

    //add the virtual object into unselect list if has
    m_plater->get_partplate_list().preprocess_exclude_areas(m_unselected, MAX_NUM_PLATES);

#if SAVE_ARRANGE_POLY
    for (auto it = m_selected.begin(); it != m_selected.end();it++) {
        BoundingBox bbox = get_extents(it->poly);
        SVG svg("SVG/"+it->name + "_arrange_poly.svg", bbox);
        svg.draw_grid(bbox,"gray",scale_(0.05));
        svg.draw_outline(it->poly);
    }
#endif

    check_unprintable();
}

void ArrangeJob::check_unprintable()
{
    for (auto it = m_selected.begin(); it != m_selected.end();) {
        if (it->poly.area() < 0.001)
        {
            m_unprintable.push_back(*it);
            wxGetApp().plater()->get_notification_manager()->push_plater_warning_notification((L("Object " + it->name + " has zero size and can't be printed!")));
            it = m_selected.erase(it);
        }
        else
            it++;
    }
}

void ArrangeJob::on_exception(const std::exception_ptr &eptr)
{
    try {
        if (eptr)
            std::rethrow_exception(eptr);
    } catch (libnest2d::GeometryException &) {
        show_error(m_plater, _(L("Could not arrange model objects! "
                                 "Some geometries may be invalid.")));
    } catch (std::exception &) {
        PlaterJob::on_exception(eptr);
    }
}

void ArrangeJob::process()
{
    const GLCanvas3D::ArrangeSettings &settings =
        static_cast<const GLCanvas3D*>(m_plater->canvas3D())->get_arrange_settings();
    auto& print = wxGetApp().plater()->get_partplate_list().get_current_fff_print();

    params.clearance_height_to_rod = print.config().extruder_clearance_height_to_rod.value;
    params.clearance_height_to_lid = print.config().extruder_clearance_height_to_lid.value;
    params.cleareance_radius = print.config().extruder_clearance_radius.value;
    params.allow_rotations = settings.enable_rotation;
    params.allow_multi_materials_on_same_plate = settings.allow_multi_materials_on_same_plate;
    params.is_seq_print = settings.is_seq_print;
    params.min_obj_distance = scaled(settings.distance);
    if (params.is_seq_print) params.min_obj_distance = std::max(params.min_obj_distance, scaled(params.cleareance_radius));

    double skirt_distance = print.has_skirt() ? print.config().skirt_distance.value : 0;
    bool is_auto_brim = print.has_auto_brim();
    double brim_max = 0;
    if (is_auto_brim) {
        brim_max = 0;
        std::for_each(m_selected.begin(), m_selected.end(), [&](ArrangePolygon ap) {  brim_max = std::max(brim_max, ap.auto_brim_width); });
    }
    else {
        brim_max = print.has_brim() ? print.default_object_config().brim_width : 0;
        std::for_each(m_selected.begin(), m_selected.end(), [&](ArrangePolygon ap) {  brim_max = std::max(brim_max, ap.user_brim_width); });
    }

    // Note: skirt_distance is now defined between outermost brim and skirt, not the object and skirt.
    // So we can't do max but do adding instead.
    params.brim_skirt_distance = skirt_distance + brim_max;
    params.bed_shrink_x = settings.bed_shrink_x + params.brim_skirt_distance;
    params.bed_shrink_y = settings.bed_shrink_y + params.brim_skirt_distance;
    
    // do not inflate brim_width. Objects are allowed to have overlapped brim.
    std::for_each(m_selected.begin(), m_selected.end(), [&](auto& ap) {ap.inflation = params.min_obj_distance / 2; });
    std::for_each(m_unselected.begin(), m_unselected.end(), [&](auto& ap) {ap.inflation = ap.is_virt_object ? scaled(params.brim_skirt_distance) : params.min_obj_distance / 2; });

    Points bedpts = get_bed_shape(*m_plater->config());

    if(0)
    { // subtract excluded region and get a polygon bed
        auto print_config = print.config();
        Pointfs excluse_area_points = print_config.bed_exclude_area.values;
        Polygons exclude_polys;
        Polygon exclude_poly;
        for (int i = 0; i < excluse_area_points.size(); i++) {
            auto pt = excluse_area_points[i];
            exclude_poly.points.emplace_back(scale_(pt.x()), scale_(pt.y()));
            if (i % 4 == 3) {  // exclude areas are always rectangle
                exclude_polys.push_back(exclude_poly);
                exclude_poly.points.clear();
            }
        }
        Polygon bed_polygon(bedpts);
        bed_polygon = diff({ bed_polygon }, exclude_polys)[0];
        bedpts = bed_polygon.points;
    }
     m_plater->get_partplate_list().preprocess_exclude_areas(params.excluded_regions, 1);

    // shrink bed by moving to center by dist
    auto shrinkFun = [](Points& bedpts, double dist, int direction) {
#define SGN(x) ((x)>=0?1:-1)
        Point center = Polygon(bedpts).bounding_box().center();
        for (auto& pt : bedpts)
            pt[direction] += dist * SGN(center[direction] - pt[direction]);
    };
    shrinkFun(bedpts, scaled(params.bed_shrink_x), 0);
    shrinkFun(bedpts, scaled(params.bed_shrink_y), 1);

    BOOST_LOG_TRIVIAL(debug) << "bed_shrink_x=" << params.bed_shrink_x
        << ", brim_max= "<<brim_max<<", "
        << "; bedpts:" << bedpts[0].transpose() << ", " << bedpts[1].transpose() << ", " << bedpts[2].transpose() << ", " << bedpts[3].transpose();
    
    params.stopcondition = [this]() { return was_canceled(); };
    
    auto count = unsigned(m_selected.size() + m_unprintable.size());
    params.progressind = [this, count](unsigned st, std::string str="") {
        st += m_unprintable.size();
        if (st > 0) update_status(int(count - st), str);
    };

    if(!params.is_seq_print)
    {
        // force all heights be the same, so items are sorted by area
        for (auto& ap : m_selected) ap.height = 1;
        for (auto& ap : m_unselected) ap.height = 1;
    }
    arrangement::arrange(m_selected, m_unselected, bedpts, params);

    // sort by item id
    std::sort(m_selected.begin(), m_selected.end(), [](auto a, auto b) {return a.itemid < b.itemid; });
    {
        BOOST_LOG_TRIVIAL(debug) << "items after arranging: ";
        for (auto selected : m_selected)
            BOOST_LOG_TRIVIAL(debug) << selected.name << ", extruder: " << selected.extrude_id << ", bed: " << selected.bed_idx;
    }

    params.progressind = [this, count](unsigned num_finished, std::string str="") {
        if (num_finished > 0) update_status(int(count - num_finished), str);
    };

    arrangement::arrange(m_unprintable, {}, bedpts, params);

    // put unpackable items to m_unprintable so they goes outside
    bool we_have_unpackable_items = false;
    for (auto item : m_selected) {
        if (item.bed_idx < 0) {
            //BBS: already processed in m_selected
            //m_unprintable.push_back(std::move(item));
            we_have_unpackable_items = true;
        }
    }

    // finalize just here.
    update_status(int(count),
        was_canceled() ? _(L("Arranging canceled.")) :
        we_have_unpackable_items ? _(L("Arranging done but we have unpacked items! Reduce spacing or bed_shrink and try again!")) : _(L("Arranging done.")));
}

static std::string concat_strings(const std::set<std::string> &strings,
                                  const std::string &delim = "\n")
{
    return std::accumulate(
        strings.begin(), strings.end(), std::string(""),
        [delim](const std::string &s, const std::string &name) {
            return s + name + delim;
        });
}

void ArrangeJob::finalize() {
    // Ignore the arrange result if aborted.
    if (was_canceled()) return;

    // Unprintable items go to the last virtual bed
    int beds = 0;

    //BBS: partplate
    PartPlateList& plate_list = m_plater->get_partplate_list();
    //clear all the relations before apply the arrangement results
    plate_list.clear();

    //BBS: adjust the bed_index, create new plates, get the max bed_index
    for (ArrangePolygon& ap : m_selected) {
        //if (ap.bed_idx < 0) continue;  // bed_idx<0 means unarrangable
        //BBS: partplate postprocess
        if (only_on_partplate)
            plate_list.postprocess_bed_index_for_current_plate(ap);
        else
            plate_list.postprocess_bed_index_for_selected(ap);

        beds = std::max(ap.bed_idx, beds);

        BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(":selected: bed_id %1%, trans {%2%,%3%}") % ap.bed_idx % unscale<double>(ap.translation(X)) % unscale<double>(ap.translation(Y));
    }

    //BBS: adjust the bed_index, create new plates, get the max bed_index
    for (ArrangePolygon& ap : m_unselected)
    {
        if (ap.is_virt_object)
            continue;

        //BBS: partplate postprocess
        if (!only_on_partplate)
            plate_list.postprocess_bed_index_for_unselected(ap);

        beds = std::max(ap.bed_idx, beds);
        BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(":unselected: bed_id %1%, trans {%2%,%3%}") % ap.bed_idx % unscale<double>(ap.translation(X)) % unscale<double>(ap.translation(Y));
    }

    for (ArrangePolygon& ap : m_locked) {
        beds = std::max(ap.bed_idx, beds);

        plate_list.postprocess_arrange_polygon(ap, false);

        ap.apply();
    }

    // Apply the arrange result to all selected objects
    for (ArrangePolygon& ap : m_selected) {
        //BBS: partplate postprocess
        plate_list.postprocess_arrange_polygon(ap, true);

        ap.apply();
    }

    // Apply the arrange result to unselected objects(due to the sukodu-style column changes, the position of unselected may also be modified)
    for (ArrangePolygon& ap : m_unselected)
    {
        if (ap.is_virt_object)
            continue;

        //BBS: partplate postprocess
        plate_list.postprocess_arrange_polygon(ap, false);

        ap.apply();
    }

    // Move the unprintable items to the last virtual bed.
    // Note ap.apply() moves relatively according to bed_idx, so we need to subtract the orignal bed_idx
    for (ArrangePolygon& ap : m_unprintable) {
        ap.bed_idx = beds + 1;
        plate_list.postprocess_arrange_polygon(ap, true);

        ap.apply();
        BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(":m_unprintable: bed_id %1%, trans {%2%,%3%}") % ap.bed_idx % unscale<double>(ap.translation(X)) % unscale<double>(ap.translation(Y));
    }

    m_plater->update();
    // BBS
    //wxGetApp().obj_manipul()->set_dirty();

    if (!m_unarranged.empty()) {
        std::set<std::string> names;
        for (ModelInstance *mi : m_unarranged)
            names.insert(mi->get_object()->name);

        m_plater->get_notification_manager()->push_notification(GUI::format(
            _L("Arrangement ignored the following objects which can't fit into a single bed:\n%s"),
            concat_strings(names, "\n")));
    }
    m_plater->get_notification_manager()->close_notification_of_type(NotificationType::ArrangeOngoing);

    //BBS: reload all objects due to arrange
    plate_list.rebuild_plates_after_arrangement(!only_on_partplate);

    // BBS: update slice context and gcode result.
    m_plater->update_slicing_context_to_current_partplate();

    wxGetApp().obj_list()->reload_all_plates();

    m_plater->update();

    Job::finalize();
}

std::optional<arrangement::ArrangePolygon>
get_wipe_tower_arrangepoly(const Plater &plater)
{
    if (auto wti = get_wipe_tower(plater))
        return get_arrange_poly(wti, &plater);

    return {};
}

//BBS: add sudoku-style stride
double bed_stride_x(const Plater* plater) {
    double bedwidth = plater->build_volume().bounding_box().size().x();
    return scaled<double>((1. + LOGICAL_BED_GAP) * bedwidth);
}

double bed_stride_y(const Plater* plater) {
    double beddepth = plater->build_volume().bounding_box().size().y();
    return scaled<double>((1. + LOGICAL_BED_GAP) * beddepth);
}

template<>
arrangement::ArrangePolygon get_arrange_poly(ModelInstance *inst,
                                             const Plater * plater)
{
    return get_arrange_poly(PtrWrapper{inst}, plater);
}

arrangement::ArrangeParams get_arrange_params(Plater *p)
{
    const GLCanvas3D::ArrangeSettings &settings =
        static_cast<const GLCanvas3D*>(p->canvas3D())->get_arrange_settings();

    arrangement::ArrangeParams params;
    params.allow_rotations  = settings.enable_rotation;
    params.min_obj_distance = scaled(settings.distance);
    //BBS: add specific params
    params.is_seq_print = settings.is_seq_print;
    params.bed_shrink_x = settings.bed_shrink_x;
    params.bed_shrink_y = settings.bed_shrink_y;

    return params;
}

}} // namespace Slic3r::GUI
