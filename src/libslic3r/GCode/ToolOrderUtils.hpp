#ifndef TOOL_ORDER_UTILS_HPP
#define TOOL_ORDER_UTILS_HPP

#include <vector>
#include <optional>
#include <functional>
#include <limits>
#include <memory>
#include <unordered_set>
#include <unordered_map>
#include "../MultiNozzleUtils.hpp"

#define DEBUG_MULTI_NOZZLE_MCMF 0
namespace Slic3r {

using FlushMatrix = std::vector<std::vector<float>>;


namespace MaxFlowGraph {
    const int INF = std::numeric_limits<int>::max();
    const int INVALID_ID = -1;
    // Upper bound for MCMF edge cost to prevent int overflow in SPFA causing infinite loops
    constexpr int MCMF_MAX_EDGE_COST = 10000000;
}

struct Edge
{
    int from, to, capacity, cost, flow;
    Edge(int u, int v, int cap, int cst = 0) : from(u), to(v), capacity(cap), cost(cst), flow(0) {}
};

class MaxFlowSolver
{
public:
    MaxFlowSolver(const std::vector<int>& u_nodes, const std::vector<int>& v_nodes,
        const std::unordered_map<int, std::vector<int>>& uv_link_limits = {},
        const std::unordered_map<int, std::vector<int>>& uv_unlink_limits = {},
        const std::vector<int>& u_capacity = {},
        const std::vector<int>& v_capacity = {},
        const std::vector<std::pair<std::set<int>, int>>& v_group_capacity = {}
    );
    std::vector<int> solve();

private:
    void add_edge(int from, int to, int capacity);

    int total_nodes;
    int source_id;
    int sink_id;
    std::vector<Edge>edges;
    std::vector<int>l_nodes;
    std::vector<int>r_nodes;
    std::vector<std::vector<int>>adj;
};


struct MinCostMaxFlow;
struct MaxFlowWithLowerBounds;

class GeneralMinCostSolver
{
public:
    GeneralMinCostSolver(const std::vector<std::vector<float>>& matrix_,
        const std::vector<int>& u_nodes,
        const std::vector<int>& v_nodes);

    std::vector<int> solve();
    ~GeneralMinCostSolver();
private:
    std::unique_ptr<MinCostMaxFlow> m_solver;
};

class GeneralMinCostLowerBoundsSolver
{
public:
    GeneralMinCostLowerBoundsSolver(
        const std::vector<FlushMatrix> &matrix_,
        const std::vector<int>& u_nodes,
        const std::vector<int>& v_nodes,
        const std::vector<int>& v_nodes_group,
        const std::unordered_map<int, std::vector<int>>& uv_link_limits = {},
        const std::unordered_map<int, std::vector<int>>& uv_unlink_limits = {});

    std::vector<int> solve();
    ~GeneralMinCostLowerBoundsSolver();

private:
    void build_feasible_graph(const std::unordered_set<int>& no_lower_groups);

    void build_graph_with_feasible_result();

    void add_edge_with_lower_bound(int from, int to, int lower, int upper, int cost);

    int get_distance(const int idx_in_left,const int idx_in_right);

private:
    std::unique_ptr<MaxFlowWithLowerBounds> m_solver_lower_bounds;
    std::unique_ptr<MinCostMaxFlow> m_solver_min_cost;

    std::vector<FlushMatrix> flush_matrix;
    std::vector<int> l_nodes;
    std::vector<int> r_nodes;
    std::vector<int> r_nodes_group;
    std::unordered_map<int, std::vector<int>> m_uv_link_limits;
    std::unordered_map<int, std::vector<int>> m_uv_unlink_limits;
    int num_groups = 0;

    // support lower bounds
    struct LowerBoundEdge{
        int edge_id;
        int lower;
    };

    std::vector<int> demand;
    std::vector<LowerBoundEdge> lower_bound_edges;

    int super_source = -1;
    int super_sink = -1;
    int source_id = -1;
    int sink_id = -1;
    int max_flow_edges = 0;
};

class GroupMinCostFlowSolver
{
public:
    GroupMinCostFlowSolver(
        const std::vector<FlushMatrix> &matrix_,
        const std::vector<int> &u_nodes,
        const std::vector<int> &v_nodes,
        const std::vector<int> &v_nodes_group,
        const std::unordered_map<int, std::vector<int>> &uv_link_limits = {},
        const std::unordered_map<int, std::vector<int>> &uv_unlink_limits = {});

