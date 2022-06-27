#include <boost/log/trivial.hpp>

#include "bambu_networking.hpp"
#include "BambuNetworkAgent.hpp"

using namespace BBL;

BambuNetworkAgent* g_bambu_network_agent = nullptr;

extern "C" {

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) void* bambu_network_create_agent()
#else
void* bambu_network_create_agent()
#endif
{
    if (!g_bambu_network_agent) {
        g_bambu_network_agent = new BambuNetworkAgent();
    }

    return (void *)g_bambu_network_agent;
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) int bambu_network_destroy_agent(void *agent)
#else
int bambu_network_destroy_agent(void *agent)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    delete g_bambu_network_agent;
    g_bambu_network_agent = nullptr;

    return BAMBU_NETWORK_SUCCESS;
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) int bambu_network_init_log(void *agent)
#else
int bambu_network_init_log(void *agent)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    //also init log here
    g_bambu_network_agent->init_log();

    return BAMBU_NETWORK_SUCCESS;
}


#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) int bambu_network_set_config_dir(void *agent, std::string config_dir)
#else
int bambu_network_set_config_dir(void *agent, std::string config_dir)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    g_bambu_network_agent->set_config_dir(config_dir);

    return BAMBU_NETWORK_SUCCESS;
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) int bambu_network_set_cert_file(void *agent, std::string folder, std::string filename)
#else
int bambu_network_set_cert_file(void *agent, std::string folder, std::string filename)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    g_bambu_network_agent->set_cert_file(folder, filename);
    return BAMBU_NETWORK_SUCCESS;
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) int bambu_network_set_country_code(void *agent, std::string country_code)
#else
int bambu_network_set_country_code(void *agent, std::string country_code)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    g_bambu_network_agent->set_country_code(country_code);
    return BAMBU_NETWORK_SUCCESS;
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) int bambu_network_start(void *agent)
#else
int bambu_network_start(void *agent)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    g_bambu_network_agent->load_config();
    return BAMBU_NETWORK_SUCCESS;
}

//set callbacks
#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) int bambu_network_set_on_ssdp_msg_fn(void *agent, OnMsgArrivedFn fn)
#else
int bambu_network_set_on_ssdp_msg_fn(void *agent, OnMsgArrivedFn fn)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    g_bambu_network_agent->set_on_ssdp_msg_fn(fn);
    return BAMBU_NETWORK_SUCCESS;
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) int bambu_network_set_on_user_login_fn(void *agent, OnUserLoginFn fn)
#else
int bambu_network_set_on_user_login_fn(void *agent, OnUserLoginFn fn)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    g_bambu_network_agent->set_on_user_login_fn(fn);
    return BAMBU_NETWORK_SUCCESS;
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) int bambu_network_set_on_printer_connected_fn(void *agent, OnPrinterConnectedFn fn)
#else
int bambu_network_set_on_printer_connected_fn(void *agent, OnPrinterConnectedFn fn)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    g_bambu_network_agent->set_on_printer_connected_fn(fn);
    return BAMBU_NETWORK_SUCCESS;
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) int bambu_network_set_on_server_connected_fn(void *agent, OnServerConnectedFn fn)
#else
int bambu_network_set_on_server_connected_fn(void *agent, OnServerConnectedFn fn)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    g_bambu_network_agent->set_on_server_connected_fn(fn);
    return BAMBU_NETWORK_SUCCESS;
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) int bambu_network_set_on_http_error_fn(void *agent, OnHttpErrorFn fn)
#else
int bambu_network_set_on_http_error_fn(void *agent, OnHttpErrorFn fn)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    g_bambu_network_agent->set_on_http_error_fn(fn);
    return BAMBU_NETWORK_SUCCESS;
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) int bambu_network_set_get_country_code_fn(void *agent, GetCountryCodeFn fn)
#else
int bambu_network_set_get_country_code_fn(void *agent, GetCountryCodeFn fn)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    g_bambu_network_agent->set_get_country_code_fn(fn);
    return BAMBU_NETWORK_SUCCESS;
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) int bambu_network_set_on_message_fn(void *agent, OnMessageFn fn)
#else
int bambu_network_set_on_message_fn(void *agent, OnMessageFn fn)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    g_bambu_network_agent->set_on_message_fn(fn);
    return BAMBU_NETWORK_SUCCESS;
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) int bambu_network_set_on_local_connect_fn(void *agent, OnLocalConnectedFn fn)
#else
int bambu_network_set_on_local_connect_fn(void *agent, OnLocalConnectedFn fn)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    g_bambu_network_agent->set_on_local_connect_fn(fn);
    return BAMBU_NETWORK_SUCCESS;
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) int bambu_network_set_on_local_message_fn(void *agent, OnMessageFn fn)
#else
int bambu_network_set_on_local_message_fn(void *agent, OnMessageFn fn)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    g_bambu_network_agent->set_on_local_message_fn(fn);
    return BAMBU_NETWORK_SUCCESS;
}

