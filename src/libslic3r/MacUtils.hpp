#ifndef __MAC_UTILS_H
#define __MAC_UTILS_H

namespace Slic3r {

bool is_macos_support_boost_add_file_log();
int  is_mac_version_15();

// Returns true when running on macOS `major` or any later version.
bool is_mac_os_at_least(int major);
}

#endif
