#ifndef __BAMBU_NETWORK_AGENT_CPP__
#define __BAMBU_NETWORK_AGENT_CPP__

#include "BambuNetworkAgent.hpp"
#include "GUI/AccountManager.hpp"
#include "GUI/SsdpDiscovery.hpp"

//BBS: iot preset type strings
#define IOT_PRINTER_TYPE_STRING     "printer"
#define IOT_FILAMENT_STRING         "filament"
#define IOT_PRINT_TYPE_STRING       "print"

#define IOT_JSON_KEY_VERSION            "version"
#define IOT_JSON_KEY_NAME               "name"
#define IOT_JSON_KEY_TYPE               "type"
#define IOT_JSON_KEY_UPDATE_TIME        "updated_time"
#define IOT_JSON_KEY_BASE_ID            "base_id"
#define IOT_JSON_KEY_SETTING_ID         "setting_id"
#define IOT_JSON_KEY_FILAMENT_ID        "filament_id"
#define IOT_JSON_KEY_USER_ID            "user_id"

namespace BBL {

BambuNetworkAgent::BambuNetworkAgent()
{
    context = new AccountManager();
    ssdp = new SsdpDiscovery();
}

BambuNetworkAgent::~BambuNetworkAgent()
{
    if (context) {
        delete (AccountManager*)context;
        context = nullptr;
    }

    if (ssdp) {
        delete ssdp;
        ssdp = nullptr;
    }
}

void BambuNetworkAgent::init_log()
{
    AccountManager* acc = (AccountManager*)context;
    acc->init_log();
}

void BambuNetworkAgent::set_config_dir(std::string dir){
    AccountManager* acc = (AccountManager*)context;
	acc->set_config_dir(dir);
}

void BambuNetworkAgent::set_cert_file(std::string folder, std::string filename)
{
    AccountManager* acc = (AccountManager*)context;
    acc->set_cert_dir_name(folder, filename);
}

void BambuNetworkAgent::set_country_code(std::string country_code)
{
    AccountManager* acc = (AccountManager*)context;
    acc->update_country_code(country_code);
}

void BambuNetworkAgent::load_config()
{
    AccountManager* acc = (AccountManager*)context;
    acc->load_config();
    acc->load_user_info();
}

void BambuNetworkAgent::set_on_user_login_fn(OnUserLoginFn fn)
{
    AccountManager* acc = (AccountManager*)context;
    acc->set_on_user_login_fn(fn);
}

void BambuNetworkAgent::set_on_printer_connected_fn(OnPrinterConnectedFn fn)
{
    AccountManager* acc = (AccountManager*)context;
    acc->set_on_printer_connected_fn(fn);
}
void BambuNetworkAgent::set_on_server_connected_fn(OnServerConnectedFn fn)
{
    AccountManager* acc = (AccountManager*)context;
    acc->set_on_server_connected_fn(fn);
}

void BambuNetworkAgent::set_on_http_error_fn(OnHttpErrorFn fn)
{
    AccountManager* acc = (AccountManager*)context;
    acc->set_on_http_error_fn(fn);
}

void BambuNetworkAgent::set_get_country_code_fn(GetCountryCodeFn fn)
{
    AccountManager* acc = (AccountManager*)context;
    acc->set_get_country_code_fn(fn);
}

void BambuNetworkAgent::set_on_message_fn(OnMessageFn fn)
{
    AccountManager* acc = (AccountManager*)context;
    acc->set_on_message_fn(fn);
}
void BambuNetworkAgent::set_on_local_connect_fn(OnLocalConnectedFn fn)
{
    AccountManager* acc = (AccountManager*)context;
    acc->set_on_local_connect_fn(fn);
}
void BambuNetworkAgent::set_on_local_message_fn(OnMessageFn fn)
{
    AccountManager* acc = (AccountManager*)context;
    acc->set_on_local_message_fn(fn);
}

void BambuNetworkAgent::set_on_ssdp_msg_fn(OnMsgArrivedFn fn)
{
    SsdpDiscovery* discovery = (SsdpDiscovery*)ssdp;
    discovery->set_on_msg_fn(fn);
}


int BambuNetworkAgent::connect_server()
{
    AccountManager* acc = (AccountManager*)context;
    return acc->connect_mqtt();
}

bool BambuNetworkAgent::is_server_connected()
{
    AccountManager* acc = (AccountManager*)context;
    return acc->is_mqtt_connected();
}

void BambuNetworkAgent::check_server_connection()
{
    AccountManager* acc = (AccountManager*)context;
    return acc->check_mqtt_connection();
}

void BambuNetworkAgent::start_subscribe(std::string module)
{
    AccountManager* acc = (AccountManager*)context;
    acc->start_subscribe(module);
}

void BambuNetworkAgent::stop_subscribe(std::string module)
{
    AccountManager* acc = (AccountManager*)context;
    acc->stop_subscribe(module);
}

int BambuNetworkAgent::send_message(std::string dev_id, std::string json_str, int qos)
{
    AccountManager* acc = (AccountManager*)context;
    return acc->send_message(dev_id, json_str, qos);
}

int BambuNetworkAgent::connect_printer(std::string dev_id, std::string dev_ip, std::string username, std::string password)
{
    AccountManager* acc = (AccountManager*)context;
    return acc->local_connect_mqtt(dev_id, dev_ip, username, password);
}
int BambuNetworkAgent::disconnect_printer()
{
    AccountManager* acc = (AccountManager*)context;
    return acc->local_disconnect_mqtt();
}

int BambuNetworkAgent::send_message_to_printer(std::string dev_id, std::string json_str, int qos)
{
    AccountManager* acc = (AccountManager*)context;
    return acc->local_send_message(dev_id, json_str, qos);
}

bool BambuNetworkAgent::start_discovery(bool start, bool sending)
{
    SsdpDiscovery* discovery = (SsdpDiscovery*)ssdp;

    discovery->set_ssdp_discovery(sending);
    if (start) {
        return discovery->start();
    } else {
        return discovery->stop();
    }
    return true;
}

void BambuNetworkAgent::change_user(std::string user_info)
{
    AccountManager* acc = (AccountManager*)context;
    acc->set_curr_user(user_info);
}

bool BambuNetworkAgent::is_user_login()
{
    AccountManager* acc = (AccountManager*)context;
    return acc->is_user_login();
}


void BambuNetworkAgent::user_logout()
{
    AccountManager* acc = (AccountManager*)context;
    acc->user_logout();
}

std::string BambuNetworkAgent::user_id()
{
    AccountManager* acc = (AccountManager*)context;
    if (acc->get_curr_user())
        return acc->get_curr_user()->user_id();
    return "";
}

std::string BambuNetworkAgent::user_name()
{
    AccountManager* acc = (AccountManager*)context;
    if (acc->get_curr_user())
        return acc->get_curr_user()->m_account;
    return "";
}

std::string BambuNetworkAgent::user_avatar()
{
    AccountManager* acc = (AccountManager*)context;
    if (acc->get_curr_user())
        return acc->get_curr_user()->m_avatar;
    return "";
}

std::string BambuNetworkAgent::user_nickanme()
{
    AccountManager* acc = (AccountManager*)context;
    if (acc->get_curr_user())
        return acc->get_curr_user()->m_name;
    return "";
}

std::string BambuNetworkAgent::build_login_cmd()
{
    AccountManager* acc = (AccountManager*)context;
    return acc->build_login_cmd();
}

std::string BambuNetworkAgent::build_logout_cmd()
{
    AccountManager* acc = (AccountManager*)context;
    return acc->build_logout_cmd();
}

std::string BambuNetworkAgent::build_login_info()
{
    AccountManager* acc = (AccountManager*)context;
    json j = json::object();
    j["command"] = "studio_userlogin";
    j["sequence_id"] = "10001";
    j["data"] = json::object();
    j["data"]["avatar"] = acc->get_curr_user()->m_avatar;
    j["data"]["name"] = acc->get_curr_user()->m_name;
    return j.dump(-1, ' ', false, json::error_handler_t::ignore);
}

int BambuNetworkAgent::start_bind(std::string dev_ip, OnUpdateStatusFn update_fn)
{
    AccountManager* acc = (AccountManager*)context;
    return acc->start_bind(dev_ip, update_fn);
}

int BambuNetworkAgent::start_unbind(std::string dev_id)
{
    AccountManager* acc = (AccountManager*)context;
    return acc->request_user_unbind(dev_id);
}

std::string BambuNetworkAgent::get_bambulab_host()
{
    AccountManager* acc = (AccountManager*)context;
    return acc->get_official_server_host();
}

std::string BambuNetworkAgent::user_selected_machine()
{
    AccountManager* acc = (AccountManager*)context;
    return acc->get_selected_machine();
}

void BambuNetworkAgent::set_user_selected_machine(std::string dev_id)
{
    AccountManager* acc = (AccountManager*)context;
    return acc->set_selected_machine(dev_id);
}


int BambuNetworkAgent::start_print(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn)
{
    AccountManager* acc = (AccountManager*)context;
    return acc->start_print(params, update_fn, cancel_fn);
}

int BambuNetworkAgent::start_local_print_with_record(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn)
{
    AccountManager* acc = (AccountManager*)context;
    return acc->start_local_print_with_record(params, update_fn, cancel_fn);
}

int BambuNetworkAgent::start_local_print(PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn)
{
    AccountManager* acc = (AccountManager*)context;
    return acc->start_local_print(params, update_fn, cancel_fn);
}

std::map<std::string, std::map<std::string, std::string>>& BambuNetworkAgent::get_user_presets()
{
    AccountManager* acc = (AccountManager*)context;
    return acc->m_my_presets;
}

std::string BambuNetworkAgent::request_setting_id(std::string name, std::map<std::string, std::string>& values_map, unsigned int& http_code)
{
    AccountManager* acc = (AccountManager*)context;
    return acc->request_setting_id(name, values_map, http_code);
}

int BambuNetworkAgent::put_setting(std::string setting_id, std::string name, std::map<std::string, std::string>& values_map, unsigned int& http_code)
{
    AccountManager* acc = (AccountManager*)context;
    return acc->put_setting(setting_id, name, values_map, http_code);
}

