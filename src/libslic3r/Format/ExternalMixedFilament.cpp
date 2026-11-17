#include "ExternalMixedFilament.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <map>
#include <sstream>

#include <boost/log/trivial.hpp>

namespace Slic3r {

namespace {

// Distribution modes of the source format; 1 (same-layer pointillism) is folded into the other two.
constexpr int k_mode_layer_cycle = 0;
constexpr int k_mode_pointillism = 1;
constexpr int k_mode_simple      = 2;

struct ExternalMixedRow
{
    unsigned int a           = 0;
    unsigned int b           = 0;
    bool         enabled     = true;
    bool         deleted     = false;
    int          mix_b       = 50;
    int          mode        = k_mode_simple;
    std::string  ids;
    std::string  weights;
    std::string  pattern;
    bool         lossy       = false;
};

std::string trim_copy(const std::string &s)
{
    size_t lo = 0, hi = s.size();
    while (lo < hi && std::isspace(static_cast<unsigned char>(s[lo])))
        ++lo;
    while (hi > lo && std::isspace(static_cast<unsigned char>(s[hi - 1])))
        --hi;
    return s.substr(lo, hi - lo);
}

bool parse_int_token(const std::string &tok, int &out)
{
    const std::string t = trim_copy(tok);
    if (t.empty())
        return false;
    char *end = nullptr;
    const long v = std::strtol(t.c_str(), &end, 10);
    if (end == nullptr || *end != '\0')
        return false;
    out = int(v);
    return true;
}

// "123" lists single-digit ids, "1/10/12" lists multi-digit ids. Duplicates are dropped.
std::vector<unsigned int> decode_component_ids(const std::string &s)
{
    std::vector<unsigned int> ids;
    auto push = [&ids](unsigned long id) {
        if (id >= 1 && std::find(ids.begin(), ids.end(), unsigned(id)) == ids.end())
            ids.push_back(unsigned(id));
    };
    if (s.find('/') != std::string::npos) {
        std::string token;
        for (char c : s + "/") {
            if (c == '/') {
                if (!token.empty())
                    push(std::strtoul(token.c_str(), nullptr, 10));
                token.clear();
            } else {
                token.push_back(c);
            }
        }
    } else {
        for (char c : s)
            if (c >= '1' && c <= '9')
                push(unsigned(c - '0'));
    }
    return ids;
}

std::vector<int> parse_weight_tokens(const std::string &s)
{
    std::vector<int> out;
    std::string token;
    for (char c : s + "/") {
        if (c >= '0' && c <= '9') {
            token.push_back(c);
        } else if (!token.empty()) {
            out.push_back(std::max(0, std::atoi(token.c_str())));
            token.clear();
        }
    }
    return out;
}

// Pattern tokens: '1'..'9' one per char, "[NN]" for multi-digit ids. ',' only separates
// per-perimeter groups, which are flattened here.
std::vector<std::string> tokenize_pattern(const std::string &pattern)
{
    std::vector<std::string> tokens;
    for (size_t i = 0; i < pattern.size(); ++i) {
        const char c = pattern[i];
        if (c >= '1' && c <= '9') {
            tokens.emplace_back(1, c);
        } else if (c == '[') {
            size_t j = i + 1;
            while (j < pattern.size() && pattern[j] >= '0' && pattern[j] <= '9')
                ++j;
            if (j > i + 1 && j < pattern.size() && pattern[j] == ']') {
                tokens.emplace_back(pattern.substr(i + 1, j - i - 1));
                i = j;
            }
        }
    }
    return tokens;
}

// A pattern group only holds '1'..'9' and "[NN]"; anything else is an unknown tag.
bool is_pattern_token(const std::string &tok)
{
    if (tok.empty())
        return false;
    for (size_t i = 0; i < tok.size(); ++i) {
        if (tok[i] >= '1' && tok[i] <= '9')
            continue;
        if (tok[i] != '[')
            return false;
        size_t j = i + 1;
        while (j < tok.size() && tok[j] >= '0' && tok[j] <= '9')
            ++j;
        if (j == i + 1 || j >= tok.size() || tok[j] != ']')
            return false;
        i = j;
    }
    return true;
}

// '1' / '2' stand for component a / b, any other token is a direct physical id.
unsigned int pattern_token_to_physical(const std::string &token, const ExternalMixedRow &row)
{
    if (token == "1")
        return row.a;
    if (token == "2")
        return row.b;
    return unsigned(std::strtoul(token.c_str(), nullptr, 10));
}

bool parse_row(const std::string &row_str, ExternalMixedRow &row)
{
    std::vector<std::string> tokens;
    {
        std::stringstream ss(row_str);
        std::string       token;
        while (std::getline(ss, token, ','))
            tokens.emplace_back(trim_copy(token));
    }
    if (tokens.size() < 4)
        return false;

    int values[5] = {0, 0, 1, 1, 50};
    if (tokens.size() == 4) {
        // Legacy: a,b,enabled,mix
        if (!parse_int_token(tokens[0], values[0]) || !parse_int_token(tokens[1], values[1]) ||
            !parse_int_token(tokens[2], values[2]) || !parse_int_token(tokens[3], values[4]))
            return false;
    } else {
        for (size_t i = 0; i < 5; ++i)
            if (!parse_int_token(tokens[i], values[i]))
                return false;
    }
    if (values[0] <= 0 || values[1] <= 0)
        return false;

    row.a       = unsigned(values[0]);
    row.b       = unsigned(values[1]);
    row.enabled = values[2] != 0;
    row.mix_b   = std::clamp(values[4], 0, 100);

    size_t token_idx = 5;
    std::vector<std::string> pattern_tokens;
    if (tokens.size() >= 6) {
        // tokens[5] is either the legacy pointillism flag, a legacy pattern, or the first tagged token.
        const std::string &legacy = tokens[5];
        if (legacy == "0" || legacy == "1") {
            token_idx = 6;
        } else if (is_pattern_token(legacy)) {
            pattern_tokens.push_back(legacy);
            token_idx = 6;
        } else {
            token_idx = 5;
        }
    }

    for (size_t i = token_idx; i < tokens.size(); ++i) {
        const std::string &tok = tokens[i];
        if (tok.empty())
            continue;
        const char tag = char(std::tolower(static_cast<unsigned char>(tok[0])));
        if (tag == 'g') {
            row.ids = tok.substr(1);
        } else if (tag == 'w') {
            row.weights = tok.substr(1);
        } else if (tag == 'm') {
            int mode = row.mode;
            if (parse_int_token(tok.substr(1), mode))
                row.mode = std::clamp(mode, k_mode_layer_cycle, k_mode_simple);
        } else if (tag == 'd') {
            int deleted = 0;
            if (parse_int_token(tok.substr(1), deleted))
                row.deleted = deleted != 0;
        } else if (tag == 'z') {
            int sublayers = 0;
            if (parse_int_token(tok.substr(1), sublayers) && sublayers > 0)
                row.lossy = true;
        } else if (tag == 'x' && tok.size() >= 3 && (tok[1] == 'a' || tok[1] == 'b' || tok[1] == 'A' || tok[1] == 'B')) {
            if (std::atof(tok.c_str() + 2) != 0.)
                row.lossy = true;
        } else if (tag == 'r') {
            // Z gradient: "r<flag>/<start>/<end>".
            if (tok.size() >= 2 && tok[1] != '0')
                row.lossy = true;
        } else if (tag == 'o' || tag == 'u' || (tag == 'c' && tok.size() >= 3 && (tok[1] == 'm' || tok[1] == 'M'))) {
            // Row identity / UI state only.
        } else if (is_pattern_token(tok)) {
            pattern_tokens.push_back(tok);
        } else {
            row.lossy = true;
        }
    }

    for (size_t i = 0; i < pattern_tokens.size(); ++i)
        row.pattern += (i == 0 ? "" : ",") + pattern_tokens[i];

    if (row.mode == k_mode_pointillism) {
        row.mode  = decode_component_ids(row.ids).size() >= 3 ? k_mode_layer_cycle : k_mode_simple;
        row.lossy = true;
    }
    return true;
}

// Mirrors the source's own load-time checks: such rows never become a virtual filament.
bool row_references_valid(const ExternalMixedRow &row, size_t num_physical)
{
    if (row.a > num_physical || row.b > num_physical || row.a == row.b)
        return false;
    for (unsigned int id : decode_component_ids(row.ids))
        if (id > num_physical)
            return false;
    for (const std::string &token : tokenize_pattern(row.pattern))
        if (token != "1" && token != "2" && std::strtoul(token.c_str(), nullptr, 10) > num_physical)
            return false;
    return true;
}

// Largest remainder rounding to 100, then lifts zero entries to 1 since our ratios must be positive.
std::vector<int> to_percentages(const std::vector<double> &weights)
{
    double sum = 0.;
    for (double w : weights)
        sum += w;
    std::vector<int> out(weights.size(), 0);
    if (weights.empty() || sum <= 0.)
        return out;

    std::vector<double> remainders(weights.size(), 0.);
    int assigned = 0;
    for (size_t i = 0; i < weights.size(); ++i) {
        const double exact = 100. * weights[i] / sum;
        out[i]             = int(std::floor(exact));
        remainders[i]      = exact - out[i];
        assigned += out[i];
    }
    for (int missing = 100 - assigned; missing > 0; --missing) {
        const size_t best = size_t(std::max_element(remainders.begin(), remainders.end()) - remainders.begin());
        ++out[best];
        remainders[best] = -1.;
    }
    for (int &v : out) {
        if (v > 0)
            continue;
        auto largest = std::max_element(out.begin(), out.end());
        if (*largest <= 1)
            break;
        --*largest;
        v = 1;
    }
    return out;
}

ExternalMixedSlot make_slot(const std::vector<std::pair<unsigned int, double>> &weighted)
{
    ExternalMixedSlot slot;
    std::vector<double> weights;
    for (const auto &[id, w] : weighted) {
        slot.components.push_back(id);
        weights.push_back(w);
    }
    slot.ratios = to_percentages(weights);
    return slot;
}

// Same precedence as the source's per-layer resolver: manual pattern, then multi-color
// weights, then the plain two-component blend.
ExternalMixedSlot convert_row(const ExternalMixedRow &row, size_t num_physical)
{
    const std::vector<std::string> pattern_tokens = tokenize_pattern(row.pattern);
    if (!pattern_tokens.empty()) {
        std::map<unsigned int, double> counts;
        for (const std::string &token : pattern_tokens) {
            const unsigned int id = pattern_token_to_physical(token, row);
            if (id >= 1 && id <= num_physical)
                counts[id] += 1.;
        }
        if (counts.size() >= 2)
            return make_slot({counts.begin(), counts.end()});
    }

    const std::vector<unsigned int> ids = decode_component_ids(row.ids);
    if (row.mode != k_mode_simple && ids.size() >= 3) {
        std::vector<int> weights = parse_weight_tokens(row.weights);
        if (weights.size() != ids.size())
            weights.assign(ids.size(), 1);
        std::vector<std::pair<unsigned int, double>> weighted;
        for (size_t i = 0; i < ids.size(); ++i)
            if (weights[i] > 0)
                weighted.emplace_back(ids[i], double(weights[i]));
        if (weighted.empty())
            for (unsigned int id : ids)
                weighted.emplace_back(id, 1.);
        if (weighted.size() >= 2)
            return make_slot(weighted);
    }

    return make_slot({{row.a, double(100 - row.mix_b)}, {row.b, double(row.mix_b)}});
}

} // namespace

std::vector<ExternalMixedSlot> parse_external_mixed_definitions(const std::string &definitions, size_t num_physical)
{
    std::vector<ExternalMixedSlot> slots;
    if (definitions.empty() || num_physical < 2)
        return slots;

    std::stringstream all(definitions);
    std::string       row_str;
    size_t            row_index = 0;
    while (std::getline(all, row_str, ';')) {
        if (trim_copy(row_str).empty())
            continue;
        ++row_index;
        ExternalMixedRow row;
        if (!parse_row(row_str, row) || !row_references_valid(row, num_physical)) {
            BOOST_LOG_TRIVIAL(warning) << "external mixed filament: skip invalid row " << row_index << ": " << row_str;
            continue;
        }
        if (row.deleted || !row.enabled) {
            BOOST_LOG_TRIVIAL(info) << "external mixed filament: skip disabled row " << row_index;
            continue;
        }
        if (row.lossy || !row.pattern.empty())
            BOOST_LOG_TRIVIAL(warning) << "external mixed filament: row " << row_index
                                       << " uses settings without an equivalent, imported as a static ratio: " << row_str;

        ExternalMixedSlot slot = convert_row(row, num_physical);
        BOOST_LOG_TRIVIAL(info) << "external mixed filament: row " << row_index << " -> virtual filament "
                                << (num_physical + slots.size() + 1) << ", components " << slot.components.size();
        slots.push_back(std::move(slot));
    }
    return slots;
}

} // namespace Slic3r
