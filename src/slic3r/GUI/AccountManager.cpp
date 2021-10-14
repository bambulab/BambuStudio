#include "libslic3r/libslic3r.h"
#include "AccountManager.hpp"
#include "DeviceManager.hpp"
#include "libslic3r/Time.hpp"
#include "libslic3r/Thread.hpp"
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

namespace pt = boost::property_tree;

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
        if (manager) {
            std::map<std::string, MachineObject*>::iterator it;
            for (it = manager->myBindMachineList.begin(); it != manager->myBindMachineList.end(); it++) {
                manager->add_subscribe(it->second);
            }
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
        mqtt_uuid_bytes(4)
    {
        mqtt_opt.set_user_name(mqtt_user);
        mqtt_opt.set_password(mqtt_pwd);
        mqtt_opt.set_max_inflight(500);
        mqtt_opt.set_connect_timeout(10);
        mqtt_opt.set_automatic_reconnect(3, 10);

        m_curr_user = nullptr;
        boost::filesystem::fstream fstream();
    }

    int AccountManager::load_user_info()
    {
        m_curr_user = AccountInfo::load_from_json(account_json);
        if (this->is_user_login()) {
            this->connect_mqtt();
            this->request_bind_list();
        }
        return 0;
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
            mqtt_cli->disconnect()->wait();
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
            boost::optional<std::string> ack_status = root.get_optional<std::string>("message");
            boost::optional<std::string> user_id = root.get_optional<std::string>("user_id");
            boost::optional<std::string> token = root.get_optional<std::string>("token");
            if (ack_status.has_value()) {
                if (ack_status.value().compare(MSG_SUCCESS) == 0) {
                    BOOST_LOG_TRIVIAL(trace) << "User = " << account << " Login Success!";
                    if (user_id.has_value() && token.has_value()) {
                        if (m_curr_user) delete m_curr_user;
                        m_curr_user = new AccountInfo(account, user_id.value(), AccountInfo::LoginStatus::STATUS_LOGIN);
                        m_curr_user->set_token(token.value());
                        save_user_info();
                        /* connect mqtt */
                        this->connect_mqtt();
                        this->request_bind_list();
                        if (fn) {
                            fn(0, "");
                        }
                        return;
                    }
                }
            }

            BOOST_LOG_TRIVIAL(trace) << "Account = " << account << " Login Failed! error = " << body;
            if (fn) {
                fn(-1, body);
            }
                }).on_error([&, this, account, fn](std::string body, std::string error, unsigned status) {
                    BOOST_LOG_TRIVIAL(trace) << "Account=" << account << " Login Failed! status=" << status << ",error=" << error << ",body=" << body;
                    std::string error_tips = "Login Failed! status=" + std::to_string(status) + ", error=" + error + ", body=" + body;
                    if (fn) {
                        fn(-1, error);
                    }
                    this->_handle_error_code(status, error, body);
                    }).perform();

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
                        Slic3r::GUI::wxGetApp().show_message_box("Register ok!");
                    }
                    else {
                        BOOST_LOG_TRIVIAL(trace) << "Account = " << account << " Register Failed! error = " << body;
                        Slic3r::GUI::wxGetApp().show_message_box("Register failed! msg=" + body);
                    }
                }
                else {
                    BOOST_LOG_TRIVIAL(trace) << "Account = " << account << " Register Failed! error = " << body;
                }
                    }).on_error([&, account](std::string body, std::string error, unsigned status) {
                        BOOST_LOG_TRIVIAL(trace) << "Account = " << account << " Register Failed! error = " << body;
                        Slic3r::GUI::wxGetApp().show_message_box("Register failed! msg=" + body);
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

    void AccountManager::update_my_bind_list(std::string body)
    {
        std::lock_guard<std::mutex> lock(listMutex);
        myBindMachineList.clear();
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

                MachineObject* obj = new MachineObject(*this, dev_name, dev_id, "");
                obj->is_online = online.compare("true") == 0 ? true : false;
                obj->set_bind_status(this->get_user_name());
                myBindMachineList.insert(std::make_pair(dev_id, obj));

                /* insert a new machine event */
                this->add_subscribe(obj);
            }
        }
        catch (std::exception& e) {
            ;
        }
    }


    int AccountManager::query_bind_status(std::vector<std::string> device_list, CompletedFn fn, ErrorFn errFn)
    {
        Http http = Http::get(_get_qeury_bind_list_url(device_list));
        try {
            http.header("accept", "application/json")
                .header("Authorization", get_token_str())
                .header("Content-Type", "application/json")
                .on_complete([&, device_list, fn](std::string body, unsigned) {
                /* eg: {"message": "free, user1, user2, user3, self"} */
                std::stringstream ss(body);
                pt::ptree root;
                pt::read_json(ss, root);
                boost::optional<std::string> message = root.get_optional<std::string>("message");
                if (message.has_value()) {
                    if (fn) {
                        fn(message.value());
                    }
                }
                    }).on_error([&, device_list, errFn](std::string body, std::string error, unsigned status) {
                        BOOST_LOG_TRIVIAL(trace) << "Query bind device list Error! error = " << body;
                        if (errFn) {
                            errFn(status, error, body);
                        }
                        void _handle_error_code(int status, std::string error, std::string body);
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

    int AccountManager::request_bind_list(ResultFn fn)
    {
        if (!is_user_login()) {
            if (fn) {
                fn(-1, "User is not login");
            }
            return -1;
        }

        Http http = Http::get(_get_bind_list_url());
        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
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
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
        std::string project_name_str = project->project_name;

        pt::ptree root;
        root.put("name", project_name_str);
        /* optional model_id */
        /* root.put("model_id", model_id_str); */
        /* optional content */
        /* root.put("content", project_content); */

        std::stringstream oss;
        pt::write_json(oss, root, false);
        return oss.str();
    }

    std::string AccountManager::json_request_body_post_profile(BBLProfile* profile)
    {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
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
        root.put("name", task_name_str);
        root.put("content", task->build_content_json());
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
        int result = -1;
        if (!project) {
            if (resFn) {
                resFn(-1, "Invalid Project");
            }
            return result;
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

    int AccountManager::upload_3mf(BBLProfile* profile, ResultFn resFn, ProgressFn proFn, bool sync)
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
            .on_progress(
                [this, proFn](Http::Progress progress, bool& cancel) {
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
        if (!task->parent_task_) return -1;
        if (!task->parent_task_->profile_) return -1;
        BBLProfile* profile = task->parent_task_->profile_;

        std::string ticket = (boost::format("%1%_%2%") % task->parent_task_->task_id % task->task_id).str();
        std::string gather = json_request_poll_3mf_gather(task);
        gather.erase(std::remove(gather.begin(), gather.end(), '\\'), gather.end());
        gather = Http::url_encode(gather);
        std::string query_params = (boost::format("?profile_id=%1%&&ticket=%2%&&gather=%3%") % profile->profile_id % ticket % gather).str();
        std::string url = (boost::format("%1%/iot/user/project/%2%%3%") % host % profile->project_id % query_params).str();
        
        int retry_ = 0;
        int retry_max = 5;
        
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

        while (task->task_url.empty() && retry_ < retry_max) {
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

    int AccountManager::poll_3mf(BBLProfile* profile)
    {
        if (!profile) return -1;

        std::string ticket = profile->profile_id;
        std::string query_params = (boost::format("?profile_id=%1%&&ticket=%2%") % profile->profile_id % ticket).str();

        std::string url = (boost::format("%1%/iot/user/project/%2%%3%") % host % profile->project_id % query_params).str();

        BBLProject* project = profile->project_;

        int retry_ = 0;
        int retry_max = 5;
        
        Http http = Http::get(url);
        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .on_complete(
                [this, project](std::string body, unsigned) {
                    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
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

    BBLTask* AccountManager::get_task(std::string task_id)
    {
        BBLTask* task = new BBLTask();
        task->task_id = task_id;
        std::string url = (boost::format("%1%/iot/user/task/%2%") % host % task_id).str();
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
                            if (root.get_child_optional("sub_task") != boost::none) {
                                pt::ptree subtask_ids = root.get_child("sub_task");
                                for (auto sub_id = subtask_ids.begin(); sub_id != subtask_ids.end(); ++sub_id) {
                                    std::string subtask_id = sub_id->first;
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
        .perform_sync();
        return task;
    }

    BBLSubTask* AccountManager::get_subtask(std::string subtask_id)
    {
        BBLSubTask* task = new BBLSubTask();
        task->task_id = subtask_id;
        std::string url = (boost::format("%1%/iot/user/task/%2%") % host % subtask_id).str();
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
                            boost::optional<std::string> status = root.get_optional<std::string>("status");
                            if (status.has_value())
                                task->task_status = BBLSubTask::parse_status(status.value());
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
                            if (parent_id.has_value())
                                task->parent_id = parent_id.value();
                        }
                    }
                }
            )
            .on_error(
                [this](std::string body, std::string error, unsigned status) {
                    BOOST_LOG_TRIVIAL(info) << "get_task info failed! body=" << body;
                }
            )
        .perform_sync();
        return task;
    }

    void AccountManager::get_project_info(BBLProject* project)
    {
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

    void AccountManager::get_profile_info(BBLProject* project, BBLProfile* profile)
    {
        if (!profile || !project) return;

        std::string query_params = (boost::format("?profile_id=%1%&gather=all") % profile->profile_id).str();

        std::string url = (boost::format("%1%/iot/user/project/%2%?%3%") % host % project->project_id % query_params).str();

        int retry_ = 0;
        int retry_max = 10;
        Http http = Http::get(url);
        while (project->project_url.empty() && retry_ < retry_max) {
            http.header("accept", "application/json")
                .header("accept", "application/json")
                .header("Authorization", get_token_str())
                .on_complete(
                    [this, project](std::string body, unsigned) {
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
                            }
                        }
                    }
                )
                .on_error(
                    [this](std::string body, std::string error, unsigned status) {
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

        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
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
            }).perform();
    }

    std::string AccountManager::get_token_str()
    {
        if (m_curr_user) {
            return (boost::format("Bearer %1%") % m_curr_user->m_token).str();
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

    std::string AccountManager::_get_register_url()
    {
        return (boost::format("%1%/user/register") % host).str();
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
            wxMessageBox("Token is invalid! Please login again!");
            break;
        default:
            return;
        }
    }

} // namespace Slic3r
