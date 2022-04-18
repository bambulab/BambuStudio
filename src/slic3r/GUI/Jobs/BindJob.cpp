#include "BindJob.hpp"

#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"

namespace Slic3r {
namespace GUI {

wxString get_login_fail_reason(std::string fail_reason)
{
    if (fail_reason == "NO Regions")
        return _L("The region parameter is incorrrect");
    else if (fail_reason == "Cloud Timeout")
        return _L("Failure of printer login");
    else if (fail_reason == "Ticket Failed")
        return _L("Failed to get ticket");
    else if (fail_reason == "Wait Auth Timeout")
        return _L("User authorization timeout");
    else if (fail_reason == "Bind Failure")
        return _L("Failure of bind");
    else
        return _L("Unknown Failure");
}

BindJob::BindJob(std::shared_ptr<ProgressIndicator> pri, Plater *plater, std::string dev_id) : PlaterJob{std::move(pri), plater},
    m_dev_id(dev_id)
{
    ;
}

void BindJob::on_exception(const std::exception_ptr &eptr)
{
    try {
        if (eptr)
            std::rethrow_exception(eptr);
    } catch (std::exception &e) {
        PlaterJob::on_exception(eptr);
    }
}

void BindJob::on_success(std::function<void()> success) 
{ 
    m_success_fun = success;
}

void BindJob::process()
{
    /* display info */
    wxString msg = _L("begin to bind");
    int curr_percent = 0;
    update_status(curr_percent, msg);

    int result = -1;
    unsigned int http_code;
    std::string http_body;
    Slic3r::AccountManager* acc = Slic3r::GUI::wxGetApp().getAccountManager();
    Slic3r::DeviceManager *dev_manager = Slic3r::GUI::wxGetApp().getDeviceManager();

    std::map<std::string, MachineObject *> list = dev_manager->get_all_machine_list();
    std::map<std::string, MachineObject *>::iterator it   = list.find(m_dev_id);
    if (it == list.end()) {
        msg = wxString::Format("Can not find Printer SN = %s", m_dev_id);
        update_status(curr_percent, msg);
        return;
    }
    MachineObject *obj = it->second;


    curr_percent = 10;
    update_status(curr_percent, "connecting printer");
    result = obj->local_connect();
    if (result < 0) {
        BOOST_LOG_TRIVIAL(trace) << "login_bind: local connect failed!";
        msg = wxString::Format("Connecting printer=%s(sn:%s), ip = %s failed!", obj->dev_name, m_dev_id, obj->dev_ip);
        update_status(curr_percent, msg);
        return;
    }


    curr_percent = 20;
    update_status(curr_percent, "sending login info");
    std::string login_request = obj->build_login_request();
    result = obj->local_client->publish(login_request);
    if (result < 0) {
        BOOST_LOG_TRIVIAL(trace) << "login_bind: send login request failed, str = " << login_request;
        obj->local_disconnect();
        return;
    }


    curr_percent = 30;
    update_status(curr_percent, "receiving login report");

    std::string json_str;
    std::string login_ticket;
    bool        timeout    = false;
    int         recv_count = 0;
    while (!timeout) {
        result = obj->local_client->recv(json_str);
        if (!json_str.empty() && result >= 0) {
            try {
                BOOST_LOG_TRIVIAL(trace) << "login_bind: json_str = " << json_str;
                json j = json::parse(json_str);
                if (j.contains("login") && !j["login"].is_null()) {
                    if (j["login"]["command"].get<std::string>() == "login_report") {
                        login_ticket       = j["login"]["ticket"].get<std::string>();
                        std::string status = j["login"]["status"].get<std::string>();
                        if (status.compare("wait_auth") == 0 && !login_ticket.empty()) { break; }
                    }
                }
            } catch (...) {
                ;
            }
        }
        recv_count++;
        if (recv_count > 10) {
            timeout = true;
        }
    }

    if (timeout || login_ticket.empty()) {
        BOOST_LOG_TRIVIAL(trace) << "login_bind: timeout to get ticket";
        update_status(curr_percent, "get ticket failed");
        obj->local_disconnect();
        return;
    }

    curr_percent = 40;
    update_status(curr_percent, "ensure ticket");

    result = acc->get_ticket(login_ticket, http_code, http_body);
    if (result < 0) {
        update_status(curr_percent, "get ticket api failed");
        BOOST_LOG_TRIVIAL(trace) << "login_bind: http_code = " << http_code << ", http_body = " << http_body;
        obj->local_disconnect();
        return;
    }

    result = acc->post_ticket(login_ticket, http_code, http_body);
    if (result < 0) {
        update_status(curr_percent, "post ticket failed");
        BOOST_LOG_TRIVIAL(trace) << "login_bind: http_code = " << http_code << ", http_body = " << http_body;
        obj->local_disconnect();
        return;
    }


    curr_percent = 60;
    update_status(curr_percent, "wait for auth");
    recv_count = 0;
    while (!timeout) {
        boost::this_thread::sleep_for(boost::chrono::milliseconds(1000));
        result = obj->local_client->recv(json_str);
        if (result >= 0) {
            BOOST_LOG_TRIVIAL(trace) << "login_bind: json_str = " << json_str;
            std::string fail_reason;
            result = obj->_parse_login_report(json_str, fail_reason);
            if (result < 0) {
                wxString reason = get_login_fail_reason(fail_reason);
                msg = wxString::Format("Bind Failed, reason = %s", reason);
                update_status(curr_percent, msg);
                BOOST_LOG_TRIVIAL(trace) << "login_bind: bind failed reason = " << fail_reason;
                obj->local_disconnect();
                return;
            } else if (result == 0) {
                break;
            } else if (result == 1) {
                ; // continue
            }
        }
        recv_count++;
        if (recv_count > 20) { timeout = true; }
    }
    if (timeout) {
        update_status(curr_percent, "timeout to received status");
        BOOST_LOG_TRIVIAL(trace) << "login_bind: timeout to receive login_report";
        obj->local_disconnect();
        return;
    }

    obj->local_disconnect();
    curr_percent = 100;
    update_status(curr_percent, "Bind Success!");

    return;

}

void BindJob::finalize()
{
    if (was_canceled()) return;

    Job::finalize();
}

}} // namespace Slic3r::GUI
