#include "wgtFilaManagerSpoolColors.h"

#include <algorithm>
#include <regex>
#include <set>

#include <boost/algorithm/string.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>

namespace Slic3r { namespace GUI {

namespace {

std::string type_key(const std::string& type) { return boost::algorithm::to_lower_copy(boost::algorithm::trim_copy(type)); }

bool usable(const FilamentSpool& s) { return s.status != "empty" && s.status != "archived"; }

// The filament types a spool is offered under, as type keys.
std::vector<std::string> offered_under(const FilamentSpool& s, const SpoolColorRules& rules)
{
    std::vector<std::string> keys;
    for (const std::string& t : spool_preset_types(s, rules)) keys.push_back(type_key(t));
    return keys;
}

// The Filament Manager has no free-text colour-name field, so a custom colour's
// name is kept in the note. Drop the trailing "(N bought)" the importer appends
// so only the colour word remains.
std::string clean_note(const std::string& note)
{
    static const std::regex bought_tail(R"(\s*\([^()]*\bbought\b[^()]*\)\s*$)", std::regex::icase);
    return boost::algorithm::trim_copy(std::regex_replace(boost::algorithm::trim_copy(note), bought_tail, std::string()));
}

// Brand + colour word. The colour word is the colour name, or the cleaned note
// when there is no colour name. With neither, the label is just the brand and
// the wx caller fills the colour word from the colour query.
std::string label_of(const FilamentSpool& s)
{
    const std::string colour = s.color_name.empty() ? clean_note(s.note) : s.color_name;
    return boost::algorithm::trim_copy(s.brand + " " + colour);
}

} // namespace

SpoolColorRules SpoolColorRules::from_json(const nlohmann::json& j)
{
    SpoolColorRules rules;
    if (!j.is_object() || !j.contains("type_aliases") || !j["type_aliases"].is_object()) return rules;

    for (const auto& [spool_type, targets] : j["type_aliases"].items()) {
        std::vector<std::string> types;
        if (targets.is_string())
            types.push_back(targets.get<std::string>());
        else if (targets.is_array())
            for (const auto& t : targets)
                if (t.is_string()) types.push_back(t.get<std::string>());
        rules.type_aliases[type_key(spool_type)] = std::move(types);
    }
    return rules;
}

SpoolColorRules SpoolColorRules::load(const std::string& path)
{
    boost::nowide::ifstream in(path);
    if (!in.is_open()) return {};
    try {
        return from_json(nlohmann::json::parse(in));
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(warning) << "Ignoring spool colour rules " << path << ": " << e.what();
        return {};
    }
}

std::vector<std::string> spool_preset_types(const FilamentSpool& s, const SpoolColorRules& rules)
{
    const std::string own = boost::algorithm::trim_copy(s.series.empty() ? s.material_type : s.series);
    auto              it  = rules.type_aliases.find(type_key(own));
    return it == rules.type_aliases.end() ? std::vector<std::string>{own} : it->second;
}

std::vector<SpoolSwatch> spool_swatches_for(const std::vector<FilamentSpool>& spools,
                                            const std::string&                filament_type,
                                            const SpoolColorRules&            rules,
                                            const std::string&                filament_id)
{
    const std::string wanted = type_key(filament_type);

    std::vector<SpoolSwatch>                                   swatches;
    std::set<std::pair<std::string, std::vector<std::string>>> seen;
    for (const FilamentSpool& s : spools) {
        if (!usable(s)) continue;
        // A spool linked to this preset by "Prints With" is offered whatever
        // its type; otherwise fall back to matching the type (and its rules).
        const bool linked = !filament_id.empty() && s.filament_id == filament_id;
        if (!linked) {
            const auto types = offered_under(s, rules);
            if (std::find(types.begin(), types.end(), wanted) == types.end()) continue;
        }

        SpoolSwatch sw;
        sw.colors = s.colors.empty() ? std::vector<std::string>{s.color_code} : s.colors;
        sw.colors.erase(std::remove(sw.colors.begin(), sw.colors.end(), std::string()), sw.colors.end());
        if (sw.colors.empty()) continue;
        sw.color_type = s.color_type;
        sw.label      = label_of(s);
        sw.brand      = boost::algorithm::trim_copy(s.brand);
        if (seen.emplace(sw.label, sw.colors).second) swatches.push_back(std::move(sw));
    }

    std::sort(swatches.begin(), swatches.end(), [](const SpoolSwatch& a, const SpoolSwatch& b) { return a.label < b.label; });
    return swatches;
}

}} // namespace Slic3r::GUI
