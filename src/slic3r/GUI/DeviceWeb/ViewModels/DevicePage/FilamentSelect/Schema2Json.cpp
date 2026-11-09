/*****************************************************************//**
 * \file   Schema2Json.cpp
 * \brief  to_json() implementations of the `device_page_filament_select` schema.
 *********************************************************************/
#include "Schema.hpp"

namespace Slic3r { namespace GUI { namespace FilamentSelectSchema { namespace format {

namespace K = FilamentSelectSchema::keys;

void to_json(nlohmann::json& j, const Chip& v)
{
    j = {
        {K::chip::key,   v.key},
        {K::chip::kind,  v.kind},
        {K::chip::label, v.label}
    };
}

void to_json(nlohmann::json& j, const Swatch& v)
{
    j = {
        {K::swatch::color_type, v.color_type},
        {K::swatch::colors,     v.colors},
        {K::swatch::color_code, v.color_code}
    };
}

void to_json(nlohmann::json& j, const Tail& v)
{
    j = {
        {K::tail::kind,      v.kind},
        {K::tail::slot_name, v.slot_name}
    };
}

void to_json(nlohmann::json& j, const SpoolRow& v)
{
    j = {
        {K::row::row_key,       v.row_key},
        {K::row::chip_key,      v.chip_key},
        {K::row::search_key,    v.search_key},
        {K::row::name,          v.name},
        {K::row::weight_g,      v.weight_g},
        {K::row::swatch,        v.swatch},
        {K::row::show_bbl_logo, v.show_bbl_logo},
        {K::row::tail,          v.tail},
        {K::row::selectable,    v.selectable}
    };
}

void to_json(nlohmann::json& j, const ManagerTab& v)
{
    j = {
        {K::manager::chips, v.chips},
        {K::manager::rows,  v.rows}
    };
}

void to_json(nlohmann::json& j, const PresetsTab& v)
{
    nlohmann::json aliases = nlohmann::json::object();
    for (const auto& [brand_key, list] : v.aliases)
        aliases[brand_key] = list;
    j = {
        {K::presets::brands,        v.brands},
        {K::presets::aliases,       aliases},
        {K::presets::initial_brand, v.initial_brand},
        {K::presets::checked_alias, v.checked_alias}
    };
}

void to_json(nlohmann::json& j, const State& v)
{
    j = {
        {K::root::theme,   v.theme},
        {K::root::manager, v.manager},
        {K::root::presets, v.presets}
    };
}

}}}} // namespace Slic3r::GUI::FilamentSelectSchema::format
