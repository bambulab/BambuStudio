#include <catch2/catch.hpp>

#include "libslic3r/GCode/ToolOrderUtils.hpp"
#include "libslic3r/GCode/ToolOrdering.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <random>
#include <optional>
#include <unordered_set>

using namespace Slic3r;
using Layers = std::vector<std::vector<unsigned int>>;

static bool same_tools(std::vector<unsigned int> a, std::vector<unsigned int> b)
{
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    return a == b;
}

// Test oracle: changes (and their cost) when the layers are printed in order; a change between the
// last tool of one layer and the first tool of the next counts too, empty layers are skipped.
static std::pair<size_t, double> changes_and_cost(const Layers &layers, const ToolChangeCosts &costs)
{
    size_t                      changes = 0;
    double                      cost    = 0.;
    std::optional<unsigned int> prev;
    for (const auto &layer : layers)
        for (unsigned int tool : layer) {
            if (prev && *prev != tool) {
                ++changes;
                cost += costs.change(*prev, tool);
            }
            prev = tool;
        }
    return {changes, cost};
}
static size_t count_tool_changes(const Layers &layers) { return changes_and_cost(layers, {}).first; }
static double tool_change_cost(const Layers &layers, const ToolChangeCosts &costs) { return changes_and_cost(layers, costs).second; }

TEST_CASE("count_tool_changes counts changes inside and between layers", "[ToolOrderLookahead]")
{
    CHECK(count_tool_changes({}) == 0);
    CHECK(count_tool_changes({{1, 2, 3}}) == 2);
    CHECK(count_tool_changes({{1, 2, 3}, {3, 1}}) == 3);
    CHECK(count_tool_changes({{1, 2}, {}, {2, 1}}) == 2);
    CHECK(count_tool_changes({{1, 2}, {1, 2}}) == 3);
}

TEST_CASE("Already optimal sequences are left untouched", "[ToolOrderLookahead]")
{
    // Same four tools on every layer, each layer starting with the previous layer's last tool:
    // what the round-robin produces for a striped print. Nothing to gain.
    Layers layers = {{1, 2, 0, 3}, {3, 2, 1, 0}, {0, 3, 2, 1}};
    const Layers original = layers;
    CHECK_FALSE(reorder_layers_with_lookahead(layers));
    CHECK(layers == original);
}

TEST_CASE("A layer ends on a tool the next layer also uses", "[ToolOrderLookahead]")
{
    // Round-robin ends layer 0 on tool 3, which layer 1 does not use: one extra change.
    Layers layers = {{1, 2, 3}, {1, 2}};
    REQUIRE(count_tool_changes(layers) == 4);

    CHECK(reorder_layers_with_lookahead(layers));
    CHECK(count_tool_changes(layers) == 3);
    CHECK(layers[0].front() == 1);               // first layer keeps its first tool
    CHECK(layers[1].front() == layers[0].back()); // no change at the layer boundary
    CHECK(same_tools(layers[0], {1, 2, 3}));
    CHECK(same_tools(layers[1], {1, 2}));
}

TEST_CASE("The whole print is optimised, not one layer at a time", "[ToolOrderLookahead]")
{
    // Greedy look-ahead would end layer 0 on 3 (shared with layer 1); layer 1 then starts on 3 and
    // must end on 2, which layer 2 does not use. Ending layer 0 on 2 instead avoids every
    // boundary change.
    Layers layers = {{1, 2, 3}, {3, 2}, {3, 4}};
    CHECK(reorder_layers_with_lookahead(layers));
    CHECK(count_tool_changes(layers) == 4); // 2 + 1 + 1, no boundary changes
    CHECK(layers[0] == std::vector<unsigned int>{1, 3, 2});
    CHECK(layers[1] == std::vector<unsigned int>{2, 3});
    CHECK(layers[2] == std::vector<unsigned int>{3, 4});
}

