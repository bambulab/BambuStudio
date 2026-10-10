#include <catch2/catch.hpp>

#include "libslic3r/Arachne/BeadingStrategy/BeadingStrategyFactory.hpp"
#include "libslic3r/Arachne/SkeletalTrapezoidation.hpp"
#include "libslic3r/Polygon.hpp"

using namespace Slic3r;
using namespace Slic3r::Arachne;

namespace {

// Keep the production filter protected; expose only the operations needed by this graph-level test.
class TestableSkeletalTrapezoidation : public SkeletalTrapezoidation
{
public:
    using SkeletalTrapezoidation::SkeletalTrapezoidation;
    using SkeletalTrapezoidation::filterCentral;
    using SkeletalTrapezoidation::isEndOfCentral;
};

} // namespace

TEST_CASE("Arachne removes a short central tip", "[Arachne][FilterCentral]")
{
    const coord_t max_length = scaled<coord_t>(0.02);
    const coord_t short_length = max_length / 2;
    const coord_t outline_size = scaled<coord_t>(2.0);

    // The constructor needs a valid outline and strategy; the assertion below uses a controlled graph.
    const Polygons outline{Polygon{{Point(0, 0), Point(outline_size, 0),
                                    Point(outline_size, outline_size), Point(0, outline_size)}}};
    auto strategy = BeadingStrategyFactory::makeStrategy(scaled<coord_t>(0.4),
                                                        scaled<coord_t>(0.4),
                                                        scaled<coord_t>(0.4));
    TestableSkeletalTrapezoidation trapezoidation(outline, *strategy,
                                                  strategy->getTransitioningAngle(),
                                                  scaled<coord_t>(0.1),
                                                  scaled<coord_t>(0.1),
                                                  scaled<coord_t>(0.1),
                                                  scaled<coord_t>(0.1),
                                                  false, std::vector<int>{});

    auto &graph = trapezoidation.graph;
    graph.edges.clear();
    graph.nodes.clear();

    // A -> B is the short central tip. B is a local maximum; A can still go upward to B.
    auto &node_a = graph.nodes.emplace_back(SkeletalTrapezoidationJoint{}, Point(0, 0));
    auto &node_b = graph.nodes.emplace_back(SkeletalTrapezoidationJoint{}, Point(short_length, 0));
    auto &node_c = graph.nodes.emplace_back(SkeletalTrapezoidationJoint{}, Point(short_length, max_length * 2));
    node_a.data.distance_to_boundary = 100;
    node_b.data.distance_to_boundary = 101;
    node_c.data.distance_to_boundary = 0;

    auto &central = graph.edges.emplace_back(SkeletalTrapezoidationEdge{});
    auto &central_twin = graph.edges.emplace_back(SkeletalTrapezoidationEdge{});
    auto &support = graph.edges.emplace_back(SkeletalTrapezoidationEdge{});
    auto &support_twin = graph.edges.emplace_back(SkeletalTrapezoidationEdge{});

    central.from = &node_a;
    central.to = &node_b;
    central.twin = &central_twin;
    central.next = &support;
    central.data.setIsCentral(true);

    central_twin.from = &node_b;
    central_twin.to = &node_a;
    central_twin.twin = &central;
    central_twin.prev = &support_twin;
    central_twin.data.setIsCentral(true);

    // The noncentral B -> C edge closes B's outgoing half-edge ring.
    support.from = &node_b;
    support.to = &node_c;
    support.twin = &support_twin;
    support.prev = &central;
    support.data.setIsCentral(false);

    support_twin.from = &node_c;
    support_twin.to = &node_b;
    support_twin.twin = &support;
    support_twin.next = &central_twin;
    support_twin.data.setIsCentral(false);

    node_a.incident_edge = &central;
    node_b.incident_edge = &central_twin;
    node_c.incident_edge = &support_twin;

    // Check that the graph reaches the wrapper's intended entry conditions.
    REQUIRE(trapezoidation.isEndOfCentral(central));
    REQUIRE(node_b.isLocalMaximum());
    REQUIRE_FALSE(node_a.isLocalMaximum());

    trapezoidation.filterCentral(max_length);

    // Both half-edges must lose the central mark when the short region is dissolved.
    CHECK_FALSE(central.data.isCentral());
    CHECK_FALSE(central_twin.data.isCentral());
}
