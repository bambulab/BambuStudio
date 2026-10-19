// boost/log/trivial.hpp defines BOOST_LOG_TRIVIAL unconditionally, so it goes before Log.hpp
// (also reached through LogSink.hpp) for our definition to be the one in effect.
#include <boost/make_shared.hpp>
#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/support/date_time.hpp>

// Log.hpp defines this guard to keep boost's BOOST_LOG_TRIVIAL out; fail loudly if boost renames it.
#ifndef BOOST_LOG_TRIVIAL_HPP_INCLUDED_
#error "<boost/log/trivial.hpp> no longer uses BOOST_LOG_TRIVIAL_HPP_INCLUDED_; update Log.hpp"
#endif

#include "Log.hpp"
#include "LogSink.hpp"
#include "Desensitize.hpp"

#include <atomic>
#include <exception>
#include <iomanip>
#include <string>
#include <string_view>
#include <utility>

#ifdef __APPLE__
#include "MacUtils.hpp"
#endif

namespace Slic3r {

static_assert(static_cast<int>(LogLevel::trace)   == static_cast<int>(boost::log::trivial::trace) &&
              static_cast<int>(LogLevel::debug)   == static_cast<int>(boost::log::trivial::debug) &&
              static_cast<int>(LogLevel::info)    == static_cast<int>(boost::log::trivial::info) &&
              static_cast<int>(LogLevel::warning) == static_cast<int>(boost::log::trivial::warning) &&
              static_cast<int>(LogLevel::error)   == static_cast<int>(boost::log::trivial::error) &&
              static_cast<int>(LogLevel::fatal)   == static_cast<int>(boost::log::trivial::fatal),
              "LogLevel must match boost::log::trivial::severity_level");

// Single source of the current level, read by the macros and the core filter from any thread.
static std::atomic<boost::log::trivial::severity_level> logSeverity{boost::log::trivial::error};

static boost::log::trivial::severity_level level_to_boost(unsigned level)
{
    switch (level) {
    // Report fatal errors only.
    case 0: return boost::log::trivial::fatal;
    // Report fatal errors and errors.
    case 1: return boost::log::trivial::error;
    // Report fatal errors, errors and warnings.
    case 2: return boost::log::trivial::warning;
    // Report all errors, warnings and infos.
    case 3: return boost::log::trivial::info;
    // Report all errors, warnings, infos and debugging.
    case 4: return boost::log::trivial::debug;
    // Report everything including fine level tracing information.
    default: return boost::log::trivial::trace;
    }
}

void set_logging_level(unsigned int level)
{
    logSeverity.store(level_to_boost(level), std::memory_order_relaxed);
}

unsigned int level_string_to_boost(const std::string& level)
{
    static constexpr std::pair<std::string_view, unsigned int> k_levels[] = {
        {"fatal", 0}, {"error", 1}, {"warning", 2}, {"info", 3}, {"debug", 4}, {"trace", 5}};
    for (const auto& [name, value] : k_levels)
        if (name == level)
            return value;
    return 0;
}

std::string get_string_logging_level(unsigned level)
{
    switch (level) {
    case 0: return "fatal";
    case 1: return "error";
    case 2: return "warning";
    case 3: return "info";
    case 4: return "debug";
    case 5: return "trace";
    default: return "error";
    }
}

unsigned get_logging_level()
{
    switch (logSeverity.load(std::memory_order_relaxed)) {
    case boost::log::trivial::fatal : return 0;
    case boost::log::trivial::error : return 1;
    case boost::log::trivial::warning : return 2;
    case boost::log::trivial::info : return 3;
    case boost::log::trivial::debug : return 4;
    case boost::log::trivial::trace : return 5;
    default: return 1;
    }
}

// Force set_logging_level(<=error) after loading of the DLL.
// This is currently only needed if libslic3r is loaded as a shared library into Perl interpreter
// to perform unit and integration tests.
static struct RunOnInit {
    RunOnInit() {
        // Installed once and evaluated per record, so changing the level needs no new filter.
        // Same check as the logging macros, which also covers records not made through them.
        boost::log::core::get()->set_filter([](const boost::log::attribute_value_set& attrs) {
            const auto severity = attrs[boost::log::trivial::severity];
            return severity && log_detail::log_enabled(static_cast<LogLevel>(severity.get()));
        });
        set_logging_level(1);

    }
} g_RunOnInit;

namespace {
boost::shared_ptr<LogSink> g_log_sink;
boost::shared_ptr<LogSinkBackend> g_log_sink_backend;
// Folder given to set_log_path_and_level; later files go to the same place.
std::string g_log_dir;
} // namespace

bool is_log_trivial_valid()
{
    return g_log_sink != nullptr;
}

namespace logging = boost::log;
namespace src = boost::log::sources;
namespace expr = boost::log::expressions;
namespace keywords = boost::log::keywords;
namespace attrs = boost::log::attributes;
void set_log_path_and_level(const std::string& log_dir, unsigned int level, const LogEncOptions& enc_options)
{
    g_log_dir = log_dir;
    // Also creates the log folder, so it runs before the early return below.
    const std::string file = LogSinkUtil::get_log_filename_format(g_log_dir, enc_options);
    g_log_sink_backend     = boost::make_shared<LogSinkBackend>(file, enc_options);
    g_log_sink = boost::make_shared<LogSink>(g_log_sink_backend);
	g_log_sink->set_formatter(
		expr::stream
		<< expr::format_date_time< boost::posix_time::ptime >("TimeStamp", "%Y-%m-%d %H:%M:%S.%f")
		<< " [" << expr::attr<attrs::current_thread_id::value_type>("ThreadID") << "]"
		// padded to the longest name, "warning", so the columns line up
		<< " [" << std::left << std::setw(7) << boost::log::trivial::severity << "] "
		<< expr::smessage
	);
	boost::log::core::get()->add_sink(g_log_sink);

	logging::add_common_attributes();

	set_logging_level(level);
	return;
}

void update_log_sink(const LogEncOptions& enc_options)
{
    if (g_log_sink_backend) {
        const std::string file = LogSinkUtil::get_log_filename_format(g_log_dir, enc_options);
        g_log_sink_backend->update_enc_option(file, enc_options);
    }
}

std::string current_exception_message()
{
    // A bare throw with no exception being handled would terminate the program.
    if (!std::current_exception())
        return "no exception";
    try {
        throw;
    } catch (const std::exception& e) {
        return e.what();
    } catch (...) {
        return "unknown exception";
    }
}

void flush_logs()
{
	if (g_log_sink)
		g_log_sink->flush();

	return;
}

} // namespace Slic3r

