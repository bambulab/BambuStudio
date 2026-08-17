#ifndef slic3r_VersionPolicyParser_hpp_
#define slic3r_VersionPolicyParser_hpp_

/**
 * @file VersionPolicyParser.hpp
 * @brief Parsing and evaluation of the cloud studio_version_policy.
 *
 * Holds the decision logic, free of any HTTP, wxWidgets or application state
 * dependency so it can be unit tested on its own. Fetching lives in
 * VersionPolicyFetcher, caching in VersionPolicyManager.
 */

#include <memory>
#include <string>
#include <vector>

#include "libslic3r/Semver.hpp"

namespace Slic3r {

/** @brief Type of the cloud package carrying the policy. */
extern const char *const POLICY_PACKAGE_TYPE;

/** @brief Name of the cloud package carrying the policy. */
extern const char *const POLICY_PACKAGE_NAME;

/**
 * @brief The "<type>:<name>" entry the request asks for.
 *
 * Shared with the response lookup, so the two cannot drift apart.
 */
std::string policy_package_query();

/** @brief Points in the workflow at which a policy can be enforced. */
enum class PolicyCheckPoint : int {
    Startup,     ///< Right after the main window is up.
    BeforeSlice, ///< Before a slicing job starts.
    BeforeSend   ///< Before a print job is sent to a printer.
};

/** @brief How strongly a policy applies to a check point. */
enum class PolicySeverity : int {
    None = 0, ///< No constraint; such a check point entry is dropped while parsing.
    Warning,  ///< Inform the user, who may still continue.
    Block     ///< Forbid the operation.
};

/** @brief How the message body of a policy should be rendered. */
enum class PolicyMessageType : int {
    Strings, ///< Plain text.
    MarkDown ///< Markdown source.
};

/** @brief One policy that matched, with its text already resolved for one language. */
struct PolicyHit
{
    std::string       policy_id;
    PolicySeverity    severity{PolicySeverity::None};
    std::string       title;
    std::string       message;
    PolicyMessageType message_type{PolicyMessageType::Strings};
    std::string       action_url; ///< Empty unless the cloud sent a valid https URL.
};

/**
 * @brief A parsed policy document, ready to be evaluated.
 *
 * Entries that are disabled or unusable are dropped while parsing, so
 * everything left contributes to the outcome of a check point.
 */
class PolicyDocument
{
public:
    /**
     * @brief Parses a raw /packages/additional_info response body.
     *
     * Rejects the document as a whole on an unsupported, missing or malformed
     * schema_version major, or a missing policy_vec; a single unusable entry is
     * skipped instead.
     *
     * @param response_body Raw HTTP response body.
     * @return The document, or nullptr when nothing usable was found.
     * @note Does not throw; a malformed body yields nullptr.
     */
    static std::unique_ptr<PolicyDocument> parse_response(const std::string &response_body);

    /**
     * @brief Collects the policies that apply to a check point.
     *
     * What the hits mean for the caller is not decided here; that is the job of
     * PolicyCheckResult, which wraps them.
     *
     * @param point     Check point being evaluated.
     * @param version   Version of the running application.
     * @param lang_code Language code used to resolve the texts, e.g. "zh-cn".
     * @return Every match, in document order; empty when nothing matched.
     * @note Does not throw; a failed evaluation yields no hits.
     */
    std::vector<PolicyHit> evaluate(PolicyCheckPoint point, const Semver &version, const std::string &lang_code) const;

    /** @brief How long the document may be cached, always positive. */
    int ttl_hours() const;

    /** @brief The "version" of the document, for logging. */
    const std::string &content_version() const;

    /** @brief Number of policies the document contributes, for logging. */
    size_t policy_count() const;

    ~PolicyDocument();
    PolicyDocument(const PolicyDocument &) = delete;
    PolicyDocument &operator=(const PolicyDocument &) = delete;

private:
    PolicyDocument();

    struct priv;
    std::unique_ptr<priv> p;
};

} // namespace Slic3r

#endif // slic3r_VersionPolicyParser_hpp_
