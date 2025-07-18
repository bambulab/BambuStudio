#include "ArrangeJob.hpp"

#include "libslic3r/SVG.hpp"
#include "libslic3r/ModelArrange.hpp"
#include "libslic3r/VectorFormatter.hpp"

#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/NotificationManager.hpp"
#include "slic3r/GUI/format.hpp"

#include "libnest2d/common.hpp"

#define SAVE_ARRANGE_POLY 0
#define ARRANGE_LOG(level) BOOST_LOG_TRIVIAL(level) << "arrange: "

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

    void apply_arrange_result(const Vec2d& tr, double rotation, int item_id, int bed_id)
    {
        m_pos = unscaled(tr); m_rotation = rotation;
        m_plate_idx = bed_id;
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
        //BBS
        ret.name = "WipeTower";
        ret.is_virt_object = true;
        ret.is_wipe_tower = true;
        ++ret.priority;

        ARRANGE_LOG(debug) << " wipe tower info:" << m_bb << ", m_pos: " << m_pos.transpose();

        return ret;
    }
};

// BBS: add partplate logic
static WipeTower get_wipe_tower(const Plater &plater, int plate_idx)
{
    return WipeTower{plater.canvas3D()->get_wipe_tower_info(plate_idx)};
}

arrangement::ArrangePolygon get_wipetower_arrange_poly(WipeTower* tower)
{
    ArrangePolygon ap = tower->get_arrange_polygon();
    ap.bed_idx = 0;
    //ap.setter = NULL; // do not move wipe tower
    ap.setter = [tower](const ArrangePolygon &p) {
        if (p.is_arranged()) {
            Vec2d t = p.translation.cast<double>();
            tower->apply_arrange_result(t, p.rotation, p.itemid, p.bed_idx);
        }
    };
    return ap;
}

void ArrangeJob::clear_input()
{
    const Model &model = m_plater->model();

    size_t count = 0, cunprint = 0; // To know how much space to reserve
    for (auto obj : model.objects)
        for (auto mi : obj->instances)
            mi->printable ? count++ : cunprint++;

    params.nonprefered_regions.clear();
    m_selected.clear();
    m_unselected.clear();
    m_unprintable.clear();
    m_locked.clear();
    m_uncompatible_plates.clear();
    m_selected.reserve(count + 1 /* for optional wti */);
    m_unselected.reserve(count + 1 /* for optional wti */);
    m_unprintable.reserve(cunprint /* for optional wti */);
    m_locked.reserve(count + 1 /* for optional wti */);
    current_plate_index = 0;
}

ArrangePolygon ArrangeJob::prepare_arrange_polygon(void *model_instance)
{
    ModelInstance* instance = (ModelInstance*)model_instance;
    auto preset_bundle = wxGetApp().preset_bundle;
    const Slic3r::DynamicPrintConfig& config = preset_bundle->full_config();
    ArrangePolygon ap = get_instance_arrange_poly(instance, config);
    return ap;
}

void ArrangeJob::prepare_selected() {
    PartPlateList& plate_list = m_plater->get_partplate_list();

    clear_input();

    Model& model = m_plater->model();
    bool selected_is_locked = false;
    //BBS: remove logic for unselected object
    //double stride = bed_stride_x(m_plater);

    std::vector<const Selection::InstanceIdxsList*>
        obj_sel(model.objects.size(), nullptr);

    for (auto& s : m_plater->get_selection().get_content())
        if (s.first < int(obj_sel.size()))
            obj_sel[size_t(s.first)] = &s.second;

    // Go through the objects and check if inside the selection
    for (size_t oidx = 0; oidx < model.objects.size(); ++oidx) {
        const Selection::InstanceIdxsList* instlist = obj_sel[oidx];
        ModelObject* mo = model.objects[oidx];

        std::vector<bool> inst_sel(mo->instances.size(), false);

        if (instlist)
            for (auto inst_id : *instlist)
                inst_sel[size_t(inst_id)] = true;

        for (size_t i = 0; i < inst_sel.size(); ++i) {
            ModelInstance* mi = mo->instances[i];
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
                if (inst_sel[i])
                    selected_is_locked = true;
                ARRANGE_LOG(debug) << __FUNCTION__ << boost::format(": skip locked instance, obj_id %1%, instance_id %2%, name %3%") % oidx % i % mo->name;
                }
            }
        }


    // If the selection was empty arrange everything
    //if (m_selected.empty()) m_selected.swap(m_unselected);
    if (m_selected.empty()) {
        if (!selected_is_locked)
            m_selected.swap(m_unselected);
        else {
            m_plater->get_notification_manager()->push_notification(NotificationType::BBLPlateInfo,
                NotificationManager::NotificationLevel::WarningNotificationLevel, into_u8(_L("All the selected objects are on the locked plate,\nWe can not do auto-arrange on these objects.")));
            }
        }

    prepare_wipe_tower();


    // The strides have to be removed from the fixed items. For the
    // arrangeable (selected) items bed_idx is ignored and the
    // translation is irrelevant.
    //BBS: remove logic for unselected object
    //for (auto &p : m_unselected) p.translation(X) -= p.bed_idx * stride;
}