    std::vector<int> solve();
    ~GroupMinCostFlowSolver();

private:
    void build_graph();
    int get_flush_cost(int l_idx, int r_idx);

    std::unique_ptr<MinCostMaxFlow> m_solver;
    std::vector<FlushMatrix> flush_matrix;
    std::vector<int> l_nodes;
    std::vector<int> r_nodes;
    std::vector<int> r_nodes_group;
    std::unordered_map<int, std::vector<int>> m_uv_link_limits;
    std::unordered_map<int, std::vector<int>> m_uv_unlink_limits;
    int num_groups = 0;
};

class MinFlushFlowSolver
{
public:
    MinFlushFlowSolver(const std::vector<std::vector<float>>& matrix_,
        const std::vector<int>& u_nodes,
        const std::vector<int>& v_nodes,
        const std::unordered_map<int, std::vector<int>>& uv_link_limits = {},
        const std::unordered_map<int, std::vector<int>>& uv_unlink_limits = {},
        const std::vector<int>& u_capacity = {},
        const std::vector<int>& v_capacity = {},
        const std::vector<std::pair<std::set<int>, int>>& v_group_capacity = {});

    std::vector<int> solve();
    ~MinFlushFlowSolver();
private:
    std::unique_ptr<MinCostMaxFlow> m_solver;
};


class MatchModeGroupSolver
{
public:
    MatchModeGroupSolver(const std::vector<std::vector<float>>& matrix_,
        const std::vector<int>& u_nodes,
        const std::vector<int>& v_nodes,
        const std::vector<int>& v_capacity,
        const std::unordered_map<int, std::vector<int>>& uv_unlink_limits = {});

    std::vector<int> solve();
    ~MatchModeGroupSolver();
private:
    std::unique_ptr<MinCostMaxFlow> m_solver;
};


int reorder_filaments_for_minimum_flush_volume(const std::vector<unsigned int> &filament_lists,
                                               const std::vector<int> &filament_maps,
                                               const std::vector<std::vector<unsigned int>> &layer_filaments,
                                               const std::vector<FlushMatrix> &flush_matrix,
                                               std::optional<std::function<bool(int, std::vector<int> &)>> get_custom_seq,
                                               std::vector<std::vector<unsigned int>> *filament_sequences,
                                               const std::unordered_map<int, int>& nozzle_status = {});

#if DEBUG_MULTI_NOZZLE_MCMF
int reorder_filaments_for_multi_nozzle_extruder(const std::vector<unsigned int>& filament_lists,
                                                const std::vector<int>& filament_maps,
                                                const std::vector<std::vector<unsigned int>>& layer_filaments,
                                                const std::vector<FlushMatrix>& flush_matrix,
                                                std::optional<std::function<bool(int, std::vector<int> &)>> get_custom_seq,
                                                int multi_nozzle_extruder_id,
                                                int multi_nozzle_num,
                                                std::vector<std::vector<unsigned int>> * layer_sequences = nullptr,
                                                std::vector<std::vector<std::vector<int>>>* nozzle_match_per_layer = nullptr);

std::vector<unsigned int> get_extruders_order(const std::vector<std::vector<float>> &wipe_volumes,
    const std::vector<unsigned int> &curr_layer_extruders,
    const std::vector<unsigned int> &next_layer_extruders,
    const std::optional<unsigned int> &start_extruder_id,
    bool use_forcast = false,
    float *cost = nullptr);

#endif




// What a tool change costs on a tool changer: the incoming tool's prime plus the outgoing tool's
// ramming, both in mm3 of filament. Empty lists: every change costs 1 (just counts changes). A tool
// beyond a list costs like tool 0, as ConfigOptionVector::get_at reads a per-filament setting.
struct ToolChangeCosts
{
    std::vector<double> into;   // per tool: cost of changing into it
    std::vector<double> out_of; // per tool: cost of changing out of it

