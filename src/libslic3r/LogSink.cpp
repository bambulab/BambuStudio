#include "LogSink.hpp"

#include "libslic3r_version.h"

#include "slic3r/Utils/BBLUtil.hpp"
#include "slic3r/Utils/Http.hpp"

#include <boost/filesystem.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <nlohmann/json.hpp>
#include <openssl/aes.h>

#include <fstream>

//#define DEBUG_LOG_ENC

#define HEADER_BEGIN_MARKER "BEGIN_HEADER\n"
#define HEADER_END_MARKER   "\nEND_HEADER\n"

namespace Slic3r
{

static std::string s_message_newline = "\n";

LogSinkBackend::LogSinkBackend(const std::string& base_path, const LogEncOptions& options)
    : m_log_enc_options(options)
{
    set_file_name_pattern(base_path);
    set_rotation_size(100 * 1024 * 1024);
    set_auto_newline_mode(boost::log::sinks::disabled_auto_newline);// newline need to be added before encryption
    set_open_mode(std::ios::binary | std::ios::app);
}

void LogSinkBackend::reset_enc_key_info()
{
    m_log_enc_key.clear();
    m_log_enc_key_iv_base.clear();
    m_log_enc_key_iv.clear();
    m_log_enc_key_tag.clear();
    m_log_enc_key_timestamp = 0;
}

bool LogSinkBackend::update_enc_option(const std::string& base_path, const LogEncOptions& options)
{
    if (m_log_enc_options.enc_type == options.enc_type &&
        m_log_enc_options.enc_key_host_env == options.enc_key_host_env &&
        m_log_enc_options.enc_key_url == options.enc_key_url) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_log_mutex);

    m_log_enc_options = options;
    reset_enc_key_info();

    try {
        set_file_name_pattern(base_path);
        rotate_file();
    } catch (const std::exception& ) {
        printf("%s, error: %s\n", __FUNCTION__, "set_file_name_pattern or rotate_file exception");
    }

    return true;
}

static int s_get_enc_block_size(LogEncOptions::LogEncType enc_type)
{
    switch (enc_type) {
        case LogEncOptions::LogEncType::LOG_ENC_AES_256_CBC:
            return AES_BLOCK_SIZE;
        default:
            break;
    }

    return AES_BLOCK_SIZE;
}

void LogSinkBackend::consume(const boost::log::record_view& rec, const std::string& formatted_message)
{
#ifdef DEBUG_LOG_ENC
    static std::string s_empty_string;
    static std::string s_encrpted_string;
    s_empty_string += formatted_message;
#endif

    if (formatted_message.empty()) {
        return;
    }

    if (m_log_enc_options.enc_type == LogEncOptions::LOG_ENC_NONE) {
        return text_file_backend::consume(rec, formatted_message + s_message_newline);
    }

    std::lock_guard<std::mutex> lock(m_log_mutex);
    update_log_enc_key();// update encryption key if needed
    try_record_new_log_file(rec);// write encryption info to the log file if needed

    if (m_log_enc_key.empty()) {
        return;
    }

    /*round messsage size as blocks*/
    // newline need to be added before encryption
    int enc_block_size = s_get_enc_block_size(m_log_enc_options.enc_type);
    int pending_size = enc_block_size - (formatted_message.size() + s_message_newline.size()) % enc_block_size;
    const std::string& log_str = formatted_message + std::string(pending_size, ' ') + s_message_newline;
    size_t int_len = log_str.size();
    unsigned char* in = (unsigned char*)malloc(int_len);
    unsigned char* output = (unsigned char*)malloc(int_len);
    if (in && output) {

        switch (m_log_enc_options.enc_type) {
            case Slic3r::LogEncOptions::LogEncType::LOG_ENC_AES_256_CBC:
            {
                unsigned output_len = 0;
                memcpy((void*)in, (void*)log_str.data(), int_len);
                if (BBL_Encrypt::AES256CBC_Encrypt((unsigned char*)in, int_len, output, output_len, m_log_enc_key, m_log_enc_key_iv)) {
                    try {
                        const auto& encrypted_output = std::string(reinterpret_cast<const char*>(output), static_cast<size_t>(output_len));
                        text_file_backend::consume(rec, encrypted_output);
                        if (output_len >= AES_BLOCK_SIZE) {
                            const unsigned char* last_block = output + (output_len - AES_BLOCK_SIZE);
                            m_log_enc_key_iv.assign(reinterpret_cast<const char*>(last_block), AES_BLOCK_SIZE);
                        }

#ifdef DEBUG_LOG_ENC
                        s_encrpted_string += encrypted_output;
#endif
                    } catch (const std::exception& e) {
                        assert(false && __FUNCTION__ && e.what());
                        printf("%s, error: %s\n", __FUNCTION__, e.what());
                    } catch (...) {
                        assert(false && __FUNCTION__ && "unknown exception");
                        printf("%s, error: %s\n", __FUNCTION__, "unknown exception");
                    }
                };

                assert(output_len % AES_BLOCK_SIZE == 0);
                break;
            }
            default:
            {
                break;
            }
        }
    }

    free(output);
    free(in);

    // Debug: decode the log file for checking
#ifdef DEBUG_LOG_ENC
    if (0) {
        unsigned output_len = 0;
        int encpted_size = static_cast<int>(s_encrpted_string.size());
        unsigned char* encrpted_data = (unsigned char*)malloc(encpted_size);
        unsigned char* decrpted_output = (unsigned char*)malloc(encpted_size);
        memcpy((void*)encrpted_data, (void*)s_encrpted_string.data(), s_encrpted_string.size());
        bool ok = BBL_Encrypt::AES256CBC_Decrypt((unsigned char*)encrpted_data, encpted_size, decrpted_output, output_len, m_log_enc_key, m_log_enc_key_iv_base);
        std::string decrpted_str = std::string(reinterpret_cast<const char*>(decrpted_output), static_cast<size_t>(output_len));
    }

    if (0) {
        DecodeAES256LogFile(get_current_file_name().string(),
                            get_current_file_name().string() + ".dec",
                            m_log_enc_key,
                            m_log_enc_key_iv_base);
    }

#endif
}