TEST_CASE("Tools between the first and last keep their relative order", "[ToolOrderLookahead]")
{
    Layers layers = {{1, 2, 3, 4, 5}, {3}};
    CHECK(reorder_layers_with_lookahead(layers));
    CHECK(layers[0] == std::vector<unsigned int>{1, 2, 4, 5, 3});
    CHECK(layers[1] == std::vector<unsigned int>{3});
}

TEST_CASE("Fixed layers are kept exactly", "[ToolOrderLookahead]")
{
    Layers layers = {{1, 2, 3}, {2, 1}, {1, 2}};
    const std::vector<unsigned int> fixed_layer = layers[1];
    reorder_layers_with_lookahead(layers, {1});
    CHECK(layers[1] == fixed_layer);
    // Layer 0 can still end on 2 to meet the fixed layer.
    CHECK(layers[0].back() == 2);
    CHECK(count_tool_changes(layers) == 4);
}

TEST_CASE("Empty layers are skipped, the last tool carries over", "[ToolOrderLookahead]")
{
    Layers layers = {{1, 2, 3}, {}, {1, 2}};
    CHECK(reorder_layers_with_lookahead(layers));
    CHECK(layers[1].empty());
    CHECK(layers[2].front() == layers[0].back());
    CHECK(count_tool_changes(layers) == 3);
}

TEST_CASE("Single-tool layers", "[ToolOrderLookahead]")
{
    // A one-tool layer can only start and end on that tool, so the layer before should end on it.
    Layers layers = {{1, 2, 3}, {2}, {2, 4}};
    REQUIRE(count_tool_changes(layers) == 4);
    CHECK(reorder_layers_with_lookahead(layers));
    CHECK(layers[0] == std::vector<unsigned int>{1, 3, 2});
    CHECK(layers[1] == std::vector<unsigned int>{2});
    CHECK(count_tool_changes(layers) == 3);

    // A two-tool first layer must start on its first tool and so cannot end on it: no gain here.
    Layers no_gain = {{1, 2}, {1}, {1, 3}};
    const Layers original = no_gain;
    CHECK_FALSE(reorder_layers_with_lookahead(no_gain));
    CHECK(no_gain == original);
}

TEST_CASE("A layer that repeats a tool is kept, the rest is still optimised", "[ToolOrderLookahead]")
{
    Layers alone = {{1, 2, 1}, {3}};
    const Layers original_alone = alone;
    CHECK_FALSE(reorder_layers_with_lookahead(alone));
    CHECK(alone == original_alone);

    // Layer 2 visits tool 1 twice and is kept; layer 0 and 1 are arranged to meet it on tool 1.
    Layers layers = {{1, 2, 3}, {1, 2}, {1, 5, 1}};
    REQUIRE(count_tool_changes(layers) == 7);
    CHECK(reorder_layers_with_lookahead(layers));
    CHECK(layers[2] == std::vector<unsigned int>{1, 5, 1});
    CHECK(layers[0] == std::vector<unsigned int>{1, 3, 2});
    CHECK(layers[1] == std::vector<unsigned int>{2, 1});
    CHECK(count_tool_changes(layers) == 5);
}

TEST_CASE("Prime volumes decide between orders with the same number of changes", "[ToolOrderLookahead]")
{
    // Layer 0 can end on 1 or 2; either way layer 1 starts without a change. Which tool is spared
    // its prime differs: tool 1 primes 60 mm3, tool 2 only 10 mm3.
    const ToolChangeCosts prime{{30., 60., 10., 30.}, {}};
    Layers layers = {{0, 1, 2}, {2, 1, 3}};
    REQUIRE(tool_change_cost(layers, prime) == Approx(60. + 10. + 60. + 30.));
    CHECK(reorder_layers_with_lookahead(layers, {}, prime));
    CHECK(layers[0] == std::vector<unsigned int>{0, 2, 1});
    CHECK(layers[1].front() == 1);
    CHECK(tool_change_cost(layers, prime) == Approx(10. + 60. + 10. + 30.));
    CHECK(count_tool_changes(layers) == 4); // same number of changes, less primed volume

    // With equal primes there is nothing to gain and the order stays.
    Layers equal = {{0, 1, 2}, {2, 1, 3}};
    CHECK_FALSE(reorder_layers_with_lookahead(equal));
}

