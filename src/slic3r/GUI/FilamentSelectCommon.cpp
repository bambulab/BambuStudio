#include "FilamentSelectCommon.hpp"

#include "AMSMaterialsSetting.hpp"     // spool_display_name
#include "DeviceCore/DevFilaSystem.h"  // DevFilaSystem::GetTrayNameByAmsSlotId
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "libslic3r/Preset.hpp"        // FilamentListKey / filament_list_less
#include "libslic3r/PresetBundle.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include "libslic3r/Log.hpp"

namespace Slic3r { namespace GUI { namespace FilamentSelectCommon {

namespace {

constexpr const char* SPOOL_RECENT_KEY = "filament_mgr_recent_spools";

int brand_rank(const std::string& brand)
{
    const auto& prio = brand_priority();
    for (int i = 0; i < (int)prio.size(); ++i)
        if (prio[i] == wxString::FromUTF8(brand)) return i;
    return (int)prio.size();
}

bool spool_has_preset(const FilamentSpool& sp, const std::set<std::string>& printer_names)
{
    auto* bundle = wxGetApp().preset_bundle;
    if (!bundle || sp.filament_id.empty()) return false;
    for (const auto& printer_name : printer_names) {
        if (bundle->get_filament_by_filament_id(sp.filament_id, printer_name).has_value())
            return true;
    }
    return false;
}

} // namespace

std::vector<wxString> load_recent_spool_ids()
{
    auto* cfg = wxGetApp().app_config;
    if (!cfg) return {};
    const std::string raw = cfg->get(SPOOL_RECENT_KEY);
    std::vector<wxString> out;
    std::istringstream ss(raw);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty())
            out.push_back(wxString::FromUTF8(line));
    }
    return out;
}

void remember_recent_spool(const wxString& spool_id)
{
    if (spool_id.IsEmpty()) return;
    auto* cfg = wxGetApp().app_config;
    if (!cfg) return;

    std::vector<wxString> items = load_recent_spool_ids();
    items.erase(std::remove(items.begin(), items.end(), spool_id), items.end());
    items.insert(items.begin(), spool_id);
    if (items.size() > SPOOL_RECENT_MAX)
        items.resize(SPOOL_RECENT_MAX);

    std::string out;
    for (const wxString& id : items) {
        if (!id.IsEmpty()) {
            if (!out.empty()) out += '\n';
            out += id.ToUTF8().data();
        }
    }
    cfg->set(SPOOL_RECENT_KEY, out);
}

const std::vector<wxString>& brand_priority()
{
    static const std::vector<wxString> prio{ "Bambu Lab", "Generic", "Polymaker" };
    return prio;
}

std::vector<wxString> order_brands(std::vector<wxString> keys, const wxString& other)
{
    const auto& prio = brand_priority();
    std::vector<wxString> out;
    for (const wxString& p : prio)
        if (std::find(keys.begin(), keys.end(), p) != keys.end()) out.push_back(p);
    std::vector<wxString> tail;
    for (const wxString& k : keys) {
        if (k == other) continue;
        if (std::find(prio.begin(), prio.end(), k) != prio.end()) continue;
        tail.push_back(k);
    }
    std::sort(tail.begin(), tail.end());
    for (const wxString& t : tail) out.push_back(t);
    if (std::find(keys.begin(), keys.end(), other) != keys.end()) out.push_back(other);
    return out;
}

bool is_bbl_brand(const std::string& brand)
{
    std::string b = brand;
    std::transform(b.begin(), b.end(), b.begin(), [](unsigned char c) { return std::tolower(c); });
    return b.find("bambu") != std::string::npos;
}

int spool_weight_grams(const FilamentSpool& sp)
{
    if (sp.net_weight > 0.0) return static_cast<int>(std::round(sp.net_weight));
    const double total = sp.effective_total_net_weight();
    if (total > 0.0) {
        const int pct = std::max(0, std::min(100, sp.remain_percent));
        return static_cast<int>(total * pct / 100.0);
    }
    return 0;
}

wxString slot_label(const FilamentSpool& sp)
{
    if (!sp.in_printer || sp.slot_id.empty() || sp.ams_id < 0) return wxString();
    try {
        const std::string s = DevFilaSystem::GetTrayNameByAmsSlotId(
            sp.dev_id, sp.ams_id, std::stoi(sp.slot_id), (DevAmsType)sp.ams_type);
        if (!s.empty()) return wxString::FromUTF8(s);
    } catch (...) {
        BOOST_LOG_TRIVIAL(warning) << current_exception_message();
    }
    return wxString::FromUTF8(sp.slot_id);
}

wxString spool_row_name(const FilamentSpool& sp)
{
    wxString name;
    if (is_bbl_brand(sp.brand)) {
        if (!sp.series.empty())             name = wxString::FromUTF8(sp.series);
        else if (!sp.material_type.empty()) name = wxString::FromUTF8(sp.material_type);
        else if (!sp.color_name.empty())    name = wxString::FromUTF8(sp.color_name);
        else                                name = _L("Filament");
    } else {
        name = spool_display_name(sp);
        if (!sp.color_name.empty()) name += " " + wxString::FromUTF8(sp.color_name);
    }
    return name;
}

wxString spool_search_key(const FilamentSpool& sp)
{
    wxString s;
    s += wxString::FromUTF8(sp.brand)         + " ";
    s += wxString::FromUTF8(sp.series)        + " ";
    s += wxString::FromUTF8(sp.material_type) + " ";
    s += wxString::FromUTF8(sp.color_name);
    return s.Lower();
}

SpoolRowList build_spool_rows(const std::set<std::string>& printer_names)
{
    SpoolRowList out;

    const wxString other; // empty key = "Other" bucket
    std::map<wxString, std::vector<FilamentSpool>> brand_to_spools;
    std::vector<FilamentSpool> unsupported;

    if (auto* store = wxGetApp().fila_manager_store()) {
        for (const auto& id : store->all_spool_ids()) {
            const FilamentSpool* sp = store->get_spool(id);
            if (!sp) continue;
            if (spool_has_preset(*sp, printer_names)) {
                const wxString b = sp->brand.empty() ? other : wxString::FromUTF8(sp->brand);
                brand_to_spools[b].push_back(*sp);
            } else {
                unsupported.push_back(*sp);
            }
        }
    }
    for (auto& kv : brand_to_spools)
        std::sort(kv.second.begin(), kv.second.end(),
                  [](const FilamentSpool& a, const FilamentSpool& b) { return a.series < b.series; });

    std::sort(unsupported.begin(), unsupported.end(),
        [](const FilamentSpool& a, const FilamentSpool& b) {
            const int ra = brand_rank(a.brand), rb = brand_rank(b.brand);
            if (ra != rb) return ra < rb;
            if (a.brand != b.brand) return a.brand < b.brand;
            return a.series < b.series;
        });

    std::vector<wxString> keys;
    for (auto& kv : brand_to_spools) keys.push_back(kv.first);
    out.brands = order_brands(keys, other);

    // Flatten supported spools in brand+series order for the passes below.
    std::vector<std::pair<wxString, const FilamentSpool*>> all_supported;
    for (const wxString& b : out.brands)
        for (const FilamentSpool& sp : brand_to_spools.at(b))
            all_supported.push_back({b, &sp});

    auto push = [&out](const wxString& brand, const FilamentSpool& sp, SpoolRowGroup group) {
        out.rows.push_back(SpoolRowEntry{sp, brand, group});
    };

    // Pass 1: recently used (LRU order, newest first), excluding in-printer
    std::set<std::string> shown_recent;
    for (const wxString& sid : load_recent_spool_ids()) {
        if (shown_recent.size() >= SPOOL_RECENT_SHOW) break;
        for (const auto& [b, sp] : all_supported) {
            if (sp->in_printer) continue;
            if (wxString::FromUTF8(sp->spool_id) != sid) continue;
            push(b, *sp, SpoolRowGroup::Recent);
            shown_recent.insert(sp->spool_id);
            break;
        }
    }

    // Pass 2: remaining supported spools (not in-printer, not recently used)
    for (const auto& [b, sp] : all_supported) {
        if (sp->in_printer) continue;
        if (shown_recent.count(sp->spool_id)) continue;
        push(b, *sp, SpoolRowGroup::Available);
    }

    // Pass 3: AMS in-printer spools (slot badge shown, not selectable)
    for (const auto& [b, sp] : all_supported) {
        if (!sp->in_printer) continue;
        push(b, *sp, SpoolRowGroup::InPrinter);
    }

    // Pass 4: unsupported spools (chip filtering only, not selectable)
    for (const FilamentSpool& sp : unsupported)
        push(wxString(), sp, SpoolRowGroup::Unsupported);

    return out;
}

PresetGroups build_preset_groups(const FilamentSelectInput& in)
{
    PresetGroups out;

    const wxString other; // empty key = "Other" vendor
    for (const wxString& alias : in.filament_items) {
        wxString vendor;
        auto it = in.vendors.find(alias);
        if (it != in.vendors.end()) vendor = it->second;
        out.aliases[vendor.IsEmpty() ? other : vendor].push_back(alias);
    }

    // Reuse the shared filament ordering (same curated order as the main-window combo boxes).
    auto fkey = [&in](const wxString& a) -> FilamentListKey {
        auto iv = in.vendors.find(a);
        auto it = in.types.find(a);
        return FilamentListKey{ std::string(a.ToUTF8().data()),
                                iv != in.vendors.end() ? std::string(iv->second.ToUTF8().data()) : std::string(),
                                it != in.types.end()   ? std::string(it->second.ToUTF8().data()) : std::string() };
    };

    for (auto& kv : out.aliases)
        std::sort(kv.second.begin(), kv.second.end(),
                  [&](const wxString& l, const wxString& r) { return filament_list_less(fkey(l), fkey(r)); });

    // Brand order: rank by each brand's leading alias, then keep "Other" pinned last.
    for (auto& kv : out.aliases) out.brands.push_back(kv.first);
    std::sort(out.brands.begin(), out.brands.end(),
              [&](const wxString& lb, const wxString& rb) {
                  return filament_list_less(fkey(out.aliases[lb].front()), fkey(out.aliases[rb].front()));
              });
    if (auto p = std::find(out.brands.begin(), out.brands.end(), other); p != out.brands.end()) {
        out.brands.erase(p);
        out.brands.push_back(other);
    }
    return out;
}

}}} // namespace Slic3r::GUI::FilamentSelectCommon