    double change(unsigned int from, unsigned int to) const;
    double into_tool(unsigned int tool) const;
    double out_of_tool(unsigned int tool) const;
};

// What a look-ahead order must improve on the original to replace it.
enum class ToolOrderPriority
{
    Balanced,      // no worse in changes and cost, better in one
    LeastWaste,    // lowest cost; fewer changes break ties
    FewestChanges, // fewest changes; lower cost breaks ties
};

// Look-ahead tool ordering for machines with one nozzle per filament (tool changers).
// Re-picks the first and last tool of every layer so that, over the whole print, as many layers as
// possible start with the tool the previous layer ended with. Exact dynamic programming over the last
// tool of each layer, for the fewest changes or for the lowest cost (each breaking ties by the other).
// Costs are compared after rounding to 1/1024 mm3, so equal totals compare equal.
// Which order replaces the original is set by `priority`. Balanced takes the fewest-changes order if it
// is no worse in cost, else the cheapest order if it is no worse in changes. (An order in between the two
// optima that beats the original in both is not searched for: that would need a state per change count
// and layer.)
// - The first non-empty layer keeps its first tool (it follows the start G-code).
// - Layers in fixed_layers (e.g. user-defined sequences), and layers that visit a tool more than
//   once, are left exactly as they are.
// - Tools between a layer's first and last tool keep their relative order.
// Returns true when layer_sequences was changed.
bool reorder_layers_with_lookahead(std::vector<std::vector<unsigned int>> &layer_sequences,
                                            const std::unordered_set<size_t>              &fixed_layers = {},
                                            const ToolChangeCosts                         &costs        = {},
                                            ToolOrderPriority                              priority     = ToolOrderPriority::Balanced);

// Look-ahead request for a print (see apply_tool_order_lookahead).
struct ToolOrderLookahead
{
    bool              enabled  = false;
    ToolOrderPriority priority = ToolOrderPriority::Balanced;
    ToolChangeCosts   costs;        // per filament; empty: every change counts the same
    // Layers kept as they are, beside user-defined sequences (layers with mixed-filament sub-layers,
    // already in their final order).
    std::vector<size_t> fixed_layers;
};

int reorder_filaments_for_multi_nozzle_extruder(const std::vector<unsigned int>& filament_lists,
                                                const MultiNozzleUtils::LayeredNozzleGroupResult& nozzle_group_result,
                                                const std::vector<std::vector<unsigned int>>& layer_filaments,
                                                const std::vector<FlushMatrix>& flush_matrix,
                                                const std::function<bool(int,std::vector<int>&)> get_custom_seq,
                                                std::vector<std::vector<unsigned int>> * filament_sequences,
                                                const MultiNozzleUtils::NozzleStatusRecorder& initial_status = {});

// True when one extruder carries one nozzle per used filament for the whole print, the only setup the
// look-ahead acts on (every tool change is then one prime).
bool tool_order_lookahead_applies(const MultiNozzleUtils::LayeredNozzleGroupResult &nozzle_group_result,
                                  const std::vector<std::vector<unsigned int>>     &layer_filaments);

enum class ToolOrderLookaheadResult
{
    Off,           // not requested
    NotApplicable, // requested, but not one nozzle per used filament on one extruder
    Unchanged,     // the original order is already as good
    Reordered,
};

// Runs the look-ahead (reorder_layers_with_lookahead) on sequences made by
// reorder_filaments_for_multi_nozzle_extruder, when it is enabled and tool_order_lookahead_applies.
// Layers with a user-defined sequence (get_custom_seq) stay as they are, as do
// lookahead.fixed_layers. filament_sequences changes only
// with the result Reordered.
ToolOrderLookaheadResult apply_tool_order_lookahead(std::vector<std::vector<unsigned int>>            &filament_sequences,
                                                    const MultiNozzleUtils::LayeredNozzleGroupResult  &nozzle_group_result,
                                                    const std::vector<std::vector<unsigned int>>      &layer_filaments,
                                                    const std::function<bool(int, std::vector<int> &)> &get_custom_seq,
                                                    const ToolOrderLookahead                          &lookahead);

}
#endif // !TOOL_ORDER_UTILS_HPP