using Score = std::pair<size_t, double>; // (tool changes, primed cost)

// Best scores by trying every first/last tool per layer: fewest changes first, and least cost first.
static std::pair<Score, Score> brute_force_best(const Layers &layers, const std::unordered_set<size_t> &fixed, const ToolChangeCosts &prime)
{
    auto repeats = [](std::vector<unsigned int> l) {
        std::sort(l.begin(), l.end());
        return std::adjacent_find(l.begin(), l.end()) != l.end();
    };
    auto score = [&prime](const Layers &l) { return Score(count_tool_changes(l), tool_change_cost(l, prime)); };
    auto changes_first = [](const Score &x, const Score &y) { return x.first < y.first || (x.first == y.first && x.second < y.second - 1e-9); };
    auto cost_first    = [](const Score &x, const Score &y) { return x.second < y.second - 1e-9 || (std::abs(x.second - y.second) <= 1e-9 && x.first < y.first); };
    Score best_changes = score(layers), best_cost = score(layers);
    Layers current(layers.size());
    std::function<void(size_t)> visit = [&](size_t layer) {
        if (layer == layers.size()) {
            const Score s = score(current);
            if (changes_first(s, best_changes))
                best_changes = s;
            if (cost_first(s, best_cost))
                best_cost = s;
            return;
        }
        const auto &seq = layers[layer];
        const bool first_active = std::all_of(layers.begin(), layers.begin() + layer, [](const auto &l) { return l.empty(); });
        if (seq.size() <= 1 || fixed.count(layer) || repeats(seq)) {
            current[layer] = seq;
            visit(layer + 1);
            return;
        }
        for (unsigned int start : seq) {
            if (first_active && start != seq.front())
                continue;
            for (unsigned int end : seq) {
                if (end == start)
                    continue;
                current[layer] = {start};
                for (unsigned int t : seq)
                    if (t != start && t != end)
                        current[layer].push_back(t);
                current[layer].push_back(end);
                visit(layer + 1);
            }
        }
    };
    visit(0);
    return {best_changes, best_cost};
}

static bool dominates(const Score &a, const Score &b)
{
    const bool no_worse = a.first <= b.first && a.second <= b.second + 1e-9;
    return no_worse && (a.first < b.first || a.second < b.second - 1e-9);
}

