#pragma once

#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "LogKeys.hpp"
#include "nlohmann/json_fwd.hpp"

namespace Slic3r {

/** \brief Log severity, ordered from the most verbose to the most severe */
enum class LogLevel : int { trace = 0, debug = 1, info = 2, warning = 3, error = 4, fatal = 5 };

/** \brief Options for encrypting the log file */
struct LogEncOptions
{
    enum LogEncType : unsigned char
    {
        LOG_ENC_NONE = 0,
        LOG_ENC_AES_256_CBC = 1,
        //ENC_RSA_2048 = 2, maybe supported in future
    };

    LogEncType enc_type = LOG_ENC_AES_256_CBC;
    std::string enc_key_url;
    std::string enc_key_host_env;
};

/**
 * \brief Set the logging level
 *
 * \param level 0 fatal, 1 error, 2 warning, 3 info, 4 debug, 5 and above trace
 */
void         set_logging_level(unsigned int level);
/** \brief Current logging level, numbered as in set_logging_level */
unsigned     get_logging_level();
/** \brief Convert a level name ("fatal" ... "trace") to its number; unknown names give 0 */
unsigned int level_string_to_boost(const std::string& level);
/** \brief Convert a level number to its name; out of range gives "error" */
std::string  get_string_logging_level(unsigned level);

/** \brief Whether the log file has been set up */
bool is_log_trivial_valid();
/**
 * \brief Start writing logs to a new file in log_dir and set the logging level
 *
 * The file is named after the start time and process id; log_dir is created if missing.
 */
void set_log_path_and_level(const std::string& log_dir, unsigned int level, const LogEncOptions& enc_options);
/** \brief Apply new encryption options; logging continues in a new file in the same folder */
void update_log_sink(const LogEncOptions& enc_options);
/** \brief Write out buffered log records */
void flush_logs();

/**
 * \brief Message of the exception being handled, for logging it in a catch block
 *
 * Gives what() for a std::exception and "unknown exception" for anything else; outside a
 * catch block, where no exception is being handled, gives "no exception".
 */
std::string current_exception_message();
} // namespace Slic3r

namespace Slic3r::log_detail {

/**
 * \brief Whether a record of the given level passes the current logging level
 *
 * Used by the logging macros; not meant to be called directly.
 */
bool log_enabled(LogLevel level);

/**
 * \brief Whether T holds a small number in a char-sized type: signed/unsigned char or an enum based on one
 *
 * std::ostream writes these as characters, so the number 0 would come out as a NUL byte.
 */
template<class T>
constexpr bool is_char_number()
{
    if constexpr (std::is_enum_v<T>)
        return is_char_number<std::underlying_type_t<T>>() || std::is_same_v<std::underlying_type_t<T>, char>;
    else
        return std::is_same_v<T, signed char> || std::is_same_v<T, unsigned char>;
}

/**
 * \brief Whether T has ToUTF8(); in practice this is wxString
 *
 * wxString's own operator<< converts to the system code page (GBK on Chinese Windows), but log files
 * are UTF-8. The type is detected by its ToUTF8() member because this header must not depend on wx.
 */
template<class T, class = void>
struct has_to_utf8 : std::false_type {};
template<class T>
struct has_to_utf8<T, std::void_t<decltype(std::declval<const T&>().ToUTF8())>> : std::true_type {};

/**
 * \brief Write a json with its sensitive fields masked, on one line
 *
 * Used by write_value; not meant to be called directly. Never throws.
 */
void write_json(std::ostream& os, const nlohmann::json& value);

/**
 * \brief Write a value to a log stream
 *
 * A null char pointer is written as "(null)", char-sized numbers are written as numbers, a json has
 * its sensitive fields masked, and strings with ToUTF8() are written as UTF-8. Plain char is written
 * as a character.
 */
template<class T>
void write_value(std::ostream& os, const T& value)
{
    if constexpr (std::is_pointer_v<T> && std::is_same_v<std::remove_cv_t<std::remove_pointer_t<T>>, char>) {
        if (value == nullptr) {
            os << "(null)";
            return;
        }
        os << value;
    } else if constexpr (is_char_number<T>()) {
        os << static_cast<int>(value);
    } else if constexpr (std::is_same_v<T, nlohmann::json>) {
        write_json(os, value);
    } else if constexpr (has_to_utf8<T>::value) {
        os << value.ToUTF8().data();
    } else {
        os << value;
    }
}

/**
 * \brief Write a placeholder for a value that could not be written; never throws
 *
 * Used by LogStream and KV; not meant to be called directly.
 */
void write_failed(std::ostream& os) noexcept;

/**
 * \brief "key=<value not logged> " for a value that could not be written; never throws
 *
 * Used by KV; not meant to be called directly.
 */
std::string kv_failed(std::string_view key) noexcept;

/**
 * \brief Collects one log record and writes it when the statement ends
 *
 * Created only by the logging macros; not meant to be used directly.
 */
class LogStream
{
public:
    LogStream(LogLevel level, const char* file, int line, const char* func);
    ~LogStream();

    LogStream(const LogStream&)            = delete;
    LogStream& operator=(const LogStream&) = delete;

    template<class T>
    LogStream& operator<<(const T& value)
    {
        // A value whose own operator<< throws must not break the code that logs it.
        try {
            write_value(m_buf, value);
        } catch (...) {
            write_failed(m_buf);
        }
        return *this;
    }

    LogStream& operator<<(std::ostream& (*manip)(std::ostream&));

private:
    LogLevel           m_level;
    std::ostringstream m_buf;
};

/**
 * \brief Build "key=value " for KV
 *
 * Used by KV; not meant to be called directly.
 */
std::string format_kv(std::string_view key, const std::string& value);

} // namespace Slic3r::log_detail

namespace Slic3r {

/**
 * \brief Format a named value for a log record as "key=value "
 *
 * Values of sensitive keys are masked in public builds, and values containing spaces or
 * quotes are quoted. Pass the raw value, not an already masked one.
 *
 * \param key   field name, preferably a log_key constant
 * \param value anything that can be written to a std::ostream
 */
template<class T>
std::string KV(std::string_view key, const T& value)
{
    // Evaluated inside log statements, so a failure must not reach the caller.
    try {
        std::ostringstream s;
        s << std::boolalpha;
        log_detail::write_value(s, value);
        return log_detail::format_kv(key, s.str());
    } catch (...) {
        return log_detail::kv_failed(key);
    }
}

} // namespace Slic3r

// The statement after the macro is evaluated only when the level passes, so the
// streamed expressions cost nothing for filtered-out records.
#undef BOOST_LOG_TRIVIAL
#define BOOST_LOG_TRIVIAL(lvl)                                                                      \
    for (bool slic3r_log_on_ = ::Slic3r::log_detail::log_enabled(::Slic3r::LogLevel::lvl);           \
         slic3r_log_on_; slic3r_log_on_ = false)                                                     \
        ::Slic3r::log_detail::LogStream(::Slic3r::LogLevel::lvl, __FILE__, __LINE__, __func__)

// Occupy the include guard of <boost/log/trivial.hpp>, so including it after this header is a
// no-op and cannot put boost's BOOST_LOG_TRIVIAL back. Log.cpp checks the guard name.
#ifndef BOOST_LOG_TRIVIAL_HPP_INCLUDED_
#define BOOST_LOG_TRIVIAL_HPP_INCLUDED_
#endif
