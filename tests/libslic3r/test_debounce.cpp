#include <catch2/catch.hpp>

#include "libslic3r/Debounce.hpp"

#include <thread>

using namespace Slic3r;
using namespace std::chrono;

// ---------------------------------------------------------------------------
// debounce_elapsed() — unit tests
// ---------------------------------------------------------------------------

SCENARIO("debounce_elapsed: first call always fires", "[Debounce]") {
    GIVEN("A default-initialised time_point (epoch)") {
        steady_clock::time_point last{};
        WHEN("debounce_elapsed is called with a 5-second interval") {
            const bool fired = debounce_elapsed(last, seconds(5));
            THEN("It returns true") {
                REQUIRE(fired);
            }
            THEN("last_tp is updated to a non-epoch value") {
                REQUIRE(last != steady_clock::time_point{});
            }
        }
    }
}

SCENARIO("debounce_elapsed: second immediate call is suppressed", "[Debounce]") {
    GIVEN("A time_point primed by a first accepted call") {
        steady_clock::time_point last{};
        debounce_elapsed(last, seconds(5)); // prime
        WHEN("debounce_elapsed is called again immediately") {
            const bool fired = debounce_elapsed(last, seconds(5));
            THEN("It returns false") {
                REQUIRE_FALSE(fired);
            }
        }
    }
}

SCENARIO("debounce_elapsed: call fires again after interval expires", "[Debounce]") {
    GIVEN("A time_point set to 10 seconds in the past") {
        // Simulate 'last' being 10 seconds old without sleeping.
        steady_clock::time_point last = steady_clock::now() - seconds(10);
        WHEN("debounce_elapsed is called with a 5-second interval") {
            const bool fired = debounce_elapsed(last, seconds(5));
            THEN("It returns true") {
                REQUIRE(fired);
            }
            THEN("last_tp is updated") {
                REQUIRE(last >= steady_clock::now() - milliseconds(100));
            }
        }
    }
}

SCENARIO("debounce_elapsed: zero interval fires every time", "[Debounce]") {
    GIVEN("A time_point primed by a first call") {
        steady_clock::time_point last{};
        debounce_elapsed(last, seconds(0));
        WHEN("debounce_elapsed is called immediately again with zero interval") {
            const bool fired = debounce_elapsed(last, seconds(0));
            THEN("It returns true") {
                REQUIRE(fired);
            }
        }
    }
}

SCENARIO("debounce_elapsed: last_tp is not modified on suppressed calls", "[Debounce]") {
    GIVEN("A time_point primed by a first accepted call") {
        steady_clock::time_point last{};
        debounce_elapsed(last, seconds(5));
        const auto snapshot = last;
        WHEN("A suppressed call is made") {
            debounce_elapsed(last, seconds(5));
            THEN("last_tp is unchanged") {
                REQUIRE(last == snapshot);
            }
        }
    }
}
