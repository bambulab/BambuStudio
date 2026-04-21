#include "FillBedJob.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ModelArrange.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "libnest2d/common.hpp"

#include <algorithm>
#include <numeric>

namespace Slic3r {
namespace GUI {

//BBS: add partplate related logic
void FillBedJob::prepare()
{
    PartPlateList& plate_list = m_plater->get_partplate_list();

    m_locked.clear();
    m_selected.clear();
    m_unselected.clear();
    m_bedpts.clear();

    params = init_arrange_params(m_plater);

    m_object_idx = m_plater->get_selected_object_idx();
    if (m_object_idx == -1)
        return;

    //select current plate at first
    int sel_id = m_plater->get_selection().get_instance_idx();
    sel_id = std::max(sel_id, 0);

    int sel_ret = plate_list.select_plate_by_obj(m_object_idx, sel_id);
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(":select plate obj_id %1%, ins_id %2%, ret %3%}") % m_object_idx % sel_id % sel_ret;

    PartPlate* plate = plate_list.get_curr_plate();
    Model& model = m_plater->model();
    BoundingBox plate_bb = plate->get_bounding_box_crd();
    int plate_cols = plate_list.get_plate_cols();
    int cur_plate_index = plate->get_index();

    ModelObject *model_object = m_plater->model().objects[m_object_idx];
    if (model_object->instances.empty()) return;

    const Slic3r::DynamicPrintConfig& global_config = wxGetApp().preset_bundle->full_config();
    m_selected.reserve(model_object->instances.size());
    for (size_t oidx = 0; oidx < model.objects.size(); ++oidx)
    {
        ModelObject* mo = model.objects[oidx];
        for (size_t inst_idx = 0; inst_idx < mo->instances.size(); ++inst_idx)
        {
            bool selected = (oidx == m_object_idx);

            ArrangePolygon ap = get_instance_arrange_poly(mo->instances[inst_idx], global_config);
            BoundingBox ap_bb = ap.transformed_poly().contour.bounding_box();
            ap.height = 1;
            ap.name = mo->name;

            // STUDIO (PRD §11.5): "在不在当前盘"用 overlap 而不是 contains 判定。
            // 旧逻辑只把"AABB 完全落在 plate_bb 内"的 instance 当作 m_unselected
            // 障碍；只要因为旋转/缩放/姿态导致 AABB 超出盘边界哪怕零点几毫米，
            // 也会被划入 m_locked，从而对 NFP 不可见，最终新副本会叠到这种"边
            // 角对象"上。改成"AABB 与 plate_bb 有任何重叠 → m_unselected"，
            // 只有"完全在盘外"才进 m_locked。
            const bool on_current_plate = plate_bb.overlap(ap_bb);

            if (selected)
            {
                if (mo->instances[inst_idx]->printable)
                {
                    ++ap.priority;
                    ap.itemid = m_selected.size();
                    m_selected.emplace_back(ap);
                }
                else
                {
                    if (on_current_plate)
                    {
                        ap.bed_idx = 0;
                        ap.itemid = m_unselected.size();
                        ap.row = cur_plate_index / plate_cols;
                        ap.col = cur_plate_index % plate_cols;
                        ap.translation(X) -= bed_stride_x(m_plater) * ap.col;
                        ap.translation(Y) += bed_stride_y(m_plater) * ap.row;
                        m_unselected.emplace_back(ap);
                    }
                    else
                    {
                        ap.bed_idx = PartPlateList::MAX_PLATES_COUNT;
                        ap.itemid = m_locked.size();
                        m_locked.emplace_back(ap);
                    }
                }
            }
            else
            {
                if (on_current_plate)
                {
                    ap.bed_idx = 0;
                    ap.itemid = m_unselected.size();
                    ap.row = cur_plate_index / plate_cols;
                    ap.col = cur_plate_index % plate_cols;
                    ap.translation(X) -= bed_stride_x(m_plater) * ap.col;
                    ap.translation(Y) += bed_stride_y(m_plater) * ap.row;
                    m_unselected.emplace_back(ap);
                }
                else
                {
                    ap.bed_idx = PartPlateList::MAX_PLATES_COUNT;
                    ap.itemid = m_locked.size();
                    m_locked.emplace_back(ap);
                }
            }
        }
    }
    /*
    for (ModelInstance *inst : model_object->instances)
        if (inst->printable) {
            ArrangePolygon ap = get_arrange_poly(inst);
            // Existing objects need to be included in the result. Only
            // the needed amount of object will be added, no more.
            ++ap.priority;
            m_selected.emplace_back(ap);
        }*/

    if (m_selected.empty()) return;

    bool enable_wrapping = global_config.option<ConfigOptionBool>("enable_wrapping_detection")->value;
    //add the virtual object into unselect list if has
    double scaled_exclusion_gap = scale_(1);
    plate_list.preprocess_exclude_areas(params.excluded_regions, enable_wrapping, 1, scaled_exclusion_gap);
    plate_list.preprocess_exclude_areas(m_unselected, enable_wrapping);

    m_bedpts = get_bed_shape(global_config);

    auto &objects = m_plater->model().objects;
    /*BoundingBox bedbb = get_extents(m_bedpts);

    for (size_t idx = 0; idx < objects.size(); ++idx)
        if (int(idx) != m_object_idx)
            for (ModelInstance *mi : objects[idx]->instances) {
                ArrangePolygon ap = get_arrange_poly(mi);
                auto ap_bb = ap.transformed_poly().contour.bounding_box();

                if (ap.bed_idx == 0 && !bedbb.contains(ap_bb))
                    ap.bed_idx = arrangement::UNARRANGED;

                m_unselected.emplace_back(ap);
            }*/
    if (auto wt = get_wipe_tower_arrangepoly(*m_plater))
        m_unselected.emplace_back(std::move(*wt));

    double sc = scaled<double>(1.) * scaled(1.);

    auto polys = offset_ex(m_selected.front().poly, params.min_obj_distance / 2);
    ExPolygon poly = polys.empty() ? m_selected.front().poly : polys.front();
    double poly_area = poly.area() / sc;
    // STUDIO (PRD §11.6): m_unselected 中所有 instance 的 bed_idx 在 prepare 阶段都被
    // 显式置为 0（见 §11.5 上方两处赋值），所以"是否在当前盘"的判定应该用 0 而不是
    // cur_plate_index。旧代码用 cur_plate_index 在非 0 号盘上 fill bed 时此项恒为 0，
    // 导致 needed_items 被过估，多生成的副本被 finalize 静默丢弃——只是浪费 CPU，
    // 不影响功能正确性，但属于明确的实现 bug。
    double unsel_area = std::accumulate(m_unselected.begin(),
                                        m_unselected.end(), 0.,
                                        [](double s, const auto &ap) {
                                            return s + (ap.bed_idx == 0) * ap.poly.area();
                                        }) / sc;

    double fixed_area = unsel_area + m_selected.size() * poly_area;
    double bed_area   = Polygon{m_bedpts}.area() / sc;

    // This is the maximum number of items, the real number will always be close but less.
    int needed_items = (bed_area - fixed_area) / poly_area;

    //int sel_id = m_plater->get_selection().get_instance_idx();
    // if the selection is not a single instance, choose the first as template
    //sel_id = std::max(sel_id, 0);
    ModelInstance *mi = model_object->instances[sel_id];
    ArrangePolygon template_ap = get_instance_arrange_poly(mi, global_config);

    // STUDIO-15820 修复（FillBed + 旋转/对齐 Y 轴时新副本越界）：
    // 抓快照模板实例的 *原始 transformation*。不能直接靠 setter 里的
    // m_plater->model().objects[m_object_idx]：finalize() 是按 m_selected 顺序逐项
    // ap.apply()，priority>0 模板会先于新副本 apply，把模板 ModelInstance 旋转了
    // rot_template；接着新副本 setter 里 add_object(*mo) 深拷贝出来的 instance 已经
    // 多了 rot_template 的旋转，再 apply_arrange_result(t, rot_new) 的
    // rotate() 是 prepend 操作 ⇒ 最终旋转 = R_z(rot_new) * R_z(rot_template) * R_3d(θ_0)，
    // 而 arrange 算 transformed_poly 时只考虑 R_2D(rot_new)，二者不一致，bbox 偏移。
    // 解决：在 setter 里把克隆出来的 instance 强制重置到模板的原始 transformation，
    // 再 apply_arrange_result，这样组合结果就是 R_z(rot_new) * R_3d(θ_0)，与 arrange 一致。
    Geometry::Transformation mi_orig_trafo = mi->get_transformation();
    for (int i = 0; i < needed_items; ++i) {
        ArrangePolygon ap = template_ap;
        ap.poly = m_selected.front().poly;
        ap.bed_idx = PartPlateList::MAX_PLATES_COUNT;
        ap.height = 1;
        ap.itemid = -1;
        ap.setter = [this, mi_orig_trafo](const ArrangePolygon &p) {
            ModelObject *mo = m_plater->model().objects[m_object_idx];
            ModelObject* newObj = m_plater->model().add_object(*mo);
            newObj->name = mo->name +" "+ std::to_string(p.itemid);
            for (ModelInstance *newInst : newObj->instances) {
                newInst->set_transformation(mi_orig_trafo);
                newInst->apply_arrange_result(p.translation.cast<double>(), p.rotation);
            }
            //m_plater->sidebar().obj_list()->paste_objects_into_list({m_plater->model().objects.size()-1});
        };
        m_selected.emplace_back(ap);
    }

    m_status_range = m_selected.size();

    // The strides have to be removed from the fixed items. For the
    // arrangeable (selected) items bed_idx is ignored and the
    // translation is irrelevant.
    //BBS: remove logic for unselected object
    /*double stride = bed_stride(m_plater);
    for (auto &p : m_unselected)
        if (p.bed_idx > 0)
            p.translation(X) -= p.bed_idx * stride;*/
}

void FillBedJob::process()
{
    if (m_object_idx == -1 || m_selected.empty()) return;
    const Slic3r::DynamicPrintConfig &global_config = wxGetApp().preset_bundle->full_config();

    update_arrange_params(params, global_config, m_selected);
    m_bedpts = get_shrink_bedpts(global_config, params);

    auto &partplate_list               = m_plater->get_partplate_list();
    if (params.avoid_extrusion_cali_region && global_config.opt_bool("scan_first_layer"))
        partplate_list.preprocess_nonprefered_areas(m_unselected, MAX_NUM_PLATES);

    // STUDIO: 当用户没有显式设置对象间距（min_obj_distance==0）时，给铺满整盘
    // 路径加一个 0.5mm 的 inflation 下限，保证副本之间至少有 1mm 的可见间隙；
    // 该下限通过 ArrangeParams 注入，不影响 ArrangeJob/CLI 等其他 arrange 调用方。
    if (params.min_obj_distance == 0)
        params.min_inflation_floor = scaled<coord_t>(0.5);

    update_selected_items_inflation(m_selected, global_config, params);
    update_unselected_items_inflation(m_unselected, global_config, params);

    bool do_stop = false;
    params.stopcondition = [this, &do_stop]() {
        return was_canceled() || do_stop;
    };

    params.progressind = [this](unsigned st,std::string str="") {
         if (st > 0)
             update_status(st, _L("Filling") + " " + wxString::FromUTF8(str));
    };

    params.on_packed = [&do_stop] (const ArrangePolygon &ap) {
        do_stop = ap.bed_idx > 0 && ap.priority == 0;
    };
    // final align用的是凸包，在有fixed item的情况下可能找到的参考点位置是错的，这里就不做了。见STUDIO-3265
    params.do_final_align = false;

    if (m_selected.size() > 100){
        // too many items, just find grid empty cells to put them
        //
        // STUDIO: grid 路径的 step 之前只算了 brim_width，完全没考虑
        // min_obj_distance，导致用户把对象间距设为 0 / 默认时副本之间是边对
        // 边贴在一起（A1→H2D 后铺满整盘，立方体之间无间隔）。这里改为复用
        // update_selected_items_inflation 计算出来的 ap.inflation，使 grid 与
        // NFP 路径在间距/膨胀上的行为一致：副本中心间距 = 物体边长 + 2*inflation。
        if (m_bedpts.empty()) {
            BOOST_LOG_TRIVIAL(warning) << "FillBedJob::process[grid]: empty m_bedpts, abort grid fill";
            update_status(m_status_range, _L("Bed filling done."));
            return;
        }

        const ArrangePolygon &tmpl = m_selected.front();
        const float inflation_mm = unscaled<float>(tmpl.inflation);
        const Vec2f step = unscaled<float>(get_extents(tmpl.poly).size())
                           + 2.f * Vec2f(inflation_mm, inflation_mm);

        // calc the polygon position offset based on origin, in order to normalize the initial position of the arrange polygon
        auto offset_on_origin = tmpl.poly.contour.bounding_box().center();

        // STUDIO: 与 NFP 路径保持一致，把已经按 bed_shrink + brim_skirt_distance/2
        // 收缩的 m_bedpts 作为安全可放置区域传给 get_empty_cells，避免边缘副本的
        // brim/skirt 越出床边界（之前直接用 build_volume(true)，仅含 SceneEpsilon，
        // 无安全裕量）。
        BoundingBox shrunk_bb = Polygon{m_bedpts}.bounding_box();
        BoundingBoxf safe_area_2d(
            Vec2d(unscale<double>(shrunk_bb.min.x()), unscale<double>(shrunk_bb.min.y())),
            Vec2d(unscale<double>(shrunk_bb.max.x()), unscale<double>(shrunk_bb.max.y()))
        );
        std::vector<Vec2f> empty_cells = Plater::get_empty_cells(step, safe_area_2d);

        // STUDIO (重新落地 STUDIO-16564 的修复)：
        // 1) 剔除被"已有真实物体"占据的格点。m_unselected 是当前盘上其它非模板对象，
        //    NFP 路径会把它们当作障碍；以前 grid 路径完全忽略，导致新副本会叠在
        //    它们身上（用户 case：盘上已有 4 个对象，铺满后产生重叠）。
        // 2) 把模板对象自身已有的实例（priority>0）也视为已占据，并在分配阶段跳过
        //    它们，这样多模板实例不会被 grid 循环改写到其它格点；与 NFP 行为对齐。
        // 注：屏蔽区已经在 get_empty_cells 内按 cell.overlap(exclude) 过滤过；
        //     m_unselected 在 prepare() 中已 stride-corrected 到 plate-0 局部坐标，
        //     priority>0 模板实例则仍是世界坐标，需要在这里同步 stride 修正后再入列。
        PartPlateList &plist       = m_plater->get_partplate_list();
        const int plate_cols       = plist.get_plate_cols();
        const int cur_plate_index  = plist.get_curr_plate_index();
        const int cur_col          = cur_plate_index % plate_cols;
        const int cur_row          = cur_plate_index / plate_cols;
        const coord_t stride_dx    = bed_stride_x(m_plater) * cur_col;
        const coord_t stride_dy    = bed_stride_y(m_plater) * cur_row;

        std::vector<BoundingBoxf> occupied;
        auto add_occupied_local = [&](const ArrangePolygon &ap, bool needs_stride_correction) {
            ArrangePolygon local = ap;
            if (needs_stride_correction) {
                local.translation(X) -= stride_dx;
                local.translation(Y) += stride_dy;
            }
            BoundingBox sbb = local.transformed_poly().contour.bounding_box();
            occupied.emplace_back(
                Vec2d(unscale<double>(sbb.min.x()), unscale<double>(sbb.min.y())),
                Vec2d(unscale<double>(sbb.max.x()), unscale<double>(sbb.max.y()))
            );
        };
        size_t unsel_blocked = 0, tmpl_blocked = 0;
        for (const auto &ap : m_unselected) {
            if (ap.is_virt_object || ap.bed_idx != 0) continue;
            add_occupied_local(ap, /*needs_stride_correction=*/false);
            ++unsel_blocked;
        }
        for (const auto &ap : m_selected) {
            if (ap.priority > 0) {
                add_occupied_local(ap, /*needs_stride_correction=*/true);
                ++tmpl_blocked;
            }
        }

        const size_t cells_before = empty_cells.size();
        if (!occupied.empty()) {
            const double half_x = step(0) / 2.0;
            const double half_y = step(1) / 2.0;
            // STUDIO: 用"严格相交（带 ε 容差）"判定 cell 是否被已有对象占据，
            // 替代 BoundingBox::overlap 的"贴边即重叠"行为。背景：step、cell 中心
            // 与模板 AABB 相加以后会落在浮点不可精确表示的尾数上，正负 ε 方向不
            // 一致会导致左右贴边的对称破缺，整版网格出现不规则缺口。这里要求 cell
            // 与障碍 AABB 至少有 EPSILON 的正面积交集才剔除，纯贴边视为可放置。
            auto strict_overlap = [](const BoundingBoxf &a, const BoundingBoxf &b) {
                const double ix = std::min(a.max.x(), b.max.x()) - std::max(a.min.x(), b.min.x());
                const double iy = std::min(a.max.y(), b.max.y()) - std::max(a.min.y(), b.min.y());
                return ix > EPSILON && iy > EPSILON;
            };
            empty_cells.erase(
                std::remove_if(empty_cells.begin(), empty_cells.end(),
                    [&](const Vec2f &c) {
                        BoundingBoxf cell(
                            Vec2d(c.x() - half_x, c.y() - half_y),
                            Vec2d(c.x() + half_x, c.y() + half_y)
                        );
                        for (const auto &bb : occupied)
                            if (strict_overlap(bb, cell)) return true;
                        return false;
                    }),
                empty_cells.end()
            );
        }

        // 分配剩余格点：模板实例（priority>0）保持原位置不动；只把"新副本"
        // (priority==0) 顺序填进 empty_cells，超出格点数则置为未排上 (-1)。
        size_t cell_idx = 0, placed = 0, skipped = 0;
        for (size_t i = 0; i < m_selected.size(); ++i) {
            if (m_selected[i].priority > 0) {
                m_selected[i].bed_idx = 0;
                continue;
            }
            if (cell_idx < empty_cells.size()) {
                m_selected[i].translation = scaled<coord_t>(empty_cells[cell_idx]);
                m_selected[i].translation -= offset_on_origin;
                m_selected[i].bed_idx = 0;
                ++cell_idx;
                ++placed;
            } else {
                m_selected[i].bed_idx = -1;
                ++skipped;
            }
        }

        BOOST_LOG_TRIVIAL(debug) << "FillBedJob::process[grid]: selected=" << m_selected.size()
            << ", unselected=" << m_unselected.size()
            << ", inflation_mm=" << inflation_mm
            << ", cells " << cells_before << "->" << empty_cells.size()
            << " (blocked unsel=" << unsel_blocked << ", tmpl=" << tmpl_blocked << ")"
            << ", placed=" << placed << ", skipped=" << skipped;
    }
    else
        arrangement::arrange(m_selected, m_unselected, m_bedpts, params);

    // finalize just here.
    update_status(m_status_range, was_canceled() ?
                                       _L("Bed filling canceled.") :
                                       _L("Bed filling done."));
}

void FillBedJob::finalize()
{
    // Ignore the arrange result if aborted.
    if (was_canceled()) return;

    if (m_object_idx == -1) return;

    ModelObject *model_object = m_plater->model().objects[m_object_idx];
    if (model_object->instances.empty()) return;

    //BBS: partplate
    PartPlateList& plate_list = m_plater->get_partplate_list();
    int plate_cols = plate_list.get_plate_cols();
    int cur_plate = plate_list.get_curr_plate_index();

    size_t inst_cnt = model_object->instances.size();

    int added_cnt = std::accumulate(m_selected.begin(), m_selected.end(), 0, [](int s, auto &ap) {
        return s + int(ap.priority == 0 && ap.bed_idx == 0);
    });

    int oldSize = m_plater->model().objects.size();

    if (added_cnt > 0) {
        //BBS: adjust the selected instances
        for (ArrangePolygon& ap : m_selected) {
            if (ap.bed_idx != 0) {
                BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(":skipped: bed_id %1%, trans {%2%,%3%}") % ap.bed_idx % unscale<double>(ap.translation(X)) % unscale<double>(ap.translation(Y));
                /*if (ap.itemid == -1)*/
                    continue;
                ap.bed_idx = plate_list.get_plate_count();
            }
            else
                ap.bed_idx = cur_plate;

            // STUDIO: 把 plate-0 局部坐标平移回 cur_plate 的世界坐标。
            // grid 路径（m_selected.size()>100）下 priority>0 的模板实例没有 setter，
            // apply() 是空操作，因此对它们的 translation 多加一份 stride 不会有副作用；
            // priority==0 的新副本则需要这一步才能落在正确的盘上（修复 STUDIO-15820 在
            // 非 0 号盘铺满整盘时副本被错放到 0 号盘的问题）。
            ap.row = ap.bed_idx / plate_cols;
            ap.col = ap.bed_idx % plate_cols;
            ap.translation(X) += bed_stride_x(m_plater) * ap.col;
            ap.translation(Y) -= bed_stride_y(m_plater) * ap.row;

            ap.apply();

            BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(":selected: bed_id %1%, trans {%2%,%3%}") % ap.bed_idx % unscale<double>(ap.translation(X)) % unscale<double>(ap.translation(Y));
        }

        int   newSize = m_plater->model().objects.size();
        auto obj_list = m_plater->sidebar().obj_list();
        for (size_t i = oldSize; i < newSize; i++) {
            obj_list->add_object_to_list(i, true, true, false);
            obj_list->update_printable_state(i, 0);
        }

        BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << ": paste_objects_into_list";

        /*for (ArrangePolygon& ap : m_selected) {
            if (ap.bed_idx != arrangement::UNARRANGED && (ap.priority != 0 || ap.bed_idx == 0))
                ap.apply();
        }*/

        //model_object->ensure_on_bed();
        //BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << ": model_object->ensure_on_bed()";

        m_plater->update();
    }

    m_plater->mark_plate_toolbar_image_dirty();

    Job::finalize();
}

}} // namespace Slic3r::GUI
