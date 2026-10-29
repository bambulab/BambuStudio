#ifndef slic3r_VersionPolicyFetcher_hpp_
#define slic3r_VersionPolicyFetcher_hpp_

/**
 * @file VersionPolicyFetcher.hpp
 * @brief Retrieval of the studio_version_policy package from the cloud.
 *
 * Owns everything network related and hands back a raw response body, knowing
 * nothing about its content. Parsing belongs to VersionPolicyParser, caching to
 * VersionPolicyManager.
 */

#include <functional>
#include <string>

namespace Slic3r {

/** @brief Downloads the policy package over the OTA endpoint. */
class VersionPolicyFetcher
{
public:
    /**
     * @brief Receives the outcome of a request, exactly once, failures included.
     *
     * @param success Whether @p body carries a response.
     * @param body    Raw response body, empty unless @p success is true.
     */
    using ResponseCallback = std::function<void(bool success, const std::string &body)>;

    /**
     * @brief Requests the policy package without blocking the caller.
     *
     * @param on_response Invoked once on the worker thread, for success and
     *                    failure alike. Anything touching the GUI has to hop
     *                    back to the main thread on its own.
     * @note Must be called from the main thread: addressing the request reads
     *       the application config.
     * @note Does not throw, and neither does the callback: an exception escaping
     *       @p on_response would terminate the worker thread of Http.
     */
    static void fetch_async(ResponseCallback on_response);
};

} // namespace Slic3r

#endif // slic3r_VersionPolicyFetcher_hpp_
