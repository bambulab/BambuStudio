#include "AccountManager.hpp"
#include "BambuNetworkAgent.hpp"
#include <thread>
#include <mutex>
#include <boost/thread.hpp>
#include <boost/log/trivial.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/foreach.hpp>
#include <boost/typeof/typeof.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <openssl/md5.h>


static std::string encode_path(const char* src)
{
#ifdef WIN32
    // Convert the source utf8 encoded string to a wide string.
    std::wstring wstr_src = boost::nowide::widen(src);
    if (wstr_src.length() == 0)
        return std::string();
    // Convert a wide string to a local code page.
    int size_needed = ::WideCharToMultiByte(0, 0, wstr_src.data(), (int)wstr_src.size(), nullptr, 0, nullptr, nullptr);
    std::string str_dst(size_needed, 0);
    ::WideCharToMultiByte(0, 0, wstr_src.data(), (int)wstr_src.size(), str_dst.data(), size_needed, nullptr, nullptr);
    return str_dst;
#else /* WIN32 */
    return src;
#endif /* WIN32 */
}

inline std::string get_transform_string(int bytes)
{
    float ms = (float)bytes / 1024.0f / 1024.0f;
    float ks = (float)bytes / 1024.0f;
    char buffer[32];
    if (ms > 0)
        ::sprintf(buffer, "%.1fM", ms);
    else if (ks > 0)
        ::sprintf(buffer, "%.1fK", ks);
    else
        ::sprintf(buffer, "%.1fK", ks);
    return buffer;
}

inline bool is_valid_property(json &j, std::string prop)
{
    return j.contains(prop) && !j[prop].is_null();
}

void split_string(std::string s, std::vector<std::string>& v) {

    std::string t = "";
    for (int i = 0; i < s.length(); ++i) {
        if (s[i] == ',') {
            v.push_back(t);
            t = "";
        }
        else {
            t.push_back(s[i]);
        }
    }
    v.push_back(t);
}

// topic_str = device/device_id/report
std::string get_dev_id_from_topic(std::string topic_str)
{
    std::vector<std::string> params;
    boost::split(params, topic_str, boost::is_any_of("/"));
    if (params.size() <= 2) return "";
    return params[1];
}

static bool bbl_calc_md5(std::string& filename, std::string& md5_out)
{
    unsigned char digest[16];
    MD5_CTX       ctx;
    MD5_Init(&ctx);
    boost::filesystem::ifstream ifs(filename, std::ios::binary);
    std::string                 buf(64 * 1024, 0);
    const std::size_t& size = boost::filesystem::file_size(filename);
    std::size_t                 left_size = size;
    while (ifs) {
        ifs.read(buf.data(), buf.size());
        int read_bytes = ifs.gcount();
        MD5_Update(&ctx, (unsigned char*)buf.data(), read_bytes);
    }
    MD5_Final(digest, &ctx);
    char md5_str[33];
    for (int j = 0; j < 16; j++) { sprintf(&md5_str[j * 2], "%02X", (unsigned int)digest[j]); }
    md5_out = std::string(md5_str);
    return true;
}

namespace BBL {


    void action_listener::on_success(const mqtt::token& tok) {
        // re sucscribe the monitoring printer
        AccountManager *manager = (AccountManager *) context_;
        for (int i = 0; i < tok.get_topics()->size(); i++) {
            BOOST_LOG_TRIVIAL(trace) << "subscribe topic:" << (*tok.get_topics())[i].c_str() << " success";
            std::string dev_id = get_dev_id_from_topic((*tok.get_topics())[i]);
             if (manager) {
                 if (manager->on_printer_connected_fn) {
                     manager->on_printer_connected_fn(dev_id);
                 }
            }
        }

        BOOST_LOG_TRIVIAL(trace) << "subscribe return code: " << tok.get_return_code();
        BOOST_LOG_TRIVIAL(trace) << "subscribe reason code: " << tok.get_reason_code();
    }


    void cloud_conn_callback::connected(const std::string& cause)
    {
        BOOST_LOG_TRIVIAL(trace) << "cloud_conn_callback::connected!";
        // re sucscribe the monitoring printer
        AccountManager* manager = (AccountManager*)context_;
        if (manager) {
            if (manager->on_server_connected_fn)
                manager->on_server_connected_fn();
        }
    }

    void cloud_conn_callback::on_failure(const mqtt::token& tok)
    {
        BOOST_LOG_TRIVIAL(trace) << "cloud_conn_callback::on_failure, Connection(mqtt) failed! return code = "
            << tok.get_return_code() << ", reason code = " <<tok.get_reason_code() << ", retry=" << nretry_;
        nretry_++;
        AccountManager* manager = (AccountManager*)context_;
        if (manager)
            manager->m_is_connecting = false;
    }

    void cloud_conn_callback::on_success(const mqtt::token& tok)
    {
        BOOST_LOG_TRIVIAL(trace) << "cloud_conn_callback::on_success, Connection(mqtt) OK! cli id=" << cli_.get_client_id();
        /* mqtt connect on success tips, same as connected */
        AccountManager* manager = (AccountManager*)context_;
        if (manager)
            manager->m_is_connecting = false;
    }

    void cloud_conn_callback::connection_lost(const std::string& cause) {
        BOOST_LOG_TRIVIAL(trace) << "cloud_conn_callback::connection_lost!, cause =" << cause;
    }

    void cloud_conn_callback::message_arrived(mqtt::const_message_ptr msg)
    {
        AccountManager* manager = (AccountManager*)context_;
        if (manager) {
            std::string topic = msg->get_topic();
            std::vector<std::string> params;
            boost::split(params, topic, boost::is_any_of("/"));
            //BBS device, dev_id, report at least 3 params
            if (params.size() <= 2) return;

            BOOST_LOG_TRIVIAL(trace) << "message topic:" << msg->get_topic() << ", payload=" << msg->get_payload_str();

            if (manager->on_message_fn)
                manager->on_message_fn(params[1], msg->get_payload_str());   
        }
    }

    void sub_action_listener::on_failure(const mqtt::token& tok) {
        for (int i = 0; i < tok.get_topics()->size(); i++) {
            BOOST_LOG_TRIVIAL(trace) << "local subscribe topic:" << (*tok.get_topics())[i].c_str() << " failed";
        }
        BOOST_LOG_TRIVIAL(trace) << "local subscribe return code: " << tok.get_return_code();
        BOOST_LOG_TRIVIAL(trace) << "local subscribe reason code: " << tok.get_reason_code();
    }

    void sub_action_listener::on_success(const mqtt::token& tok) {
        for (int i = 0; i < tok.get_topics()->size(); i++) {
            BOOST_LOG_TRIVIAL(trace) << "subscribe topic:" << (*tok.get_topics())[i].c_str() << " success";
            std::string dev_id = get_dev_id_from_topic((*tok.get_topics())[i]);
        }

        BOOST_LOG_TRIVIAL(trace) << "subscribe return code: " << tok.get_return_code();
        BOOST_LOG_TRIVIAL(trace) << "subscribe reason code: " << tok.get_reason_code();
    }


    void local_conn_callback::connected(const std::string& cause)
    {
        BOOST_LOG_TRIVIAL(trace) << "client_conn_callback::connected!";
        try {
            AccountManager* acc = (AccountManager*)context_;
            if (acc) {
                if (acc->on_local_connect_fn) {
                    std::string dev_id = get_dev_id_from_topic(sub_topics[0]);
                    acc->on_local_connect_fn(ConnectStatus::ConnectStatusOk, dev_id, cause);
                }
                for (int i = 0; i < sub_topics.size(); i++) {
                    sub_action_listener* sub_listener = new sub_action_listener("LanSubscriber_" + sub_topics[i]);
                    cli_.subscribe(sub_topics[i], 0, nullptr, *sub_listener);
                }
            }
        }
        catch (mqtt::exception& e) {
            BOOST_LOG_TRIVIAL(trace) << "client_conn_callback::connected, exception=" << e.what();
        }
    }

    void local_conn_callback::on_failure(const mqtt::token& tok)
    {
        BOOST_LOG_TRIVIAL(trace) << "client_conn_callback::on_failure, Connection(mqtt) failed! retry=" << nretry_;
        try {
            AccountManager* acc = (AccountManager*)context_;
            if (acc) {
                if (acc->on_local_connect_fn) {
                    std::string dev_id = get_dev_id_from_topic(sub_topics[0]);
                    acc->on_local_connect_fn(ConnectStatus::ConnectStatusFailed, dev_id, "");
                }
            }
        }
        catch (mqtt::exception& e) {
            BOOST_LOG_TRIVIAL(trace) << "client_conn_callback::connected, exception=" << e.what();
        }
    }

    void local_conn_callback::on_success(const mqtt::token& tok)
    {
        BOOST_LOG_TRIVIAL(trace) << "client_conn_callback::on_success, Connection(mqtt) OK!";
        try {
            ;
        }
        catch (mqtt::exception& e) {
            BOOST_LOG_TRIVIAL(trace) << "client_conn_callback::connected, exception=" << e.what();
        }
    }

    void local_conn_callback::connection_lost(const std::string& cause) {
        BOOST_LOG_TRIVIAL(trace) << "client_conn_callback::connection_lost!, cause =" << cause;
        try {
            AccountManager* acc = (AccountManager*)context_;
            if (acc) {
                if (acc->on_local_connect_fn) {
                    std::string dev_id = get_dev_id_from_topic(sub_topics[0]);
                    acc->on_local_connect_fn(ConnectStatus::ConnectStatusLost, dev_id, cause);
                }
            }
        }
        catch (mqtt::exception& e) {
            BOOST_LOG_TRIVIAL(trace) << "client_conn_callback::connected, exception=" << e.what();
        }
        ++nretry_;
    }

    void local_conn_callback::message_arrived(mqtt::const_message_ptr msg)
    {
        BOOST_LOG_TRIVIAL(trace) << "local message topic:" << msg->get_topic() << ", payload=" << msg->get_payload_str();
        try {
            AccountManager* acc = (AccountManager*)context_;
            if (acc) {
                if (acc->on_local_message_fn) {
                    std::string dev_id = get_dev_id_from_topic(msg->get_topic());
                    acc->on_local_message_fn(dev_id, msg->get_payload_str());
                }
            }
        }
        catch (mqtt::exception& e) {
            BOOST_LOG_TRIVIAL(trace) << "client_conn_callback::connected, exception=" << e.what();
        }
    }

    std::string VersionInfo::convert_full_version(std::string short_version)
    {
        std::string result = "";
        std::vector<std::string> items;
        boost::split(items, short_version, boost::is_any_of("."));
        if (items.size() == VERSION_LEN) {
            for (int i = 0; i < VERSION_LEN; i++) {
                std::stringstream ss;
                ss << std::setw(2) << std::setfill('0') << items[i];
                result += ss.str();
                if (i != VERSION_LEN - 1)
                    result += ".";
            }
            return result;
        }
        return result;
    }

    std::string VersionInfo::convert_short_version(std::string full_version)
    {
        full_version.erase(std::remove(full_version.begin(), full_version.end(), '0'), full_version.end());
        return full_version;
    }


    AccountInfo::AccountInfo(std::string account, std::string user_id, AccountInfo::LoginStatus status)
    {
        m_account = account;
        m_user_id = user_id;
        m_login_status = status;
    }

    AccountInfo::AccountInfo( std::string account, std::string user_id, std::string strToken, std::string strName, std::string strAvatar, AccountInfo::LoginStatus status, std::string strAutotestToken)
    {
        m_account      = account;
        m_user_id      = user_id;
        m_token        = strToken;
        m_name         = strName;
        m_avatar       = strAvatar;
        m_login_status = status;
        m_autotest_token = strAutotestToken;
    }

    AccountInfo::AccountInfo(std::string account, std::string user_id, std::string strName, std::string strAvatar, AccountInfo::LoginStatus status, std::string strRefreshToken, long long refreshExpiresIn, std::string strToken, long long expiresIn, std::string strAutotestToken)
    {
        m_account = account;
        m_user_id = user_id;
        m_name = strName;
        m_avatar = strAvatar;
        m_login_status = status;
        m_token = strToken;
        m_expires_in = expiresIn;
        m_refresh_token = strRefreshToken;
        m_refresh_expires_in = refreshExpiresIn;
        m_autotest_token = strAutotestToken;
    }

    int AccountInfo::save_to_json(json& config_json)
    {
        config_json["user"]["account"] = m_account;
        config_json["user"]["token"] = m_token;
        config_json["user"]["expires_in"] = m_expires_in;
        config_json["user"]["refresh_token"] = m_refresh_token;
        config_json["user"]["refresh_expires_in"] = m_refresh_expires_in;
        config_json["user"]["user_id"] = m_user_id;
        config_json["user"]["login_status"] = (int)m_login_status;
        config_json["user"]["autotest_token"] = m_autotest_token;
        config_json["user"]["name"] = m_name;
        config_json["user"]["avatar"] = m_avatar;
        return 0;
    }

    AccountInfo* AccountInfo::load_from_json(json& config_json)
    {
        try {
            if (config_json.contains("user")) {
                if (config_json["user"].contains("account")
                    && config_json["user"].contains("token")
                    && config_json["user"].contains("user_id")) {
                    std::string account = config_json["user"]["account"].get<std::string>();
                    std::string token = config_json["user"]["token"].get<std::string>();
                    long long expires_in = config_json["user"]["expires_in"].get<long long>();
                    std::string refresh_token = config_json["user"]["refresh_token"].get<std::string>();
                    long long refresh_expires_in = config_json["user"]["refresh_expires_in"].get<long long>();
                    std::string user_id = config_json["user"]["user_id"].get<std::string>();
                    std::string autotest_token = config_json["user"]["autotest_token"].get<std::string>();
                    std::string sAvatar = config_json["user"]["avatar"].get<std::string>();
                    std::string sName = config_json["user"]["name"].get<std::string>();
                    AccountInfo::LoginStatus status = AccountInfo::LoginStatus::STATUS_LOGOUT;
                    if (config_json["user"].contains("login_status"))
                        status = (AccountInfo::LoginStatus)config_json["user"]["login_status"].get<int>();
                    AccountInfo* info = new AccountInfo(account, user_id, sName, sAvatar, status, refresh_token, refresh_expires_in, token, expires_in, autotest_token);
                    /*info->m_autotest_token = autotest_token;
                    info->set_token(token);*/
                    return info;
                }
            }
            else {
                return nullptr;
            }
        }
        catch (std::exception& e)
        {
            BOOST_LOG_TRIVIAL(error) << "AccountManager::load_from_json() failed! exception=" << e.what();
        }
        return nullptr;
    }

    std::string AccountManager::get_emqx_server_host()
    {
        return user_region_server.mqtt_server_host;
    }

    std::string AccountManager::get_official_server_host()
    {
        if (!user_region_server.base_domain.empty()) {
            if (!user_region_server.environment.empty())
                return (boost::format("https://portal%1%.%2%") % user_region_server.environment % user_region_server.base_domain).str();
            else
                return (boost::format("https://%1%") % user_region_server.base_domain).str();
        }
        return "";
    }

    int AccountManager::save_config()
    {
        BOOST_LOG_TRIVIAL(trace) << "Agent: save_config";
        std::string dir_str = (boost::filesystem::path(config_dir) / AGENT_CONFIG_FILE).make_preferred().string();
        std::ofstream json_file(encode_path(dir_str.c_str()));
        if (json_file.is_open()) {
            json_file << std::setw(4) << config_json << std::endl;
            return 0;
        }
        else {
            return -1;
        }
        return 0;
    }

    int AccountManager::load_config()
    {
        BOOST_LOG_TRIVIAL(trace) << "Agent: load_config";
        std::string dir_str = (boost::filesystem::path(config_dir) / AGENT_CONFIG_FILE).make_preferred().string();
        std::ifstream json_file(encode_path(dir_str.c_str()));
        try {
            if (json_file.is_open()) {
                json_file >> config_json;
                if (config_json.contains("last_monitor_machine"))
                    this->default_machine = config_json["last_monitor_machine"];
                return 0;
            }
        }
        catch (...) {
            return -1;
        }
        return 0;
    }

    std::string AccountManager::get_config(std::string section, std::string key)
    {
        if (config_json.contains(section)) {
            if (config_json["section"].contains(key)) {
                return config_json[section][key].get<std::string>();
            }
        }
        return "";
    }

