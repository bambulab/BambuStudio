/**
 * @file VersionPolicyParser.cpp
 * @brief Implementation of the studio_version_policy parsing and evaluation.
 *
 * Each helper sits directly above the function using it, so the file reads from
 * the raw JSON down to the hits of a check point.
 */

#include "VersionPolicyParser.hpp"

#include <algorithm>
#include <utility>

#include <boost/algorithm/string.hpp>
#include <boost/log/trivial.hpp>
#include <boost/optional.hpp>

#include "nlohmann/json.hpp"

namespace Slic3r {

using json = nlohmann::json;

const char *const POLICY_PACKAGE_TYPE = "bambu_studio";
const char *const POLICY_PACKAGE_NAME = "studio_version_policy";

std::string policy_package_query() { return std::string(POLICY_PACKAGE_TYPE) + ":" + POLICY_PACKAGE_NAME; }

/**
 * @brief Internal shape of a policy document.
 *
 * Named rather than anonymous: these types are fields of PolicyDocument::priv,
 * which has external linkage, and internal linkage there would warn.
 */
namespace version_policy_detail {

/** @brief How a policy selects the Studio versions it applies to. */
enum class VersionMatchType : int {
    All,    ///< Every version matches.
    Range,  ///< Closed interval, either bound may be open.
    Specify ///< Exact list of versions.
};

/** @brief One version selector of a policy. Several of them combine as a logical OR. */
struct VersionMatchRule
{
    VersionMatchType        type{VersionMatchType::All};
    boost::optional<Semver> range_min; ///< Inclusive lower bound, unset means open.
    boost::optional<Semver> range_max; ///< Inclusive upper bound, unset means open.
    std::vector<Semver>     versions;  ///< Candidates for VersionMatchType::Specify.
};

/** @brief Language code to text, in the order the cloud published them. */
using LocalizedText = std::vector<std::pair<std::string, std::string>>;

/** @brief What one policy does at one check point. */
struct PolicyCheckPointRule
{
    PolicyCheckPoint  point{PolicyCheckPoint::Startup};
    PolicySeverity    severity{PolicySeverity::None};
    LocalizedText     title;
    LocalizedText     message;
    PolicyMessageType message_type{PolicyMessageType::Strings};
    std::string       action_url;
};

/** @brief A single rule of the policy document. */
struct PolicyEntry
{
    std::string                       policy_id;
    std::vector<VersionMatchRule>     match_versions;
    std::vector<PolicyCheckPointRule> check_points;
};

/**
 * @brief Everything a parsed document holds.
 *
 * Separate from PolicyDocument::priv, which is private and therefore out of
 * reach of the parsing helpers below.
 */
struct DocumentData
{
    std::string              content_version;
    int                      ttl_hours{48};
    std::vector<PolicyEntry> entries;
};

} // namespace version_policy_detail

using namespace version_policy_detail;

/** @brief The state of a document; kept out of the header on purpose. */
struct PolicyDocument::priv : DocumentData
{};

namespace {

/**
 * @brief Parses a policy version, requiring the full four field form.
 *
 * The BBS semver parser folds the fourth field into patch as patch * 100 +
 * build, so "1.10.2" yields patch 2 where "01.10.02.00" yields 200. A short
 * string would compare against something its author never wrote.
 */
boost::optional<Semver> parse_policy_version(const std::string &str)
{
    if (std::count(str.begin(), str.end(), '.') != 3) {
        BOOST_LOG_TRIVIAL(warning) << "[VersionPolicy]: version must be AA.BB.CC.DD, got " << str;
        return boost::none;
    }
    return Semver::parse(str);
}

/** @brief Reads the bounds of a Range selector. At least one of them must be usable. */
bool parse_version_range(const json &item, VersionMatchRule &rule)
{
    if (item.contains("versions_range") && item["versions_range"].is_object()) {
        const json &range = item["versions_range"];
        if (range.contains("min") && range["min"].is_string()) {
            rule.range_min = parse_policy_version(range["min"].get<std::string>());
        }
        if (range.contains("max") && range["max"].is_string()) {
            rule.range_max = parse_policy_version(range["max"].get<std::string>());
        }
    }

    // Open on both sides would cover every version, which a range never means.
    if (!rule.range_min && !rule.range_max) {
        BOOST_LOG_TRIVIAL(warning) << "[VersionPolicy]: range rule without a valid min or max, ignored";
        return false;
    }
    return true;
}

/** @brief Reads the version list of a Specify selector. */
bool parse_version_list(const json &item, VersionMatchRule &rule)
{
    if (item.contains("versions") && item["versions"].is_array()) {
        for (const auto &v : item["versions"]) {
            if (!v.is_string()) {
                continue;
            }
            if (auto parsed = parse_policy_version(v.get<std::string>())) {
                rule.versions.emplace_back(*parsed);
            }
        }
    }

    if (rule.versions.empty()) {
        BOOST_LOG_TRIVIAL(warning) << "[VersionPolicy]: specify rule without a valid version, ignored";
        return false;
    }
    return true;
}

/**
 * @brief Reads the match_versions array of a policy.
 * @return true when at least one usable selector was found.
 */
bool parse_match_versions(const json &array, std::vector<VersionMatchRule> &out)
{
    if (!array.is_array()) {
        return false;
    }

    for (const auto &item : array) {
        if (!item.is_object() || !item.contains("match_type") || !item["match_type"].is_string()) {
            continue;
        }

        VersionMatchRule  rule;
        const std::string type = boost::to_lower_copy(item["match_type"].get<std::string>());

        if (type == "all") {
            rule.type = VersionMatchType::All;
        } else if (type == "range") {
            rule.type = VersionMatchType::Range;
            if (!parse_version_range(item, rule)) {
                continue;
            }
        } else if (type == "specify") {
            rule.type = VersionMatchType::Specify;
            if (!parse_version_list(item, rule)) {
                continue;
            }
        } else {
            BOOST_LOG_TRIVIAL(warning) << "[VersionPolicy]: unknown match_type " << type << ", ignored";
            continue;
        }

        out.emplace_back(std::move(rule));
    }

    return !out.empty();
}

/** @brief Maps a point_type value onto the enum. */
boost::optional<PolicyCheckPoint> parse_check_point_type(const std::string &str)
{
    if (str == "startup") {
        return PolicyCheckPoint::Startup;
    }
    if (str == "before_slice") {
        return PolicyCheckPoint::BeforeSlice;
    }
    if (str == "before_send") {
        return PolicyCheckPoint::BeforeSend;
    }
    return boost::none;
}

/** @brief Maps a severity value onto the enum, defaulting to no constraint. */
PolicySeverity parse_severity(const std::string &str)
{
    if (str == "warning") {
        return PolicySeverity::Warning;
    }
    if (str == "block") {
        return PolicySeverity::Block;
    }
    return PolicySeverity::None;
}

/**
 * @brief Collects the language entries of an object, preserving document order.
 *
 * A message object carries its type among the language entries; both spellings
 * of that key are skipped here and read by @ref parse_message_type.
 */
LocalizedText parse_localized(const json &object)
{
    LocalizedText texts;
    if (!object.is_object()) {
        return texts;
    }

    for (auto it = object.begin(); it != object.end(); ++it) {
        const std::string key = boost::to_lower_copy(it.key());
        if (key == "message_type" || key == "messgage_type") {
            continue;
        }
        if (!it.value().is_string()) {
            continue;
        }
        texts.emplace_back(key, it.value().get<std::string>());
    }
    return texts;
}

/** @brief Reads the rendering type of a message object, tolerating both spellings. */
PolicyMessageType parse_message_type(const json &message)
{
    for (const char *key : {"messgage_type", "message_type"}) {
        if (!message.contains(key) || !message[key].is_string()) {
            continue;
        }
        if (boost::to_lower_copy(message[key].get<std::string>()) == "mark_down") {
            return PolicyMessageType::MarkDown;
        }
        break;
    }
    return PolicyMessageType::Strings;
}

/**
 * @brief Keeps an action_url only when it is https.
 *
 * The value comes from the server and ends up opening a browser.
 */
std::string sanitize_action_url(const std::string &url)
{
    if (url.empty()) {
        return url;
    }
    if (!boost::istarts_with(url, "https://")) {
        BOOST_LOG_TRIVIAL(warning) << "[VersionPolicy]: dropped non-https action_url";
        return std::string();
    }
    return url;
}

/** @brief Reads the check_points array of a policy, skipping entries without a constraint. */
void parse_check_points(const json &array, std::vector<PolicyCheckPointRule> &out)
{
    if (!array.is_array()) {
        return;
    }

    for (const auto &item : array) {
        if (!item.is_object() || !item.contains("point_type") || !item["point_type"].is_string()) {
            continue;
        }

        const auto point = parse_check_point_type(boost::to_lower_copy(item["point_type"].get<std::string>()));
        if (!point) {
            BOOST_LOG_TRIVIAL(warning) << "[VersionPolicy]: unknown point_type, ignored";
            continue;
        }

        PolicyCheckPointRule rule;
        rule.point = *point;

        if (item.contains("severity") && item["severity"].is_string()) {
            rule.severity = parse_severity(boost::to_lower_copy(item["severity"].get<std::string>()));
        }
        if (rule.severity == PolicySeverity::None) {
            continue;
        }

        if (item.contains("title")) {
            rule.title = parse_localized(item["title"]);
        }

        if (item.contains("message") && item["message"].is_object()) {
            rule.message      = parse_localized(item["message"]);
            rule.message_type = parse_message_type(item["message"]);
        }

        if (item.contains("action_url") && item["action_url"].is_string()) {
            rule.action_url = sanitize_action_url(item["action_url"].get<std::string>());
        }

        out.emplace_back(std::move(rule));
    }
}

/**
 * @brief Checks the schema_version header of a document.
 *
 * Only the major number gates compatibility; a minor bump is additive and its
 * unknown fields are ignored elsewhere.
 */
bool schema_is_supported(const json &document)
{
    static const int SUPPORTED_SCHEMA_MAJOR = 1;

    if (!document.contains("schema_version") || !document["schema_version"].is_string()) {
        BOOST_LOG_TRIVIAL(warning) << "[VersionPolicy]: missing schema_version, document dropped";
        return false;
    }

    const std::string schema = document["schema_version"].get<std::string>();
    int               major  = 0;
    try {
        major = std::stoi(schema.substr(0, schema.find('.')));
    } catch (...) {
        BOOST_LOG_TRIVIAL(warning) << "[VersionPolicy]: malformed schema_version " << schema << ", document dropped";
        return false;
    }

    if (major != SUPPORTED_SCHEMA_MAJOR) {
        BOOST_LOG_TRIVIAL(warning) << "[VersionPolicy]: unsupported schema_version " << schema << ", document dropped";
        return false;
    }
    return true;
}

/** @brief Reads one policy_vec element; returns false when it carries nothing usable. */
bool parse_policy_entry(const json &item, PolicyEntry &out)
{
    if (!item.is_object()) {
        return false;
    }

    if (item.contains("enabled") && item["enabled"].is_boolean() && !item["enabled"].get<bool>()) {
        return false;
    }

    if (item.contains("policy_id") && item["policy_id"].is_string()) {
        out.policy_id = item["policy_id"].get<std::string>();
    }
    if (out.policy_id.empty()) {
        BOOST_LOG_TRIVIAL(warning) << "[VersionPolicy]: policy without policy_id, ignored";
        return false;
    }

    if (!item.contains("match_versions") || !parse_match_versions(item["match_versions"], out.match_versions)) {
        BOOST_LOG_TRIVIAL(warning) << "[VersionPolicy]: policy " << out.policy_id << " has no usable match_versions, ignored";
        return false;
    }

    if (item.contains("check_points")) {
        parse_check_points(item["check_points"], out.check_points);
    }
    return !out.check_points.empty();
}

/** @brief Reads a policy document, i.e. the content of the "setting" object. */
bool parse_document_json(const json &document, DocumentData &out)
{
    if (!document.is_object()) {
        BOOST_LOG_TRIVIAL(warning) << "[VersionPolicy]: document is not an object, dropped";
        return false;
    }

    if (!schema_is_supported(document)) {
        return false;
    }

    if (document.contains("version") && document["version"].is_string()) {
        out.content_version = document["version"].get<std::string>();
    }

    if (document.contains("ttl_hours") && document["ttl_hours"].is_number_integer()) {
        const int ttl = document["ttl_hours"].get<int>();
        if (ttl > 0) {
            out.ttl_hours = ttl;
        } else {
            BOOST_LOG_TRIVIAL(warning) << "[VersionPolicy]: ttl_hours must be positive, keeping the default";
        }
    }

    if (document.contains("policy_vec") && document["policy_vec"].is_array()) {
        for (const auto &item : document["policy_vec"]) {
            PolicyEntry entry;
            if (parse_policy_entry(item, entry)) {
                out.entries.emplace_back(std::move(entry));
            }
        }
    }

    return true;
}

/**
 * @brief Tells whether a package entry is the one carrying the policy.
 *
 * The request asks for "<type>:<name>". Whether the response repeats that whole
 * string in "name" or only the name part is not settled, so both are accepted.
 */
bool is_policy_package(const json &pkg)
{
    if (!pkg.is_object() || !pkg.contains("name") || !pkg["name"].is_string()) {
        return false;
    }

    const std::string name = pkg["name"].get<std::string>();
    return name == POLICY_PACKAGE_NAME || name == policy_package_query();
}

} // namespace

PolicyDocument::PolicyDocument() : p(new priv()) {}

PolicyDocument::~PolicyDocument() = default;

std::unique_ptr<PolicyDocument> PolicyDocument::parse_response(const std::string &response_body)
{
    try {
        const json body = json::parse(response_body);
        if (!body.contains("general_pkg_info_list") || !body["general_pkg_info_list"].is_array()) {
            BOOST_LOG_TRIVIAL(warning) << "[VersionPolicy]: response carries no general_pkg_info_list";
            return nullptr;
        }

        for (const auto &pkg : body["general_pkg_info_list"]) {
            if (!is_policy_package(pkg)) {
                continue;
            }

            if (!pkg.contains("setting") || !pkg["setting"].is_object()) {
                BOOST_LOG_TRIVIAL(warning) << "[VersionPolicy]: " << POLICY_PACKAGE_NAME << " package carries no setting object";
                return nullptr;
            }

            std::unique_ptr<PolicyDocument> document(new PolicyDocument());
            if (!parse_document_json(pkg["setting"], *document->p)) {
                return nullptr;
            }
            return document;
        }

        BOOST_LOG_TRIVIAL(info) << "[VersionPolicy]: no " << POLICY_PACKAGE_NAME << " package in the response";
        return nullptr;
    } catch (const std::exception &e) {
        // Covers json::parse and every typed access down to the last helper.
        BOOST_LOG_TRIVIAL(error) << "[VersionPolicy]: failed to parse the response, " << e.what();
        return nullptr;
    } catch (...) {
        BOOST_LOG_TRIVIAL(error) << "[VersionPolicy]: failed to parse the response, unknown error";
        return nullptr;
    }
}

int PolicyDocument::ttl_hours() const { return p->ttl_hours; }

const std::string &PolicyDocument::content_version() const { return p->content_version; }

size_t PolicyDocument::policy_count() const { return p->entries.size(); }

namespace {

/**
 * @brief Picks the text for a language, falling back until something can be shown.
 *
 * Full code, then its primary subtag so "zh-cn" still finds "zh", then English,
 * then the first entry published.
 */
std::string resolve_localized(const LocalizedText &texts, const std::string &lang_code)
{
    if (texts.empty()) {
        return std::string();
    }

    const auto find = [&texts](const std::string &key) {
        return std::find_if(texts.begin(), texts.end(), [&key](const std::pair<std::string, std::string> &entry) { return entry.first == key; });
    };

    auto it = find(lang_code);
    if (it != texts.end()) {
        return it->second;
    }

    const std::string primary = lang_code.substr(0, lang_code.find('-'));
    if (!primary.empty() && primary != lang_code) {
        it = find(primary);
        if (it != texts.end()) {
            return it->second;
        }
    }

    it = find("en");
    if (it != texts.end()) {
        return it->second;
    }

    return texts.front().second;
}

/** @brief Tells whether a version selector covers a version. */
bool version_matches(const VersionMatchRule &rule, const Semver &version)
{
    switch (rule.type) {
    case VersionMatchType::All: return true;
    case VersionMatchType::Range:
        // The parser rejects a fully open range; guard against one slipping through.
        if (!rule.range_min && !rule.range_max) {
            return false;
        }
        if (rule.range_min && version < *rule.range_min) {
            return false;
        }
        if (rule.range_max && version > *rule.range_max) {
            return false;
        }
        return true;
    case VersionMatchType::Specify:
        return std::any_of(rule.versions.begin(), rule.versions.end(), [&version](const Semver &v) { return version == v; });
    default: return false;
    }
}

} // namespace

std::vector<PolicyHit> PolicyDocument::evaluate(PolicyCheckPoint point, const Semver &version, const std::string &lang_code) const
{
    std::vector<PolicyHit> hits;

    try {
        for (const auto &entry : p->entries) {
            const bool version_hit = std::any_of(entry.match_versions.begin(), entry.match_versions.end(),
                                                 [&version](const VersionMatchRule &rule) { return version_matches(rule, version); });
            if (!version_hit) {
                continue;
            }

            for (const auto &rule : entry.check_points) {
                if (rule.point != point) {
                    continue;
                }

                PolicyHit hit;
                hit.policy_id    = entry.policy_id;
                hit.severity     = rule.severity;
                hit.title        = resolve_localized(rule.title, lang_code);
                hit.message      = resolve_localized(rule.message, lang_code);
                hit.message_type = rule.message_type;
                hit.action_url   = rule.action_url;

                hits.emplace_back(std::move(hit));
            }
        }
    } catch (const std::exception &e) {
        // A half built list would be worse than none, hence the empty one below.
        BOOST_LOG_TRIVIAL(error) << "[VersionPolicy]: evaluation failed, allowing, " << e.what();
        return std::vector<PolicyHit>();
    } catch (...) {
        BOOST_LOG_TRIVIAL(error) << "[VersionPolicy]: evaluation failed with an unknown error, allowing";
        return std::vector<PolicyHit>();
    }

    return hits;
}

} // namespace Slic3r
