#pragma once

#include <string>
#include <ctime>
#include <unordered_set>

#include <boost/log/sinks/basic_sink_backend.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/utility/setup.hpp>

#include "Utils.hpp"

namespace Slic3r
{

// thread-safe log sink backend with encryption support
class LogSinkBackend : public boost::log::sinks::text_file_backend
{
private:
    std::mutex m_log_mutex;

    // encryption options
    LogEncOptions m_log_enc_options;

    // encryption info
    std::string m_log_enc_key;
    std::string m_log_enc_key_iv_base; //the original iv from server
    std::string m_log_enc_key_iv;
    std::string m_log_enc_key_tag;
    time_t  m_log_enc_key_timestamp = 0;

    // record of generated log files
    std::unordered_set<std::string> m_log_files;// the generated log files

public:
    explicit LogSinkBackend(const std::string& base_path, const LogEncOptions& options);

public:
    bool update_enc_option(const std::string& base_path, const LogEncOptions& options);
    void consume(const boost::log::record_view& rec, const std::string& formatted_message);

    // Debug function, decode AES-256 encrypted log file
    static bool DecodeAES256LogFile(const std::string& file_path,
                                    const std::string& output_path,
                                    const std::string& log_enc_key,
                                    const std::string& log_enc_key_iv);

private:
    // write header to the log file
    void try_record_new_log_file(const boost::log::record_view& rec);

    // update encryption key if needed
    void update_log_enc_key();
    void get_aes_256_cbc(const LogEncOptions& enc_options,
                         std::string& key_str,
                         std::string& key_iv,
                         std::string& key_tag,
                         time_t key_time) const;

    std::string get_enc_key_type(const std::string& key_tag);
    void reset_enc_key_info();
};

class LogSinkUtil 
{
public:
    LogSinkUtil() = delete;
    static std::string get_log_filaname_format(const LogEncOptions& enc_opts);
};

typedef boost::log::sinks::synchronous_sink<LogSinkBackend> LogSink;

} // namespace Slic3r