    AccountManager::AccountManager()
        :mqtt_opt(mqtt::connect_options_builder().clean_session().finalize()),
        mqtt_cli(nullptr),
        mqtt_cb(nullptr),
        mqtt_uuid_bytes(4)
    {
        mqtt_opt.set_max_inflight(500);
        mqtt_opt.set_connect_timeout(10);
        mqtt_opt.set_ssl(mqtt_ssl_opt);

        m_curr_user = nullptr;
    }

    AccountManager::~AccountManager()
    {
        on_user_login_fn = nullptr;
        on_printer_connected_fn = nullptr;
        on_server_connected_fn = nullptr;
        on_http_error_fn = nullptr;
        get_country_code_fn = nullptr;
        on_message_fn = nullptr;
        on_local_connect_fn = nullptr;
        on_local_message_fn = nullptr;

        save_config();
        if (mqtt_cli) {
            if (mqtt_cli->is_connected()) {
                mqtt_cli->disable_callbacks();
                mqtt_cli->disconnect()->wait();
            }
        }

        if (mqtt_local_cli) {
            if (mqtt_local_cli->is_connected()) {
                mqtt_local_cli->disable_callbacks();
                mqtt_local_cli->disconnect()->wait();
            }
        }
        Http::disable_log();
    }

    void AccountManager::set_product_mqtt_opt()
    {
        std::string trust_store = cert_dir + "/" + cert_name;
        mqtt_ssl_opt.set_trust_store(trust_store);
        mqtt_opt.set_ssl(mqtt_ssl_opt);
    }

    void AccountManager::set_engineering_mqtt_opt()
    {
        mqtt_ssl_opt.ca_path(cert_dir);
        std::string key_store = cert_dir + "/slicer.crt";
        std::string trust_store = cert_dir + "/slicer_chain.crt";
        std::string private_key = cert_dir + "/slicer_pri.pem";
        mqtt_ssl_opt.set_key_store(key_store);
        mqtt_ssl_opt.set_trust_store(trust_store);
        mqtt_ssl_opt.set_private_key(private_key);
    }

    void AccountManager::init_log()
    {
        std::time_t t = std::time(0);
        std::tm* now_time = std::localtime(&t);
        std::stringstream buf;
        buf << std::put_time(now_time, "debug_http_%a_%b_%d_%H_%M_%S.log");

        auto log_folder = (boost::filesystem::path(config_dir) / "log").make_preferred();
        if (!boost::filesystem::exists(log_folder)) {
            boost::filesystem::create_directory(log_folder);
        }

        auto http_log_path = ( log_folder / buf.str()).make_preferred();
        std::string log_filename = encode_path(http_log_path.string().c_str());
        Http::enable_log(log_filename.c_str());
        Http::register_global_handler(
            [this](std::string body, std::string error, unsigned int status) {
                if (on_http_error_fn)
                    on_http_error_fn(status, body);
            }
        );
    }

    int AccountManager::load_user_info()
    {
        m_curr_user = AccountInfo::load_from_json(config_json);
        if (this->is_user_login()) {
            if (on_user_login_fn) {
                on_user_login_fn(0, true);
            }
        }
        return 0;
    }

    int AccountManager::save_user_info()
    {
        if (m_curr_user) {
            m_curr_user->save_to_json(config_json);
            save_config();
        }
        return 0;
    }

    bool AccountManager::is_mqtt_connected()
    {
        if (mqtt_cli)
            return mqtt_cli->is_connected();
        return false;
    }


    int AccountManager::connect_mqtt(bool sync)
    {
        BOOST_LOG_TRIVIAL(trace) << "connect_cloud_mqtt";
        if (m_curr_user == nullptr) {
            return -1;
        }

        try {
            if (mqtt_cli) {
                BOOST_LOG_TRIVIAL(trace) << "mqtt_cli is exists!";
                return -1;
            }
            boost::uuids::uuid uuid = boost::uuids::random_generator()();
            mqtt_uuid = to_string(uuid).substr(0, mqtt_uuid_bytes);
            std::string client_id = (boost::format("slicer:%1%:%2%") % m_curr_user->m_user_id % mqtt_uuid).str();

            // update mqtt user_name and password
            std::string user_name = (boost::format("u_%1%") % m_curr_user->m_user_id).str();
            mqtt_opt.set_user_name(user_name);
            std::string password = get_token_str(true);
            if (password.empty()) { //invalid refreshToken or failed to get accessToken
                user_logout();
                return -1;
            }
            mqtt_opt.set_password(password);
            std::string mqtt_host = get_emqx_server_host();
            BOOST_LOG_TRIVIAL(trace) << "connect_cloud_mqtt, client_id = " << client_id;
            BOOST_LOG_TRIVIAL(trace) << "connect_cloud_mqtt, user_name = " << user_name;
            BOOST_LOG_TRIVIAL(trace) << "connect_cloud_mqtt, password = " << password;
            BOOST_LOG_TRIVIAL(trace) << "connect_cloud_mqtt, mqtt_host = " << mqtt_host;

            std::string country_code;
            if (get_country_code_fn)
                country_code = get_country_code_fn();
            if (country_code == "ENV_CN_PRE" || country_code == "ENV_CN_QA" || country_code == "ENV_CN_DEV") {
                set_engineering_mqtt_opt();
            } else {
                set_product_mqtt_opt();
            }
            mqtt_opt.set_ssl(mqtt_ssl_opt);
            mqtt_cli = new mqtt::async_client(mqtt_host, client_id);
            mqtt_cb = new cloud_conn_callback(*mqtt_cli, mqtt_opt, this);
            if (mqtt_cli) {
                mqtt_cli->set_callback(*mqtt_cb);
                m_is_connecting = true;
                if (sync)
                    mqtt_cli->connect(mqtt_opt, this, *mqtt_cb)->wait_for(3000);
                else
                    mqtt_cli->connect(mqtt_opt, this, *mqtt_cb);
            }
            return 0;
        }
        catch (mqtt::exception& e) {
            BOOST_LOG_TRIVIAL(trace) << "connect_cloud_mqtt, exception=" << e.get_error_str();
            return -1;
        }
        catch (...) {
            BOOST_LOG_TRIVIAL(trace) << "connect_cloud_mqtt, exception";
            return -1;
        }
        return 0;
    }

    int AccountManager::disconnect_mqtt()
    {
        try{
            if (mqtt_cli) {
                mqtt_cli->stop_consuming();
                if (mqtt_cli->is_connected()) {
                    mqtt_cli->disconnect();
                }
                delete mqtt_cli;
                mqtt_cli = nullptr;
            }

            if (mqtt_cb) {
                delete mqtt_cb;
                mqtt_cb = nullptr;
            }
        } catch(...) {
            ;
        }
        return 0;
    }

    void AccountManager::check_mqtt_connection()
    {
        if (is_user_login() && mqtt_cli && !mqtt_cli->is_connected() && !m_is_connecting) {
            try {
                m_is_connecting = true;
                std::string password = get_token_str(true);
                if (password.empty()) { //invalid refreshToken or failed to get accessToken
                    user_logout();
                    return;
                }
                mqtt_opt.set_password(password);
                if (!mqtt_cb)
                    mqtt_cb = new cloud_conn_callback(*mqtt_cli, mqtt_opt, this);
                mqtt_cli->connect(mqtt_opt, this, *mqtt_cb);
                BOOST_LOG_TRIVIAL(trace) << "check_mqtt_connection: reconnecting";
            } catch(const mqtt::exception& exc) {
                BOOST_LOG_TRIVIAL(error) << "mqtt_exception: " << exc.what();
            }
            catch(...) {
                BOOST_LOG_TRIVIAL(error) << "mqtt_exception occur";
            }
        }
    }

    void AccountManager::add_subscribe(std::string dev_id)
    {
        std::string report_topic = (boost::format("device/%1%/report") % dev_id).str();
        try {
            if (mqtt_cli && mqtt_cli->is_connected()) {
                action_listener* sub_listener = new action_listener("MQTT_Subscriber_" + report_topic, this);
                mqtt_cli->subscribe(report_topic, 0, this, *sub_listener);
            }
            else {
                BOOST_LOG_TRIVIAL(trace) << "add_subscribe failed, topic=" << report_topic << ", mqtt_cli is disconnect or invalid!";
            }
        }
        catch (mqtt::exception& e) {
            BOOST_LOG_TRIVIAL(trace) << "add_subscribe exception, topic=" << report_topic << ", exception error_str=" << e.get_error_str() << ", message=" << e.get_message();
        }
        catch (...) {
            BOOST_LOG_TRIVIAL(trace) << "add_subscribe exception, topic=" << report_topic;
        }
    }

    void AccountManager::del_subscribe(std::string dev_id)
    {
        std::string report_topic = (boost::format("device/%1%/report") % dev_id).str();
        try {
            if (mqtt_cli && mqtt_cli->is_connected()) {
                BOOST_LOG_TRIVIAL(trace) << "del_subscribe topic=" << report_topic;
                mqtt_cli->unsubscribe(report_topic);
            }
        }
        catch (mqtt::exception& e) {
            BOOST_LOG_TRIVIAL(trace) << "del_subscribe exception, topic=" << report_topic << ", exception error_str=" << e.get_error_str() << ", message=" << e.get_message();
        }
        catch (...) {
            BOOST_LOG_TRIVIAL(trace) << "del_subscribe exception, topic=" << report_topic;
        }
    }

    int AccountManager::local_send_message(std::string dev_id, std::string json_str, int qos)
    {
        if (!mqtt_local_cli || !mqtt_local_cli->is_connected()) { 
            return -1;
        }

        std::string topic = (boost::format("device/%1%/request") % dev_id).str();
        json_str += '\0';
        BOOST_LOG_TRIVIAL(trace) << "local send message topic=" << topic << ", payload=" << json_str;
        auto msg = mqtt::message::create(topic, json_str, qos, false);
        mqtt_local_cli->publish(msg);
        return 0;
    }

    void AccountManager::set_monitor_machine(std::string dev_id)
    {
        
        BOOST_LOG_TRIVIAL(trace) << "set monitor machine = " << dev_id;
        std::string old_dev_id = this->default_machine;

        if (dev_id.empty()) {
            return;
        }

        /*if (!mqtt_cli->is_connected()) {
            BOOST_LOG_TRIVIAL(trace) << "set monitor machine not connected";
            return;
        }*/

        // store last_monitor_printer
        config_json["last_monitor_machine"] = dev_id;
        this->default_machine = dev_id;

        //unsubscribe old machine
        if (!old_dev_id.empty() && old_dev_id.compare(dev_id) != 0) {
            this->del_subscribe(old_dev_id);
        }

        //subscribe new machine, call start_subscribe first
        if (m_is_subscribing) {
            this->add_subscribe(dev_id);
        }
    }

    void AccountManager::set_default_machine(std::string dev_id)
    {
        config_json["last_monitor_machine"] = dev_id;
        default_machine = dev_id;
        save_config();
    }
   

    int AccountManager::local_connect_mqtt(std::string dev_id, std::string dev_ip, std::string username, std::string password)
    {
        /* lan mqtt connection */
        BOOST_LOG_TRIVIAL(trace) << "local_connect_mqtt: dev_id = " << dev_id << ", dev_ip = " << dev_ip;
        // get a new mqtt_uuid
        boost::uuids::uuid uuid = boost::uuids::random_generator()();
        mqtt_uuid = to_string(uuid).substr(0, 4);

        try {
            mqtt_local_opt = mqtt::connect_options_builder().clean_session().finalize();
            mqtt_local_opt.set_automatic_reconnect(3, 10);
            mqtt_local_opt.set_max_inflight(1000);
            mqtt_local_opt.set_connect_timeout(5);

            if (!username.empty() || !password.empty()) {
                mqtt_opt.set_user_name(username);
                mqtt_opt.set_password(password);
            }

            std::string client_id = (boost::format("%1%:%2%") % "studio_client_id" % mqtt_uuid).str();
            std::string report_topic = (boost::format("device/%1%/report") % dev_id).str();
            mqtt_local_cli = new mqtt::async_client(dev_ip, client_id);
            mqtt_local_cb = new local_conn_callback(*mqtt_local_cli, mqtt_local_opt, this);
            mqtt_local_cb->add_topics(report_topic);
            mqtt_local_cli->set_callback(*mqtt_local_cb);
            
            mqtt_local_cli->connect(mqtt_local_opt, this, *mqtt_local_cb);
        } catch (...) {
            ;
        }

        return 0;
    }

    int AccountManager::local_disconnect_mqtt()
    {
        if (mqtt_local_cli) {
            try {
                mqtt_local_cli->disable_callbacks();
                mqtt_local_cli->disconnect();
                delete mqtt_local_cb;
                mqtt_local_cb = nullptr;
            }
            catch (std::exception& e) {

            }
            catch (...) {
                ;
            }
            delete mqtt_local_cli;
            mqtt_local_cli = NULL;
        }
        return 0;
    }


    void AccountManager::start_subscribe(std::string module)
    {
        BOOST_LOG_TRIVIAL(trace) << "start_subscribe, machine=" << default_machine << ", module = " << module;
        if (!default_machine.empty())
            this->add_subscribe(default_machine);

        if (!module.empty() && subscribe_module.find(module) == subscribe_module.end()) {
            subscribe_module.emplace(std::make_pair(module, true));
        }
        m_is_subscribing = true;
    }

    void AccountManager::stop_subscribe(std::string module)
    {
        BOOST_LOG_TRIVIAL(trace) << "stop_subscribe, machine=" << default_machine << ", module = " << module;
        if (!module.empty() && subscribe_module.find(module) != subscribe_module.end())
            subscribe_module.erase(module);
        else
            return;
        if (subscribe_module.empty()) {
            if (!default_machine.empty()) {
                this->del_subscribe(default_machine);
            }
            m_is_subscribing = false;
        } else {
            return;
        }
    }

    bool AccountManager::is_user_login()
    {
        if (m_curr_user) {
            if (m_curr_user->is_valid())
                return m_curr_user->login_status() == AccountInfo::LoginStatus::STATUS_LOGIN;
        }
        return false;
    }

    int AccountManager::user_logout()
    {
        if (m_curr_user) {
            m_curr_user->set_login_status(AccountInfo::LoginStatus::STATUS_LOGOUT);
            this->disconnect_mqtt();
            clean_user_data();
            save_user_info();
        }
        return 0;
    }

    void AccountManager::clean_user_data()
    {
        set_default_machine("");
        myProjectList.clear();
    }

    void AccountManager::user_check_report(int* query_task_id, bool* printable)
    {
        if (!m_curr_user) {
            *printable = false;
            return;
        }

        std::string user_id = m_curr_user->get_user_id();
        std::string url = (boost::format("http://192.168.0.12:8000/api/user_last_task_report?user_id=%1%") % user_id).str();
        Http http = Http::get(url);
        http.auth_basic("slicer", "znFx94AAew8VVHv");
        http.on_complete([this, printable, query_task_id](std::string body, unsigned status) {
            try {
                json j = json::parse(body);
                *query_task_id = j["task_id"].get<int>();
                *printable = j["print_flag"].get<bool>();
            }
            catch (...) {
                ;
            }
            })
            .on_error([this](std::string body, std::string error, unsigned status) {
                BOOST_LOG_TRIVIAL(trace) << "user_check_report: body = " << body << ", status = " << status;
            })
            .perform_sync();
    }

    int AccountManager::get_notification(BBLProfile* profile, unsigned int& http_code, std::string& http_body, CancelFn cancel_fn, int timeout)
    {
        int result = -1, retry = 0;
        if (!profile || profile->upload_ticket.empty()) return -1;

        std::string url = (boost::format("%1%/iot-service/api/user/notification?action=upload&ticket=%2%") % host % profile->upload_ticket).str();
        Http http = Http::get(url);
        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .on_complete(
                [this, &result, &http_code, &http_body](std::string body, unsigned status) {
                    http_code = status;
                    http_body = body;
                    try {
                        json j = json::parse(body);
                        if (is_valid_property(j, "message")) {
                            std::string message_str = j["message"].get<std::string>();
                            if (message_str == MSG_SUCCESS)
                                result = 1;
                            else if (message_str == "running")
                                result = 0;
                            else
                                result = -1;
                        }
                    }
                    catch(...) {
                        ;
                    }
                }
            ).on_error(
                [this, &result, &http_code, &http_body](std::string body, std::string error, unsigned status) {
                    result = -1;
                    http_code = status;
                    http_body = body;
                }
            );

        std::chrono::system_clock::time_point curr_time = std::chrono::system_clock::now();
        bool has_timeout = false;
        while (!has_timeout) {
            http.perform_sync();
            /* failed */
            if (result == -1) return -1;

            /* success */
            if (result == 1) return 0;

            if (cancel_fn) {
                if (cancel_fn()) return RET_ERR_CANCEL;
            }

            retry++;
            BOOST_LOG_TRIVIAL(trace) << "get notification, retry = " << retry;
            std::chrono::system_clock::time_point last_update_time = std::chrono::system_clock::now();
            auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(curr_time - last_update_time);

            /* timeout */
            if (diff.count() > timeout * 1000) {
                has_timeout = true;
                break;
            }
            boost::this_thread::sleep_for(boost::chrono::milliseconds(POLL_NOTIFICATION_INTERVAL * 1000));
        }
        if (has_timeout) {
            /* timeout */
            return RET_ERR_TIMEOUT;
        }
        return result;
    }

