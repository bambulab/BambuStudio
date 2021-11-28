#include "libslic3r/libslic3r.h"
#include "AccountManager.hpp"
#include "DeviceManager.hpp"
#include "libslic3r/Time.hpp"
#include "libslic3r/Thread.hpp"
#include "libslic3r/AppConfig.hpp"
#include "slic3r/Utils/Http.hpp"
#include "slic3r/GUI/GUI_App.hpp"
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
    void cloud_conn_callback::connected(const std::string& cause)
    {
        BOOST_LOG_TRIVIAL(trace) << "cloud_conn_callback::connected!";
        /* update sub_topics */
        if (successFn) {
            successFn(cli_.get_client_id());
        }
        AccountManager* manager = (AccountManager*)context_;
        boost::thread update_thread = Slic3r::create_thread([manager] { manager->update_subscription(); });
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
            BOOST_LOG_TRIVIAL(trace) << "message topic:" << msg->get_topic() << ", payload=" << msg->get_payload_str();
            std::string topic = msg->get_topic();
            std::map<std::string, MachineObject*>::iterator it = manager->mqtt_topics.find(topic);
            if (it != manager->mqtt_topics.end()) {
                if (it->second)
                    it->second->parse_json(msg->get_topic(), msg->get_payload_str());

                // update my bind list machine
                std::map<std::string, MachineObject*>::iterator iter = manager->myBindMachineList.find(it->second->dev_id);
                if (iter != manager->myBindMachineList.end()) {
                    iter->second->parse_json(msg->get_topic(), msg->get_payload_str());
                }
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

    int AccountInfo::save_to_json(std::string filename)
    {
        try {
            pt::ptree root;
            root.put("account", m_account);
            root.put("token", m_token);
            root.put("user_id", m_user_id);
            root.put("login_status", m_login_status);
            pt::write_json(filename, root);
            return 0;
        }
        catch (std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "AccountManager::save_to_json() failed! exception=" << e.what();
            return -1;
        }
    }

    AccountInfo* AccountInfo::load_from_json(std::string filename)
    {
        try {
            std::ifstream f(filename.c_str());
            if (f.good()) {
                pt::ptree root;
                pt::read_json(f, root);
                f.close();
                std::string account = root.get<std::string>("account");
                std::string token = root.get<std::string>("token");
                std::string user_id = root.get<std::string>("user_id");
                AccountInfo::LoginStatus status = (AccountInfo::LoginStatus)root.get<int>("login_status");
                AccountInfo* info = new AccountInfo(account, user_id, status);
                info->set_token(token);
                return info;
            }
            else {
                BOOST_LOG_TRIVIAL(trace) << "load json failed! filename=" << filename;
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

        mqtt_opt.set_user_name(mqtt_user);
        mqtt_opt.set_password(mqtt_pwd);
        mqtt_opt.set_max_inflight(500);
        mqtt_opt.set_connect_timeout(10);
        mqtt_opt.set_automatic_reconnect(3, 10);

        m_curr_user = nullptr;

        std::time_t t = std::time(0);
        std::tm* now_time = std::localtime(&t);
        std::stringstream buf;
        buf << std::put_time(now_time, "debug_http_%a_%b_%d_%H_%M_%S.log");
        std::string log_filename = buf.str();
        Http::enable_log(log_filename.c_str());
        Http::register_global_handler(
            [this](std::string body, std::string error, unsigned int status) {
                handle_http_error(status, body);
            }
        );
    }

    AccountManager::~AccountManager()
    {
        Http::disable_log();
    }

    int AccountManager::load_user_info()
    {
        m_curr_user = AccountInfo::load_from_json(account_json);
        if (this->is_user_login()) {
            this->on_user_login();
        }
        return 0;
    }

    void AccountManager::on_user_login(bool online_login)
    {
        BOOST_LOG_TRIVIAL(info) << "set_preset: set preset_folder = " << get_curr_user()->get_user_id();
        GUI::wxGetApp().app_config->set("preset_folder", get_curr_user()->get_user_id());
        connect_mqtt();
        request_bind_list();
 

 
        if (online_login)
            GUI::wxGetApp().reload_user_presets();
    }

    int AccountManager::save_user_info()
    {
        if (m_curr_user) {
            return m_curr_user->save_to_json(account_json);
        }
        return 0;
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
            std::string client_id = (boost::format("%1%:%2%") % m_curr_user->m_user_id % mqtt_uuid).str();
            mqtt_cli = new mqtt::async_client(MQTT_HOST, client_id);
            mqtt_cb = new cloud_conn_callback(*mqtt_cli, mqtt_opt, this);
            if (mqtt_cli) {
                mqtt_cli->set_callback(*mqtt_cb);
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
        if (mqtt_cli) {
            mqtt_cli->stop_consuming();
            if (mqtt_cli->is_connected()) {
                mqtt_cli->disconnect();
            }
            delete mqtt_cli;
            mqtt_cli = nullptr;
        }
        return 0;
    }

    void AccountManager::add_subscribe(MachineObject* obj)
    {
        std::string report_topic = (boost::format("device/%1%/report") % obj->dev_id).str();
        std::map<std::string, MachineObject*>::iterator it = mqtt_topics.find(report_topic);
        if (it != mqtt_topics.end()) {
            return;
        }

        try {
            if (mqtt_cli && mqtt_cli->is_connected()) {
                if (mqtt_topics.find(report_topic) == mqtt_topics.end()) {
                    BOOST_LOG_TRIVIAL(trace) << "add_subscribe topic=" << report_topic;
                    mqtt_topics.insert(std::make_pair(report_topic, obj));
                    action_listener* sub_listener = new action_listener("MQTT_Subscriber_" + report_topic, this);
                    mqtt_cli->subscribe(report_topic, 0, this, *sub_listener);
                }
                else {
                    BOOST_LOG_TRIVIAL(trace) << "add_subscribe topic=" << report_topic << " is exists";
                }
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

    void AccountManager::update_subscription()
    {
        std::map<std::string, MachineObject*>::iterator it;
        for (it = myBindMachineList.begin(); it != myBindMachineList.end(); it++) {
            add_subscribe(it->second);
        }
    }

    bool AccountManager::is_user_login()
    {
        if (m_curr_user) {
            return m_curr_user->login_status() == AccountInfo::LoginStatus::STATUS_LOGIN;
        }
        else
            return false;
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
                    if (m_curr_user) delete m_curr_user;
                    m_curr_user = new AccountInfo(account, "", AccountInfo::LoginStatus::STATUS_LOGIN);
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
                            this->on_user_login(true);
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

    MachineObject* AccountManager::get_default_machine()
    {
        if (default_machine.empty())
            return nullptr;

        /* find in local list */
        std::map<std::string, MachineObject*>::iterator it = myBindMachineList.find(default_machine);
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
                        obj->is_online = online.compare("true") == 0 ? true : false;
                    }
                } else {
                    MachineObject* obj = new MachineObject(*this, dev_name, dev_id, "");
                    obj->is_online = online.compare("true") == 0 ? true : false;
                    obj->set_bind_status(this->get_user_name());
                    myBindMachineList.insert(std::make_pair(dev_id, obj));

                    /* insert a new machine event */
                    this->add_subscribe(obj);
                }
            }
            std::map<std::string, MachineObject*>::iterator iterat;
            for (iterat = myBindMachineList.begin(); iterat != myBindMachineList.end(); ) {
                if (new_list.find(iterat->first) == new_list.end()) {
                    this->del_subscribe(iterat->second);
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
                    BOOST_LOG_TRIVIAL(trace) << "Query bind device list Error! error = " << body;
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

        std::string url = (boost::format("%1%/iot/device/%2%/bind") % host % device_id).str();
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
        std::string url = (boost::format("%1%/iot/user/bind") % host).str();

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
        std::string url = (boost::format("%1%/iot/user/report") % host).str();
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

    void AccountManager::check_new_version()
    {
        std::string platform = "WINDOWS";
#ifdef __WINDOWS__
        platform = "WINDOWS";
#endif
#ifdef __APPLE__
        platform = "MAC";
#endif
#ifdef __LINUX__
        platform = "LINUX";
#endif
        std::string query_params = (boost::format("?name=BBLS&&version=%1%&&platform=%2%&&guide_version=%3%")
            % VersionInfo::convert_full_version(SLIC3R_RC_VERSION)
            % platform
            % VersionInfo::convert_full_version("0.0.0.1")
            ).str();
        std::string url = _get_slicer_info_url() + query_params;
        Http http = Http::get(url);
        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .on_complete([this](std::string body, unsigned) {
                std::stringstream ss(body);
                pt::ptree root;
                pt::read_json(ss, root);
                if (root.empty()) return;
                boost::optional<std::string> message = root.get_optional<std::string>("message");
                if (message.has_value()) {
                    if (message.value().compare(MSG_SUCCESS) == 0) {
                        if (root.get_child_optional("software") != boost::none) {
                            pt::ptree software_node = root.get_child("software");
                            boost::optional<std::string> url = software_node.get_optional<std::string>("url");
                            boost::optional<std::string> version = software_node.get_optional<std::string>("version");
                            boost::optional<std::string> description = software_node.get_optional<std::string>("description");

                            if (url.has_value())
                                version_info.url = url.value();
                            if (version.has_value()) {
                                version_info.parse_version_str(version.value());
                            }
                            if (description.has_value())
                                version_info.description = description.value();

                            check_update();
                        }
                    }
                }
            }).perform();
    }

    void AccountManager::check_update()
    {
        if (version_info.version_str.empty()) return;
        if (version_info.url.empty()) return;

        if (version_info.compare(SLIC3R_RC_VERSION) > 0) {
            GUI::wxGetApp().request_new_version();
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
                        this->update_my_bind_list(body);
                        if (fn) {
                            fn(0, "get bind list ok");
                        }
                        return;
                    }
                    else if (message.value().compare("nodev") == 0) {
                        this->update_my_bind_list(body);
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
        std::string project_name_str = project->project_name;

        pt::ptree root;
        root.put("name", project_name_str);
        /* optional model_id */
        if (!project->project_model_id.empty()) {
            root.put("model_id", project->project_model_id);
        }
        /* optional content */
        if (!project->project_content.empty()) {
            root.put("content", project->project_content);
        }

        std::stringstream oss;
        pt::write_json(oss, root, false);
        return oss.str();
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

        std::string task_name_str = task->task_name;
        pt::ptree root;
        root.put<int>("parent", 0);
        root.put("project_id", task->task_project_id);
        root.put("profile_id", task->task_profile_id);
        root.put("name", task_name_str);
        root.put("content", task->build_content_json());
        std::stringstream oss;
        pt::write_json(oss, root, false);
        return oss.str();
    }

    std::string AccountManager::json_request_body_post_task(BBLSubTask* task)
    {
        if (!task) return "";

        std::string task_name_str = task->task_name;
        pt::ptree root;
        
        if (task->parent_task_) {
            root.put("parent", task->parent_task_->task_id);
            root.put("project_id", task->parent_task_->task_project_id);
            root.put("profile_id", task->parent_task_->task_profile_id);
        }
        root.put("dev_id", task->task_printer_dev_id);
        root.put("name", task_name_str);
        root.put("content", task->build_content_json());
        std::stringstream oss;
        pt::write_json(oss, root, false);
        return oss.str();
    }

    std::string AccountManager::json_request_body_post_setting(Preset* preset)
    {
        std::string result;
        if (!preset) return "";

        pt::ptree root, setting_node;
        root.put<bool>("public", preset->is_system ? true : false);
        if (!preset->version.empty())
            root.put("version", preset->version);
        else
            root.put("version", DEFAULT_BBL_SETTING_VERSION);
        root.put("type", Preset::get_type_string(preset->type));
        root.put("name", preset->name);

        if (!preset->base_id.empty()) {
            root.put("base_id", "");
            //TODO put changed values to setting node
            root.add_child("setting", setting_node);
        }
        
        std::stringstream oss;
        pt::write_json(oss, root, false);
        result = oss.str();
        result = boost::replace_all_copy(result, "\"true\"", "true");
        result = boost::replace_all_copy(result, "\"false\"", "false");
        return result;
    }

    std::string AccountManager::json_request_body_put_setting(Preset* preset)
    {
        if (!preset) return "";

        pt::ptree root, setting_node;
        if (!preset->version.empty())
            root.put("version", preset->version);
        else
            root.put("version", DEFAULT_BBL_SETTING_VERSION);
        root.put("name", preset->name);
        for (const std::string &opt_key : preset->config.keys()) {
            setting_node.put(opt_key, preset->config.opt_serialize(opt_key));
        }
        root.add_child("setting", setting_node);
        std::stringstream oss;
        pt::write_json(oss, root, false);
        return oss.str();
    }

    std::string AccountManager::json_request_poll_3mf_gather_model_only()
    {
        pt::ptree root, profile_files;
        root.put<bool>("base_model", true);
        root.put<bool>("profile_config", true);
        root.put<bool>("profile_thumbnail", false);
        root.put<bool>("profile_gcode", false);
        root.add_child("profile_files", profile_files);
        std::stringstream oss;
        pt::write_json(oss, root, false);
        return oss.str();
    }

    std::string AccountManager::json_request_poll_3mf_gather(BBLSubTask* task)
    {
        if (!task) return "";

        pt::ptree root, profile_files, files;
        root.put<bool>("base_model", false);
        root.put<bool>("profile_config", false);
        root.put<bool>("profile_thumbnail", false);
        root.put<bool>("profile_gcode", false);
        files.put("", task->task_gcode_in_3mf);
        profile_files.push_back(std::make_pair("", files));
        root.add_child("profile_files", profile_files);
        std::stringstream oss;
        pt::write_json(oss, root, false);
        return oss.str();
    }

    int AccountManager::request_project_id(BBLProject* project, ResultFn resFn)
    {
        int result = 0;
        if (!project) {
            if (resFn) {
                resFn(-1, "Invalid Project");
            }
            return -1;
        }

        /* get a project id and model id */
        std::string json_str = json_request_body_post_project(project);

        Http http_post = Http::post(_get_project_url());
        http_post.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .header("Content-Type", "application/json")
            .set_post_body(json_str)
            .on_complete(
                [this, project, resFn](std::string body, unsigned) {
                    try {
                        BOOST_LOG_TRIVIAL(trace) << "AccountManager::request_project_id, body=" << body;
                        std::stringstream ss(body);
                        pt::ptree root;
                        pt::read_json(ss, root);
                        boost::optional<std::string> message = root.get_optional<std::string>("message");
                        boost::optional<std::string> project_id = root.get_optional<std::string>("project_id");
                        boost::optional<std::string> model_id = root.get_optional<std::string>("model_id");
                        if (message.has_value()) {
                            if (message.value().compare(MSG_SUCCESS) == 0) {
                                if (project_id.has_value() && model_id.has_value()) {
                                    project->project_id = project_id.value();
                                    project->project_model_id = model_id.value();
                                    if (resFn) {
                                        resFn(0, "");
                                    }
                                    //success return
                                    return;
                                }
                            }
                        }

                        // failed return
                        if (resFn) {
                            resFn(-1, "get project id failed! body=" + body);
                        }
                    }
                    catch (std::exception& e) {
                        BOOST_LOG_TRIVIAL(trace) << (boost::format("AccountManager::create_project on_complete parsing failed, exception=%1% body=%2%")
                            % e.what()
                            % body).str();
                        if (resFn) {
                            resFn(-1, "get project id failed! body=" + body);
                        }
                    }
                    catch (...) {
                        BOOST_LOG_TRIVIAL(trace) << "AccountManager::create_project on_complete parsing failed, body=" << body;
                        if (resFn) {
                            resFn(-1, "get project id failed! body=" + body);
                        }
                    }
                }
            )
            .on_error(
                [&, resFn](std::string body, std::string error, unsigned status) {
                    BOOST_LOG_TRIVIAL(trace) << boost::format("status=%1%, error=%2%, body=%3%")
                        % status
                        % error
                        % body;
                    delete project;
                    project = nullptr;
                    if (resFn) {
                        resFn(-1, "get project id failed! body=" + body);
                    }
                }
            )
        .perform_sync();
        return 0;
    }

    int AccountManager::request_profile_id(BBLProfile* profile, ResultFn resFn)
    {
        if (!profile) return -1;

        std::string project_url = (boost::format("%1%/iot/user/project/%2%") % host % profile->project_id).str();

        std::vector<std::string> params;
        std::string request_str = json_request_body_post_profile(profile);

        Http http_create_profile = Http::post(project_url);
        http_create_profile.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .header("Content-Type", "application/json")
            .set_post_body(request_str)
            .on_complete(
                [this, profile, resFn](std::string body, unsigned) {
                    BOOST_LOG_TRIVIAL(trace) << "request profile id, body=" << body;
                    std::stringstream ss(body);
                    pt::ptree root;
                    pt::read_json(ss, root);
                    boost::optional<std::string> message = root.get_optional<std::string>("message");
                    boost::optional<std::string> profile_id = root.get_optional<std::string>("profile_id");
                    if (message.has_value()) {
                        if (message.value().compare(MSG_SUCCESS) == 0) {
                            if (profile_id.has_value()) {
                                profile->profile_id = profile_id.value();
                                if (resFn) {
                                    resFn(0, "");
                                }
                                // success return
                                return;
                            }
                        }
                    }

                    if (resFn) {
                        resFn(-1, "get profile id failed! body=" + body);
                    }
                    return;
                }
            )
            .on_error(
                [this, profile, resFn](std::string body, std::string error, unsigned status) {
                    BOOST_LOG_TRIVIAL(trace) << "create_profile failed! body=" << body << ", status=" << status;
                    if (resFn) {
                        resFn(-1, "get profile id failed! body=" + body);
                    }
                }
            )
            .perform_sync();
        return 0;
    }

    int AccountManager::request_task_id(BBLTask* task, ResultFn resFn)
    {
        if (!task) return -1;
        std::string json_str = json_request_body_post_task(task);
        std::string url = (boost::format("%1%/iot/user/task") % host).str();
        task->task_id.clear();

        Http http = Http::post(url);
        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .header("Content-Type", "application/json")
            .set_post_body(json_str)
            .on_complete(
                [this, task, resFn](std::string body, unsigned) {
                    BOOST_LOG_TRIVIAL(trace) << "request task id, body=" << body;
                    std::stringstream ss(body);
                    pt::ptree root;
                    pt::read_json(ss, root);
                    boost::optional<std::string> message = root.get_optional<std::string>("message");
                    boost::optional<std::string> task_id = root.get_optional<std::string>("task_id");
                    if (message.has_value()) {
                        if (message.value().compare(MSG_SUCCESS) == 0) {
                            if (task_id.has_value()) {
                                task->task_id = task_id.value();
                                if (resFn) {
                                    resFn(0, "");
                                }
                                // success return
                                return;
                            }
                        }
                    }

                    if (resFn) {
                        resFn(-1, "get task id failed! body=" + body);
                    }
                    return;
                }
            )
            .on_error(
                [this, resFn](std::string body, std::string error, unsigned status) {
                    BOOST_LOG_TRIVIAL(trace) << "create_profile failed! body=" << body << ", status=" << status;
                    if (resFn) {
                        resFn(-1, "get task id failed! body=" + body);
                    }
                }
            )
            .perform_sync();
        return 0;
    }

    int AccountManager::request_subtask_id(BBLSubTask* task, ResultFn resFn)
    {
        if (!task) return -1;
        std::string json_str = json_request_body_post_task(task);
        std::string url = (boost::format("%1%/iot/user/task") % host).str();
        task->task_id.clear();

        Http http = Http::post(url);
        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .header("Content-Type", "application/json")
            .set_post_body(json_str)
            .on_complete(
                [this, task, resFn](std::string body, unsigned) {
                    BOOST_LOG_TRIVIAL(trace) << "request task id, body=" << body;
                    std::stringstream ss(body);
                    pt::ptree root;
                    pt::read_json(ss, root);
                    boost::optional<std::string> message = root.get_optional<std::string>("message");
                    boost::optional<std::string> task_id = root.get_optional<std::string>("task_id");
                    if (message.has_value()) {
                        if (message.value().compare(MSG_SUCCESS) == 0) {
                            if (task_id.has_value()) {
                                task->task_id = task_id.value();
                                if (resFn) {
                                    resFn(0, "");
                                }
                                // success return
                                return;
                            }
                        }
                    }

                    if (resFn) {
                        resFn(-1, "get task id failed! body=" + body);
                    }
                    return;
                }
            )
            .on_error(
                [this, resFn](std::string body, std::string error, unsigned status) {
                    BOOST_LOG_TRIVIAL(trace) << "create_profile failed! body=" << body << ", status=" << status;
                    if (resFn) {
                        resFn(-1, "get task id failed! body=" + body);
                    }
                }
            )
            .perform_sync();
        return 0;
    }

    int AccountManager::upload_3mf(BBLProfile* profile, ResultFn resFn, Http::ProgressFn proFn, bool sync)
    {
        if (!profile || !profile->project_) return -1;

        /* upload 3mf or gcode to cloud */
        std::string project_url = (boost::format("%1%/iot/user/project/%2%") % host % profile->project_id).str();
        std::string file_str("file");
        std::string profile_id_str("profile_id");
        std::string project_file = encode_path(profile->project_->project_path.generic_string().c_str());

        Http http_put = Http::put2(project_url);
        http_put.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .header("Content-Type", "multipart/form-data")
            .mime_form_add_file(file_str, project_file.c_str())
            .mime_form_add_text(profile_id_str, profile->profile_id)
            .on_complete(
                [this, resFn](std::string body, unsigned) {
                    std::stringstream ss(body);
                    pt::ptree root;
                    pt::read_json(ss, root);
                    boost::optional<std::string> message = root.get_optional<std::string>("message");
                    if (message.has_value()) {
                        if (message.value().compare(MSG_SUCCESS) == 0) {
                            BOOST_LOG_TRIVIAL(info) << "create_project, upload project ok!";
                            if (resFn) {
                                resFn(0, "");
                            }
                            return;
                        }
                    }
                    if (resFn) {
                        resFn(-1, "upload_3mf_to_project failed! body=" + body);
                    }
                }
            )
            .on_progress(proFn)
            .on_error(
                [this, resFn](std::string body, std::string error, unsigned status) {
                    BOOST_LOG_TRIVIAL(info) << "create_project, upload project failed! body=" << body;
                    if (resFn) {
                        std::string info("upload project failed!");
                        info += " body=" + body;
                        resFn(-1, info);
                    }
                }
            );

        if (sync) {
            http_put.perform_sync();
        }
        else {
            http_put.perform();
        }
        return 0;
    }

    int AccountManager::poll_3mf(BBLSubTask* task)
    {
        if (!task) return -1;
        if (task->parent_id.empty() || task->task_profile_id.empty() || task->task_project_id.empty()) return -1;

        std::string ticket = (boost::format("%1%_%2%") % task->parent_id % task->task_id).str();
        std::string gather = json_request_poll_3mf_gather(task);
        gather.erase(std::remove(gather.begin(), gather.end(), '\\'), gather.end());
        gather = Http::url_encode(gather);
        std::string query_params = (boost::format("?profile_id=%1%&&ticket=%2%&&gather=%3%") % task->task_profile_id % ticket % gather).str();
        std::string url = (boost::format("%1%/iot/user/project/%2%%3%") % host % task->task_project_id % query_params).str();
        
        int retry_ = 0;
        int retry_max = POLL_3MF_TIMEOUT;
        
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
                        if (message.value().compare("ready") == 0) {
                            BOOST_LOG_TRIVIAL(info) << "get_project_info ok!";
                            boost::optional<std::string> url = root.get_optional<std::string>("url");
                            if (url.has_value()) {
                                // check valid url
                                if (url.value().compare("null") != 0) {
                                    task->task_url = url.value();
                                }
                            }
                            boost::optional<std::string> md5 = root.get_optional<std::string>("md5");
                            if (md5.has_value()) {
                                if (md5.value().compare("null") != 0) {
                                    task->task_url_md5 = md5.value();
                                }
                            }
                            //success
                            return;
                        }
                    }
                    //failed
                    return;
                }
            ).on_error(
                [this](std::string body, std::string error, unsigned status) {
                    BOOST_LOG_TRIVIAL(info) << "get_project_info failed! body=" << body;
                });

        while (task->task_url.empty() || task->task_url.compare("null") == 0 && retry_ < retry_max) {
            http.perform_sync();
            retry_++;
            BOOST_LOG_TRIVIAL(trace) << "get_task_url, retry=" << retry_;
            boost::this_thread::sleep_for(boost::chrono::milliseconds(1000));
        }
        if (retry_ == retry_max) {
            BOOST_LOG_TRIVIAL(trace) << "get_task_url, retry_max";
            return -1;
        }
        return 0;
    }

    // poll 3mf must have a profile id
    int AccountManager::poll_3mf(BBLProject* project, std::string profile_id, Http::ErrorFn errFn)
    {
        if (!project || project->project_id.empty()) return -1;

        int retry_ = 0;
        int retry_max = POLL_3MF_TIMEOUT;

        std::string gather = json_request_poll_3mf_gather_model_only();
        gather = boost::replace_all_copy(gather, "\"true\"", "true");
        gather = boost::replace_all_copy(gather, "\"false\"", "false");

        gather.erase(std::remove(gather.begin(), gather.end(), '\\'), gather.end());
        gather = Http::url_encode(gather);
        std::string ticket = "0";
        std::string query_params = (boost::format("?profile_id=%1%&&gather=%2%&&ticket=%3%") % profile_id % gather % ticket).str();
        std::string url = (boost::format("%1%/iot/user/project/%2%%3%") % host % project->project_id % query_params).str();
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

    int AccountManager::poll_3mf(BBLProfile* profile)
    {
        if (!profile) return -1;

        std::string ticket = profile->profile_id;
        std::string query_params = (boost::format("?profile_id=%1%&&ticket=%2%") % profile->profile_id % ticket).str();

        std::string url = (boost::format("%1%/iot/user/project/%2%%3%") % host % profile->project_id % query_params).str();

        BBLProject* project = profile->project_;

        int retry_ = 0;
        int retry_max = POLL_3MF_TIMEOUT;
        
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
                        if (message.value().compare("ready") == 0) {
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
                            //success
                            return;
                        }
                    }
                    //failed
                    return;
                }
            )
            .on_error(
                [this](std::string body, std::string error, unsigned status) {
                    BOOST_LOG_TRIVIAL(info) << "get_project_info failed! body=" << body;
                });
        

        while (project->project_url.empty() && retry_ < retry_max) {
                http.perform_sync();
                retry_++;
                BOOST_LOG_TRIVIAL(trace) << "get_profile_info, retry=" << retry_;
                boost::this_thread::sleep_for(boost::chrono::milliseconds(1000));
        }
        if (retry_ == retry_max) {
            BOOST_LOG_TRIVIAL(trace) << "get_profile_info, retry_max";
            return -1;
        }
        return 0;
    }

    void AccountManager::get_task(BBLTask* &task)
    {
        std::string url = (boost::format("%1%/iot/user/task/%2%") % host % task->task_id).str();
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
        std::string url = (boost::format("%1%/iot/user/task/%2%") % host % subtask->task_id).str();
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

    void AccountManager::get_profile(BBLProject*& project, BBLProfile*& profile)
    {
        if (!profile || !project) return;

        std::string query_params = (boost::format("?profile_id=%1%") % profile->profile_id).str();
        std::string url = (boost::format("%1%/iot/user/project/%2%%3%") % host % profile->project_id % query_params).str();

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
        std::string url = (boost::format("%1%/iot/user/project/%2%?%3%") % host % project->project_id % query_params).str();
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

        std::string url = (boost::format("%1%/iot/user/project/%2%?%3%") % host % project->project_id % query_params).str();

        int retry_ = 0;
        int retry_max = 10;
        Http http = Http::get(url);
        while (project->project_url.empty() && retry_ < retry_max) {
            http.header("accept", "application/json")
                .header("Authorization", get_token_str())
                .on_complete(
                    [this, project, profile](std::string body, unsigned) {
                        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
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

    void AccountManager::create_task(BBLProject* project, BBLTask* task, ResultFn resFn)
    {

        if (!project || !task) {
            return;
        }

        std::string task_url = (boost::format("%1%/iot/user/task") % host % project->project_id).str();
        std::string json_str = json_request_body_post_task(task);

        Http http = Http::post(task_url);
        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .header("Content-Type", "application/json")
            .set_post_body(json_str)
            .on_complete(
                [this, task, resFn](std::string body, unsigned) {
                    std::stringstream ss(body);
                    pt::ptree root;
                    pt::read_json(ss, root);
                    boost::optional<std::string> message = root.get_optional<std::string>("message");
                    boost::optional<std::string> task_id = root.get_optional<std::string>("task_id");
                    if (message.has_value()) {
                        if (message.value().compare(MSG_SUCCESS) == 0) {
                            BOOST_LOG_TRIVIAL(info) << "create_profile ok!";
                            if (task_id.has_value()) {
                                task->task_id = task_id.value();
                            }
                        }
                    }
                }
            )
            .on_error(
                [this, task, resFn](std::string body, std::string error, unsigned status) {
                    BOOST_LOG_TRIVIAL(info) << "create_profile failed! body=" << body << ", status=" << status;
                    if (resFn) {
                        resFn(-2, "create task id failed!");
                    }
                }).perform_sync();
    }

    void AccountManager::post_task(BBLSubTask* task, ResultFn resFn, ProgressFn proFn)
    {
        if (!task) return;

        std::string url = (boost::format("%1%/iot/user/storage") % host).str();

        std::string file_str = "file";
        std::string name_str = "name";
        std::string expires_str = "expires";
        std::string name_value = "name";
        std::string expires_value = "86400";
        std::string filename_str = task->task_path.filename().generic_string();
        std::string task_file = encode_path(task->task_path.generic_string().c_str());
        
        Http http = Http::post(url);
        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .header("Content-Type", "multipart/form-data")
            .mime_form_add_file(file_str, task_file.c_str())
            .mime_form_add_text(name_str, filename_str)
            .mime_form_add_text(expires_str, expires_value)
            .on_complete(
                [this, task, resFn](std::string body, unsigned) {
                    std::stringstream ss(body);
                    pt::ptree root;
                    pt::read_json(ss, root);
                    boost::optional<std::string> message = root.get_optional<std::string>("message");
                    boost::optional<std::string> url = root.get_optional<std::string>("url");
                    boost::optional<std::string> md5 = root.get_optional<std::string>("md5");
                    if (message.has_value()) {
                        if (message.value().compare(MSG_SUCCESS) == 0) {
                            if (url.has_value() && md5.has_value()) {
                                task->task_url = url.value();
                                task->task_url_md5 = md5.value();
                                if (resFn) {
                                    resFn(0, "get task url=" + url.value());
                                }
                                return;
                            }
                        }
                    }
                    if (resFn) {
                        resFn(-1, "error=" + body);
                    }
                }
            )
            .on_progress(
                [this, proFn](Http::Progress progress, bool &cancel) {
                    BOOST_LOG_TRIVIAL(trace) << "progress:" << progress.ulnow << "/" << progress.ultotal;
                    int percent = 0;
                    if (progress.ultotal != 0) {
                        percent = progress.ulnow * 100 / progress.ultotal;
                    }
                    if (proFn) {
                        proFn(percent);
                    }
                }
            )
            .on_error(
                [this, resFn](std::string body, std::string error, unsigned status) {
                    BOOST_LOG_TRIVIAL(info) << "upload subtask failed! body=" << body << ", status=" << status;
                    if (resFn) {
                        resFn(-2, "create task id failed!");
                    }
                    return;
            }).perform();
    }

    bool AccountManager::can_publish()
    {
        if (!is_user_login()) return false;
        return true;
    }

    //BBS sync preset bundle when login
    int AccountManager::get_setting_list(Http::ErrorFn errFn)
    {
        my_presets.clear();

        std::string version = DEFAULT_BBL_SETTING_VERSION;
        std::string query_params = (boost::format("?version=%s") % version).str();
        std::string url = (boost::format("%1%/iot/slicer/setting%2%") % host % query_params).str();
        Http http = Http::get(url);
        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .on_complete(
                [this](std::string body, unsigned) {
                    std::stringstream ss(body);
                    pt::ptree root;
                    pt::read_json(ss, root);
                    if (root.empty()) return;
                    boost::optional<std::string> message = root.get_optional<std::string>("message");
                    if (message.has_value() && message.value().compare(MSG_SUCCESS) == 0) {
                        if (root.get_child_optional("printer") != boost::none) {
                            pt::ptree printer_node = root.get_child("printer");
                            if (printer_node.get_child_optional("public") != boost::none) {
                                pt::ptree public_node = printer_node.get_child("public");
                                for (auto setting_item = public_node.begin(); setting_item != public_node.end(); ++setting_item) {
                                    parse_setting(setting_item->second, "printer", "public");
                                }
                            }
                            if (printer_node.get_child_optional("private") != boost::none) {
                                    pt::ptree private_node = printer_node.get_child("private");
                                    for (auto setting_item = private_node.begin(); setting_item != private_node.end(); ++setting_item) {
                                        parse_setting(setting_item->second, "printer", "private");
                                    }
                                }
                        }
                        if (root.get_child_optional("filament") != boost::none) {
                            pt::ptree filament_node = root.get_child("filament");
                            if (filament_node.get_child_optional("public") != boost::none) {
                                pt::ptree public_node = filament_node.get_child("public");
                                for (auto setting_item = public_node.begin(); setting_item != public_node.end(); ++setting_item) {
                                    parse_setting(setting_item->second, "filament", "public");
                                }
                            }
                            if (filament_node.get_child_optional("private") != boost::none) {
                                pt::ptree private_node = filament_node.get_child("private");
                                for (auto setting_item = private_node.begin(); setting_item != private_node.end(); ++setting_item) {
                                    parse_setting(setting_item->second, "filament", "private");
                                }
                            }
                        }
                        if (root.get_child_optional("print") != boost::none) {
                            pt::ptree print_node = root.get_child("print");
                            if (print_node.get_child_optional("public") != boost::none) {
                                pt::ptree public_node = print_node.get_child("public");
                                for (auto setting_item = public_node.begin(); setting_item != public_node.end(); ++setting_item) {
                                    parse_setting(setting_item->second, "print", "public");
                                }
                            }
                            if (print_node.get_child_optional("private") != boost::none) {
                                pt::ptree private_node = print_node.get_child("private");
                                for (auto setting_item = private_node.begin(); setting_item != private_node.end(); ++setting_item) {
                                    parse_setting(setting_item->second, "print", "private");
                                }
                            }
                        }
                        }
                    }
        ).on_error(errFn)
        .perform_sync();

        std::map<std::string, Preset*>::iterator it;
        for (it = my_presets.begin(); it != my_presets.end(); it++) {
            get_setting(it->second, true);
        }

        GUI::wxGetApp().reload_settings();
        return 0;
    }

    void AccountManager::get_setting(Preset* &preset, bool sync)
    {
        std::string url = (boost::format("%1%/iot/slicer/setting/%2%") % host % preset->setting_id).str();
        Http http = Http::get(url);

        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .on_complete(
                [this, preset](std::string body, unsigned) {
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

                        // check setting field and update setting field
                        if (root.get_child_optional("setting") != boost::none) {
                            pt::ptree setting_node = root.get_child("setting");
                            for (auto item = setting_node.begin(); item != setting_node.end(); ++item) {
                                preset->key_values.insert(std::make_pair(item->first, item->second.data()));
                            }
                        }
                        if (version.has_value())
                            preset->key_values.insert(std::make_pair("version", version.value()));
                        if (m_curr_user)
                            preset->key_values.insert(std::make_pair("user_id", m_curr_user->get_user_id()));
                    }
                }
        );
        if (sync)
            http.perform_sync();
        else
            http.perform();
    }

    int AccountManager::request_setting_id(Preset* &preset)
    {
        if (!preset) return -1;

        std::string url = (boost::format("%1%/iot/slicer/setting") % host).str();

        std::vector<std::string> params;
        std::string request_str = json_request_body_post_setting(preset);

        Http http = Http::post(url);
        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .header("Content-Type", "application/json")
            .set_post_body(request_str)
            .on_complete(
                [this, preset](std::string body, unsigned) {
                    BOOST_LOG_TRIVIAL(trace) << "request setting id, body=" << body;
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
                }
            ).perform_sync();
        return 0;
    }

    int AccountManager::put_setting(Preset* preset)
    {
        int result = -1;
        int* result_ptr = &result;
        std::string request_body = json_request_body_put_setting(preset);
        std::string url = (boost::format("%1%/iot/slicer/setting/%2%") % host % preset->setting_id).str();
        Http http = Http::put2(url);
        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .header("Content-Type", "application/json")
            .set_post_body(request_body)
            .on_complete(
                [this, result_ptr](std::string body, unsigned) {
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
                }
        ).perform_sync();

        return result;
    }

    int AccountManager::del_setting(std::string setting_id)
    {
        int result = -1;
        int* result_ptr = &result;
        std::string url = (boost::format("%1%/iot/slicer/setting/%2%") % host % setting_id).str();
        Http http = Http::del(url);
        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .on_complete(
                [this, result_ptr](std::string body, unsigned) {
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
                }
        ).perform_sync();
        return result;
    }

    void AccountManager::parse_setting(pt::ptree node, std::string type, std::string attr)
    {
        _parse_preset_internal(my_presets, node, type, attr);
    }

    void AccountManager::_parse_preset_internal(std::map<std::string, Preset*>& presets, pt::ptree node, std::string type, std::string attr)
    {
        boost::optional<std::string> setting_id = node.get_optional<std::string>("setting_id");
        boost::optional<std::string> version = node.get_optional<std::string>("version");
        boost::optional<std::string> name = node.get_optional<std::string>("name");

        if (setting_id.has_value()) {
            std::map<std::string, Preset*>::iterator it = presets.find(setting_id.value());
            if (it == presets.end()) {
                /* insert a new setting */
                Preset::Type curr_type = Preset::get_type_from_string(type);
                if (curr_type == Preset::Type::TYPE_INVALID) {
                    BOOST_LOG_TRIVIAL(info) << "type is invalid";
                    return;
                }
                Preset* preset = new Preset(curr_type, name.value(), false);
                preset->setting_id = setting_id.value();
                preset->is_system = attr.compare("public") == 0 ? true : false;
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

    std::string AccountManager::get_user_name()
    {
        if (m_curr_user) {
            return m_curr_user->m_account;
        }
        return "";
    }

    void AccountManager::set_host(std::string host_url)
    {
        BOOST_LOG_TRIVIAL(trace) << "set host to " << host_url;
        host = host_url;
    }

    std::string AccountManager::_get_query_url(std::string device_id)
    {
        if (m_curr_user) {
            return (boost::format("%1%/iot/user/bind?dev_id=%2%") % host % device_id).str();
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
            return (boost::format("%1%/iot/user/bind_list?dev_ids=%2%") % host % dev_id).str();
        }
        else {
            return "";
        }
    }


    std::string AccountManager::_get_bind_url(std::string device_id)
    {
        return (boost::format("%1%/iot/user/%2%/bind") % host % device_id).str();
    }

    std::string AccountManager::_get_login_url()
    {
        return (boost::format("%1%/user/login") % host).str();
    }

    std::string AccountManager::_get_user_profile_url(std::string account)
    {
        return (boost::format("%1%/my/profile") % host).str();
    }

    std::string AccountManager::_get_register_url()
    {
        return (boost::format("%1%/user/register") % host).str();
    }

    std::string AccountManager::_get_slicer_info_url()
    {
        return (boost::format("%1%/iot/slicer/resource") % host).str();
    }

    std::string AccountManager::_get_bind_list_url()
    {
        if (m_curr_user) {
            return (boost::format("%1%/iot/user/bind") % host).str();
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
        return (boost::format("%1%/iot/user/project") % host).str();
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
                else if (command_str.compare("request_model_download") == 0) {
                    if (root.get_child_optional("data") != boost::none) {
                        pt::ptree data_node = root.get_child("data");
                        boost::optional<std::string> model_id = data_node.get_optional<std::string>("model_id");
                        if (model_id.has_value()) {
                            this->request_model_download(model_id.value());
                        }
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

    void AccountManager::request_model_download(std::string model_id)
    {
        int result = 0;
        BBLProject* project = new BBLProject();
        project->project_model_id = model_id;
        result = request_project_id(project,
            [this, project](int result, std::string info) {
                GUI::wxGetApp().download_project(project->project_id);
            }
        );
    }

    void AccountManager::request_project_download(std::string project_id)
    {
        GUI::wxGetApp().download_project(project_id);
    }

    void AccountManager::request_open_project(std::string project_id)
    {
        ;
    }

} // namespace Slic3r
