#include "ArrangeJob.hpp"

#include "libslic3r/SVG.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ModelArrange.hpp"
#include "libslic3r/VectorFormatter.hpp"
#include "libslic3r/Geometry/ConvexHull.hpp"

#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/WipeTowerPlacement.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/NotificationManager.hpp"
#include "slic3r/GUI/format.hpp"

#include "libnest2d/common.hpp"

#include <chrono>

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

    // Sets m_pos directly, skipping apply_wipe_tower()'s plate_origin subtraction.
    void set_pos(const Vec2d &pos) { m_pos = pos; }

    ArrangePolygon get_arrange_polygon() const
    {
        // m_bb is the tower footprint from wipe_tower_nest_footprint(), already in place.
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

// Same as reload_scene()/init_wipe_tower_placed_flag(): ByObject has a tower
// only when the plate has exactly one printable instance.
static bool plate_allows_wipe_tower(PartPlate *pl)
{
    if (!pl) return false;
    if (pl->get_real_print_seq() != PrintSequence::ByObject)
        return true;
    return pl->printable_instance_size() == 1;
}

// Smooth timelapse / wrapping detection always needs a tower, even single-color.
static bool wipe_tower_is_mandatory()
{
    const DynamicPrintConfig &cfg = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    auto sop = cfg.option("timelapse_type");
    if (sop && sop->getInt() == TimelapseType::tlSmooth)
        return true;
    if (cfg.has("enable_wrapping_detection") && cfg.opt_bool("enable_wrapping_detection"))
        return true;
    return false;
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

int ArrangeJob::score(const ArrangePolygons &selected, const ArrangePolygons &unselected) const
{
    std::set<int> beds;
    int off_plate = 0;
    for (const ArrangePolygon &ap : selected) {
        if (ap.is_virt_object)
            continue;
        if (ap.bed_idx != 0)
            ++off_plate;   // a plate arrange sends what does not fit outside
        if (ap.bed_idx >= 0)
            beds.insert(ap.bed_idx);
    }
    // Plates the fixed parts occupy count too, or opening fresh plates would look like a win.
    for (const ArrangePolygon &ap : unselected)
        if (!ap.is_virt_object && ap.bed_idx >= 0)
            beds.insert(ap.bed_idx);
    return only_on_partplate ? off_plate : int(beds.size());
}

int ArrangeJob::unplaced(const ArrangePolygons &aps) const
{
    if (only_on_partplate)
        return 0; // counted by score()
    return int(std::count_if(aps.begin(), aps.end(), [](const ArrangePolygon &ap) { return !ap.is_virt_object && ap.bed_idx < 0; }));
}

// Stock arrange packs convex hulls, sparrow true outlines, so the area the hulls waste is
// what sparrow can win back. Below 15% waste it never saved a plate in a 39-project corpus;
// above it, a wrong yes costs a visible, stoppable search, a wrong no a missed plate.
bool ArrangeJob::predict_better(int stock_score, double bed_area) const
{
    double hull = 0., outline = 0.;
    for (const ArrangePolygon &ap : m_selected) {
        if (ap.is_virt_object)
            continue;
        hull += Geometry::convex_hull(ap.poly.contour.points).area();
        outline += ap.poly.area(); // openings count as free space
    }
    ARRANGE_LOG(info) << "pack tighter check: stock score " << stock_score << ", unplaced " << m_stock_unplaced << ", outline/hull " << outline / hull;
    // A plate arrange that left parts off tries whenever their outlines could fit the plate:
    // a miss costs one plate's search.
    if (only_on_partplate)
        return stock_score >= 1 && outline <= bed_area;
    return (stock_score >= 2 || m_stock_unplaced > 0) && hull > 0. && outline / hull < 0.85;
}

std::string ArrangeJob::done_message() const
{
    if (m_packed_score < 0) {
        if (!m_packing)
            return was_canceled() ? _u8L("Arranging canceled.") : _u8L("Arranging done.");
        return was_canceled() ? _u8L("Packing canceled. Kept the standard arrangement.")
                              : _u8L("No tighter layout found. Kept the standard arrangement.");
    }
    if (m_packed_unplaced < m_stock_unplaced)
        return (boost::format(_u8L("Placed %1% more parts, on %2% plates.")) % (m_stock_unplaced - m_packed_unplaced) % m_packed_score).str();
    // Relative to the standard arrange, which the user saw first.
    if (only_on_partplate)
        return m_packed_score == 0
            ? (boost::format(_u8L("Packed tighter: all parts fit on the plate (standard arrange left %1% off).")) % m_stock_score).str()
            : (boost::format(_u8L("Packed tighter: %1% parts left off the plate instead of %2%.")) % m_packed_score % m_stock_score).str();
    return (boost::format(_u8L("Packed onto %1% plates instead of %2%.")) % m_packed_score % m_stock_score).str();
}

void ArrangeJob::restore_live()
{
    // Nothing stops an edit during the job, so skip instances deleted since prepare().
    std::set<const ModelInstance *> alive;
    for (const ModelObject *mo : m_plater->model().objects)
        alive.insert(mo->instances.begin(), mo->instances.end());
    for (auto &[instance, trafo] : m_live_restore)
        if (alive.count(instance))
            instance->set_transformation(trafo);

    // Prime towers too: applying a result moves them (optimal position, exclusion zones).
    PartPlateList &plates = m_plater->get_partplate_list();
    for (int i = 0; i < plates.get_plate_count(); ++i)
        plates.get_plate(i)->set_wipe_tower_placed(i < int(m_live_tower_placed.size()) && m_live_tower_placed[i]);
    DynamicConfig &proj_cfg = wxGetApp().preset_bundle->project_config;
    auto          *wtx      = proj_cfg.opt<ConfigOptionFloats>("wipe_tower_x");
    auto          *wty      = proj_cfg.opt<ConfigOptionFloats>("wipe_tower_y");
    if (!wtx || !wty || m_live_tower_x.size() != m_live_tower_y.size())
        return;
    auto &positions = m_plater->model().wipe_tower.positions;
    for (int i = 0; i < plates.get_plate_count(); ++i) {
        if (i < int(m_live_tower_x.size()) && i < int(wtx->values.size()) && i < int(wty->values.size())) {
            wtx->values[i] = m_live_tower_x[i];
            wty->values[i] = m_live_tower_y[i];
        } else
            plates.set_default_wipe_tower_pos_for_plate(i); // a plate added since prepare()
        if (i < int(positions.size()))
            positions[i] = i < int(m_live_tower_pos.size()) ? m_live_tower_pos[i] : Vec2d(wtx->values[i], wty->values[i]);
    }
}

// UI thread. Re-applies from the saved transformations, because ArrangePolygon::apply()
// adds its rotation to the instance's current one.
void ArrangeJob::apply_live(const std::vector<arrangement::ArrangeParams::Pose> &poses)
{
    if (!m_live_open || poses.size() != m_live_aps.size())
        return;
    m_live_used = true;
    restore_live();
    PartPlateList &plate_list = m_plater->get_partplate_list();

    // Sparrow fills plates from the first one, so its plates go in front and the stock
    // plates, with the parts it has not taken yet, follow.
    int sparrow_plates = 0;
    for (size_t i = 0; i < poses.size(); ++i)
        if (!m_live_aps[i].is_virt_object)
            sparrow_plates = std::max(sparrow_plates, poses[i].bed_idx + 1);

    // Assign every plate first: a new plate can reflow the grid that positions depend on.
    std::vector<ArrangePolygon> live, waiting;
    for (size_t i = 0; i < poses.size(); ++i) {
        ArrangePolygon ap = m_live_aps[i];
        if (ap.is_virt_object)
            continue;
        arrangement::ArrangeParams::Pose pose = poses[i];
        if (pose.bed_idx < 0 && !only_on_partplate && i < m_live_stock.size() && m_live_stock[i].bed_idx >= 0) {
            pose = m_live_stock[i];
            pose.bed_idx += sparrow_plates;
        }
        if (pose.bed_idx < 0) {
            waiting.push_back(ap); // not placed yet: wait outside in the original layout
            continue;
        }
        ap.bed_idx     = pose.bed_idx;
        ap.translation = pose.translation;
        ap.rotation    = pose.rotation;
        // Creates plates as needed; finalize() recycles any left empty.
        if (only_on_partplate)
            plate_list.postprocess_bed_index_for_current_plate(ap);
        else
            plate_list.postprocess_bed_index_for_selected(ap);
        live.push_back(ap);
    }
    for (ArrangePolygon &ap : waiting)
        ap.bed_idx = plate_list.get_plate_count();
    live.insert(live.end(), waiting.begin(), waiting.end());
    for (ArrangePolygon &ap : live) {
        plate_list.postprocess_arrange_polygon(ap, true);
        ap.apply();
    }
    m_plater->canvas3D()->reload_scene(true);
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

    const DynamicPrintConfig& current_config = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    const auto* opt_enable_wrapping = current_config.option<ConfigOptionBool>("enable_wrapping_detection");
    bool   enable_wrapping = opt_enable_wrapping ? opt_enable_wrapping->value : false;

    // add the virtual object into unselect list if has
    plate_list.preprocess_exclude_areas(m_unselected, enable_wrapping, MAX_NUM_PLATES);
}

// Builds and persists a nest obstacle for a plate with no tower yet.
static arrangement::ArrangePolygon estimate_wipe_tower_info(int plate_index, std::set<int>& extruder_ids, bool tower_already_placed, bool need_wipe_tower_global)
{
    PartPlateList& ppl = wxGetApp().plater()->get_partplate_list();
    const DynamicPrintConfig& full_config = wxGetApp().preset_bundle->full_config();
    const DynamicPrintConfig& print_cfg   = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    int plate_count = ppl.get_plate_count();
    // plate_index may be an about-to-be-created overflow plate; borrow an existing
    // plate for geometry, but plate_has_own_slot guards its wipe_tower_x/y slot.
    int plate_index_valid = std::min(plate_index, plate_count - 1);
    PartPlate *plate = ppl.get_plate(plate_index_valid);
    const bool plate_has_own_slot = plate_index < plate_count;

    int extruder_size = extruder_ids.empty() ? 2 : (int) extruder_ids.size();
    int nozzle_nums = wxGetApp().preset_bundle->get_printer_extruder_count();

    const float prime_tower_width = print_cfg.opt_float("prime_tower_width");
    std::vector<double> prime_volumes = full_config.option<ConfigOptionFloats>("filament_prime_volume")->values;
    if (full_config.option<ConfigOptionEnum<PrimeVolumeMode>>("prime_volume_mode")->value == pvmSaving)
        for (auto &pv : prime_volumes) pv = 15.f;
    const bool enable_wrapping = full_config.opt_bool("enable_wrapping_detection");
    const Vec3d wt_size_3d = plate->estimate_wipe_tower_size(print_cfg, prime_tower_width,
                                                              get_max_element(prime_volumes),
                                                              nozzle_nums, extruder_size, false, enable_wrapping);
    const Vec2d tower_size(wt_size_3d.x(), wt_size_3d.y());

    arrangement::ArrangePolygon arrange_poly;
    arrange_poly.bed_idx = plate_index;
    if ((tower_size.x() <= EPSILON || tower_size.y() <= EPSILON) && !tower_already_placed && !need_wipe_tower_global)
        return arrange_poly; // no tower needed

    DynamicConfig &proj_cfg = wxGetApp().preset_bundle->project_config;
    ConfigOptionFloats *wtx = proj_cfg.opt<ConfigOptionFloats>("wipe_tower_x");
    ConfigOptionFloats *wty = proj_cfg.opt<ConfigOptionFloats>("wipe_tower_y");
    const Vec3d plate_origin = plate->get_origin();

    float x, y;
    if (plate_has_own_slot && wtx && wty && plate_index_valid < (int) wtx->values.size() && plate_index_valid < (int) wty->values.size()) {
        x = (float) wtx->values[plate_index_valid] + (float) plate_origin.x();
        y = (float) wty->values[plate_index_valid] + (float) plate_origin.y();
    } else {
        const Vec2d def_pos = ppl.get_machine_default_wipe_tower_pos();
        x = (float) def_pos.x() + (float) plate_origin.x();
        y = (float) def_pos.y() + (float) plate_origin.y();
    }

    std::vector<ForbiddenRect2d> forbidden;
    for (const BoundingBoxf3 &b : plate->get_exclude_areas())
        forbidden.push_back({b.min.x(), b.min.y(), b.max.x(), b.max.y()});
    if (enable_wrapping) {
        const Pointfs wpts = ppl.get_wrapping_exclude_area();
        if (!wpts.empty()) {
            BoundingBoxf wrap_bb(wpts);
            forbidden.push_back({wrap_bb.min.x() + plate_origin.x(), wrap_bb.min.y() + plate_origin.y(),
                                  wrap_bb.max.x() + plate_origin.x(), wrap_bb.max.y() + plate_origin.y()});
        }
    }

    const double brim = wipe_tower_brim_width(print_cfg, wt_size_3d.z());
    if (!tower_already_placed)
        wipe_tower_pullback_then_avoid(x, y, tower_size, plate->get_build_volume(true), brim,
                                       wipe_tower_line_width(print_cfg), forbidden);

    const float local_x = x - (float) plate_origin.x();
    const float local_y = y - (float) plate_origin.y();

    if (plate_has_own_slot && wtx && wty) {
        ConfigOptionFloat wt_x_opt(local_x);
        ConfigOptionFloat wt_y_opt(local_y);
        wtx->set_at(&wt_x_opt, plate_index_valid, 0);
        wty->set_at(&wt_y_opt, plate_index_valid, 0);
        Model &model = wxGetApp().plater()->model();
        if (plate_index_valid < (int) model.wipe_tower.positions.size())
            model.wipe_tower.positions[plate_index_valid] = Vec2d(wt_x_opt.value, wt_y_opt.value);
    }

    const BoundingBoxf footprint = wipe_tower_nest_footprint(tower_size.x(), tower_size.y(), brim);
    Polygon ap({
        {scaled(footprint.min)},
        {scaled(footprint.max.x()), scaled(footprint.min.y())},
        {scaled(footprint.max)},
        {scaled(footprint.min.x()), scaled(footprint.max.y())}
        });
    arrange_poly.poly.contour = std::move(ap);
    arrange_poly.translation = Vec2crd{scaled(local_x), scaled(local_y)};
    arrange_poly.setter = NULL; // do not move wipe tower
    arrange_poly.name = "WipeTower";
    arrange_poly.is_virt_object = true;
    arrange_poly.is_wipe_tower = true;
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
bool ArrangeJob::selected_items_need_wipe_tower() const
{
    // if wipe tower is explicitly disabled, no need to estimate
    DynamicPrintConfig& current_config = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    auto                op = current_config.option("enable_prime_tower");
    bool enable_prime_tower = op && op->getBool();
    if (!enable_prime_tower) return false;

    if (wipe_tower_is_mandatory()) return true;

    // estimate if we need wipe tower for all plates:
    // need wipe tower if some object has multiple extruders (has paint-on colors or support material)
    for (const auto& item : m_selected) {
        if (item.extrude_id_filament_types.size() > 1) {
            ARRANGE_LOG(info) << "need wipe tower because object " << item.name << " has multiple extruders (has paint-on colors)";
            return true;
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
                ARRANGE_LOG(info) << "need wipe tower because allow_multi_materials_on_same_plate=true and we have multiple extruders of same type";
                return true;
            }
        }
    }
    return false;
}

// Retry single-filament items rejected by the tower-crowded main pass on fresh plates.
static void retry_unfit_single_color_items(ArrangePolygons &selected, const ArrangePolygons &unselected,
                                            const Points &bedpts, const arrangement::ArrangeParams &params)
{
    // No tower obstacle in the main pass: rejected items were genuinely oversized.
    if (std::none_of(unselected.begin(), unselected.end(),
                     [](const ArrangePolygon &ap) { return ap.is_wipe_tower; }))
        return;

    // Step 1: rejected items that only use one filament.
    std::vector<size_t> candidates;
    for (size_t i = 0; i < selected.size(); i++) {
        const ArrangePolygon &ap = selected[i];
        // Skip virtual objects: finalize() doesn't skip them, so giving them a fallback
        // bed_idx would create empty plates. Real instances always have >= 1 filament id.
        if (!ap.is_virt_object && ap.bed_idx < 0 && ap.extrude_id_filament_types.size() == 1)
            candidates.push_back(i);
    }
    if (candidates.empty())
        return;

    // Step 2: group by filament id so each group gets its own fresh plate; mixing
    // filaments on one plate would need a tower again.
    std::map<int, std::vector<size_t>> groups;
    for (size_t idx : candidates)
        groups[selected[idx].extrude_id_filament_types.begin()->first].push_back(idx);

    // Step 3: fallback obstacles - unselected items minus wipe towers.
    ArrangePolygons fallback_unselected;
    for (const ArrangePolygon &ap : unselected) {
        if (!ap.is_wipe_tower)
            fallback_unselected.emplace_back(ap);
    }

    // Step 4: first free plate index.
    int base_bed = 0;
    for (const ArrangePolygon &ap : selected) {
        if (ap.bed_idx >= 0)
            base_bed = std::max(base_bed, ap.bed_idx + 1);
    }

    ARRANGE_LOG(info) << boost::format("wipe tower fallback: %1% candidates in %2% groups, base bed %3%")
                             % candidates.size() % groups.size() % base_bed;

    // Silence progress callback so the fallback's own counter doesn't jump the main pass back.
    arrangement::ArrangeParams fallback_params = params;
    fallback_params.progressind                = nullptr;

    // Step 5: rearrange group by group.
    for (auto &kv : groups) {
        ArrangePolygons group;
        group.reserve(kv.second.size());
        for (size_t idx : kv.second)
            group.emplace_back(selected[idx]);

        for (ArrangePolygon &ap : group)
            ap.bed_idx = 0;

        arrangement::arrange(group, fallback_unselected, bedpts, fallback_params);

        int next_base = base_bed;
        for (ArrangePolygon &ap : group) {
            if (ap.bed_idx < 0)
                continue;   // still oversized, leave it outside
            ap.bed_idx += base_bed;
            if (ap.bed_idx >= MAX_NUM_PLATES)
                ap.bed_idx = -1;
            else
                next_base = std::max(next_base, ap.bed_idx + 1);
        }
        base_bed = next_base;

        // Write back by index, not itemid: arrange() overwrites itemid with packing order.
        for (size_t k = 0; k < kv.second.size() && k < group.size(); k++) {
            const int original_itemid   = selected[kv.second[k]].itemid;
            selected[kv.second[k]]      = group[k];
            selected[kv.second[k]].itemid = original_itemid;
            ARRANGE_LOG(debug) << "wipe tower fallback: " << group[k].name << " -> bed " << group[k].bed_idx;
        }
    }
}

void ArrangeJob::prepare_wipe_tower(bool select)
{
    DynamicPrintConfig& current_config = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    auto                op = current_config.option("enable_prime_tower");
    bool enable_prime_tower = op && op->getBool();
    if (!enable_prime_tower) return;
    // ByObject: a plate's future tower can't be evaluated before it exists.
    if (params.is_seq_print) return;

    bool need_wipe_tower = selected_items_need_wipe_tower();
    ARRANGE_LOG(info) << "need_wipe_tower=" << need_wipe_tower;


    ArrangePolygon    wipe_tower_ap;
    wipe_tower_ap.name = "WipeTower";
    wipe_tower_ap.is_virt_object = true;
    wipe_tower_ap.is_wipe_tower = true;
    const GLCanvas3D* canvas3D = static_cast<const GLCanvas3D*>(m_plater->canvas3D());

    std::set<int> extruder_ids;
    for (const auto &item : m_selected)
        for (const auto &kv : item.extrude_id_filament_types)
            extruder_ids.insert(kv.first);

    PartPlateList& ppl = wxGetApp().plater()->get_partplate_list();
    int plate_count = ppl.get_plate_count();

    int bedid_unlocked = 0;
    for (int bedid = 0; bedid < MAX_NUM_PLATES; bedid++) {
        int plate_index_valid = std::min(bedid, plate_count - 1);
        PartPlate* pl = ppl.get_plate(plate_index_valid);
        if(bedid<plate_count && pl->is_locked())
            continue;
        if (bedid < plate_count && !plate_allows_wipe_tower(pl))
            continue;
        if (bedid < plate_count) {
            DynamicConfig *proj_cfg_ptr = &wxGetApp().preset_bundle->project_config;
            auto *wtx = proj_cfg_ptr->opt<ConfigOptionFloats>("wipe_tower_x");
            auto *wty = proj_cfg_ptr->opt<ConfigOptionFloats>("wipe_tower_y");
            if (wtx && wty) {
                const DynamicPrintConfig &fc = wxGetApp().preset_bundle->full_config();
                ensure_wipe_tower_clears_forbidden_regions(*pl, bedid, m_plater->model(), ppl, *wtx, *wty, fc);
            }
        }
        if (auto wti = get_wipe_tower(*m_plater, bedid)) {
            wipe_tower_ap = get_wipetower_arrange_poly(&wti);
            wipe_tower_ap.setter = NULL;
            wipe_tower_ap.bed_idx = bedid_unlocked;
            wipe_tower_ap.name    = "WipeTower" + std::to_string(bedid_unlocked);
            if (select)
                m_selected.emplace_back(wipe_tower_ap);
            else
                m_unselected.emplace_back(wipe_tower_ap);
        }
        else if (need_wipe_tower || (bedid < plate_count && pl->is_wipe_tower_placed())) {
            wipe_tower_ap = estimate_wipe_tower_info(bedid, extruder_ids, bedid < plate_count && pl->is_wipe_tower_placed(), need_wipe_tower);
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
    DynamicPrintConfig& current_config_wt = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    auto  op_wt = current_config_wt.option("enable_prime_tower");
    bool  enable_prime_tower_wt = op_wt && op_wt->getBool();
    if (params.is_seq_print) {
        // ByObject: only avoid an existing tower, never reserve one. get_wipe_tower()'s
        // GLVolume only exists when the tower is enabled, so no extra check needed here.
        if (auto wti = get_wipe_tower(*m_plater, current_plate_index)) {
            ArrangePolygon&& ap = get_wipetower_arrange_poly(&wti);
            ap.setter = NULL;
            m_unselected.emplace_back(std::move(ap));
        }
    }
    else if (enable_prime_tower_wt) {
        DynamicConfig *proj_cfg_ptr = &wxGetApp().preset_bundle->project_config;
        auto *wtx = proj_cfg_ptr->opt<ConfigOptionFloats>("wipe_tower_x");
        auto *wty = proj_cfg_ptr->opt<ConfigOptionFloats>("wipe_tower_y");
        if (wtx && wty) {
            const DynamicPrintConfig &fc = wxGetApp().preset_bundle->full_config();
            ensure_wipe_tower_clears_forbidden_regions(*plate, current_plate_index, model, plate_list, *wtx, *wty, fc);
        }
        if (auto wti = get_wipe_tower(*m_plater, current_plate_index)) {
            ArrangePolygon&& ap = get_wipetower_arrange_poly(&wti);
            ap.setter = NULL;
            m_unselected.emplace_back(std::move(ap));
        }
        else {
            bool need_wipe_tower = selected_items_need_wipe_tower();
            if (need_wipe_tower || plate->is_wipe_tower_placed()) {
                auto plate_extruders = plate->get_extruders(true);
                std::set<int> extruder_ids(plate_extruders.begin(), plate_extruders.end());
                ArrangePolygon&& ap = estimate_wipe_tower_info(current_plate_index, extruder_ids, plate->is_wipe_tower_placed(), need_wipe_tower);
                m_unselected.emplace_back(std::move(ap));
            }
        }
    }

    const DynamicPrintConfig &current_config  = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    const auto* opt_enable_wrapping = current_config.option<ConfigOptionBool>("enable_wrapping_detection");
    bool   enable_wrapping = opt_enable_wrapping ? opt_enable_wrapping->value : false;

    // add the virtual object into unselect list if has
    plate_list.preprocess_exclude_areas(m_unselected, enable_wrapping, current_plate_index + 1);
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

    const DynamicPrintConfig &current_config  = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    const auto* opt_enable_wrapping = current_config.option<ConfigOptionBool>("enable_wrapping_detection");
    bool enable_wrapping = opt_enable_wrapping ? opt_enable_wrapping->value : false;

    // add the virtual object into unselect list if has
    plate_list.preprocess_exclude_areas(m_unselected, enable_wrapping, current_plate_index + 1);
}

//BBS: add partplate logic
void ArrangeJob::prepare()
{
    params = init_arrange_params(m_plater);
    int state = m_plater->get_prepare_state();
    m_pack_eligible = arrangement::sparrow_available() && !params.is_seq_print && state != Job::JobPrepareState::PREPARE_STATE_OUTSIDE_BED;
    // Collect true outlines for sparrow; stock arrange takes their convex hulls itself.
    arrangement::use_true_outline.store(m_pack_eligible, std::memory_order_relaxed);
    m_packing      = false;
    m_stock_score  = -1;
    m_packed_score = -1;
    m_stock_unplaced = m_packed_unplaced = 0;
    m_stock_selected.clear();
    m_live_stock.clear();
    m_live_open    = false;
    m_live_used    = false;

    //BBS update extruder params and speed table before arranging
    const Slic3r::DynamicPrintConfig& config = wxGetApp().preset_bundle->full_config();
    auto& print = wxGetApp().plater()->get_partplate_list().get_current_fff_print();
    auto print_config = print.config();
    int filament_count = wxGetApp().preset_bundle->filament_presets.size();

    Model::setExtruderParams(config, filament_count);
    Model::setPrintSpeedTable(config, print_config);

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
        Polygons available_bed = diff({bed_poly}, exclude_polys);
        if (!available_bed.empty())
            bed_poly = std::move(available_bed.front());
    }

    if (!bed_poly.points.empty()) {
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
    }
#endif

    check_unprintable();
    arrangement::use_true_outline.store(false, std::memory_order_relaxed);

    if (m_pack_eligible) {
        m_live_aps = m_selected;
        m_live_restore.clear();
        for (ModelObject *mo : m_plater->model().objects)
            for (ModelInstance *mi : mo->instances)
                m_live_restore.emplace_back(mi, mi->get_transformation());
        DynamicConfig &proj_cfg = wxGetApp().preset_bundle->project_config;
        const auto    *wtx      = proj_cfg.opt<ConfigOptionFloats>("wipe_tower_x");
        const auto    *wty      = proj_cfg.opt<ConfigOptionFloats>("wipe_tower_y");
        m_live_tower_x   = wtx ? wtx->values : std::vector<double>();
        m_live_tower_y   = wty ? wty->values : std::vector<double>();
        m_live_tower_pos = m_plater->model().wipe_tower.positions;
        PartPlateList &plates = m_plater->get_partplate_list();
        m_live_tower_placed.clear();
        for (int i = 0; i < plates.get_plate_count(); ++i)
            m_live_tower_placed.push_back(plates.get_plate(i)->is_wipe_tower_placed());
    }

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
    // finalize() does not run: undo a live preview the packing pass may have applied.
    m_live_open = false;
    if (m_live_used) {
        restore_live();
        if (!only_on_partplate)
            m_plater->get_partplate_list().rebuild_plates_after_arrangement(true, true);
        m_plater->update();
    }
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

    const auto* opt_enable_wrapping = global_config.option<ConfigOptionBool>("enable_wrapping_detection");
    bool   enable_wrapping = opt_enable_wrapping ? opt_enable_wrapping->value : false;
    partplate_list.preprocess_exclude_areas(params.excluded_regions, enable_wrapping, 1, scale_(1));

    ARRANGE_LOG(debug) << "bedpts:" << bedpts[0].transpose() << ", " << bedpts[1].transpose() << ", " << bedpts[2].transpose() << ", " << bedpts[3].transpose();

    params.stopcondition = [this]() { return was_canceled(); };

    m_progress_value = 0;
    m_progress_label = _L("Arranging");
    params.progressind = [this](unsigned num_finished, std::string str = "") {
        // Monotone across sparrow and the stock pass that places its leftovers.
        m_progress_value = std::max(m_progress_value, int(num_finished));
        update_status(m_progress_value, m_progress_label + " " + wxString::FromUTF8(str));
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

    // Input for the sparrow phase, copied after the inflation is set.
    ArrangePolygons pack_selected;
    if (m_pack_eligible)
        pack_selected = m_selected;

    arrangement::arrange(m_selected, m_unselected, bedpts, params);

    // Retry single-color items squeezed out by a preloaded tower placeholder, on fresh
    // tower-free plates.
    const bool mandatory_tower = wipe_tower_is_mandatory();
    if (!only_on_partplate && !mandatory_tower)
        retry_unfit_single_color_items(m_selected, m_unselected, bedpts, params);
    else
        ARRANGE_LOG(info) << "wipe tower fallback skipped: only_on_partplate=" << only_on_partplate
                          << ", mandatory_tower=" << mandatory_tower;

    m_stock_score    = score(m_selected, m_unselected);
    m_stock_unplaced = unplaced(m_selected);
    if (m_pack_eligible && !was_canceled() && predict_better(m_stock_score, std::abs(Polygon(bedpts).area())))
        pack_tighter(pack_selected, bedpts);

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
        was_canceled() && !m_packing ? _(L("Arranging canceled.")) :
        we_have_unpackable_items ? _(L("Arranging is done but there are unpacked items. Reduce spacing and try again.")) : _(L("Arranging done.")));
}

void ArrangeJob::pack_tighter(ArrangePolygons &selected, const Points &bedpts)
{
    m_packing        = true;
    m_progress_value = 0;
    m_progress_label = only_on_partplate ? wxString::Format(_L("Packing tighter (%d parts off the plate)"), m_stock_score)
                                         : wxString::Format(_L("Packing tighter (standard: %d plates)"), m_stock_score);
    update_status(0, m_progress_label);

    // Show the stock result first, held for a moment before sparrow's layouts replace it.
    std::vector<arrangement::ArrangeParams::Pose> stock;
    for (const ArrangePolygon &ap : m_selected)
        stock.push_back({ap.bed_idx, ap.translation, ap.rotation});
    m_live_open = true;
    wxGetApp().CallAfter([this, stock] {
        m_live_stock = stock;
        apply_live(stock);
    });
    auto last = std::make_shared<std::chrono::steady_clock::time_point>(std::chrono::steady_clock::now() + std::chrono::milliseconds(875));

    // Sparrow ignores the input bed of movable items, so one left unplaced means it failed.
    for (ArrangePolygon &ap : selected)
        if (!ap.is_virt_object) // prime towers stay pinned to their plates
            ap.bed_idx = arrangement::UNARRANGED;
    arrangement::ArrangeParams pack = params;
    pack.use_sparrow  = true;
    pack.sparrow_only = true;
    // Mixing never puts more filaments on a plate than stock's busiest one: if that
    // plate loads on this printer, so does any of sparrow's.
    std::map<int, std::set<int>> plate_filaments;
    for (const ArrangePolygon &ap : m_selected)
        if (ap.bed_idx >= 0)
            for (const auto &filament : ap.extrude_id_filament_types)
                plate_filaments[ap.bed_idx].insert(filament.first);
    for (const auto &[bed, filaments] : plate_filaments)
        pack.sparrow_max_filaments_per_plate = std::max(pack.sparrow_max_filaments_per_plate, int(filaments.size()));
    pack.on_layout = [this, last](const std::vector<arrangement::ArrangeParams::Pose> &poses) {
        const auto now = std::chrono::steady_clock::now();
        if (now - *last < std::chrono::milliseconds(125))
            return;
        *last = now;
        wxGetApp().CallAfter([this, poses] { apply_live(poses); });
    };
    arrangement::arrange(selected, m_unselected, bedpts, pack);
    // Stock places sparrow's leftovers around tower placeholders too; give them the same retry.
    if (!only_on_partplate && !wipe_tower_is_mandatory())
        retry_unfit_single_color_items(selected, m_unselected, bedpts, params);

    // Fewer parts left out wins, then fewer plates. A stop falls back to the stock result.
    const int packed = score(selected, m_unselected), packed_unplaced = unplaced(selected);
    ARRANGE_LOG(info) << "pack tighter: stock " << m_stock_score << " / " << m_stock_unplaced << " unplaced, sparrow " << packed << " / " << packed_unplaced;
    if (!was_canceled() && (packed_unplaced < m_stock_unplaced || (packed_unplaced == m_stock_unplaced && packed < m_stock_score))) {
        m_packed_score     = packed;
        m_packed_unplaced  = packed_unplaced;
        m_stock_selected   = std::move(m_selected);
        m_selected         = std::move(selected);
    }
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

void ArrangeJob::apply_optimal_wipe_tower_positions()
{
    PartPlateList &plate_list  = m_plater->get_partplate_list();
    Model         &model       = m_plater->model();
    DynamicConfig &proj_cfg    = wxGetApp().preset_bundle->project_config;
    const DynamicPrintConfig &full_config = wxGetApp().preset_bundle->full_config();

    ConfigOptionFloats *wipe_tower_x = proj_cfg.opt<ConfigOptionFloats>("wipe_tower_x");
    ConfigOptionFloats *wipe_tower_y = proj_cfg.opt<ConfigOptionFloats>("wipe_tower_y");
    if (!wipe_tower_x || !wipe_tower_y)
        return;

    const int plate_count = static_cast<int>(plate_list.get_plate_count());
    for (int plate_idx = 0; plate_idx < plate_count; ++plate_idx) {
        if (only_on_partplate && plate_idx != current_plate_index)
            continue;
        PartPlate *plate = plate_list.get_plate(plate_idx);
        if (!plate)
            continue;
        try_optimal_wipe_tower_position_for_plate(*plate, plate_idx, model, plate_list,
                                                  *wipe_tower_x, *wipe_tower_y,
                                                  full_config, params);
    }
}

void ArrangeJob::apply_result(ArrangePolygons &selected, ArrangePolygons &unselected, ArrangePolygons &locked, ArrangePolygons &unprintable)
{
    PartPlateList &plate_list = m_plater->get_partplate_list();
    // Unprintable items go to the last virtual bed
    int beds = 0;

    //clear all the relations before apply the arrangement results
    if (only_on_partplate) {
        plate_list.clear(false, false, true, current_plate_index);
    }
    else
        plate_list.clear(false, false, true, -1);
    //BBS: adjust the bed_index, create new plates, get the max bed_index
    for (ArrangePolygon& ap : selected) {
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
    for (ArrangePolygon& ap : unselected) {
        if (ap.is_virt_object)
            continue;

        //BBS: partplate postprocess
        if (!only_on_partplate)
            plate_list.postprocess_bed_index_for_unselected(ap);

        beds = std::max(ap.bed_idx, beds);
        ARRANGE_LOG(debug) << __FUNCTION__ << boost::format(": unselected %4%: bed_id %1%, trans {%2%, %3%}") % ap.bed_idx % unscale<double>(ap.translation(X)) % unscale<double>(ap.translation(Y)) % ap.name;
    }

    for (ArrangePolygon& ap : locked) {
        beds = std::max(ap.bed_idx, beds);

        plate_list.postprocess_arrange_polygon(ap, false);

        ap.apply();
        ARRANGE_LOG(debug) << boost::format(": locked %4%: bed_id %1%, trans {%2%, %3%}") % ap.bed_idx % unscale<double>(ap.translation(X)) %
                                  unscale<double>(ap.translation(Y)) % ap.name;
    }

    // Apply the arrange result to all selected objects
    for (ArrangePolygon& ap : selected) {
        //BBS: partplate postprocess
        plate_list.postprocess_arrange_polygon(ap, true);

        ap.apply();
    }

    // Apply the arrange result to unselected objects(due to the sukodu-style column changes, the position of unselected may also be modified)
    for (ArrangePolygon& ap : unselected) {
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
    for (ArrangePolygon& ap : unprintable) {
        ap.bed_idx = -1;
        plate_list.postprocess_arrange_polygon(ap, true);

        ap.apply();
        ARRANGE_LOG(debug) << __FUNCTION__ << boost::format(": unprintable: name: %4%, bed_id %1%, trans {%2%,%3%}") % ap.bed_idx % unscale<double>(ap.translation(X)) % unscale<double>(ap.translation(Y)) % ap.name;
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

    // Re-place virtual-bed items against the post-rebuild plate count.
    //   unprintable: must always sit on the current virtual bed (idx == plate_count).
    //   selected:    only items overflowing the real plates (idx > plate_count) belong on the virtual bed.
    // rotation = 0 because ModelInstance::apply_arrange_result prepends rotation incrementally;
    // the first apply already wrote the arrange rotation, so the re-apply must add zero
    // and only refresh translation (set absolutely). is_applied = 0 forces the second
    // apply because ArrangePolygon::apply() is idempotent by design.
    // NOTE: plate ownership is decided earlier by reload_all_objects() inside
    // rebuild_plates_after_arrangement() and is recorded as (obj_id, instance_id) in
    // PartPlate::obj_to_instance_set, NOT by bbox. The set_offset() done here only
    // refreshes the visual position of items already attached to unprintable_plate;
    // it cannot re-route a printable item back into a real plate.
    const int plate_count = static_cast<int>(plate_list.get_plate_count());
    auto reapply_virtual_bed = [&](ArrangePolygon& ap, bool always_to_virtual_bed) {
        const bool need_reapply = always_to_virtual_bed
            ? (ap.bed_idx != plate_count)
            : (ap.bed_idx >  plate_count);
        if (!need_reapply) return;
        ap.bed_idx    = -1;
        plate_list.postprocess_arrange_polygon(ap, true);
        ap.rotation   = 0;
        ap.is_applied = 0;
        ap.apply();
    };
    for (ArrangePolygon& ap : selected)    reapply_virtual_bed(ap, /*always_to_virtual_bed=*/false);
    for (ArrangePolygon& ap : unprintable) reapply_virtual_bed(ap, /*always_to_virtual_bed=*/true);

    if (wipe_tower_optimal_pos_enabled())
        apply_optimal_wipe_tower_positions();

    {
        DynamicConfig &proj_cfg = wxGetApp().preset_bundle->project_config;
        auto *wtx = proj_cfg.opt<ConfigOptionFloats>("wipe_tower_x");
        auto *wty = proj_cfg.opt<ConfigOptionFloats>("wipe_tower_y");
        if (wtx && wty) {
            const DynamicPrintConfig &fc = wxGetApp().preset_bundle->full_config();
            const int plate_count_now = static_cast<int>(plate_list.get_plate_count());
            for (int idx = 0; idx < plate_count_now; ++idx) {
                if (only_on_partplate && idx != current_plate_index)
                    continue;
                PartPlate *p = plate_list.get_plate(idx);
                if (p) {
                    ensure_wipe_tower_clears_forbidden_regions(*p, idx, m_plater->model(), plate_list, *wtx, *wty, fc);
                    init_wipe_tower_placed_flag(*p);
                }
            }
        }
    }

    // BBS: update slice context and gcode result.
    m_plater->update_slicing_context_to_current_partplate();

    wxGetApp().obj_list()->reload_all_plates();

    m_plater->update();
}

void ArrangeJob::finalize()
{
    // BBS: drop the live preview; the result applies relative to the original transformations.
    m_live_open = false;
    if (m_live_used)
        restore_live();

    // Ignore the arrange result if aborted, unless only the sparrow phase was: keep the stock result.
    if (!was_canceled() || m_packing) {

        // BBS: the stock result first, as its own undo step: undo goes packed -> stock -> before.
        // Not with locked or skipped plates: applying unlocks those, and a second apply would
        // then map beds onto them.
        if (m_packed_score >= 0 && m_locked.empty() && m_uncompatible_plates.empty()) {
            ArrangePolygons unselected = m_unselected, locked = m_locked, unprintable = m_unprintable;
            apply_result(m_stock_selected, unselected, locked, unprintable);
            m_plater->take_snapshot("Pack tighter");
            restore_live();
        }
        apply_result(m_selected, m_unselected, m_locked, m_unprintable);
        if (!m_selected.empty())
            m_plater->get_notification_manager()->push_notification(NotificationType::ArrangeOngoing, NotificationManager::NotificationLevel::RegularNotificationLevel,
                                                                    done_message());
    }
    else {
        if (m_live_used)
            m_plater->update();
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
    params.prime_tower_always                  = wipe_tower_is_mandatory();
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

    // Convex hulls; ArrangeJob::prepare() collects true outlines when it may pack tighter.
    arrangement::use_true_outline.store(false, std::memory_order_relaxed);
    return params;
}

}} // namespace Slic3r::GUI
