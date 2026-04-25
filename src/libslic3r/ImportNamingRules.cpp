#include "ImportNamingRules.hpp"
#include <algorithm>
#include <cctype>

namespace Slic3r {

std::vector<ImportNamingRule> default_import_naming_rules()
{
    std::vector<ImportNamingRule> rules;

    // Part type rules
    rules.push_back({"part",    0, ModelVolumeType::MODEL_PART});
    rules.push_back({"neg",     0, ModelVolumeType::NEGATIVE_VOLUME});
    rules.push_back({"mod",     0, ModelVolumeType::PARAMETER_MODIFIER});
    rules.push_back({"blk",     0, ModelVolumeType::SUPPORT_BLOCKER});
    rules.push_back({"enf",     0, ModelVolumeType::SUPPORT_ENFORCER});

    // Filament rules f1–f9
    for (int i = 1; i <= 16; ++i) {
        ImportNamingRule r;
        r.tag      = "f" + std::to_string(i);
        r.filament = i;
        r.volume_type = ModelVolumeType::INVALID;
        rules.push_back(r);
    }

    return rules;
}

static std::string to_lower(const std::string& s)
{
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return out;
}

ImportNameTags parse_import_name_tags(const std::string&                  name,
                                      const std::vector<ImportNamingRule>& rules)
{
    ImportNameTags result;

    // Extract all [...] tokens from the name (case-insensitive)
    std::string lower_name = to_lower(name);
    std::regex  tag_re(R"(\[([a-z0-9]+)\])");
    auto        begin = std::sregex_iterator(lower_name.begin(), lower_name.end(), tag_re);
    auto        end   = std::sregex_iterator();

    for (auto it = begin; it != end; ++it) {
        std::string token = (*it)[1].str(); // content inside brackets, already lower
        for (const auto& rule : rules) {
            if (to_lower(rule.tag) == token) {
                if (rule.filament > 0)
                    result.filament = rule.filament;
                if (rule.volume_type != ModelVolumeType::INVALID)
                    result.volume_type = rule.volume_type;
                break;
            }
        }
    }

    return result;
}

void apply_import_name_tags(ModelVolume& volume, const ImportNameTags& tags)
{
    if (tags.filament > 0)
        volume.config.set_key_value("extruder", new ConfigOptionInt(tags.filament));

    if (tags.volume_type != ModelVolumeType::INVALID)
        volume.set_type(tags.volume_type);
}

void apply_naming_rules_to_volume(ModelVolume&                         volume,
                                  const std::vector<ImportNamingRule>& rules)
{
    ImportNameTags tags = parse_import_name_tags(volume.name, rules);
    apply_import_name_tags(volume, tags);
}

void apply_naming_rules_to_objects(const ModelObjectPtrs&               objects,
                                   const std::vector<ImportNamingRule>& rules)
{
    for (ModelObject* obj : objects) {
        if (!obj) continue;
        for (ModelVolume* vol : obj->volumes) {
            if (!vol) continue;
            apply_naming_rules_to_volume(*vol, rules);
        }
    }
}

} // namespace Slic3r