void ArrangeJob::prepare_all() {
    clear_input();

    PartPlateList& plate_list = m_plater->get_partplate_list();
    for (size_t i = 0; i < plate_list.get_plate_count(); i++) {
        PartPlate* plate = plate_list.get_plate(i);
        bool same_as_global_print_seq = true;
        plate->get_real_print_seq(&same_as_global_print_seq);
        if (plate->is_locked() == false && !same_as_global_print_seq) {
            plate->lock(true);
            m_uncompatible_plates.push_back(i);
        }
    }


    Model &model = m_plater->model();
    bool selected_is_locked = false;

    // Go through the objects and check if inside the selection
    for (size_t oidx = 0; oidx < model.objects.size(); ++oidx) {
        ModelObject *mo = model.objects[oidx];

        for (size_t i = 0; i < mo->instances.size(); ++i) {
            ModelInstance * mi = mo->instances[i];
            ArrangePolygon&& ap = prepare_arrange_polygon(mo->instances[i]);
            //BBS: partplate_list preprocess
            //remove the locked plate's instances, neither in selected, nor in un-selected
            bool locked = plate_list.preprocess_arrange_polygon(oidx, i, ap, true);
            if (!locked)
            {
                ArrangePolygons& cont = mo->instances[i]->printable ? m_selected :m_unprintable;

                ap.itemid = cont.size();
                cont.emplace_back(std::move(ap));
            }
            else
            {
                //skip this object due to be locked in plate
                ap.itemid = m_locked.size();
                m_locked.emplace_back(std::move(ap));
                selected_is_locked = true;
                ARRANGE_LOG(debug) << __FUNCTION__ << boost::format(": skip locked instance, obj_id %1%, instance_id %2%") % oidx % i;
            }
        }
    }


    if (m_selected.empty()) {
        if (!selected_is_locked) {
            m_plater->get_notification_manager()->push_notification(NotificationType::BBLPlateInfo,
                NotificationManager::NotificationLevel::WarningNotificationLevel, into_u8(_L("No arrangeable objects are selected.")));
        }
        else {
            m_plater->get_notification_manager()->push_notification(NotificationType::BBLPlateInfo,
                NotificationManager::NotificationLevel::WarningNotificationLevel, into_u8(_L("All the selected objects are on the locked plate,\nWe can not do auto-arrange on these objects.")));
        }
    }
    if (!m_uncompatible_plates.empty()) {
        auto msg = _L("The following plates are skipped due to different arranging settings from global:");
        for (int i : m_uncompatible_plates) { msg += "\n"+_L("Plate") + " " + std::to_string(i + 1);
        }
        m_plater->get_notification_manager()->push_notification(NotificationType::BBLPlateInfo,
                       NotificationManager::NotificationLevel::WarningNotificationLevel, into_u8(msg));
    }

    prepare_wipe_tower();

    // add the virtual object into unselect list if has
    plate_list.preprocess_exclude_areas(m_unselected, MAX_NUM_PLATES);
}

arrangement::ArrangePolygon estimate_wipe_tower_info(int plate_index, std::set<int>& extruder_ids)
{
    PartPlateList& ppl = wxGetApp().plater()->get_partplate_list();
    const auto& full_config = wxGetApp().preset_bundle->full_config();
    int plate_count = ppl.get_plate_count();
    int plate_index_valid = std::min(plate_index, plate_count - 1);

    // we have to estimate the depth using the extruder number of all plates
    int extruder_size = extruder_ids.size();

    Vec3d wipe_tower_size, wipe_tower_pos;
    int nozzle_nums = wxGetApp().preset_bundle->get_printer_extruder_count();
    auto arrange_poly = ppl.get_plate(plate_index_valid)->estimate_wipe_tower_polygon(full_config, plate_index, wipe_tower_pos, wipe_tower_size, nozzle_nums, extruder_size);
    arrange_poly.bed_idx = plate_index;
    return arrange_poly;
}