 int BambuNetworkAgent::get_setting_list(std::string bundle_version)
{
    AccountManager* acc = (AccountManager*)context;
    return acc->get_setting_list(bundle_version);
}

std::vector<std::string>& BambuNetworkAgent::get_delete_cache_presets()
{
    AccountManager* acc = (AccountManager*)context;
    return acc->need_delete_presets;
}

void BambuNetworkAgent::delete_preset(std::string setting_id)
{
    AccountManager* acc = (AccountManager*)context;
    acc->need_delete_presets.push_back(setting_id);
}

 int BambuNetworkAgent::del_setting(std::string setting_id)
 {
     AccountManager* acc = (AccountManager*)context;
     unsigned http_code;
     return acc->del_setting(setting_id, http_code);
 }

std::string BambuNetworkAgent::get_studio_info_url()
{
    AccountManager* acc = (AccountManager*)context;
    return acc->get_slicer_info_url();
}

void BambuNetworkAgent::set_extra_http_header(std::map<std::string, std::string> extra_headers)
{
    Http::set_extra_headers(extra_headers);
}

void BambuNetworkAgent::check_user_task_report(int& task_id, bool& printable)
{
    AccountManager* acc = (AccountManager*)context;
    acc->user_check_report(task_id, printable);
}

int BambuNetworkAgent::get_user_print_info(unsigned int& http_code, std::string& http_body)
{
    AccountManager* acc = (AccountManager*)context;
    return acc->get_print_info(http_code, http_body);
}

int BambuNetworkAgent::get_printer_firmware(std::string dev_id, unsigned& http_code, std::string& http_body)
{
    AccountManager* acc = (AccountManager*)context;
    return acc->get_machine_version(dev_id, http_code, http_body);
}

void BambuNetworkAgent::get_task_plate_index(std::string task_id, int& plate_index)
{
    AccountManager* acc = (AccountManager*)context;
    acc->get_plate_index(task_id, plate_index);
}

void BambuNetworkAgent::get_slice_info(std::string project_id, std::string profile_id, int plate_index, std::string& slice_json)
{
    AccountManager* acc = (AccountManager*)context;
    acc->get_slice_info(project_id, profile_id, plate_index, slice_json);
}

int BambuNetworkAgent::query_bind_status(std::vector<std::string> query_list, unsigned int& http_code, std::string& http_body)
{
    AccountManager* acc = (AccountManager*)context;
    return acc->query_bind_status(query_list, http_code, http_body);
}

int BambuNetworkAgent::modify_printer_name(std::string dev_id, std::string dev_name)
{
    AccountManager* acc = (AccountManager*)context;
    unsigned int http_code;
    std::string http_body;
    return acc->modify_device_name(dev_id, dev_name, http_code, http_body);
}

void BambuNetworkAgent::get_camera_url(std::string dev_id, std::function<void(std::string)> callback)
{
    AccountManager* acc = (AccountManager*)context;
    acc->get_camera_url(dev_id, callback);
}

}

#endif
