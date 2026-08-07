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

// Minimum grid capacity (rough cell count = floor(W/step_x) * floor(H/step_y))
// for FillBed to take the fast grid path; otherwise fall back to the NFP arrange.
// Empirical threshold (STUDIO-18064): NFP path is noticeably slow even with
// only a few dozen candidate cells, so we prefer the grid path whenever the
// bed/step ratio leaves room for a reasonable number of copies.
static constexpr long long FILL_BED_GRID_PATH_MIN_CAPACITY = 50;

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

            // overlap 判定，避免姿态导致 AABB 略超盘边的对象被错划入 m_locked。
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
                    if (plate_bb.contains(ap_bb))
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

    if (m_selected.empty()) return;

    const auto* opt_enable_wrapping = global_config.option<ConfigOptionBool>("enable_wrapping_detection");
    bool enable_wrapping = opt_enable_wrapping ? opt_enable_wrapping->value : false;
    //add the virtual object into unselect list if has
    double scaled_exclusion_gap = scale_(1);
    plate_list.preprocess_exclude_areas(params.excluded_regions, enable_wrapping, 1, scaled_exclusion_gap);
    plate_list.preprocess_exclude_areas(m_unselected, enable_wrapping);

    m_bedpts = get_bed_shape(global_config);

    if (auto wt = get_wipe_tower_arrangepoly(*m_plater))
        m_unselected.emplace_back(std::move(*wt));

    double sc = scaled<double>(1.) * scaled(1.);

    auto polys = offset_ex(m_selected.front().poly, params.min_obj_distance / 2);
    ExPolygon poly = polys.empty() ? m_selected.front().poly : polys.front();
    double poly_area = poly.area() / sc;
    // m_unselected 的 bed_idx 在 prepare 阶段已统一置 0，按 0 判定而非 cur_plate_index。
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

    // STUDIO-15820: 快照模板原始 transformation；setter 里强制 reset 后再 apply_arrange_result，
    // 避免 finalize 顺序 apply 时新副本被复合上模板的额外旋转、与 arrange 用的 transformed_poly 不一致。
    Geometry::Transformation mi_orig_trafo = mi->get_transformation();
    for (int i = 0; i < needed_items; ++i) {
        ArrangePolygon ap = template_ap;
        ap.poly = m_selected.front().poly;
        ap.bed_idx = PartPlateList::MAX_PLATES_COUNT;
        ap.height = 1;
        ap.itemid = -1;
        ap.setter = [this, sel_id, mi_orig_trafo](const ArrangePolygon &p) {
            ModelObject *mo = m_plater->model().objects[m_object_idx];

            if (m_instances) {
                ModelInstance *new_instance =
                    mo->add_instance(*mo->instances[sel_id]);

                new_instance->set_transformation(mi_orig_trafo);
                new_instance->apply_arrange_result(
                    p.translation.cast<double>(),
                    p.rotation);
            } else {
                ModelObject *new_object =
                    m_plater->model().add_object(*mo);

                new_object->name =
                    mo->name + " " + std::to_string(p.itemid);

                for (ModelInstance *new_instance : new_object->instances) {
                    new_instance->set_transformation(mi_orig_trafo);
                    new_instance->apply_arrange_result(
                        p.translation.cast<double>(),
                        p.rotation);
                }

                m_plater->model().set_assembly_pos(new_object);
            }
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

    // min_obj_distance==0 时给 fill bed 加 0.5mm inflation 下限，保证副本间至少 1mm 可见间隙。
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

    if (m_bedpts.empty()) {
        BOOST_LOG_TRIVIAL(warning) << "FillBedJob::process: empty m_bedpts, abort";
        update_status(m_status_range, _L("Bed filling aborted: invalid bed shape."));
        return;
    }

    // step 复用 update_selected_items_inflation 算出的 ap.inflation，与 NFP 路径间距一致。
    const ArrangePolygon &tmpl = m_selected.front();
    const float inflation_mm = unscaled<float>(tmpl.inflation);
    const Vec2f step = unscaled<float>(get_extents(tmpl.poly).size())
                       + 2.f * Vec2f(inflation_mm, inflation_mm);

    // re-click consistency: pick path by grid capacity (stable across clicks).
    // Use double here so the floor() near-edge result is not perturbed by float rounding.
    // Use long long for the product so a (pathological) tiny step on a huge bed
    // cannot overflow signed int.
    const auto bed_size = get_extents(m_bedpts).size();
    const double bed_w  = unscaled<double>(bed_size.x());
    const double bed_h  = unscaled<double>(bed_size.y());
    const double step_x = double(step.x());
    const double step_y = double(step.y());
    const long long cells_x = (step_x > 0.0) ? (long long)std::floor(bed_w / step_x) : 0;
    const long long cells_y = (step_y > 0.0) ? (long long)std::floor(bed_h / step_y) : 0;
    const long long grid_cap = cells_x * cells_y;

    if (grid_cap > FILL_BED_GRID_PATH_MIN_CAPACITY) {
        auto offset_on_origin = tmpl.poly.contour.bounding_box().center();

        // 用 m_bedpts (已含 bed_shrink + brim_skirt_distance/2) 作为安全区，避免边缘副本 brim/skirt 越界。
        BoundingBox shrunk_bb = Polygon{m_bedpts}.bounding_box();
        BoundingBoxf safe_area_2d(
            Vec2d(unscale<double>(shrunk_bb.min.x()), unscale<double>(shrunk_bb.min.y())),
            Vec2d(unscale<double>(shrunk_bb.max.x()), unscale<double>(shrunk_bb.max.y()))
        );
        std::vector<Vec2f> empty_cells = Plater::get_empty_cells(step, safe_area_2d);

        // STUDIO-16564:
        // m_unselected 中 bed_idx==0 的项已经在 prepare 阶段统一规整到 plate-0 LOCAL，
        // 既包含真实对象，也包含 cur_plate 的虚拟障碍 (Excluded / Wrapping /
        // Nonprefered)。这些虚拟障碍必须算占用：Plater::get_empty_cells 内置的
        // exclude 过滤只在 cur_plate==0 上生效，cur_plate>0 时不会再过滤这些区域。
        // bed_idx>0 的项是 preprocess_*_areas(num_plates=MAX_NUM_PLATES) 给其它 plate
        // 注入的副本，与本次 fill bed 无关，跳过。
        // m_selected 中 priority>0 的模板实例使用世界坐标，必须先做 stride 修正再算 bbox。
        // TODO: root-cause fix is to make Plater::get_empty_cells honor exclude regions for any plate.
        PartPlateList &plist       = m_plater->get_partplate_list();
        const int plate_cols       = plist.get_plate_cols();
        const int cur_plate_index  = plist.get_curr_plate_index();
        const int cur_col          = cur_plate_index % plate_cols;
        const int cur_row          = cur_plate_index / plate_cols;
        const coord_t stride_dx    = bed_stride_x(m_plater) * cur_col;
        const coord_t stride_dy    = bed_stride_y(m_plater) * cur_row;

        std::vector<BoundingBoxf> occupied;
        auto add_occupied_local = [&](const ArrangePolygon &ap, bool needs_stride_correction) {
            BoundingBox sbb = ap.transformed_poly().contour.bounding_box();
            if (needs_stride_correction) {
                sbb.translate(-stride_dx, stride_dy);
            }
            occupied.emplace_back(
                Vec2d(unscale<double>(sbb.min.x()), unscale<double>(sbb.min.y())),
                Vec2d(unscale<double>(sbb.max.x()), unscale<double>(sbb.max.y()))
            );
        };
        size_t unsel_blocked = 0, tmpl_blocked = 0;
        for (const auto &ap : m_unselected) {
            if (ap.bed_idx != 0) continue;
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
            // 严格相交（带 ε 容差），避免浮点贴边误判导致 grid 出现不规则缺口。
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

        // 模板实例 (priority>0) 原位不动；新副本 (priority==0) 顺序填 empty_cells，超出则置 -1。
        // priority>0 的 ap.translation 是 cur_plate 的 WORLD（来自 mi->get_transformation()），
        // setter 由 ModelArrange.cpp::get_arrange_poly 设置，命中条件是 p.is_arranged() 而不是
        // p.bed_idx == 0，所以 finalize 会无条件 set_offset 到 ap.translation。
        // finalize 主循环会做 `ap.translation += bed_stride_x * ap.col`，这里先把 translation
        // 从 WORLD 减一个 stride 变成 plate-0 LOCAL，让那一步的 +stride 正好抵消回 WORLD，
        // 模板对象 ModelInstance 才能 set_offset 回原位置。
        // 旧实现里这一段被误读为冗余而删除过一次，cur_plate>0 时模板被多加一个 stride
        // 跑到盘外（见 STUDIO-18064 review 回归）。
        size_t cell_idx = 0, placed = 0, skipped = 0;
        for (size_t i = 0; i < m_selected.size(); ++i) {
            if (was_canceled()) break;
            if (m_selected[i].priority > 0) {
                m_selected[i].bed_idx = 0;
                m_selected[i].translation(X) -= stride_dx;
                m_selected[i].translation(Y) += stride_dy;
                continue;
            }
            if (cell_idx < empty_cells.size()) {
                m_selected[i].translation = scaled<coord_t>(empty_cells[cell_idx]);
                m_selected[i].translation -= offset_on_origin;
                m_selected[i].bed_idx = 0;
                ++cell_idx;
                ++placed;
                m_selected[i].itemid = static_cast<int>(placed);
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

    PartPlateList& plate_list = m_plater->get_partplate_list();
    int plate_cols = plate_list.get_plate_cols();
    int cur_plate = plate_list.get_curr_plate_index();

    size_t inst_cnt = model_object->instances.size();

    int added_cnt = std::accumulate(m_selected.begin(), m_selected.end(), 0, [](int s, auto &ap) {
        return s + int(ap.priority == 0 && ap.bed_idx == 0);
    });

    const size_t old_size = m_plater->model().objects.size();

    if (added_cnt > 0) {
        for (ArrangePolygon& ap : m_selected) {
            if (ap.bed_idx != 0) {
                BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(":skipped: bed_id %1%, trans {%2%,%3%}") % ap.bed_idx % unscale<double>(ap.translation(X)) % unscale<double>(ap.translation(Y));
                continue;
            }
            else
                ap.bed_idx = cur_plate;

            // STUDIO-15820: 把 plate-0 局部坐标平移回 cur_plate 世界坐标，否则非 0 号盘的新副本会落到 0 号盘。
            ap.row = ap.bed_idx / plate_cols;
            ap.col = ap.bed_idx % plate_cols;
            ap.translation(X) += bed_stride_x(m_plater) * ap.col;
            ap.translation(Y) -= bed_stride_y(m_plater) * ap.row;

            ap.apply();

            BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(":selected: bed_id %1%, trans {%2%,%3%}") % ap.bed_idx % unscale<double>(ap.translation(X)) % unscale<double>(ap.translation(Y));
        }

        auto *obj_list = m_plater->sidebar().obj_list();

        if (m_instances) {
            const size_t new_inst_cnt = model_object->instances.size();

            if (new_inst_cnt > inst_cnt) {
                // A single instance has no separate child node in the object
                // list. When transitioning to multiple instances, add nodes
                // for both the original instance and the newly created ones.
                const size_t list_items_to_add =
                    inst_cnt == 1
                        ? new_inst_cnt
                        : new_inst_cnt - inst_cnt;

                obj_list->increase_object_instances(
                    m_object_idx,
                    list_items_to_add);
            }

            BOOST_LOG_TRIVIAL(debug)
                << __FUNCTION__
                << ": added "
                << (new_inst_cnt - inst_cnt)
                << " instances";
        } else {
            const size_t new_size = m_plater->model().objects.size();

            for (size_t i = old_size; i < new_size; ++i) {
                obj_list->add_object_to_list(i, true, true, false);
                obj_list->update_printable_state(i, 0);
            }

            BOOST_LOG_TRIVIAL(debug)
                << __FUNCTION__
                << ": paste_objects_into_list";
        }

        m_plater->update();
    }

    m_plater->mark_plate_toolbar_image_dirty();

    Job::finalize();
}

}} // namespace Slic3r::GUI
