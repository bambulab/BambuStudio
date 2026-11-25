// Regression tests for the LAN print-path data race (bambulab/BambuStudio#13312).
//
// Pre-fix, MachineObject::parse_json (main thread) wrote print_status / job_id_ /
// is_support_wait_sending_finish while PrintJob::wait_fn (job worker thread) polled
// them unsynchronized -> std::string reassign under the reader -> heap corruption
// ("BUG IN CLIENT OF LIBMALLOC: memory corruption of free block" observed on macOS).
//
// The fix guards those fields with m_print_state_mutex and exposes
// get_print_state_snapshot(). Run this suite under TSan on the base commit to see
// the race; on the fix branch it must stay clean.

#include <catch_main.hpp>

#include <atomic>
#include <string>
#include <thread>

#include "slic3r/GUI/DeviceManager.hpp"

using namespace Slic3r::GUI;

namespace {
// manager/agent are only stored, not dereferenced by the paths under test.
// MachineObject is non-movable (mutex members), so construct in place.
constexpr const char* kDevId = "00M00A0B0000000";
} // namespace

TEST_CASE("MachineObject print-state snapshot reflects locked writes", "[device_manager]")
{
    MachineObject obj(nullptr, nullptr, "test-printer", kDevId, "192.168.50.122");

    auto snap0 = obj.get_print_state_snapshot();
    REQUIRE(snap0.print_status.empty());
    REQUIRE(snap0.job_id.empty());
    REQUIRE(snap0.support_wait_sending_finish == false);

    obj.set_print_state("RUNNING");
    auto snap1 = obj.get_print_state_snapshot();
    REQUIRE(snap1.print_status == "RUNNING");
}

TEST_CASE("Concurrent set_print_state vs get_print_state_snapshot never tears", "[device_manager][threading]")
{
    // Mirrors the crash-2 access pattern: one thread reassigning print_status in a
    // tight loop (parse_json side), another polling it (PrintJob::wait_fn side).
    MachineObject obj(nullptr, nullptr, "test-printer", kDevId, "192.168.50.122");
    const std::string a(64, 'a'); // > SSO: every reassign frees/reallocs the heap buffer
    const std::string b(64, 'b');
    obj.set_print_state(a);

    std::atomic<bool> stop{false};
    std::thread writer([&] {
        bool which = true;
        while (!stop.load(std::memory_order_relaxed)) {
            obj.set_print_state(which ? a : b);
            which = !which;
        }
    });

    for (int i = 0; i < 200000; ++i) {
        auto snap = obj.get_print_state_snapshot();
        // A torn read (string mid-reassign) shows up as neither a nor b, or crashes.
        REQUIRE((snap.print_status == a || snap.print_status == b));
    }

    stop.store(true);
    writer.join();
}
