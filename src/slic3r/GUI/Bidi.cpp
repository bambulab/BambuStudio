#include "Bidi.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>

namespace Slic3r {
namespace GUI {
namespace bidi {

// ---------------------------------------------------------------------------------------------
// UTF-8 <-> UTF-32
// ---------------------------------------------------------------------------------------------
namespace {

constexpr char32_t k_replacement = 0xFFFD;

// Decodes one code point starting at s[i] and advances i past it. Malformed input yields U+FFFD and
// consumes only the bytes that were valid up to the error, so decoding always makes progress.
char32_t decode_one(std::string_view s, std::size_t& i)
{
    const unsigned char b0 = static_cast<unsigned char>(s[i++]);
    if (b0 < 0x80)
        return b0;

    int      extra = 0;
    char32_t cp    = 0;
    char32_t min   = 0; // smallest value legal for this length (rejects overlong forms)
    if (b0 >= 0xC2 && b0 <= 0xDF) {
        extra = 1; cp = b0 & 0x1F; min = 0x80;
    } else if ((b0 & 0xF0) == 0xE0) {
        extra = 2; cp = b0 & 0x0F; min = 0x800;
    } else if (b0 >= 0xF0 && b0 <= 0xF4) {
        extra = 3; cp = b0 & 0x07; min = 0x10000;
    } else
        return k_replacement;

    for (int k = 0; k < extra; ++k) {
        if (i >= s.size() || (static_cast<unsigned char>(s[i]) & 0xC0) != 0x80)
            return k_replacement;
        cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3F);
        ++i;
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
        return k_replacement;
    return cp;
}

void append_utf8(std::string& out, char32_t c)
{
    if (c < 0x80) {
        out.push_back(static_cast<char>(c));
    } else if (c < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (c >> 6)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else if (c < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (c >> 12)));
        out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (c >> 18)));
        out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    }
}

// ---------------------------------------------------------------------------------------------
// Character classification (simplified Bidi_Class)
// ---------------------------------------------------------------------------------------------

// L = strong left-to-right, R = strong right-to-left (Hebrew), EN = European number,
// ES/ET/CS = number separators/terminators, NSM = combining mark, WS = whitespace, ON = other neutral.
enum class Cls : std::uint8_t { L, R, EN, ES, ET, CS, NSM, WS, ON };

// Explicit directional formatting characters: removed from the input (we run a fixed paragraph direction).
bool is_format_mark(char32_t c)
{
    return c == 0x061C                    // ALM
        || c == 0x200E || c == 0x200F     // LRM, RLM
        || (c >= 0x202A && c <= 0x202E)   // LRE, RLE, PDF, LRO, RLO
        || (c >= 0x2066 && c <= 0x2069);  // LRI, RLI, FSI, PDI
}

Cls classify(char32_t c)
{
    // Hebrew block. Points/cantillation (NSM) attach to the preceding letter (rule W1).
    if (c >= 0x0590 && c <= 0x05FF) {
        const bool point = (c >= 0x0591 && c <= 0x05BD) || c == 0x05BF || c == 0x05C1 || c == 0x05C2 ||
                           c == 0x05C4 || c == 0x05C5 || c == 0x05C7;
        return point ? Cls::NSM : Cls::R; // maqaf U+05BE, paseq U+05C0, sof pasuq U+05C3, geresh... are R
    }
    if (c >= 0xFB1D && c <= 0xFB4F) // Hebrew presentation forms
        return c == 0xFB1E ? Cls::NSM : Cls::R;

    if (c < 0x80) {
        if (c >= '0' && c <= '9')
            return Cls::EN;
        if (((c | 0x20) >= 'a') && ((c | 0x20) <= 'z'))
            return Cls::L;
        switch (c) {
        case ' ':
        case '\t': return Cls::WS;
        case '+':
        case '-': return Cls::ES;
        case ',':
        case '.':
        case '/':
        case ':': return Cls::CS;
        case '#':
        case '$':
        case '%': return Cls::ET;
        default: return Cls::ON; // other punctuation, C0 controls (ImGui colour/icon markers 0x01-0x1F), DEL
        }
    }

    if (c < 0x100) {
        if (c == 0xA0) return Cls::CS;                       // NBSP
        if ((c >= 0xA2 && c <= 0xA5) || c == 0xB0 || c == 0xB1) return Cls::ET; // currency, degree, plus-minus
        if (c < 0xC0) return (c == 0xAA || c == 0xB5 || c == 0xBA) ? Cls::L : Cls::ON; // C1 controls, Latin-1 punctuation
        return (c == 0xD7 || c == 0xF7) ? Cls::ON : Cls::L;  // multiplication / division sign
    }

    if (c >= 0x0300 && c <= 0x036F) return Cls::NSM;         // combining diacritics
    // ImGui icon glyphs live at U+0800..U+0850 (custom font rects, see imconfig.h) and in the
    // Private Use Area; both must behave as neutral objects, never as strong text.
    if (c >= 0x0800 && c <= 0x085F) return Cls::ON;
    if (c >= 0xE000 && c <= 0xF8FF) return Cls::ON;

    if (c >= 0x2000 && c <= 0x200A) return Cls::WS;          // en/em/thin... spaces
    if (c >= 0x200B && c <= 0x200D) return Cls::ON;          // ZWSP / ZWNJ / ZWJ
    if (c == 0x2028 || c == 0x2029) return Cls::WS;
    if (c >= 0x2010 && c <= 0x2027) return Cls::ON;          // dashes, quotes, bullets, ellipsis
    if (c == 0x202F) return Cls::CS;
    if (c >= 0x2030 && c <= 0x2034) return Cls::ET;          // per mille, prime
    if (c >= 0x2035 && c <= 0x206F) return Cls::ON;
    if (c >= 0x20A0 && c <= 0x20CF) return Cls::ET;          // currency symbols
    if (c == 0x2212) return Cls::ES;                         // minus sign
    if (c >= 0x2190 && c <= 0x2BFF) return Cls::ON;          // arrows, math operators, technical, shapes, dingbats
    if (c == 0x3000) return Cls::WS;
    if (c >= 0x3001 && c <= 0x303F) return Cls::ON;
    if (c >= 0xFF01 && c <= 0xFF0F) return Cls::ON;          // fullwidth punctuation
    if (c == 0xFFFD) return Cls::ON;
    return Cls::L;                                           // everything else is treated as LTR text
}

// L4: glyph mirroring for characters that are drawn at an odd (RTL) level.
char32_t mirror(char32_t c)
{
    switch (c) {
    case '(': return ')';
    case ')': return '(';
    case '[': return ']';
    case ']': return '[';
    case '{': return '}';
    case '}': return '{';
    case '<': return '>';
    case '>': return '<';
    // ImGui colour markers (ColorMarkerStart/End, imconfig.h) delimit a highlighted span exactly like a
    // bracket pair, so they must swap places together with the text when a run is reversed.
    case 0x02: return 0x03;
    case 0x03: return 0x02;
    case 0x00AB: return 0x00BB; // guillemets
    case 0x00BB: return 0x00AB;
    case 0x2039: return 0x203A; // single guillemets
    case 0x203A: return 0x2039;
    case 0x2264: return 0x2265; // less/greater-or-equal
    case 0x2265: return 0x2264;
    case 0x27E8: return 0x27E9; // mathematical angle brackets
    case 0x27E9: return 0x27E8;
    default: return c;
    }
}

// ---------------------------------------------------------------------------------------------
// Single line reordering (no '\n' inside)
// ---------------------------------------------------------------------------------------------
std::u32string reorder_line(const std::u32string& line, bool base_rtl)
{
    // Directional formatting characters are dropped up front.
    std::u32string cp;
    cp.reserve(line.size());
    for (char32_t c : line)
        if (!is_format_mark(c))
            cp.push_back(c);

    const std::size_t n = cp.size();
    if (n == 0)
        return cp;

    std::vector<Cls> orig(n);
    for (std::size_t i = 0; i < n; ++i)
        orig[i] = classify(cp[i]);
    std::vector<Cls> t = orig;

    const Cls sot = base_rtl ? Cls::R : Cls::L; // start/end-of-line type = paragraph embedding direction

    // W1: a combining mark takes the type of the character it follows (sot at line start).
    {
        Cls prev = sot;
        for (std::size_t i = 0; i < n; ++i) {
            if (t[i] == Cls::NSM)
                t[i] = prev;
            else
                prev = t[i];
        }
    }
    // W4: a single ES or CS between two European numbers becomes EN ("1,234.5", "15:30", "0.2").
    for (std::size_t i = 1; i + 1 < n; ++i)
        if ((t[i] == Cls::ES || t[i] == Cls::CS) && t[i - 1] == Cls::EN && t[i + 1] == Cls::EN)
            t[i] = Cls::EN;
    // W5: a sequence of ET adjacent to an EN becomes EN ("50%", "$5", "-3").
    for (std::size_t i = 0; i < n;) {
        if (t[i] != Cls::ET) { ++i; continue; }
        std::size_t j = i;
        while (j < n && t[j] == Cls::ET)
            ++j;
        if ((i > 0 && t[i - 1] == Cls::EN) || (j < n && t[j] == Cls::EN))
            for (std::size_t k = i; k < j; ++k)
                t[k] = Cls::EN;
        i = j;
    }
    // W6: remaining separators and terminators become neutral.
    for (std::size_t i = 0; i < n; ++i)
        if (t[i] == Cls::ES || t[i] == Cls::ET || t[i] == Cls::CS)
            t[i] = Cls::ON;
    // W7: EN preceded (through any neutrals) by strong L becomes L ("Lab X1C" stays one LTR run).
    {
        Cls strong = sot;
        for (std::size_t i = 0; i < n; ++i) {
            if (t[i] == Cls::L || t[i] == Cls::R)
                strong = t[i];
            else if (t[i] == Cls::EN && strong == Cls::L)
                t[i] = Cls::L;
        }
    }
    // N1/N2: a run of neutrals takes the direction of the surrounding strong text when both sides
    // agree (EN counts as R for this purpose), otherwise the embedding direction.
    // (N0, paired-bracket resolution, is intentionally omitted: for single RTL-base UI strings it
    // yields the same result as N1/N2.)
    {
        auto strong_of = [](Cls c) { return c == Cls::L ? Cls::L : Cls::R; };
        for (std::size_t i = 0; i < n;) {
            if (t[i] != Cls::ON && t[i] != Cls::WS) { ++i; continue; }
            std::size_t j = i;
            while (j < n && (t[j] == Cls::ON || t[j] == Cls::WS))
                ++j;
            const Cls before = (i == 0) ? sot : strong_of(t[i - 1]);
            const Cls after  = (j == n) ? sot : strong_of(t[j]);
            const Cls res    = (before == after) ? before : sot;
            for (std::size_t k = i; k < j; ++k)
                t[k] = res;
            i = j;
        }
    }

    // I1/I2: resolved embedding levels. RTL paragraph: R=1, L=2, EN=2. LTR paragraph: L=0, R=1, EN=2.
    const std::uint8_t base_level = base_rtl ? 1 : 0;
    std::vector<std::uint8_t> lvl(n);
    for (std::size_t i = 0; i < n; ++i) {
        switch (t[i]) {
        case Cls::R:  lvl[i] = 1; break;
        case Cls::EN: lvl[i] = 2; break;
        default:      lvl[i] = base_rtl ? 2 : 0; break; // L
        }
    }
    // L1: trailing whitespace is reset to the paragraph level so it does not jump to the wrong edge.
    for (std::size_t i = n; i > 0 && orig[i - 1] == Cls::WS; --i)
        lvl[i - 1] = base_level;

    // L2: from the highest level down to the lowest odd level, reverse every contiguous run at that level or higher.
    std::vector<std::size_t> order(n);
    std::iota(order.begin(), order.end(), std::size_t(0));
    {
        std::uint8_t max_level = 0, min_odd = 255;
        for (std::uint8_t l : lvl) {
            max_level = std::max(max_level, l);
            if (l & 1)
                min_odd = std::min(min_odd, l);
        }
        if (min_odd != 255) {
            for (int level = max_level; level >= int(min_odd); --level) {
                std::size_t i = 0;
                while (i < n) {
                    if (lvl[order[i]] < level) { ++i; continue; }
                    std::size_t j = i;
                    while (j < n && lvl[order[j]] >= level)
                        ++j;
                    std::reverse(order.begin() + std::ptrdiff_t(i), order.begin() + std::ptrdiff_t(j));
                    i = j;
                }
            }
        }
    }

    // For every combining mark remember the base character it belongs to.
    std::vector<std::size_t> base_of(n, n);
    {
        std::size_t last_base = n;
        for (std::size_t i = 0; i < n; ++i) {
            if (orig[i] != Cls::NSM)
                last_base = i;
            base_of[i] = last_base;
        }
    }

    // Emit in visual order. ImGui draws a mark after its base with zero advance, so a mark cluster
    // that was reversed (mark ahead of its base) is flipped back to base + marks. L4: mirror brackets
    // at odd levels.
    std::u32string out;
    out.reserve(n);
    auto emit = [&](std::size_t idx) {
        char32_t c = cp[idx];
        if ((lvl[idx] & 1) && orig[idx] != Cls::NSM)
            c = mirror(c);
        out.push_back(c);
    };
    for (std::size_t k = 0; k < n;) {
        if (orig[order[k]] == Cls::NSM) {
            std::size_t m = k;
            while (m < n && orig[order[m]] == Cls::NSM)
                ++m;
            if (m < n && base_of[order[k]] == order[m]) {
                emit(order[m]);
                for (std::size_t q = m; q-- > k;)
                    emit(order[q]);
                k = m + 1;
                continue;
            }
        }
        emit(order[k]);
        ++k;
    }
    return out;
}

// Advances past one code point plus any combining marks that follow it (a "cluster").
std::size_t next_boundary(std::string_view s, std::size_t i)
{
    decode_one(s, i);
    while (i < s.size()) {
        std::size_t j = i;
        if (classify(decode_one(s, j)) != Cls::NSM)
            break;
        i = j;
    }
    return i;
}

void wrap_paragraph(std::string_view                              p,
                    float                                         width,
                    const std::function<float(std::string_view)>& measure,
                    std::vector<std::string>&                     out)
{
    if (width <= 0.f || !measure) {
        out.push_back(visual(p));
        return;
    }

    std::size_t line_begin = 0, line_end = 0, i = 0;
    bool        any = false;
    auto        emit = [&](std::size_t b, std::size_t e) {
        out.push_back(visual(p.substr(b, e - b)));
        any = true;
    };

    while (i < p.size()) {
        std::size_t ws = i;
        while (ws < p.size() && p[ws] == ' ')
            ++ws;
        std::size_t we = ws;
        while (we < p.size() && p[we] != ' ')
            ++we;
        if (ws == we)
            break; // only trailing spaces remain

        // The word does not fit behind the current content: close the line (spaces at the break are dropped).
        if (line_end > line_begin && measure(p.substr(line_begin, we - line_begin)) > width) {
            emit(line_begin, line_end);
            line_begin = line_end = ws;
        }
        // Single word wider than a whole line: break it at cluster boundaries.
        if (line_end == line_begin && measure(p.substr(ws, we - ws)) > width) {
            std::size_t b = ws;
            for (;;) {
                std::size_t e = next_boundary(p, b); // at least one cluster per chunk guarantees progress
                while (e < we) {
                    const std::size_t nx = next_boundary(p, e);
                    if (measure(p.substr(b, nx - b)) > width)
                        break;
                    e = nx;
                }
                if (e >= we)
                    break; // the remainder fits on the current line
                emit(b, e);
                b = e;
            }
            line_begin = b;
        }
        line_end = we;
        i        = we;
    }

    if (line_end > line_begin)
        emit(line_begin, line_end);
    else if (!any)
        out.emplace_back(); // empty paragraph -> one empty line
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------------------------
std::u32string utf8_to_utf32(std::string_view utf8)
{
    std::u32string out;
    out.reserve(utf8.size());
    std::size_t i = 0;
    while (i < utf8.size())
        out.push_back(decode_one(utf8, i));
    return out;
}

std::string utf32_to_utf8(std::u32string_view utf32)
{
    std::string out;
    out.reserve(utf32.size() * 2);
    for (char32_t c : utf32)
        append_utf8(out, c);
    return out;
}

bool has_rtl(std::string_view s)
{
    const std::size_t n = s.size();
    for (std::size_t i = 0; i < n; ++i) {
        const unsigned char b = static_cast<unsigned char>(s[i]);
        if (b < 0x80)
            continue; // hot path: ASCII
        if (b == 0xD7) // U+05C0..U+05FF
            return true;
        if (b == 0xD6) { // U+0580..U+05BF; U+0580..U+058F is Armenian, Hebrew starts at U+0590 = D6 90
            if (i + 1 < n && static_cast<unsigned char>(s[i + 1]) >= 0x90)
                return true;
        } else if (b == 0xEF && i + 2 < n) { // U+FB1D..U+FB4F: EF AC 9D .. EF AD 8F
            const unsigned char b1 = static_cast<unsigned char>(s[i + 1]);
            const unsigned char b2 = static_cast<unsigned char>(s[i + 2]);
            if ((b1 == 0xAC && b2 >= 0x9D) || (b1 == 0xAD && b2 <= 0x8F))
                return true;
        }
    }
    return false;
}

std::string visual(std::string_view s, bool base_rtl)
{
    if (!has_rtl(s))
        return std::string(s); // zero-cost path for every other language

    std::string out;
    out.reserve(s.size());
    std::size_t pos = 0;
    for (;;) {
        const std::size_t nl   = s.find('\n', pos);
        std::string_view  line = s.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        const bool        cr   = !line.empty() && line.back() == '\r'; // keep CR at the end of the line
        if (cr)
            line.remove_suffix(1);
        // Lines without Hebrew (e.g. a Latin-only line in a Hebrew text) are not touched.
        if (has_rtl(line))
            out += utf32_to_utf8(reorder_line(utf8_to_utf32(line), base_rtl));
        else
            out.append(line.data(), line.size());
        if (cr)
            out.push_back('\r');
        if (nl == std::string_view::npos)
            break;
        out.push_back('\n');
        pos = nl + 1;
    }
    return out;
}

std::string visual_label(std::string_view label, bool base_rtl)
{
    const std::size_t p = label.find("##");
    if (p == std::string_view::npos)
        return visual(label, base_rtl);
    std::string out = visual(label.substr(0, p), base_rtl);
    out.append(label.data() + p, label.size() - p); // ImGui ID part must stay byte-identical
    return out;
}

std::vector<std::string> wrap_and_reorder(std::string_view                              text,
                                          float                                         wrap_width,
                                          const std::function<float(std::string_view)>& measure)
{
    std::vector<std::string> lines;
    std::size_t              pos = 0;
    for (;;) {
        const std::size_t nl   = text.find('\n', pos);
        std::string_view  para = text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        if (!para.empty() && para.back() == '\r')
            para.remove_suffix(1);
        wrap_paragraph(para, wrap_width, measure, lines);
        if (nl == std::string_view::npos)
            break;
        pos = nl + 1;
    }
    return lines;
}

} // namespace bidi
} // namespace GUI
} // namespace Slic3r
