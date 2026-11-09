#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "nlohmann/json_fwd.hpp"

/**
 * \brief Masks sensitive values before they are written to logs
 *
 * Masking happens only in builds released to the public; other builds return the input unchanged.
 */
namespace Slic3r::Desensitize {

/** \brief Mask a device id / serial number, keeping the first and last 3 characters */
std::string dev_id(const std::string& str);
/** \brief Mask the first two segments of an IPv4 address */
std::string dev_ip(const std::string& str);
/** \brief Mask a device name, keeping at most the last 3 characters */
std::string dev_name(const std::string& str);
/**
 * \brief Dump a json with sensitive fields masked, recursing into nested objects and arrays
 *
 * Never throws: invalid UTF-8 in strings is replaced, and a json that cannot be processed gives a
 * placeholder instead of a partly masked dump.
 *
 * \param indent as in nlohmann::json::dump; -1 gives a single line
 */
std::string json_log(const nlohmann::json& json, int indent = 1);
/** \brief Mask a user id, keeping the last 2 characters */
std::string user_id(const std::string& uid);
/** \brief Mask the whole value, for credentials such as tokens and access codes */
std::string mask_all(const std::string& str);
/**
 * \brief Mask a value by its field name, using the table of registered sensitive fields
 *
 * The name must match a registered field exactly, such as a log_key constant.
 *
 * \return the masked value, or nullopt if the field is not registered
 */
std::optional<std::string> mask_by_key(std::string_view key, const std::string& value);

} // namespace Slic3r::Desensitize