//connect
#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) int bambu_network_connect_server(void *agent)
#else
int bambu_network_connect_server(void *agent)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    int ret = g_bambu_network_agent->connect_server();

    if (ret) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ", connect_server returned error:" << ret <<std::endl;
        return BAMBU_NETWORK_ERR_CONNECT_FAILED;
    }
    else
        return BAMBU_NETWORK_SUCCESS;
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) bool bambu_network_is_server_connected(void *agent)
#else
bool bambu_network_is_server_connected(void *agent)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return false;

    return g_bambu_network_agent->is_server_connected();
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) int bambu_network_refresh_connection(void *agent)
#else
int bambu_network_refresh_connection(void *agent)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    g_bambu_network_agent->check_server_connection();

    return BAMBU_NETWORK_SUCCESS;
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) int bambu_network_start_subscribe(void *agent, std::string module)
#else
int bambu_network_start_subscribe(void *agent, std::string module)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    g_bambu_network_agent->start_subscribe(module);

    return BAMBU_NETWORK_SUCCESS;
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) int bambu_network_stop_subscribe(void *agent, std::string module)
#else
int bambu_network_stop_subscribe(void *agent, std::string module)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    g_bambu_network_agent->stop_subscribe(module);

    return BAMBU_NETWORK_SUCCESS;
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) int bambu_network_send_message(void *agent, std::string dev_id, std::string json_str, int qos)
#else
int bambu_network_send_message(void *agent, std::string dev_id, std::string json_str, int qos)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    int ret = g_bambu_network_agent->send_message(dev_id, json_str, qos);

    if (ret) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ", send_message returned error:" << ret <<std::endl;
        return BAMBU_NETWORK_ERR_CONNECT_FAILED;
    }
    else
        return BAMBU_NETWORK_SUCCESS;
}

//printer
#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) int bambu_network_connect_printer(void *agent, std::string dev_id, std::string dev_ip, std::string username, std::string password)
#else
int bambu_network_connect_printer(void *agent, std::string dev_id, std::string dev_ip, std::string username, std::string password)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    int ret = g_bambu_network_agent->connect_printer(dev_id, dev_ip, username, password);

    if (ret) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ", connect_printer returned error:" << ret <<std::endl;
        return BAMBU_NETWORK_ERR_CONNECT_FAILED;
    }
    else
        return BAMBU_NETWORK_SUCCESS;
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) int bambu_network_disconnect_printer(void *agent)
#else
int bambu_network_disconnect_printer(void *agent)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    int ret = g_bambu_network_agent->disconnect_printer();

    if (ret) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ", disconnect_printer returned error:" << ret <<std::endl;
        return BAMBU_NETWORK_ERR_DISCONNECT_FAILED;
    }
    else
        return BAMBU_NETWORK_SUCCESS;
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) int bambu_network_send_message_to_printer(void *agent, std::string dev_id, std::string json_str, int qos)
#else
int bambu_network_send_message_to_printer(void *agent, std::string dev_id, std::string json_str, int qos)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    int ret = g_bambu_network_agent->send_message_to_printer(dev_id, json_str, qos);

    if (ret) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ", send_message_to_printer returned error:" << ret <<std::endl;
        return BAMBU_NETWORK_ERR_SEND_MSG_FAILED;
    }
    else
        return BAMBU_NETWORK_SUCCESS;
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) bool bambu_network_start_discovery(void *agent, bool start, bool sending)
#else
bool bambu_network_start_discovery(void *agent, bool start, bool sending)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return false;

    return g_bambu_network_agent->start_discovery(start, sending);
}

