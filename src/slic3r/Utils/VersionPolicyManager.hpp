#ifndef slic3r_VersionPolicyManager_hpp_
#define slic3r_VersionPolicyManager_hpp_

/**
 * @file VersionPolicyManager.hpp
 * @brief Entry point of the cloud driven version policy.
 *
 * Fetches the package through VersionPolicyFetcher, parses it with
 * VersionPolicyParser, caches the document and answers check points from it.
 * The layer fails open: anything that goes wrong resolves to Allow.
 */

#include <functional>
#include <memory>
#include <vector>

#include "VersionPolicyParser.hpp"

/**
 * @brief Build time switch of the whole feature.
 *
 * The emergency brake against a policy that turns out to be harmful: set it to
 * 1, e.g. through -DBBL_DISABLE_VERSION_POLICY=1, for a build that never
 * fetches a document and lets every check point through.
 */
#ifndef BBL_DISABLE_VERSION_POLICY
#define BBL_DISABLE_VERSION_POLICY 0
#endif

namespace Slic3r {

/** @brief What the caller of a check point must do. */
enum class PolicyAction : int {
    Allow,            ///< Nothing matched, or the policy layer failed open.
    AllowWithWarning, ///< The caller may continue but should show the message.
    Blocked           ///< The caller must abort the operation.
};

/**
 * @brief Outcome of evaluating one check point.
 *
 * The hits are the whole state; everything else is derived from them.
 */
struct PolicyCheckResult
{
    /** @brief Every hit on the check point, in document order, so messages can be merged. */
    std::vector<PolicyHit> all_hits;

    /**
     * @brief The most severe hit, deciding the action and the title to show.
     * @return nullptr when nothing matched.
     */
    const PolicyHit *primary_hit() const;

    /** @brief What the caller must do, following the severity of primary_hit(). */
    PolicyAction primary_action() const;

    bool blocked() const { return primary_action() == PolicyAction::Blocked; }
    bool has_message() const { return !all_hits.empty(); }
};

/**
 * @brief Applies the cloud version policy to the running Studio version.
 *
 * The document is held in memory only, so it is reloaded on every launch.
 */
class VersionPolicyManager
{
public:
    /** @brief The single instance, created on first use. */
    static VersionPolicyManager &inst();

    /**
     * @brief Starts the initial fetch, once the network is up.
     *
     * Does nothing when BBL_DISABLE_VERSION_POLICY is set.
     *
     * @param on_loaded Invoked once on the worker thread after the first
     *                  document arrives, never on a later refresh. The startup
     *                  check point needs it because the cache is still empty
     *                  when init() returns; a GUI caller has to hop back to the
     *                  main thread on its own.
     * @note Ignored while a request is already in flight. Does not throw.
     */
    void init(std::function<void()> on_loaded = nullptr);

    /**
     * @brief Evaluates a check point against the cached policy document.
     *
     * Refreshes an expired cache in the background, answering from the previous
     * document meanwhile rather than blocking the caller on a request.
     *
     * @param point Check point to evaluate.
     * @return An Allow result when nothing matched or when anything failed.
     * @note Does not throw and performs no network I/O; safe on the main thread.
     */
    PolicyCheckResult check(PolicyCheckPoint point) const;

private:
    VersionPolicyManager();
    ~VersionPolicyManager();
    VersionPolicyManager(const VersionPolicyManager &) = delete;
    VersionPolicyManager &operator=(const VersionPolicyManager &) = delete;

    struct priv;
    std::unique_ptr<priv> p;
};

} // namespace Slic3r

#endif // slic3r_VersionPolicyManager_hpp_
