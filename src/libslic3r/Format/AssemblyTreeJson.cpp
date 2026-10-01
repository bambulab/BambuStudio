#include "AssemblyTreeJson.hpp"

#include "nlohmann/json.hpp"

#include <utility>

namespace Slic3r {

using nlohmann::json;

static json node_to_json(const AssemblyTreeNodeData& node)
{
    return json{
        {"id", node.id},
        {"parent_id", node.parent_id},
        {"object_idx", node.object_idx},
        {"volume_idx", node.volume_idx},
        {"selectable", node.selectable},
        {"uid", node.uid},
        {"label", node.label},
        {"children", node.children}
    };
}
bool                        AssemblyTreeData::show_origin_step_tree = false;
static AssemblyTreeNodeData node_from_json(const json &j)
{
    AssemblyTreeNodeData node;
    node.id         = j.value("id", -1);
    node.parent_id  = j.value("parent_id", -1);
    node.object_idx = j.value("object_idx", -1);
    node.volume_idx = j.value("volume_idx", -1);
    node.selectable = j.value("selectable", true);
    node.uid        = j.value("uid", std::string());
    node.label      = j.value("label", std::string());
    node.children   = j.value("children", std::vector<int>());
    return node;
}

std::string AssemblyTreeData::to_json_string() const
{
    json j;
    j["version"] = 1;//V1.0 2020610
    j["roots"]   = this->roots;
    j["checked_node_indices"] = this->checked_node_indices;
    j["nodes"]   = json::array();
    for (const AssemblyTreeNodeData& node : this->nodes)
        j["nodes"].push_back(node_to_json(node));
    return j.dump();
}

bool AssemblyTreeData::from_json_string(const std::string& json_string, AssemblyTreeData& tree, std::string* error)
{
    try {
        json j = json::parse(json_string);
        AssemblyTreeData parsed;
        parsed.roots = j.value("roots", std::vector<int>());
        const auto checked_node_indices_it = j.find("checked_node_indices");
        if (checked_node_indices_it != j.end())
            parsed.checked_node_indices = checked_node_indices_it->get<std::vector<int>>();
        if (auto nodes_it = j.find("nodes"); nodes_it != j.end() && nodes_it->is_array()) {
            parsed.nodes.reserve(nodes_it->size());
            for (const json& node_json : *nodes_it)
                parsed.nodes.emplace_back(node_from_json(node_json));
        }
        if (checked_node_indices_it == j.end()) {
            parsed.checked_node_indices.reserve(parsed.nodes.size());
            for (size_t node_idx = 0; node_idx < parsed.nodes.size(); ++node_idx)
                parsed.checked_node_indices.emplace_back(parsed.nodes[node_idx].id >= 0 ? parsed.nodes[node_idx].id : static_cast<int>(node_idx));
        }
        tree = std::move(parsed);
        return true;
    } catch (const std::exception& e) {
        if (error != nullptr)
            *error = e.what();
        return false;
    }
}

} // namespace Slic3r
