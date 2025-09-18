#pragma once

#include <string>

namespace Slic3r {
namespace GUI {

// UTF-8 safe: return the length of the valid prefix that does not exceed byte_len, avoiding truncating multi-byte characters
size_t utf8_safe_prefix_len(const std::string &text, size_t byte_len);

// Based on ImGui font measurement ellipsis truncation: return the result of adding "..." when the text width exceeds max_width
// Requires calling in a valid ImGui context (for CalcTextSize)
std::string ellipsize_text_imgui(const std::string &text, float max_width);

} // namespace GUI
} // namespace Slic3r


