/**
 * @file VersionPolicyManager.cpp
 * @brief Implementation of the version policy cache and orchestration.
 */

#include "VersionPolicyManager.hpp"

#include <atomic>
#include <chrono>
#include <mutex>

#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

#include "libslic3r/AppConfig.hpp"
#include "libslic3r_version.h"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/Utils/VersionPolicyFetcher.hpp"

namespace Slic3r {

namespace {

/** @brief Tells whether the feature was compiled out. */
constexpr bool policy_disabled() { return BBL_DISABLE_VERSION_POLICY != 0; }

/** @brief Language code of the running application, used to resolve policy texts. */
std::string current_language_code()
{
    AppConfig *config = GUI::wxGetApp().app_config;
    return config ? config->get_language_code() : std::string("en");
}

} // namespace

/** @brief Cached document and the state guarding its refresh. */
struct VersionPolicyManager::priv
{
    mutable std::mutex                    mutex;    ///< Guards everything below it.
    std::unique_ptr<PolicyDocument>       document; ///< Null until the first load succeeds.
    std::chrono::steady_clock::time_point loaded_at;

    std::atomic<bool>     fetching{false};       ///< Keeps concurrent requests down to one.
    std::atomic<bool>     load_announced{false}; ///< Ensures on_loaded runs only once.
    std::function<void()> on_loaded;

    /** @brief Whether the cached document has outlived its ttl_hours. Call with the lock held. */
    bool is_expired() const
    {
        if (!document) {
            return true;
        }
        return std::chrono::steady_clock::now() - loaded_at >= std::chrono::hours(document->ttl_hours());
    }

    /** @brief Parses a response body and replaces the cache with it. Runs on the worker thread. */
    void store(const std::string &body)
    {
        std::unique_ptr<PolicyDocument> parsed = PolicyDocument::parse_response(body);
        if (!parsed) {
            return;
        }

        const std::string content_version = parsed->content_version();
        const size_t      policy_count    = parsed->policy_count();
        const int         ttl_hours       = parsed->ttl_hours();

        {
            std::lock_guard<std::mutex> lock(mutex);
            document  = std::move(parsed);
            loaded_at = std::chrono::steady_clock::now();
        }

        BOOST_LOG_TRIVIAL(info) << boost::format("[VersionPolicy]: loaded version %1%, %2% policies, ttl %3%h") % content_version % policy_count %
                                       ttl_hours;

        // Only the first load is announced; a refresh must not replay the
        // startup check point.
        if (on_loaded && !load_announced.exchange(true)) {
            on_loaded();
        }
    }

    /** @brief Requests a document unless one is already on its way. */
    void fetch()
    {
        bool expected = false;
        if (!fetching.compare_exchange_strong(expected, true)) {
            return;
        }

        try {
            VersionPolicyFetcher::fetch_async([this](bool success, const std::string &body) {
                try {
                    if (success) {
                        store(body);
                    }
                } catch (const std::exception &e) {
                    BOOST_LOG_TRIVIAL(error) << "[VersionPolicy]: failed to store the document, " << e.what();
                } catch (...) {
                    BOOST_LOG_TRIVIAL(error) << "[VersionPolicy]: failed to store the document, unknown error";
                }

                // Released whatever happened above; a raised flag would block
                // every later refresh.
                fetching = false;
            });
        } catch (...) {
            BOOST_LOG_TRIVIAL(error) << "[VersionPolicy]: failed to start the request";
            fetching = false;
        }
    }
};

const PolicyHit *PolicyCheckResult::primary_hit() const
{
    const PolicyHit *top = nullptr;

    for (const auto &hit : all_hits) {
        // Strictly greater, so ties keep the first one in document order.
        if (!top || hit.severity > top->severity) {
            top = &hit;
        }
    }
    return top;
}

PolicyAction PolicyCheckResult::primary_action() const
{
    const PolicyHit *hit = primary_hit();
    if (!hit) {
        return PolicyAction::Allow;
    }

    switch (hit->severity) {
    case PolicySeverity::Block: return PolicyAction::Blocked;
    case PolicySeverity::Warning: return PolicyAction::AllowWithWarning;
    default: return PolicyAction::Allow;
    }
}

VersionPolicyManager::VersionPolicyManager() : p(new priv()) {}

VersionPolicyManager::~VersionPolicyManager() = default;

VersionPolicyManager &VersionPolicyManager::inst()
{
    // Intentionally leaked: a request whose callback writes into this instance
    // may still be in flight while static destructors run at shutdown.
    static VersionPolicyManager *s_inst = new VersionPolicyManager();
    return *s_inst;
}

void VersionPolicyManager::init(std::function<void()> on_loaded)
{
    try {
        if (policy_disabled()) {
            BOOST_LOG_TRIVIAL(info) << "[VersionPolicy]: disabled at build time, skipping the fetch";
            return;
        }

        // Set before the request goes out, so the worker thread cannot miss it.
        p->on_loaded = std::move(on_loaded);
        p->fetch();
    } catch (const std::exception &e) {
        BOOST_LOG_TRIVIAL(error) << "[VersionPolicy]: init failed, " << e.what();
    } catch (...) {
        BOOST_LOG_TRIVIAL(error) << "[VersionPolicy]: init failed with an unknown error";
    }
}

PolicyCheckResult VersionPolicyManager::check(PolicyCheckPoint point) const
{
    PolicyCheckResult result;
    bool              expired = false;

    try {
        if (policy_disabled()) {
            return result;
        }

        const std::string lang_code = current_language_code();

        {
            std::lock_guard<std::mutex> lock(p->mutex);
            expired = p->is_expired();

            if (p->document) {
                if (auto version = Semver::parse(SLIC3R_VERSION)) {
                    result.all_hits = p->document->evaluate(point, *version, lang_code);
                } else {
                    BOOST_LOG_TRIVIAL(error) << "[VersionPolicy]: cannot parse " << SLIC3R_VERSION << ", allowing";
                }
            }
        }
    } catch (const std::exception &e) {
        // Runs in the middle of slicing and sending, where a problem here has to
        // stay invisible to the caller.
        BOOST_LOG_TRIVIAL(error) << "[VersionPolicy]: check failed, allowing, " << e.what();
        return PolicyCheckResult();
    } catch (...) {
        BOOST_LOG_TRIVIAL(error) << "[VersionPolicy]: check failed with an unknown error, allowing";
        return PolicyCheckResult();
    }

    // Refreshed outside the lock and after the answer: a stale document beats
    // holding up the main thread, and the new one applies from the next check.
    if (expired) {
        p->fetch();
    }

    return result;
}

} // namespace Slic3r
