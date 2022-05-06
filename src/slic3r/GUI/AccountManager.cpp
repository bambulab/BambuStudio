#include "libslic3r/libslic3r.h"
#include "AccountManager.hpp"
#include "DeviceManager.hpp"
#include "libslic3r/Time.hpp"
#include "libslic3r/Thread.hpp"
#include "libslic3r/Format/Secure.hpp"
#include "libslic3r/AppConfig.hpp"
#include "slic3r/Utils/Http.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/LoginDialog.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "nlohmann/json.hpp"
#include "slic3r/Utils/minilzo_extension.hpp"
#include <boost/filesystem/path.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <thread>
#include <mutex>
#include <codecvt>
#include <boost/foreach.hpp>
#include <boost/typeof/typeof.hpp>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include "slic3r/GUI/WebUserLoginDialog.hpp"

using namespace nlohmann;

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

uint64_t lzo_out_len = 5 * 1024;
const uint64_t LZO_OUT_MAX_LEN = 5 * 1024;
unsigned char lzo_out[LZO_OUT_MAX_LEN];


std::string RegionServer::convert_region_to_contry_code(std::string region)
{
    if (region == "CHN")
        return "CN";
    else if (region == "USA")
        return "US";
    else
        return "Others";
}

namespace Slic3r {
    void cloud_conn_callback::connected(const std::string& cause)
    {
        BOOST_LOG_TRIVIAL(trace) << "cloud_conn_callback::connected!";
        /* update sub_topics */
        if (successFn) {
            successFn(cli_.get_client_id());
        }
        // re sucscribe the monitoring printer
        AccountManager* manager = (AccountManager*)context_;
        if (manager) {
            GUI::wxGetApp().CallAfter([manager] {
                manager->load_last_machine();
            });
        }
    }

    void cloud_conn_callback::on_failure(const mqtt::token& tok)
    {
        BOOST_LOG_TRIVIAL(trace) << "cloud_conn_callback::on_failure, Connection(mqtt) failed! retry=" << nretry_;
        /* TODO mqtt connect failed tips */
        if (failedFn) {
            failedFn(cli_.get_client_id());
        }
    }

    void cloud_conn_callback::on_success(const mqtt::token& tok)
    {
        BOOST_LOG_TRIVIAL(trace) << "cloud_conn_callback::on_success, Connection(mqtt) OK! cli id=" << cli_.get_client_id();
        /* mqtt connect on success tips, same as connected */
    }

