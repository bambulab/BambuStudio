#include "wgtFilaManagerCsv.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <regex>
#include <sstream>

namespace Slic3r { namespace GUI {

namespace {

// Only fields that persist through the cloud Update endpoint. Fields the
// backend silently drops are kept out so export can't imply they are editable.
const std::vector<std::string> kColumns = {
    "id", "brand", "material_type", "series", "color_code",
    "net_weight", "note", "setting_id", "deleted",
};

// Export-only reference columns; never read on import.
const std::vector<std::string> kReadonlyColumns = {
    "color_name", "diameter", "initial_weight", "spool_weight",
    "remain_percent", "status", "favorite",
};

std::string trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool is_truthy_flag(const std::string& value)
{
    const std::string v = lower(trim(value));
    return v == "true" || v == "1" || v == "yes" || v == "y" || v == "x";
}

// 1000.0 -> "1000", 1.75 -> "1.75": no trailing zeros, as a spreadsheet shows.
std::string num(double value)
{
    if (!std::isfinite(value)) return "";
    std::ostringstream os;
    os << value;
    return os.str();
}

std::string csv_cell(const std::string& value)
{
    if (value.find_first_of(",\"\r\n") != std::string::npos) {
        std::string out = "\"";
        for (char c : value) {
            if (c == '"') out += "\"\"";
            else out += c;
        }
        out += "\"";
        return out;
    }
    return value;
}

const std::regex kHexRe("^#?[0-9a-fA-F]{6}([0-9a-fA-F]{2})?$");

std::string spool_label(const FilamentSpool& s)
{
    // Prefer series ("PLA+") over the generic material_type ("PLA"); not both.
    const std::string& type = s.series.empty() ? s.material_type : s.series;
    std::string label;
    for (const std::string& part : {s.brand, type, s.color_name}) {
        if (part.empty()) continue;
        if (!label.empty()) label += " ";
        label += part;
    }
    label = trim(label);
    if (label.empty()) label = s.spool_id;
    if (!s.note.empty()) label += " \xE2\x80\x94 " + s.note; // em dash separator
    return label;
}

// Validate and project a row into a JSON patch; false + `error` if invalid.
bool row_to_payload(const CsvRow& row, nlohmann::json& payload, std::string& error)
{
    const std::string brand = row.get("brand");
    if (brand.empty()) { error = "Missing required field: brand"; return false; }
    const std::string material = row.get("material_type");
    if (material.empty()) { error = "Missing required field: material_type"; return false; }

    const std::string color_raw = row.get("color_code");
    if (!color_raw.empty() && !std::regex_match(color_raw, kHexRe)) {
        error = "Invalid color_code \"" + color_raw + "\" (expected 6- or 8-digit hex)";
        return false;
    }

    payload = nlohmann::json::object();
    payload["brand"]         = brand;
    payload["material_type"] = material;
    payload["series"]        = row.get("series");
    // Normalize color_code to the stored "#RRGGBB(AA)" form.
    payload["color_code"]    = color_raw.empty()
                                   ? std::string()
                                   : "#" + (color_raw[0] == '#' ? color_raw.substr(1) : color_raw);
    payload["note"]          = row.get("note");

    const std::string setting_id = row.get("setting_id");
    if (!setting_id.empty()) payload["setting_id"] = setting_id;

    const std::string net_weight = row.get("net_weight");
    if (!net_weight.empty()) {
        try {
            size_t consumed = 0;
            const double n = std::stod(net_weight, &consumed);
            if (consumed != net_weight.size() || !std::isfinite(n)) throw std::invalid_argument("nan");
            payload["net_weight"] = n;
        } catch (...) {
            error = "Invalid number for net_weight: \"" + net_weight + "\"";
            return false;
        }
    }
    return true;
}

// Logical cell values for one spool, keyed by column. Reconcile reuses these to
// project an existing spool through the same parse path, so an unchanged row
// compares equal.
std::map<std::string, std::string> spool_to_cells(const FilamentSpool& s)
{
    return {
        {"id",            s.spool_id},
        {"brand",         s.brand},
        {"material_type", s.material_type},
        {"series",        s.series},
        {"color_code",    s.color_code},
        {"net_weight",    num(s.net_weight)},
        {"note",          s.note},
        {"setting_id",    s.filament_id},
        {"deleted",       ""},
    };
}

std::map<std::string, std::string> spool_to_readonly_cells(const FilamentSpool& s)
{
    return {
        {"color_name",     s.color_name},
        {"diameter",       num(s.diameter)},
        {"initial_weight", num(s.initial_weight)},
        {"spool_weight",   num(s.spool_weight)},
        {"remain_percent", std::to_string(s.remain_percent)},
        {"status",         s.status},
        {"favorite",       s.favorite ? "true" : "false"},
    };
}

// The payload an unchanged import of this spool would produce, so reconcile can
// skip rows that match. False if the spool fails validation (treat as changed).
bool existing_payload(const FilamentSpool& s, nlohmann::json& out)
{
    CsvRow row;
    for (const auto& kv : spool_to_cells(s)) row.cells[kv.first] = kv.second;
    std::string ignored;
    return row_to_payload(row, out, ignored);
}

// One parsed record: its fields and 1-based start line.
struct RawRecord { std::vector<std::string> fields; int line; };

std::vector<RawRecord> read_records(const std::string& text)
{
    std::vector<RawRecord> records;
    std::string field;
    std::vector<std::string> fields;
    bool in_quotes = false;
    int  line = 1;
    int  record_line = 1;
    bool started = false;

    auto push_field  = [&]() { fields.push_back(field); field.clear(); };
    auto push_record = [&]() {
        push_field();
        records.push_back({fields, record_line});
        fields.clear();
        started = false;
    };

    for (size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (!started && !in_quotes) { record_line = line; started = true; }
        if (in_quotes) {
            if (ch == '"') {
                if (i + 1 < text.size() && text[i + 1] == '"') { field += '"'; ++i; }
                else in_quotes = false;
            } else {
                if (ch == '\n') ++line;
                field += ch;
            }
            continue;
        }
        if (ch == '"')  { in_quotes = true; continue; }
        if (ch == ',')  { push_field(); continue; }
        if (ch == '\r') { continue; }
        if (ch == '\n') { push_record(); ++line; continue; }
        field += ch;
    }
    if (started || !field.empty() || !fields.empty()) push_record();
    return records;
}

} // namespace

const std::vector<std::string>& filament_csv_columns()          { return kColumns; }
const std::vector<std::string>& filament_csv_readonly_columns() { return kReadonlyColumns; }

std::string CsvRow::get(const std::string& col) const
{
    auto it = cells.find(col);
    return it == cells.end() ? std::string() : it->second;
}

std::string export_spools_to_csv(const std::vector<FilamentSpool>& spools,
                                  bool include_readonly, const std::string& readonly_suffix)
{
    auto join_line = [](const std::vector<std::string>& cells) {
        std::string line;
        for (size_t i = 0; i < cells.size(); ++i) {
            if (i) line += ",";
            line += csv_cell(cells[i]);
        }
        line += "\n";
        return line;
    };

    std::vector<std::string> header(kColumns);
    if (include_readonly)
        for (const std::string& c : kReadonlyColumns) header.push_back(c + readonly_suffix);

    std::string out = join_line(header);

    for (const FilamentSpool& s : spools) {
        const std::map<std::string, std::string> cells = spool_to_cells(s);
        std::vector<std::string> values;
        values.reserve(header.size());
        for (const std::string& c : kColumns) values.push_back(cells.at(c));
        if (include_readonly) {
            const std::map<std::string, std::string> ro = spool_to_readonly_cells(s);
            for (const std::string& c : kReadonlyColumns) values.push_back(ro.at(c));
        }
        out += join_line(values);
    }
    return out;
}

ParsedCsv parse_csv(const std::string& text)
{
    ParsedCsv parsed;
    std::vector<RawRecord> records;
    for (RawRecord& rec : read_records(text)) {
        if (rec.fields.size() == 1 && trim(rec.fields[0]).empty()) continue; // blank line
        records.push_back(std::move(rec));
    }
    if (records.empty()) return parsed;

    for (const std::string& h : records[0].fields) parsed.header.push_back(trim(h));
    for (size_t r = 1; r < records.size(); ++r) {
        CsvRow row;
        row.line = records[r].line;
        for (size_t c = 0; c < parsed.header.size(); ++c) {
            const std::string value = c < records[r].fields.size() ? records[r].fields[c] : std::string();
            row.cells[parsed.header[c]] = trim(value);
        }
        parsed.rows.push_back(std::move(row));
    }
    return parsed;
}

ImportPlan reconcile_import(const ParsedCsv& parsed, const std::vector<FilamentSpool>& existing)
{
    ImportPlan plan;
    std::map<std::string, const FilamentSpool*> by_id;
    for (const FilamentSpool& s : existing) by_id[s.spool_id] = &s;

    for (const CsvRow& row : parsed.rows) {
        const int         line    = row.line;
        const std::string id      = row.get("id");
        const bool        deleted = is_truthy_flag(row.get("deleted"));

        if (deleted) {
            if (!id.empty() && by_id.count(id)) {
                plan.deletes.push_back({id, spool_label(*by_id[id])});
            } else {
                plan.errors.push_back({line, id.empty()
                    ? "Row marked deleted but has no id"
                    : "Cannot delete unknown id \"" + id + "\""});
            }
            continue;
        }

        nlohmann::json payload;
        std::string    error;
        if (!row_to_payload(row, payload, error)) {
            plan.errors.push_back({line, error});
            continue;
        }

        if (!id.empty()) {
            if (by_id.count(id)) {
                // Only update when a field actually changed.
                nlohmann::json current;
                if (existing_payload(*by_id[id], current) && current == payload)
                    continue;
                payload["spool_id"] = id;
                plan.updates.push_back(std::move(payload));
            } else {
                plan.errors.push_back({line, "Unknown id \"" + id + "\" - no spool to update"});
            }
        } else {
            plan.creates.push_back(std::move(payload));
        }
    }
    return plan;
}

}} // namespace Slic3r::GUI
