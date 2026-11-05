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

namespace {
// Field names in device and cloud json that have no log_key of their own.
constexpr std::string_view k_ip             = "ip";
constexpr std::string_view k_user_id        = "user_id";
constexpr std::string_view k_passwd         = "passwd";
constexpr std::string_view k_channel_name   = "channel_name";
constexpr std::string_view k_model_id_camel = "modelId";

// Written instead of a json that could not be masked, so that no partly masked dump reaches the log.
constexpr std::string_view k_masking_failed = "<json not logged: masking failed>";

// Dumps without throwing: invalid UTF-8 in strings is replaced rather than raising type_error.
std::string dump_for_log(const nlohmann::json& json, int indent)
{
    return json.dump(indent, ' ', false, nlohmann::json::error_handler_t::replace);
}
} // namespace

std::string Desensitize::json_log(const nlohmann::json& json, int indent)
{
    if (!s_enable_cross_talk) { return dump_for_log(json, indent); }// Return the original JSON string if cross-talk is disabled 

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
                    // Registered fields first, by exact name; the rules below catch other spellings by substring.
                    if (auto by_key = mask_by_key(key_str, iter.value().get<std::string>()))
                    {
                        iter.value() = *by_key;
                    }
                    else if (key_str.find(log_key::dev_id) != std::string::npos || key_str.find(log_key::sn) != std::string::npos)
                    {
                        iter.value() = dev_id(iter.value().get<std::string>());
                    }
                    else if (key_str.find(log_key::dev_name) != std::string::npos)
                    {
                        iter.value() = dev_name(iter.value().get<std::string>());
                    }
                    // "ip" and "user_id" are matched whole: as substrings they would also hit ipcam/description and tag_uid/tray_uuid
                    else if (key_str.find(log_key::dev_ip) != std::string::npos || key_str == k_ip)
                    {
                        iter.value() = dev_ip(iter.value().get<std::string>());
                    }
                    else if (key_str == k_user_id)
                    {
                        iter.value() = user_id(iter.value().get<std::string>());
                    }
                    else if (key_str.find(log_key::password) != std::string::npos || key_str.find(k_passwd) != std::string::npos)
                    {
                        iter.value() = "******";
                    }
                    else if (key_str.find(log_key::access_code)!= std::string::npos)
                    {
                        iter.value() = "******";
                    }
                    else if (key_str.find(log_key::url) != std::string::npos)
                    {
                        iter.value() = "******";
                    }
                    else if (key_str.find(k_channel_name) != std::string::npos)
                    {
                        iter.value() = channel_name(iter.value().get<std::string>());
                    }
                    else if (key_str.find(log_key::ttcode_enc) != std::string::npos)
                    {
                        iter.value() = "******";
                    }
                    else if (key_str.find(log_key::authkey) != std::string::npos)
                    {
                        iter.value() = "******";
                    }
                    else if (key_str == log_key::uid)
                    {
                        iter.value() = user_id(iter.value().get<std::string>());
                    }
                    else if (key_str.find(log_key::region) != std::string::npos)
                    {
                        iter.value() = "******";
                    }
                    else if (key_str.find(log_key::token) != std::string::npos)
                    {
                        iter.value() = "******";
                    }
                    else if(key_str.find(k_model_id_camel) != std::string::npos || key_str.find(log_key::model_id) != std::string::npos)
                    {
                        iter.value() = "******";
                    }
                }
                else if (iter.value().is_number())
                {
                    if (key_str == log_key::uid || key_str == k_user_id) {
                        iter.value() = user_id(iter.value().dump());
                    } else if (key_str == k_ip) {
                        // the printer reports its IP as a number; there is no dotted form to keep part of
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
                        if (array_item.is_object() || array_item.is_array()) {
                            to_traverse_jsons.push_back(&array_item);
                        } else if (array_item.is_string()) {
                            try {
                                const auto& string_val = array_item.get<std::string>();
                                if (!string_val.empty()) {
                                    const auto& sub_json = nlohmann::json::parse(string_val);
                                    array_item = json_log(sub_json, indent);
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
        return std::string(k_masking_failed);
    }
    catch (const std::exception& e)
    {
        BOOST_LOG_TRIVIAL(info) << "Error processing JSON for cross-talk: " << e.what();
        return std::string(k_masking_failed);
    }
    catch (...)
    {
        BOOST_LOG_TRIVIAL(info) << "Error processing JSON for cross-talk";
        return std::string(k_masking_failed);
    };

    return dump_for_log(copied_json, indent);
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

namespace {
using MaskFn = std::string (*)(const std::string&);

// Registered sensitive fields: field name -> mask function. Used for KV keys and, by exact name,
// for json fields. constexpr so it is ready before any static initializer can log.
constexpr std::pair<std::string_view, MaskFn> k_masks[] = {
    {log_key::dev_id,      &Desensitize::dev_id},
    {log_key::sn,          &Desensitize::dev_id},
    {log_key::dev_ip,      &Desensitize::dev_ip},
    {log_key::dev_name,    &Desensitize::dev_name},
    {log_key::uid,         &Desensitize::user_id},
    {log_key::access_code, &Desensitize::mask_all},
    {log_key::token,       &Desensitize::mask_all},
    {log_key::password,    &Desensitize::mask_all},
    {log_key::authkey,     &Desensitize::mask_all},
    {log_key::ttcode_enc,  &Desensitize::mask_all},
    {log_key::url,         &Desensitize::mask_all},
    {log_key::region,      &Desensitize::mask_all},
    {log_key::model_id,    &Desensitize::mask_all},
};
} // namespace

std::optional<std::string> Desensitize::mask_by_key(std::string_view key, const std::string& value)
{
    for (const auto& [name, mask] : k_masks)
        if (name == key)
            return mask(value);
    return std::nullopt;
}

} // namespace Slic3r
