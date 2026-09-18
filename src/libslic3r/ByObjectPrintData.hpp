#pragma once
#include "GCode/ToolOrdering.hpp"
#include "GCode/WipeTower.hpp"
#include "BoundingBox.hpp"
#include "Polygon.hpp"
#include "TriangleMesh.hpp"
#include <utility>


namespace Slic3r {

struct PrintInstance;
class PrintObject;

// Prime/wipe tower planned for a single sequential print object. Value types
// only, so it can live in ByObjectPrintData's map (WipeTowerData is non-copyable).
struct ObjectWipeTowerPlan {
    std::vector<std::vector<WipeTower::ToolChangeResult>> tool_changes;
    WipeTower::ToolChangeResult                           final_purge{};
    float                                                 depth = 0.f;
    float                                                 brim_width = 0.f;
    BoundingBoxf                                          bbx;              // local frame, incl. brim
    Vec2f                                                 rib_offset{0.f, 0.f};
    Vec2f                                                 position{0.f, 0.f}; // plate-frame XY for this object's tower
    std::vector<float>                                    used_filament;
    int                                                   number_of_toolchanges = 0;
    bool                                                  has_tower = false;

    // Preview meshes for the plater (same content as WipeTowerData::WipeTowerMeshData,
    // stored inline to avoid a Print.hpp <-> ByObjectPrintData.hpp include cycle).
    // Local frame; the plater places them at `position`.
    Polygon      preview_bottom;
    TriangleMesh preview_tower_mesh;
    TriangleMesh preview_brim_mesh;
};

struct ByObjectPrintData{
    // print instance的打印顺序
    std::vector<const PrintInstance*> print_instance_order;
    // 每个instance对应的tool ordering
    std::unordered_map<const PrintObject*, ToolOrdering> object_tool_ordering_map;
    // object的打印顺序
    std::vector<const PrintObject*> print_object_order;
    // 每个object对应的prime tower（逐件打印）
    std::unordered_map<const PrintObject*, ObjectWipeTowerPlan> object_wipe_tower_map;

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