//user
#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) int bambu_network_change_user(void *agent, std::string user_info)
#else
int bambu_network_change_user(void *agent, std::string user_info)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    g_bambu_network_agent->change_user(user_info);

    return BAMBU_NETWORK_SUCCESS;
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) bool bambu_network_is_user_login(void *agent)
#else
bool bambu_network_is_user_login(void *agent)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return false;

    return g_bambu_network_agent->is_user_login();
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) int bambu_network_user_logout(void *agent)
#else
int bambu_network_user_logout(void *agent)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    g_bambu_network_agent->user_logout();

    return BAMBU_NETWORK_SUCCESS;
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) std::string bambu_network_get_user_id(void *agent)
#else
std::string bambu_network_get_user_id(void *agent)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return std::string();

    return g_bambu_network_agent->user_id();
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) std::string bambu_network_get_user_name(void *agent)
#else
std::string bambu_network_get_user_name(void *agent)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return std::string();

    return g_bambu_network_agent->user_name();
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) std::string bambu_network_get_user_avatar(void *agent)
#else
std::string bambu_network_get_user_avatar(void *agent)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return std::string();

    return g_bambu_network_agent->user_avatar();
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) std::string bambu_network_get_user_nickanme(void *agent)
#else
std::string bambu_network_get_user_nickanme(void *agent)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return std::string();

    return g_bambu_network_agent->user_nickanme();
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) std::string bambu_network_build_login_cmd(void *agent)
#else
std::string bambu_network_build_login_cmd(void *agent)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return std::string();

    return g_bambu_network_agent->build_login_cmd();
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) std::string bambu_network_build_logout_cmd(void *agent)
#else
std::string bambu_network_build_logout_cmd(void *agent)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return std::string();

    return g_bambu_network_agent->build_logout_cmd();
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) std::string bambu_network_build_login_info(void *agent)
#else
std::string bambu_network_build_login_info(void *agent)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return std::string();

    return g_bambu_network_agent->build_login_info();
}

//bind/unbind
#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) int bambu_network_bind(void *agent, std::string dev_ip, std::string timezone, OnUpdateStatusFn update_fn)
#else
int bambu_network_bind(void *agent, std::string dev_ip, std::string timezone, OnUpdateStatusFn update_fn)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    int ret = g_bambu_network_agent->start_bind(dev_ip, timezone, update_fn);

    if (ret) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ", start_bind returned error:" << ret <<std::endl;
        return BAMBU_NETWORK_ERR_BIND_FAILED;
    }
    else
        return BAMBU_NETWORK_SUCCESS;
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) int bambu_network_unbind(void *agent, std::string dev_id)
#else
int bambu_network_unbind(void *agent, std::string dev_id)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    int ret = g_bambu_network_agent->start_unbind(dev_id);

    if (ret) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ", start_unbind returned error:" << ret <<std::endl;
        return BAMBU_NETWORK_ERR_UNBIND_FAILED;
    }
    else
        return BAMBU_NETWORK_SUCCESS;
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) std::string bambu_network_get_bambulab_host(void *agent)
#else
std::string bambu_network_get_bambulab_host(void *agent)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return std::string();

    return g_bambu_network_agent->get_bambulab_host();
}

//select machine
#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) std::string bambu_network_get_user_selected_machine(void *agent)
#else
std::string bambu_network_get_user_selected_machine(void *agent)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return std::string();

    return g_bambu_network_agent->user_selected_machine();
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) int bambu_network_set_user_selected_machine(void *agent, std::string dev_id)
#else
int bambu_network_set_user_selected_machine(void *agent, std::string dev_id)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    g_bambu_network_agent->set_user_selected_machine(dev_id);

    return BAMBU_NETWORK_SUCCESS;
}

