#include "I18N.hpp"
#include "GCode.hpp"
#include "Print.hpp"
#include "Exception.hpp"
#include "libslic3r/format.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Slic3r {

#define L(s) (s)
#define _(s) Slic3r::I18N::translate(s)

std::vector<GCode::LayerToPrint> GCode::collect_layers_to_print(const PrintObject &object)
{
    std::vector<GCode::LayerToPrint> layers_to_print;
    layers_to_print.reserve(object.layers().size() + object.support_layers().size());

    std::vector<std::pair<double, double>> warning_ranges;

    size_t              idx_object_layer     = 0;
    size_t              idx_support_layer    = 0;
    const LayerToPrint *last_extrusion_layer = nullptr;
    while (idx_object_layer < object.layers().size() || idx_support_layer < object.support_layers().size()) {
        LayerToPrint layer_to_print;
        double       print_z_min = std::numeric_limits<double>::max();
        if (idx_object_layer < object.layers().size()) {
            layer_to_print.object_layer = object.layers()[idx_object_layer++];
            print_z_min                 = std::min(print_z_min, layer_to_print.object_layer->print_z);
        }

        if (idx_support_layer < object.support_layers().size()) {
            layer_to_print.support_layer = object.support_layers()[idx_support_layer++];
            print_z_min                  = std::min(print_z_min, layer_to_print.support_layer->print_z);
        }

        if (layer_to_print.object_layer && layer_to_print.object_layer->print_z > print_z_min + EPSILON) {
            layer_to_print.object_layer = nullptr;
            --idx_object_layer;
        }

        if (layer_to_print.support_layer && layer_to_print.support_layer->print_z > print_z_min + EPSILON) {
            layer_to_print.support_layer = nullptr;
            --idx_support_layer;
        }

        layer_to_print.original_object = &object;
        layers_to_print.push_back(layer_to_print);

        bool has_extrusions = (layer_to_print.object_layer && layer_to_print.object_layer->has_extrusions()) ||
                              (layer_to_print.support_layer && layer_to_print.support_layer->has_extrusions());

        if (layers_to_print.size() == 1u) {
            if (! has_extrusions)
                throw Slic3r::SlicingError(_(L("The following object(s) have empty initial layer and can't be printed. Please cut the bottom or enable supports.")),
                                           object.id().id);
        }

        if ((layer_to_print.object_layer && layer_to_print.object_layer->has_extrusions()) || layer_to_print.support_layer) {
            double top_cd    = object.config().support_top_z_distance;
            double bottom_cd = object.config().support_bottom_z_distance == 0. ? top_cd : object.config().support_bottom_z_distance;
            top_cd           = std::ceil(top_cd / object.config().layer_height) * object.config().layer_height;
            bottom_cd        = std::ceil(bottom_cd / object.config().layer_height) * object.config().layer_height;
            double extra_gap = layer_to_print.support_layer ? bottom_cd : top_cd;

            if (last_extrusion_layer && last_extrusion_layer->support_layer) {
                double raft_gap = top_cd == 0 ? 0 : object.config().raft_contact_distance.value;
                raft_gap        = std::ceil(raft_gap / object.config().layer_height) * object.config().layer_height;
                extra_gap       = std::max(extra_gap, top_cd == 0 ? 0 : object.config().raft_contact_distance.value);
            }
            double maximal_print_z =
                (last_extrusion_layer ? last_extrusion_layer->print_z() : 0.) + layer_to_print.layer()->height + std::max(0., extra_gap);

            if (has_extrusions && layer_to_print.print_z() > maximal_print_z + 2. * EPSILON)
                warning_ranges.emplace_back(std::make_pair((last_extrusion_layer ? last_extrusion_layer->print_z() : 0.), layers_to_print.back().print_z()));
        }
        if (has_extrusions)
            last_extrusion_layer = &layers_to_print.back();
    }

    if (! warning_ranges.empty()) {
        std::string warning;
        size_t      i = 0;
        for (i = 0; i < std::min(warning_ranges.size(), size_t(5)); ++i)
            warning += Slic3r::format(_(L("Object can't be printed for empty layer between %1% and %2%.")), warning_ranges[i].first, warning_ranges[i].second) + "\n";
        warning += Slic3r::format(_(L("Object: %1%")), object.model_object()->name) + "\n" +
                   _(L("Maybe parts of the object at these height are too thin, or the object has faulty mesh"));

        const_cast<Print *>(object.print())->active_step_add_warning(
            PrintStateBase::WarningLevel::CRITICAL, warning, PrintStateBase::SlicingEmptyGcodeLayers);
    }

    return layers_to_print;
}