// 准备料塔。逻辑如下：
// 1. 以下几种情况不需要料塔：
//    1）料塔被禁用，
//    2）逐件打印，
//    3）不允许不同材料落在相同盘，且没有多色对象
// 2. 以下情况需要料塔：
//    1）某对象是多色对象；
//    2）打开了支撑，且支撑体与接触面使用的是不同材料
//    3）允许不同材料落在相同盘，且所有选定对象中使用了多种热床温度相同的材料
//     （所有对象都是单色的，但不同对象的材料不同，例如：对象A使用红色PLA，对象B使用白色PLA）
void ArrangeJob::prepare_wipe_tower(bool select)
{
    bool need_wipe_tower = false;

    // if wipe tower is explicitly disabled, no need to estimate
    DynamicPrintConfig& current_config = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    auto                op = current_config.option("enable_prime_tower");
    bool enable_prime_tower = op && op->getBool();
    if (!enable_prime_tower || params.is_seq_print) return;

    bool smooth_timelapse = false;
    auto sop = current_config.option("timelapse_type");
    if (sop) { smooth_timelapse = sop->getInt() == TimelapseType::tlSmooth; }
    if (smooth_timelapse) { need_wipe_tower = true; }

    // estimate if we need wipe tower for all plates:
    // need wipe tower if some object has multiple extruders (has paint-on colors or support material)
    for (const auto& item : m_selected) {
        if (item.extrude_id_filament_types.size() > 1) {
            need_wipe_tower = true;
            ARRANGE_LOG(info) << "need wipe tower because object " << item.name << " has multiple extruders (has paint-on colors)";
            break;
        }
    }

    // if multile extruders have same bed temp, we need wipe tower
    // 允许不同材料落在相同盘，且所有选定对象中使用了多种热床温度相同的材料
    if (params.allow_multi_materials_on_same_plate) {
        std::map<int, std::set<int>> bedTemp2extruderIds;
        for (const auto& item : m_selected)
            for (auto id : item.extrude_id_filament_types) { bedTemp2extruderIds[item.bed_temp].insert(id.first); }
        for (const auto& be : bedTemp2extruderIds) {
            if (be.second.size() > 1) {
                need_wipe_tower = true;
                ARRANGE_LOG(info) << "need wipe tower because allow_multi_materials_on_same_plate=true and we have multiple extruders of same type";
                break;
            }
        }
    }
    ARRANGE_LOG(info) << "need_wipe_tower=" << need_wipe_tower;


    ArrangePolygon    wipe_tower_ap;
    wipe_tower_ap.name = "WipeTower";
    wipe_tower_ap.is_virt_object = true;
    wipe_tower_ap.is_wipe_tower = true;
    const GLCanvas3D* canvas3D = static_cast<const GLCanvas3D*>(m_plater->canvas3D());

    std::set<int> extruder_ids;
    PartPlateList& ppl = wxGetApp().plater()->get_partplate_list();
    int plate_count = ppl.get_plate_count();
    if (!only_on_partplate) {
        extruder_ids = ppl.get_extruders(true);
    }

    int bedid_unlocked = 0;
    for (int bedid = 0; bedid < MAX_NUM_PLATES; bedid++) {
        int plate_index_valid = std::min(bedid, plate_count - 1);
        PartPlate* pl = ppl.get_plate(plate_index_valid);
        if(bedid<plate_count && pl->is_locked())
            continue;
        if (auto wti = get_wipe_tower(*m_plater, bedid)) {
            // wipe tower is already there
            wipe_tower_ap = get_wipetower_arrange_poly(&wti);
            wipe_tower_ap.bed_idx = bedid_unlocked;
            wipe_tower_ap.name    = "WipeTower" + std::to_string(bedid_unlocked);
            if (select)
                m_selected.emplace_back(wipe_tower_ap);
            else
                m_unselected.emplace_back(wipe_tower_ap);
        }
        else if (need_wipe_tower) {
            if (only_on_partplate) {
                auto plate_extruders = pl->get_extruders(true);
                extruder_ids.clear();
                extruder_ids.insert(plate_extruders.begin(), plate_extruders.end());
            }
            wipe_tower_ap = estimate_wipe_tower_info(bedid, extruder_ids);
            wipe_tower_ap.bed_idx = bedid_unlocked;
            m_unselected.emplace_back(wipe_tower_ap);
        }
        bedid_unlocked++;
    }
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
        ARRANGE_LOG(info) << __FUNCTION__ << boost::format(": no instances in current plate!");

        return;
    }

    if (plate->is_locked()) {
        m_plater->get_notification_manager()->push_notification(NotificationType::BBLPlateInfo,
            NotificationManager::NotificationLevel::WarningNotificationLevel, into_u8(_L("This plate is locked,\nWe can not do auto-arrange on this plate.")));
        return;
    }

    Model& model = m_plater->model();

    // Go through the objects and check if inside the selection
    for (size_t oidx = 0; oidx < model.objects.size(); ++oidx)
    {
        ModelObject* mo = model.objects[oidx];
        for (size_t inst_idx = 0; inst_idx < mo->instances.size(); ++inst_idx)
        {
            bool             in_plate = plate->contain_instance(oidx, inst_idx) || plate->intersect_instance(oidx, inst_idx);
            ArrangePolygon&& ap = prepare_arrange_polygon(mo->instances[inst_idx]);

            ArrangePolygons &cont   = mo->instances[inst_idx]->printable ? m_selected : m_unprintable;
            bool locked = plate_list.preprocess_arrange_polygon_other_locked(oidx, inst_idx, ap, in_plate);
            if (!locked && in_plate)
            {
                ap.itemid = cont.size();
                cont.emplace_back(std::move(ap));
            }
            else
            {
                //skip this object due to be not in current plate, treated as locked
                ap.itemid = m_locked.size();
                m_locked.emplace_back(std::move(ap));
                //ARRANGE_LOG(debug) << __FUNCTION__ << boost::format(": skip locked instance, obj_id %1%, name %2%") % oidx % mo->name;
            }
        }
    }

    // BBS
    if (auto wti = get_wipe_tower(*m_plater, current_plate_index)) {
        ArrangePolygon&& ap = get_wipetower_arrange_poly(&wti);
        m_unselected.emplace_back(std::move(ap));
    }

    // add the virtual object into unselect list if has
    plate_list.preprocess_exclude_areas(m_unselected, current_plate_index + 1);
}

