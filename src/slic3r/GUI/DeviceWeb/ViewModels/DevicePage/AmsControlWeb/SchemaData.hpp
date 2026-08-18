#ifndef AMSCONTROLWEBSCHEMA_DATA_HPP
#define AMSCONTROLWEBSCHEMA_DATA_HPP

// The `data` half of the payload: a straight mirror of DevFilaSystem AMS units,
// virtual trays and DevFilaSwitch, using the same tray field names as
// FilamentManagerVM::build_ams_data(). Rendering decisions belong in
// SchemaDisplay.hpp.

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace Slic3r { namespace GUI { namespace AmsControlWebSchema {

namespace data {
    inline constexpr const char* selected_dev_id        = "selected_dev_id";
    inline constexpr const char* ams_units              = "ams_units";
    inline constexpr const char* ext_slots              = "ext_slots";
    inline constexpr const char* fila_switch            = "fila_switch";
    inline constexpr const char* detect_remain_enabled  = "detect_remain_enabled";
} // namespace data

// DevFilaSwitch mirror: two input slots (A / B) multiplexed onto two extruders.
namespace fila_switch {
    inline constexpr const char* installed         = "installed";
    inline constexpr const char* ready             = "ready";
    inline constexpr const char* cali_status       = "cali_status";
    inline constexpr const char* cali_status_name  = "cali_status_name";
    inline constexpr const char* in_a              = "in_a";
    inline constexpr const char* in_b              = "in_b";
    inline constexpr const char* out_a_extruder_id = "out_a_extruder_id";
    inline constexpr const char* out_b_extruder_id = "out_b_extruder_id";
} // namespace fila_switch

namespace fila_switch_port {
    inline constexpr const char* has_filament = "has_filament";// -1 when the machine did not report it, 0 / 1 otherwise.
    inline constexpr const char* ams_id       = "ams_id";
    inline constexpr const char* slot_id      = "slot_id";
} // namespace fila_switch_port

namespace unit {
    inline constexpr const char* ams_id                = "ams_id";
    inline constexpr const char* ams_type              = "ams_type";// DevAmsType enumerator name, e.g. "N3F".
    inline constexpr const char* ams_type_name         = "ams_type_name";
    // DevAms::GetAmsType() reports AMS_LITE_MIXED as AMS_LITE, so the N9 variant
    // can only be told apart through this flag.
    inline constexpr const char* is_ams_lite_mixed     = "is_ams_lite_mixed";
    inline constexpr const char* humidity_level        = "humidity_level";
    inline constexpr const char* humidity_percent      = "humidity_percent";
    inline constexpr const char* humidity_display_type = "humidity_display_type";
    // Reported per unit, not per slot: this is what decides the side the unit is
    // drawn on.
    inline constexpr const char* binded_extruder_ids   = "binded_extruder_ids";
    inline constexpr const char* switcher_port         = "switcher_port";
    inline constexpr const char* trays                 = "trays";
} // namespace unit

// Mirrors DevAmsTray.
namespace tray {
    inline constexpr const char* ams_id      = "ams_id";
    inline constexpr const char* slot_id     = "slot_id";
    inline constexpr const char* tray_label  = "tray_label";
    inline constexpr const char* is_exists   = "is_exists";
    inline constexpr const char* fila_type   = "fila_type";
    inline constexpr const char* sub_brands  = "sub_brands";
    inline constexpr const char* setting_id  = "setting_id";
    inline constexpr const char* tag_uid     = "tag_uid";
    inline constexpr const char* color       = "color";
    inline constexpr const char* colors      = "colors";
    inline constexpr const char* color_type  = "color_type";
    inline constexpr const char* remain      = "remain";
    inline constexpr const char* remain_g    = "remain_g";
    inline constexpr const char* is_bbl      = "is_bbl";
    inline constexpr const char* reading     = "reading";
    inline constexpr const char* info_ready  = "info_ready";
    inline constexpr const char* binded_extruder_ids = "binded_extruder_ids";
    inline constexpr const char* current_extruder_id = "current_extruder_id";
    inline constexpr const char* switcher_port       = "switcher_port";
} // namespace tray

namespace values {
    namespace humidity_display_type {
        inline constexpr const char* none    = "none";
        inline constexpr const char* level   = "level";
        inline constexpr const char* percent = "percent";
    } // namespace humidity_display_type

    // DevFilaSwitch::CaliStatus equivalent.
    namespace fila_switch_cali_status {
        inline constexpr const char* idle     = "idle";
        inline constexpr const char* stepping = "stepping";
        inline constexpr const char* unknown  = "unknown";
    } // namespace fila_switch_cali_status

    // DevFilaSwitch::SwitchPos equivalent.
    namespace switcher_port {
        inline constexpr const char* a = "a";
        inline constexpr const char* b = "b";
    } // namespace switcher_port
} // namespace values

namespace format {

struct Tray
{
    std::string              ams_id;
    std::string              slot_id;
    std::string              tray_label;
    std::string              fila_type;
    std::string              sub_brands;
    std::string              setting_id;
    std::string              tag_uid;
    std::string              color      = "#D9D9D9";
    std::vector<std::string> colors;
    int                      color_type = 2; // DevFilaColorType: 0 gradient, 1 multi, 2 single
    int                      remain     = 100;
    int                      remain_g   = -1;

    bool                     is_bbl     = false;
    bool                     reading    = false;
    bool                     is_exists  = false;
    bool                     info_ready = false;

    int                      current_extruder_id = -1;
    std::vector<int>         binded_extruder_ids;
    std::string              switcher_port;
};

struct Unit
{
    std::string       ams_id;
    int               ams_type              = 1; // DevAmsType
    std::string       ams_type_name         = "AMS";
    bool              is_ams_lite_mixed     = false;
    int               humidity_level        = -1;
    int               humidity_percent      = -1;
    std::string       humidity_display_type = values::humidity_display_type::none;
    std::vector<int>  binded_extruder_ids;
    std::string       switcher_port;
    std::vector<Tray> trays;
};

struct FilaSwitchPort
{
    int         has_filament = -1; // tri-state: -1 unknown, 0 no, 1 yes
    std::string ams_id;
    std::string slot_id;
};

struct FilaSwitchData
{
    bool           installed        = false;
    bool           ready            = false;
    int            cali_status      = -1;
    std::string    cali_status_name = values::fila_switch_cali_status::unknown;
    FilaSwitchPort in_a;
    FilaSwitchPort in_b;
    int            out_a_extruder_id = -1;
    int            out_b_extruder_id = -1;
};

struct AmsListData
{
    std::string       selected_dev_id;
    std::vector<Unit> ams_units;
    std::vector<Tray> ext_slots;
    FilaSwitchData    fila_switch;
    bool              detect_remain_enabled = false;// with detection off the reported remain is meaningless
};

inline void to_json(nlohmann::json& j, const Tray& v)
{
    j = {
        {tray::ams_id,     v.ams_id},
        {tray::slot_id,    v.slot_id},
        {tray::tray_label, v.tray_label},
        {tray::is_exists,  v.is_exists},
        {tray::fila_type,  v.fila_type},
        {tray::sub_brands, v.sub_brands},
        {tray::setting_id, v.setting_id},
        {tray::tag_uid,    v.tag_uid},
        {tray::color,      v.color},
        {tray::colors,     v.colors},
        {tray::color_type, v.color_type},
        {tray::remain,     v.remain},
        {tray::remain_g,   v.remain_g},
        {tray::is_bbl,     v.is_bbl},
        {tray::reading,    v.reading},
        {tray::info_ready, v.info_ready},
        {tray::binded_extruder_ids, v.binded_extruder_ids},
        {tray::current_extruder_id, v.current_extruder_id},
        {tray::switcher_port,       v.switcher_port},
    };
}

inline void to_json(nlohmann::json& j, const Unit& v)
{
    j = {
        {unit::ams_id,                v.ams_id},
        {unit::ams_type,              v.ams_type},
        {unit::ams_type_name,         v.ams_type_name},
        {unit::is_ams_lite_mixed,     v.is_ams_lite_mixed},
        {unit::humidity_level,        v.humidity_level},
        {unit::humidity_percent,      v.humidity_percent},
        {unit::humidity_display_type, v.humidity_display_type},
        {unit::binded_extruder_ids,   v.binded_extruder_ids},
        {unit::switcher_port,         v.switcher_port},
        {unit::trays,                 v.trays},
    };
}

inline void to_json(nlohmann::json& j, const FilaSwitchPort& v)
{
    j = {
        {fila_switch_port::has_filament, v.has_filament},
        {fila_switch_port::ams_id,       v.ams_id},
        {fila_switch_port::slot_id,      v.slot_id},
    };
}

inline void to_json(nlohmann::json& j, const FilaSwitchData& v)
{
    j = {
        {fila_switch::installed,         v.installed},
        {fila_switch::ready,             v.ready},
        {fila_switch::cali_status,       v.cali_status},
        {fila_switch::cali_status_name,  v.cali_status_name},
        {fila_switch::in_a,              v.in_a},
        {fila_switch::in_b,              v.in_b},
        {fila_switch::out_a_extruder_id, v.out_a_extruder_id},
        {fila_switch::out_b_extruder_id, v.out_b_extruder_id},
    };
}

inline void to_json(nlohmann::json& j, const AmsListData& v)
{
    j = {
        {data::selected_dev_id,       v.selected_dev_id},
        {data::ams_units,             v.ams_units},
        {data::ext_slots,             v.ext_slots},
        {data::fila_switch,           v.fila_switch},
        {data::detect_remain_enabled, v.detect_remain_enabled},
    };
}

} // namespace format

}}} // namespace Slic3r::GUI::AmsControlWebSchema

#endif // AMSCONTROLWEBSCHEMA_DATA_HPP
