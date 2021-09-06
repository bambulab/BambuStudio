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


    /* mqtt cloud connection callbacks */
    void cloud_conn_callback::reconnect()
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2500));
        try {
            BOOST_LOG_TRIVIAL(trace) << "cloud_conn_callback::reconnect()  connecting...";
            cli_.connect(connOpts_, context_, *this);
        }
        catch (const mqtt::exception& exc) {
            BOOST_LOG_TRIVIAL(trace) << "cloud_conn_callback::reconnect() exception:" << exc.get_message();
        }
        catch (std::exception& e) {
            BOOST_LOG_TRIVIAL(trace) << "cloud_conn_callback::reconnect() exception:" << e.what();
        }
    }

    void cloud_conn_callback::connected(const std::string& cause)
    {
        BOOST_LOG_TRIVIAL(trace) << "cloud_conn_callback::connected!";
        /* subscribe binded device */
        /* TODO subscribe cloud online device topics */
        /* subscribe device reqeust and report */
        /* subscribe connected */
        /* update sub_topics */
        BOOST_LOG_TRIVIAL(trace) << "client_conn_callback::connected!";
        /* subscribe current device reqeust and report */
        try {
            for (int i = 0; i < sub_topics.size(); i++) {
                action_listener* sub_listener = new action_listener("Subscriber_" + sub_topics[i]);
                cli_.subscribe(sub_topics[i], 0, nullptr, *sub_listener);
            }
        }
        catch (mqtt::exception& e) {
            BOOST_LOG_TRIVIAL(trace) << "cloud_conn_callback::connected, exception=" << e.what();
        }
    }

    void cloud_conn_callback::on_failure(const mqtt::token& tok)
    {
        BOOST_LOG_TRIVIAL(trace) << "cloud_conn_callback::on_failure, Connection(mqtt) failed! retry=" << nretry_;
        /* TODO mqtt connect failed tips */
        ++nretry_;
        reconnect();
    }

    void cloud_conn_callback::on_success(const mqtt::token& tok)
    {
        BOOST_LOG_TRIVIAL(trace) << "cloud_conn_callback::on_success, Connection(mqtt) OK!";
        /* mqtt connect on success tips, same as connected */
    }

    void cloud_conn_callback::connection_lost(const std::string& cause) {
        BOOST_LOG_TRIVIAL(trace) << "cloud_conn_callback::connection_lost!, cause =" << cause;
        ++nretry_;
        reconnect();
    }

    void cloud_conn_callback::message_arrived(mqtt::const_message_ptr msg)
    {
        AccountInfo* info = (AccountInfo*)context_;
        if (info) {
            info->msg_recv_fn(msg->get_topic(), msg->get_payload_str());
        }
    }

    AccountInfo::AccountInfo(std::string account, std::string user_id, AccountInfo::LoginStatus status)
        :mqtt_opt(mqtt::connect_options_builder().clean_session().finalize()),
        mqtt_cli(nullptr),
        mqtt_cb(nullptr),
        mqtt_uuid_bytes(4)
    {
        mqtt_opt.set_user_name(mqtt_user);
        mqtt_opt.set_password(mqtt_pwd);

        boost::uuids::uuid uuid = boost::uuids::random_generator()();
        mqtt_uuid = to_string(uuid).substr(0, mqtt_uuid_bytes);

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
                /* connect mqtt server */
                info->connect_mqtt();
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


    int AccountInfo::connect_mqtt()
    {
        try {
            std::string client_id = (boost::format("%1%:%2%") % m_user_id % mqtt_uuid).str();
            mqtt_cli = new mqtt::async_client(MQTT_HOST, client_id);
            mqtt_cb = new cloud_conn_callback(*mqtt_cli, mqtt_opt, this);
            /* TODO add topics */
            mqtt_cli->set_callback(*mqtt_cb);
            mqtt_cli->connect(mqtt_opt, this, *mqtt_cb);
            return 0;
        }
        catch (mqtt::exception& e) {
            return -1;
        }
        catch (...) {
            return -1;
        }
        return 0;
    }

    int AccountInfo::disconnect_mqtt()
    {
        if (mqtt_cli) {
            mqtt_cli->disconnect()->wait();
        }
        return 0;
    }

    void AccountInfo::add_topics(std::string topic)
    {
        /*try {
            action_listener* sub_listener = new action_listener("Subscriber_" + topic);
            mqtt_cli->subscribe(topic, 0, nullptr, *sub_listener);
        }
        catch (...) {

        }*/
    }

    void AccountInfo::remove_topics(std::string topic)
    {
        mqtt_cli->unsubscribe(topic);
    }


    AccountManager::AccountManager()
    {
        //host = "http://iot.dev.bbl";
        //host = "192.168.0.146";
        host = "http://api.qa.bbl";
        m_curr_user = NULL;
        boost::filesystem::fstream fstream();
    }

    int AccountManager::load_user_info()
    {
        m_curr_user = AccountInfo::load_from_json(account_json);
        return 0;
    }

    int AccountManager::save_user_info()
    {
        if (m_curr_user) {
            return m_curr_user->save_to_json(account_json);
        }
        return 0;
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
                    if (fn) {
                        fn(0, "");
                    }
                    if (user_id.has_value() && token.has_value()) {
                        if (m_curr_user) delete m_curr_user;
                        m_curr_user = new AccountInfo(account, user_id.value(), AccountInfo::LoginStatus::STATUS_LOGIN);
                        m_curr_user->set_token(token.value());
                        save_user_info();
                        /* connect mqtt */
                        m_curr_user->connect_mqtt();
                        this->request_bind_list(user_id.value());
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
            save_user_info();
        }
        return 0;
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
        pt::write_json(oss, root);
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
        pt::write_json(oss, root);
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
                    } else {
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
        std::string url = (boost::format("%1%/iot/device/%2%/report") % "iot.qa.bbl" % device_id).str();
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

    int AccountManager::request_bind_list(std::string user_id)
    {
        if (!is_user_login()) return -1;

        Http http = Http::get(_get_bind_list_url());
        http.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .on_complete([&](std::string body, unsigned) {
            try {
                std::stringstream ss(body);
                pt::ptree root;
                pt::read_json(ss, root);
                boost::optional<std::string> message = root.get_optional<std::string>("message");
                DeviceManager* manager = Slic3r::GUI::wxGetApp().getDeviceManager();
                if (message.has_value()) {
                    if (message.value().compare(MSG_SUCCESS) == 0) {
                        pt::ptree bind_list = root.get_child("devices");
                        for (BOOST_AUTO(pos, bind_list.begin()); pos != bind_list.end(); ++pos)
                        {
                            std::string dev_id = pos->second.get_value<std::string>("dev_id");
                            std::string dev_name = pos->second.get_value<std::string>("name");
                            std::string online = pos->second.get_value<std::string>("online");
                            //TODO add new machine object
                            /*DeviceInfo* info = new DeviceInfo(dev_id, dev_name, MQTT_CONNECTION);
                            info->set_mqtt_conn_status(online.compare("online") == 0);
                            manager->add_new_device(info);
                            */
                        }
                    }
                    else if (message.value().compare("nodev") == 0) {
                        BOOST_LOG_TRIVIAL(trace) << "Get bind list failed! body=" << body;
                    }
                    else {
                        BOOST_LOG_TRIVIAL(trace) << "Get bind list failed! body=" << body;
                    }
                }
                else {
                    BOOST_LOG_TRIVIAL(trace) << "Get bind list failed! body=" << body;
                }
            }
            catch (std::exception& e) {
                BOOST_LOG_TRIVIAL(trace) << "request_bind_list, on_complete exception" << std::string(e.what());
            }
                })
            .on_error([&](std::string body, std::string error, unsigned status) {
                BOOST_LOG_TRIVIAL(trace) << "Get bind list failed!, status=" << status << ", body=" << body << ", error=" << error;
                //TODO
                //Slic3r::GUI::wxGetApp().show_message_box("Get bind list failed");
            })
            .perform();
        return 0;
    }

    std::string AccountManager::json_request_body_post_project(BBLProject* project)
    {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
        std::string project_name_str = converter.to_bytes(project->project_name);

        pt::ptree root;
        root.put("name", project_name_str);
        std::stringstream oss;
        pt::write_json(oss, root);
        return oss.str();
    }

    std::string AccountManager::json_request_body_post_profile(BBLProfile* profile)
    {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
        std::string profile_name_str = converter.to_bytes(profile->profile_name);
        pt::ptree root;
        root.put("name", profile_name_str);
        root.put("content", profile->profile_content);
        std::stringstream oss;
        pt::write_json(oss, root);
        return oss.str();
    }

    BBLProject* AccountManager::create_project(BBLProject::ProjectType type, std::wstring file, ResultFn resFn, ProgressFn proFn)
    {
        BBLProject* project = new BBLProject(file, type);
        
        std::string json_str = json_request_body_post_project(project);

        /* get a project id and model id */
        Http http_post = Http::post(_get_project_url());
        http_post.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .header("Content-Type", "application/json")
            .set_post_body(json_str)
            .on_complete(
                [this, project, resFn](std::string body, unsigned) {
                    try {
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
                                        resFn(0, "get project id ok!");
                                    }
                                }    
                            }
                            else {
                                BOOST_LOG_TRIVIAL(trace) << "AccountManager::create_project on_complete, body=" << body;
                            }
                        }
                    }
                    catch (std::exception& e) {
                        BOOST_LOG_TRIVIAL(trace) << (boost::format("AccountManager::create_project on_complete parsing failed, exception=%1% body=%2%")
                            % e.what()
                            % body).str();
                        if (resFn) {
                            resFn(-1, "get project id failed!");
                        }
                    }
                    catch (...) {
                        BOOST_LOG_TRIVIAL(trace) << "AccountManager::create_project on_complete parsing failed, body=" << body;
                        if (resFn) {
                            resFn(-1, "create project id failed!");
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
                        resFn(-1, "create project id failed!");
                    }
                }
            )
            .perform_sync();

        if (!project) {
            delete project;
            project = nullptr;
            return nullptr;
        }

        /* init project name and path */
        project->project_name = file;
        project->project_path = fs::path(file.c_str());


        /* use default profile in 3mf, create a profile */
        BBLProfile* profile = new BBLProfile();
        profile->profile_name = L"default_profile";    // get profile name from 3mf
        project->profiles.push_back(profile);

        std::string project_url = (boost::format("%1%/iot/user/project/%2%") % host % project->project_id).str();

        std::vector<std::string> params;
        std::string request_str = json_request_body_post_profile(profile);

        Http http_create_profile = Http::post(project_url);
        http_create_profile.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .header("Content-Type","application/json")
            .set_post_body(request_str)
            .on_complete(
                [this, profile, resFn](std::string body, unsigned) {
                    std::stringstream ss(body);
                    pt::ptree root;
                    pt::read_json(ss, root);
                    boost::optional<std::string> message = root.get_optional<std::string>("message");
                    boost::optional<std::string> profile_id = root.get_optional<std::string>("profile_id");
                    if (message.has_value()) {
                        if (message.value().compare(MSG_SUCCESS) == 0) {
                            BOOST_LOG_TRIVIAL(info) << "create_profile ok!";
                            if (profile_id.has_value()) {
                                profile->profile_id = profile_id.value();
                            }
                        }
                    }
                }
            )
            .on_error(
                [this, profile, resFn](std::string body, std::string error, unsigned status) {
                    BOOST_LOG_TRIVIAL(info) << "create_profile failed! body=" << body << ", status=" << status;
                    if (resFn) {
                        resFn(-2, "create profile id failed!");
                    }
                }
            )
            .perform_sync();

        if (profile->profile_id.empty()) {
            return project;
        }

        std::string file_str("file");
        std::string profile_id_str("profile_id");

        /* upload 3mf or gcode to cloud */
        Http http_put = Http::put2(project_url);   
        http_put.header("accept", "application/json")
            .header("Authorization", get_token_str())
            .header("Content-Type", "multipart/form-data")
            .mime_form_add_file(file_str, project->project_path)
            .mime_form_add_text(profile_id_str, profile->profile_id)
            .on_complete(
                [this, project, resFn](std::string body, unsigned) {
                    std::stringstream ss(body);
                    pt::ptree root;
                    pt::read_json(ss, root);
                    boost::optional<std::string> message = root.get_optional<std::string>("message");
                    if (message.has_value()) {
                        if (message.value().compare(MSG_SUCCESS) == 0) {
                            BOOST_LOG_TRIVIAL(info) << "create_project, upload project ok!";
                            if (resFn) {
                                resFn(0, "upload project ok!");
                            }
                        }
                    }
                }
            )
            .on_error(
                [this, project, resFn](std::string body, std::string error, unsigned status) {
                    BOOST_LOG_TRIVIAL(info) << "create_project, upload project failed!";
                    if (resFn) {
                        resFn(-1, "upload project failed!");
                    }
                }
            ).perform_sync();
        
        if (project) {
            default_project = project;
        }
        return project;
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
                            boost::optional<std::string> model_id = root.get_optional<std::string>("model_id");
                            boost::optional<std::string> name = root.get_optional<std::string>("name");
                            boost::optional<std::string> create_time = root.get_optional<std::string>("create_time");
                            boost::optional<std::string> content = root.get_optional<std::string>("content");
                            boost::optional<std::string> profiles = root.get_optional<std::string>("profiles");
                            boost::optional<std::string> url = root.get_optional<std::string>("url");
                            boost::optional<std::string> md5 = root.get_optional<std::string>("md5");
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
                                    project->project_name = converter.from_bytes(name.value());
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

    void AccountManager::create_task(BBLProject* project, BBLTask* task, ResultFn resFn) {
        std::string task_url = (boost::format("%1%/iot/user/project/%1%/task") % host % project->project_id).str();

        std::string json_str = json_request_body_post_project(project);
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
            return (boost::format("%1%/iot/device/bind_list?dev_ids=%2%") % host % dev_id).str();
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
        pt::write_json(oss, root);
        return oss.str();
    }

    std::string AccountManager::_get_device_json(std::string device_id)
    {
        pt::ptree root;
        root.put("dev_id", device_id);
        std::stringstream oss;
        pt::write_json(oss, root);
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
        pt::write_json(oss, root);
        return oss.str();
    }

    std::string AccountManager::_get_register_request(std::string account, std::string password)
    {
        pt::ptree root;
        root.put("account", account);
        root.put("password", password);
        std::stringstream oss;
        pt::write_json(oss, root);
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