void ArrangeJob::prepare_outside_plate() {
    clear_input();
    typedef std::tuple<int, int, int>         obj_inst_plate_t;
    std::map<std::pair<int,int>,int>        all_inside_objects;
    std::map<std::pair<int, int>, int>      all_outside_objects;

    Model         &model      = m_plater->model();
    PartPlateList &plate_list = m_plater->get_partplate_list();
    //collect all the objects outside
    for (int plate_idx = 0; plate_idx < plate_list.get_plate_count(); plate_idx++) {
        PartPlate *plate = plate_list.get_plate(plate_idx);
        assert(plate != nullptr);
        std::set<std::pair<int, int>>& plate_objects = plate->get_obj_and_inst_set();
        std::set<std::pair<int, int>>& plate_outside_objects = plate->get_obj_and_inst_outside_set();
        if (plate_objects.empty()) {
            // no instances on this plate
            ARRANGE_LOG(info) << __FUNCTION__ << format(": no instances in plate %d!", plate_idx);
            continue;
        }

        for (auto &obj_inst : plate_objects) { all_inside_objects[obj_inst] = plate_idx; }
        for (auto &obj_inst : plate_outside_objects) { all_outside_objects[obj_inst] = plate_idx; }

        if (plate->is_locked()) {
            ARRANGE_LOG(info) << __FUNCTION__ << format(": skip locked plate %d!", plate_idx);
            continue;
        }

        // if there are objects inside the plate, lock the plate and don't put new objects in it
        //if (plate_objects.size() > plate_outside_objects.size()) {
        //    plate->lock(true);
        //    m_uncompatible_plates.push_back(plate_idx);
        //    ARRANGE_LOG(info) << __FUNCTION__ << format(": lock plate %d because there are objects inside!", plate_idx);
        //}
    }

    std::set<int> locked_plates;
    for (int obj_idx = 0; obj_idx < model.objects.size(); obj_idx++) {
        ModelObject *object = model.objects[obj_idx];
        for (size_t inst_idx = 0; inst_idx < object->instances.size(); ++inst_idx) {
            ModelInstance * instance = object->instances[inst_idx];

            auto iter1 = all_inside_objects.find(std::pair(obj_idx, inst_idx));
            auto iter2 = all_outside_objects.find(std::pair(obj_idx, inst_idx));
            bool outside_plate = false;
            PartPlate *plate_locked         = nullptr;
            if (iter1 == all_inside_objects.end()) {
                continue;
            } else {
                int plate_idx = iter1->second;
                if (plate_list.get_plate(plate_idx)->is_locked()) {
                    plate_locked = plate_list.get_plate(plate_idx);
                    locked_plates.insert(plate_idx);
                }
            }
            if (iter2 != all_outside_objects.end()) {
                outside_plate = true;
                ARRANGE_LOG(debug) << object->name << " is outside!";
            }
            ArrangePolygon&& ap = prepare_arrange_polygon(instance);
            ArrangePolygons &cont = !instance->printable ? m_unprintable : plate_locked ? m_locked : outside_plate ? m_selected : m_unselected;
            ap.itemid                      = cont.size();
            if (!outside_plate) {
                plate_list.preprocess_arrange_polygon(obj_idx, inst_idx, ap, false);
                ap.bed_idx      = iter1->second;
                ap.locked_plate = iter1->second;
            }
            cont.emplace_back(std::move(ap));
        }
    }

    if (!locked_plates.empty()) {
        std::sort(m_unselected.begin(), m_unselected.end(), [](auto &ap1, auto &ap2) { return ap1.bed_idx < ap2.bed_idx; });
        for (auto &ap : m_unselected) {
            int locked_plate_count = 0;
            for (auto &plate_idx : locked_plates) {
                if (plate_idx < ap.bed_idx)
                    locked_plate_count++;
                else
                    break;
            }
            ap.bed_idx -= locked_plate_count;
        }
    }

    prepare_wipe_tower(true);

    // add the virtual object into unselect list if has
    plate_list.preprocess_exclude_areas(m_unselected, current_plate_index + 1);
}

