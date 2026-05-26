#ifndef slic3r_Format_AssemblyTreeJson_hpp_
#define slic3r_Format_AssemblyTreeJson_hpp_

#include <string>
#include <vector>

namespace Slic3r {

struct AssemblyTreeNodeData
{
    int              id { -1 };
    int              parent_id { -1 };
    int              object_idx { -1 };
    int              volume_idx { -1 };
    bool             selectable{true};
    std::string      uid;
    std::string      label;
    std::vector<int> children;
};

struct AssemblyTreeData
{
    std::vector<AssemblyTreeNodeData> nodes;
    std::vector<int>                  roots;
    std::vector<int>                  checked_node_indices;
    static bool                       show_origin_step_tree;
    bool empty() const { return nodes.empty(); }
    void clear()
    {
        nodes.clear();
        roots.clear();
        checked_node_indices.clear();
    }
    // JSON I/O helpers. The conversion is context-free (no Model dependency) — JSON
    std::string to_json_string() const;
    static bool from_json_string(const std::string& json_string,
                                 AssemblyTreeData&  tree,
                                 std::string*       error = nullptr);
};

} // namespace Slic3r

#endif // slic3r_Format_AssemblyTreeJson_hpp_
