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

namespace pt = boost::property_tree;

namespace Slic3r {

    AccountInfo::AccountInfo(std::string account, std::string user_id)
    {
        m_account = account;
        m_user_id = user_id;
    }

    AccountManager::AccountManager()
    {
        m_curr_user = NULL;
    }

    bool AccountManager::is_user_login()
    {
        if (m_curr_user)
            return true;
        else
            return false;
    }

    int AccountManager::user_login(std::string account, std::string password)
    {
        Http http = Http::post(std::move(_get_login_url()));
        std::string json_str = _get_login_request(account, password);

        http.set_post_body(json_str)
            .on_complete([&, account](std::string body, unsigned) {
            std::stringstream ss(body);
            pt::ptree root;
            pt::read_json(ss, root);
            boost::optional<std::string> ack_status = root.get_optional<std::string>("message");
            boost::optional<std::string> user_id = root.get_optional<std::string>("user_id");
            if (ack_status.has_value()) {
                if (ack_status.value().compare("success") == 0) {
                    BOOST_LOG_TRIVIAL(trace) << "User = " << account << " Login Success!";
                    if (user_id.has_value()) {
                        m_curr_user = new AccountInfo(account, user_id.value());

                        Slic3r::CommuBackend* backend = Slic3r::GUI::wxGetApp().getCommuBackend();
                        DeviceManager* manager = Slic3r::GUI::wxGetApp().getDeviceManager();
                        backend->connect_mqtt_server(user_id.value());
                        return;
                    }
                }
            }
            BOOST_LOG_TRIVIAL(trace) << "Account = " << account << " Login Failed! error = " << body;
            }).on_error([&, account](std::string body, std::string error, unsigned status) {
                BOOST_LOG_TRIVIAL(trace) << "Account = " << account << " Login Failed! error = " << body;
            }).perform();
        
        return 0;
    }

    int AccountManager::user_logout(std::string account)
    {
        if (m_curr_user) {
            delete m_curr_user;
            m_curr_user = NULL;
        }
        return 0;
    }

    int AccountManager::user_register(std::string account, std::string password)
    {
        Http http = Http::post(std::move(_get_register_url()));
        std::string json_str = _get_login_request(account, password);
        try {
            http.set_post_body(json_str)
                .on_complete([&, account](std::string body, unsigned) {
                std::stringstream ss(body);
                pt::ptree root;
                pt::read_json(ss, root);
                boost::optional<std::string> ack_status = root.get_optional<std::string>("message");
                if (ack_status.has_value()) {
                    if (ack_status.value().compare("success") == 0) {
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

    int AccountManager::user_get_info()
    {
        return 0;
    }

    int AccountManager::query_bind_status(std::string device_id)
    {
        Http http = Http::post(_get_query_url());
        std::string json_str = _get_query_bind_request(device_id);
        try {
            http.set_post_body(json_str)
                .on_complete([&, device_id](std::string body, unsigned) {
                std::stringstream ss(body);
                pt::ptree root;
                pt::read_json(ss, root);
                boost::optional<std::string> bind_status = root.get_optional<std::string>("message");
                if (bind_status.has_value()) {
                    Slic3r::DeviceManager* manager = Slic3r::GUI::wxGetApp().getDeviceManager();
                    manager->update_bind_status(device_id, std::string(bind_status.value()));
                    //TODO add mqtt subscriber.
                    if (this->is_user_login()) {
                        Slic3r::CommuBackend* backend = Slic3r::GUI::wxGetApp().getCommuBackend();
                        //backend->subscribe_device_topic(device_id);
                    }
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

    int AccountManager::request_bind(std::string device_id)
    {
        Http http = Http::post(std::move(_get_bind_url()));
        std::string json_str = _get_query_bind_request(device_id);
        http.set_post_body(json_str)
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
                }
                else if (bind_status.value().compare("conflict") == 0) {
                    BOOST_LOG_TRIVIAL(trace) << "Bind Device " << device_id << "  Conflict!";
                }
            }
            else {
                BOOST_LOG_TRIVIAL(trace) << "Bind Device " << device_id << "  Failed! error=" << body;
            }
        }).on_error([&, device_id](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(trace) << "Bind Device " << device_id << " Failed!";
        }).perform();
        return 0;
    }

    int AccountManager::request_unbind(std::string device_id)
    {
        Http http = Http::post(_get_unbind_url());
        std::string json_str = _get_unbind_request(device_id);
        http.set_post_body(json_str)
        .on_complete([&, device_id](std::string body, unsigned) {
            std::stringstream ss(body);
            pt::ptree root;
            pt::read_json(ss, root);
            boost::optional<std::string> bind_status = root.get_optional<std::string>("message");
            boost::optional<std::string> user_ca = root.get_optional<std::string>("user_ca");
            boost::optional<std::string> devs_ca = root.get_optional<std::string>("devs_ca");
            if (bind_status.has_value()) {
                if (bind_status.value().compare("FREE") == 0) {
                    BOOST_LOG_TRIVIAL(trace) << "Unind Device " << device_id << " OK!";
                }
                else {
                    BOOST_LOG_TRIVIAL(trace) << "Unind Device " << device_id << " Failed! status = " << bind_status.value();
                }
            }
            
        }).on_error([&, device_id](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(trace) << "Unbind Device " << device_id << " Failed!";
            ;
        }).perform();
        return 0;
    }

    std::string AccountManager::_get_query_url()
    {
        if (m_curr_user) {
            return (boost::format("%1%/api/iot/user/%2%/bind_status") % host % m_curr_user->user_id()).str();
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
            return (boost::format("%1%/api/iot/user/%2%/unbind") % host % m_curr_user->user_id()).str();
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

} // namespace Slic3r