//print
#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) int bambu_network_start_print(void *agent, PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn)
#else
int bambu_network_start_print(void *agent, PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    int ret = g_bambu_network_agent->start_print(params, update_fn, cancel_fn);

    if (ret) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ", start_print returned error:" << ret <<std::endl;
        return BAMBU_NETWORK_ERR_PRINT_FAILED;
    }
    else
        return BAMBU_NETWORK_SUCCESS;
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) int bambu_network_start_local_print_with_record(void *agent, PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn)
#else
int bambu_network_start_local_print_with_record(void *agent, PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    int ret = g_bambu_network_agent->start_local_print_with_record(params, update_fn, cancel_fn);

    if (ret) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ", start_local_print_with_record returned error:" << ret <<std::endl;
        return BAMBU_NETWORK_ERR_LOCAL_PRINT_FAILED;
    }
    else
        return BAMBU_NETWORK_SUCCESS;
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) int bambu_network_start_local_print(void *agent, PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn)
#else
int bambu_network_start_local_print(void *agent, PrintParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    int ret = g_bambu_network_agent->start_local_print(params, update_fn, cancel_fn);

    if (ret) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ", start_local_print returned error:" << ret <<std::endl;
        return BAMBU_NETWORK_ERR_LOCAL_PRINT_FAILED;
    }
    else
        return BAMBU_NETWORK_SUCCESS;
}

// presets
#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) int bambu_network_get_user_presets(void *agent, std::map<std::string, std::map<std::string, std::string>>* user_presets)
#else
int bambu_network_get_user_presets(void *agent, std::map<std::string, std::map<std::string, std::string>>* user_presets)
#endif
{
    if (((void *)g_bambu_network_agent != agent) || (!user_presets))
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    g_bambu_network_agent->get_user_presets(user_presets);

    return BAMBU_NETWORK_SUCCESS;
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) std::string bambu_network_request_setting_id(void *agent, std::string name, std::map<std::string, std::string>* values_map, unsigned int* http_code)
#else
std::string bambu_network_request_setting_id(void *agent, std::string name, std::map<std::string, std::string>* values_map, unsigned int* http_code)
#endif
{
    if (((void *)g_bambu_network_agent != agent) || (!values_map) || (!http_code))
        return std::string();

    return g_bambu_network_agent->request_setting_id(name, values_map, http_code);
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) int bambu_network_put_setting(void *agent, std::string setting_id, std::string name, std::map<std::string, std::string>* values_map, unsigned int* http_code)
#else
int bambu_network_put_setting(void *agent, std::string setting_id, std::string name, std::map<std::string, std::string>* values_map, unsigned int* http_code)
#endif
{
    if (((void *)g_bambu_network_agent != agent) || (!values_map) || (!http_code))
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    int ret = g_bambu_network_agent->put_setting(setting_id, name, values_map, http_code);

    if (ret) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ", put_setting returned error:" << ret <<std::endl;
        return BAMBU_NETWORK_ERR_PUT_SETTING_FAILED;
    }
    else
        return BAMBU_NETWORK_SUCCESS;
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) int bambu_network_get_setting_list(void *agent, std::string bundle_version, ProgressFn pro_fn)
#else
int bambu_network_get_setting_list(void *agent, std::string bundle_version, ProgressFn pro_fn)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    int ret = g_bambu_network_agent->get_setting_list(bundle_version, pro_fn);

    if (ret) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ", get_setting_list returned error:" << ret <<std::endl;
        return BAMBU_NETWORK_ERR_GET_SETTING_LIST_FAILED;
    }
    else
        return BAMBU_NETWORK_SUCCESS;
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) int bambu_network_delete_setting(void *agent, std::string setting_id)
#else
int bambu_network_delete_setting(void *agent, std::string setting_id)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    int ret = g_bambu_network_agent->delete_setting(setting_id);

    if (ret) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ", delete_setting returned error:" << ret <<std::endl;
        return BAMBU_NETWORK_ERR_DEL_SETTING_FAILED;
    }
    else
        return BAMBU_NETWORK_SUCCESS;
}

