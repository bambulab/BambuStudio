#ifndef slic3r_ReleaseUpdater_hpp_
#define slic3r_ReleaseUpdater_hpp_
#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
namespace Slic3r::ReleaseUpdater {
struct Release {
    std::string version, notes, url, sha256;
    std::uint64_t size = 0;
};
void validate_release(const Release &release);
std::optional<Release> select_release(const std::string &json, const std::string &repository,
                                     const std::string &asset, const std::string &current_version,
                                     bool include_prereleases);
// HTTPS-only streaming transport; the sink must verify the digest before use.
void download(const Release &, const std::atomic<bool> &cancel, std::atomic<int> &percent,
              const std::function<void(const char *, std::size_t)> &append);
}
#endif
