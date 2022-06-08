#include "libslic3r/libslic3r.h"
#include "AccountManager.hpp"
#include "DeviceManager.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/Utils/Http.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include <thread>
#include <mutex>
#include <codecvt>
#include <boost/filesystem/path.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/foreach.hpp>
#include <boost/typeof/typeof.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

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

namespace Slic3r {


    void action_listener::on_success(const mqtt::token& tok) {
        // re sucscribe the monitoring printer
        AccountManager *manager = (AccountManager *) context_;
        for (int i = 0; i < tok.get_topics()->size(); i++) {
            BOOST_LOG_TRIVIAL(trace) << "subscribe topic:" << (*tok.get_topics())[i].c_str() << " success";
            std::string topic_str = (*tok.get_topics())[i];
            // topic_str = device/device_id/report
            std::vector<std::string> params;
            boost::split(params, topic_str, boost::is_any_of("/"));
            // BBS device, dev_id, report at least 3 params
            /* params[1] is dev id, topic is : device/[dev_id]/report */
            if (params.size() <= 2) return;
            std::string dev_id = params[1];
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

            /* params[1] is dev id, topic is : device/[dev_id]/report */
            std::map<std::string, MachineObject*>::iterator it = manager->myBindMachineList.find(params[1]);
            if (it == manager->myBindMachineList.end()) return;

            if (it->second) {
                BOOST_LOG_TRIVIAL(trace) << "message topic:" << msg->get_topic() << ", payload=" << msg->get_payload_str();
                it->second->parse_json(msg->get_topic(), msg->get_payload_str());

#if !BBL_RELEASE_TO_PUBLIC
                if (it->second->is_ams_need_update) {
                    if (manager->on_ams_update_fn) {
                        manager->on_ams_update_fn(params[1]);
                    }
                }
#endif
            }
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
        ifstream json_file(encode_path(dir_str.c_str()));
        try {
            if (json_file.is_open()) {
                json_file >> config_json;
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
        mqtt_uuid_bytes(4),
        default_project(new BBLProject())
    {
        default_profile = new BBLProfile(default_project);
        mqtt_opt.set_max_inflight(500);
        mqtt_opt.set_connect_timeout(10);
        mqtt_opt.set_ssl(mqtt_ssl_opt);

        m_curr_user = nullptr;
    }

    AccountManager::~AccountManager()
    {
        save_config();
        if (mqtt_cli->is_connected())
            mqtt_cli->disconnect();
        Http::disable_log();
    }

    void AccountManager::set_product_mqtt_opt()
    {
        std::string trust_store = resources_dir() + "/cert/slicer_base64.cer";
        mqtt_ssl_opt.set_trust_store(trust_store);
        mqtt_opt.set_ssl(mqtt_ssl_opt);
    }

    void AccountManager::set_engineering_mqtt_opt()
    {
        mqtt_ssl_opt.ca_path(resources_dir() + "/cert");
        std::string key_store = resources_dir() + "/cert/slicer.crt";
        std::string trust_store = resources_dir() + "/cert/slicer_chain.crt";
        std::string private_key = resources_dir() + "/cert/slicer_pri.pem";
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
        auto log_folder = (boost::filesystem::path(data_dir()) / "log").make_preferred();
        if (!boost::filesystem::exists(log_folder)) {
            boost::filesystem::create_directory(log_folder);
        }
//#if !BBL_RELEASE_TO_PUBLIC
        auto http_log_path = ( log_folder / buf.str()).make_preferred();
        std::string log_filename = encode_path(http_log_path.string().c_str());
        Http::enable_log(log_filename.c_str());
//#endif
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
                on_user_login_fn(0);
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

    void AccountManager::set_monitor_machine(std::string dev_id)
    {
        BOOST_LOG_TRIVIAL(trace) << "set monitor machine = " << dev_id;
        std::string old_dev_id = this->default_machine;

        // store last_monitor_printer
        config_json["agent"]["last_monitor_machine"] = dev_id;
        this->default_machine = dev_id;

        //unsubscribe old machine
        if (!old_dev_id.empty() && old_dev_id.compare(dev_id) != 0) {
            this->del_subscribe(old_dev_id);
        }

        //subscribe new machine, call start_subscribe first
        if (m_is_subscribing) {
            this->add_subscribe(dev_id);
        }

        std::map<std::string, MachineObject *>::iterator it = myBindMachineList.find(dev_id);
        if (it != myBindMachineList.end()) {
            it->second->reset();
        }
    }

    void AccountManager::load_last_machine()
    {
        if (myBindMachineList.empty()) return;
        else if (myBindMachineList.size() == 1) {
            auto it = myBindMachineList.begin();
            if (it != myBindMachineList.end() && it->second)
                set_monitor_machine(it->second->dev_id);
        } else {
            std::string last_monitor_machine = get_config("agent", "last_monitor_machine");
            auto it = myBindMachineList.find(last_monitor_machine);
            if (it != myBindMachineList.end()) {
                set_monitor_machine(it->second->dev_id);
            } else {
                auto it = myBindMachineList.begin();
                if (it != myBindMachineList.end() && it->second)
                    set_monitor_machine(it->second->dev_id);
            }
        }
    }

    void AccountManager::on_printer_connected(std::string dev_id)
    {
        /* request_pushing_print */
        std::map<std::string, MachineObject *>::iterator it = myBindMachineList.find(dev_id);
        if (it != myBindMachineList.end()) {
            it->second->command_request_push_all();
            it->second->command_get_version();
        }
    }
    

    void AccountManager::start_subscribe(std::string module)
    {
        BOOST_LOG_TRIVIAL(trace) << "start_subscribe, machine=" << default_machine << ", module = " << module;
        if (!default_machine.empty())
            this->add_subscribe(default_machine);

        MachineObject* obj = get_default_machine();
        if (obj)
            obj->reset();
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

    int AccountManager::user_login_autotest(std::string account, std::string password)
    {
        std::string::size_type pos = account.find_last_of('@');
        if (pos == account.npos) {
            BOOST_LOG_TRIVIAL(trace) << "invalid account name = " << account;
            return -1;
        }
        account = account.substr(0, pos);
        std::string url = "https://keycloak-qa.bambu-lab.com/auth/realms/staff/protocol/openid-connect/token";
        std::string post_body = (boost::format("username=%1%&password=%2%&grant_type=password&client_id=slicer&client_secret=98f3173c-b4cf-4610-b265-dd4867af8241")
                                % account
                                % password).str();
        Http http = Http::post(url);
        http.header("Content-Type", "application/x-www-form-urlencoded")
            .set_post_body(post_body)
            .on_complete([&, this](std::string body, unsigned) {
                    try {
                        json j = json::parse(body);
                        m_curr_user = new AccountInfo("", "", AccountInfo::LoginStatus::STATUS_LOGIN);
                        m_curr_user->m_autotest_token = j["access_token"].get<std::string>();
                    }
                    catch (...) {
                        ;
                    }
                })
            .on_error([&, this](std::string body, std::string error, unsigned status) {
                    BOOST_LOG_TRIVIAL(trace) << "error = " << error << ", body = " << body << ", status = " << status;
                }
            ).perform_sync();
        return 0;
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
        default_machine = "";
        default_project = nullptr;
        std::lock_guard<std::mutex> lock(listMutex);
        std::map<std::string, MachineObject*>::iterator it;
        for (it = myBindMachineList.begin(); it != myBindMachineList.end(); it++) {
            delete it->second;
            it->second = nullptr;
        }
        myBindMachineList.clear();
        mqtt_topics.clear();
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
        int result = -1;
        if (!profile || profile->upload_ticket.empty()) return -1;

        /* retry 120 seconds, 60 * 2 seconds */
        int retry = 0, retry_max = POLL_NOTIFICATION_TIMEOUT / POLL_NOTIFICATION_INTERVAL;

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
                if (cancel_fn()) return RET_POLLING_CANEL;
            }

            retry++;
            BOOST_LOG_TRIVIAL(trace) << "get notification, retry = " << retry;
            std::chrono::system_clock::time_point last_update_time = std::chrono::system_clock::now();
            auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(curr_time - last_update_time);
            if (diff.count() > timeout * 1000) {
                has_timeout = true;
            }
            boost::this_thread::sleep_for(boost::chrono::milliseconds(POLL_NOTIFICATION_INTERVAL * 1000));
        }
        if (has_timeout) {
            /* timeout */
            return RET_POLLING_TIMEOUT;
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
    int AccountManager::get_print_info(std::string& result, int& err_code, std::string err_msg, bool sync)
    {
        std::string message;
        std::string url = (boost::format("%1%/iot-service/api/user/print?force=true&device_id=") % host).str();
        Http http  = Http::get(url);
        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .header("Content-Type", "application/json")
            .on_complete([&result, &err_code, &err_msg, &message](std::string body, unsigned status) {
                try {
                    json j = json::parse(body);
                    result = body;
                    message = j["message"];
                    if (!j["code"].is_null())
                        err_code = j["code"].get<int>();
                    if (!j["error"].is_null())
                        err_msg = j["error"].get<std::string>();
                }
                catch(...) {
                    ;
                }
            })
            .on_error([&err_code, &err_msg](std::string body, std::string error, unsigned status) {
                err_msg = (boost::format("status=%1%, body=%2%") % status %body).str();
            }).perform_sync();
        if (sync) {
            http.perform_sync();
            if (message == MSG_SUCCESS) {
                return 0;
            }
            return -1;
        } else {
            http.perform();
            return 0;
        }
        return 0;
    }

    void AccountManager::update_my_machine_list_info(int &err_code, std::string &err_msg, bool sync)
    {
        std::vector<MachineObject*>  show_list;
        std::string print_info;
        int result = get_print_info(print_info, err_code, err_msg, sync);
        if (result == 0) {
            try {
                json j = json::parse(print_info);


                if (!j["devices"].is_null() && j["devices"].is_array()) {
                    for (auto& elem : j["devices"]) {
                        MachineObject* obj = new MachineObject(*this, "", "", "");
                        if (!elem["dev_id"].is_null())
                            obj->dev_id = elem["dev_id"];
                        if (!elem["dev_name"].is_null())
                            obj->dev_name = elem["dev_name"];
                        if (!elem["dev_online"].is_null())
                            obj->m_is_online = elem["dev_online"].get<bool>();
                        if (!elem["progress"].is_null())
                            obj->mc_print_percent = elem["progress"].get<int>();
                        if (!elem["task_name"].is_null())
                            obj->iot_printing_taskname = elem["task_name"].get<std::string>();
                        if (!elem["task_id"].is_null())
                            obj->iot_task_id = elem["task_id"].get<std::string>();
                        if (!elem["profile_id"].is_null())
                            obj->iot_profile_id = elem["profile_id"].get<std::string>();
                        if (!elem["project_id"].is_null())
                            obj->iot_project_id = elem["project_id"].get<std::string>();
                        if (!elem["task_status"].is_null())
                            obj->iot_task_status = elem["task_status"].get<std::string>();
                        if (elem.contains("dev_model_name") && !elem["dev_model_name"].is_null())
                            obj->printer_type = MachineObject::parse_iot_printer_type(elem["dev_model_name"].get<std::string>());
                        if (elem.contains("dev_product_name") && !elem["dev_product_name"].is_null())
                            obj->product_name = elem["dev_product_name"].get<std::string>();
                        show_list.push_back(obj);
                    }
                }
            }
            catch (...) {
                ;
            }
        }
        else {
            // get empty list set empty list
        }

        update_my_machine_list(show_list);
    }

    void AccountManager::update_my_machine_list(std::vector<MachineObject*> list)
    {
        myBindMachineList.clear();
        for (auto obj : list) {
            myBindMachineList.emplace(std::make_pair(obj->dev_id, obj));
        }
    }

    MachineObject* AccountManager::get_default_machine()
    {
        std::map<std::string, MachineObject*>::iterator it;
        if (default_machine.empty() && !myBindMachineList.empty()) {
            load_last_machine();
            it = myBindMachineList.begin();
            if (!it->second) return nullptr;
            default_machine = it->second->dev_id;
            return it->second;
        }

        it = myBindMachineList.find(default_machine);
        if (it != myBindMachineList.end()) {
            return it->second;
        }

        return nullptr;
    }

    MachineObject* AccountManager::find_machine(std::string dev_id)
    {
        std::map<std::string, MachineObject*>::iterator it = myBindMachineList.find(dev_id);
        if (it != myBindMachineList.end()) {
            return it->second;
        }
        return nullptr;
    }
    std::vector<MachineObject*> AccountManager::get_select_machine_list()
    {
        std::vector<MachineObject*> show_list;
        std::map<std::string, MachineObject*>::iterator it;
        for (it = myBindMachineList.begin();it != myBindMachineList.end(); it++)
        {
            show_list.push_back(it->second);
        }
        return show_list;
    }


    void AccountManager::update_my_bind_list(std::string body)
    {
        std::lock_guard<std::mutex> lock(listMutex);

        std::set<std::string> new_list;
        try {
            pt::ptree root;
            std::stringstream ss(body);
            pt::read_json(ss, root);
            pt::ptree bind_list = root.get_child("devices");
            pt::ptree::iterator it = bind_list.begin();

            int count = 0;
            for (BOOST_AUTO(pos, bind_list.begin()); pos != bind_list.end(); ++pos)
            {
                std::string dev_id = pos->second.get_optional<std::string>("dev_id").value();
                std::string dev_name = pos->second.get_optional<std::string>("name").value();
                std::string online = pos->second.get_optional<std::string>("online").value();
                new_list.insert(dev_id);

                std::map<std::string, MachineObject*>::iterator iter = myBindMachineList.find(dev_id);
                if (iter != myBindMachineList.end()) {
                    /* update field */
                    MachineObject* obj = iter->second;
                    if (obj) {
                        obj->dev_name = dev_name;
                        obj->set_online_state(online.compare("true") == 0 ? true : false);
                    }
                } else {
                    MachineObject* obj = new MachineObject(*this, dev_name, dev_id, "");
                    obj->set_online_state(online.compare("true") == 0 ? true : false);
                    obj->set_bind_status(this->get_user_name());
                    myBindMachineList.insert(std::make_pair(dev_id, obj));
                }
            }
            std::map<std::string, MachineObject*>::iterator iterat;
            for (iterat = myBindMachineList.begin(); iterat != myBindMachineList.end(); ) {
                if (new_list.find(iterat->first) == new_list.end()) {
                    iterat = myBindMachineList.erase(iterat);
                } else {
                    iterat++;
                }
            }
        }
        catch (std::exception& e) {
            ;
        }
    }


    int AccountManager::query_bind_status(std::vector<std::string> device_list, AccountManager::CompletedFn cFn)
    {
        Http http = Http::get(_get_qeury_bind_list_url(device_list));
        try {
            http.header("accept", "application/json")
                .header("Authorization", get_token_str())
                .header("Content-Type", "application/json")
                .on_complete([&, cFn](std::string body, unsigned) {
                    /* eg: {"message": "free, user1, user2, user3, self"} */
                    std::stringstream ss(body);
                    pt::ptree root;
                    pt::read_json(ss, root);
                    if (root.empty()) return;
                    DeviceManager* device_manager = Slic3r::GUI::wxGetApp().getDeviceManager();
                    std::map<std::string, MachineObject*> list = device_manager->get_all_machine_list();
                    std::map<std::string, MachineObject*>::iterator it;
                    if (root.get_child_optional("bind_list") != boost::none) {
                        bind_list_map.clear();
                        pt::ptree bind_list = root.get_child("bind_list");
                        for (auto bind_item = bind_list.begin(); bind_item != bind_list.end(); ++bind_item) {
                            boost::optional<std::string> dev_id = bind_item->second.get_optional<std::string>("dev_id");
                            boost::optional<std::string> user_id = bind_item->second.get_optional<std::string>("user_id");
                            boost::optional<std::string> user_name = bind_item->second.get_optional<std::string>("user_name");
                            if (dev_id.has_value()) {
                                it = list.find(dev_id.value());
                                if (it != list.end()) {
                                    if (it->second) {
                                        it->second->bind_user_id = user_id.value();
                                        it->second->bind_user_name = user_name.value();
                                    }
                                }
                            }
                        }
                        if (cFn) {
                            cFn("");
                        }
                    }
                }).on_error([&, device_list](std::string body, std::string error, unsigned status) {
                    ;
                }).perform();
        }
        catch (std::exception& e) {
            ;
        }
        return 0;
    }

    int AccountManager::request_user_unbind(std::string device_id, ResultFn fn)
    {
        std::string url = (boost::format("%1%/iot-service/api/user/bind") % host).str();

        std::string json_str;
        pt::ptree root;
        root.put("dev_id", device_id);
        //root.put("force", "false");
        std::stringstream oss;
        pt::write_json(oss, root, false);
        json_str = oss.str();

        Http http = Http::del(url);
        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .header("Content-Type", "application/json")
            .set_del_body(json_str)
            .on_complete([&, device_id, fn](std::string body, unsigned) {
            std::stringstream ss(body);
            pt::ptree root;
            pt::read_json(ss, root);
            boost::optional<std::string> message = root.get_optional<std::string>("message");
            boost::optional<std::string> user_ca = root.get_optional<std::string>("user_ca");
            boost::optional<std::string> devs_ca = root.get_optional<std::string>("devs_ca");
            if (message.has_value()) {
                if (message.value().compare("success") == 0) {
                    BOOST_LOG_TRIVIAL(trace) << "Unind Device " << device_id << " OK!";
                    if (fn) {
                        fn(0, body);
                    }
                    return;
                }
                // already unbind, status is free
                else if (message.value().compare("free") == 0) {
                    if (fn) {
                        fn(0, body);
                    }
                    return;
                }
            }
            if (fn) {
                std::string info = (boost::format("Unind Device %1% Failed! error=%2%") % device_id % body).str();
                fn(-1, info);
            }

            BOOST_LOG_TRIVIAL(trace) << "Unind Device " << device_id << " Failed! status = " << message.value();
                }).on_error([&, device_id, fn](std::string body, std::string error, unsigned status) {
                    BOOST_LOG_TRIVIAL(trace) << "Unbind Device " << device_id << " Failed!";
                    if (fn) {
                        std::string info = "Unbind device=" + device_id + "failed! error=" + body;
                        fn(-1, info);
                    }
                    }).perform();
                    return 0;
    }

    int AccountManager::request_unbind(std::string device_id, ResultFn fn)
    {
        Http http = Http::del(_get_bind_url(device_id));
        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .on_complete([&, device_id, fn](std::string body, unsigned) {
            std::stringstream ss(body);
            pt::ptree root;
            pt::read_json(ss, root);
            boost::optional<std::string> bind_status = root.get_optional<std::string>("message");
            boost::optional<std::string> user_ca = root.get_optional<std::string>("user_ca");
            boost::optional<std::string> devs_ca = root.get_optional<std::string>("devs_ca");
            if (bind_status.has_value()) {
                if (bind_status.value().compare("success") == 0) {
                    BOOST_LOG_TRIVIAL(trace) << "Unind Device " << device_id << " OK!";
                    if (fn) {
                        fn(0, body);
                    }
                }
                // already unbind, status is free
                else if (bind_status.value().compare("free") == 0) {
                    if (fn) {
                        fn(0, body);
                    }
                }
                else {
                    BOOST_LOG_TRIVIAL(trace) << "Unind Device " << device_id << " Failed! status = " << bind_status.value();
                }
            }
                }).on_error([&, device_id, fn](std::string body, std::string error, unsigned status) {
                    BOOST_LOG_TRIVIAL(trace) << "Unbind Device " << device_id << " Failed!";
                    if (fn) {
                        std::string info = "Unbind device=" + device_id + "failed! error=" + body;
                        fn(-1, info);
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
                        /* clear my bind machine list */
                        //this->update_my_bind_list(body);
                        if (fn) {
                            fn(0, "get bind list ok");
                        }
                        return;
                    }
                    else if (message.value().compare("nodev") == 0) {
                        //this->update_my_bind_list(body);
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
        pt::ptree root;
        root.put("name", profile_name_str);
        root.put("content", profile->profile_content);
        std::stringstream oss;
        pt::write_json(oss, root, false);
        return oss.str();
    }

    std::string AccountManager::json_request_body_post_task(BBLTask* task)
    {
        if (!task) return "";
        if (task->task_project_id.empty()) return "";
        if (task->task_profile_id.empty()) return "";

        json j;
        j["parent"] = 0;
        j["project_id"] = task->task_project_id;
        j["profile_id"] = task->task_profile_id;
        j["name"] = task->task_name;
        j["content"] = task->build_content_json();

        return j.dump();
    }

    std::string AccountManager::json_request_body_post_task(BBLSubTask* task)
    {
        if (!task) return "";

        if (task->task_printer_dev_id.empty()) return "";

        json j;
        if (task->parent_task_) {
            if (!task->parent_task_->task_id.empty())
                j["parent"] = task->parent_task_->task_id;
            if (!task->parent_task_->task_project_id.empty())
                j["project_id"] = task->parent_task_->task_project_id;
            if (!task->parent_task_->task_profile_id.empty())
                j["profile_id"] = task->parent_task_->task_profile_id;
        }
        j["dev_id"] = task->task_printer_dev_id;
        j["name"] = task->task_name;
        j["content"] = task->build_content_json();

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
        return j.dump();
    }

    std::string AccountManager::json_request_body_post_setting(Preset* preset)
    {
        std::string result;
        if (!preset) return "";

        json j, setting_node;

        j["public"] = preset->is_system ? true : false;
        j["version"] = preset->version.to_string();
        j["type"] = Preset::get_iot_type_string(preset->type);
        j["name"] = preset->name;
        j["base_id"] = preset->base_id;
        for (const std::string &opt_key : preset->config.keys()) {
            setting_node[opt_key] = preset->config.opt_serialize(opt_key);
        }
        setting_node["updated_time"] = std::to_string(preset->updated_time);
        j["setting"] = setting_node;

        return j.dump();
    }

    std::string AccountManager::json_request_body_put_setting(Preset* preset)
    {
        if (!preset) return "";

        pt::ptree root, setting_node;
        root.put("version", preset->version.to_string());
        root.put("name", preset->name);
        root.put("base_id", preset->base_id);
        for (const std::string &opt_key : preset->config.keys()) {
            setting_node.put(opt_key, preset->config.opt_serialize(opt_key));
        }
        setting_node.put("updated_time", std::to_string(preset->updated_time));
        root.add_child("setting", setting_node);
        std::stringstream oss;
        pt::write_json(oss, root, false);
        return oss.str();
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
        int result = RET_POLLING_CANEL;
        if (!profile || !profile->project_) return -1;
        if (profile->upload_url.empty()) return -1;
        if (!fs::exists(profile->project_->project_path)) {
            BOOST_LOG_TRIVIAL(trace) << "file is not exists!";
            return -1;
        }

        std::string md5_str;
        std::string full_filename = profile->project_->project_path.generic_string();
        Slic3r::bbl_calc_md5(full_filename, md5_str);
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

    // GET /api/user/profile/{profile_id}
    int AccountManager::get_profile_3mf(BBLProfile *profile, unsigned int &http_code, std::string http_body)
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
                [this, subtask](std::string body, unsigned) {
                    std::stringstream ss(body);
                    pt::ptree root;
                    pt::read_json(ss, root);
                    boost::optional<std::string> message = root.get_optional<std::string>("message");
                    if (message.has_value()) {
                        if (message.value().compare(MSG_SUCCESS) == 0) {
                            boost::optional<std::string> name = root.get_optional<std::string>("name");
                            if (name.has_value())
                                subtask->task_name = name.value();
                            boost::optional<std::string> status = root.get_optional<std::string>("status");
                            if (status.has_value())
                                subtask->task_status = BBLSubTask::parse_status(status.value());
                            boost::optional<std::string> content = root.get_optional<std::string>("content");
                            if (content.has_value())
                                subtask->parse_content_json(content.value());
                            boost::optional<std::string> create_time = root.get_optional<std::string>("create_time");
                            if (create_time.has_value())
                                subtask->task_create_time = create_time.value();
                            boost::optional<std::string> update_time = root.get_optional<std::string>("update_time");
                            if (update_time.has_value())
                                subtask->task_update_time = update_time.value();
                            boost::optional<std::string> parent_id = root.get_optional<std::string>("parent");
                            if (parent_id.has_value())
                                subtask->parent_id = parent_id.value();
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
                callback(ttcode.empty() ? ttcode : "tutk:///" + ttcode + url);
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
                                ; //TODO convert name to wstring
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
                [this, task, &result, &http_code, &http_body](std::string body, unsigned status) {
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
        config_json["agent"]["country_code"] = country_code;
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
    int AccountManager::get_setting_list(Http::ErrorFn errFn)
    {
        PresetBundle* preset_bundle = GUI::wxGetApp().preset_bundle;
        my_presets.clear();

        std::string version = preset_bundle->get_vendor_profile_version(PresetBundle::BBL_BUNDLE).to_string();
        std::string query_params = (boost::format("?version=%s") % version).str();
        std::string url = (boost::format("%1%/iot-service/api/slicer/setting%2%") % host % query_params).str();
        Http http = Http::get(url);
        http.timeout_max(10)
            .header("accept", "application/json")
            .header("Authorization", get_token_str())
            .on_complete(
                [this, preset_bundle](std::string body, unsigned) {
                    std::stringstream ss(body);
                    pt::ptree root;
                    pt::read_json(ss, root);
                    if (root.empty()) return;
                    boost::optional<std::string> message = root.get_optional<std::string>("message");
                    if (message.has_value() && message.value().compare(MSG_SUCCESS) == 0) {
                        if (root.get_child_optional(PRESET_IOT_PRINTER_TYPE) != boost::none) {
                            pt::ptree printer_node = root.get_child(PRESET_IOT_PRINTER_TYPE);
                            if (printer_node.get_child_optional("public") != boost::none) {
                                pt::ptree public_node = printer_node.get_child("public");
                                for (auto setting_item = public_node.begin(); setting_item != public_node.end(); ++setting_item) {
                                    parse_setting(preset_bundle, setting_item->second, PRESET_IOT_PRINTER_TYPE, "public");
                                }
                            }
                            if (printer_node.get_child_optional("private") != boost::none) {
                                    pt::ptree private_node = printer_node.get_child("private");
                                    for (auto setting_item = private_node.begin(); setting_item != private_node.end(); ++setting_item) {
                                        parse_setting(preset_bundle, setting_item->second, PRESET_IOT_PRINTER_TYPE, "private");
                                    }
                                }
                        }
                        if (root.get_child_optional(PRESET_IOT_FILAMENT_TYPE) != boost::none) {
                            pt::ptree filament_node = root.get_child(PRESET_IOT_FILAMENT_TYPE);
                            if (filament_node.get_child_optional("public") != boost::none) {
                                pt::ptree public_node = filament_node.get_child("public");
                                for (auto setting_item = public_node.begin(); setting_item != public_node.end(); ++setting_item) {
                                    parse_setting(preset_bundle, setting_item->second, PRESET_IOT_FILAMENT_TYPE, "public");
                                }
                            }
                            if (filament_node.get_child_optional("private") != boost::none) {
                                pt::ptree private_node = filament_node.get_child("private");
                                for (auto setting_item = private_node.begin(); setting_item != private_node.end(); ++setting_item) {
                                    parse_setting(preset_bundle, setting_item->second, PRESET_IOT_FILAMENT_TYPE, "private");
                                }
                            }
                        }
                        if (root.get_child_optional(PRESET_IOT_PRINT_TYPE) != boost::none) {
                            pt::ptree print_node = root.get_child(PRESET_IOT_PRINT_TYPE);
                            if (print_node.get_child_optional("public") != boost::none) {
                                pt::ptree public_node = print_node.get_child("public");
                                for (auto setting_item = public_node.begin(); setting_item != public_node.end(); ++setting_item) {
                                    parse_setting(preset_bundle, setting_item->second, PRESET_IOT_PRINT_TYPE, "public");
                                }
                            }
                            if (print_node.get_child_optional("private") != boost::none) {
                                pt::ptree private_node = print_node.get_child("private");
                                for (auto setting_item = private_node.begin(); setting_item != private_node.end(); ++setting_item) {
                                    parse_setting(preset_bundle, setting_item->second, PRESET_IOT_PRINT_TYPE, "private");
                                }
                            }
                        }
                        }
                    }
        ).on_error(errFn)
        .perform_sync();

        std::map<std::string, Preset*>::iterator it;
        for (it = my_presets.begin(); it != my_presets.end(); it++) {
            get_setting(it->second);
        }

        GUI::wxGetApp().reload_settings();
        return 0;
    }

    void AccountManager::get_setting(Preset *&preset, std::function<void(void)> callback)
    {
        std::string url = (boost::format("%1%/iot-service/api/slicer/setting/%2%") % host % preset->setting_id).str();
        Http http = Http::get(url);

        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .on_complete(
                [this, preset, callback](std::string body, unsigned) {
                    std::stringstream ss(body);
                    pt::ptree root;
                    pt::read_json(ss, root);
                    if (root.empty()) return;
                    boost::optional<std::string> message = root.get_optional<std::string>("message");
                    if (message.has_value() && message.value().compare(MSG_SUCCESS) == 0) {
                        boost::optional<bool> public_str = root.get_optional<bool>("public");
                        boost::optional<std::string> version = root.get_optional<std::string>("version");
                        boost::optional<std::string> name = root.get_optional<std::string>("name");
                        boost::optional<std::string> type = root.get_optional<std::string>("type");
                        boost::optional<std::string> base_id = root.get_optional<std::string>("base_id");
                        boost::optional<std::string> filament_id = root.get_optional<std::string>("filament_id");

                        if (name.has_value()) {
                            if (preset->name.empty())
                                preset->name = name.value();
                        }

                        if (base_id.has_value()) {
                            preset->base_id = base_id.value();
                            if (preset->base_id.compare("null") == 0)
                                preset->base_id.clear();
                        }

                        // check setting field and update setting field
                        if (root.get_child_optional("setting") != boost::none) {
                            pt::ptree setting_node = root.get_child("setting");
                            for (auto item = setting_node.begin(); item != setting_node.end(); ++item) {
                                if (item->first == "updated_time")
                                    preset->updated_time = std::atoll(item->second.data().c_str());
                                else
                                    preset->key_values.insert(std::make_pair(item->first, item->second.data()));
                            }
                        }
                        if (version.has_value())
                            preset->key_values.insert(std::make_pair("version", version.value()));
                        if (filament_id.has_value()) {
                            preset->filament_id = filament_id.value();
                        }
                        if (m_curr_user)
                            preset->key_values.insert(std::make_pair("user_id", m_curr_user->get_user_id()));
                    }
                    if (callback) callback();
                }
        );
        if (callback == nullptr)
            http.perform_sync();
        else
            http.perform();
    }

    int AccountManager::request_setting_id(Preset* &preset, unsigned int& http_code)
    {
        if (!preset) return -1;

        std::string url = (boost::format("%1%/iot-service/api/slicer/setting") % host).str();

        std::vector<std::string> params;
        std::string request_str = json_request_body_post_setting(preset);

        Http http = Http::post(url);
        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .header("Content-Type", "application/json")
            .set_post_body(request_str)
            .on_complete(
                [this, preset, &http_code](std::string body, unsigned int code) {
                    BOOST_LOG_TRIVIAL(trace) << "request setting id, body=" << body;
                    http_code = code;
                    std::stringstream ss(body);
                    pt::ptree root;
                    pt::read_json(ss, root);
                    if (root.empty()) return;
                    boost::optional<std::string> message = root.get_optional<std::string>("message");
                    boost::optional<std::string> setting_id = root.get_optional<std::string>("setting_id");
                    if (message.has_value()) {
                        if (message.value().compare(MSG_SUCCESS) == 0) {
                            if (setting_id.has_value()) {
                                preset->setting_id = setting_id.value();
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
        return 0;
    }

    int AccountManager::put_setting(Preset* preset, unsigned int& http_code)
    {
        int result = -1;
        int* result_ptr = &result;
        std::string request_body = json_request_body_put_setting(preset);
        std::string url = (boost::format("%1%/iot-service/api/slicer/setting/%2%") % host % preset->setting_id).str();
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

    void AccountManager::parse_setting(PresetBundle* preset_bundle, pt::ptree node, std::string type, std::string attr)
    {
        _parse_preset_internal(preset_bundle, my_presets, node, type, attr);
    }

    void AccountManager::_parse_preset_internal(PresetBundle* preset_bundle, std::map<std::string, Preset*>& presets, pt::ptree node, std::string type, std::string attr)
    {
        boost::optional<std::string> setting_id = node.get_optional<std::string>("setting_id");
        boost::optional<std::string> version = node.get_optional<std::string>("version");
        boost::optional<std::string> name = node.get_optional<std::string>("name");

        if (setting_id.has_value()) {
            bool is_system = attr.compare("public") == 0 ? true : false;
            Preset::Type curr_type = Preset::get_type_from_string(type);
            if (curr_type == Preset::Type::TYPE_INVALID) {
                BOOST_LOG_TRIVIAL(info) << boost::format("type %1% is invalid")%type;
                return;
            }
            if (is_system) {
                PresetCollection* preset_collection = nullptr;
                switch(curr_type) {
                    case Preset::TYPE_FILAMENT:
                        preset_collection = &(preset_bundle->filaments);
                        break;
                    case Preset::TYPE_PRINTER:
                        preset_collection = &(preset_bundle->printers);
                        break;
                    case Preset::TYPE_PRINT:
                    default:
                        preset_collection = &(preset_bundle->prints);
                        break;
                }
                Preset* preset = preset_collection->find_preset(name.value(), false, true);
                if (preset) {
                    if (!preset->setting_id.empty() && (preset->setting_id.compare(setting_id.value()) != 0)) {
                        BOOST_LOG_TRIVIAL(error) << boost::format("name %1%, local setting_id %2% is different with remote id %3%")
                            %preset->name %preset->setting_id %setting_id.value();
                    }
                    else if (preset->setting_id.empty())
                        preset->setting_id = setting_id.value();
                }
            }
            else {
                std::map<std::string, Preset*>::iterator it = presets.find(setting_id.value());
                if (it == presets.end()) {
                    /* insert a new setting */
                    Preset* preset = new Preset(curr_type, name.value(), false);
                    preset->setting_id = setting_id.value();
                    preset->is_system = false;
                    if (version.has_value()) {
                        preset->version = version.value();
                    }
                    if (m_curr_user) {
                        preset->user_id = m_curr_user->get_user_id();
                    }
                    presets.insert(std::make_pair(setting_id.value(), preset));
                }
            }
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


    void AccountManager::reset_project()
    {
        ;
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

    std::string AccountManager::_get_bind_url(std::string device_id)
    {
        return (boost::format("%1%/iot-service/api/user/%2%/bind") % host % device_id).str();
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



} // namespace Slic3r