    int AccountManager::calc_get_notification_timeout(boost::filesystem::path &file)
    {
        int result = 0;
        boost::uintmax_t size = boost::filesystem::file_size(file);
        boost::uintmax_t size_m = size / 1024 / 1024;
        int timeout = size_m * 2;
        result = std::max(timeout, POLL_NOTIFICATION_TIMEOUT);
        result = std::min(result, POLL_NOTIFICATION_TIMEOUT_MAX);
        BOOST_LOG_TRIVIAL(trace) << "calc_get_notification_timeout, file size = " << size << ", timeout = " << timeout;
        return result;
    }

    int AccountManager::put_notification(BBLProfile* profile, std::string upload_filename, unsigned int &http_code, std::string &http_error)
    {
        int result = -1;
        if (upload_filename.empty()) return -1;
        if (profile->upload_ticket.empty()) return -1;

        std::string url = (boost::format("%1%/iot-service/api/user/notification") % host).str();
        Http http = Http::put2(std::move(url));

        json post_body_json;
        post_body_json["upload"]["ticket"] = profile->upload_ticket;
        post_body_json["upload"]["origin_file_name"] = upload_filename;

        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .header("Content-Type", "application/json")
            .set_post_body(post_body_json.dump())
            .on_complete([&result, &http_code, &http_error](std::string body, unsigned status) {
                http_code = status;
                http_error = body;
                json j = json::parse(body);
                try {
                    if (is_valid_property(j, "message")) {
                        if (j["message"].get<std::string>() == MSG_SUCCESS)
                            result = 0;
                    }
                } catch(...) {
                    ;
                }
            }).on_error([&http_code, &http_error](std::string body, std::string error_str, unsigned status) {
                http_code = status;
                http_error = body;
            }).perform_sync();
        return result;
    }

    int AccountManager::modify_device_name(std::string dev_id, std::string dev_name, unsigned int& http_code, std::string& http_body)
    {
        int result = -1;
        std::string url = (boost::format("%1%/iot-service/api/user/device/info") % host).str();

        json j;
        j["dev_id"] = dev_id;
        j["dev_name"] = dev_name;

        Http http = Http::patch(url);
        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .header("Content-Type", "application/json")
            .set_post_body(j.dump())
            .on_complete(
                [this, &result, &http_code, &http_body](std::string body, unsigned int status) {
                    http_code = status;
                    http_body = body;
                    try {
                        json j = json::parse(body);
                        if (j.contains("message") && j["message"].get<std::string>() == MSG_SUCCESS) {
                            result = 0;
                        }
                    } catch(...) {
                        ;
                    }
            })
            .on_error([this, &http_code, &http_body](std::string body, std::string error, unsigned status) {
                    http_code = status;
                    http_body = body;
        }).perform_sync();
        return result;
    }

    /* print apis */
    int AccountManager::get_print_info(unsigned int& http_code, std::string &http_body)
    {
        int result = -1;
        std::string url = (boost::format("%1%/iot-service/api/user/print?force=true") % host).str();
        Http http  = Http::get(url);
        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .header("Content-Type", "application/json")
            .on_complete([&result, &http_code, &http_body](std::string body, unsigned status) {
                http_code = status;
                http_body = body;
                result = 0;
            })
            .on_error([&http_code, &http_body](std::string body, std::string error, unsigned status) {
                http_code = status;
                http_body = body;
            }).perform_sync();
        return result;
    }

    int AccountManager::query_bind_status(std::vector<std::string> device_list, unsigned int &http_code, std::string &http_body)
    {
        int result = -1;
        Http http = Http::get(_get_qeury_bind_list_url(device_list));
        try {
            http.header("accept", "application/json")
                .header("Authorization", get_token_str())
                .header("Content-Type", "application/json")
                .on_complete([&result, &http_code, &http_body](std::string body, unsigned status) {
                    http_code = status;
                    http_body = body;
                    result = 0;
                }).on_error([&result, &http_code, &http_body](std::string body, std::string error, unsigned status) {
                    http_code = status;
                    http_body = body;
                    result = -1;
                }).perform_sync();
        }
        catch (std::exception& e) {
            ;
        }
        return 0;
    }

    int AccountManager::request_user_unbind(std::string device_id, ResultFn fn)
    {
        std::string url = (boost::format("%1%/iot-service/api/user/bind") % host).str();

        json j;
        j["dev_id"] = device_id;
        j["force"] = false;
        std::string json_str = j.dump();

        Http http = Http::del(url);
        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .header("Content-Type", "application/json")
            .set_del_body(json_str)
            .on_complete([&, fn](std::string body, unsigned status) {
                try {
                    json j = json::parse(body);
                    if (j.contains("message")) {
                        if (j["message"].get<std::string>() == MSG_SUCCESS) {
                            fn(0, "");
                        }
                    }
                } catch (...) {
                    ;
                }
            }).on_error([&, device_id, fn](std::string body, std::string error, unsigned status) {
                BOOST_LOG_TRIVIAL(trace) << "Unbind Device " << device_id << " Failed!";
                if (fn) {
                    fn(-1, "");
                }
            }).perform();
        return 0;
    }

    int AccountManager::request_bind_list(ResultFn fn)
    {
        if (!is_user_login()) {
            if (fn) {
                fn(-1, "User is not login");
            }
            return -1;
        }

        std::string url = _get_bind_list_url();
        std::string token_str = get_token_str();
        if (token_str.empty() || url.empty()) {
            return -1;
        }

        Http http = Http::get(url);
        http.header("accept", "application/json")
            .header("Authorization", token_str)
            .on_complete([this, fn](std::string body, unsigned) {
            try {
                std::stringstream ss(body);
                pt::ptree root;
                pt::read_json(ss, root);
                boost::optional<std::string> message = root.get_optional<std::string>("message");
                if (message.has_value()) {
                    if (message.value().compare(MSG_SUCCESS) == 0) {
                        if (fn) {
                            fn(0, "get bind list ok");
                        }
                        return;
                    }
                    else if (message.value().compare("nodev") == 0) {
                        if (fn) {
                            fn(0, "get bind list ok");
                        }
                        return;
                    }
                }

                if (fn) {
                    fn(-1, "Get bind list failed! body=" + body);
                }
            }
            catch (std::exception& e) {
                BOOST_LOG_TRIVIAL(trace) << "request_bind_list, on_complete exception" << std::string(e.what());
                if (fn) {
                    fn(-1, "Get bind list failed! body=" + body);
                }
            }
                }).on_error([&, fn](std::string body, std::string error, unsigned status) {
                    BOOST_LOG_TRIVIAL(trace) << "Get bind list failed!, status=" << status << ", body=" << body << ", error=" << error;
                    if (fn) {
                        fn(-1, "Get bind list failed! body=" + body);
                    }
                    }).perform();
                    return 0;
    }

    std::string AccountManager::json_request_body_post_project(BBLProject* project)
    {
        if (!project) return "";

        json j;
        j["name"] = project->project_name;
        if (!project->project_model_id.empty())
            j["model_id"] = project->project_model_id;
        if (!project->project_content.empty())
            j["content"] = project->project_content;
        return j.dump();
    }

    std::string AccountManager::json_request_body_post_profile(BBLProfile* profile)
    {
        std::string profile_name_str = profile->profile_name;
        json j;
        j["name"] = profile_name_str;
        j["content"] = profile->profile_content;
        return j.dump();
    }

    std::string AccountManager::json_request_body_post_subtask(BBLProject* project, BBLProfile* profile, BBLSubTask* task)
    {
        if (!task || !project || !profile) return "";
        if (project->project_model_id.empty()) return "";
        if (task->task_printer_dev_id.empty()) return "";
        if (profile->profile_id.empty()) return "";
        if (task->task_partplate_idx.empty()) return "";

        json j;
        j["modelId"] = project->project_model_id;
        j["title"] = task->task_name;
        j["cover"] = "";
        j["deviceId"] = task->task_printer_dev_id;
        j["filamentSettingIds"] = json::array();
        j["profileId"] = stoi(profile->profile_id);
        j["plateIndex"] = stoi(task->task_partplate_idx);
        j["timelapse"] = task->task_record_timelapse;
        j["bedType"] = task->task_bed_type;
        j["bedLeveling"] = task->task_bed_leveling;
        j["flowCali"] = task->task_flow_cali;
        j["vibrationCali"] = task->task_vibration_cali;
        j["layerInspect"] = task->task_layer_inspect;
        if (!task->task_ams_mapping.empty()) {
            json mapping = json::parse(task->task_ams_mapping);
            j["amsMapping"] = mapping;
        }
        j["mode"] = task->task_mode;
        return j.dump();
    }

    std::string AccountManager::json_request_body_post_setting(std::string name, bool is_system, std::map<std::string, std::string>& values_map)
    {
        json j, setting_node;

        j["public"] = is_system ? true : false;
        j["name"] = name;
        for (auto it = values_map.begin(); it != values_map.end(); it++) {
            if (boost::iequals(it->first,IOT_JSON_KEY_VERSION)) {
                j[IOT_JSON_KEY_VERSION] = it->second;
            }
            else if (boost::iequals(it->first, IOT_JSON_KEY_TYPE)) {
                j[IOT_JSON_KEY_TYPE] = it->second;
            }
            else if (boost::iequals(it->first, IOT_JSON_KEY_BASE_ID)) {
                j[IOT_JSON_KEY_BASE_ID] = it->second;
            }
            else
                setting_node[it->first] = it->second;
        }

        /*j["type"] = values_map[IOT_JSON_KEY_TYPE];

        j["base_id"] = preset->base_id;
        for (const std::string &opt_key : preset->config.keys()) {
            setting_node[opt_key] = preset->config.opt_serialize(opt_key);
        }
        setting_node["updated_time"] = std::to_string(preset->updated_time);*/
        j["setting"] = setting_node;

        return j.dump();
    }

    std::string AccountManager::json_request_body_put_setting(std::string name, std::map<std::string, std::string>& values_map)
    {
        json j, setting_node;

        j["name"] = name;
         for (auto it = values_map.begin(); it != values_map.end(); it++) {
            if (boost::iequals(it->first,IOT_JSON_KEY_VERSION)) {
                j[IOT_JSON_KEY_VERSION] = it->second;
            }
            else if (boost::iequals(it->first, IOT_JSON_KEY_TYPE)) {
                //skip type in this api
            }
            else if (boost::iequals(it->first, IOT_JSON_KEY_BASE_ID)) {
                j[IOT_JSON_KEY_BASE_ID] = it->second;
            }
            else
                setting_node[it->first] = it->second;
        }

        j["setting"] = setting_node;

        return j.dump();
    }

    std::string AccountManager::json_request_poll_3mf_gather_model_only()
    {
        json j;
        j["base_model"] = true;
        j["profile_config"] = true;
        j["profile_thumbnail"] = false;
        j["profile_gcode"] = false;
        j["profile_files"] = json::array();
        return j.dump();
    }

    std::string AccountManager::json_request_poll_3mf_gather(BBLSubTask* task)
    {
        if (!task) return "";
        json j;
        j["base_model"] = false;
        j["profile_config"] = false;
        j["profile_thumbnail"] = true;
        j["profile_gcode"] = false;
        j["profile_pattern"] = true;
        if (!task->task_gcode_in_3mf.empty()) {
            j["profile_files"] = json::array({task->task_gcode_in_3mf});
        }
        else {
            j["profile_files"] = json::array();
        }
        return j.dump();
    }

    // POST /api/user/project
    int AccountManager::request_project_profile_id(BBLProject *project, BBLProfile *profile, unsigned int &http_code, std::string &http_body)
    {
        http_code = 0;
        http_body = "";

        int result = -1;
        if (!project || !profile) return -1;

        /* get a project id and model id */
        std::string json_str = json_request_body_post_project(project);

        Http http_post = Http::post(_get_project_url());
        http_post.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .header("Content-Type", "application/json")
            .set_post_body(json_str)
            .on_complete([this, project, profile, &result, &http_code, &http_body](std::string body, unsigned int status) {
                try {
                    http_code = status;
                    http_body = body;
                    BOOST_LOG_TRIVIAL(trace) << "AccountManager::request_project_id, body=" << body;
                    json j = json::parse(body);
                    if (is_valid_property(j, "message")) {
                        if (j["message"].get<std::string>() == MSG_SUCCESS) {
                            if (is_valid_property(j, "project_id"))
                                project->project_id = j["project_id"].get<std::string>();
                            if (is_valid_property(j, "model_id"))
                                project->project_model_id = j["model_id"].get<std::string>();
                            if (is_valid_property(j, "name"))
                                project->project_name = j["name"].get<std::string>();
                            if (is_valid_property(j, "profile_id") && is_valid_property(j, "upload_url") && is_valid_property(j, "upload_ticket")) {
                                profile->project_id    = project->project_id;
                                profile->profile_id    = j["profile_id"].get<std::string>();
                                profile->upload_url    = j["upload_url"].get<std::string>();
                                profile->upload_ticket = j["upload_ticket"].get<std::string>();
                                result                 = 0;
                            }
                        }
                    }
                } catch (...) {
                    BOOST_LOG_TRIVIAL(trace) << "request_project_id: on_complete parsing failed, body=" << body;
                }
            })
            .on_error([&http_code, &http_body](std::string body, std::string error, unsigned status) {
                http_code = status;
                http_body = body;
            })
            .perform_sync();
        return result;
    }

    int AccountManager::request_project_id(BBLProject* project, unsigned int &http_code, std::string &http_body)
    {
        http_code = 0;
        http_body = "";

        int result = -1;
        if (!project) return -1;

        /* get a project id and model id */
        std::string json_str = json_request_body_post_project(project);

        Http http_post = Http::post(_get_project_url());
        http_post.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .header("Content-Type", "application/json")
            .set_post_body(json_str)
            .on_complete(
                [this, project, &result, &http_code, &http_body](std::string body, unsigned int status) {
                    try {
                        http_code = status;
                        http_body = body;
                        BOOST_LOG_TRIVIAL(trace) << "AccountManager::request_project_id, body=" << body;
                        json j = json::parse(body);
                        if (is_valid_property(j, "message")) {
                            if (j["message"].get<std::string>() == MSG_SUCCESS) {
                                if (is_valid_property(j, "project_id"))
                                    project->project_id = j["project_id"].get<std::string>();
                                if (is_valid_property(j, "model_id"))
                                    project->project_model_id = j["model_id"].get<std::string>();
                                if (is_valid_property(j, "name"))
                                    project->project_name = j["name"].get<std::string>();
                                result = 0;
                            }
                        }
                    }
                    catch (...) {
                        BOOST_LOG_TRIVIAL(trace) << "request_project_id: on_complete parsing failed, body=" << body;
                    }
                }
            )
            .on_error(
                [&http_code, &http_body](std::string body, std::string error, unsigned status) {
                    http_code = status;
                    http_body = body;
                }
            )
        .perform_sync();
        return result;
    }

    int AccountManager::patch_project(BBLProject* project, std::string patch_body, unsigned int& http_code, std::string& http_body)
    {
        int result = -1;
        std::string url = (boost::format("%1%/iot-service/api/user/project/%2%") % host % project->project_id).str();

        Http http = Http::patch(url);
        http.header("Authorization", get_token_str())
            .header("Content-Type", "application/json")
            .set_post_body(patch_body)
            .on_complete(
                [this, &result](std::string body, unsigned int status) {
                    try {
                        json j = json::parse(body);
                        if (j.contains("message"))
                            if (j["message"].get<std::string>() == MSG_SUCCESS)
                                result = 0;
                    } catch(...) {
                        ;
                    }
                }
            ).on_error(
                [this, &http_code, &http_body, &result](std::string body, std::string error, unsigned status) {
                    http_code = status;
                    http_body = body;
                    result = -1;
                }
            ).perform_sync();
        return result;
    }

