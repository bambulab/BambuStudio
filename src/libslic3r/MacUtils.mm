#import <Foundation/Foundation.h>
#import "MacUtils.hpp"

namespace Slic3r {

bool is_macos_support_boost_add_file_log()
{
    if (@available(macOS 12.0, *)) {
        return true;
    } else {
        return false;
    }
}

int is_mac_version_15()
{
    if (@available(macOS 15.0, *)) {//This code runs on macOS 15 or later.
        return true;
    } else {
        return false;
    }
}

// Runtime check that works regardless of the SDK used to build, and is
// inherently true for the given major version and all later ones.
bool is_mac_os_at_least(int major)
{
    NSOperatingSystemVersion version = { major, 0, 0 };
    return [[NSProcessInfo processInfo] isOperatingSystemAtLeastVersion:version];
}
}; // namespace Slic3r
