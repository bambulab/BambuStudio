/**
 * \file BuildInfo.hpp
 * \brief Per-build stamp values that change on every CMake configure
 *
 * These values sit behind functions on purpose, so that a reconfigure recompiles one source file only.
 * Do not move them back into libslic3r_version.h: almost every source file includes that header.
 */
#pragma once

namespace Slic3r {

/**
 * \brief Time of the CMake configure that produced this binary, as "YYYYMMDD-HHMMSS"
 */
const char* build_time();

/**
 * \brief Git commit of the source tree at configure time
 *
 * \note Empty in public release builds. An incremental build without a reconfigure can report an older commit.
 */
const char* build_commit();

} // namespace Slic3r