//iot
#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) std::string bambu_network_get_studio_info_url(void *agent)
#else
std::string bambu_network_get_studio_info_url(void *agent)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return std::string();

    return g_bambu_network_agent->get_studio_info_url();
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) int bambu_network_set_extra_http_header(void *agent, std::map<std::string, std::string> extra_headers)
#else
int bambu_network_set_extra_http_header(void *agent, std::map<std::string, std::string> extra_headers)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    g_bambu_network_agent->set_extra_http_header(extra_headers);

    return BAMBU_NETWORK_SUCCESS;
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) int bambu_network_check_user_task_report(void *agent, int* task_id, bool* printable)
#else
int bambu_network_check_user_task_report(void *agent, int* task_id, bool* printable)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    g_bambu_network_agent->check_user_task_report(task_id, printable);

    return BAMBU_NETWORK_SUCCESS;
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) int bambu_network_get_user_print_info(void *agent, unsigned int* http_code, std::string* http_body)
#else
int bambu_network_get_user_print_info(void *agent, unsigned int* http_code, std::string* http_body)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    int ret = g_bambu_network_agent->get_user_print_info(http_code, http_body);

    if (ret) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ", get_user_print_info returned error:" << ret <<std::endl;
        return BAMBU_NETWORK_ERR_GET_USER_PRINTINFO_FAILED;
    }
    else
        return BAMBU_NETWORK_SUCCESS;
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) int bambu_network_get_printer_firmware(void *agent, std::string dev_id, unsigned* http_code, std::string* http_body)
#else
int bambu_network_get_printer_firmware(void *agent, std::string dev_id, unsigned* http_code, std::string* http_body)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    int ret = g_bambu_network_agent->get_printer_firmware(dev_id, http_code, http_body);

    if (ret) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ", get_user_print_info returned error:" << ret <<std::endl;
        return BAMBU_NETWORK_ERR_GET_USER_PRINTINFO_FAILED;
    }
    else
        return BAMBU_NETWORK_SUCCESS;
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) int bambu_network_get_task_plate_index(void *agent, std::string task_id, int* plate_index)
#else
int bambu_network_get_task_plate_index(void *agent, std::string task_id, int* plate_index)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    g_bambu_network_agent->get_task_plate_index(task_id, plate_index);

    return BAMBU_NETWORK_SUCCESS;
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) int bambu_network_get_slice_info(void *agent, std::string project_id, std::string profile_id, int plate_index, std::string* slice_json)
#else
int bambu_network_get_slice_info(void *agent, std::string project_id, std::string profile_id, int plate_index, std::string* slice_json)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    g_bambu_network_agent->get_slice_info(project_id, profile_id, plate_index, slice_json);

    return BAMBU_NETWORK_SUCCESS;
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) int bambu_network_query_bind_status(void *agent, std::vector<std::string> query_list, unsigned int* http_code, std::string* http_body)
#else
int bambu_network_query_bind_status(void *agent, std::vector<std::string> query_list, unsigned int* http_code, std::string* http_body)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    int ret = g_bambu_network_agent->query_bind_status(query_list, http_code, http_body);

    if (ret) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ", query_bind_status returned error:" << ret <<std::endl;
        return BAMBU_NETWORK_ERR_QUERY_BIND_INFO_FAILED;
    }
    else
        return BAMBU_NETWORK_SUCCESS;
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) int bambu_network_modify_printer_name(void *agent, std::string dev_id, std::string dev_name)
#else
int bambu_network_modify_printer_name(void *agent, std::string dev_id, std::string dev_name)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    int ret = g_bambu_network_agent->modify_printer_name(dev_id, dev_name);

    if (ret) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ", modify_printer_name returned error:" << ret <<std::endl;
        return BAMBU_NETWORK_ERR_MODIFY_PRINTER_NAME_FAILED;
    }
    else
        return BAMBU_NETWORK_SUCCESS;
}

#if defined(_MSC_VER) || defined(_WIN32)
__declspec(dllexport) int bambu_network_get_camera_url(void *agent, std::string dev_id, std::function<void(std::string)> callback)
#else
int bambu_network_get_camera_url(void *agent, std::string dev_id, std::function<void(std::string)> callback)
#endif
{
    if ((void *)g_bambu_network_agent != agent)
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    g_bambu_network_agent->get_camera_url(dev_id, callback);

    return BAMBU_NETWORK_SUCCESS;
}

}//extern "C"