    int AccountManager::request_profile_id(BBLProfile* profile, unsigned int &http_code, std::string  &http_body)
    {
        int result = -1;
        if (!profile || profile->project_id.empty()) return -1;

        std::string project_url = (boost::format("%1%/iot-service/api/user/project/%2%") % host % profile->project_id).str();

        std::vector<std::string> params;
        std::string request_str = json_request_body_post_profile(profile);

        Http http_create_profile = Http::post(project_url);
        http_create_profile.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .header("Content-Type", "application/json")
            .set_post_body(request_str)
            .on_complete(
                [this, profile, &result, &http_code, &http_body](std::string body, unsigned status) {
                    try {
                        http_code = status;
                        http_body = body;
                        json j = json::parse(body);
                        if (is_valid_property(j, "message")) {
                            if (j["message"].get<std::string>() == MSG_SUCCESS) {
                                if (is_valid_property(j, "profile_id") && is_valid_property(j, "upload_url") && is_valid_property(j, "upload_ticket")) {
                                    profile->profile_id = j["profile_id"].get<std::string>();
                                    profile->upload_url = j["upload_url"].get<std::string>();
                                    profile->upload_ticket = j["upload_ticket"].get<std::string>();
                                    result = 0;
                                }
                            }

                        }
                    }
                    catch(...) {
                        ;
                    }
                }
            )
            .on_error(
                [this, &result, &http_code, &http_body](std::string body, std::string error, unsigned status) {
                    result = -1;
                    http_code = status;
                    http_body = body;
                }
            )
            .perform_sync();
        return result;
    }

    int AccountManager::upload_3mf_to_oss(BBLProfile* profile, unsigned int &http_code, std::string &http_body, Http::ProgressFn proFn)
    {
        int result = RET_ERR_CANCEL;
        if (!profile || !profile->project_) return -1;
        if (profile->upload_url.empty()) return -1;
        if (!fs::exists(profile->project_->project_path)) {
            BOOST_LOG_TRIVIAL(trace) << "file is not exists!";
            return -1;
        }

        std::string md5_str;
        std::string full_filename = profile->project_->project_path.generic_string();
        bbl_calc_md5(full_filename, md5_str);
        BOOST_LOG_TRIVIAL(trace) << "print_job: upload_3mf md5 = " << md5_str;

        // reset values
        http_code = 0;
        http_body = "";
        Http http_put   = Http::put2(profile->upload_url);

        http_put.set_put_body(profile->project_->project_path)
                .on_complete(
                    [this, &result, &http_code, &http_body](std::string body, unsigned int status) {
                        result = 0;
                        http_code = status;
                        http_body = body;
                    }
                )
                .on_progress(proFn)
            .on_header_callback([this, &result, &md5_str](std::string headers) {
                if (headers.empty()) return;
                int tag_pos = headers.find("ETag:");
                if (tag_pos > 0) {
                    size_t start_pos = headers.find("\"", tag_pos);
                    std::string md5_in_header = headers.substr(start_pos + 1, 32);
                    if (md5_in_header.compare(md5_str) != 0 && !md5_str.empty()) {
                        result = RET_MD5_CHECK_FAILED;
                    }
                    BOOST_LOG_TRIVIAL(trace) << "print_job: found Etag = " << md5_in_header;
                }
            })
            .on_error(
                [this, &result, &http_code, &http_body](std::string body, std::string error, unsigned status) {
                    http_code = status;
                    http_body = body;
                    result = -1;
                }
            );
        http_put.perform_sync();
        return result;
    }

    int AccountManager::upload_file_to_oss(std::string upload_url, fs::path file, std::string md5_str, unsigned int &http_code, std::string &http_body, Http::ProgressFn proFn)
    {
        int result = RET_ERR_CANCEL;
        if (!fs::exists(file)) {
            BOOST_LOG_TRIVIAL(trace) << "file is not exists!";
            return -1;
        }

        if (md5_str.empty()) {
            std::string md5_str;
            std::string full_filename = file.generic_string();
            bbl_calc_md5(full_filename, md5_str);
            BOOST_LOG_TRIVIAL(trace) << "print_job: upload_3mf md5 = " << md5_str;
        }

        // reset values
        http_code = 0;
        http_body = "";
        Http http_put = Http::put2(upload_url);

        http_put.set_put_body(file)
            .on_complete(
                [this, &result, &http_code, &http_body](std::string body, unsigned int status) {
                    result = 0;
                    http_code = status;
                    http_body = body;
                }
            )
            .on_progress(proFn)
                    .on_header_callback([this, &result, &md5_str](std::string headers) {
                    if (headers.empty()) return;
                    int tag_pos = headers.find("ETag:");
                    if (tag_pos > 0) {
                        size_t start_pos = headers.find("\"", tag_pos);
                        std::string md5_in_header = headers.substr(start_pos + 1, 32);
                        if (md5_in_header.compare(md5_str) != 0 && !md5_str.empty()) {
                            result = RET_MD5_CHECK_FAILED;
                        }
                        BOOST_LOG_TRIVIAL(trace) << "print_job: found Etag = " << md5_in_header;
                    }
                        })
            .on_error(
                [this, &result, &http_code, &http_body](std::string body, std::string error, unsigned status) {
                    http_code = status;
                    http_body = body;
                    result = -1;
                }
            );
        http_put.perform_sync();
        return result;
    }

    int AccountManager::get_design_info(std::string model_id, std::string& design_id, unsigned int& http_code, std::string& http_body)
    {
        http_code = 0;
        http_body = "";
        int result = -1;
        if (model_id.empty()) return -1;
        design_id.clear();

        std::string url = (boost::format("%1%/design-service/model/%2%") % host % model_id).str();

        Http http = Http::get(url);
        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .on_complete([this, &result, &design_id, &http_code, &http_body](std::string body, unsigned status) {
                try {
                    http_code = status;
                    http_body = body;
                    json j = json::parse(body);
                    if (is_valid_property(j, "id")) {
                        design_id = std::to_string(j["id"].get<int>());
                        result = 0;
                    }
                } catch(...) {
                    ;
                }
            })
            .on_error(
                [this, &result, &http_code, &http_body](std::string body, std::string error, unsigned status) {
                    http_code = status;
                    http_body = body;
                    if (status == 404)
                        result = 0;
                    else {
                        result = -1;
                    }
                }
            )
            .perform_sync();
        return result;
    }

    int AccountManager::get_user_upload(std::vector<std::string> files, std::vector<std::string>& urls)
    {
        int result = -1;
        std::string params;
        for (int i = 0; i < files.size(); i++) {
            params += "models=" + files[i];
            if (i != files.size() -1)
                params += "&";
        }

         std::string url = (boost::format("%1%/iot-service/api/user/upload?%2%") % host % params).str();
         Http http = Http::get(url);
         http.header("accept", "application/json")
             .header("Authorization", get_token_str())
             .on_complete(
                 [this, &result, &urls](std::string body, unsigned status) {
                     try {
                         json j = json::parse(body);
                         if (is_valid_property(j, "message")) {
                             if (j["message"] == MSG_SUCCESS) {
                                 for (json::iterator it = j["urls"].begin(); it != j["urls"].end(); it++) {
                                     urls.push_back((*it)["url"]);
                                 }
                                 result = 0;
                             }
                         }
                     }
                     catch (...) {
                         ;
                     }
                 })
            .perform_sync();

        if (files.size() != urls.size())
            result = -1;
        return result;
    }

    // GET /api/user/profile/{profile_id}
    int AccountManager::get_profile_3mf(BBL::BBLProfile *profile, unsigned int &http_code, std::string http_body)
    {
        int result = -1;
        http_code  = 0;
        http_body  = "";
        if (!profile) return -1;
        if (profile->profile_id.empty()) return -1;
        if (profile->model_id.empty()) return -1;

        std::string url  = (boost::format("%1%/iot-service/api/user/profile/%2%?model_id=%3%") % host % profile->profile_id % profile->model_id).str();
        Http http = Http::get(url);
        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .on_complete(
                [this, &result, &http_code, &http_body, profile](std::string body, unsigned status) {
                try {
                    http_code = status;
                    json j = json::parse(body);
                    if (is_valid_property(j, "message")) {
                        if (j["message"] == MSG_SUCCESS) {
                            result = 0;
                            if (is_valid_property(j, "url") && is_valid_property(j, "md5")) {
                                profile->url = j["url"].get<std::string>();
                                profile->md5 = j["md5"].get<std::string>();
                            }
                            if (is_valid_property(j, "name")) {
                                profile->profile_name = j["name"].get<std::string>();
                            }
                            if (is_valid_property(j, "filename")) {
                                profile->filename = j["filename"].get<std::string>();
                            }
                        }
                    }
                }
                catch (...) {
                    ;
                }
            })
            .on_error([this, &http_code, &http_body](std::string body, std::string error, unsigned status) {
                http_code = status;
                http_body = body;
            })
            .perform_sync();
        return result;
    }

    void AccountManager::get_task(BBLTask* &task)
    {
        std::string url = (boost::format("%1%/iot-service/api/user/task/%2%") % host % task->task_id).str();
        Http http = Http::get(url);
        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .on_complete(
                [this, task](std::string body, unsigned) {
                    std::stringstream ss(body);
                    pt::ptree root;
                    pt::read_json(ss, root);
                    boost::optional<std::string> message = root.get_optional<std::string>("message");
                    if (message.has_value()) {
                        if (message.value().compare(MSG_SUCCESS) == 0) {
                            boost::optional<std::string> name = root.get_optional<std::string>("name");
                            if (name.has_value())
                                task->task_name = name.value();
                            boost::optional<std::string> project_id = root.get_optional<std::string>("project_id");
                            if (project_id.has_value())
                                task->task_project_id = project_id.value();
                            boost::optional<std::string> profile_id = root.get_optional<std::string>("profile_id");
                            if (profile_id.has_value())
                                task->task_profile_id = profile_id.value();
                            boost::optional<std::string> status = root.get_optional<std::string>("status");
                            if (status.has_value())
                                task->task_status = status.value().compare("active") ? BBLTask::TaskStatus::TASK_ACTIVE : BBLTask::TaskStatus::TASK_INACTIVE;
                            boost::optional<std::string> content = root.get_optional<std::string>("content");
                            if (content.has_value())
                                task->parse_content_json(content.value());
                            boost::optional<std::string> create_time = root.get_optional<std::string>("create_time");
                            if (create_time.has_value())
                                task->task_create_time = create_time.value();
                            boost::optional<std::string> update_time = root.get_optional<std::string>("update_time");
                            if (update_time.has_value())
                                task->task_update_time = update_time.value();
                            boost::optional<std::string> parent_id = root.get_optional<std::string>("parent");
                            if (root.get_child_optional("subtask") != boost::none) {
                                pt::ptree subtask_node = root.get_child("subtask");
                                task->subtasks.clear();
                                for (auto subtask_item = subtask_node.begin(); subtask_item != subtask_node.end(); ++subtask_item) {
                                    BBLSubTask* subtask = new BBLSubTask(task);
                                    subtask->task_id = subtask_item->second.get_optional<std::string>("task_id").value_or("");
                                    subtask->task_name = subtask_item->second.get_optional<std::string>("name").value_or("");
                                    subtask->task_create_time = subtask_item->second.get_optional<std::string>("create_time").value_or("");
                                    subtask->task_update_time = subtask_item->second.get_optional<std::string>("update_time").value_or("");
                                    boost::optional<std::string> content = subtask_item->second.get_optional<std::string>("content");
                                    if (content.has_value()) {
                                        subtask->parse_content_json(content.value());
                                    }
                                    task->subtasks.push_back(subtask);
                                }
                            }
                            if (root.get_child_optional("context") != boost::none) {
                                task->slice_info.clear();
                                pt::ptree context = root.get_child("context");
                                if (context.get_child_optional("plates") != boost::none) {
                                    // parse plate sliced info
                                    pt::ptree plates = context.get_child("plates");
                                    for (auto plate = plates.begin(); plate != plates.end(); ++plate) {
                                        boost::optional<std::string> index = plate->second.get_optional<std::string>("index");
                                        if (!index.has_value()) {
                                            continue;
                                        }

                                        BBLSliceInfo* info = nullptr;
                                        /* do not update if info is initialized */
                                        if (task->slice_info.find(index.value()) != task->slice_info.end()) {
                                            continue;
                                        }
                                        info = new BBLSliceInfo();
                                        info->index = index.value();
                                        // set a format to title
                                        boost::optional<int> prediction = plate->second.get_optional<int>("prediction");
                                        if (prediction.has_value()) {
                                            info->prediction = prediction.value();
                                        }
                                        boost::optional<std::string> weight = plate->second.get_optional<std::string>("weight");
                                        if (weight.has_value()) {
                                            info->weight = weight.value();
                                        }
                                        if (plate->second.get_child_optional("thumbnail") != boost::none) {
                                            pt::ptree thumbnail_node = plate->second.get_child("thumbnail");
                                            boost::optional<std::string> thumbnail_name = thumbnail_node.get_optional<std::string>("name");
                                            if (thumbnail_name.has_value()) {
                                                info->thumbnail_name = thumbnail_name.value();
                                            }
                                            boost::optional<std::string> thumbnail_dir = thumbnail_node.get_optional<std::string>("dir");
                                            if (thumbnail_dir.has_value()) {
                                                info->thumbnail_dir = thumbnail_dir.value();
                                            }
                                            boost::optional<std::string> thumbnail_url = thumbnail_node.get_optional<std::string>("url");
                                            if (thumbnail_url.has_value()) {
                                                info->thumbnail_url = thumbnail_url.value();
                                            }
                                        }
                                        if (plate->second.get_child_optional("gcode") != boost::none) {
                                            pt::ptree gcode_node = plate->second.get_child("gcode");
                                            boost::optional<std::string> gcode_name = gcode_node.get_optional<std::string>("name");
                                            if (gcode_name.has_value()) {
                                                info->gcode_name = gcode_name.value();
                                            }
                                            boost::optional<std::string> gcode_dir = gcode_node.get_optional<std::string>("dir");
                                            if (gcode_dir.has_value()) {
                                                info->gcode_dir = gcode_dir.value();
                                            }
                                            boost::optional<std::string> gcode_url = gcode_node.get_optional<std::string>("url");
                                            if (gcode_url.has_value()) {
                                                info->gcode_url = gcode_url.value();
                                            }
                                        }
                                        task->slice_info.insert(std::make_pair(index.value(), info));
                                    }
                                }
                            }
                        }
                    }
                }
            )
            .on_error(
                [this](std::string body, std::string error, unsigned status) {
                    BOOST_LOG_TRIVIAL(info) << "get_task info failed! body=" << body;
                }
            )
            .perform();
    }

    void AccountManager::get_subtask(BBLSubTask* &subtask)
    {
        std::string url = (boost::format("%1%/iot-service/api/user/task/%2%") % host % subtask->task_id).str();
        Http http = Http::get(url);
        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .on_complete(
                [this, subtask](std::string body, unsigned status) {
                    try {
                        json j = json::parse(body);

                        if (j.contains("message")) {
                            if (j["message"].get<std::string>() == MSG_SUCCESS) {
                                if (j.contains("name"))
                                    subtask->task_name = j["name"].get<std::string>();
                                if (j.contains("status"))
                                    subtask->task_status = BBLSubTask::parse_status(j["status"].get<std::string>());
                                if (j.contains("content"))
                                    subtask->parse_content_json(j["content"].get<std::string>());
                                if (j.contains("create_time"))
                                    subtask->task_create_time = j["create_time"].get<std::string>();
                            }
                        }
                    } catch(...) {
                        ;
                    }
                }
            )
            .on_error(
                [this](std::string body, std::string error, unsigned status) {
                    BOOST_LOG_TRIVIAL(info) << "get_task info failed! body=" << body;
                }
            )
            .perform();
    }

    void AccountManager::get_plate_index(std::string subtask_id, int& plate_index)
    {
        std::string url = (boost::format("%1%/iot-service/api/user/task/%2%") % host % subtask_id).str();
        Http http = Http::get(url);
        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .on_complete(
                [this, &plate_index](std::string body, unsigned status) {
                    try {
                        json j = json::parse(body);
                        if (j.contains("message")) {
                            if (j["message"].get<std::string>() == MSG_SUCCESS) {
                                if (j.contains("content")) {
                                    std::string content_str = j["content"].get<std::string>();
                                    json content_json = json::parse(content_str);
                                    plate_index = content_json["info"]["plate_idx"].get<int>();
                                }
                            }
                        }
                    }
                    catch (...) {
                        ;
                    }
                }
            )
            .on_error(
                [this](std::string body, std::string error, unsigned status) {
                    BOOST_LOG_TRIVIAL(info) << "get_task info failed! body=" << body;
                }
            )
            .perform_sync();
    }

