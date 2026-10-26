#pragma once

#include <algorithm>
#include <optional>
#include <wx/string.h>
#include "slic3r/Utils/json_diff.hpp"

namespace Slic3r
{
class MachineObject;
class DevFilaBlacklist
{
public:
    struct CheckFilamentInfo
    {
        std::string dev_id;
        std::string model_id;

        std::string fila_id;
        std::string fila_type;
        std::string fila_name;
        std::string fila_vendor;

        std::string calib_mode;
        bool has_filament_switch = false;

        std::optional<bool> used_for_print_support;// optional
        std::optional<bool> used_for_print_object;// optional

        int ams_id;
        int slot_id;

        std::optional<int> extruder_id;// optional
        std::optional<std::string> nozzle_flow;// optional
        std::optional<float> nozzle_diameter;// optional
        std::optional<int> ams_type;// optional, DevAmsType id of the slot's AMS
    };

    // Severity of a matched item, from the strongest to the weakest:
    // prohibition blocks the print, critical warning and warning only inform the user.
    static constexpr const char* ACTION_PROHIBITION      = "prohibition";
    static constexpr const char* ACTION_CRITICAL_WARNING = "critical_warning";
    static constexpr const char* ACTION_WARNING          = "warning";

    // Scenes that can show a matched item. "pre_check" is the send-print page,
    // "double_check" is the confirm dialog opened from that page.
    static constexpr const char* SCENE_PRE_CHECK    = "pre_check";
    static constexpr const char* SCENE_DOUBLE_CHECK = "double_check";

    // Severity of an action, the greater the stricter. Used to pick a verdict when the preset and
    // the blacklist both judge the same filament.
    static int GetSeverity(const std::string& action)
    {
        if (action == ACTION_PROHIBITION) { return 3; }
        if (action == ACTION_CRITICAL_WARNING) { return 2; }
        if (action == ACTION_WARNING) { return 1; }
        return 0;
    }

    struct CheckResultItem
    {
        std::string action;// prohibition/critical_warning/warning
        wxString    info_msg;
        wxString    wiki_url;
        std::string group;
        int         priority = 0;
        // Scenes listed here hide this item. An absent hide_in shows it in every scene.
        std::vector<std::string> hide_in;

        bool IsHiddenIn(const char* scene) const
        {
            return std::find(hide_in.begin(), hide_in.end(), scene) != hide_in.end();
        }
        bool IsProhibition() const { return action == ACTION_PROHIBITION; }
        bool IsCriticalWarning() const { return action == ACTION_CRITICAL_WARNING; }
        bool IsWarning() const { return action == ACTION_WARNING; }
        int  GetSeverity() const { return DevFilaBlacklist::GetSeverity(action); }
    };

    struct CheckResult
    {
        std::map<std::string, std::vector<CheckResultItem>> action_items;
        std::vector<CheckResultItem> get_items_by_action(const std::string& action) const
        {
            auto it = action_items.find(action);
            if (it != action_items.end()) {
                return it->second;
            }
            return std::vector<CheckResultItem>();
        }

        std::vector<CheckResultItem> get_prohibition_items() const { return get_items_by_action(ACTION_PROHIBITION); }
        std::vector<CheckResultItem> get_critical_items() const { return get_items_by_action(ACTION_CRITICAL_WARNING); }
        std::vector<CheckResultItem> get_warning_items() const { return get_items_by_action(ACTION_WARNING); }

        bool empty() const { return action_items.empty(); }
        int  GetMaxSeverity() const
        {
            int severity = 0;
            for (const auto& action_pair : action_items) {
                if (!action_pair.second.empty()) {
                    severity = std::max(severity, DevFilaBlacklist::GetSeverity(action_pair.first));
                }
            }

            return severity;
        }
    };

public:
    static bool load_filaments_blacklist_config();
    static CheckResult check_filaments_in_blacklist(const CheckFilamentInfo& info);

public:
    static json filaments_blacklist;
};// class DevFilaBlacklist

}// namespace Slic3r