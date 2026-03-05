#pragma once

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

        std::optional<bool> used_for_print_support;// optional
        std::optional<bool> used_for_print_object;// optional

        int ams_id;
        int slot_id;

        std::optional<int> extruder_id;// optional
        std::optional<std::string> nozzle_flow;// optional
        std::optional<float> nozzle_diameter;// optional
    };

    struct CheckResultItem
    {
        std::string action;
        wxString    info_msg;
        wxString    wiki_url;
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
    };

public:
    static bool load_filaments_blacklist_config();
    static CheckResult check_filaments_in_blacklist(const CheckFilamentInfo& info);

public:
    static json filaments_blacklist;
};// class DevFilaBlacklist

}// namespace Slic3r