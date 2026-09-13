#include <catch2/catch.hpp>

#include "libslic3r/GCode/LayerTimeSmoothing.hpp"

#include <cfloat>

using namespace Slic3r;

// Layer made of a single extruder with one adjustable inner extrusion block and one adjustable outer wall block.
// Times are chosen so that the layer takes inner_time + outer_time seconds, min_speed bounds the slow down.
static std::vector<PerExtruderAdjustments> make_layer(float inner_time, float outer_time, float feedrate = 100.f, float min_speed = 20.f)
{
    std::vector<PerExtruderAdjustments> layer(1);
    PerExtruderAdjustments &adj = layer.front();
    adj.slow_down_min_speed = min_speed;
    auto add = [&adj, feedrate, min_speed](float time, bool outer) {
        CoolingLine line(CoolingLine::TYPE_G1 | CoolingLine::TYPE_ADJUSTABLE | (outer ? CoolingLine::TYPE_EXTERNAL_PERIMETER : 0), 0, 0);
        line.length   = time * feedrate;
        line.feedrate = feedrate;
        line.time     = time;
        line.time_max = min_speed > 0.f ? line.length / min_speed : FLT_MAX;
        adj.lines.emplace_back(line);
    };
    if (inner_time > 0.f)
        add(inner_time, false);
    if (outer_time > 0.f)
        add(outer_time, true);
    return layer;
}

static void check_variation(const std::vector<float> &t, float v, size_t first)
{
    for (size_t i = first + 1; i < t.size(); ++ i) {
        REQUIRE(t[i] >= t[i - 1] * (1.f - v) - 1e-2f);
        REQUIRE(t[i - 1] >= t[i] * (1.f - v) - 1e-2f);
    }
}

TEST_CASE("Layer time smoothing: target times obey the variation limit", "[LayerTimeSmoothing]")
{
    // Example of the OrcaSlicer prototype discussion: 10, 100, 200, 50, 10 seconds, 20% variation, no caps.
    std::vector<float> times = { 10.f, 100.f, 200.f, 50.f, 10.f };
    std::vector<float> caps(times.size(), FLT_MAX);
    std::vector<float> t = LayerTimeSmoother::solve_target_times(times, caps, 0.2f, 0);

    check_variation(t, 0.2f, 0);
    // Layers are only ever slowed down.
    for (size_t i = 0; i < t.size(); ++ i)
        REQUIRE(t[i] >= times[i]);
    // The slowest layer is not changed, its neighbours are raised to exactly 80% of it.
    REQUIRE(t[2] == Approx(200.f));
    REQUIRE(t[1] == Approx(160.f));
    REQUIRE(t[3] == Approx(160.f));
    REQUIRE(t[0] == Approx(128.f));
    REQUIRE(t[4] == Approx(128.f));
}

TEST_CASE("Layer time smoothing: layers before first_layer are untouched and do not constrain", "[LayerTimeSmoothing]")
{
    std::vector<float> times = { 300.f, 10.f, 10.f, 10.f };
    std::vector<float> caps(times.size(), FLT_MAX);
    std::vector<float> t = LayerTimeSmoother::solve_target_times(times, caps, 0.25f, 1);
    REQUIRE(t[0] == Approx(300.f));
    REQUIRE(t[1] == Approx(10.f));
    REQUIRE(t[2] == Approx(10.f));
    REQUIRE(t[3] == Approx(10.f));
}

TEST_CASE("Layer time smoothing: caps stop the propagation", "[LayerTimeSmoothing]")
{
    std::vector<float> times = { 6.f, 6.f, 6.f, 40.f, 40.f };
    // Each layer may at most triple.
    std::vector<float> caps;
    for (float t : times)
        caps.emplace_back(t * 3.f);
    std::vector<float> t = LayerTimeSmoother::solve_target_times(times, caps, 0.25f, 0);
    // The layer below the step hits its cap, the layers below it ramp down with 25% per layer.
    REQUIRE(t[2] == Approx(18.f));
    REQUIRE(t[1] == Approx(13.5f));
    REQUIRE(t[0] == Approx(10.125f));
    REQUIRE(t[3] == Approx(40.f));
    REQUIRE(t[4] == Approx(40.f));
}