//BBS: add partplate logic
void ArrangeJob::prepare()
{
    params = init_arrange_params(m_plater);

    //BBS update extruder params and speed table before arranging
    const Slic3r::DynamicPrintConfig& config = wxGetApp().preset_bundle->full_config();
    auto& print = wxGetApp().plater()->get_partplate_list().get_current_fff_print();
    auto print_config = print.config();
    int filament_count = wxGetApp().preset_bundle->filament_presets.size();

    Model::setExtruderParams(config, filament_count);
    Model::setPrintSpeedTable(config, print_config);

    int state = m_plater->get_prepare_state();
    if (state == Job::JobPrepareState::PREPARE_STATE_DEFAULT) {
        only_on_partplate = false;
        prepare_all();
    }
    else if (state == Job::JobPrepareState::PREPARE_STATE_MENU) {
        only_on_partplate = true;   // only arrange items on current plate
        prepare_partplate();
    } else if (state == Job::JobPrepareState::PREPARE_STATE_OUTSIDE_BED) {
        only_on_partplate = false;
        prepare_outside_plate();
    }

    ARRANGE_LOG(info) << "prepare state: " << state << ", items selected : " << m_selected.size();

#if SAVE_ARRANGE_POLY
    if (1)
    { // subtract excluded region and get a polygon bed
        auto& print = wxGetApp().plater()->get_partplate_list().get_current_fff_print();
        bed_poly.points              = get_bed_shape(config);
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
        bed_poly = diff({ bed_poly }, exclude_polys)[0];
    }

    BoundingBox bbox = bed_poly.bounding_box();
    Point center = bbox.center();
    auto polys_to_draw = m_selected;
    for (auto it = polys_to_draw.begin(); it != polys_to_draw.end(); it++) {
        it->poly.translate(center);
        bbox.merge(get_extents(it->poly));
    }
    SVG svg("SVG/arrange_poly.svg", bbox);
    if (svg.is_opened()) {
        svg.draw_outline(bed_poly);
        //svg.draw_grid(bbox, "gray", scale_(0.05));
        std::vector<std::string> color_array = { "red","black","yellow","gree","blue" };
        for (auto it = polys_to_draw.begin(); it != polys_to_draw.end(); it++) {
            std::string color = color_array[(it - polys_to_draw.begin()) % color_array.size()];
            svg.add_comment(it->name);
            svg.draw_text(get_extents(it->poly).min, it->name.c_str(), color.c_str());
            svg.draw_outline(it->poly, color);
        }
    }
#endif

    check_unprintable();

    if (!m_selected.empty()) {
        m_plater->get_notification_manager()->push_notification(NotificationType::ArrangeOngoing, NotificationManager::NotificationLevel::RegularNotificationLevel,
                                                                _u8L("Arranging") + "...");
        m_plater->get_notification_manager()->bbl_close_plateinfo_notification();
    }
}

