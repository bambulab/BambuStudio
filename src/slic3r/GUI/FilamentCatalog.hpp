#pragma once

#include "EncodedFilament.hpp"
#include <wx/tokenzr.h>
#include "fila_manager/wgtFilaManagerStore.h"
#include <cmath>
#include <optional>
#include <algorithm>

namespace Slic3r { namespace GUI {

// Search catalog names rather than translated UI strings. Match every word, in any
// order, across all supplied translations so labels on physical swatches work too.
inline bool filament_search_matches(wxString text, const wxString& query)
{
    text.MakeLower();
    wxStringTokenizer words(query.Lower());
    while (words.HasMoreTokens())
        if (!text.Contains(words.GetNextToken())) return false;
    return true;
}

inline wxString filament_catalog_search_text(const FilamentColorCode& color)
{
    wxString text = "Bambu " + color.GetFilaType() + " " + color.GetFilaColorCode();
    for (const auto& name : color.GetColorNames()) text += " " + name.second;
    const auto value = color.GetFilaColor();
    for (const auto& c : value.GetColors())
        text += wxString::Format(" #%02X%02X%02X%02X", c.Red(), c.Green(), c.Blue(), c.Alpha());
    return text;
}

inline bool filament_catalog_matches(const FilamentColorCode& color, const wxString& query)
{
    return filament_search_matches(filament_catalog_search_text(color), query);
}

inline wxString filament_vendor_key(const std::string& brand)
{
    wxString key = wxString::FromUTF8(brand).Trim().Trim(false).Lower();
    return key == "bambu" ? wxString("bambu lab") : key;
}

inline wxString inventory_material_name(const FilamentSpool& spool)
{
    wxString type = wxString::FromUTF8(spool.material_type).Trim().Trim(false);
    wxString series = wxString::FromUTF8(spool.series).Trim().Trim(false);
    if (series.empty()) return type;
    const wxString brand = wxString::FromUTF8(spool.brand).Trim().Trim(false);
    if (!brand.empty() && series.Lower().StartsWith(brand.Lower() + " ")) series = series.Mid(brand.size() + 1);
    else if (filament_vendor_key(spool.brand) == "bambu lab" && series.Lower().StartsWith("bambu ")) series = series.Mid(6);
    if (series.Lower().StartsWith(type.Lower())) return series;
    return type.empty() ? series : type + " " + series;
}

inline FilamentColor inventory_filament_color(const FilamentSpool& spool)
{
    FilamentColor result;
    const std::vector<std::string> values = spool.colors.empty() ? std::vector<std::string>{spool.color_code} : spool.colors;
    for (const auto& value : values) {
        wxString hex = wxString::FromUTF8(value).Trim().Trim(false);
        if (hex.StartsWith("#")) hex = hex.Mid(1);
        if (hex.size() != 6 && hex.size() != 8) return {};
        for (auto c : hex) if (!wxString("0123456789abcdefABCDEF").Contains(c)) return {};
        wxColour color("#" + hex);
        if (!color.IsOk()) return {};
        result.AddColor(color);
    }
    result.EndSet(spool.color_type);
    return result;
}

inline bool inventory_spool_available(const FilamentSpool& spool)
{
    return spool.status != "archived" && spool.status != "empty";
}

inline std::optional<double> inventory_remaining_weight(const FilamentSpool& spool)
{
    // A zero net_weight is also the store's default for an unknown measurement.
    // Prefer a measured value, then estimate from a known net capacity/percentage.
    if (std::isfinite(spool.net_weight) && spool.net_weight > 0) return spool.net_weight;
    if (!std::isfinite(spool.initial_weight) || !std::isfinite(spool.spool_weight) ||
        spool.spool_weight < 0 || spool.remain_percent < 0 || spool.remain_percent > 100)
        return std::nullopt;
    const double capacity = spool.initial_weight - spool.spool_weight;
    if (capacity > 0) return capacity * spool.remain_percent / 100.0;
    return std::nullopt;
}

struct InventoryRemaining {
    size_t spools = 0;
    size_t unknown = 0;
    double grams = 0;

    void add(const FilamentSpool& spool)
    {
        ++spools;
        const auto remaining = inventory_remaining_weight(spool);
        if (remaining) grams += *remaining;
        else ++unknown;
    }
};

struct InventoryNotes {
    std::vector<wxString> values;

    wxString add(wxString note)
    {
        note.Trim().Trim(false);
        if (!note.empty() && std::find(values.begin(), values.end(), note) == values.end())
            values.push_back(std::move(note));
        wxString result;
        for (const auto& value : values) {
            if (!result.empty()) result += "; ";
            result += value;
        }
        return result;
    }
};

inline int compare_inventory_remaining(const InventoryRemaining& a, const InventoryRemaining& b, bool ascending)
{
    // Complete totals first, then partial totals, unknown weights, and unowned
    // catalog entries. Missing data must never masquerade as the lightest spool.
    const auto category = [](const InventoryRemaining& value) {
        if (value.spools == 0) return 3;
        if (value.unknown == value.spools) return 2;
        return value.unknown > 0 ? 1 : 0;
    };
    const int ac = category(a), bc = category(b);
    if (ac != bc) return ac < bc ? -1 : 1;
    if (ac >= 2 || a.grams == b.grams) return 0;
    const int order = a.grams < b.grams ? -1 : 1;
    return ascending ? order : -order;
}

inline std::vector<std::string> filament_catalog_colors(const FilamentColor& color)
{
    std::vector<std::string> result;
    for (const auto& c : color.GetColors())
        result.push_back(wxString::Format("#%02X%02X%02X%02X", c.Red(), c.Green(), c.Blue(), c.Alpha()).ToStdString());
    return result;
}

}} // namespace Slic3r::GUI