TEST_CASE("Layer time smoothing: total time increase limit relaxes the variation", "[LayerTimeSmoothing]")
{
    std::vector<float> times = { 5.f, 5.f, 5.f, 5.f, 50.f, 5.f, 5.f, 5.f, 5.f };
    std::vector<float> caps(times.size(), FLT_MAX);
    float effective = 0.f;

    // Without a budget the increase is large.
    std::vector<float> unlimited = LayerTimeSmoother::solve_target_times(times, caps, 0.25f, 100.f, 0, effective);
    REQUIRE(effective == Approx(0.25f));
    double base = 0., increase_unlimited = 0.;
    for (size_t i = 0; i < times.size(); ++ i) {
        base += times[i];
        increase_unlimited += unlimited[i] - times[i];
    }
    REQUIRE(increase_unlimited > 0.5 * base);

    // With a 20% budget the variation is relaxed until the increase fits.
    std::vector<float> limited = LayerTimeSmoother::solve_target_times(times, caps, 0.25f, 0.2f, 0, effective);
    REQUIRE(effective > 0.25f);
    REQUIRE(effective <= 1.f);
    double increase_limited = 0.;
    for (size_t i = 0; i < times.size(); ++ i) {
        REQUIRE(limited[i] >= times[i]);
        increase_limited += limited[i] - times[i];
    }
    REQUIRE(increase_limited <= 0.2 * base + 1e-2);
    check_variation(limited, effective, 0);
}

TEST_CASE("Layer time smoothing: stretching a layer slows the extrusions proportionally", "[LayerTimeSmoothing]")
{
    std::vector<PerExtruderAdjustments> layer = make_layer(10.f, 5.f);
    REQUIRE(LayerTimeSmoother::layer_time(layer) == Approx(15.f));
    REQUIRE(LayerTimeSmoother::layer_time_max(layer, ltsAll) == Approx(75.f));

    float reached = LayerTimeSmoother::stretch_layer(layer, 30.f, ltsAll);
    REQUIRE(reached == Approx(30.f));
    for (const CoolingLine &line : layer.front().lines) {
        REQUIRE(line.slowdown);
        REQUIRE(line.feedrate == Approx(50.f));
        REQUIRE(line.time == Approx(line.length / line.feedrate));
    }
}

TEST_CASE("Layer time smoothing: outer walls can be excluded", "[LayerTimeSmoothing]")
{
    std::vector<PerExtruderAdjustments> layer = make_layer(10.f, 5.f);
    REQUIRE(LayerTimeSmoother::layer_time_max(layer, ltsExcludeOuterWalls) == Approx(55.f));

    float reached = LayerTimeSmoother::stretch_layer(layer, 25.f, ltsExcludeOuterWalls);
    REQUIRE(reached == Approx(25.f));
    const CoolingLine &inner = layer.front().lines[0];
    const CoolingLine &outer = layer.front().lines[1];
    REQUIRE(inner.slowdown);
    REQUIRE(inner.time == Approx(20.f));
    REQUIRE(! outer.slowdown);
    REQUIRE(outer.feedrate == Approx(100.f));
    REQUIRE(outer.time == Approx(5.f));
}

TEST_CASE("Layer time smoothing: the minimum print speed is respected", "[LayerTimeSmoothing]")
{
    std::vector<PerExtruderAdjustments> layer = make_layer(10.f, 0.f);
    // Slowing to 20 mm/s from 100 mm/s allows at most 50 s.
    float reached = LayerTimeSmoother::stretch_layer(layer, 500.f, ltsAll);
    REQUIRE(reached == Approx(50.f));
    REQUIRE(layer.front().lines[0].feedrate == Approx(20.f));
}

TEST_CASE("Layer time smoothing: process ties everything together", "[LayerTimeSmoothing]")
{
    std::vector<std::vector<PerExtruderAdjustments>> layers;
    layers.emplace_back(make_layer(30.f, 10.f)); // first layer, untouched
    layers.emplace_back(make_layer(4.f, 2.f));
    layers.emplace_back(make_layer(4.f, 2.f));
    layers.emplace_back(make_layer(30.f, 10.f));
    layers.emplace_back(make_layer(4.f, 2.f));

    std::vector<std::vector<PerExtruderAdjustments> *> ptrs;
    std::vector<float>                                 times;
    for (auto &l : layers) {
        ptrs.emplace_back(&l);
        times.emplace_back(LayerTimeSmoother::layer_time(l));
    }

    LayerTimeSmoother::Params params;
    params.max_variation      = 0.25f;
    params.max_layer_slowdown = 10.f;
    params.max_total_increase = 10.f;
    params.scope              = ltsAll;
    LayerTimeSmoother smoother(params);
    size_t n = smoother.process(ptrs, times, 1);

    REQUIRE(n == 3);
    REQUIRE(times[0] == Approx(40.f));
    REQUIRE(times[3] == Approx(40.f));
    REQUIRE(times[2] == Approx(30.f));
    REQUIRE(times[4] == Approx(30.f));
    REQUIRE(times[1] == Approx(22.5f));
    // The parsed lines were actually slowed down.
    REQUIRE(LayerTimeSmoother::layer_time(layers[2]) == Approx(30.f));
    REQUIRE(LayerTimeSmoother::layer_time(layers[0]) == Approx(40.f));
    REQUIRE(smoother.layer_comment(2) == "; LAYER_TIME_SMOOTHING factor=0.200 t_raw=6.00 t_out=30.00\n");
    REQUIRE(smoother.layer_comment(0) == "; LAYER_TIME_SMOOTHING factor=1.000 t_raw=40.00 t_out=40.00\n");
}
