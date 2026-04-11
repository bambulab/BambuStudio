#include "ByObjectPrintData.hpp"
#include "Print.hpp"
#include "FilamentGroupUtils.hpp"
#include "GCode.hpp"
#include "GCode/ToolOrdering.hpp"
#include "MultiNozzleUtils.hpp"
#include "FilamentMixer.hpp"

namespace Slic3r {


std::vector<std::vector<unsigned int>> ByObjectPrintData::collect_filament_data(
    const Print* print, const std::vector<const PrintObject *> &print_obj_order)
{
    std::vector<std::vector<unsigned int>> all_filaments;

    const PrintObject* prev_object = nullptr;
    ToolOrdering temp_tool_ordering;
    for (auto obj : print_obj_order) {
        // 构造临时的tool ordering（不排序，只用于收集filament)
        temp_tool_ordering = ToolOrdering(*obj,(unsigned int)(-1));

        // 收集每层的filament，展开混色槽位为物理组分
        const auto &is_mixed = print->config().filament_is_mixed.values;
        const auto &comp_strs = print->config().filament_mixed_components.values;
        for(size_t idx = 0; idx < temp_tool_ordering.layer_tools().size(); ++idx){
            auto layer_filament = temp_tool_ordering.layer_tools()[idx].extruders;
            if (has_any_mixed_filament(is_mixed))
                layer_filament = expand_mixed_filaments(layer_filament, is_mixed, comp_strs);
            all_filaments.emplace_back(layer_filament);
        }
    }

    return all_filaments;
}

ByObjectPrintData ByObjectPrintData::build(Print* print)
{
    ByObjectPrintData data;

    if(!print->is_sequential_print()){
        return data;
    }

    data.print_instance_order = sort_object_instances_by_model_order(*print);
    if(data.print_instance_order.empty()){
        return data;
    }
    const PrintObject* prev_object = nullptr;
    for (auto instance : data.print_instance_order) {
        const PrintObject *object = instance->print_object;
        if (object != prev_object) {
            data.print_object_order.emplace_back(object);
            prev_object = object;
        }
    }

    auto all_filaments = collect_filament_data(print,data.print_object_order);
    auto used_filaments = collect_sorted_used_filaments(all_filaments);
    auto physical_unprintables = print->get_physical_unprintable_filaments(used_filaments);
    auto geometric_unprintables = print->get_geometric_unprintable_filaments();
    auto filament_unprintable_volumes = print->get_filament_unprintable_flow(used_filaments);

    if (!print->is_dynamic_group_reorder()) {
        // 按照Object打印顺序，收集所有用到的耗材序列
        auto map_mode = print->get_filament_map_mode();
        auto group_result = ToolOrdering::get_recommended_filament_maps(
            print,
            all_filaments,
            map_mode,
            physical_unprintables,
            geometric_unprintables,
            filament_unprintable_volumes
        );
        // 不带选料器时，要先将结果写入到print
        print->set_nozzle_group_result(std::make_shared<MultiNozzleUtils::LayeredNozzleGroupResult>(group_result));
    }

    prev_object = nullptr;
    int last_filament_id = -1;
    std::vector<std::vector<int>> nozzle_map_per_layer;

    // 逐件打印支持选料器时，需要逐个object追加nozzle_map，构造最终的group result
    for(size_t idx =0; idx< data.print_instance_order.size(); ++idx){
        const auto& instance = data.print_instance_order[idx];
        const auto& object = instance->print_object;
        if(object != prev_object){
            data.object_tool_ordering_map[object] = ToolOrdering(*object, last_filament_id);
            auto & curr_ordering = data.object_tool_ordering_map[object];
            curr_ordering.sort_and_build_data(*object, last_filament_id);

            if (print->is_dynamic_group_reorder()) {
                auto obj_nozzle_map_per_layer = curr_ordering.get_layered_nozzle_group_result().get_layer_filament_nozzle_maps();
                nozzle_map_per_layer.insert(nozzle_map_per_layer.end(), obj_nozzle_map_per_layer.begin(), obj_nozzle_map_per_layer.end());
            }

            int new_last_filament = curr_ordering.last_extruder();
            if(new_last_filament != -1){
                last_filament_id = new_last_filament;
            }
            prev_object = object;
        }
    }

    // 支持选料器时，需要根据nozzle_map_per_layer，重新构造group result，并写入到print中
    if (print->is_dynamic_group_reorder()) {
        auto grouping_context = GroupReorder::build_filament_group_context(
            print,
            all_filaments,
            physical_unprintables,
            geometric_unprintables,
            filament_unprintable_volumes,
            print->config().filament_map_mode.value
        );

        auto result = MultiNozzleUtils::LayeredNozzleGroupResult::create(
            nozzle_map_per_layer,
            grouping_context.nozzle_info.nozzle_list,
            used_filaments,
            all_filaments
        );

        print->set_nozzle_group_result(
            result ? std::make_shared<MultiNozzleUtils::LayeredNozzleGroupResult>(*result) :
                     std::make_shared<MultiNozzleUtils::LayeredNozzleGroupResult>()
        );
    }

    auto grouping_result = print->get_layered_nozzle_group_result();
    if (!grouping_result->is_support_dynamic_nozzle_map()) {
        print->update_filament_maps_to_config(FilamentGroupUtils::update_used_filament_values(print->config().filament_map.values, grouping_result->get_extruder_map(false),
                                                                                              grouping_result->get_used_filaments()),
                                              FilamentGroupUtils::update_used_filament_values(print->config().filament_volume_map.values, grouping_result->get_volume_map(),
                                                                                              grouping_result->get_used_filaments()),
                                              grouping_result->get_nozzle_map());
    } else {
        print->update_to_config_by_nozzle_group_result(*grouping_result);
    }
    return data;
}



void ByObjectPrintData::clear()
{
    print_instance_order.clear();
    object_tool_ordering_map.clear();
}



}
