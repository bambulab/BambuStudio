#include "ReleaseUpdater.hpp"
#include <algorithm>
#include <array>
#include <exception>
#include <cstdlib>
#include <memory>
#include <regex>
#include <stdexcept>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
namespace Slic3r::ReleaseUpdater {
constexpr std::uint64_t max_image_size = 2ULL * 1024 * 1024 * 1024;
using Version = std::array<unsigned long, 4>;
Version parse_version(const std::string &value)
{
    static const std::regex pattern(R"(^v?([0-9]{1,5})\.([0-9]{1,5})\.([0-9]{1,5})\.([0-9]{1,5})$)");
    std::smatch match;
    if (!std::regex_match(value, match, pattern))
        throw std::runtime_error("Invalid update version");
    return {std::stoul(match[1]), std::stoul(match[2]), std::stoul(match[3]), std::stoul(match[4])};
}

void validate_release(const Release &release)
{
    if (release.size < 64 || release.size > max_image_size ||
        !std::regex_match(release.sha256, std::regex("[0-9a-f]{64}")))
        throw std::runtime_error("The release has no valid SHA-256 digest or size");
}

std::optional<Release> select_release(const std::string &body, const std::string &repository,
                                     const std::string &asset_name, const std::string &current_version,
                                     bool include_prereleases)
{
    if (!std::regex_match(repository, std::regex(R"([A-Za-z0-9_-]+/[A-Za-z0-9_.-]+)")) ||
        !std::regex_match(asset_name, std::regex(R"([A-Za-z0-9_.-]+\.(AppImage|exe|dmg))")))
        throw std::runtime_error("Invalid updater build configuration");
    const auto releases = nlohmann::json::parse(body);
    if (!releases.is_array())
        throw std::runtime_error("Unexpected release service response");
    auto best_version = parse_version(current_version);
    std::optional<Release> best;
    for (const auto &entry : releases) {
        if (entry.value("draft", false) || (!include_prereleases && entry.value("prerelease", false)))
            continue;
        const std::string tag = entry.value("tag_name", "");
        Version version;
        try { version = parse_version(tag); } catch (const std::exception &) { continue; }
        if (version <= best_version || !entry.contains("assets") || !entry["assets"].is_array())
            continue;
        for (const auto &asset : entry["assets"]) {
            if (asset.value("name", "") != asset_name)
                continue;
            Release release;
            release.version = tag[0] == 'v' ? tag.substr(1) : tag;
            release.notes = entry.contains("body") && entry["body"].is_string() ? entry["body"].get<std::string>() : "";
            release.url = asset.at("browser_download_url").get<std::string>();
            // Prevent selecting arbitrary URLs, even if metadata is malformed.
            const std::string expected = "https://github.com/" + repository + "/releases/download/" + tag + "/" + asset_name;
            if (release.url != expected)
                throw std::runtime_error("The update URL does not belong to the configured repository");
            const std::string digest = asset.value("digest", "");
            if (digest.compare(0, 7, "sha256:") != 0)
                throw std::runtime_error("The release has no SHA-256 digest");
            release.sha256 = digest.substr(7);
            if (!asset.at("size").is_number_unsigned())
                throw std::runtime_error("Invalid update size");
            release.size = asset["size"].get<std::uint64_t>();
            validate_release(release);
            best = std::move(release);
            best_version = version;
            break;
        }
    }
    return best;
}

void download(const Release &release, const std::atomic<bool> &cancel, std::atomic<int> &percent,
              const std::function<void(const char *, std::size_t)> &append)
{
    validate_release(release);
    if (cancel.load()) throw std::runtime_error("Update cancelled");
    struct Context {
        const std::function<void(const char *, std::size_t)> &append;
        const Release &release;
        const std::atomic<bool> &cancel;
        std::atomic<int> &percent;
        std::exception_ptr error;
    } context{append, release, cancel, percent, {}};
    using CurlHandle = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;
    CurlHandle curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl) throw std::runtime_error("Could not initialize update download");
    curl_easy_setopt(curl.get(), CURLOPT_URL, release.url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 2L);
#ifdef _WIN32
    // The bundled curl uses OpenSSL, whose build-machine CA path is not usable
    // on an end user's PC. Import the Windows trust store (curl >= 7.71).
    curl_easy_setopt(curl.get(), CURLOPT_SSL_OPTIONS, static_cast<long>(CURLSSLOPT_NATIVE_CA));
#elif defined(__APPLE__)
    const char *ca_file = std::getenv("SSL_CERT_FILE");
    curl_easy_setopt(curl.get(), CURLOPT_CAINFO, ca_file ? ca_file : "/etc/ssl/cert.pem");
#endif
    curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS, static_cast<long>(CURLPROTO_HTTPS));
    curl_easy_setopt(curl.get(), CURLOPT_REDIR_PROTOCOLS, static_cast<long>(CURLPROTO_HTTPS));
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl.get(), CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 1800L);
    curl_easy_setopt(curl.get(), CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl.get(), CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "BambuStudio-Updater");
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &context);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, +[](char *data, size_t size, size_t count, void *opaque) -> size_t {
        auto &ctx = *static_cast<Context *>(opaque);
        if (ctx.cancel.load()) return 0;
        try { ctx.append(data, size * count); }
        catch (...) { ctx.error = std::current_exception(); return 0; }
        return size * count;
    });
    curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, &context);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION, +[](void *opaque, curl_off_t, curl_off_t now, curl_off_t, curl_off_t) -> int {
        auto &ctx = *static_cast<Context *>(opaque);
        ctx.percent = static_cast<int>(std::min<std::uint64_t>(99, std::max<curl_off_t>(0, now) * 100ULL / ctx.release.size));
        return ctx.cancel.load() ? 1 : 0;
    });
    const auto result = curl_easy_perform(curl.get());
    if (cancel.load()) throw std::runtime_error("Update cancelled");
    if (context.error) std::rethrow_exception(context.error);
    if (result != CURLE_OK) throw std::runtime_error(std::string("Download failed: ") + curl_easy_strerror(result));
    long status = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
    if (status != 200) throw std::runtime_error("Unexpected update download response");
    if (cancel.load()) throw std::runtime_error("Update cancelled");
}
} // namespace Slic3r::ReleaseUpdater
