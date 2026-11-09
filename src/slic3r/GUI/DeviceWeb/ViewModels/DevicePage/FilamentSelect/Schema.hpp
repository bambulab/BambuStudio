#ifndef FILAMENTSELECTSCHEMA_HPP
#define FILAMENTSELECTSCHEMA_HPP

/*****************************************************************//**
 * \file   Schema.hpp
 * \brief  JSON contract of the `device_page_filament_select` module.
 *
 * The Web side mirrors the same shape in `filament-select/types.ts`; keep this
 * header as the single source of truth for field names and enum values.
 *
 * Display-only: the page renders `manager` and `presets` as sent. The only
 * action gate is `rows[].selectable`; confirm is re-validated by
 * FilamentSelectActions.
 *
 * No spool_id / dev_id / AMS SN / device name is ever sent: a row is
 * addressed by `row_key`, an index into the C++ snapshot.
 *********************************************************************/

#include <nlohmann/json.hpp>

#include <map>
#include <string>
#include <vector>

namespace Slic3r { namespace GUI { namespace FilamentSelectSchema {

inline constexpr const char* module_name = "device_page_filament_select";

namespace keys {

namespace root {
    inline constexpr const char* theme   = "theme";
    inline constexpr const char* manager = "manager";
    inline constexpr const char* presets = "presets";
}

namespace manager {
    inline constexpr const char* chips = "chips";
    inline constexpr const char* rows  = "rows";
}

/// manager.chips[] and presets.brands[]
namespace chip {
    inline constexpr const char* key   = "key";
    inline constexpr const char* kind  = "kind";  // values::chip_kind
    inline constexpr const char* label = "label"; // brand name; empty unless kind == brand
}

namespace row {
    inline constexpr const char* row_key       = "row_key";
    inline constexpr const char* chip_key      = "chip_key";
    inline constexpr const char* search_key    = "search_key";
    inline constexpr const char* name          = "name";
    inline constexpr const char* weight_g      = "weight_g";
    inline constexpr const char* swatch        = "swatch";
    inline constexpr const char* show_bbl_logo = "show_bbl_logo";
    inline constexpr const char* tail          = "tail";
    inline constexpr const char* selectable    = "selectable";
}

namespace swatch {
    inline constexpr const char* color_type = "color_type"; // 0 gradient, 1 multicolour, 2 single
    inline constexpr const char* colors     = "colors";
    inline constexpr const char* color_code = "color_code";
}

namespace tail {
    inline constexpr const char* kind      = "kind";      // values::tail_kind
    inline constexpr const char* slot_name = "slot_name"; // "A1" etc., only for kind == slot
}

namespace presets {
    inline constexpr const char* brands        = "brands";
    inline constexpr const char* aliases       = "aliases";       // brand key -> sorted aliases
    inline constexpr const char* initial_brand = "initial_brand"; // brand key shown first
    inline constexpr const char* checked_alias = "checked_alias"; // preselected alias, may be empty
}

/// Payload of `action` / `confirm`.
namespace confirm {
    inline constexpr const char* tab     = "tab";     // values::tab
    inline constexpr const char* row_key = "row_key"; // tab == manager
    inline constexpr const char* alias   = "alias";   // tab == presets
}

} // namespace keys

namespace values {

namespace theme {
    inline constexpr const char* light = "light";
    inline constexpr const char* dark  = "dark";
}

namespace chip_kind {
    inline constexpr const char* all         = "all";
    inline constexpr const char* brand       = "brand";
    inline constexpr const char* other       = "other";
    inline constexpr const char* unsupported = "unsupported";
}

namespace chip_key {
    inline constexpr const char* all          = "all";
    inline constexpr const char* other        = "other";
    inline constexpr const char* unsupported  = "unsupported";
    inline constexpr const char* brand_prefix = "brand:";
}

namespace tail_kind {
    inline constexpr const char* slot   = "slot";
    inline constexpr const char* recent = "recent";
    inline constexpr const char* none   = "none";
}

namespace tab {
    inline constexpr const char* manager = "manager";
    inline constexpr const char* presets = "presets";
}

} // namespace values

namespace format {

struct Chip
{
    std::string key;
    std::string kind; // values::chip_kind
    std::string label;
};

struct Swatch
{
    int                      color_type = 2;
    std::vector<std::string> colors;
    std::string              color_code;
};

struct Tail
{
    std::string kind = values::tail_kind::none;
    std::string slot_name;
};

struct SpoolRow
{
    std::string row_key;
    std::string chip_key;
    std::string search_key;
    std::string name;
    int         weight_g      = 0;
    Swatch      swatch;
    bool        show_bbl_logo = false;
    Tail        tail;
    bool        selectable    = false;
};

struct ManagerTab
{
    std::vector<Chip>     chips;
    std::vector<SpoolRow> rows;
};

struct PresetsTab
{
    std::vector<Chip>                               brands; // kind: brand | other
    std::map<std::string, std::vector<std::string>> aliases;
    std::string                                     initial_brand;
    std::string                                     checked_alias;
};

struct State
{
    std::string theme = values::theme::light;
    ManagerTab  manager;
    PresetsTab  presets;
};

void to_json(nlohmann::json& j, const Chip& v);
void to_json(nlohmann::json& j, const Swatch& v);
void to_json(nlohmann::json& j, const Tail& v);
void to_json(nlohmann::json& j, const SpoolRow& v);
void to_json(nlohmann::json& j, const ManagerTab& v);
void to_json(nlohmann::json& j, const PresetsTab& v);
void to_json(nlohmann::json& j, const State& v);

} // namespace format

}}} // namespace Slic3r::GUI::FilamentSelectSchema

#endif // FILAMENTSELECTSCHEMA_HPP