    void AccountManager::get_machine_last_report_url(std::string dev_id, std::string& last_url)
    {
        std::string* return_url = new std::string();
        std::string url = (boost::format("http://192.168.0.12:8000/api/device_last_task_report?dev_id=%1%")
                        % dev_id).str();
        Http http = Http::get(url);
        http.auth_basic("slicer", "znFx94AAew8VVHv");
        http.on_complete([return_url](std::string body, unsigned status) {
            json j = json::parse(body);
            std::string report_url = j["report_url"].get<std::string>();
            *return_url = report_url;
        })
        .on_error([](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(trace) << "on_subtask_report, body = " << body << ", error = " << error << ", status = " << status;
        })
        .perform_sync();
        last_url = *return_url;
        delete return_url;
    }

    void AccountManager::get_camera_url(
        std::string const & device,
        std::function<void(std::string)> callback)
    {
        if (m_curr_user == NULL) return;
        std::string url = (boost::format("%1%/iot-service/api/user/ttcode") % host)
                              .str();
        std::string body = (boost::format("{\"dev_id\": \"%1%\"}") % device)
                               .str();
        Http http = Http::post(url);
        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .header("user-id", m_curr_user->get_user_id())
            .header("Content-Type", "application/json")
            .set_post_body(body)
            .on_complete([this, callback](std::string body, unsigned) {
                std::stringstream ss(body);
                pt::ptree         root;
                pt::read_json(ss, root);
                std::string ttcode =
                    root.get_optional<std::string>("ttcode").get_value_or("");
                std::string authkey =
                    root.get_optional<std::string>("authkey").get_value_or("");
                std::string passwd =
                    root.get_optional<std::string>("passwd").get_value_or("");
                std::string url;
                if (!authkey.empty()) {
                    url.append(url.empty() ? "?" : "&");
                    url.append("authkey=" + authkey);
                }
                if (!passwd.empty()) {
                    url.append(url.empty() ? "?" : "&");
                    url.append("passwd=" + passwd);
                }
                std::string region = get_tutk_region();
                if (!region.empty()) {
                    url.append(url.empty() ? "?" : "&");
                    url.append("region=" + region);
                }
                if (ttcode == "null") ttcode.clear();
                callback(ttcode.empty() ? ttcode : "bambu:///" + ttcode + url);
            })
            .on_error([this, callback](std::string body, std::string error,
                                        unsigned status) {
                    BOOST_LOG_TRIVIAL(info)
                        << "get_camera_ttcode info failed! body=" << body;
                    callback("");
                })
            .perform();
    }

    std::string AccountManager::get_tutk_region()
    {
        auto region = user_region_server.tutk_server_host;
        boost::algorithm::to_lower(region);
        return region;
    }

    int AccountManager::get_machine_version(std::string dev_id, unsigned &http_code, std::string &http_body)
    {
        if (dev_id.empty()) return -1;

        int result = -1;
        std::string url = (boost::format("%1%/iot-service/api/user/device/version?dev_id=%2%") % host % dev_id).str();
        Http http = Http::get(url);
        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .header("Content-Type", "application/json")
            .on_complete(
                [&result, &http_code, &http_body](std::string body, unsigned status) {
                    http_code = status;
                    http_body = body;
                    try {
                        json j = json::parse(body);
                        if (is_valid_property(j, "message")) {
                            if (j["message"] == MSG_SUCCESS) {
                                result = 0;
                            }
                        }
                    } catch (...) {
                    }
                }
            )
            .on_error(
                [&http_code, &http_body](std::string body, std::string error, unsigned status) {
                    http_code = status;
                    http_body = body;
                }
            )
            .perform_sync();
        return result;
    }

    void AccountManager::set_curr_user(AccountInfo* user_info)
    {
        if (m_curr_user)
            delete m_curr_user;
        m_curr_user = user_info;
        save_user_info();
    }

    void AccountManager::get_profile(BBLProject*& project, BBLProfile*& profile)
    {
        if (!profile || !project) return;

        std::string query_params = (boost::format("?profile_id=%1%") % profile->profile_id).str();
        std::string url = (boost::format("%1%/iot-service/api/user/project/%2%%3%") % host % profile->project_id % query_params).str();

        int retry_ = 0;
        int retry_max = 10;
        Http http = Http::get(url);

        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .on_complete(
                [this, project, profile](std::string body, unsigned) {
                    std::stringstream ss(body);
                    pt::ptree root;
                    pt::read_json(ss, root);
                    boost::optional<std::string> message = root.get_optional<std::string>("message");
                    if (message.has_value()) {
                        if (message.value().compare(MSG_SUCCESS) == 0) {
                            BOOST_LOG_TRIVIAL(info) << "get_project_info ok!";
                            boost::optional<std::string> status = root.get_optional<std::string>("status");
                            boost::optional<std::string> model_id = root.get_optional<std::string>("model_id");
                            if (model_id.has_value()) { project->project_model_id = model_id.value(); }
                            boost::optional<std::string> name = root.get_optional<std::string>("name");
                            if (name.has_value()) {
                                project->project_name = name.value();
                            }
                            boost::optional<std::string> create_time = root.get_optional<std::string>("create_time");
                            boost::optional<std::string> content = root.get_optional<std::string>("content");

                            // parse profiles list
                            boost::optional<std::string> profiles = root.get_optional<std::string>("profiles");
                            if (root.get_child_optional("profiles") != boost::none) {
                                pt::ptree profiles = root.get_child("profiles");
                                for (auto prof = profiles.begin(); prof != profiles.end(); ++prof) {
                                    boost::optional<std::string> profile_id = prof->second.get_optional<std::string>("profile_id");
                                    boost::optional<std::string> profile_name = prof->second.get_optional<std::string>("name");
                                    if (profile_name.has_value()) {
                                        profile->profile_name = profile_name.value();
                                    }
                                    // if find current profile, get infos
                                    if (profile_id.has_value() && profile_id.value().compare(profile->profile_id) == 0) {
                                        if (prof->second.get_child_optional("context") != boost::none) {
                                            profile->slice_info.clear();
                                            /* to be deleted */
                                            pt::ptree context = prof->second.get_child("context");
                                            if (context.get_child_optional("plates") != boost::none) {
                                                pt::ptree plates = context.get_child("plates");
                                                for (auto plate = plates.begin(); plate != plates.end(); ++plate) {
                                                    boost::optional<std::string> index = plate->second.get_optional<std::string>("index");
                                                    if (!index.has_value()) {
                                                        continue;
                                                    }

                                                    BBLSliceInfo* info = nullptr;
                                                    /* do not update if info is initialized */
                                                    if (profile->slice_info.find(index.value()) != profile->slice_info.end()) {
                                                        info = profile->slice_info.find(index.value())->second;
                                                    }
                                                    else {
                                                        info = new BBLSliceInfo(profile);
                                                        info->index = index.value();
                                                        // set a format to title
                                                        info->title = (boost::format("%1%-(plate %2%)") % profile->profile_name % info->index).str();
                                                        boost::optional<int> prediction = plate->second.get_optional<int>("prediction");
                                                        if (prediction.has_value()) {
                                                            info->prediction = prediction.value();
                                                        }
                                                        boost::optional<std::string> weight = plate->second.get_optional<std::string>("weight");
                                                        if (weight.has_value()) {
                                                            info->weight = weight.value();
                                                        }
                                                        if (plate->second.get_child_optional("thumbnail") != boost::none) {
                                                            pt::ptree thumbnail_node = plate->second.get_child("thumbnail");
                                                            boost::optional<std::string> thumbnail_name = thumbnail_node.get_optional<std::string>("name");
                                                            if (thumbnail_name.has_value()) {
                                                                info->thumbnail_name = thumbnail_name.value();
                                                            }
                                                            boost::optional<std::string> thumbnail_dir = thumbnail_node.get_optional<std::string>("dir");
                                                            if (thumbnail_dir.has_value()) {
                                                                info->thumbnail_dir = thumbnail_dir.value();
                                                            }
                                                            boost::optional<std::string> thumbnail_url = thumbnail_node.get_optional<std::string>("url");
                                                            if (thumbnail_url.has_value()) {
                                                                info->thumbnail_url = thumbnail_url.value();
                                                            }
                                                        }
                                                        if (plate->second.get_child_optional("gcode") != boost::none) {
                                                            pt::ptree gcode_node = plate->second.get_child("gcode");
                                                            boost::optional<std::string> gcode_name = gcode_node.get_optional<std::string>("name");
                                                            if (gcode_name.has_value()) {
                                                                info->gcode_name = gcode_name.value();
                                                            }
                                                            boost::optional<std::string> gcode_dir = gcode_node.get_optional<std::string>("dir");
                                                            if (gcode_dir.has_value()) {
                                                                info->gcode_dir = gcode_dir.value();
                                                            }
                                                            boost::optional<std::string> gcode_url = gcode_node.get_optional<std::string>("url");
                                                            if (gcode_url.has_value()) {
                                                                info->gcode_url = gcode_url.value();
                                                            }
                                                        }
                                                        if (plate->second.get_child_optional("filaments") != boost::none) {
                                                            pt::ptree filaments = plate->second.get_child("filaments");
                                                            for (auto filament = filaments.begin(); filament != filaments.end(); ++filament) {
                                                                FilamentInfo f;
                                                                try {
                                                                    boost::optional<std::string> id = filament->second.get_optional<std::string>("id");
                                                                    if (id.has_value() && !id.value().empty() && id.value().compare("null") != 0) f.id = stoi(id.value()) - 1;
                                                                    f.color  = filament->second.get<std::string>("color");
                                                                    f.type   = filament->second.get<std::string>("type");
                                                                    f.used_m = stof(filament->second.get<std::string>("used_m"));
                                                                    f.used_g = stof(filament->second.get<std::string>("used_g"));
                                                                } catch (...) {
                                                                    continue;
                                                                }
                                                                info->filaments_info.push_back(f);
                                                            }
                                                        }
                                                        profile->slice_info.insert(std::make_pair(index.value(), info));
                                                    }
                                                }
                                                continue;
                                            }
                                            else if (context.get_child_optional("thumbnail_files") != boost::none) {
                                                pt::ptree thumbnails = context.get_child("thumbnail_files");
                                                for (auto thumbnail = thumbnails.begin(); thumbnail != thumbnails.end(); ++thumbnail) {
                                                    std::string index = thumbnail->second.get_optional<std::string>("index").value();
                                                    std::string  name = thumbnail->second.get_optional<std::string>("name").value();
                                                    std::string   dir = thumbnail->second.get_optional<std::string>("dir").value_or("");
                                                    std::string   url = thumbnail->second.get_optional<std::string>("url").value();
                                                    BBLSliceInfo* info = nullptr;
                                                    if (profile->slice_info.find(index) != profile->slice_info.end()) {
                                                        info = profile->slice_info.find(index)->second;
                                                    }
                                                    else {
                                                        info = new BBLSliceInfo(profile);
                                                        info->index = index;
                                                        info->thumbnail_name = name;
                                                        info->thumbnail_dir = dir;
                                                        info->thumbnail_url = url;
                                                        profile->slice_info.insert(std::make_pair(index, info));
                                                    }
                                                }
                                            }
                                        }
                                    }
                                    BOOST_LOG_TRIVIAL(trace) << "get_profile_info id = " << profile->profile_id << ", profile_name = " << profile->profile_name;
                                }
                            }
                        }
                    }
                }
            )
            .on_error(
                [this, project, profile](std::string body, std::string error, unsigned status) {
                    BOOST_LOG_TRIVIAL(info) << "get_profile_info failed! body=" << body;
                }
            )
            .perform();
    }

