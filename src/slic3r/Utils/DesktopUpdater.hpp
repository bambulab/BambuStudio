#ifndef slic3r_DesktopUpdater_hpp_
#define slic3r_DesktopUpdater_hpp_
#include "ReleaseUpdater.hpp"
#include <memory>
namespace Slic3r::DesktopUpdater {
// The private directory is created on the installation filesystem on macOS.
class StagedPackage {
public:
    StagedPackage(const std::string &parent, const ReleaseUpdater::Release &, const std::string &filename);
    ~StagedPackage();
    StagedPackage(const StagedPackage &) = delete;
    StagedPackage &operator=(const StagedPackage &) = delete;
    void append(const char *, std::size_t);
    // Only a fully verified package is retained after destruction.
    std::string finish();
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
std::string download_package(const std::string &parent, const ReleaseUpdater::Release &,
                             const std::string &filename, const std::atomic<bool> &, std::atomic<int> &);
const char *windows_install_script();
const char *mac_install_script();
#ifdef __APPLE__
void prepare_mac_bundle(const std::string &package, const std::string &application);
#endif
}
#endif
