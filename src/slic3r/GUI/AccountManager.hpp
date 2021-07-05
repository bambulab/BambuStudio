#ifndef slic3r_AccountManager_hpp_
#define slic3r_AccountManager_hpp_

#include <map>
#include <vector>
#include <string>
#include <memory>
#include <boost/thread.hpp>

namespace Slic3r {

class AccountInfo {
public:
    AccountInfo(std::string account, std::string user_id);

    std::string user_id() { return m_user_id; }
    void set_token(std::string token) { m_token = token; }
    std::string get_token() { return m_token; }
private:
    std::string m_account;
    std::string m_password;
    std::string m_user_id;
    std::string m_token;
};


class AccountManager
{
private:
    AccountInfo* m_curr_user;
    //std::string host = "http://192.168.0.10:9000";
    std::string host = "http://iot.qa.bbl";

    std::string _get_query_url();
    std::string _get_bind_url();
    std::string _get_unbind_url();
    std::string _get_login_url();
    std::string _get_register_url();
    std::string _get_bind_list_url();
    std::string _get_bind_list_request();
    std::string _get_device_json(std::string device_id);
    std::string _get_query_bind_request(std::string device_id);
    std::string _get_bind_request(std::string device_id);
    std::string _get_unbind_request(std::string device_id);
    std::string _get_login_request(std::string account, std::string password);
    std::string _get_register_request(std::string account, std::string password);

public:
    AccountManager();
    ~AccountManager() {}

    bool is_user_login();
    int user_login(std::string account, std::string password);
    int user_logout(std::string account);
    int user_register(std::string account, std::string passoword);
    int user_get_info();
    int query_bind_status(std::string device_id);
    int request_bind(std::string device_id);
    int request_unbind(std::string device_id);
    int request_bind_list(std::string user_id);
    void set_host(std::string host_url);
};

} // namespace Slic3r

#endif //  slic3r_DeviceManager_hpp_