TEST_CASE("Matches brute force on random small prints", "[ToolOrderLookahead]")
{
    std::mt19937 rng(20261107);
    for (int round = 0; round < 1000; ++round) {
        const size_t n_layers = 2 + rng() % 4; // 2..5 layers
        Layers layers(n_layers);
        for (auto &layer : layers) {
            std::vector<unsigned int> tools = {0, 1, 2, 3, 4};
            std::shuffle(tools.begin(), tools.end(), rng);
            tools.resize(rng() % 5); // 0..4 tools, empty layers included
            if (!tools.empty() && rng() % 8 == 0)
                tools.push_back(tools.front()); // now and then a layer that repeats a tool
            layer = tools;
        }
        std::unordered_set<size_t> fixed;
        if (rng() % 3 == 0)
            fixed.insert(rng() % n_layers);
        ToolChangeCosts prime; // empty half the time: plain change counting
        if (rng() % 2 == 0)
            for (int t = 0; t < 5; ++t) {
                prime.into.push_back(double(5 + rng() % 60));
                prime.out_of.push_back(rng() % 3 == 0 ? double(rng() % 40) : 0.);
            }

        const Layers original = layers;
        const Score  before   = {count_tool_changes(original), tool_change_cost(original, prime)};
        const auto   best     = brute_force_best(original, fixed, prime);
        INFO("round " << round);

        // Fewest changes / least waste: the best order by that measure, when it beats the original.
        for (ToolOrderPriority priority : {ToolOrderPriority::FewestChanges, ToolOrderPriority::LeastWaste}) {
            const bool   changes_first = priority == ToolOrderPriority::FewestChanges;
            const Score &target        = changes_first ? best.first : best.second;
            const bool   gain          = changes_first ? target.first < before.first || (target.first == before.first && target.second < before.second - 1e-9) :
                                                         target.second < before.second - 1e-9 || (std::abs(target.second - before.second) <= 1e-9 && target.first < before.first);
            Layers       by_priority   = original;
            INFO("priority " << int(priority));
            CHECK(reorder_layers_with_lookahead(by_priority, fixed, prime, priority) == gain);
            CHECK(gain == (by_priority != original));
            CHECK(count_tool_changes(by_priority) == (gain ? target.first : before.first));
            CHECK(tool_change_cost(by_priority, prime) == Approx(gain ? target.second : before.second));
            for (size_t i = 0; i < n_layers; ++i) {
                CHECK(same_tools(by_priority[i], original[i]));
                if (fixed.count(i))
                    CHECK(by_priority[i] == original[i]);
            }
            for (size_t i = 0; i < n_layers; ++i)
                if (!original[i].empty()) {
                    CHECK(by_priority[i].front() == original[i].front()); // first printed tool kept
                    break;
                }
        }

        // Balanced (the default).
        const bool  changed = reorder_layers_with_lookahead(layers, fixed, prime);
        const Score after   = {count_tool_changes(layers), tool_change_cost(layers, prime)};
        // Never worse in either measure; changed exactly when better in one.
        CHECK(after.first <= before.first);
        CHECK(after.second <= before.second + 1e-9);
        CHECK(changed == dominates(after, before));
        CHECK(changed == (layers != original));
        // The best order that dominates the original is found: fewest changes if that one qualifies,
        // else least primed volume if that one does.
        if (dominates(best.first, before)) {
            CHECK(after.first == best.first.first);
            CHECK(after.second == Approx(best.first.second));
        } else if (dominates(best.second, before)) {
            CHECK(after.first == best.second.first);
            CHECK(after.second == Approx(best.second.second));
        } else {
            CHECK_FALSE(changed);
        }
        for (size_t i = 0; i < n_layers; ++i) {
            CHECK(same_tools(layers[i], original[i]));
            if (fixed.count(i))
                CHECK(layers[i] == original[i]);
        }
        for (size_t i = 0; i < n_layers; ++i)
            if (!original[i].empty()) {
                CHECK(layers[i].front() == original[i].front()); // first printed tool kept
                break;
            }
    }
}

TEST_CASE("Fewer changes are not bought with more primed volume", "[ToolOrderLookahead]")
{
    // Tool 0 rams 40 mm3 when changed out of, tool 1 primes 50 mm3. The original order makes 7 changes
    // for 96 mm3; the order with the fewest changes makes 6 but costs 135 mm3, and no order is better
    // in both. So nothing changes.
    const ToolChangeCosts costs{{1., 50., 1., 1., 1.}, {40., 0., 0., 0., 0.}};
    Layers layers = {{2, 4, 0}, {0, 4, 3}, {1, 2, 0}};
    const Layers original = layers;
    REQUIRE(count_tool_changes(layers) == 7);
    REQUIRE(tool_change_cost(layers, costs) == Approx(96.));
    CHECK_FALSE(reorder_layers_with_lookahead(layers, {}, costs));
    CHECK(layers == original);

    // Asked for the fewest changes, it takes the 6-change order although it wastes more.
    Layers fewest = original;
    CHECK(reorder_layers_with_lookahead(fewest, {}, costs, ToolOrderPriority::FewestChanges));
    CHECK(count_tool_changes(fewest) == 6);
    CHECK(tool_change_cost(fewest, costs) > 96.);

    // Asked for the least waste, it never wastes more than the original, whatever the change count.
    Layers least = original;
    reorder_layers_with_lookahead(least, {}, costs, ToolOrderPriority::LeastWaste);
    CHECK(tool_change_cost(least, costs) <= 96. + 1e-9);
}

