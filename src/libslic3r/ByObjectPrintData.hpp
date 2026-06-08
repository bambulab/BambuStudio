#pragma once
#include "GCode/ToolOrdering.hpp"
#include <utility>


namespace Slic3r {

struct PrintInstance;
class PrintObject;

struct ByObjectPrintData{
    // print instance的打印顺序
    std::vector<const PrintInstance*> print_instance_order;
    // 每个instance对应的tool ordering
    std::unordered_map<const PrintObject*, ToolOrdering> object_tool_ordering_map;
    // object的打印顺序
    std::vector<const PrintObject*> print_object_order;

    void clear();

    // 主构造函数：构造toolodering前生成filament_map。内部完成 print instance 顺序，以及每个instance对应的tool ordering
    static ByObjectPrintData build(Print* print);

private:
    static std::vector<std::vector<unsigned int>> collect_filament_data(
        const Print* print,
        const std::vector<const PrintObject*>& print_obj_order
    );
};
}
