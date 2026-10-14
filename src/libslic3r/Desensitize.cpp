#include "Desensitize.hpp"

#include "nlohmann/json.hpp"

#include "libslic3r_version.h"
#include "libslic3r/Log.hpp"

#if BBL_RELEASE_TO_PUBLIC
static constexpr bool s_enable_cross_talk = true;
#else
static constexpr bool s_enable_cross_talk = false;
#endif

namespace Slic3r
{

// warning: do not use KV in this function; KV calls it to mask values, so using KV here recurses
std::string Desensitize::dev_id(const std::string& str)
{
    if (!s_enable_cross_talk) { return str; }
    if (str.size() > 6)
    {
        const std::string& cs_devid = str.substr(0, 3) + std::string(str.size() - 6, '*') + str.substr(str.size() - 3, 3);
        return cs_devid;
    }

    return str;
}

// warning: do not use KV in this function; KV calls it to mask values, so using KV here recurses
std::string Desensitize::dev_ip(const std::string& str)
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

// warning: do not use KV in this function; KV calls it to mask values, so using KV here recurses
std::string Desensitize::dev_name(const std::string& str)
{
    if (!s_enable_cross_talk) { return str; }

    auto get_name = [](const std::string& name, int count) ->std::string
    {
        const std::string& cs_dev_name = std::string(name.size() - count, '*') + name.substr(name.size() - count, count);
        return cs_dev_name;
    };

    if (str.size() > 3)
    {
        return get_name(str, 3);
    }
    else if(str.size() > 2)
    {
        return get_name(str, 2);
    }
    else if(str.size() > 1)
    {
        return get_name(str, 1);
    }

    return  std::string(str.size(), '*');
}

namespace {
/** \brief Mask the device id part of "<dev_id>_<suffix>"; returns "****" when there is no suffix */
std::string channel_name(const std::string& str)
{
    if (!s_enable_cross_talk) { return str; }

    int pos = str.find("_");
    if (pos != std::string::npos && pos < str.size() - 1)
    {
        std::string cs_channel_name = Desensitize::dev_id(str.substr(0, pos))  + str.substr(pos, str.size() - pos);
        return cs_channel_name;
    }

    return "****";
}
} // namespace

std::string Desensitize::json_log(const nlohmann::json& json)
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
                    if (key_str.find("dev_id") != std::string::npos || key_str.find("sn") != std::string::npos)
                    {
                        iter.value() = dev_id(iter.value().get<std::string>());
                    }
                    else if (key_str.find("dev_name") != std::string::npos)
                    {
                        iter.value() = dev_name(iter.value().get<std::string>());
                    }
                    else if (key_str.find("access_code")!= std::string::npos)
                    {
                        iter.value() = "******";
                    }
                    else if (key_str.find("url") != std::string::npos)
                    {
                        iter.value() = "******";
                    }
                    else if (key_str.find("channel_name") != std::string::npos)
                    {
                        iter.value() = channel_name(iter.value().get<std::string>());
                    }
                    else if (key_str.find("ttcode_enc") != std::string::npos)
                    {
                        iter.value() = "******";
                    }
                    else if (key_str.find("authkey") != std::string::npos)
                    {
                        iter.value() = "******";
                    }
                    else if (key_str == "uid")
                    {
                        iter.value() = user_id(iter.value().get<std::string>());
                    }
                    else if (key_str.find("region") != std::string::npos)
                    {
                        iter.value() = "******";
                    }
                    else if (key_str.find("token") != std::string::npos)
                    {
                        iter.value() = "******";
                    }
                    else if(key_str.find("modelId") != std::string::npos || key_str.find("model_id") != std::string::npos)
                    {
                        iter.value() = "******";
                    }
                }
                else if (iter.value().is_number())
                {
                    if (key_str == "uid") {
                        iter.value() = user_id(iter.value().dump());
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
                        if (array_item.is_object() || array_item.is_array()) {
                            to_traverse_jsons.push_back(&array_item);
                        } else if (array_item.is_string()) {
                            try {
                                const auto& string_val = array_item.get<std::string>();
                                if (!string_val.empty()) {
                                    const auto& sub_json = nlohmann::json::parse(string_val);
                                    array_item = json_log(sub_json);
                                }
                            } catch (...) {
                                continue;
                            }
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

// warning: do not use KV in this function; KV calls it to mask values, so using KV here recurses
std::string Desensitize::user_id(const std::string& uid)
{
    if (!s_enable_cross_talk) { return uid; }
    if (uid.size() > 2)
    {
        const std::string& cs_uid = std::string(uid.size() - 2, '*') + uid.substr(uid.size() - 2, 2);
        return cs_uid;
    }

    return "******";
}

// warning: do not use KV in this function; KV calls it to mask values, so using KV here recurses
std::string Desensitize::mask_all(const std::string& str)
{
    if (!s_enable_cross_talk) { return str; }
    return "******";
}

} // namespace Slic3r
