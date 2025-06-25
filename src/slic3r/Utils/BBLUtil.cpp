#include "BBLUtil.hpp"
#include "libslic3r_version.h"

using namespace std;

#if BBL_RELEASE_TO_PUBLIC
static bool s_enable_cross_talk = true;
#else
static bool s_enable_cross_talk = true;
#endif

namespace Slic3r
{

std::string BBLCrossTalk::Crosstalk_DevId(const std::string& dev_id)
{
    if (!s_enable_cross_talk) { return dev_id; }
    if (dev_id.size() > 6)
    {
        const string& cs_devid = dev_id.substr(0, 3) + std::string(dev_id.size() - 6, '*') + dev_id.substr(dev_id.size() - 3, 3);
        return cs_devid;
    }

    return dev_id;
}

std::string BBLCrossTalk::Crosstalk_DevIP(const std::string& str)
{
    if (!s_enable_cross_talk) { return str; }

    std::string format_ip = str;
    size_t pos_st = 0;
    size_t pos_en = 0;

    for (int i = 0; i < 2; i++) {
        pos_en = format_ip.find('.', pos_st + 1);
        if (pos_en == std::string::npos) {
            return str;
        }
        format_ip.replace(pos_st, pos_en - pos_st, "***");
        pos_st = pos_en + 1;
    }

    return format_ip;
}

// TODO
std::string BBLCrossTalk::Crosstalk_JsonLog(const nlohmann::json& json)
{
    if (!s_enable_cross_talk) { return json.dump(1); }// Return the original JSON string if cross-talk is disabled 

    nlohmann::json copied_json = json;
    std::vector<nlohmann::json*> to_traverse_jsons {&copied_json};
    while (!to_traverse_jsons.empty())
    {
        nlohmann::json* json = to_traverse_jsons.back();
        to_traverse_jsons.pop_back();

        for (auto& item : json->items())
        {
            const std::string& key_str = item.key();
            if (item.value().is_string())
            {
                if (key_str.find("dev_id") != string::npos)
                {
                    item.value() = Crosstalk_DevId(item.value().get<std::string>());
                }
                else if (key_str.find("url") != string::npos)
                {
                    item.value() = "url.******";
                }
            }
            else if (item.value().is_object())
            {
                to_traverse_jsons.push_back(&item.value());
            }
        }
    }

#if BBL_RELEASE_TO_PUBLIC
    return json.dump();
#else
    return json.dump(1);
#endif
}

}// End of namespace Slic3r