TEST_CASE("Least waste may add a tool change to save filament", "[ToolOrderLookahead]")
{
    // Tool 4 primes 50 mm3 and rams 40 mm3. The original order already has the fewest changes (4, for
    // 182 mm3). One more change lets tool 4 be swapped in and out once less: 5 changes for 143 mm3.
    const ToolChangeCosts costs{{1., 1., 1., 1., 50.}, {0., 0., 0., 0., 40.}};
    const Layers original = {{0, 4, 2}, {2, 4}, {4, 1}};
    REQUIRE(count_tool_changes(original) == 4);
    REQUIRE(tool_change_cost(original, costs) == Approx(182.));

    Layers least = original;
    CHECK(reorder_layers_with_lookahead(least, {}, costs, ToolOrderPriority::LeastWaste));
    CHECK(count_tool_changes(least) == 5);
    CHECK(tool_change_cost(least, costs) == Approx(143.));

    for (ToolOrderPriority priority : {ToolOrderPriority::Balanced, ToolOrderPriority::FewestChanges}) {
        Layers layers = original;
        CHECK_FALSE(reorder_layers_with_lookahead(layers, {}, costs, priority));
        CHECK(layers == original);
    }
}

TEST_CASE("The outgoing tool's ramming counts", "[ToolOrderLookahead]")
{
    // Tool 1 rams 30 mm3 each time it is changed out of. The original order changes out of it twice
    // (105 mm3); the best orders change out of it once, e.g. by ending the print on it (75 mm3),
    // with the same 4 changes.
    const ToolChangeCosts costs{{10., 10., 10., 10.}, {0., 30., 5., 0.}};
    Layers layers = {{0, 1, 2}, {2, 1, 3}};
    REQUIRE(tool_change_cost(layers, costs) == Approx(105.));
    CHECK(reorder_layers_with_lookahead(layers, {}, costs));
    CHECK(count_tool_changes(layers) == 4);
    CHECK(tool_change_cost(layers, costs) == Approx(75.));
}

// Through the multi-nozzle orderer, as the slicer calls it for a one-extruder tool changer.
static MultiNozzleUtils::LayeredNozzleGroupResult tool_changer(const std::vector<int> &filament_to_nozzle, int nozzles, int extruders = 1)
{
    std::vector<MultiNozzleUtils::NozzleInfo> nozzle_list;
    for (int n = 0; n < nozzles; ++n) {
        MultiNozzleUtils::NozzleInfo info;
        info.diameter    = "0.4";
        info.volume_type = NozzleVolumeType::nvtStandard;
        info.extruder_id = n % extruders;
        info.group_id    = n;
        nozzle_list.push_back(info);
    }
    std::vector<unsigned int> used;
    for (unsigned int f = 0; f < filament_to_nozzle.size(); ++f)
        used.push_back(f);
    auto result = MultiNozzleUtils::LayeredNozzleGroupResult::create(filament_to_nozzle, nozzle_list, used, true);
    REQUIRE(result.has_value());
    return *result;
}