namespace Slic3r::log_detail {

namespace {
// Keeps only the file name of __FILE__; the build directory layout is not useful in the log.
const char* file_basename(const char* path)
{
    const char* name = path;
    for (const char* p = path; *p != '\0'; ++p)
        if (*p == '/' || *p == '\\')
            name = p + 1;
    return name;
}

// Fields that are not registered for masking are written as is.
std::string masked(std::string_view key, const std::string& value)
{
    return Desensitize::mask_by_key(key, value).value_or(value);
}

// Quotes values that would otherwise be ambiguous in "key=value" form: empty, or containing
// a separator, a quote or a line break.
std::string quoted_if_needed(const std::string& value)
{
    if (!value.empty() && value.find_first_of(" =\"\t\r\n") == std::string::npos)
        return value;

    std::string out;
    out.reserve(value.size() + 2);
    out += '"';
    for (char c : value) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:   out += c;
        }
    }
    out += '"';
    return out;
}
} // namespace

std::string format_kv(std::string_view key, const std::string& value)
{
    std::string out(key);
    out += '=';
    out += quoted_if_needed(masked(key, value));
    out += ' ';
    return out;
}

void write_json(std::ostream& os, const nlohmann::json& value)
{
    // json_log masks by field name in public builds; copying a large json can still run out of memory.
    try {
        os << Desensitize::json_log(value, -1);
    } catch (...) {
        write_failed(os);
    }
}

void write_failed(std::ostream& os) noexcept
{
    try {
        os << "<value not logged>";
    } catch (...) {
    }
}

std::string kv_failed(std::string_view key) noexcept
{
    try {
        return std::string(key) + "=<value not logged> ";
    } catch (...) {
        return std::string();
    }
}

bool log_enabled(LogLevel level)
{
    // A record made while this thread is writing another one would deadlock on the sink's locks.
    if (LogSinkBackend::is_consuming_on_this_thread())
        return false;
    return static_cast<boost::log::trivial::severity_level>(level) >= logSeverity.load(std::memory_order_relaxed);
}

LogStream::LogStream(LogLevel level, const char* file, int line, const char* func)
    : m_level(level)
{
    try {
        // Same as Boost.Log's record stream, so bools keep printing as true/false.
        m_buf.setf(std::ios_base::boolalpha);
        // The location leads the message; records from code outside our macro simply have none.
        m_buf << '[' << file_basename(file) << ':' << line << ' ' << func << "] ";
    } catch (...) {
        // Out of memory only; the record is written without its location.
    }
}

LogStream& LogStream::operator<<(std::ostream& (*manip)(std::ostream&))
{
    m_buf << manip;
    return *this;
}

LogStream::~LogStream()
{
    try {
        std::string message = m_buf.str();
        // The sink ends every record with a newline; one written by the caller would leave an empty line.
        while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
            message.pop_back();
        BOOST_LOG_STREAM_WITH_PARAMS(::boost::log::trivial::logger::get(),
                                     (::boost::log::keywords::severity = static_cast<boost::log::trivial::severity_level>(m_level)))
            << message;
    } catch (...) {
        // A destructor must not throw, and a failure to log has nowhere else to be reported.
    }
}

} // namespace Slic3r::log_detail