    void AccountManager::get_slice_info(std::string project_id, std::string profile_id, int plate_index, std::string & slice_info_json)
    {
        std::string query_params = (boost::format("?profile_id=%1%") % profile_id).str();
        std::string url = (boost::format("%1%/iot-service/api/user/project/%2%%3%") % host % project_id % query_params).str();
        Http http = Http::get(url);

        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .on_complete(
                [this, profile_id, plate_index, &slice_info_json](std::string body, unsigned status) {
                    try {
                        json j = json::parse(body);
                        if (j.contains("message")) {
                            if (j["message"].get<std::string>() == MSG_SUCCESS) {
                                BOOST_LOG_TRIVIAL(info) << "get slice info ok!";
                                if (j.contains("profiles")) {
                                    for (auto &profile: j["profiles"]) {
                                        if (profile["profile_id"].get<std::string>() == profile_id) {
                                            for( auto plate:profile["context"]["plates"]) {
                                                if (plate["index"].get<int>() == plate_index) {
                                                    slice_info_json = plate.dump(4);
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    catch(...) {
                        ;
                    }
                }
            ).on_error(
                [this](std::string body, std::string error, unsigned status) {
                    BOOST_LOG_TRIVIAL(info) << "get_profile_info failed! body=" << body;
                }
            ).perform_sync();
    }

    void AccountManager::get_project_info(BBLProject* project)
    {
        if (!project || project->project_id.empty()) return;

        std::string query_params("?gather=all");
        std::string url = (boost::format("%1%/iot-service/api/user/project/%2%?%3%") % host % project->project_id % query_params).str();
        Http http = Http::get(url);
        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .on_complete(
                [this, project](std::string body, unsigned) {
                    std::stringstream ss(body);
                    pt::ptree root;
                    pt::read_json(ss, root);
                    boost::optional<std::string> message = root.get_optional<std::string>("message");
                    if (message.has_value()) {
                        if (message.value().compare(MSG_SUCCESS) == 0) {
                            BOOST_LOG_TRIVIAL(info) << "get_project_info ok!";
                            boost::optional<std::string> status = root.get_optional<std::string>("status");
                            if (status.has_value()) project->project_status = status.value();

                            boost::optional<std::string> model_id = root.get_optional<std::string>("model_id");
                            if (model_id.has_value()) {
                                if (project->project_model_id.compare(model_id.value()) != 0) {
                                    BOOST_LOG_TRIVIAL(trace) << "project_model_id changed from " << project->project_model_id << " to " << model_id.value();
                                    project->project_model_id = model_id.value();
                                }
                            }
                            boost::optional<std::string> name = root.get_optional<std::string>("name");
                            if (name.has_value()) {
                                ;//TODO project->project_name = name.value();
                            }
                            boost::optional<std::string> create_time = root.get_optional<std::string>("create_time");
                            if (create_time.has_value()) {
                                project->project_create_time = create_time.value();
                            }
                            boost::optional<std::string> content = root.get_optional<std::string>("content");
                            if (content.has_value()) {
                                project->project_content = content.value();
                            }
                            boost::optional<std::string> profiles = root.get_optional<std::string>("profiles");
                            boost::optional<std::string> url = root.get_optional<std::string>("url");
                            if (url.has_value()) {
                                project->project_url = url.value();
                            }
                            boost::optional<std::string> md5 = root.get_optional<std::string>("md5");
                            if (md5.has_value()) {
                                project->project_url_md5 = md5.value();
                            }
                            BOOST_LOG_TRIVIAL(info) << "get_project_info ok!";
                        }
                    }
                }
            )
            .on_error(
                [this](std::string body, std::string error, unsigned status) {
                    BOOST_LOG_TRIVIAL(info) << "get_project_info failed! body=" << body;
                }
            )
            .perform();
    }

    void AccountManager::get_profile_info(BBLProject* &project, BBLProfile* &profile)
    {
        if (!profile || !project) return;

        std::string query_params = (boost::format("?profile_id=%1%&gather=all") % profile->profile_id).str();

        std::string url = (boost::format("%1%/iot-service/api/user/project/%2%?%3%") % host % project->project_id % query_params).str();

        int retry_ = 0;
        int retry_max = 10;
        Http http = Http::get(url);
        while (project->project_url.empty() && retry_ < retry_max) {
            http.header("accept", "application/json")
                .header("Authorization", get_token_str())
                .on_complete(
                    [this, project, profile](std::string body, unsigned) {
                        std::stringstream ss(body);
                        pt::ptree root;
                        pt::read_json(ss, root);
                        boost::optional<std::string> message = root.get_optional<std::string>("message");
                        if (message.has_value()) {
                            if (message.value().compare(MSG_SUCCESS) == 0) {
                                BOOST_LOG_TRIVIAL(info) << "get_project_info ok!";
                                boost::optional<std::string> status = root.get_optional<std::string>("status");
                                boost::optional<std::string> model_id = root.get_optional<std::string>("model_id");
                                if (model_id.has_value()) { project->project_model_id = model_id.value(); }
                                boost::optional<std::string> name = root.get_optional<std::string>("name");
                                if (name.has_value()) {
                                    project->project_name = name.value();
                                }
                                boost::optional<std::string> create_time = root.get_optional<std::string>("create_time");
                                boost::optional<std::string> content = root.get_optional<std::string>("content");
                                boost::optional<std::string> profiles = root.get_optional<std::string>("profiles");
                                boost::optional<std::string> url = root.get_optional<std::string>("url");
                                if (url.has_value()) {
                                    // check valid url
                                    if (url.value().compare("null") != 0) {
                                        project->project_url = url.value();
                                    }
                                }
                                boost::optional<std::string> md5 = root.get_optional<std::string>("md5");
                                if (md5.has_value()) {
                                    if (md5.value().compare("null") != 0) {
                                        project->project_url_md5 = md5.value();
                                    }
                                }
                                if (root.get_child_optional("context") != boost::none) {
                                    profile->slice_info.clear();
                                    pt::ptree context = root.get_child("context");
                                    if (context.get_child_optional("thumbnail_files") != boost::none) {
                                        pt::ptree thumbnails = context.get_child("thumbnail_files");
                                        for (auto thumbnail = thumbnails.begin(); thumbnail != thumbnails.end(); ++thumbnail) {
                                            std::string index = thumbnail->second.get_optional<std::string>("index").value();
                                            std::string  name = thumbnail->second.get_optional<std::string>("name").value();
                                            std::string   dir = thumbnail->second.get_optional<std::string>("dir").value_or("");
                                            std::string   url = thumbnail->second.get_optional<std::string>("url").value();
                                            BBLSliceInfo* info = nullptr;
                                            if (profile->slice_info.find(index) != profile->slice_info.end()) {
                                                info = profile->slice_info.find(index)->second;
                                            }
                                            else {
                                                info = new BBLSliceInfo(profile);
                                                info->index = index;
                                            }
                                            info->thumbnail_name = name;
                                            info->thumbnail_dir = dir;
                                            info->thumbnail_url = url;
                                        }
                                    }
                                }
                            }
                        }
                    }
                )
                .on_error(
                    [this, project, profile](std::string body, std::string error, unsigned status) {
                        BOOST_LOG_TRIVIAL(info) << "get_project_info failed! body=" << body;
                    }
                )
                .perform_sync();
                retry_++;
                BOOST_LOG_TRIVIAL(trace) << "get_profile_info, retry=" << retry_;
                boost::this_thread::sleep_for(boost::chrono::milliseconds(1000));
        }
        if (retry_ == retry_max) {
            BOOST_LOG_TRIVIAL(trace) << "get_profile_info, retry_max";
        }
    }

    bool AccountManager::can_publish()
    {
        if (!is_user_login()) return false;
        return true;
    }

    int AccountManager::post_task(BBLProject* project, BBLProfile* profile, BBLSubTask* task, unsigned& http_code, std::string& http_body)
    {
        int result = -1;
        if (!project || !profile || !task) return -1;

        std::string url = (boost::format("%1%/user-service/my/task") % host).str();

        std::string post_body_str = json_request_body_post_subtask(project, profile, task);
        if (post_body_str.empty()) return -1;

        Http http = Http::post(url);
        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .header("Content-Type", "application/json")
            .set_post_body(post_body_str)
            .on_complete(
                [this, &result, &http_code, &http_body](std::string body, unsigned status) {
                    http_code = status;
                    http_body = body;
                    if (http_code == 200)
                        result = 0;
                }
            )
            .on_error(
                [this, &result, &http_code, &http_body](std::string body, std::string error, unsigned status) {
                    result = -1;
                    http_code = status;
                    http_body = body;
                }
            )
            .perform_sync();
        return result;
    }

    int AccountManager::get_ticket(std::string ticket, unsigned int &http_code, std::string &http_body)
    {
        if (ticket.empty()) return -1;

        int         result = -1;
        std::string url = (boost::format("%1%/user-service/my/ticket/%2%") % host % ticket).str();
        Http        http = Http::get(url);
        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .header("Content-Type", "application/json")
            .on_complete([this, &result, &http_code, &http_body](std::string body, unsigned status) {
                http_code = status;
                http_body = body;
                if (http_code == 200) {
                    try {
                        json j = json::parse(body);
                        if (j.contains("isValid")) {
                            if (j["isValid"].get<bool>() == true) {
                                result = 0;
                            }
                        }
                    }
                    catch(...) {
                        ;
                    }
                }
            })
            .on_error([this, &result, &http_code, &http_body](std::string body, std::string error, unsigned status) {
                result    = -1;
                http_code = status;
                http_body = body;
            })
            .perform_sync();
        return result;
    }

    int AccountManager::post_ticket(std::string ticket, unsigned int &http_code, std::string &http_body)
    {
        if (ticket.empty()) return -1;

        int result = -1;

        json j;
        j["ticket"]          = ticket;
        std::string json_str = j.dump();
        std::string url = (boost::format("%1%/user-service/my/ticket/%2%") % host % ticket).str();

        std::string              post_body_str = j.dump();
        if (post_body_str.empty()) return -1;

        Http http = Http::post(url);
        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .header("Content-Type", "application/json")
            .set_post_body(post_body_str)
            .on_complete([this, &result, &http_code, &http_body](std::string body, unsigned status) {
                http_code = status;
                http_body = body;
                if (http_code == 200) result = 0;
            })
            .on_error([this, &result, &http_code, &http_body](std::string body, std::string error, unsigned status) {
                result    = -1;
                http_code = status;
                http_body = body;
            })
            .perform_sync();
        return result;
    }

    int AccountManager::get_tasks(std::string dev_id, unsigned limit, unsigned &http_code, std::string &http_body)
    {
        if (dev_id.empty()) return -1;
        int result = -1;
        http_code = 0;
        http_body = "";

        std::string url = (boost::format("%1%/user-service/my/tasks?deviceId=%2%&limit=%3%") % host % dev_id % limit).str();
        Http http = Http::get(url);
        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .on_complete(
                [this, &result, &http_code, &http_body](std::string body, unsigned status) {
                try {
                    http_code = status;
                    http_body = body;
                    if (status == 200) result = 0;
                }
                catch(...) {
                    ;
                }
            })
            .on_error([this](std::string body, std::string error, unsigned status) {
                ;
            }).perform_sync();
        return result;
    }

    int AccountManager::update_country_code(std::string country_code)
    {
        config_json["country_code"] = country_code;
        return load_servers_from_region(country_code);
    }

    int AccountManager::load_servers_from_region(std::string country_code)
    {
        if (country_code == "CN") {
            user_region_server.iot_server_host  = "https://api.bambulab.cn/v1";
            user_region_server.api_servier_host = "https://api.bambulab.cn/";
            user_region_server.mqtt_server_host = "ssl://cn.mqtt.bambulab.com:8883";
            user_region_server.tutk_server_host = "CN";
            user_region_server.wifi_code = "CN";
            user_region_server.base_domain = "bambulab.cn";
            user_region_server.environment = "";
        }
        else if (country_code == "US") {
            user_region_server.iot_server_host = "https://api.bambulab.com/v1";
            user_region_server.api_servier_host = "https://api.bambulab.com/";
            user_region_server.mqtt_server_host = "ssl://us.mqtt.bambulab.com:8883";
            user_region_server.tutk_server_host = "US";
            user_region_server.wifi_code = "US";
            user_region_server.base_domain = "bambulab.com";
            user_region_server.environment = "";
        }
        else if (country_code == "ENV_CN_PRE") {
            user_region_server.iot_server_host = "https://api-pre.bambu-lab.com/v1";
            user_region_server.api_servier_host = "https://api-pre.bambu-lab.com/";
            user_region_server.mqtt_server_host = "ssl://47.100.225.51:8883";
            user_region_server.tutk_server_host = "CN";
            user_region_server.wifi_code = "CN";
            user_region_server.base_domain = "bambu-lab.com";
            user_region_server.environment = "-pre";
        }
        else if (country_code == "ENV_CN_QA") {
            user_region_server.iot_server_host = "https://api-qa.bambu-lab.com/v1";
            user_region_server.api_servier_host = "https://api-qa.bambu-lab.com/";
            user_region_server.mqtt_server_host = "ssl://47.100.225.51:8883";
            user_region_server.tutk_server_host = "CN";
            user_region_server.wifi_code = "CN";
            user_region_server.base_domain = "bambu-lab.com";
            user_region_server.environment = "-qa";
        }
        else if (country_code == "ENV_CN_DEV") {
            user_region_server.iot_server_host = "https://api-dev.bambu-lab.com/v1";
            user_region_server.api_servier_host = "https://api-dev.bambu-lab.com/";
            user_region_server.mqtt_server_host = "ssl://47.100.225.51:8883";
            user_region_server.tutk_server_host = "CN";
            user_region_server.wifi_code = "CN";
            user_region_server.base_domain = "bambu-lab.com";
            user_region_server.environment = "-dev";
        }
        else {
            user_region_server.iot_server_host = "https://api.bambulab.com/v1";
            user_region_server.api_servier_host = "https://api.bambulab.com/";
            user_region_server.mqtt_server_host = "ssl://us.mqtt.bambulab.com:8883";
            user_region_server.tutk_server_host = "ALL";
            user_region_server.wifi_code = "DE";
            user_region_server.base_domain = "https://bambulab.com";
            user_region_server.environment = "";
        }
        this->set_host(user_region_server.iot_server_host);

        BOOST_LOG_TRIVIAL(trace) << "region update iot = " << user_region_server.iot_server_host;
        BOOST_LOG_TRIVIAL(trace) << "region update api = " << user_region_server.api_servier_host;
        BOOST_LOG_TRIVIAL(trace) << "region update mqtt = " << user_region_server.mqtt_server_host;
        BOOST_LOG_TRIVIAL(trace) << "region update tutk = " << user_region_server.tutk_server_host;
        BOOST_LOG_TRIVIAL(trace) << "region update base domain = " << user_region_server.base_domain;
        BOOST_LOG_TRIVIAL(trace) << "region update environment = " << user_region_server.environment;
        BOOST_LOG_TRIVIAL(trace) << "region update wifi_code = " << user_region_server.wifi_code;
        return 0;
    }

    //BBS sync preset bundle when login
    int AccountManager::get_setting_list(std::string bundle_version, Http::ErrorFn errFn)
    {
        m_my_presets.clear();
        m_system_presets.clear();

        std::string query_params = (boost::format("?version=%s") % bundle_version).str();
        std::string url = (boost::format("%1%/iot-service/api/slicer/setting%2%") % host % query_params).str();
        Http http = Http::get(url);
        http.timeout_max(10)
            .header("accept", "application/json")
            .header("Authorization", get_token_str())
            .on_complete(
                [this](std::string body, unsigned) {
                    std::stringstream ss(body);
                    pt::ptree root;
                    pt::read_json(ss, root);
                    if (root.empty()) return;
                    boost::optional<std::string> message = root.get_optional<std::string>("message");
                    if (message.has_value() && message.value().compare(MSG_SUCCESS) == 0) {
                        if (root.get_child_optional(IOT_PRINTER_TYPE_STRING) != boost::none) {
                            pt::ptree printer_node = root.get_child(IOT_PRINTER_TYPE_STRING);
                            if (printer_node.get_child_optional("public") != boost::none) {
                                pt::ptree public_node = printer_node.get_child("public");
                                for (auto setting_item = public_node.begin(); setting_item != public_node.end(); ++setting_item) {
                                    parse_setting(setting_item->second, IOT_PRINTER_TYPE_STRING, "public");
                                }
                            }
                            if (printer_node.get_child_optional("private") != boost::none) {
                                    pt::ptree private_node = printer_node.get_child("private");
                                    for (auto setting_item = private_node.begin(); setting_item != private_node.end(); ++setting_item) {
                                        parse_setting(setting_item->second, IOT_PRINTER_TYPE_STRING, "private");
                                    }
                                }
                        }
                        if (root.get_child_optional(IOT_FILAMENT_STRING) != boost::none) {
                            pt::ptree filament_node = root.get_child(IOT_FILAMENT_STRING);
                            if (filament_node.get_child_optional("public") != boost::none) {
                                pt::ptree public_node = filament_node.get_child("public");
                                for (auto setting_item = public_node.begin(); setting_item != public_node.end(); ++setting_item) {
                                    parse_setting(setting_item->second, IOT_FILAMENT_STRING, "public");
                                }
                            }
                            if (filament_node.get_child_optional("private") != boost::none) {
                                pt::ptree private_node = filament_node.get_child("private");
                                for (auto setting_item = private_node.begin(); setting_item != private_node.end(); ++setting_item) {
                                    parse_setting(setting_item->second, IOT_FILAMENT_STRING, "private");
                                }
                            }
                        }
                        if (root.get_child_optional(IOT_PRINT_TYPE_STRING) != boost::none) {
                            pt::ptree print_node = root.get_child(IOT_PRINT_TYPE_STRING);
                            if (print_node.get_child_optional("public") != boost::none) {
                                pt::ptree public_node = print_node.get_child("public");
                                for (auto setting_item = public_node.begin(); setting_item != public_node.end(); ++setting_item) {
                                    parse_setting(setting_item->second, IOT_PRINT_TYPE_STRING, "public");
                                }
                            }
                            if (print_node.get_child_optional("private") != boost::none) {
                                pt::ptree private_node = print_node.get_child("private");
                                for (auto setting_item = private_node.begin(); setting_item != private_node.end(); ++setting_item) {
                                    parse_setting(setting_item->second, IOT_PRINT_TYPE_STRING, "private");
                                }
                            }
                        }
                        }
                    }
        ).on_error(errFn)
        .perform_sync();

        std::map<std::string, std::map<std::string, std::string>>::iterator it;
        for (it = m_my_presets.begin(); it != m_my_presets.end(); it++) {
            get_setting(it->first, it->second);
        }
        return 0;
    }

    void AccountManager::get_setting(std::string name, std::map<std::string, std::string>& values_map, std::function<void(void)> callback)
    {
        std::string setting_id = values_map[IOT_JSON_KEY_SETTING_ID];
        std::string url = (boost::format("%1%/iot-service/api/slicer/setting/%2%") % host % setting_id).str();
        Http http = Http::get(url);

        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .on_complete(
                [this, &values_map, callback](std::string body, unsigned) {
                    std::stringstream ss(body);
                    pt::ptree root;
                    pt::read_json(ss, root);
                    if (root.empty()) return;
                    boost::optional<std::string> message = root.get_optional<std::string>("message");
                    if (message.has_value() && message.value().compare(MSG_SUCCESS) == 0) {
                        boost::optional<bool> public_str = root.get_optional<bool>("public");
                        boost::optional<std::string> version = root.get_optional<std::string>(IOT_JSON_KEY_VERSION);
                        boost::optional<std::string> name = root.get_optional<std::string>(IOT_JSON_KEY_NAME);
                        boost::optional<std::string> type = root.get_optional<std::string>(IOT_JSON_KEY_TYPE);
                        boost::optional<std::string> base_id = root.get_optional<std::string>(IOT_JSON_KEY_BASE_ID);
                        boost::optional<std::string> filament_id = root.get_optional<std::string>(IOT_JSON_KEY_FILAMENT_ID);

                        /*if (name.has_value()) {
                            if (values_map[IOT_JSON_KEY_NAME].empty())
                                values_map[IOT_JSON_KEY_NAME] = name.value();
                        }*/

                        if (base_id.has_value()) {
                            values_map[IOT_JSON_KEY_BASE_ID] = base_id.value();
                            if (values_map[IOT_JSON_KEY_BASE_ID].compare("null") == 0)
                                values_map[IOT_JSON_KEY_BASE_ID].clear();
                        }

                        // check setting field and update setting field
                        if (root.get_child_optional("setting") != boost::none) {
                            pt::ptree setting_node = root.get_child("setting");
                            for (auto item = setting_node.begin(); item != setting_node.end(); ++item) {
                                values_map[item->first] = item->second.data();
                            }
                        }
                        if (version.has_value())
                            values_map[IOT_JSON_KEY_VERSION] = version.value();
                        if (filament_id.has_value()) {
                            values_map[IOT_JSON_KEY_FILAMENT_ID] = filament_id.value();
                        }
                        if (m_curr_user)
                            values_map[IOT_JSON_KEY_USER_ID] = m_curr_user->get_user_id();
                    }
                    if (callback) callback();
                }
        );
        if (callback == nullptr)
            http.perform_sync();
        else
            http.perform();
    }

    std::string AccountManager::request_setting_id(std::string name, std::map<std::string, std::string>& values_map, unsigned int& http_code)
    {
        std::string url = (boost::format("%1%/iot-service/api/slicer/setting") % host).str();

        std::vector<std::string> params;
        std::string request_str = json_request_body_post_setting(name, false, values_map);
        std::string new_setting_id;

        Http http = Http::post(url);
        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .header("Content-Type", "application/json")
            .set_post_body(request_str)
            .on_complete(
                [this, &new_setting_id, &http_code](std::string body, unsigned int code) {
                    BOOST_LOG_TRIVIAL(trace) << "request setting id, body=" << body;
                    http_code = code;
                    std::stringstream ss(body);
                    pt::ptree root;
                    pt::read_json(ss, root);
                    if (root.empty()) return;
                    boost::optional<std::string> message = root.get_optional<std::string>("message");
                    boost::optional<std::string> setting_id = root.get_optional<std::string>(IOT_JSON_KEY_SETTING_ID);
                    if (message.has_value()) {
                        if (message.value().compare(MSG_SUCCESS) == 0) {
                            if (setting_id.has_value()) {
                                new_setting_id = setting_id.value();
                                return;
                            }
                        }
                    }
                })
            .on_error([this, &http_code](std::string body, std::string error, unsigned code) {
                    http_code = code;
                    BOOST_LOG_TRIVIAL(trace) << "error = " << error << ", body = " << body << ", code = " << code;
                }
            ).perform_sync();
        return new_setting_id;
    }

    int AccountManager::put_setting(std::string setting_id, std::string name, std::map<std::string, std::string>& values_map, unsigned int& http_code)
    {
        int result = -1;
        int* result_ptr = &result;
        std::string request_body = json_request_body_put_setting(name, values_map);
        std::string url = (boost::format("%1%/iot-service/api/slicer/setting/%2%") % host % setting_id).str();
        Http http = Http::patch(url);
        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .header("Content-Type", "application/json")
            .set_post_body(request_body)
            .on_complete(
                [this, result_ptr, &http_code](std::string body, unsigned int code) {
                    http_code = code;
                    std::stringstream ss(body);
                    pt::ptree root;
                    pt::read_json(ss, root);
                    if (root.empty()) return;
                    boost::optional<std::string> message = root.get_optional<std::string>("message");
                    if (message.has_value()) {
                        if (message.value().compare(MSG_SUCCESS) == 0) {
                            BOOST_LOG_TRIVIAL(trace) << "put setting success!";
                            *result_ptr = 0;
                        }
                    }
                })
            .on_error([this, &http_code](std::string body, std::string error, unsigned code) {
                    http_code = code;
                    BOOST_LOG_TRIVIAL(trace) << "error = " << error << ", body = " << body << ", code = " << code;
                }
        ).perform_sync();

        return result;
    }

    int AccountManager::del_setting(std::string setting_id, unsigned int& http_code)
    {
        int result = -1;
        int* result_ptr = &result;
        std::string url = (boost::format("%1%/iot-service/api/slicer/setting/%2%") % host % setting_id).str();
        Http http = Http::del(url);
        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .on_complete(
                [this, result_ptr, &http_code](std::string body, unsigned int code) {
                    http_code = code;
                    std::stringstream ss(body);
                    pt::ptree root;
                    pt::read_json(ss, root);
                    if (root.empty()) return;
                    boost::optional<std::string> message = root.get_optional<std::string>("message");
                    if (message.has_value()) {
                        if (message.value().compare(MSG_SUCCESS) == 0) {
                            BOOST_LOG_TRIVIAL(trace) << "del setting success!";
                            *result_ptr = 0;
                        }
                    }
                })
            .on_error([this, &http_code](std::string body, std::string error, unsigned code) {
                    http_code = code;
                    BOOST_LOG_TRIVIAL(trace) << "error = " << error << ", body = " << body << ", code = " << code;
                }
        ).perform_sync();
        return result;
    }

    void AccountManager::parse_setting(pt::ptree node, std::string type, std::string attr)
    {
        bool is_system = attr.compare("public") == 0 ? true : false;
        if (is_system)
            _parse_preset_internal(m_system_presets, node, type, is_system);
        else
            _parse_preset_internal(m_my_presets, node, type, is_system);
    }

    void AccountManager::_parse_preset_internal(std::map<std::string, std::map<std::string, std::string>>& setting_maps, pt::ptree node, std::string type, bool is_system)
    {
        std::map<std::string, std::string> key_values;
        boost::optional<std::string> setting_id = node.get_optional<std::string>(IOT_JSON_KEY_SETTING_ID);
        boost::optional<std::string> version = node.get_optional<std::string>(IOT_JSON_KEY_VERSION);
        boost::optional<std::string> name = node.get_optional<std::string>(IOT_JSON_KEY_NAME);

        if (name.has_value() && setting_id.has_value()) {
            key_values[IOT_JSON_KEY_SETTING_ID] = setting_id.value();
            key_values[IOT_JSON_KEY_TYPE] = type;
            if (!is_system) {
                if (version.has_value()) {
                    key_values[IOT_JSON_KEY_VERSION] = version.value();
                }
                if (m_curr_user) {
                    key_values[IOT_JSON_KEY_USER_ID] = m_curr_user->get_user_id();
                }
            }
            setting_maps.emplace(name.value(), std::move(key_values));
        }
    }

    // update access_token
    int AccountManager::request_refreshtoken(std::string refresh_token)
    {
        //std::string refresh_token = m_curr_user->get_refresh_token();
        if (refresh_token.empty()) return -1;

        int result = -1;

        json j;
        j["refreshToken"] = refresh_token;
        std::string json_str = j.dump();
        std::string url = (boost::format("%1%/user-service/user/refreshtoken") % host).str();

        std::string post_body_str = j.dump();
        if (post_body_str.empty()) return -1;

        Http http = Http::post(url);
        http.header("accept", "application/json")
            .header("Content-Type", "application/json")
            .set_post_body(post_body_str)
            .on_complete([this, &result](std::string body, unsigned status) {
            if (status == 200) {
                try {
                    json j = json::parse(body);
                    result = 1;
                    m_curr_user->set_token(j["accessToken"].get<std::string>());
                    m_curr_user->set_expires_in(j["expiresIn"].get<int>() + std::time(nullptr));
                    m_curr_user->set_refresh_token(j["refreshToken"].get<std::string>());
                    m_curr_user->set_refresh_expires_in(j["refreshExpiresIn"].get<int>() + std::time(nullptr));
                    BOOST_LOG_TRIVIAL(trace) << "get access_token = " << m_curr_user->get_token();
                    BOOST_LOG_TRIVIAL(trace) << "get access_token_expires_in = " << std::to_string(m_curr_user->get_expires_in());
                    BOOST_LOG_TRIVIAL(trace) << "get refresh_token = " << m_curr_user->get_refresh_token();
                    BOOST_LOG_TRIVIAL(trace) << "get access_token_expires_in = " << std::to_string(m_curr_user->get_refresh_expires_in());

                }
                catch (...) {
                    result = -1;
                }
            }})
            .on_error([this, &result](std::string body, std::string error, unsigned status) {
                result = -1;
            })
            .perform_sync();
            return result;
    }

    std::string AccountManager::get_token_str(bool only_token)
    {
        if (m_curr_user) {
            if (m_curr_user->get_refresh_expires_in() - std::time(nullptr) > TOKEN_MIN_EXPIRES_IN) { 
                if (m_curr_user->get_expires_in() - std::time(nullptr) < TOKEN_MIN_EXPIRES_IN) {// need to update accessToken
                    if (request_refreshtoken(m_curr_user->get_refresh_token()) == -1) { // failed to acquire new accessToken
                        return "";
                    }
                }
                if (only_token) {
                    return m_curr_user->get_token();
                }
                return (boost::format("Bearer %1%") % m_curr_user->get_token()).str();
            }
            return ""; //invalid refreshToken
        }
        return "";
    }

    std::string AccountManager::get_user_name()
    {
        if (m_curr_user) {
            return m_curr_user->m_account;
        }
        return "";
    }

    std::string AccountManager::get_nick_name()
    {
        if (m_curr_user)
            return m_curr_user->m_name;
        return "";
    }

    void AccountManager::set_host(std::string host_url)
    {
        BOOST_LOG_TRIVIAL(trace) << "set host to " << host_url;
        host = host_url;
    }

    std::string AccountManager::_get_qeury_bind_list_url(std::vector<std::string> device_id_list)
    {
        std::string dev_id = "";
        std::vector<std::string>::iterator it;
        for (it = device_id_list.begin(); it != device_id_list.end(); it++) {
            if (it == device_id_list.begin())
                dev_id += *it;
            else {
                dev_id += "," + *it;
            }
        }

        if (m_curr_user) {
            return (boost::format("%1%/iot-service/api/user/bind_list?dev_ids=%2%") % host % dev_id).str();
        }
        else {
            return "";
        }
    }

    std::string AccountManager::_get_slicer_info_url()
    {
        return (boost::format("%1%/iot-service/api/slicer/resource") % host).str();
    }

    std::string AccountManager::_get_bind_list_url()
    {
        if (m_curr_user) {
            return (boost::format("%1%/iot-service/api/user/bind") % host).str();
        }
        else {
            return "";
        }
    }

    std::string AccountManager::_get_project_url() {
        return (boost::format("%1%/iot-service/api/user/project") % host).str();
    }

    bool AccountManager::_check_valid(std::string user, std::string password)
    {
        if (user.empty() || password.empty()) {
            return false;
        }
        return true;
    }

    std::string AccountManager::build_login_cmd()
    {
        json j = json::object();
        j["command"]        = "studio_userlogin";
        j["sequence_id"]    = "10001";
        j["data"]           = json::object();
        j["data"]["avatar"] = m_curr_user->m_avatar;
        j["data"]["name"]   = m_curr_user->m_name;

        return j.dump(-1, ' ', true, json::error_handler_t::ignore);
    }

    std::string AccountManager::build_logout_cmd()
    {
        json j = json::object();
        j["command"]        = "studio_useroffline";
        j["sequence_id"]    = "10001";
        return j.dump(-1, ' ', false, json::error_handler_t::ignore);
    }

    int AccountManager::start_bind(std::string dev_ip, OnUpdateStatusFn update_fn)
    {
        int result = 0;
        unsigned int http_code;
        std::string  http_body;
        bool was_cancelled = false;
        std::string msg;

        if (update_fn) {
            msg = "connecting";
            update_fn(LoginStageConnect, 0, msg);
        }

        BBL::LocalClient* local_client = new BBL::LocalClient();
        if (!local_client) {
            if (update_fn) update_fn(LoginStageConnect, -1, "create socket failed");
            return -1;
        }

        result = local_client->connect(dev_ip, LOCAL_COMMU_PORT);
        if (result < 0) {
            BOOST_LOG_TRIVIAL(trace) << "login_bind: local connect failed!";
            if (update_fn) update_fn(LoginStageConnect, -2, "connect failed");
            return -1;
        }

        
        if (update_fn) update_fn(LoginStageLogin, 0, "");

        std::string login_request = this->build_login_request();
        result = local_client->publish(login_request);
        if (result < 0) {
            BOOST_LOG_TRIVIAL(trace) << "login_bind: send login request failed, str = " << login_request;
            if (update_fn) update_fn(LoginStageLogin, -1, "send msg failed");
            local_client->disconnect();
            return -1;
        }

        if (update_fn) update_fn(LoginStageWaitForLogin, 0, "");

        std::string json_str;
        std::string login_ticket;
        bool        timeout = false;
        int         recv_count = 0;
        while (!timeout) {
            result = local_client->recv(json_str);
            if (!json_str.empty() && result >= 0) {
                try {
                    BOOST_LOG_TRIVIAL(trace) << "login_bind: json_str = " << json_str;
                    json j = json::parse(json_str);
                    if (j.contains("login") && !j["login"].is_null()) {
                        if (j["login"]["command"].get<std::string>() == "login_report") {
                            std::string status;
                            if (j["login"].contains("status"))
                                status = j["login"]["status"].get<std::string>();
                            if (j["login"].contains("ticket"))
                                login_ticket = j["login"]["ticket"].get<std::string>();
                            if (status.compare("wait_auth") == 0 && !login_ticket.empty()) {
                                break;
                            }
                        }
                    }
                }
                catch (...) {
                    ;
                }
            }
            recv_count++;
            if (recv_count > 10) {
                timeout = true;
            }
        }


        if (timeout || login_ticket.empty()) {
            BOOST_LOG_TRIVIAL(trace) << "login_bind: timeout to get ticket";
            if (update_fn) update_fn(LoginStageWaitForLogin, RET_ERR_TIMEOUT, "timeout");
            local_client->disconnect();
            return -1;
        }

        if (update_fn) update_fn(LoginStageGetIdentify, 0, "");

        result = get_ticket(login_ticket, http_code, http_body);
        if (result < 0) {
            BOOST_LOG_TRIVIAL(trace) << "login_bind: http_code = " << http_code << ", http_body = " << http_body;
            if (update_fn) update_fn(LoginStageGetIdentify, http_code, http_body);
            local_client->disconnect();
            return -1;
        }

        result = post_ticket(login_ticket, http_code, http_body);
        if (result < 0) {
            if (update_fn) update_fn(LoginStageGetIdentify, http_code, http_body);
            BOOST_LOG_TRIVIAL(trace) << "login_bind: http_code = " << http_code << ", http_body = " << http_body;
            local_client->disconnect();
            return -1;
        }

        if (update_fn) update_fn(LoginStageWaitAuth, 0, "");
        recv_count = 0;
        while (!timeout) {
            boost::this_thread::sleep_for(boost::chrono::milliseconds(1000));
            result = local_client->recv(json_str);
            if (result >= 0) {
                BOOST_LOG_TRIVIAL(trace) << "login_bind: json_str = " << json_str;
                std::string fail_reason;
                result = this->_parse_login_report(json_str, fail_reason);
                if (result < 0) {
                    BOOST_LOG_TRIVIAL(trace) << "login_bind: bind failed reason = " << fail_reason;
                    if (update_fn) update_fn(LoginStageWaitAuth, -1, fail_reason);
                    local_client->disconnect();
                    return -1;
                }
                else if (result == 0) {
                    break;
                }
                else if (result == 1) {
                    ; // continue
                }
            }
            recv_count++;
            if (recv_count > 20) { timeout = true; }
        }
        if (timeout) {
            if (update_fn) update_fn(LoginStageWaitAuth, RET_ERR_TIMEOUT, "");
            BOOST_LOG_TRIVIAL(trace) << "login_bind: timeout to receive login_report";
            local_client->disconnect();
            return -1;
        }

        local_client->disconnect();
        
        if (update_fn) update_fn(LoginStageFinished, 0, "");
        return 0;
    }


std::string AccountManager::build_login_request() {
    json j;
    j["login"]["sequence_id"] = std::to_string(AccountManager::m_sequence_id++);
    j["login"]["command"]     = "login";
    j["login"]["wifi"]        = user_region_server.wifi_code;
    j["login"]["tutk"]        = user_region_server.tutk_server_host;
    j["login"]["iot"]         = get_host();
    j["login"]["apix"]        = user_region_server.api_servier_host;
    j["login"]["emqx"]        = get_emqx_server_host();
    j["login"]["base_domain"] = user_region_server.base_domain;
    j["login"]["environment"] = user_region_server.environment;
    return j.dump();
}

int AccountManager::_parse_login_report(std::string json_str, std::string fail_reason)
{
    try {
        json j = json::parse(json_str);
        if (j["login"]["command"].get<std::string>() == "login_report") {
            std::string status = j["login"]["status"].get<std::string>();
            if (status == "SUCCESS") {
                return 0;
            }
            else if (status == "wait_auth") {
                // continue to wait
                return 1;
            }
            else {
                fail_reason = j["login"]["reason"].get<std::string>();
                return -1;
            }
        }
    }
    catch (...) {
    }
    return -1;
}


int AccountManager::start_print(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn)
{
    int res = 0;
    unsigned http_code = 0;
    std::string http_body;
    std::string msg;

    // Create Printing Job
    if (update_fn) update_fn(PrintingStageCreate, 0, "");

    BBLProject* project = new BBLProject(params.project_name);
    project->project_3mf_file = params.config_filename;
    project->project_path = fs::path(params.config_filename);

    BBLProfile* profile = new BBLProfile(project);
    profile->profile_name = params.preset_name;

    BOOST_LOG_TRIVIAL(trace) << "print_job: request project id";
    res = request_project_profile_id(project, profile, http_code, http_body);

    if (res < 0 || project->project_id.empty()
        || profile->profile_id.empty()
        || profile->upload_ticket.empty()
        || profile->upload_url.empty()) {
        update_fn(PrintingStageCreate, http_code, http_body);
        BOOST_LOG_TRIVIAL(trace) << "print_job: request project id failed. code = " << http_code << ", http_body = " << http_body;
        return -1;
    }

    BOOST_LOG_TRIVIAL(trace) << "print_job: get project id = " << project->project_id;
    BOOST_LOG_TRIVIAL(trace) << "print_job: get profile id = " << profile->profile_id;

    if (cancel_fn && cancel_fn()) {
        return -1;
    }

    // Uploading 3mf File
    if (update_fn) update_fn(PrintingStageUpload, 0, "");

    BOOST_LOG_TRIVIAL(trace) << "print_job: start to uploading...";
    // check file size
    if (!Http::check_file_size(project->project_path)) {
        if (update_fn) update_fn(PrintingStageUpload, RET_ERR_OVERSIZE, "The size of the uploaded file cannot exceed 1 GB.");
        return -1;
    }

    res = this->upload_3mf_to_oss(profile, http_code, http_body,
        [this, cancel_fn, update_fn, &msg](Http::Progress progress, bool& cancel) {
            if (cancel_fn && cancel_fn()) {
                cancel = true;
                return;
            }
        });

    if (cancel_fn && cancel_fn()) {
        return -1;
    }

    if (res < 0) {
        if (res == RET_MD5_CHECK_FAILED) {
            msg = "check md5 failed.";
            if (update_fn) update_fn(PrintingStageUpload, RET_MD5_CHECK_FAILED, msg);
            BOOST_LOG_TRIVIAL(trace) << "print_job: uploading failed, check md5 failed";
        }
        else {
            if (update_fn) update_fn(PrintingStageUpload, http_code, http_body);
            BOOST_LOG_TRIVIAL(trace) << "print_job: uploading is failed";
        }
        return -1;
    }

    /* put notifications */
    res = this->put_notification(profile, project->project_path.filename().string(), http_code, http_body);
    if (res < 0) {
        if (update_fn) update_fn(PrintingStageUpload, http_code, http_body);
        return -1;
    }

    /* get notifications */
    bool cancel = false;
    int  timeout = this->calc_get_notification_timeout(project->project_path);
    res = this->get_notification(profile, http_code, http_body,
        [this, cancel_fn]() {
            if (cancel_fn)
                return cancel_fn();
            return false;
        }, timeout);

    if (res < 0) {
        if (res == RET_ERR_CANCEL) {
            if (update_fn) update_fn(PrintingStageUpload, RET_ERR_CANCEL, "");
        } else if (res == RET_ERR_TIMEOUT) {
            if (update_fn) update_fn(PrintingStageUpload, RET_ERR_TIMEOUT, "");
        } else {
            if (update_fn) update_fn(PrintingStageUpload, http_code, http_body);
        }
        return -1;
    }

    /* upload 3mf to oss */
    std::string file_3mf_url = (boost::format("%1%_%2%_%3%.3mf") % project->project_model_id % profile->profile_id % params.plate_index).str();
    std::vector<std::string> files;
    std::vector<std::string> urls;
    files.push_back(file_3mf_url);
    res = this->get_user_upload(files, urls);
    if (res < 0 || urls.empty()) {
        BOOST_LOG_TRIVIAL(info) << "get_user_upload failed";
        return -1;
    }


    // calc md5
    std::string md5_str;
    fs::path upload_file = fs::path(params.filename);
    std::string full_filename = upload_file.generic_string();
    bbl_calc_md5(full_filename, md5_str);
    BOOST_LOG_TRIVIAL(trace) << "print_job: upload_3mf(withgcode) md5 = " << md5_str;

    // update to
    res = this->upload_file_to_oss(urls[0], upload_file, md5_str, http_code, http_body,
        [this, cancel_fn, update_fn, &msg](Http::Progress progress, bool& cancel) {
            if (cancel_fn && cancel_fn()) {
                cancel = true;
                return;
            }

            msg = (boost::format("%1%/%2%") % get_transform_string(progress.ulnow) % get_transform_string(progress.ultotal)).str();
            if (update_fn) update_fn(PrintingStageUpload, 0, msg);
        });

    if (cancel_fn && cancel_fn()) {
        return -1;
    }

    if (res < 0) {
        BOOST_LOG_TRIVIAL(info) << "print_job: upload_file_to_oss failed";
        return -1;
    }

    json j, j_file;
    json j_url = json::array();
    j["profile_id"] = profile->profile_id;
    j_file["plate_idx"] = params.plate_index;
    j_file["url"] = urls[0];
    j_file["md5"] = md5_str;
    j_url.push_back(j_file);
    j["profile_print_3mf"] = j_url;

    res = this->patch_project(project, j.dump(), http_code, http_body);
    if (res < 0) {
        BOOST_LOG_TRIVIAL(info) << "patch project failed";
        return -1;
    }

    /* post task */
    if (update_fn) update_fn(PrintingStageSending, 0, "");

    BBLSubTask* subTask = new BBLSubTask();

    //BBS hide bed choice
    subTask->task_mode              = "cloud_file";
    subTask->task_bed_leveling      = params.task_bed_leveling;
    subTask->task_flow_cali         = params.task_flow_cali;
    subTask->task_vibration_cali    = params.task_vibration_cali;
    subTask->task_record_timelapse  = params.task_record_timelapse;
    subTask->task_layer_inspect     = params.task_layer_inspect;
    subTask->task_gcode_in_3mf      = (boost::format("Metadata/plate_%1%.gcode") % (params.plate_index)).str();
    subTask->task_partplate_idx     = std::to_string(params.plate_index);
    subTask->task_printer_dev_id    = params.dev_id;
    subTask->task_ams_mapping       = params.ams_mapping;
    subTask->task_mode              = "cloud_file";
    if (project->project_name.empty())
        subTask->task_name = (boost::format("Plate %1%") % params.plate_index).str();
    else
        subTask->task_name = params.project_name;

    res = this->post_task(project, profile, subTask, http_code, http_body);
    if (res < 0) {
        if (update_fn) update_fn(PrintingStageSending, http_code, http_body);
        return -1;
    }

    if (profile)
        delete profile;
    if (project)
        delete project;
    if (subTask)
        delete subTask;
    
    if (update_fn) update_fn(PrintingStageSending, 0, "");
    return 0;
}

int AccountManager::start_local_print_with_record(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn)
{
    int res = 0;
    unsigned http_code = 0;
    std::string http_body;
    std::string msg;

    // Create Printing Job
    if (update_fn) update_fn(PrintingStageCreate, 0, "");

    BBLProject* project = new BBLProject(params.project_name);
    project->project_3mf_file = params.config_filename;
    project->project_path = fs::path(params.config_filename);

    BBLProfile* profile = new BBLProfile(project);
    profile->profile_name = params.preset_name;

    BOOST_LOG_TRIVIAL(trace) << "print_job: request project id";
    res = request_project_profile_id(project, profile, http_code, http_body);

    if (res < 0 || project->project_id.empty()
        || profile->profile_id.empty()
        || profile->upload_ticket.empty()
        || profile->upload_url.empty()) {
        update_fn(PrintingStageCreate, http_code, http_body);
        BOOST_LOG_TRIVIAL(trace) << "print_job: request project id failed. code = " << http_code << ", http_body = " << http_body;
        return -1;
    }

    BOOST_LOG_TRIVIAL(trace) << "print_job: get project id = " << project->project_id;
    BOOST_LOG_TRIVIAL(trace) << "print_job: get profile id = " << profile->profile_id;

    if (cancel_fn && cancel_fn()) {
        return -1;
    }

    // Uploading 3mf File
    if (update_fn) update_fn(PrintingStageUpload, 0, "");

    BOOST_LOG_TRIVIAL(trace) << "print_job: start to uploading...";
    // check file size
    if (!Http::check_file_size(project->project_path)) {
        if (update_fn) update_fn(PrintingStageUpload, RET_ERR_OVERSIZE, "The size of the uploaded file cannot exceed 1 GB.");
        return -1;
    }

    res = this->upload_3mf_to_oss(profile, http_code, http_body,
        [this, cancel_fn, update_fn, &msg](Http::Progress progress, bool& cancel) {
            if (cancel_fn && cancel_fn()) {
                cancel = true;
                return;
            }

            msg = (boost::format("%1%/%2%") % get_transform_string(progress.ulnow) % get_transform_string(progress.ultotal)).str();
            if (update_fn) update_fn(PrintingStageUpload, 0, msg);
        });

    if (cancel_fn && cancel_fn()) {
        return -1;
    }

    if (res < 0) {
        if (res == RET_MD5_CHECK_FAILED) {
            msg = "check md5 failed.";
            if (update_fn) update_fn(PrintingStageUpload, RET_MD5_CHECK_FAILED, msg);
            BOOST_LOG_TRIVIAL(trace) << "print_job: uploading failed, check md5 failed";
        }
        else {
            if (update_fn) update_fn(PrintingStageUpload, http_code, http_body);
            BOOST_LOG_TRIVIAL(trace) << "print_job: uploading is failed";
        }
        return -1;
    }


    if (update_fn) update_fn(PrintingStageWaiting, 0, "");

    /* put notifications */
    res = this->put_notification(profile, project->project_path.filename().string(), http_code, http_body);
    if (res < 0) {
        if (update_fn) update_fn(PrintingStageWaiting, http_code, http_body);
        return -1;
    }

    /* get notifications */
    bool cancel = false;
    int  timeout = this->calc_get_notification_timeout(project->project_path);
    res = this->get_notification(profile, http_code, http_body,
        [this, cancel_fn]() {
            if (cancel_fn)
                return cancel_fn();
            return false;
        }, timeout);

    if (res < 0) {
        if (res == RET_ERR_CANCEL) {
            if (update_fn) update_fn(PrintingStageWaiting, RET_ERR_CANCEL, "");
        }
        else if (res == RET_ERR_TIMEOUT) {
            if (update_fn) update_fn(PrintingStageWaiting, RET_ERR_TIMEOUT, "");
        }
        else {
            if (update_fn) update_fn(PrintingStageWaiting, http_code, http_body);
        }
        return -1;
    }

    /* post task */
    if (update_fn) update_fn(PrintingStageSending, 0, "");

    /* sftp uploading */
    std::string src_file = params.filename;
    std::string dst_file = "/local_print.gcode.3mf";
    std::string dev_ip = params.dev_ip;
    Sftp sftp = Sftp::upload(dev_ip, src_file, dst_file, params.username, params.password);
    sftp.on_complete(
        [this, &res](std::string body) {
            res = 0;
        })
        .on_progress(
            [this, cancel_fn, update_fn, &msg](Sftp::Progress progress, bool& cancel) {
                if (cancel_fn && cancel_fn()) {
                    cancel = true;
                    return;
                }
                msg = (boost::format("%1%/%2%") % get_transform_string(progress.ulnow) % get_transform_string(progress.ultotal)).str();
                if (update_fn) update_fn(PrintingStageUpload, 0, msg);
            }
        ).on_error(
            [this, &res, &msg](std::string error) {
                msg = error;
                res = -1;
            }
        ).perform_sync();

    if (res < 0) {
        BOOST_LOG_TRIVIAL(trace) << "print_job: uploading failed, check md5 failed";
        msg = "upload failed";
        if (update_fn) update_fn(PrintingStageUpload, -1, msg);
        return -1;
    }

    BBLSubTask* subTask = new BBLSubTask();

    //BBS hide bed choice
    subTask->task_bed_leveling = params.task_bed_leveling;
    subTask->task_flow_cali = params.task_flow_cali;
    subTask->task_vibration_cali = params.task_vibration_cali;
    subTask->task_record_timelapse = params.task_record_timelapse;
    subTask->task_layer_inspect = params.task_layer_inspect;
    subTask->task_gcode_in_3mf = (boost::format("Metadata/plate_%1%.gcode") % (params.plate_index)).str();
    subTask->task_partplate_idx = std::to_string(params.plate_index);
    subTask->task_printer_dev_id = params.dev_id;
    subTask->task_ams_mapping = params.ams_mapping;
    subTask->task_mode = "lan_file";
    if (project->project_name.empty())
        subTask->task_name = (boost::format("Plate %1%") % params.plate_index).str();
    else
        subTask->task_name = params.project_name;

    res = this->post_task(project, profile, subTask, http_code, http_body);
    if (res < 0) {
        if (update_fn) update_fn(PrintingStageSending, http_code, http_body);
        return -1;
    }

    if (profile)
        delete profile;
    if (project)
        delete project;
    if (subTask)
        delete subTask;

    if (update_fn) update_fn(PrintingStageSending, 0, "");
    return 0;
}

int AccountManager::start_local_print(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn)
{
    int res = 0;
    unsigned http_code = 0;
    std::string http_body;
    std::string msg;

    // Create Printing Job
    if (update_fn) update_fn(PrintingStageCreate, 0, "");

    // Uploading 3mf File
    if (update_fn) update_fn(PrintingStageUpload, 0, "");
    BOOST_LOG_TRIVIAL(trace) << "local_print_job: start to uploading...";

    fs::path file_path = fs::path(params.filename);
    if (!Http::check_file_size(file_path)) {
        if (update_fn) update_fn(PrintingStageUpload, RET_ERR_OVERSIZE, "The size of the uploaded file cannot exceed 1 GB.");
        return -1;
    }

    std::string src_file = params.filename;
    std::string dst_file = "/local_print.gcode.3mf";
    std::string dev_ip = params.dev_ip;
    Sftp sftp = Sftp::upload(dev_ip, src_file, dst_file, params.username, params.password);
    sftp.on_complete(
        [this, &res](std::string body) {
            res = 0;
        })
        .on_progress(
            [this, cancel_fn, update_fn, &msg](Sftp::Progress progress, bool& cancel) {
                if (cancel_fn && cancel_fn()) {
                    cancel = true;
                    return;
                }
                msg = (boost::format("%1%/%2%") % get_transform_string(progress.ulnow) % get_transform_string(progress.ultotal)).str();
                if (update_fn) update_fn(PrintingStageUpload, 0, msg);
            }
        ).on_error(
            [this, &res, &msg](std::string error) {
                msg = error;
                res = -1;
            }
        ).perform_sync();

    if (res < 0) {
        BOOST_LOG_TRIVIAL(trace) << "print_job: uploading failed, check md5 failed";
        msg = "upload failed";
        if (update_fn) update_fn(PrintingStageUpload, -1, msg);
        return -1;
    }

    // calc md5
    std::string md5_str;
    fs::path upload_file = fs::path(params.filename);
    std::string full_filename = upload_file.generic_string();
    bbl_calc_md5(full_filename, md5_str);
    BOOST_LOG_TRIVIAL(trace) << "print_job: upload_3mf(withgcode) md5 = " << md5_str;

    // mqtt print
    if (update_fn) update_fn(PrintingStageWaiting, 0, "");
    json j;
    j["print"]["command"] = "project_file";
    j["print"]["sequence_id"] = std::to_string(AccountManager::m_sequence_id++);
    j["print"]["param"] = (boost::format("Metadata/plate_%1%.gcode") % params.plate_index).str();
    j["print"]["project_id"] = "0";
    j["print"]["profile_id"] = "0";
    j["print"]["task_id"] = "0";
    j["print"]["subtask_id"] = "0";
    j["print"]["subtask_name"] = params.project_name;
    //TODO fixed prefix
    j["print"]["url"] = "file:///userdata" + dst_file;
    j["print"]["md5"] = md5_str;
    j["print"]["timelapse"] = params.task_record_timelapse;
    j["print"]["bed_type"] = "auto";
    j["print"]["bed_leveling"] = params.task_bed_leveling;
    j["print"]["flow_cali"] = params.task_flow_cali;
    j["print"]["vibration_cali"] = params.task_vibration_cali;
    j["print"]["layer_inspect"] = params.task_layer_inspect;

    std::string topic = (boost::format("device/%1%/request") % params.dev_id).str();
    std::string json_str = j.dump();
    auto mqtt_msg = mqtt::message::create(topic, json_str, 1, false);
    mqtt_local_cli->publish(mqtt_msg);
    
    if (update_fn) update_fn(PrintingStageSending, 0, "");
    return 0;
}

}