TEST_CASE("The look-ahead step applies only when asked and only with one nozzle per filament on one extruder", "[ToolOrderLookahead]")
{
    const std::vector<unsigned int> filaments = {0, 1, 2, 3};
    const Layers layer_filaments = {{0, 1, 2}, {0, 1}, {0, 1}, {0, 1, 2}, {1, 2}};
    const std::vector<FlushMatrix> flush(1, FlushMatrix(4, std::vector<float>(4, 0.f)));
    std::function<bool(int, std::vector<int> &)> no_custom = [](int, std::vector<int> &) { return false; };
    ToolOrderLookahead lookahead;
    lookahead.enabled = true;

    // Round-robin order, then the look-ahead step on top of it.
    auto ordered = [&](const MultiNozzleUtils::LayeredNozzleGroupResult &nozzles, const std::function<bool(int, std::vector<int> &)> &custom,
                       const ToolOrderLookahead &request, bool *changed = nullptr) {
        Layers sequences;
        reorder_filaments_for_multi_nozzle_extruder(filaments, nozzles, layer_filaments, flush, custom, &sequences);
        const auto result = apply_tool_order_lookahead(sequences, nozzles, layer_filaments, custom, request);
        if (changed)
            *changed = result == ToolOrderLookaheadResult::Reordered;
        if (!request.enabled)
            CHECK(result == ToolOrderLookaheadResult::Off);
        return sequences;
    };

    const auto per_nozzle = tool_changer({0, 1, 2, 3}, 4);
    bool       changed    = false;
    const Layers off = ordered(per_nozzle, no_custom, {});
    const Layers on  = ordered(per_nozzle, no_custom, lookahead, &changed);
    REQUIRE(off.size() == layer_filaments.size());
    CHECK(changed);
    CHECK(count_tool_changes(on) < count_tool_changes(off));
    for (size_t i = 0; i < on.size(); ++i)
        CHECK(same_tools(on[i], layer_filaments[i]));

    // A user-defined sequence on layer 1 (1-based filament ids) is kept.
    std::function<bool(int, std::vector<int> &)> custom = [](int layer, std::vector<int> &seq) {
        if (layer != 1)
            return false;
        seq = {2, 1};
        return true;
    };
    CHECK(ordered(per_nozzle, custom, lookahead)[1] == std::vector<unsigned int>{1, 0});

    // Layers the request marks as fixed are kept as they are.
    ToolOrderLookahead with_fixed = lookahead;
    with_fixed.fixed_layers       = {1, 3};
    const Layers kept             = ordered(per_nozzle, no_custom, with_fixed);
    CHECK(kept[1] == off[1]);
    CHECK(kept[3] == off[3]);

    // Two used filaments sharing one nozzle: not one nozzle per filament, so the look-ahead is skipped.
    const auto shared = tool_changer({0, 0, 1, 2}, 3);
    CHECK(ordered(shared, no_custom, lookahead, &changed) == ordered(shared, no_custom, {}));
    CHECK_FALSE(changed);

    // One nozzle per filament, but spread over two extruders: skipped too.
    const auto two_extruders = tool_changer({0, 1, 2, 3}, 4, 2);
    CHECK(ordered(two_extruders, no_custom, lookahead, &changed) == ordered(two_extruders, no_custom, {}));
    CHECK_FALSE(changed);
}

TEST_CASE("Costs that differ only by rounding count as equal", "[ToolOrderLookahead]")
{
    // Found by search: summed in floating point, another order of these layers looks a hair cheaper
    // than the original although, in exact arithmetic, it is no better. That must not count as a gain.
    ToolChangeCosts costs;
    costs.into   = {0.9, 0.1, 0.5, 0.4, 0.9};
    costs.out_of = {0.8, 0., 0., 0.5, 0.};
    const Layers original = {{4, 0, 3}, {1, 2}, {0}};
    for (ToolOrderPriority priority : {ToolOrderPriority::Balanced, ToolOrderPriority::LeastWaste, ToolOrderPriority::FewestChanges}) {
        INFO("priority " << int(priority));
        Layers layers = original;
        CHECK_FALSE(reorder_layers_with_lookahead(layers, {}, costs, priority));
        CHECK(layers == original);
    }
}

TEST_CASE("Mixed-filament order is kept by a second pass", "[ToolOrderLookahead]")
{
    // The look-ahead gives mixed layers their final order up front and keeps them; the pass that runs
    // again after ordering must then leave them as they are.
    std::mt19937 rng(20261114);
    for (int round = 0; round < 500; ++round) {
        std::vector<unsigned int> layer = {0, 1, 2, 3, 4, 5};
        std::shuffle(layer.begin(), layer.end(), rng);
        layer.resize(2 + rng() % 5);
        std::vector<LayerTools::MixedSubLayerGroup> groups(1 + rng() % 2);
        for (auto &group : groups) {
            std::vector<unsigned int> components = {0, 1, 2, 3, 4, 5};
            std::shuffle(components.begin(), components.end(), rng);
            components.resize(2 + rng() % 3);
            group.components_0based = components;
        }
        INFO("round " << round);
        const auto once  = mixed_component_order(layer, groups);
        const auto twice = mixed_component_order(once, groups);
        CHECK(same_tools(once, layer));
        CHECK(twice == once);
    }
}

