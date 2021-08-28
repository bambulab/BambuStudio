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
#include <boost/foreach.hpp>
#include <boost/typeof/typeof.hpp>

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
            pt::ptree root;
            pt::read_json(filename, root);
            std::string account = root.get<std::string>("account");
            std::string token = root.get<std::string>("token");
            std::string user_id = root.get<std::string>("user_id");
            AccountInfo::LoginStatus status = (AccountInfo::LoginStatus)root.get<int>("login_status");
            AccountInfo* info = new AccountInfo(account, user_id, status);
            info->set_token(token);
            return info;
        }
        catch (std::exception& e)
        {
            BOOST_LOG_TRIVIAL(error) << "AccountManager::load_from_json() failed! exception=" << e.what();
            return nullptr;
        }
    }

    AccountManager::AccountManager()
    {
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

        http.set_post_body(json_str)
            .set_header("")
            .on_complete([&, this, account, fn](std::string body, unsigned) {
            std::stringstream ss(body);
            pt::ptree root;
            pt::read_json(ss, root);
            boost::optional<std::string> ack_status = root.get_optional<std::string>("message");
            boost::optional<std::string> user_id = root.get_optional<std::string>("user_id");
            boost::optional<std::string> token = root.get_optional<std::string>("token");
            if (ack_status.has_value()) {
                if (ack_status.value().compare("success") == 0) {
                    BOOST_LOG_TRIVIAL(trace) << "User = " << account << " Login Success!";
                    if (fn) {
                        fn(0, "");
                    }
                    if (user_id.has_value() && token.has_value()) {
                        if (m_curr_user) delete m_curr_user;
                        m_curr_user = new AccountInfo(account, user_id.value(), AccountInfo::LoginStatus::STATUS_LOGIN);
                        m_curr_user->set_token(token.value());
                        save_user_info();
                        this->request_bind_list(user_id.value());
                        return;
                    }
                }
            }

            BOOST_LOG_TRIVIAL(trace) << "Account = " << account << " Login Failed! error = " << body;
            if (fn) {
                fn(-1, body);
            }
            }).on_error([&, this, account](std::string body, std::string error, unsigned status) {
                BOOST_LOG_TRIVIAL(trace) << "Account=" << account << " Login Failed! status=" << status << ",error=" << error << ",body=" << body;
                std::string error_tips = "Login Failed! status=" + std::to_string(status) + ", error=" + error + ", body=" + body;
                if (fn) {
                    fn(status, error);
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
            http.set_post_body(json_str)
                .set_header(m_curr_user->get_token())
                .on_complete([&, account](std::string body, unsigned) {
                std::stringstream ss(body);
                pt::ptree root;
                pt::read_json(ss, root);
                boost::optional<std::string> ack_status = root.get_optional<std::string>("message");
                if (ack_status.has_value()) {
                    if (ack_status.value().compare("success") == 0) {
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

    AccountInfo* AccountManager::get_curr_user()
    {
        return m_curr_user;
    }

    int AccountManager::query_bind_status(std::string device_id)
    {
        Http http = Http::get(_get_query_url(device_id));
        std::string json_str = _get_query_bind_request(device_id);
        try {
             http.set_header(m_curr_user->get_token())
                .on_complete([&, device_id](std::string body, unsigned) {
                std::stringstream ss(body);
                pt::ptree root;
                pt::read_json(ss, root);
                boost::optional<std::string> bind_status = root.get_optional<std::string>("message");
                if (bind_status.has_value()) {
                    Slic3r::DeviceManager* manager = Slic3r::GUI::wxGetApp().getDeviceManager();
                    manager->update_bind_status(device_id, std::string(bind_status.value()));
                }
                }).on_error([&, device_id](std::string body, std::string error, unsigned status) {
                    BOOST_LOG_TRIVIAL(trace) << "Query bind device = " << device_id << " Error! error = " << body;
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
            http.set_header(m_curr_user->get_token())
                .on_complete([&, device_list, fn](std::string body, unsigned) {
                /* eg: {"message": "free, user1, user2, user3, self"} */
                std::stringstream ss(body);
                pt::ptree root;
                pt::read_json(ss, root);
                boost::optional<std::string> bind_status = root.get_optional<std::string>("message");
                if (bind_status.has_value()) {
                    Slic3r::DeviceManager* manager = Slic3r::GUI::wxGetApp().getDeviceManager();
                    std::vector<std::string> status_list;
                    split_string(bind_status.value(), status_list);
                    // update device bind status list
                    for (int i = 0; i < status_list.size() && i < device_list.size(); i++) {
                        manager->update_bind_status(device_list[i], status_list[i]);
                    }
                }
                if (fn) {
                    fn();
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

    int AccountManager::request_bind(std::string device_id, CompletedFn fn)
    {
        Http http = Http::put2(std::move(_get_bind_url()));
        std::string json_str = _get_query_bind_request(device_id);
        http.set_post_body(json_str)
            .set_header(m_curr_user->get_token())
            .on_complete([&, device_id](std::string body, unsigned) {
            std::stringstream ss(body);
            pt::ptree root;
            pt::read_json(ss, root);
            boost::optional<std::string> bind_status = root.get_optional<std::string>("message");
            boost::optional<std::string> user_ca = root.get_optional<std::string>("user_ca");
            boost::optional<std::string> devs_ca = root.get_optional<std::string>("devs_ca");
            if (bind_status.has_value()) {
                if (bind_status.value().compare("success") == 0) {
                    BOOST_LOG_TRIVIAL(trace) << "Bind Device " << device_id << " OK!";
                    Slic3r::GUI::wxGetApp().show_message_box("Bind device=" + device_id + " success!");
                }
                else if (bind_status.value().compare("conflict") == 0) {
                    BOOST_LOG_TRIVIAL(trace) << "Bind Device " << device_id << "  Conflict!";
                    Slic3r::GUI::wxGetApp().show_message_box("Bind device=" + device_id + " conflict!");
                }
                else {
                    BOOST_LOG_TRIVIAL(trace) << "Bind Device " << device_id << "  Failed! error=" << body;
                }
                //refresh bind status 
                /*if (fn) {
                    fn();
                }*/
            }
            else {
                BOOST_LOG_TRIVIAL(trace) << "Bind Device " << device_id << "  Failed! error=" << body;
                Slic3r::GUI::wxGetApp().show_message_box("Bind device=" + device_id + " failed! error=" + body);
            }
        }).on_error([&, device_id](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(trace) << "Bind Device " << device_id << " Failed!";
            Slic3r::GUI::wxGetApp().show_message_box("Bind device=" + device_id + " failed! error=" + body);
        }).perform();
        return 0;
    }

    int AccountManager::request_unbind(std::string device_id, CompletedFn fn)
    {
        Http http = Http::del(_get_unbind_url());
        std::string json_str = _get_unbind_request(device_id);
        http.set_post_body(json_str)
            .set_header(m_curr_user->get_token())
        .on_complete([&, device_id](std::string body, unsigned) {
            std::stringstream ss(body);
            pt::ptree root;
            pt::read_json(ss, root);
            boost::optional<std::string> bind_status = root.get_optional<std::string>("message");
            boost::optional<std::string> user_ca = root.get_optional<std::string>("user_ca");
            boost::optional<std::string> devs_ca = root.get_optional<std::string>("devs_ca");
            if (bind_status.has_value()) {
                if (bind_status.value().compare("success") == 0) {
                    BOOST_LOG_TRIVIAL(trace) << "Unind Device " << device_id << " OK!";
                    Slic3r::GUI::wxGetApp().show_message_box("Unbind device=" + device_id + " ok!");
                }
                else {
                    BOOST_LOG_TRIVIAL(trace) << "Unind Device " << device_id << " Failed! status = " << bind_status.value();
                    Slic3r::GUI::wxGetApp().show_message_box("Unbind device=" + device_id + " failed! error=" + body);
                }
            }
            
        }).on_error([&, device_id](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(trace) << "Unbind Device " << device_id << " Failed!";
            Slic3r::GUI::wxGetApp().show_message_box("Unbind device=" + device_id + " failed! error=" + body);
            ;
        }).perform();
        return 0;
    }

    int AccountManager::submit_print_result(std::string device_id, std::string json_str, ResultFn fn)
    {
        std::string url = (boost::format("%1%/api/device/%2%/report") % "iot.qa-1.bbl" % device_id).str();
        Http http = Http::post(url);
        http.set_post_body(json_str)
            .set_header(m_curr_user->get_token())
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
        Http http = Http::get(_get_bind_list_url());
        http.set_header(m_curr_user->get_token())
            .on_complete([&, user_id](std::string body, unsigned) {
            try {
                std::stringstream ss(body);
                pt::ptree root;
                pt::read_json(ss, root);
                boost::optional<std::string> ack_message = root.get_optional<std::string>("message");
                //std::vector<std::string> device_list;
                DeviceManager* manager = Slic3r::GUI::wxGetApp().getDeviceManager();
                if (ack_message.has_value()) {
                    if (ack_message.value().compare("success") == 0) {
                        pt::ptree bind_list = root.get_child("devices");
                        for (BOOST_AUTO(pos, bind_list.begin()); pos != bind_list.end(); ++pos)
                        {
                            std::string dev_id = pos->second.get_value<std::string>("dev_id");
                            std::string dev_name = pos->second.get_value<std::string>("name");
                            std::string online = pos->second.get_value<std::string>("online");
                            DeviceInfo* info = new DeviceInfo(dev_id, dev_name, MQTT_CONNECTION);
                            info->set_mqtt_conn_status(online.compare("online") == 0);
                            manager->add_new_device(info);
                        }
                    }
                    else if (ack_message.value().compare("nodev") == 0) {
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
            catch (std::exception &e) {
                BOOST_LOG_TRIVIAL(trace) << "request_bind_list, on_complete exception" << std::string(e.what());
            }
            }).on_error([&](std::string body, std::string error, unsigned status) {
                BOOST_LOG_TRIVIAL(trace) << "Get bind list failed!";
                Slic3r::GUI::wxGetApp().show_message_box("Get bind list failed");
                }).perform();
                return 0;
    }

    void AccountManager::set_host(std::string host_url)
    {
        BOOST_LOG_TRIVIAL(trace) << "set host to " << host_url;
        host = host_url;
    }

    std::string AccountManager::_get_query_url(std::string device_id)
    {
        if (m_curr_user) {
            return (boost::format("%1%/api/iot/user/%2%/bind?dev_id=%3%") % host % m_curr_user->user_id() % device_id).str();
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
                dev_id += "," +  *it;
            }
        }

        if (m_curr_user) {
            return (boost::format("%1%/api/iot/user/%2%/bind?dev_id=%3%") % host % m_curr_user->user_id() % dev_id).str();
        }
        else {
            return "";
        }
    }

    std::string AccountManager::_get_bind_url()
    {
        if (m_curr_user) {
            return (boost::format("%1%/api/iot/user/%2%/bind") % host % m_curr_user->user_id()).str();
        }
        else {
            return "";
        }
    }

    std::string AccountManager::_get_unbind_url()
    {
        if (m_curr_user) {
            return (boost::format("%1%/api/iot/user/%2%/bind") % host % m_curr_user->user_id()).str();
        }
        else {
            return "";
        }
    }

    std::string AccountManager::_get_login_url()
    {
        return (boost::format("%1%/api/account/login") % host).str();
    }

    std::string AccountManager::_get_register_url()
    {
        return (boost::format("%1%/api/iot/account/register") % host).str();
    }

    std::string AccountManager::_get_bind_list_url()
    {
        if (m_curr_user) {
            return (boost::format("%1%/api/iot/user/%2%/bind_list") % host % m_curr_user->user_id()).str();
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
