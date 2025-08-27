#include "BBLUtil.hpp"
#include "libslic3r_version.h"

#include <boost/log/trivial.hpp>

using namespace std;

#if BBL_RELEASE_TO_PUBLIC
static bool s_enable_cross_talk = true;
#else
static bool s_enable_cross_talk = false;
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

std::string BBLCrossTalk::Crosstalk_DevName(const std::string& dev_name)
{
    if (!s_enable_cross_talk) { return dev_name; }

    auto get_name = [](const std::string& dev_name, int count) ->std::string
    {
        const string& cs_dev_name = std::string(dev_name.size() - count, '*') + dev_name.substr(dev_name.size() - count, count);
        return cs_dev_name;
    };

    if (dev_name.size() > 3)
    {
        return get_name(dev_name, 3);
    }
    else if(dev_name.size() > 2)
    {
        return get_name(dev_name, 2);
    }
    else if(dev_name.size() > 1)
    {
        return get_name(dev_name, 1);
    }

    return  std::string(dev_name.size(), '*');
}

std::string BBLCrossTalk::Crosstalk_ChannelName(const std::string& channel_name)
{
    if (!s_enable_cross_talk) { return channel_name; }

    int pos = channel_name.find("_");
    if (pos != std::string::npos && pos < channel_name.size() - 1)
    {
        std::string cs_channel_name = Crosstalk_DevId(channel_name.substr(0, pos))  + channel_name.substr(pos, channel_name.size() - pos);
        return cs_channel_name;
    }

    return "****";
}

std::string BBLCrossTalk::Crosstalk_JsonLog(const nlohmann::json& json)
{
    if (!s_enable_cross_talk) { return json.dump(1); }// Return the original JSON string if cross-talk is disabled 

    nlohmann::json copied_json = json;

    try
    {
        std::vector<nlohmann::json*> to_traverse_jsons {&copied_json};
        while (!to_traverse_jsons.empty())
        {
            nlohmann::json* json = to_traverse_jsons.back();
            to_traverse_jsons.pop_back();

            auto iter = json->begin();
            while (iter != json->end())
            {
                const std::string& key_str = iter.key();
                if (iter.value().is_string())
                {
                    if (key_str.find("dev_id") != string::npos || key_str.find("sn") != string::npos)
                    {
                        iter.value() = Crosstalk_DevId(iter.value().get<std::string>());
                    }
                    else if (key_str.find("dev_name") != string::npos)
                    {
                        iter.value() = Crosstalk_DevName(iter.value().get<std::string>());
                    }
                    else if (key_str.find("access_code")!= string::npos)
                    {
                        iter.value() = "******";
                    }
                    else if (key_str.find("url") != string::npos)
                    {
                        iter.value() = "******";
                    }
                    else if (key_str.find("channel_name") != string::npos)
                    {
                        iter.value() = Crosstalk_ChannelName(iter.value().get<std::string>());
                    }
                    else if (key_str.find("ttcode_enc") != string::npos)
                    {
                        iter.value() = "******";
                    }
                }
                else if (iter.value().is_object())
                {
                    to_traverse_jsons.push_back(&iter.value());
                }
                else if (iter.value().is_array())
                {
                    for (auto& array_item : iter.value())
                    {
                        if (array_item.is_object() || array_item.is_array())
                        {
                            to_traverse_jsons.push_back(&array_item);
                        }
                    }
                }

                iter++;
            }
        }
    }
    catch (const nlohmann::json::exception& e)
    {
        // Handle JSON parsing exceptions if necessary
        BOOST_LOG_TRIVIAL(info) << "Error processing JSON for cross-talk: " << e.what();
        return std::string();
    }
    catch (const std::exception& e)
    {
        BOOST_LOG_TRIVIAL(info) << "Error processing JSON for cross-talk: " << e.what();
        return std::string();
    }
    catch (...)
    {
        BOOST_LOG_TRIVIAL(info) << "Error processing JSON for cross-talk";
    };

    return copied_json.dump(1);
}

}// End of namespace Slic3r

