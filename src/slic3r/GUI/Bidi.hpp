#ifndef slic3r_GUI_Bidi_hpp_
#define slic3r_GUI_Bidi_hpp_

// Minimal, dependency-free, Hebrew-restricted implementation of the Unicode Bidirectional
// Algorithm (UAX #9) for the *non-shaping* text renderer used by ImGui.
//
// Why restricted: Hebrew needs no contextual shaping (unlike Arabic), so reordering of the
// logical string into visual (left-to-right drawing) order is all that is required. Only the
// rules that matter for short UI strings are implemented:
//   W1 (NSM), W4/W5/W6 (separators / terminators), W7 (EN after L), N1/N2 (neutrals),
//   I1/I2 (levels), L1 (trailing whitespace), L2 (reverse), L4 (bracket mirroring).
// Not implemented: explicit embeddings/overrides/isolates (stripped from input), N0 paired
// bracket resolution, Arabic classes (AL/AN). Input that contains no Hebrew is returned unchanged.
//
// The module has no wxWidgets / ImGui dependency so it can be unit tested standalone.

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Slic3r {
namespace GUI {
namespace bidi {

// UTF-8 <-> UTF-32 helpers. Invalid UTF-8 bytes decode to U+FFFD (one per offending byte).
std::u32string utf8_to_utf32(std::string_view utf8);
std::string    utf32_to_utf8(std::u32string_view utf32);

// True if the string contains at least one Hebrew code point (U+0590-U+05FF, U+FB1D-U+FB4F).
// Allocation free; this is the fast path guard used before any reordering.
bool has_rtl(std::string_view utf8);

// Converts a logical-order UTF-8 string into visual order (the order in which glyphs are
// drawn left to right by a non-bidi-aware renderer).
//  - '\n' separates paragraphs/lines, each reordered independently.
//  - Lines without Hebrew are left untouched, so Latin-only text is returned unchanged.
//  - base_rtl selects the paragraph embedding direction (true = RTL, the normal case for a Hebrew UI).
//  - Explicit directional formatting characters (LRM, RLM, ALM, LRE..PDF, LRI..PDI) are removed.
std::string visual(std::string_view logical_utf8, bool base_rtl = true);

// Same as visual(), but understands ImGui labels of the form "visible text##id" / "text###id":
// only the part before the first "##" is reordered, the ID suffix is kept byte-for-byte
// so ImGui widget IDs stay stable.
std::string visual_label(std::string_view label, bool base_rtl = true);

// Greedy word wrap on the *logical* string (words are separated by ' '), then reorder every
// resulting line. Wrapping must happen before reordering, otherwise the first words of the
// paragraph would end up on the wrong (last) line. '\n' forces a line break.
// `measure` returns the rendered width of a string (e.g. via ImGui::CalcTextSize); it is only
// ever called on logical-order substrings. wrap_width <= 0 disables wrapping.
// Over-long single words are broken at code point boundaries (never before a combining mark).
// Returned strings are in visual order, ready to be drawn left to right.
std::vector<std::string> wrap_and_reorder(std::string_view                                  logical_utf8,
                                          float                                             wrap_width,
                                          const std::function<float(std::string_view)>&     measure);

// Lightweight holder for "visual form of a label, only converted when needed". Non-Hebrew strings
// are not copied: c_str() returns the original pointer (zero allocation, zero cost for other
// languages). Typical use:   ImGui::Button(bidi::vis(label).c_str());
// The original string must outlive the object (it normally does: the object is a temporary).
class VisualText
{
public:
    explicit VisualText(const char* s) : m_src(s ? s : "")
    {
        if (has_rtl(m_src)) {
            m_buf     = visual_label(m_src);
            m_changed = true;
        }
    }
    explicit VisualText(const std::string& s) : VisualText(s.c_str()) {}

    const char*        c_str() const { return m_changed ? m_buf.c_str() : m_src; }
    std::string_view   view() const { return m_changed ? std::string_view(m_buf) : std::string_view(m_src); }
    bool               changed() const { return m_changed; }

private:
    const char* m_src;
    std::string m_buf;
    bool        m_changed{false};
};

inline VisualText vis(const char* s) { return VisualText(s); }
inline VisualText vis(const std::string& s) { return VisualText(s); }

} // namespace bidi
} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_Bidi_hpp_