std::vector<std::pair<coordf_t, std::vector<GCode::LayerToPrint>>> GCode::collect_layers_to_print(const Print &print)
{
    struct OrderingItem {
        coordf_t print_z;
        size_t   object_idx;
        size_t   layer_idx;
    };

    std::vector<std::vector<LayerToPrint>> per_object(print.objects().size(), std::vector<LayerToPrint>());
    std::vector<OrderingItem>              ordering;

    std::vector<Slic3r::SlicingError> errors;

    for (size_t i = 0; i < print.objects().size(); ++i) {
        try {
            per_object[i] = collect_layers_to_print(*print.objects()[i]);
        } catch (const Slic3r::SlicingError &e) {
            errors.push_back(e);
            continue;
        }
        OrderingItem        ordering_item;
        ordering_item.object_idx = i;
        ordering.reserve(ordering.size() + per_object[i].size());
        const LayerToPrint &front = per_object[i].front();
        for (const LayerToPrint &ltp : per_object[i]) {
            ordering_item.print_z = ltp.print_z();
            ordering_item.layer_idx = &ltp - &front;
            ordering.emplace_back(ordering_item);
        }
    }

    if (! errors.empty())
        throw Slic3r::SlicingErrors(errors);

    std::sort(ordering.begin(), ordering.end(), [](const OrderingItem &oi1, const OrderingItem &oi2) { return oi1.print_z < oi2.print_z; });

    std::vector<std::pair<coordf_t, std::vector<LayerToPrint>>> layers_to_print;

    for (size_t i = 0; i < ordering.size();) {
        size_t   j    = i + 1;
        coordf_t zmax = ordering[i].print_z + EPSILON;
        for (; j < ordering.size() && ordering[j].print_z <= zmax; ++j)
            ;
        std::pair<coordf_t, std::vector<LayerToPrint>> merged;
        merged.first = 0.5 * (ordering[i].print_z + ordering[j - 1].print_z);
        merged.second.assign(print.objects().size(), LayerToPrint());
        for (; i < j; ++i) {
            const OrderingItem &oi = ordering[i];
            merged.second[oi.object_idx] = std::move(per_object[oi.object_idx][oi.layer_idx]);
        }
        layers_to_print.emplace_back(std::move(merged));
    }

    return layers_to_print;
}

std::vector<const PrintInstance *> sort_object_instances_by_model_order(const Print &print, bool init_order)
{
    std::vector<std::pair<const ModelInstance *, const PrintInstance *>> model_instance_to_print_instance;
    model_instance_to_print_instance.reserve(print.num_object_instances());
    for (const PrintObject *print_object : print.objects())
        for (const PrintInstance &print_instance : print_object->instances()) {
            if (init_order) {
                if (print.objects().size() == 1)
                    const_cast<ModelInstance *>(print_instance.model_instance)->arrange_order = 1;
                else
                    const_cast<ModelInstance *>(print_instance.model_instance)->arrange_order = print_instance.model_instance->id().id;
            }
            model_instance_to_print_instance.emplace_back(print_instance.model_instance, &print_instance);
        }
    std::sort(model_instance_to_print_instance.begin(), model_instance_to_print_instance.end(),
              [](auto &l, auto &r) { return l.first->arrange_order < r.first->arrange_order; });

    std::vector<const PrintInstance *> instances;
    instances.reserve(model_instance_to_print_instance.size());
    for (const ModelObject *model_object : print.model().objects)
        for (const ModelInstance *model_instance : model_object->instances) {
            auto it = std::lower_bound(model_instance_to_print_instance.begin(), model_instance_to_print_instance.end(),
                                       std::make_pair(model_instance, nullptr),
                                       [](auto &l, auto &r) { return l.first->arrange_order < r.first->arrange_order; });
            if (it != model_instance_to_print_instance.end() && it->first == model_instance)
                instances.emplace_back(it->second);
        }
    std::sort(instances.begin(), instances.end(), [](auto &l, auto &r) { return l->model_instance->arrange_order < r->model_instance->arrange_order; });
    return instances;
}

} // namespace Slic3r
