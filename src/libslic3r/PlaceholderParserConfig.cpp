#include "PlaceholderParser.hpp"

#include <cstring>
#include <ctime>
#include <iomanip>
#include <sstream>

#ifdef _MSC_VER
    #include <stdlib.h>
#else
    #include <unistd.h>
#endif

#ifdef __APPLE__
#include <crt_externs.h>
#undef environ
#define environ (*_NSGetEnviron())
#else
    #ifdef _MSC_VER
       #define environ _environ
    #else
        extern char **environ;
    #endif
#endif

namespace Slic3r {

PlaceholderParser::PlaceholderParser(const DynamicConfig *external_config) : m_external_config(external_config)
{
    this->set("version", std::string(SLIC3R_VERSION));
    this->apply_env_variables();
    this->update_timestamp();
}

void PlaceholderParser::update_timestamp(DynamicConfig &config)
{
    time_t     rawtime;
    time(&rawtime);
    struct tm *timeinfo = localtime(&rawtime);

    {
        std::ostringstream ss;
        ss << (1900 + timeinfo->tm_year);
        ss << std::setw(2) << std::setfill('0') << (1 + timeinfo->tm_mon);
        ss << std::setw(2) << std::setfill('0') << timeinfo->tm_mday;
        ss << "-";
        ss << std::setw(2) << std::setfill('0') << timeinfo->tm_hour;
        ss << std::setw(2) << std::setfill('0') << timeinfo->tm_min;
        ss << std::setw(2) << std::setfill('0') << timeinfo->tm_sec;
        config.set_key_value("timestamp", new ConfigOptionString(ss.str()));
    }
    config.set_key_value("year", new ConfigOptionInt(1900 + timeinfo->tm_year));
    config.set_key_value("month", new ConfigOptionInt(1 + timeinfo->tm_mon));
    config.set_key_value("day", new ConfigOptionInt(timeinfo->tm_mday));
    config.set_key_value("hour", new ConfigOptionInt(timeinfo->tm_hour));
    config.set_key_value("minute", new ConfigOptionInt(timeinfo->tm_min));
    config.set_key_value("second", new ConfigOptionInt(timeinfo->tm_sec));
}

static inline bool opts_equal(const DynamicConfig &config_old, const DynamicConfig &config_new, const std::string &opt_key)
{
    const ConfigOption *opt_old = config_old.option(opt_key);
    const ConfigOption *opt_new = config_new.option(opt_key);
    assert(opt_new != nullptr);
    return opt_old != nullptr && *opt_new == *opt_old;
}

std::vector<std::string> PlaceholderParser::config_diff(const DynamicPrintConfig &rhs)
{
    std::vector<std::string> diff_keys;
    for (const t_config_option_key &opt_key : rhs.keys())
        if (!opts_equal(m_config, rhs, opt_key))
            diff_keys.emplace_back(opt_key);
    return diff_keys;
}

bool PlaceholderParser::apply_config(const DynamicPrintConfig &rhs)
{
    bool modified = false;
    for (const t_config_option_key &opt_key : rhs.keys()) {
        if (!opts_equal(m_config, rhs, opt_key)) {
            this->set(opt_key, rhs.option(opt_key)->clone());
            modified = true;
        }
    }
    return modified;
}

void PlaceholderParser::apply_only(const DynamicPrintConfig &rhs, const std::vector<std::string> &keys)
{
    for (const t_config_option_key &opt_key : keys)
        this->set(opt_key, rhs.option(opt_key)->clone());
}

void PlaceholderParser::apply_config(DynamicPrintConfig &&rhs)
{
    m_config += std::move(rhs);
}

void PlaceholderParser::apply_env_variables()
{
    for (char **env = environ; *env; ++env) {
        if (strncmp(*env, "SLIC3R_", 7) == 0) {
            std::stringstream ss(*env);
            std::string       key, value;
            std::getline(ss, key, '=');
            ss >> value;
            this->set(key, value);
        }
    }
}

} // namespace Slic3r