// warning: do not use BOOST_LOG_TRIVIAL in this function to avoid deadlock
void LogSinkBackend::try_record_new_log_file(const boost::log::record_view& rec)
{
    // don't use const&, since the string object will destroye after function exit on MAC\Linux
    // const std::string& current_file = get_current_file_name().string();
    std::string current_file = get_current_file_name().string();
    if (current_file.empty() || m_log_files.count(current_file) == 0) {
        std::string content;
        content += HEADER_BEGIN_MARKER;
        nlohmann::json header_json;

        // app info
        header_json["app_name"] = SLIC3R_APP_NAME;
        header_json["app_version"] = SLIC3R_VERSION;
        header_json["app_build_time"] = SLIC3R_BUILD_TIME;

        // encryption info
        header_json["enc_env_name"] = m_log_enc_options.enc_key_host_env;
        header_json["enc_key_tag"] = m_log_enc_key_tag;
        header_json["enc_key_type"] = get_enc_key_type(m_log_enc_key_tag);
        switch (m_log_enc_options.enc_type) {
            case Slic3r::LogEncOptions::LOG_ENC_AES_256_CBC:
                header_json["enc_type"] = "AES_256";
                break;
            default:
                break;
        }
        header_json["enc_block_size"] = s_get_enc_block_size(m_log_enc_options.enc_type);
        header_json["enc_version"] = "1.0.0.0";

        content += header_json.dump(4);
        content += HEADER_END_MARKER;
        text_file_backend::consume(rec, content);
    }

    // don't use const&, since the string object will destroye after function exit on MAC\Linux
    // const std::string& new_current_file = get_current_file_name().string();
    std::string new_current_file = get_current_file_name().string();
    m_log_files.insert(new_current_file);
}

// warning: do not use BOOST_LOG_TRIVIAL in this function to avoid deadlock
static std::string s_generate_uuid()
{
    try {
        boost::uuids::uuid uuid = boost::uuids::random_generator()();
        return boost::uuids::to_string(uuid).c_str();

    } catch (const std::exception& e) {
        assert(false && __FUNCTION__ && e.what());
        printf("%s, error: %s\n", __FUNCTION__, e.what());
    }

    return "";
}

#define DEFAULT_KEY_TAG_CN_1 "68ba6f1721a2a225e9a499c1f73678931761106584"
#define DEFAULT_KEY_STR_CN_1 "OruMpXAHc7K8cgqLbJnRbAPOcQmFnH3J"
#define DEFAULT_KEY_IV_CN_1  "Ln2XZ0u6SLGfhftc"
#define DEFAULT_KEY_TIME_CN_1 202510221215

