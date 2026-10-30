#pragma once

#include <string_view>

/**
 * \brief Field names for KV in log records
 *
 * Use these instead of string literals so the same value is always logged under the same name.
 * Values of sensitive fields are masked in public builds; see KV.
 */
namespace Slic3r::log_key {

// Sensitive: masked in public builds
inline constexpr std::string_view dev_id      = "dev_id";
inline constexpr std::string_view sn          = "sn";
inline constexpr std::string_view dev_ip      = "dev_ip";
inline constexpr std::string_view dev_name    = "dev_name";
inline constexpr std::string_view uid         = "uid";
inline constexpr std::string_view access_code = "access_code";
inline constexpr std::string_view token       = "token";
inline constexpr std::string_view password    = "password";
inline constexpr std::string_view authkey     = "authkey";
inline constexpr std::string_view ttcode_enc  = "ttcode_enc";
inline constexpr std::string_view url         = "url";
inline constexpr std::string_view region      = "region";
inline constexpr std::string_view model_id    = "model_id";

// Not sensitive
inline constexpr std::string_view seq         = "seq";
inline constexpr std::string_view ret         = "ret";
inline constexpr std::string_view task_id     = "task_id";
inline constexpr std::string_view subtask_id  = "subtask_id";
inline constexpr std::string_view job_id      = "job_id";
inline constexpr std::string_view plate       = "plate";
inline constexpr std::string_view plate_id    = "plate_id";
inline constexpr std::string_view network_agent = "network_agent";
inline constexpr std::string_view http_code   = "http_code";
inline constexpr std::string_view filament_id = "filament_id";
inline constexpr std::string_view setting_id  = "setting_id";
inline constexpr std::string_view base_id     = "base_id";
inline constexpr std::string_view ams_id      = "ams_id";
inline constexpr std::string_view slot_id     = "slot_id";
inline constexpr std::string_view tray_id     = "tray_id";
inline constexpr std::string_view extruder_id = "extruder_id";
inline constexpr std::string_view nozzle_diameter = "nozzle_diameter";
inline constexpr std::string_view file_count  = "file_count";
inline constexpr std::string_view thumbnail_count = "thumbnail_count";

// Environment, logged once at startup and when OpenGL is set up
inline constexpr std::string_view app_version  = "app_version";
inline constexpr std::string_view build_time   = "build_time";
inline constexpr std::string_view build_commit = "build_commit";
inline constexpr std::string_view gl_version   = "gl_version";
inline constexpr std::string_view glsl_version = "glsl_version";
inline constexpr std::string_view gpu_vendor   = "gpu_vendor";
inline constexpr std::string_view gpu          = "gpu";

} // namespace Slic3r::log_key
