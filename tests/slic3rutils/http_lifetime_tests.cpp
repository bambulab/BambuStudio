#include <catch2/catch.hpp>

#include "slic3r/Utils/Http.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <memory>
#include <vector>

namespace {
using namespace std::chrono_literals;

std::string test_url(const char *path)
{
    const char *base = std::getenv("BAMBU_TEST_HTTP_URL");
    if (!base)
        throw std::runtime_error("Run these tests with run_http_lifetime_tests.py");
    return std::string(base) + path;
}

struct CallbackState {
    std::promise<void> entered;
    std::promise<void> release;
    std::shared_future<void> released = release.get_future().share();
    std::atomic<bool> completed{false};
    std::atomic<bool> progress_entered{false};
};
} // namespace

TEST_CASE("Http wait joins an in-flight success or error callback", "[HttpLifetime][.]")
{
    // Staffpick and Makerlab also dispatch their panel callback on HTTP errors.
    const bool error_response = GENERATE(false, true);
    auto state = std::make_shared<CallbackState>();
    auto entered = state->entered.get_future();
    std::atomic<bool> error_callback{false};
    auto callback = [state] {
        state->entered.set_value();
        state->released.wait();
        state->completed = true;
    };
    auto request = Slic3r::Http::get(test_url(error_response ? "/error" : "/complete"))
        .timeout_max(5)
        .on_complete([callback](std::string, unsigned) { callback(); })
        .on_error([callback, &error_callback](std::string, std::string, unsigned) {
            error_callback = true;
            callback();
        }).perform();

    const bool callback_started = entered.wait_for(5s) == std::future_status::ready;
    if (!callback_started) {
        state->release.set_value();
        request->cancel();
        request->wait();
        FAIL("The loopback completion callback did not start");
    }

    auto joined = std::async(std::launch::async, [request] { request->wait(); });
    const bool waited_for_callback = joined.wait_for(50ms) == std::future_status::timeout;
    const bool finished_during_callback = request->is_finished();
    state->release.set_value();
    joined.get();
    CHECK(waited_for_callback);
    CHECK_FALSE(finished_during_callback);
    CHECK(state->completed.load());
    CHECK(error_callback.load() == error_response);
    CHECK(request->is_finished());
    // A second owner-side wait must be harmless after the thread has been joined.
    request->wait();
}

TEST_CASE("Http owner cancellation joins all overlapping requests", "[HttpLifetime][.]")
{
    std::vector<std::shared_ptr<CallbackState>> states;
    std::vector<std::future<void>> entered;
    std::vector<Slic3r::Http::Ptr> requests;
    // A homepage can have Staffpick, Makerlab and Academy in flight together.
    for (int i = 0; i < 3; ++i) {
        auto state = std::make_shared<CallbackState>();
        entered.push_back(state->entered.get_future());
        states.push_back(state);
        requests.push_back(Slic3r::Http::get(test_url("/slow"))
            .timeout_max(5)
            .on_progress([state](Slic3r::Http::Progress progress, bool &) {
                if (progress.dlnow && !state->progress_entered.exchange(true)) {
                    state->entered.set_value();
                    state->released.wait();
                }
            })
            .on_complete([state](std::string, unsigned) { state->completed = true; })
            .perform());
    }

    bool all_started = true;
    for (auto &started : entered)
        all_started = (started.wait_for(5s) == std::future_status::ready) && all_started;
    // Match WebViewPanel's cancel-all-before-join shutdown order.
    for (const auto &request : requests) request->cancel();
    for (const auto &state : states) state->release.set_value();
    for (const auto &request : requests) request->wait();

    CHECK(all_started);
    for (size_t i = 0; i < requests.size(); ++i) {
        CHECK_FALSE(states[i]->completed.load());
        CHECK(requests[i]->is_finished());
    }
}

TEST_CASE("Http cancellation is observed by the transfer worker before wait returns", "[HttpLifetime][.]")
{
    auto state = std::make_shared<CallbackState>();
    auto entered = state->entered.get_future();
    auto request = Slic3r::Http::get(test_url("/slow"))
        .timeout_max(5)
        .on_progress([state](Slic3r::Http::Progress progress, bool &) {
            if (progress.dlnow && !state->progress_entered.exchange(true)) {
                state->entered.set_value();
                state->released.wait();
            }
        })
        .on_complete([state](std::string, unsigned) { state->completed = true; })
        .perform();

    const bool transfer_started = entered.wait_for(5s) == std::future_status::ready;
    request->cancel();
    state->release.set_value();
    request->wait();
    CHECK(transfer_started);
    CHECK_FALSE(state->completed.load());
    CHECK(request->is_finished());
}

TEST_CASE("Http wait is harmless before an asynchronous request is started", "[HttpLifetime][.]")
{
    auto request = Slic3r::Http::get(test_url("/complete"));
    request.cancel();
    request.wait();
    CHECK_FALSE(request.is_finished());
}
