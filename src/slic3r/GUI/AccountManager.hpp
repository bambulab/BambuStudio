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
    enum LoginStatus
    {
        STATUS_LOGIN,
        STATUS_LOGOUT,
    };

    AccountInfo(std::string account, std::string user_id);

    std::string user_id() { return m_user_id; }
    void set_token(std::string token) { m_token = token; }
    std::string get_token() { return m_token; }
    LoginStatus login_status() { return m_login_status; }

    template<typename Archive>
    void serialize(Archive& ar, const unsigned int version)
    {
        //ar(m_account, m_user_id, m_token, m_login_status);
        ar &m_account;
        ar &m_user_id;
        ar &m_token;
        ar &m_login_status;
        /*ar(m_account);
        ar(m_user_id);
        ar(m_token);
        ar(m_login_status);*/
    }

private:
    friend class boost::serialization::access;
    std::string m_account;
    std::string m_password;
    std::string m_user_id;
    std::string m_token;
    LoginStatus m_login_status;
};


class AccountManager
{
private:
    AccountInfo* m_curr_user;
    boost::filesystem::path m_user_info_path;
    std::string host = "http://iot.dev.bbl";
    //std::string host = "http://iot.qa.bbl";
    //std::string host = "192.168.0.146";

    std::string _get_query_url(std::string device_id);
    std::string _get_bind_url();
    std::string _get_unbind_url();
    std::string _get_login_url();
    std::string _get_register_url();
    std::string _get_bind_list_url();
    std::string _get_bind_list_request();
    std::string _get_device_json(std::string device_id);
    std::string _get_query_bind_request(std::string device_id);
    std::string _get_qeury_bind_list_url(std::vector<std::string> device_id_list);
    
    std::string _get_bind_request(std::string device_id);
    std::string _get_unbind_request(std::string device_id);
    std::string _get_login_request(std::string account, std::string password);
    std::string _get_register_request(std::string account, std::string password);
    /* check valid of user or pwd */
    bool _check_valid(std::string user, std::string password);
    /* common error code handler */
    void _handle_error_code(int status, std::string error, std::string body);

public:
    
    typedef std::function<void(int retcode, std::string info)> LoginFn;

    AccountManager();
    ~AccountManager() {}

    // Check user last login status
    int load_user_info();
    int save_user_info();

    bool is_user_login();
    int user_login(std::string account, std::string password, LoginFn fn);
    int user_logout();
    int user_register(std::string account, std::string passoword);
    int user_get_info();
    int query_bind_status(std::string device_id);
    int query_bind_status(std::vector<std::string> device_list);
    int request_bind(std::string device_id);
    int request_unbind(std::string device_id);
    int request_bind_list(std::string user_id);
    void set_host(std::string host_url);
    void set_user_info_path(boost::filesystem::path path) { m_user_info_path = path; }
    std::string get_user_id() { return m_curr_user->user_id(); }
};

} // namespace Slic3r

#endif //  slic3r_DeviceManager_hpp_
