#pragma once

#include <string>

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
 * \return indented dump of the masked json, or an empty string if the json cannot be processed
 */
std::string json_log(const nlohmann::json& json);
/** \brief Mask a user id, keeping the last 2 characters */
std::string user_id(const std::string& uid);
/** \brief Mask the whole value, for credentials such as tokens and access codes */
std::string mask_all(const std::string& str);

} // namespace Slic3r::Desensitize