    void cloud_conn_callback::connection_lost(const std::string& cause) {
        BOOST_LOG_TRIVIAL(trace) << "cloud_conn_callback::connection_lost!, cause =" << cause;
        if (lostFn) {
            lostFn(cli_.get_client_id());
        }
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

            std::string json_str;

            if (it->second) {
                try {
                    // BBS check valid json
                    char head = *msg->get_payload_ref().c_str();
                    if (head == '{') {
                        json j = json::parse(msg->get_payload_str());
                        if (j.is_null()) return;
                        json_str = msg->get_payload_str();
                        BOOST_LOG_TRIVIAL(trace) << "message topic:" << msg->get_topic() << ", payload=" << json_str;
                    }
                    else {
                        int result = 0;
                        lzo_out_len = LZO_OUT_MAX_LEN;
                        result = lzo_decompress((unsigned char*)msg->get_payload_ref().data(), msg->get_payload().length(), lzo_out, &lzo_out_len);
                        if (result == 0) {
                            json_str = std::string((char*)lzo_out, lzo_out_len);
                            BOOST_LOG_TRIVIAL(trace) << "message topic:" << msg->get_topic() << ", decompress payload=" << json_str;
                        }
                        else {
                            BOOST_LOG_TRIVIAL(trace) << "message_arrived: invalid json and decompress failed, result = " << result;
                        }
                    }
                }
                catch (...) {
                    ;
                }
                if (json_str.empty()) return;

                it->second->parse_json(msg->get_topic(), json_str);

                if (it->second->is_ams_need_update)
                    GUI::wxGetApp().CallAfter([manager, m = params[1]] {
                        std::map<std::string, MachineObject *>::iterator it = manager->myBindMachineList.find(m);
                        GUI::wxGetApp().sidebar().load_ams_list(it->second->amsList);
                    });
            }
        }
    }

    void cloud_conn_callback::set_connect_fns(SuccessFn sFn, FailedFn fFn, LostFn lFn)
    {
        successFn = sFn;
        failedFn = fFn;
        lostFn = lFn;
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


    int AccountInfo::save_to_json()
    {
        AppConfig* config = GUI::wxGetApp().app_config;
        config->set("user", "account", m_account);
        config->set("user", "token", m_token);
        config->set("user", "user_id", m_user_id);
        config->set("user", "login_status", std::to_string((int)m_login_status));
        config->set("user", "autotest_token", m_autotest_token);
        config->set("user", "name", m_name);
        config->set("user", "avatar", m_avatar);
        config->set_dirty();
        return 0;
    }

    AccountInfo* AccountInfo::load_from_json()
    {
        try {
            AppConfig* config = GUI::wxGetApp().app_config;
            if (config->has_section("user")) {
                std::string account = config->get("user", "account");
                std::string token = config->get("user", "token");
                std::string user_id = config->get("user", "user_id");
                std::string autotest_token = config->get("user", "autotest_token");
                std::string sAvatar        = config->get("user", "avatar");
                std::string sName          = config->get("user", "name");
                AccountInfo::LoginStatus status = AccountInfo::LoginStatus::STATUS_LOGOUT;
                if (!config->get("user", "login_status").empty())
                    status = (AccountInfo::LoginStatus)std::stoi(config->get("user", "login_status"));
                AccountInfo* info = new AccountInfo(account, user_id,token,sName,sAvatar,status,autotest_token);
                info->m_autotest_token = autotest_token;
                info->set_token(token);
                return info;
            } else {
                return nullptr;
            }
        }
        catch (std::exception& e)
        {
            BOOST_LOG_TRIVIAL(error) << "AccountManager::load_from_json() failed! exception=" << e.what();
            return nullptr;
        }
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

        mqtt_ssl_opt.ca_path(resources_dir() + "/cert");
        std::string key_store = resources_dir() + "/cert/slicer.crt";
        std::string trust_store = resources_dir() + "/cert/slicer_chain.crt";
        std::string private_key = resources_dir() + "/cert/slicer_pri.pem";
        mqtt_ssl_opt.set_key_store(key_store);
        mqtt_ssl_opt.set_trust_store(trust_store);
        mqtt_ssl_opt.set_private_key(private_key);
        mqtt_opt.set_ssl(mqtt_ssl_opt);

        m_curr_user = nullptr;
    }

    AccountManager::~AccountManager()
    {
        if (mqtt_cli->is_connected())
            mqtt_cli->disconnect();
        Http::disable_log();
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
                handle_http_error(status, body);
            }
        );
    }

    int AccountManager::load_user_info()
    {
        m_curr_user = AccountInfo::load_from_json();
        if (this->is_user_login()) {
            this->on_user_login(0);
        }
        return 0;
    }

    void AccountManager::on_user_login(int online_login)
    {
        KeyStore::global_consumers.clear();
        auto kek = m_curr_user->get_user_id();
        kek.resize(32, '0');
        KeyStore::global_consumers.push_back({m_curr_user->get_user_id(), "", kek});

        auto evt = new wxCommandEvent(EVT_USER_LOGIN);
        evt->SetInt(online_login);
        wxQueueEvent(&GUI::wxGetApp(), evt);
    }

    int AccountManager::save_user_info()
    {
        if (m_curr_user) {
            return m_curr_user->save_to_json();
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
        /*if (!is_region_config_ready) {
            BOOST_LOG_TRIVIAL(error) << "config is not ready!";
            return -1;
        }*/

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
            mqtt_opt.set_user_name((boost::format("u_%1%") % m_curr_user->m_user_id).str());
            mqtt_opt.set_password(m_curr_user->get_token());
            //mqtt_cli = new mqtt::async_client(user_region_server.mqtt_server_host, client_id);
            mqtt_cli = new mqtt::async_client(MQTT_HOST, client_id);
            mqtt_cb = new cloud_conn_callback(*mqtt_cli, mqtt_opt, this);
            if (mqtt_cli) {
                mqtt_cli->set_callback(*mqtt_cb);
                if (sync)
                    mqtt_cli->connect(mqtt_opt, this, *mqtt_cb)->wait_for(3000);
                else
                    mqtt_cli->connect(mqtt_opt, this, *mqtt_cb);
            }

            reconn_thread = Slic3r::create_thread([this] {
                try {
                    while(mqtt_cli) {
                        check_mqtt_connection();
                        boost::this_thread::sleep_for(boost::chrono::milliseconds(2000));
                    }
                }
                catch (boost::thread_interrupted&) {
                    BOOST_LOG_TRIVIAL(trace) << "reconn_thread is interrupted";
                }
            });
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
                reconn_thread.interrupt();
                if (reconn_thread.joinable()) {
                    reconn_thread.join();
                }
            }
        } catch(...) {
            ;
        }
        return 0;
    }

    void AccountManager::check_mqtt_connection()
    {
        if (is_user_login() && mqtt_cli && !mqtt_cli->is_connected()) {
            try {
                mqtt_cli->connect(mqtt_opt, this, *mqtt_cb);
            } catch(const mqtt::exception& exc) {
                BOOST_LOG_TRIVIAL(error) << "mqtt_exception: " << exc.what();
            }
            catch(...) {
                BOOST_LOG_TRIVIAL(error) << "mqtt_exception occur";
            }
        }
    }

    void AccountManager::add_subscribe(MachineObject* obj)
    {
        std::string report_topic = (boost::format("device/%1%/report") % obj->dev_id).str();
        try {
            if (mqtt_cli && mqtt_cli->is_connected()) {
                if (mqtt_topics.find(report_topic) == mqtt_topics.end()) {
                    BOOST_LOG_TRIVIAL(trace) << "add_subscribe topic=" << report_topic;
                    mqtt_topics.insert(std::make_pair(report_topic, obj));
                }
                else {
                    BOOST_LOG_TRIVIAL(trace) << "add_subscribe topic=" << report_topic << " is exists";
                }
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


    void AccountManager::del_subscribe(MachineObject* obj)
    {
        std::string report_topic = (boost::format("device/%1%/report") % obj->dev_id).str();
        std::map<std::string, MachineObject*>::iterator it = mqtt_topics.find(report_topic);
        if (it == mqtt_topics.end()) {
            return;
        }

        try {
            if (mqtt_cli) {
                BOOST_LOG_TRIVIAL(trace) << "del_subscribe topic=" << report_topic;
                mqtt_topics.erase(report_topic);
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

    void AccountManager::del_subscribe(std::string dev_id)
    {
        std::string report_topic = (boost::format("device/%1%/report") % dev_id).str();
        try {
            if (mqtt_cli && mqtt_cli->is_connected()) {
                BOOST_LOG_TRIVIAL(trace) << "del_subscribe topic=" << report_topic;
                mqtt_cli->unsubscribe(report_topic);
            }
        }
        catch(mqtt::exception& e) {
            BOOST_LOG_TRIVIAL(trace) << "del_subscribe exception, topic=" << report_topic << ", exception error_str=" << e.get_error_str() << ", message=" << e.get_message();
        }
        catch(...) {
            BOOST_LOG_TRIVIAL(trace) << "del_subscribe exception, topic=" << report_topic;
        }
    }

    void AccountManager::update_subscription()
    {
        std::map<std::string, MachineObject*>::iterator it;
        BOOST_LOG_TRIVIAL(trace) << "update_subscription, machine list = " << myBindMachineList.size();
        for (it = myBindMachineList.begin(); it != myBindMachineList.end(); it++) {
            add_subscribe(it->second);
        }
    }

    void AccountManager::set_monitor_machine(std::string dev_id)
    {
        BOOST_LOG_TRIVIAL(trace) << "set monitor machine = " << dev_id;
        std::string old_dev_id = this->default_machine;

        // store last_monitor_printer
        wxGetApp().app_config->set("last_monitor_machine", dev_id);
        this->default_machine = dev_id;

        //unsubscribe old machine
        if (!old_dev_id.empty() && old_dev_id.compare(dev_id) != 0) {
            this->del_subscribe(old_dev_id);
        }

        //subscribe new machine
        this->add_subscribe(dev_id);

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
            std::string last_monitor_machine = wxGetApp().app_config->get("last_monitor_machine");
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

    int AccountManager::user_login(std::string account, std::string password, LoginFn fn)
    {
        // check valid account
        if (!_check_valid(account, password)) {
            return -1;
        }

        Http http = Http::post(std::move(_get_login_url()));
        std::string json_str = _get_login_request(account, password);
        http.header("accept", "application/json")
            .header("Content-Type", "application/json")
            .set_post_body(json_str)
            .on_complete([&, this, account, fn](std::string body, unsigned) {
                std::stringstream ss(body);
                pt::ptree root;
                pt::read_json(ss, root);

                boost::optional<std::string> acceptToken = root.get_optional<std::string>("accessToken");
                if (acceptToken.has_value()) {
                    BOOST_LOG_TRIVIAL(trace) << "User = " << account << " Login Success!";
                    if (!m_curr_user) {
                        m_curr_user = new AccountInfo(account, "", AccountInfo::LoginStatus::STATUS_LOGIN);
                    } else {
                        m_curr_user->m_account = account;
                        m_curr_user->set_login_status(AccountInfo::LoginStatus::STATUS_LOGIN);
                    }
                    m_curr_user->set_token(acceptToken.value());

                    //get user id
                    this->user_get_profile(account, fn);
                    return;
                }

                BOOST_LOG_TRIVIAL(trace) << "Account = " << account << " Login Failed! error = " << body;
                if (fn) {
                    fn(-1, body);
                }
            }).on_error([&, this, account, fn](std::string body, std::string error, unsigned status) {
                std::string error_tips = (boost::format("Login Failed! status=%1%, error=%2%, body=%3%")
                                         % status % error % body).str();
                BOOST_LOG_TRIVIAL(trace) << "Account= " << account << " Login Failed! msg: " << error_tips;
                if (fn) {
                    fn(-1, error_tips);
                }
            }).perform();

        return 0;
    }

    int AccountManager::user_get_profile(std::string account, LoginFn fn)
    {
        Http http = Http::get(_get_user_profile_url(account));
        try {
            http.header("accept", "application/json")
                .header("Authorization", get_token_str())
                .header("Content-Type", "application/json")
                .on_complete([&, fn](std::string body, unsigned code) {
                    std::stringstream ss(body);
                    pt::ptree root;
                    pt::read_json(ss, root);
                    boost::optional<std::string> uid = root.get_optional<std::string>("uid");
                    boost::optional<std::string> account = root.get_optional<std::string>("account");
                    boost::optional<std::string> name = root.get_optional<std::string>("name");
                    boost::optional<std::string> avatar = root.get_optional<std::string>("avatar");
                    if (uid.has_value()) {
                        // update user info
                        AccountInfo* info = this->get_curr_user();
                        if (info) {
                            info->m_name = name.has_value() ? name.value() : "";
                            info->m_user_id = uid.value();
                            info->m_avatar = avatar.has_value() ? avatar.value() : "";
                            save_user_info();

                            /* connect mqtt */
                            this->on_user_login(1);
                            if (fn) {
                                fn(0, "Login Ok!");
                            }
                            return;
                        }
                    }
                    if (fn) {
                        fn(-1, "Login Failed! body=" + body);
                    }
                })
                .on_error([fn](std::string body, std::string error, unsigned status) {
                    BOOST_LOG_TRIVIAL(trace) << "Get user profile failed! status=" << status << ", error = " << body;
                    if (fn) {
                        fn(-1, "get user profile failed");
                    }
                })
                .perform_sync();
        }
        catch (std::exception& e) {
            ;
        }
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

    int AccountManager::get_notification(BBLProfile* profile, unsigned int& http_code, std::string& http_body, CancelFn cancel_fn)
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

        while (retry < retry_max) {
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
            boost::this_thread::sleep_for(boost::chrono::milliseconds(POLL_NOTIFICATION_INTERVAL * 1000));
        }
        if (retry == retry_max) {
            /* timeout */
            return RET_POLLING_TIMEOUT;
        }
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

    int AccountManager::user_register(std::string account, std::string password)
    {
        Http http = Http::post(std::move(_get_register_url()));
        std::string json_str = _get_login_request(account, password);
        try {
            http.header("accept", "application/json")
                .header("Content-Type", "application/json")
                .set_post_body(json_str)
                .on_complete([&, account](std::string body, unsigned) {
                std::stringstream ss(body);
                pt::ptree root;
                pt::read_json(ss, root);
                boost::optional<std::string> ack_status = root.get_optional<std::string>("message");
                if (ack_status.has_value()) {
                    if (ack_status.value().compare(MSG_SUCCESS) == 0) {
                        BOOST_LOG_TRIVIAL(trace) << "Account = " << account << " Register Success!";
                    }
                    else {
                        BOOST_LOG_TRIVIAL(trace) << "Account = " << account << " Register Failed! error = " << body;
                    }
                }
                else {
                    BOOST_LOG_TRIVIAL(trace) << "Account = " << account << " Register Failed! error = " << body;
                }
                    }).on_error([&, account](std::string body, std::string error, unsigned status) {
                        BOOST_LOG_TRIVIAL(trace) << "Account = " << account << " Register Failed! error = " << body;
                        }).perform();
        }
        catch (std::exception& e) {
            ;
        }
        return 0;
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


    int AccountManager::query_bind_status(std::vector<std::string> device_list, AccountManager::CompletedFn cFn, ErrorFn errFn)
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
                }).on_error([&, device_list, errFn](std::string body, std::string error, unsigned status) {
                    if (errFn) {
                        errFn(status, error, body);
                    }
                }).perform();
        }
        catch (std::exception& e) {
            ;
        }
        return 0;
    }

    int AccountManager::request_bind(std::string device_id, ResultFn fn)
    {
        if (!m_curr_user) {
            return -1;
        }

        std::string url = (boost::format("%1%/iot-service/api/device/%2%/bind") % host % device_id).str();
        Http http = Http::put2(std::move(url));

        std::string json_str;
        pt::ptree root;
        root.put("user_id", m_curr_user->get_user_id());
        std::stringstream oss;
        pt::write_json(oss, root, false);
        json_str = oss.str();

        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .header("Content-Type", "application/json")
            .set_post_body(json_str)
            .on_complete([&, device_id, fn](std::string body, unsigned) {
            std::stringstream ss(body);
            pt::ptree root;
            pt::read_json(ss, root);
            boost::optional<std::string> bind_status = root.get_optional<std::string>("message");
            boost::optional<std::string> user_ca = root.get_optional<std::string>("user_ca");
            boost::optional<std::string> devs_ca = root.get_optional<std::string>("devs_ca");
            if (bind_status.has_value()) {
                if (bind_status.value().compare("success") == 0) {
                    BOOST_LOG_TRIVIAL(trace) << "Bind Device " << device_id << " OK!";
                    // call complete function
                    if (fn) {
                        fn(0, body);
                        return;
                    }
                }
                else if (bind_status.value().compare("conflict") == 0) {
                    BOOST_LOG_TRIVIAL(trace) << "Bind Device " << device_id << "  Conflict!";
                }
            }

            if (fn) {
                std::string info = "Bind Device=" + device_id + " Failed error=" + body;
                fn(-1, info);
                BOOST_LOG_TRIVIAL(trace) << "Bind Device " << device_id << "  Failed! error=" << body;
            }
                }).on_error([&, device_id, fn](std::string body, std::string error, unsigned status) {
                    if (fn) {
                        fn(-1, error);
                    }
                    BOOST_LOG_TRIVIAL(trace) << "Bind Device " << device_id << " Failed!";
                    }).perform();
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
                    Slic3r::GUI::wxGetApp().show_message_box("Unbind device=" + device_id + " failed! error=" + body);
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

    int AccountManager::submit_print_result(std::string device_id, std::string json_str, ResultFn fn)
    {
        std::string url = (boost::format("%1%/iot-service/api/user/report") % host).str();
        Http http = Http::post(url);
        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .header("Content-Type", "application/json")
            .set_post_body(json_str)
            .on_complete([&, fn](std::string body, unsigned) {
            BOOST_LOG_TRIVIAL(trace) << "AccountManager::submit_print_result complete, body=" << body;
            if (fn) {
                fn(0, body);
            }
                })
            .on_error([&, fn](std::string body, std::string error, unsigned status) {
                    std::string result_info = "status=" + std::to_string(status) + ",error=" + error + ",body=" + body;
                    BOOST_LOG_TRIVIAL(trace) << "AccountManager::submit_print_result error, info=" << result_info;
                    if (fn) {
                        fn(-1, result_info);
                    }
                }).perform();
                return 0;
    }

    void AccountManager::check_new_version(bool show_tips)
    {
        std::string platform = "windows";
#ifdef __WINDOWS__
        platform = "windows";
#endif
#ifdef __APPLE__
        platform = "macos";
#endif
#ifdef __LINUX__
        platform = "linux";
#endif
        std::string query_params = (boost::format("?name=slicer&&version=%1%&&platform=%2%&&guide_version=%3%")
            % VersionInfo::convert_full_version(SLIC3R_VERSION)
            % platform
            % VersionInfo::convert_full_version("0.0.0.1")
            ).str();
        std::string url = _get_slicer_info_url() + query_params;
        Http http = Http::get(url);
        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .on_complete([this, show_tips](std::string body, unsigned) {
                std::stringstream ss(body);
                pt::ptree root;
                try {
                    pt::read_json(ss, root);
                    if (root.empty()) return;
                    boost::optional<std::string> message  = root.get_optional<std::string>("message");
                    boost::optional<std::string> err_code = root.get_optional<std::string>("code");
                    if (message.has_value()) {
                        if (message.value().compare(MSG_SUCCESS) == 0) {
                            if (root.get_child_optional("software") != boost::none) {
                                pt::ptree software_node = root.get_child("software");

                                // newest version
                                if (software_node.empty() && err_code.value().compare("null") == 0 && show_tips) {
                                    GUI::wxGetApp().no_new_version();
                                } else {
                                    boost::optional<std::string> url         = software_node.get_optional<std::string>("url");
                                    boost::optional<std::string> version     = software_node.get_optional<std::string>("version");
                                    boost::optional<std::string> description = software_node.get_optional<std::string>("description");
                                    if (version.has_value() && url.has_value() && description.has_value()) {
                                        version_info.url = url.value();
                                        version_info.parse_version_str(version.value());
                                        version_info.description = description.value();
                                        check_update(show_tips);
                                    }
                                }
                            }
                        }
                    }
                }
                catch(...) {
                    ;
                }
            })
            .on_error([this](std::string body, std::string error, unsigned int status) {
                BOOST_LOG_TRIVIAL(error) << "check new version" << body;
            }).perform();
    }

    void AccountManager::check_update(bool show_tips)
    {
        if (version_info.version_str.empty()) return;
        if (version_info.url.empty()) return;

        if (version_info.compare(SLIC3R_VERSION) > 0) {
            GUI::wxGetApp().request_new_version();
        }
        // Same Version
        else if (version_info.compare(SLIC3R_VERSION) == 0) {
            GUI::wxGetApp().no_new_version();
        }
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
        j["vabrationCali"] = task->task_vabration_cali;
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

    int AccountManager::request_task_id(BBLTask* task, unsigned int& http_code, std::string& http_body)
    {
        int result = -1;

        if (!task) return -1;

        std::string json_str = json_request_body_post_task(task);
        if (json_str.empty()) {
            BOOST_LOG_TRIVIAL(trace) << "reqeust_task_id failed, json_str is empty";
            return -1;
        }

        std::string url = (boost::format("%1%/iot-service/api/user/task") % host).str();

        Http http = Http::post(url);
        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .header("Content-Type", "application/json")
            .set_post_body(json_str)
            .on_complete(
                [this, task, &result, &http_code, &http_body](std::string body, unsigned int status) {
                    http_code = status;
                    http_body = body;
                    try {
                        json j = json::parse(body);
                        if (is_valid_property(j, "message")) {
                            if (j["message"].get<std::string>() == MSG_SUCCESS) {
                                if (is_valid_property(j, "task_id")) {
                                    task->task_id = j["task_id"].get<std::string>();
                                    result = 0;
                                }
                            }
                        }
                    }
                    catch (...) {
                        result = -1;
                    }

                    std::stringstream ss(body);
                    pt::ptree root;
                    pt::read_json(ss, root);
                    boost::optional<std::string> message = root.get_optional<std::string>("message");
                    boost::optional<std::string> task_id = root.get_optional<std::string>("task_id");
                    if (message.has_value()) {
                        if (message.value().compare(MSG_SUCCESS) == 0) {
                            if (task_id.has_value()) {
                                task->task_id = task_id.value();
                                return;
                            }
                        }
                    }
                    return;
                }
            )
            .on_error(
                [this, &result, &http_code, &http_body](std::string body, std::string error, unsigned status) {
                    result = -1;
                    http_code = status;
                    http_body = error;
                }
            )
            .perform_sync();
        return result;
    }

    int AccountManager::request_subtask_id(BBLSubTask* task, unsigned int &http_code, std::string &http_body)
    {
        int result = -1;
        if (!task) return -1;

        std::string json_str = json_request_body_post_task(task);
        if (json_str.empty()) {
            BOOST_LOG_TRIVIAL(trace) << "reqeust_subtask_id json_str is empty";
            return -1;
        }

        std::string url = (boost::format("%1%/iot-service/api/user/task") % host).str();

        Http http = Http::post(url);
        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .header("Content-Type", "application/json")
            .set_post_body(json_str)
            .on_complete(
                [this, task, &result, &http_code, &http_body](std::string body, unsigned status) {
                    http_code = status;
                    http_body = body;
                    try {
                        json j = json::parse(body);
                        if (is_valid_property(j, "message")) {
                            if (j["message"].get<std::string>() == MSG_SUCCESS) {
                                if (is_valid_property(j, "task_id")) {
                                    task->task_id = j["task_id"].get<std::string>();
                                    result = 0;
                                }
                            }
                        }

                    } catch(...) {
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

    // poll 3mf must have a profile id
    int AccountManager::poll_3mf(BBLProject* project, std::string profile_id, bool& cancel, Http::ErrorFn errFn)
    {
        if (!project || project->project_id.empty()) return -1;

        int retry_ = 0;
        int retry_max = POLL_3MF_TIMEOUT;

        std::string gather = json_request_poll_3mf_gather_model_only();

        gather.erase(std::remove(gather.begin(), gather.end(), '\\'), gather.end());
        gather = Http::url_encode(gather);
        std::string ticket = "0";
        std::string query_params = (boost::format("?profile_id=%1%&&gather=%2%&&ticket=%3%") % profile_id % gather % ticket).str();
        std::string url = (boost::format("%1%/iot-service/api/user/project/%2%%3%") % host % project->project_id % query_params).str();
        Http http = Http::get(url);

        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .on_complete(
                [this, project](std::string body, unsigned) {
                    std::stringstream ss(body);
                    pt::ptree root;
                    pt::read_json(ss, root);
                    if (root.empty()) return;
                    boost::optional<std::string> message = root.get_optional<std::string>("message");
                    if (message.has_value()) {
                        if (message.value().compare("ready") == 0 || message.value().compare(MSG_SUCCESS) == 0) {
                            BOOST_LOG_TRIVIAL(info) << "get_project_info ok!";
                            boost::optional<std::string> status = root.get_optional<std::string>("status");
                            boost::optional<std::string> model_id = root.get_optional<std::string>("model_id");
                            if (model_id.has_value()) {
                                project->project_model_id = model_id.value();
                            }
                            boost::optional<std::string> name = root.get_optional<std::string>("name");
                            if (name.has_value()) {
                                project->project_name = name.value();
                            }
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
                            boost::optional<std::string> create_time = root.get_optional<std::string>("create_time");
                            if (create_time.has_value()) {
                                project->project_create_time = create_time.value();
                            }
                            boost::optional<std::string> content = root.get_optional<std::string>("content");
                            if (content.has_value()) {
                                project->project_content = content.value();
                            }

                            //success
                            return;
                        }
                    }
                }
        ).on_error(errFn);

        while (project->project_url.empty() && retry_ < retry_max) {
            // cancelled
            if (cancel) {
                BOOST_LOG_TRIVIAL(trace) << "download project cancelled";
                return -1;
            }

            http.perform_sync();
            retry_++;
            BOOST_LOG_TRIVIAL(trace) << "download project failed, retry=" << retry_;
            boost::this_thread::sleep_for(boost::chrono::milliseconds(1000));
        }

        if (retry_ == retry_max) {
            BOOST_LOG_TRIVIAL(trace) << "download project failed, retry_max";
            return -1;
        }
        return 0;
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

    int AccountManager::prepare_region_config()
    {
        unsigned int http_code;
        std::string http_body;
        int result = get_region_config(http_code, http_body);
        json js;
        if (result < 0) {
            //try to load from local file
            boost::filesystem::path config_file = (boost::filesystem::path(Slic3r::data_dir()) / "region.conf");
            if (!boost::filesystem::exists(config_file))
                return result;
            std::ifstream ifs(config_file.make_preferred().string());
            js = json::parse(ifs);
        }
        else {
            // write to local config file
            std::string path = (boost::filesystem::path(Slic3r::data_dir()) / ("region.conf")).make_preferred().string();
            try {
                js = json::parse(http_body);
                std::ofstream file(path);
                file << js;
                file.close();
            } 
            catch (json::parse_error &e) {
                BOOST_LOG_TRIVIAL(trace) << "prepare_region_config, parse json failed! e=" << e.what();
                return -1;
            }
            catch (...) {
                BOOST_LOG_TRIVIAL(trace) << "prepare_region_config, parse json failed!";
                return -1;
            }
        }

        // load region server
        bool found = false;
        AppConfig * config       = wxGetApp().app_config;
        std::string country_code = RegionServer::convert_region_to_contry_code(config->get("region"));
        try {
            if (js.contains(country_code)) {
                found                                     = true;
                this->user_region_server.iot_server_host  = js[country_code]["api"].get<std::string>();
                this->user_region_server.mqtt_server_host = js[country_code]["emqx"].get<std::string>();
                this->user_region_server.tutk_server_host = js[country_code]["tutk"].get<std::string>();
                this->user_region_server.wifi_code        = js[country_code]["wifi"].get<std::string>();
            }
        }
        catch(...) {
            return -1;
        }
        if (found) {
            is_region_config_ready = true;
            return 0;
        } else {
            BOOST_LOG_TRIVIAL(error) << "can not fount " << country_code;
        }
        return -1;
    }

    int AccountManager::get_region_config(unsigned int &http_code, std::string &http_body)
    {
        int result = -1;
        std::string url = REGION_JSON_CONFIG_URL;
        Http http = Http::get(url);
        http.timeout_max(10)
            .header("accept", "application/json")
            .header("Authorization", get_token_str())
            .on_complete(
                [this, &http_code, &http_body, &result](std::string body, unsigned status)
                {
                    http_code = status;
                    http_body = body;
                    result = 0;
                })
            .on_error([this, &http_code, &http_body](std::string body, std::string error, unsigned status) {
                    http_code = status;
                    http_body = body;
                })
        .perform_sync();
        return result;
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

    std::string AccountManager::get_token_str()
    {
        if (m_curr_user) {
            return (boost::format("Bearer %1%") % m_curr_user->m_token).str();
        }
        return "";
    }

    void AccountManager::reset_project()
    {
        ;
    }

    void AccountManager::change_curr_user(AccountInfo *pAcc)
    {
        if (m_curr_user)
        {
            delete m_curr_user;
        }

        m_curr_user = pAcc;
        save_user_info();

        if (is_user_login())
        {
            on_user_login(1);
        }
    }

    std::string AccountManager::get_user_name()
    {
        if (m_curr_user) {
            return m_curr_user->m_account;
        }
        return "";
    }

    void AccountManager::set_host(std::string host_url)
    {
        /* invalid token and logout */
        user_logout();

        BOOST_LOG_TRIVIAL(trace) << "set host to " << host_url;
        host = host_url;
    }

    std::string AccountManager::_get_query_url(std::string device_id)
    {
        if (m_curr_user) {
            return (boost::format("%1%/iot-service/api/user/bind?dev_id=%2%") % host % device_id).str();
        }
        else {
            return "";
        }
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

    std::string AccountManager::_get_login_url()
    {
        return (boost::format("%1%/user-service/user/login") % host).str();
    }

    std::string AccountManager::_get_user_profile_url(std::string account)
    {
        return (boost::format("%1%/user-service/my/profile") % host).str();
    }

    std::string AccountManager::_get_register_url()
    {
        return (boost::format("%1%/user/register") % host).str();
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

    std::string AccountManager::_get_bind_list_request()
    {
        pt::ptree root;
        std::stringstream oss;
        pt::write_json(oss, root, false);
        return oss.str();
    }

    std::string AccountManager::_get_device_json(std::string device_id)
    {
        pt::ptree root;
        root.put("dev_id", device_id);
        std::stringstream oss;
        pt::write_json(oss, root, false);
        return oss.str();
    }

    std::string AccountManager::_get_query_bind_request(std::string device_id)
    {
        return _get_device_json(device_id);
    }

    std::string AccountManager::_get_bind_request(std::string device_id)
    {
        return _get_device_json(device_id);
    }

    std::string AccountManager::_get_unbind_request(std::string device_id)
    {
        return _get_device_json(device_id);
    }

    std::string AccountManager::_get_login_request(std::string account, std::string password)
    {
        pt::ptree root;
        root.put("account", account);
        root.put("password", password);
        std::stringstream oss;
        pt::write_json(oss, root, false);
        return oss.str();
    }

    std::string AccountManager::_get_register_request(std::string account, std::string password)
    {
        pt::ptree root;
        root.put("account", account);
        root.put("password", password);
        std::stringstream oss;
        pt::write_json(oss, root, false);
        return oss.str();
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

    void AccountManager::_handle_error_code(int status, std::string error, std::string body)
    {
        switch (status) {
        case 400:
        case 401:
            wxMessageBox("Token is invalid! Tips: input username@email.com, like: bbl@bambulab.com");
            break;
        default:
            return;
        }
    }

    std::string AccountManager::handle_web_request(std::string cmd)
    {
        try {
            //BBS use nlohmann json format
            json j = json::parse(cmd);

            std::string web_cmd = j["command"].get<std::string>();
            if (web_cmd == "request_model_download") {
                json j_data = j["data"];
                json import_j;
                import_j["model_id"]    = j["data"]["model_id"].get<std::string>();
                import_j["profile_id"] = j["data"]["profile_id"].get<std::string>();
                import_j["design_id"] = "";
                if (j["data"].contains("design_id"))
                    import_j["design_id"] = j["data"]["design_id"].get<std::string>();
                this->request_model_download(import_j.dump());
            }

            std::stringstream ss(cmd), oss;
            pt::ptree root, response;
            pt::read_json(ss, root);
            if (root.empty())
                return "";

            boost::optional<std::string> sequence_id    = root.get_optional<std::string>("sequence_id");
            boost::optional<std::string> command        = root.get_optional<std::string>("command");
            if (command.has_value()) {
                std::string command_str = command.value();
                if (command_str.compare("request_user_token") == 0) {
                    AccountInfo* info = get_curr_user();
                    if (info && is_user_login()) {
                        response.put("token", info->get_token());
                        root.put_child("response", response);
                        pt::write_json(oss, root, false);
                        return oss.str();
                    }
                    else {
                        response.put("token", "");
                        root.put_child("response", response);
                        pt::write_json(oss, root, false);
                        return oss.str();
                    }
                }
                else if (command_str.compare("request_project_download") == 0) {
                    if (root.get_child_optional("data") != boost::none) {
                        pt::ptree data_node = root.get_child("data");
                        boost::optional<std::string> project_id = data_node.get_optional<std::string>("project_id");
                        if (project_id.has_value()) {
                            this->request_project_download(project_id.value());
                        }
                    }
                }
                else if (command_str.compare("open_project") == 0) {
                    if (root.get_child_optional("data") != boost::none) {
                        pt::ptree data_node = root.get_child("data");
                        boost::optional<std::string> project_id = data_node.get_optional<std::string>("project_id");
                        if (project_id.has_value()) {
                            this->request_open_project(project_id.value());
                        }
                    }
                } else if (command_str.compare("get_login_info")==0) {
                    GUI::wxGetApp().CallAfter([this] { this->show_login_info(); });
                }
                else if (command_str.compare("homepage_login_or_register") == 0) {
                    GUI::wxGetApp().CallAfter([this] { this->request_login_or_register(); });
                } else if (command_str.compare("homepage_logout")==0) {
                    GUI::wxGetApp().CallAfter([this] { this->request_logout(); });
                }
                else if (command_str.compare("homepage_newproject") == 0) {
                    this->request_open_project("<new>");
                }
                else if (command_str.compare("homepage_openproject") == 0) {
                    this->request_open_project({});
                }
                else if (command_str.compare("get_recent_projects") == 0) {
                    GUI::wxGetApp().mainframe->m_webview->SendRecentList(from_u8(sequence_id.value()));
                }
                else if (command_str.compare("homepage_open_recentfile") == 0) {
                    if (root.get_child_optional("data") != boost::none) {
                        pt::ptree data_node = root.get_child("data");
                        boost::optional<std::string> path = data_node.get_optional<std::string>("path");
                        if (path.has_value()) {
                            this->request_open_project(path.value());
                        }
                    }
                }
                else if (command_str.compare("homepage_open_hotspot") == 0) {
                    if (root.get_child_optional("data") != boost::none) {
                        pt::ptree data_node = root.get_child("data");
                        boost::optional<std::string> url       = data_node.get_optional<std::string>("url");
                        if (url.has_value()) {
                            this->request_open_project(url.value());
                        }
                    }
                }
            }
        }
        catch (...) {
            BOOST_LOG_TRIVIAL(trace) << "parse json cmd failed " << cmd;
            return "";
        }
        return "";
    }

    void AccountManager::handle_http_error(unsigned int status, std::string body)
    {
        GUI::wxGetApp().handle_http_error(status, body);
    }

    void AccountManager::request_model_download(std::string import_json)
    {
        if (!is_user_login()) {
            GUI::wxGetApp().request_login();
            return;
        }
        GUI::wxGetApp().request_model_download(import_json);
    }

    void AccountManager::request_project_download(std::string project_id)
    {
        if (!is_user_login()) {
            GUI::wxGetApp().request_login();
            return;
        }
        GUI::wxGetApp().download_project(project_id);
    }

    void AccountManager::request_open_project(std::string project_id)
    {
        if (project_id == "<new>")
            GUI::wxGetApp().plater()->new_project();
        else if (project_id.empty())
            GUI::wxGetApp().plater()->load_project();
        else if (std::find_if_not(project_id.begin(), project_id.end(),
                [](char c) { return std::isdigit(c);}) == project_id.end())
            ;
        else if (boost::algorithm::starts_with(project_id, "http"))
            ;
        else
            GUI::wxGetApp().plater()->load_project(wxString::FromUTF8(project_id));
    }

    void AccountManager::request_login_or_register()
    {
         //GUI::LoginDialog dlg;
         //dlg.ShowModal();

         GUI::ZUserLogin dlg;
         dlg.run();

         show_login_info();
    }

    void AccountManager::show_login_info()
    {
        if (is_user_login()) {
            json m_Res              = json::object();
            m_Res["command"]        = "studio_userlogin";
            m_Res["sequence_id"]    = "10001";
            m_Res["data"]           = json::object();
            m_Res["data"]["avatar"] = m_curr_user->m_avatar;
            m_Res["data"]["name"]   = m_curr_user->m_name;

            wxString strJS = wxString::Format("HandleStudio(%s)", m_Res.dump(-1, ' ', false, json::error_handler_t::ignore));

            GUI::wxGetApp().run_script(strJS);
        } else {
            request_logout();
        }    
    }


    void AccountManager::request_logout()
    {
        user_logout();

        json m_Res              = json::object();
        m_Res["command"]        = "studio_useroffline";
        m_Res["sequence_id"]    = "10001";

        wxString strJS = wxString::Format("HandleStudio(%s)", m_Res.dump(-1, ' ', false, json::error_handler_t::ignore));

        GUI::wxGetApp().run_script(strJS);
    }



} // namespace Slic3r