TEST_CASE("The look-ahead is requested only where the option can act", "[ToolOrderLookahead]")
{
    PrintConfig config;
    config.filament_colour.values          = {"#FFFFFF", "#000000", "#FF0000"};
    config.filament_prime_volume.values    = {30., 45., 20.};
    config.filament_prime_volume_nc.values = {60.};
    config.filament_diameter.values        = {1.75};
    config.filament_change_length.values   = {0., 10., 0.};

    // Off by default.
    CHECK_FALSE(make_tool_order_lookahead(config, true, {}).enabled);

    // On only for printers that treat each hotend as a tool (where the settings page shows it).
    config.tool_order_lookahead.value      = ToolOrderLookaheadMode::Balanced;
    config.handle_hotend_as_extruder.value   = false;
    CHECK_FALSE(make_tool_order_lookahead(config, true, {}).enabled);
    config.handle_hotend_as_extruder.value = true;
    config.extruder_max_nozzle_count.values = {1};
    CHECK_FALSE(make_tool_order_lookahead(config, true, {}).enabled); // one nozzle: nothing to order per nozzle
    config.extruder_max_nozzle_count.values = {9};
    REQUIRE(config.nozzle_diameter.values.size() == 1);
    CHECK(is_single_ext_multi_nozzle(config));
    auto lookahead = make_tool_order_lookahead(config, true, {});
    CHECK(lookahead.enabled);
    // Not on a printer with two extruders (where filaments may be split between them or mapped per layer).
    config.nozzle_diameter.values = {0.4, 0.4};
    CHECK_FALSE(make_tool_order_lookahead(config, true, {}).enabled);
    config.nozzle_diameter.values = {0.4};
    // With a tower, a change costs the incoming filament's filament-change prime plus the outgoing
    // filament's ramming (filament_change_length of 1.75 mm filament).
    CHECK(lookahead.costs.into == std::vector<double>{30., 45., 20.});
    REQUIRE(lookahead.costs.out_of.size() == 3);
    CHECK(lookahead.costs.out_of[0] == Approx(0.));
    CHECK(lookahead.costs.out_of[1] == Approx(10. * M_PI * 1.75 * 1.75 / 4.));

    // Layers with mixed-filament sub-layers, and their neighbours, are kept as they are.
    lookahead = make_tool_order_lookahead(config, true, {0, 5});
    CHECK(lookahead.enabled);
    CHECK(lookahead.fixed_layers == std::vector<size_t>{0, 5});

    // Without a tower every change counts the same.
    lookahead = make_tool_order_lookahead(config, false, {});
    CHECK(lookahead.enabled);
    CHECK(lookahead.costs.into.empty());
    CHECK(lookahead.costs.out_of.empty());

    // The chosen priority is passed on.
    CHECK(lookahead.priority == ToolOrderPriority::Balanced);
    config.tool_order_lookahead.value = ToolOrderLookaheadMode::LeastWaste;
    CHECK(make_tool_order_lookahead(config, true, {}).priority == ToolOrderPriority::LeastWaste);
    config.tool_order_lookahead.value = ToolOrderLookaheadMode::FewestChanges;
    CHECK(make_tool_order_lookahead(config, true, {}).priority == ToolOrderPriority::FewestChanges);
    config.tool_order_lookahead.value = ToolOrderLookaheadMode::Off;
    CHECK_FALSE(make_tool_order_lookahead(config, true, {}).enabled);
    config.tool_order_lookahead.value = ToolOrderLookaheadMode::Balanced;

    // Saving mode primes every filament alike.
    config.prime_volume_mode.value = PrimeVolumeMode::pvmSaving;
    CHECK(make_tool_order_lookahead(config, true, {}).costs.into == std::vector<double>{15., 15., 15.});
}
