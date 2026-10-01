#include "wgtFilaManagerFeature.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace Slic3r { namespace GUI {

namespace {

std::string normalize_flag(const std::string& value)
{
    std::string normalized(value);
    normalized.erase(normalized.begin(), std::find_if(normalized.begin(), normalized.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    normalized.erase(std::find_if(normalized.rbegin(), normalized.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), normalized.end());
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    return normalized;
}

bool is_truthy_flag(const std::string& value)
{
    return value == "1" || value == "true" || value == "on" || value == "yes";
}

bool is_falsey_flag(const std::string& value)
{
    return value == "0" || value == "false" || value == "off" || value == "no";
}

} // namespace

bool is_fila_manager_disabled_by_config(const std::string& enabled_value, bool is_macos)
{
    const std::string normalized = normalize_flag(enabled_value);
    if (normalized.empty())
        return is_macos;
    if (is_falsey_flag(normalized))
        return true;
    return !is_truthy_flag(normalized);
}

}} // namespace Slic3r::GUI
