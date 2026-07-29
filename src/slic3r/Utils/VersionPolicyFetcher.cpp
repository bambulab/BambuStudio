/**
 * @file VersionPolicyFetcher.cpp
 * @brief Implementation of the studio_version_policy download.
 */

#include "VersionPolicyFetcher.hpp"

#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

#include "libslic3r/AppConfig.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/Utils/Http.hpp"
#include "slic3r/Utils/VersionPolicyParser.hpp"

namespace Slic3r {

namespace {

/// Appended to the OTA host built by GUI_App::get_http_url().
static const char *POLICY_API_PATH = "v1/iot-service/api/packages/additional_info";

// The policy is advisory and fails open, so a slow server must not keep the
// startup check point waiting: give up quickly rather than block the message.
static const long POLICY_TIMEOUT_CONNECT = 3;
static const long POLICY_TIMEOUT_MAX     = 6;

/**
 * @brief Builds the request URL for the running client.
 *
 * @return Fully addressed URL, query parameters included.
 * @note Reads the application config, so it must be called from the main thread.
 */
std::string build_request_url()
{
    GUI::GUI_App &app          = GUI::wxGetApp();
    std::string   country_code = app.app_config ? app.app_config->get_country_code() : std::string();

    std::string url = app.get_http_url(country_code, POLICY_API_PATH);

    // The client type and version travel as headers, added to every request by
    // Http from the globals of GUI_App::get_extra_header().
    //
    // names_with_type has to be a JSON array even for a single package; a bare
    // string is answered with 400 IOT_ERROR_INVALID_ARGUMENT.
    const std::string names = "[\"" + policy_package_query() + "\"]";
    url += "?names_with_type=" + Http::url_encode(names);
    return url;
}

/**
 * @brief Hands the outcome to the callback without ever letting it throw.
 *
 * Http runs its callbacks on a worker thread, where an escaping exception would
 * reach the thread entry point and terminate the process.
 */
void deliver(const VersionPolicyFetcher::ResponseCallback &on_response, bool success, const std::string &body)
{
    if (!on_response) {
        return;
    }

    try {
        on_response(success, body);
    } catch (const std::exception &e) {
        BOOST_LOG_TRIVIAL(error) << "[VersionPolicy]: the response handler threw, " << e.what();
    } catch (...) {
        BOOST_LOG_TRIVIAL(error) << "[VersionPolicy]: the response handler threw an unknown error";
    }
}

} // namespace

void VersionPolicyFetcher::fetch_async(ResponseCallback on_response)
{
    try {
        const std::string url = build_request_url();
        BOOST_LOG_TRIVIAL(info) << "[VersionPolicy]: fetching " << url;

        Http http = Http::get(url);
        http.header("accept", "application/json")
            .timeout_connect(POLICY_TIMEOUT_CONNECT)
            .timeout_max(POLICY_TIMEOUT_MAX)
            .on_complete([on_response](std::string body, unsigned /* status */) {
                deliver(on_response, true, body);
                })
            .on_error([on_response](std::string /* body */, std::string error, unsigned status) {
                BOOST_LOG_TRIVIAL(error) << boost::format("[VersionPolicy]: fetch failed, status=%1%, error=%2%") % status % error;
                deliver(on_response, false, std::string());
            })
            .perform();
    } catch (const std::exception &e) {
        // The request never went out, so no callback will arrive from the worker
        // thread; report the failure here instead.
        BOOST_LOG_TRIVIAL(error) << "[VersionPolicy]: failed to start the request, " << e.what();
        deliver(on_response, false, std::string());
    } catch (...) {
        BOOST_LOG_TRIVIAL(error) << "[VersionPolicy]: failed to start the request, unknown error";
        deliver(on_response, false, std::string());
    }
}

} // namespace Slic3r
