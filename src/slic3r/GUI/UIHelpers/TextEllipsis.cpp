#include "TextEllipsis.hpp"
#include <imgui/imgui.h>

namespace Slic3r {
namespace GUI {

size_t utf8_safe_prefix_len(const std::string &text, size_t byte_len)
{
    if (byte_len >= text.size()) return text.size();
    while (byte_len > 0) {
        unsigned char c = static_cast<unsigned char>(text[byte_len]);
        if ((c & 0xC0) != 0x80) break; // If not a continuation byte, stop
        --byte_len;
    }
    return byte_len;
}

std::string ellipsize_text_imgui(const std::string &text, float max_width)
{
    if (text.empty()) return text;
    const ImVec2 full_sz = ImGui::CalcTextSize(text.c_str());
    if (full_sz.x <= max_width) return text;

    const char *ellipsis = "...";
    const float ellipsis_w = ImGui::CalcTextSize(ellipsis).x;
    if (max_width <= ellipsis_w)
        return std::string(ellipsis);

    size_t low = 0, high = text.size();
    size_t best = 0;
    while (low <= high) {
        size_t mid = (low + high) / 2;
        size_t safe = utf8_safe_prefix_len(text, mid);
        std::string candidate = text.substr(0, safe);
        const float w = ImGui::CalcTextSize(candidate.c_str()).x + ellipsis_w;
        if (w <= max_width) {
            best = safe;
            low = mid + 1;
        } else {
            if (mid == 0) break;
            high = mid - 1;
        }
    }
    if (best == 0) return std::string(ellipsis);
    return text.substr(0, best) + ellipsis;
}

} // namespace GUI
} // namespace Slic3r