#define DEFAULT_KEY_TAG_US_1 "7f79c976547b46fa4e1293f78b0e94541761106519"
#define DEFAULT_KEY_STR_US_1 "tzLvjYZy8QDFqVOirPxDxEDF0yFENgl0"
#define DEFAULT_KEY_IV_US_1  "YGUgmQ9mCs5N3yqJ"
#define DEFAULT_KEY_TIME_US_1 202510221215


std::string LogSinkBackend::get_enc_key_type(const std::string& key_tag)
{
    if (key_tag.empty()) {
        return "";
    }

    static std::unordered_set<std::string> s_local_tags = {
        DEFAULT_KEY_TAG_CN_1,
        DEFAULT_KEY_TAG_US_1
    };

    return (s_local_tags.find(key_tag) != s_local_tags.end()) ? "local" : "cloud";
}

// warning: do not use BOOST_LOG_TRIVIAL in this function to avoid deadlock
void LogSinkBackend::get_aes_256_cbc(const LogEncOptions& enc_options,
                                     std::string& key_str,
                                     std::string& key_iv,
                                     std::string& key_tag,
                                     time_t key_time) const
{
    // the default key
    if (enc_options.enc_key_host_env == "cn") {
        key_tag = DEFAULT_KEY_TAG_CN_1;
        key_str = DEFAULT_KEY_STR_CN_1;
        key_iv = DEFAULT_KEY_IV_CN_1;
        key_time = DEFAULT_KEY_TIME_CN_1;
    } else {
        key_tag = DEFAULT_KEY_TAG_US_1;
        key_str = DEFAULT_KEY_STR_US_1;
        key_iv = DEFAULT_KEY_IV_US_1;
        key_time = DEFAULT_KEY_TIME_US_1;
    }

    static std::string s_uuid = s_generate_uuid();
    if (!s_uuid.empty() && !enc_options.enc_key_url.empty()) {
        const std::string& url = (boost::format("%1%?UID=%2%") % enc_options.enc_key_url % s_uuid).str();
        Slic3r::Http http = Slic3r::Http::get(url);
        http.timeout_max(6).on_complete([&key_str, &key_iv, &key_tag, &key_time](std::string body, unsigned status) {
            try {
                const nlohmann::json& recv_json = nlohmann::json::parse(body);
                if (recv_json.contains("tag")) {
                    key_tag = recv_json["tag"].get<std::string>();
                    key_iv = recv_json["iv"].get<std::string>();
                    key_str = recv_json["secret"].get<std::string>();
                    key_time = time(nullptr);
                }
            } catch (...) {
                printf("%s, error: %s\n", __FUNCTION__, "parse json error, use default key");
            }
        }).on_error([](std::string body, std::string error, unsigned status) {
            printf("%s, status = %u, error: %s, use default key\n", __FUNCTION__, status, error.c_str());
        }).perform_sync();
    };
}

// note: the enc key info only be updated when it's empty
// warning: do not use BOOST_LOG_TRIVIAL in this function to avoid deadlock
void LogSinkBackend::update_log_enc_key()
{
    // don't use const&, since the string object will destroye after function exit on MAC\Linux
    // const std::string& current_file = get_current_file_name().string();
    std::string current_file = get_current_file_name().string();
    if (current_file.empty() || m_log_files.count(current_file) == 0) {
        m_log_enc_key_iv = m_log_enc_key_iv_base; //reset iv for new file
    }

    if (m_log_enc_key.empty() || m_log_enc_key_iv.empty()) {
        switch (m_log_enc_options.enc_type) {
            case Slic3r::LogEncOptions::LOG_ENC_AES_256_CBC:
                get_aes_256_cbc(m_log_enc_options, m_log_enc_key, m_log_enc_key_iv, m_log_enc_key_tag, m_log_enc_key_timestamp);
                break;
            default:
                break;
        }

        m_log_enc_key_iv_base = m_log_enc_key_iv;
    }
}

