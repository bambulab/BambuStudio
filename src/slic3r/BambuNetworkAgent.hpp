#ifndef __BAMBU_NETWORK_AGENT_HPP__
#define __BAMBU_NETWORK_AGENT_HPP__

#include "BambuNetworkDefine.hpp"

namespace BBL {

class BambuNetworkAgent
{

public:
    BambuNetworkAgent();
    ~BambuNetworkAgent();

    void init_log();
    void set_config_dir(std::string dir);
    void set_cert_file(std::string folder, std::string filename);
    void set_country_code(std::string country_code);
    void load_config();

    void set_on_user_login_fn(OnUserLoginFn fn);
    void set_on_printer_connected_fn(OnPrinterConnectedFn fn);
    void set_on_server_connected_fn(OnServerConnectedFn fn);
    void set_on_http_error_fn(OnHttpErrorFn fn);
    void set_get_country_code_fn(GetCountryCodeFn fn);
    void set_on_message_fn(OnMessageFn fn);
    void set_on_local_connect_fn(OnLocalConnectedFn fn);
    void set_on_local_message_fn(OnMessageFn fn);
    void set_on_ssdp_msg_fn(OnMsgArrivedFn fn);

    // connect
    int connect_server();
    bool is_server_connected();
    void check_server_connection();
    void start_subscribe(std::string module);
    void stop_subscribe(std::string module);
    int send_message(std::string dev_id, std::string json_str, int qos);

    int connect_printer(std::string dev_id, std::string dev_ip, std::string username = "", std::string password = "");
    int disconnect_printer();
    int send_message_to_printer(std::string dev_id, std::string json_str, int qos);

    // ssdp
    bool start_discovery(bool start, bool sending);

    // user
    void change_user(std::string user_info);
    bool is_user_login();
    void user_logout();
    std::string user_id();
    std::string user_name();
    std::string user_avatar();
    std::string user_nickanme();
    std::string build_login_cmd();
    std::string build_logout_cmd();
    std::string build_login_info();
    int start_bind(std::string dev_ip, OnUpdateStatusFn update_fn);
    int start_unbind(std::string dev_id);
    std::string get_bambulab_host();

    std::string user_selected_machine();
    void set_user_selected_machine(std::string dev_id);

    // print
    int start_print(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn);
    int start_local_print_with_record(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn);
    int start_local_print(PrintParams, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn);

    // presets
    std::map<std::string, std::map<std::string, std::string>>& get_user_presets();

    std::string request_setting_id(std::string name, std::map<std::string, std::string>& values_map, unsigned int& http_code);
    int put_setting(std::string setting_id, std::string name, std::map<std::string, std::string>& values_map, unsigned int& http_code);
    int get_setting_list(std::string bundle_version);
    std::vector<std::string>& get_delete_cache_presets();
    void delete_preset(std::string setting_id);
    int del_setting(std::string setting_id);

    // iot
    std::string get_studio_info_url();
    void set_extra_http_header(std::map<std::string, std::string> extra_headers);

    void check_user_task_report(int &task_id, bool &printable);
    int get_user_print_info(unsigned int &http_code, std::string &http_body);
    int get_printer_firmware(std::string dev_id, unsigned &http_code, std::string &http_body);
    void get_task_plate_index(std::string task_id, int &plate_index);
    void get_slice_info(std::string project_id, std::string profile_id, int plate_index, std::string &slice_json);
    int query_bind_status(std::vector<std::string> query_list, unsigned int &http_code, std::string &http_body);
    int modify_printer_name(std::string dev_id, std::string dev_name);

    // camera
    void get_camera_url(std::string dev_id, std::function<void(std::string)> callback);

private:

    void*                   context { nullptr };
    void*                   ssdp{ nullptr };
};

}

#endif