void ArrangeJob::check_unprintable()
{
    for (auto it = m_selected.begin(); it != m_selected.end();) {
        if (it->poly.area() < 0.001 || it->height>params.printable_height)
        {
#if SAVE_ARRANGE_POLY
            SVG svg(data_dir() + "/SVG/arrange_unprintable_"+it->name+".svg", get_extents(it->poly));
            if (svg.is_opened())
                svg.draw_outline(it->poly);
#endif
            if (it->poly.area() < 0.001) {
                auto msg = (boost::format(_u8L("Object %1% has zero size and can't be arranged.")) % it->name).str();
                m_plater->get_notification_manager()->push_notification(NotificationType::BBLPlateInfo,
                    NotificationManager::NotificationLevel::WarningNotificationLevel, msg);
            }
            m_unprintable.push_back(*it);
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
        show_error(m_plater, _L("Arrange failed. "
                                 "Found some exceptions when processing object geometries."));
    } catch (std::exception &) {
        PlaterJob::on_exception(eptr);
    }
}

void ArrangeJob::process()
{
    auto & partplate_list = m_plater->get_partplate_list();

    const Slic3r::DynamicPrintConfig& global_config = wxGetApp().preset_bundle->full_config();
    if (params.avoid_extrusion_cali_region && global_config.opt_bool("scan_first_layer"))
        partplate_list.preprocess_nonprefered_areas(m_unselected, MAX_NUM_PLATES);

    update_arrange_params(params, global_config, m_selected);
    update_selected_items_inflation(m_selected, global_config, params);
    update_unselected_items_inflation(m_unselected, global_config, params);

    Points      bedpts = get_shrink_bedpts(global_config,params);

    partplate_list.preprocess_exclude_areas(params.excluded_regions, 1, scale_(1));

    ARRANGE_LOG(debug) << "bedpts:" << bedpts[0].transpose() << ", " << bedpts[1].transpose() << ", " << bedpts[2].transpose() << ", " << bedpts[3].transpose();

    params.stopcondition = [this]() { return was_canceled(); };

    params.progressind = [this](unsigned num_finished, std::string str = "") {
        update_status(num_finished, _L("Arranging") + " "+ wxString::FromUTF8(str));
    };

    {
        ARRANGE_LOG(warning)<< "full params: "<< params.to_json();
        ARRANGE_LOG(info) << boost::format("items selected before arranging: %1%") % m_selected.size();
        for (auto selected : m_selected) {
            ARRANGE_LOG(debug) << selected.name << ", extruder: " << MapFormatter(selected.extrude_id_filament_types) << ", bed: " << selected.bed_idx
                               << ", filemant_type:" << selected.filament_temp_type << ", trans: " << unscale<double>(selected.translation(X)) << ","
                               << unscale<double>(selected.translation(Y)) << ", rotation: " << selected.rotation;
        }
        ARRANGE_LOG(debug) << "items unselected before arrange: " << m_unselected.size();
        for (auto item : m_unselected)
            BOOST_LOG_TRIVIAL(debug) << item.name << ", bed: " << item.bed_idx << ", trans: " << unscale<double>(item.translation(X)) << ","
                                     << unscale<double>(item.translation(Y));
    }

    arrangement::arrange(m_selected, m_unselected, bedpts, params);

    // sort by item id
    std::sort(m_selected.begin(), m_selected.end(), [](auto a, auto b) {return a.itemid < b.itemid; });
    {
        ARRANGE_LOG(info) << boost::format("items selected after arranging: %1%") % m_selected.size();
        for (auto selected : m_selected)
            ARRANGE_LOG(debug) << selected.name << ", extruder: " << MapFormatter(selected.extrude_id_filament_types) << ", bed: " << selected.bed_idx
                                     << ", bed_temp: " << selected.first_bed_temp << ", print_temp: " << selected.print_temp
                                     << ", trans: " << unscale<double>(selected.translation(X)) << "," << unscale<double>(selected.translation(Y))
                                     << ", rotation: " << selected.rotation;
        ARRANGE_LOG(debug) << "items unselected after arrange: " << m_unselected.size();
        for (auto item : m_unselected)
            BOOST_LOG_TRIVIAL(debug) << item.name << ", bed: " << item.bed_idx << ", trans: " << unscale<double>(item.translation(X)) << "," << unscale<double>(item.translation(Y));
    }

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
    update_status(status_range(),
        was_canceled() ? _(L("Arranging canceled.")) :
        we_have_unpackable_items ? _(L("Arranging is done but there are unpacked items. Reduce spacing and try again.")) : _(L("Arranging done.")));
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

void ArrangeJob::finalize()
{
    // BBS: partplate
    PartPlateList &plate_list = m_plater->get_partplate_list();
    // Ignore the arrange result if aborted.
    if (!was_canceled()) {

        // Unprintable items go to the last virtual bed
        int beds = 0;

        //clear all the relations before apply the arrangement results
        if (only_on_partplate) {
            plate_list.clear(false, false, true, current_plate_index);
        }
        else
            plate_list.clear(false, false, true, -1);
        //BBS: adjust the bed_index, create new plates, get the max bed_index
        for (ArrangePolygon& ap : m_selected) {
            //if (ap.bed_idx < 0) continue;  // bed_idx<0 means unarrangable
            //BBS: partplate postprocess
            if (only_on_partplate)
                plate_list.postprocess_bed_index_for_current_plate(ap);
            else
                plate_list.postprocess_bed_index_for_selected(ap);

            beds = std::max(ap.bed_idx, beds);

            ARRANGE_LOG(debug) << __FUNCTION__ << boost::format(": selected %4%: bed_id %1%, trans {%2%, %3%}") % ap.bed_idx % unscale<double>(ap.translation(X)) % unscale<double>(ap.translation(Y)) % ap.name;
        }

        //BBS: adjust the bed_index, create new plates, get the max bed_index
        for (ArrangePolygon& ap : m_unselected) {
            if (ap.is_virt_object)
                continue;

            //BBS: partplate postprocess
            if (!only_on_partplate)
                plate_list.postprocess_bed_index_for_unselected(ap);

            beds = std::max(ap.bed_idx, beds);
            ARRANGE_LOG(debug) << __FUNCTION__ << boost::format(": unselected %4%: bed_id %1%, trans {%2%, %3%}") % ap.bed_idx % unscale<double>(ap.translation(X)) % unscale<double>(ap.translation(Y)) % ap.name;
        }

        for (ArrangePolygon& ap : m_locked) {
            beds = std::max(ap.bed_idx, beds);

            plate_list.postprocess_arrange_polygon(ap, false);

            ap.apply();
            ARRANGE_LOG(debug) << boost::format(": locked %4%: bed_id %1%, trans {%2%, %3%}") % ap.bed_idx % unscale<double>(ap.translation(X)) %
                                      unscale<double>(ap.translation(Y)) % ap.name;
        }

        // Apply the arrange result to all selected objects
        for (ArrangePolygon& ap : m_selected) {
            //BBS: partplate postprocess
            plate_list.postprocess_arrange_polygon(ap, true);

            ap.apply();
        }

        // Apply the arrange result to unselected objects(due to the sukodu-style column changes, the position of unselected may also be modified)
        for (ArrangePolygon& ap : m_unselected) {
            if (ap.is_virt_object)
                continue;

            //BBS: partplate postprocess
            plate_list.postprocess_arrange_polygon(ap, false);

            ap.apply();
            ARRANGE_LOG(debug) << boost::format(": unselected %4%: bed_id %1%, trans {%2%, %3%}") % ap.bed_idx % unscale<double>(ap.translation(X)) %
                                      unscale<double>(ap.translation(Y)) % ap.name;
        }

        // Move the unprintable items to the last virtual bed.
        // Note ap.apply() moves relatively according to bed_idx, so we need to subtract the orignal bed_idx
        for (ArrangePolygon& ap : m_unprintable) {
            ap.bed_idx = -1;
            plate_list.postprocess_arrange_polygon(ap, true);

            ap.apply();
            ARRANGE_LOG(debug) << __FUNCTION__ << boost::format(": m_unprintable: name: %4%, bed_id %1%, trans {%2%,%3%}") % ap.bed_idx % unscale<double>(ap.translation(X)) % unscale<double>(ap.translation(Y)) % ap.name;
        }

        m_plater->update();

        // unlock the plates we just locked
        for (int i : m_uncompatible_plates) {
            PartPlate* plate = plate_list.get_plate(i);
            if (plate) plate->lock(false);
        }

        //BBS: reload all objects due to arrange
        if (only_on_partplate) {
            plate_list.rebuild_plates_after_arrangement(!only_on_partplate, true, current_plate_index);
        }
        else {
            plate_list.rebuild_plates_after_arrangement(!only_on_partplate, true);
        }

        // BBS: update slice context and gcode result.
        m_plater->update_slicing_context_to_current_partplate();

        wxGetApp().obj_list()->reload_all_plates();

        m_plater->update();
        if (!m_selected.empty())
            m_plater->get_notification_manager()->push_notification(NotificationType::ArrangeOngoing, NotificationManager::NotificationLevel::RegularNotificationLevel,
                                                                    _u8L("Arranging done."));
    }
    else {
        m_plater->get_notification_manager()->push_notification(NotificationType::ArrangeOngoing,
            NotificationManager::NotificationLevel::RegularNotificationLevel, _u8L("Arranging canceled."));
    }
    Job::finalize();

    m_plater->m_arrange_running.store(false);
}

std::optional<arrangement::ArrangePolygon>
get_wipe_tower_arrangepoly(const Plater &plater)
{
    int id = plater.canvas3D()->fff_print()->get_plate_index();
    if (auto wti = get_wipe_tower(plater, id))
        return get_wipetower_arrange_poly(&wti);

    return {};
}

//BBS: add sudoku-style stride
double bed_stride_x(const Plater* plater) {
    double bedwidth = plater->build_volume().bounding_box().size().x();
    return (1. + LOGICAL_BED_GAP) * bedwidth;
}

double bed_stride_y(const Plater* plater) {
    double beddepth = plater->build_volume().bounding_box().size().y();
    return (1. + LOGICAL_BED_GAP) * beddepth;
}

// call before get selected and unselected
arrangement::ArrangeParams init_arrange_params(Plater *p)
{
    arrangement::ArrangeParams         params;
    GLCanvas3D::ArrangeSettings       &settings     = p->canvas3D()->get_arrange_settings();
    auto                              &print        = wxGetApp().plater()->get_partplate_list().get_current_fff_print();
    const PrintConfig                 &print_config = print.config();

    params.clearance_height_to_rod             = print_config.extruder_clearance_height_to_rod.value;
    params.clearance_height_to_lid             = print_config.extruder_clearance_height_to_lid.value;
    params.cleareance_radius                   = print_config.extruder_clearance_max_radius.value;
    params.printable_height                    = print_config.printable_height.value;
    params.nozzle_height                       = print.config().nozzle_height.value;
    params.align_center                        = print_config.best_object_pos.value;
    params.allow_rotations                     = settings.enable_rotation;
    params.allow_multi_materials_on_same_plate = settings.allow_multi_materials_on_same_plate;
    params.avoid_extrusion_cali_region         = settings.avoid_extrusion_cali_region;
    params.is_seq_print                        = settings.is_seq_print;
    params.min_obj_distance                    = scaled(settings.distance);
    params.align_to_y_axis                     = settings.align_to_y_axis;
#if !BBL_RELEASE_TO_PUBLIC
    params.save_svg                            = settings.save_svg;
#endif

    int state = p->get_prepare_state();
    if (state == Job::JobPrepareState::PREPARE_STATE_MENU) {
        PartPlateList &plate_list = p->get_partplate_list();
        PartPlate *    plate      = plate_list.get_curr_plate();
        bool plate_same_as_global = true;
        params.is_seq_print       = plate->get_real_print_seq(&plate_same_as_global) == PrintSequence::ByObject;
        // if plate's print sequence is not the same as global, the settings.distance is no longer valid, we set it to auto
        if (!plate_same_as_global)
            params.min_obj_distance = 0;
    }

    if (params.is_seq_print) {
        params.bed_shrink_x = BED_SHRINK_SEQ_PRINT;
        params.bed_shrink_y = BED_SHRINK_SEQ_PRINT;
    }
    return params;
}

}} // namespace Slic3r::GUI