bool LogSinkBackend::DecodeAES256LogFile(const std::string& file_path, const std::string& output_path, const std::string& log_enc_key, const std::string& log_enc_key_iv)
{
    if (log_enc_key.size() != 32 || log_enc_key_iv.size() != AES_BLOCK_SIZE) {
        return false;
    }

    // Read the whole input file
    std::ifstream ifs(file_path, std::ios::binary);
    if (!ifs) {
        return false;
    }

    ifs.seekg(0, std::ios::end);
    const std::streamoff file_size = ifs.tellg();
    ifs.seekg(0, std::ios::beg);

    if (file_size <= 0) {
        return false;
    }

    std::vector<char> file_buf(static_cast<size_t>(file_size));
    if (!ifs.read(file_buf.data(), file_size)) {
        return false;
    }

    // Try to locate the plaintext header written by try_record_new_log_file.
    size_t enc_offset = 0;
    const std::string begin_marker = HEADER_BEGIN_MARKER;
    const std::string end_marker = HEADER_END_MARKER;
    const std::string_view sv(file_buf.data(), file_buf.size());
    size_t begin_pos = sv.find(begin_marker);
    if (begin_pos != std::string_view::npos) {
        size_t json_start = begin_pos + begin_marker.size();
        size_t end_pos = sv.find(end_marker, json_start);
        if (end_pos != std::string_view::npos) {
            enc_offset = end_pos + end_marker.size();
        }
    }

    // Determine ciphertext buffer (may be entire file if header not found)
    if (enc_offset >= file_buf.size()) {
        return false;
    }

    const unsigned char* enc_ptr = reinterpret_cast<const unsigned char*>(file_buf.data() + enc_offset);
    size_t enc_len = file_buf.size() - enc_offset;
    if (enc_len == 0) {
        return false;
    }

    int aligned_len = static_cast<int>(enc_len - (enc_len % AES_BLOCK_SIZE));
    if (aligned_len != enc_len) {
        printf("%s, warning: ciphertext not aligned to block size, truncating %zu trailing bytes\n", __FUNCTION__, enc_len - aligned_len);
    }

    std::vector<unsigned char> plain(aligned_len);
    unsigned out_len = 0;
    bool ok = BBL_Encrypt::AES256CBC_Decrypt(const_cast<unsigned char*>(enc_ptr),
                                             static_cast<unsigned>(aligned_len),
                                             plain.data(), out_len,
                                             log_enc_key, log_enc_key_iv);
    if (!ok) {
        return false;
    }

    // Remove zero-padding
    std::string decoded;
    decoded.reserve(out_len);
    for (unsigned i = 0; i < out_len; ++i) {
        if (plain[i] != 0) decoded.push_back(static_cast<char>(plain[i]));
    }

    // Write decoded plaintext (without the header) to output_path
    std::ofstream ofs(output_path, std::ios::binary | std::ios::trunc);
    if (!ofs) {
        return false;
    }
    ofs.write(decoded.data(), static_cast<std::streamsize>(decoded.size()));
    if (!ofs) {
        return false;
    }

    return true;
}

// rules
// studio_%a_%b_%d_%H_%M_%S_<pid>[_enc][_cn].log.%N
std::string LogSinkUtil::get_log_filaname_format(const LogEncOptions& enc_opts)
{
    std::time_t       t = std::time(0);
    std::tm* now_time = std::localtime(&t);
    std::stringstream buf;
    buf << std::put_time(now_time, "studio_%a_%b_%d_%H_%M_%S_");
    buf << get_current_pid();
    if (enc_opts.enc_type == LogEncOptions::LOG_ENC_NONE) {
        buf << ".log.%N";
        return buf.str();
    }

    // default to us
    buf << "_enc";
    if (enc_opts.enc_key_host_env == "cn") {
        buf << "_cn";
    } else if (enc_opts.enc_key_host_env == "dc") {
        buf << "_dc"; // dc means the user doesn't set the region
    } else {
        // no suffix for "us" region
    }

    buf << ".log.%N";

    //BBS log file at C:\\Users\\[yourname]\\AppData\\Roaming\\BambuStudio\\log\\[log_filename].log
    try{
        auto log_folder = boost::filesystem::path(Slic3r::data_dir()) / "log";
        if (!boost::filesystem::exists(log_folder)) {
            boost::filesystem::create_directory(log_folder);
        }
        auto base_path = (log_folder / buf.str()).make_preferred();
        return base_path.string();
    } catch (const std::exception& e) {
        printf("%s, error: %s\n", __FUNCTION__, e.what());
    }

    return buf.str();
}

} // namespace Slic3r
