#include "libslic3r/libslic3r.h"
#include "DeviceManager.hpp"
#include "libslic3r/Time.hpp"
#include "libslic3r/Thread.hpp"
#include "slic3r/Utils/Http.hpp"
#include "slic3r/Utils/Sftp.hpp"

#include <thread>
#include <mutex>
#include <codecvt>
#include <boost/foreach.hpp>
#include <boost/typeof/typeof.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>


namespace Slic3r {

/* Common Functions */
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


/* mqtt client connection callbacks */
void machine_conn_callback::reconnect()
{
    /* sleep ? */
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));
    try {
        BOOST_LOG_TRIVIAL(trace) << "machine_conn_callback::reconnect()  connecting...";
        MachineObject* obj = (MachineObject*)context_;
        if (obj) {
            obj->conn_state = MachineObject::CONNECTION_STATE::STATE_CONNECTING;
        }
        cli_.connect(connOpts_, context_, *this);
    }
    catch (const mqtt::exception& exc) {
        BOOST_LOG_TRIVIAL(trace) << "machine_conn_callback::reconnect() exception:" << exc.get_message();
    }
    catch (std::exception& e) {
        BOOST_LOG_TRIVIAL(trace) << "machine_conn_callback::reconnect() exception:" << e.what();
    }
}

void machine_conn_callback::connected(const std::string& cause)
{
    BOOST_LOG_TRIVIAL(trace) << "client_conn_callback::connected!";
    /* subscribe current device reqeust and report */
    try {
        if (successFn) {
            successFn(cli_.get_client_id());
        }
        for (int i = 0; i < sub_topics.size(); i++) {
            sub_action_listener* sub_listener = new sub_action_listener("LanSubscriber_" + sub_topics[i]);
            cli_.subscribe(sub_topics[i], 0, nullptr, *sub_listener);
        }
        MachineObject* obj = (MachineObject*)context_;
        if (obj) {
            obj->conn_state = MachineObject::CONNECTION_STATE::STATE_CONNECTED;
        }
    }
    catch (mqtt::exception& e) {
        BOOST_LOG_TRIVIAL(trace) << "client_conn_callback::connected, exception=" << e.what();
    }
}

void machine_conn_callback::on_failure(const mqtt::token& tok)
{
    BOOST_LOG_TRIVIAL(trace) << "client_conn_callback::on_failure, Connection(mqtt) failed! retry=" << nretry_;
    /* mqtt connect failed tips */
    if (failedFn) {
        failedFn(cli_.get_client_id());
    }
    MachineObject* obj = (MachineObject*)context_;
    if (obj) {
        obj->set_connect_state(MachineObject::CONNECTION_STATE::STATE_DISCONNECTED);
    }
    ++nretry_;
    reconnect();
}

void machine_conn_callback::on_success(const mqtt::token& tok)
{
    BOOST_LOG_TRIVIAL(trace) << "client_conn_callback::on_success, Connection(mqtt) OK!";
    MachineObject* obj = (MachineObject*)context_;
    if (obj) {
        obj->set_connect_state(MachineObject::CONNECTION_STATE::STATE_CONNECTED);
    }
}

void machine_conn_callback::connection_lost(const std::string& cause) {
    BOOST_LOG_TRIVIAL(trace) << "client_conn_callback::connection_lost!, cause =" << cause;
    if (lostFn) {
        lostFn(cli_.get_client_id());
    }
    MachineObject* obj = (MachineObject*)context_;
    if (obj) {
        obj->set_connect_state(MachineObject::CONNECTION_STATE::STATE_DISCONNECTED);
    }
    ++nretry_;
    reconnect();
}

void machine_conn_callback::message_arrived(mqtt::const_message_ptr msg)
{
    MachineObject* obj = (MachineObject*)context_;
    if (obj->msg_recv_fn) {
        obj->msg_recv_fn(msg->get_topic(), msg->get_payload_str());
    }
}

void machine_conn_callback::set_connect_fns(SuccessFn sFn, FailedFn fFn, LostFn lFn)
{
    successFn = sFn;
    failedFn = fFn;
    lostFn = lFn;
}


MachineObject::MachineObject(AccountManager& acc, std::string name, std::string id, std::string ip)
    :acc_(acc),
    mqtt_cb(nullptr),
    mqtt_cli(nullptr),
    msg_send_fn(nullptr),
    msg_recv_fn(nullptr),
    dev_name(name),
    dev_id(id),
    dev_ip(ip),
    dev_bind_status(MACHINE_BIND_UNKOWN),
    conn_type(CONNECTION_LAN),
    is_alive(false),
    is_online(false),
    mqtt_uuid_bytes(4),
    mqtt_opt(mqtt::connect_options_builder()
        .clean_session()
        .finalize())
{
    boost::uuids::uuid uuid = boost::uuids::random_generator()();
    mqtt_uuid = to_string(uuid).substr(0, mqtt_uuid_bytes);
}

bool MachineObject::check_valid_ip()
{
    if (dev_ip.empty()) {
        return false;
    }

    return true;
}

int MachineObject::connect(SuccessFn sFn, FailedFn fFn, LostFn lFn)
{
    if (!check_valid_ip()) {
        if (fFn) {
            fFn("Invalid IP!");
        }
        return -1;
    }

    try {
        if (acc_.is_user_login()) {
            if (is_connected()) {
                if (sFn) {
                    sFn("Already Connected!");
                }
                return 0;
            }
            if (mqtt_cli != nullptr) {
                if (fFn) {
                    fFn("Connecting state!");
                    return -1;
                }
            }

            /* lan mqtt connection */
            std::string client_id = (boost::format("%1%:%2%") % acc_.get_user_id() % mqtt_uuid).str();
            std::string report_topic = build_report_topic(dev_id);
            mqtt_cli = new mqtt::async_client(dev_ip, client_id);
            mqtt_cb = new machine_conn_callback(*mqtt_cli, mqtt_opt, this);
            mqtt_cb->set_connect_fns(sFn, fFn, lFn);
            mqtt_cb->add_topics(report_topic);
            mqtt_cli->set_callback(*mqtt_cb);
            mqtt_cli->connect(mqtt_opt, this, *mqtt_cb);

            /* wan mqtt connenction */
            /* TODO
            sub_action_listener* sub_listener = new sub_action_listener("WanSubscriber_" + report_topic);
            mqtt_cloud.subscribe(report_topic, 0, this, *sub_listener);
            */
            return 0;
        }
    }
    catch (std::exception& e) {
        return -1;
    }
    return 0;
}

int MachineObject::disconnect()
{
    if (mqtt_cli) {
        try {
            mqtt_cli->disable_callbacks();
            mqtt_cli->disconnect()->wait_for(100);
        }
        catch (std::exception& e) {

        }
        catch (...) {
            ;
        }
        delete mqtt_cli;
        mqtt_cli = NULL;
    }
    return 0;
}

bool MachineObject::is_connected()
{
    if (mqtt_cli) {
        return mqtt_cli->is_connected();
    }
    else {
        return false;
    }
    return false;
}

int MachineObject::publish_json(std::string json_str, ResultFn resFn, CONNECTION_TYPE conn_type)
{
    if (conn_type == CONNECTION_TYPE::CONNECTION_DEFAULT) {
        conn_type = CONNECTION_TYPE::CONNECTION_LAN;
    }

    mqtt::async_client* client = nullptr;
    if (conn_type == CONNECTION_LAN) {
        client = mqtt_cli;
    }
    else if (conn_type == CONNECTION_WAN) {
        client = acc_.get_client();
    }
    else {
        ;
    }

    if (client) {
        if (client->is_connected()) {
            std::string topic = (boost::format("device/%1%/request") % dev_id).str();
            json_str += '\0';
            client->publish(topic, json_str);
            if (msg_send_fn) {
                msg_send_fn(topic, json_str);
            }
        }
        else {
            if (resFn) {
                resFn(-1, "Please Connect First!");
            }
        }
    }
    return 0;
}

std::wstring get_printer_dest_file(std::wstring file)
{
    std::wstring result;

    result = L"/data/";

    int name_start = file.find_last_of(L"\\");
    if (name_start <= 0) {
        return result;
    }
    result = result + file.substr(name_start + 1, file.size());

    return result;
}

int MachineObject::send_print_task(BBLTask* task)
{
    if (conn_type == CONNECTION_WAN) {
        send_wan_print_task(task);
    }
    else {
        ;
    }
    return 0;
}

int MachineObject::send_wan_print_task(BBLTask* task)
{
    /* send json command */
    pt::ptree root, print;
    print.put("sequence_id", MachineObject::m_sequence_id++);
    print.put("command", "gcode_file");
    print.put("project_id", task->task_project_id);
    print.put("profile_id", task->task_profile_id);
    print.put("url", task->task_url);
    print.put("md5", task->task_url_md5);
    print.put("task_id", task->task_id);
    print.put("subtask", "0");
    root.put_child("print", print);
    std::stringstream oss;
    pt::write_json(oss, root);
    std::string json_str = oss.str();
    /* !!! remove '\' !!!! */
    json_str.erase(std::remove(json_str.begin(), json_str.end(), '\\'), json_str.end());
    this->publish_json(json_str);
    return 0;
}


int MachineObject::send_print_subtask(BBLSubTask *task, UploadedFn uploadedFn, UploadProgressFn proFn, ErrorFn errFn)
{
    if (conn_type == CONNECTION_LAN) {
        send_lan_print_subtask(task, uploadedFn, proFn, errFn);
    }
    else if (conn_type == CONNECTION_WAN) {
        send_wan_print_subtask(task, uploadedFn, proFn, errFn);
    }
    else {
        ;
    }
    return 0;
}

int MachineObject::send_lan_print_subtask(BBLSubTask* task, UploadedFn uploadedFn, UploadProgressFn proFn, ErrorFn errFn)
{
    std::wstring src_file = task->task_file;
    std::wstring dst_file = get_printer_dest_file(src_file);
    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
    std::string dst_file_str = converter.to_bytes(dst_file);

    Sftp sftp = Sftp::upload(dev_ip, src_file, dst_file, "root", "root");
    
    sftp.on_complete(
        [this, src_file, dst_file_str, uploadedFn](std::string body) {
            /* boost::filesystem::file_size not right */
            if (uploadedFn) {
                uploadedFn();
            }

            BOOST_LOG_TRIVIAL(trace) << "transform gcode ok!";


            /* send json command */
            pt::ptree root, print;
            print.put("sequence_id", MachineObject::m_sequence_id++);
            print.put("command", "gcode_file");
            print.put<std::string>("param", dst_file_str);
            root.put_child("print", print);
            std::stringstream oss;
            pt::write_json(oss, root);
            std::string json_str = oss.str();
            /* !!! remove '\' !!!! */
            json_str.erase(std::remove(json_str.begin(), json_str.end(), '\\'), json_str.end());
            this->publish_json(json_str);
        })
        .on_error([errFn, src_file](std::string error) {
            if (errFn) {
                errFn(error);
            }
            BOOST_LOG_TRIVIAL(trace) << boost::format("transform gcode %1% failed, error = %2%")
                % src_file.c_str()
                % error;
        })
        .on_progress([proFn](Slic3r::Sftp::Progress progress, bool& cancel) {
            BOOST_LOG_TRIVIAL(trace) << " progress:" << progress.ulnow << "/" << progress.ultotal;
            int percent = 0;
            if (progress.ultotal != 0) {
                percent = progress.ulnow * 100 / progress.ultotal;
            }
            if (proFn) {
                proFn(percent);
            }
        })
        .perform();

    return 0;
}

int MachineObject::send_wan_print_subtask(BBLSubTask* task, UploadedFn uploadedFn, UploadProgressFn proFn, ErrorFn errFn)
{
    acc_.post_task(task,
        [this, task, uploadedFn, errFn](int result, std::string info) {
            if (result == 0) {
                if (uploadedFn) {
                    uploadedFn();
                }

                std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
                std::string task_file_str = converter.to_bytes(task->task_name);

                pt::ptree root, print;
                print.put("sequence_id", MachineObject::m_sequence_id++);
                print.put("command", "project_file");
                print.put("param", task_file_str);
                print.put("url", task->task_url);   /* 3mf or gcode */
                print.put("md5", task->task_url_md5);
                /* project */
                print.put("project_id", task->task_project_id);
                print.put("task_id", task->parent_task_id);
                print.put("subtask_id", task->task_id);
                root.put_child("print", print);
                std::stringstream oss;
                pt::write_json(oss, root);
                std::string json_str = oss.str();
                this->publish_json(json_str);
            }
            else {
                errFn(info);
            }
        }
        ,
        [this, proFn](int percent) {
            if (proFn) {
                proFn(percent);
            }
        }
        );
    return 0;
}

void MachineObject::request_bind(ResultFn resFn, bool force_bind)
{
    if (force_bind) {
        acc_.request_bind(dev_id, resFn);
    }
    else {
        /* send json command */
        pt::ptree root, bind;
        bind.put("sequence_id", MachineObject::m_sequence_id++);
        bind.put<std::string>("dev_id", this->dev_id);
        bind.put<std::string>("user_id", acc_.get_user_id());
        root.put_child("bind", bind);
        std::stringstream oss;
        pt::write_json(oss, root);
        std::string json_str = oss.str();
        this->publish_json(json_str, resFn);
    }
}

void MachineObject::request_unbind(ResultFn fn)
{
    acc_.request_user_unbind(this->dev_id, fn);
}

void MachineObject::set_bind_status(std::string status)
{
    if (status.compare("free") == 0) {
        dev_bind_status = MACHINE_BIND_FREE;
        owner = "";
    }
    else if (status.compare("self") == 0) {
        dev_bind_status = MACHINE_BIND_SELF;
        owner = "";
    }
    else if (status.compare("other") == 0) {
        dev_bind_status = MACHINE_BIND_OHTER;
        owner = "";
    }
    else {
        dev_bind_status = MACHINE_BIND_OHTER;
        owner = status;
    }
}

void MachineObject::set_connect_state(CONNECTION_STATE state)
{
    conn_state = state;
    if (state == STATE_DISCONNECTED) {
        /* unsubscribe topics in account manager */
        //TODO
        acc_.add_subscribe(this);
    }
    else if (state == STATE_CONNECTED) {
        /* subscribe topics in account manager */
        acc_.del_subscribe(this);
    }
    else
    {
        ;
    }
}

std::string MachineObject::get_bind_str()
{
    if (dev_bind_status == MACHINE_BIND_FREE) {
        return "free";
    }
    else if (dev_bind_status == MACHINE_BIND_SELF) {
        return "self";
    }
    else if (dev_bind_status == MACHINE_BIND_OHTER) {
        if (!owner.empty()) {
            return owner;
        }
        else {
            return "other";
        }
    }
    else if (dev_bind_status == MACHINE_BIND_UNKOWN) {
        return "unknown";
    }
    else {
        return "unknown";
    }
}

std::string MachineObject::build_report_topic(std::string dev_id)
{
    return (boost::format("device/%1%/report") % dev_id).str();
}

DeviceManager::DeviceManager(AccountManager& acc, CommuBackend& backend)
    : acc_(acc),
    backend_(backend)
{
    try {
        m_device_check_alive = Slic3r::create_thread([this] { this->check_alive(); });
    }
    catch (std::exception& e) {
        ;
    }
}

DeviceManager::~DeviceManager()
{
    if (m_check_alive_quit) return;

    m_check_alive_quit = true;
    m_device_check_alive.try_join_for(boost::chrono::milliseconds(200));
}

void DeviceManager::on_machine_alive(std::string dev_name, std::string dev_id, std::string dev_ip)
{
    std::lock_guard<std::mutex> lock(listMutex);
    MachineObject* obj;
    std::map<std::string, MachineObject*>::iterator it = localMachineList.find(dev_id);
    if (it != localMachineList.end()) {
        // update properties
        /* ip changed */
        obj = it->second;
        if (obj->dev_ip.compare(dev_ip) != 0) {
            obj->dev_ip = dev_ip;
            /* TODO if ip changed reconnect mqtt */
        }
        obj->last_alive = Slic3r::Utils::get_current_time_utc();
        obj->is_alive = true;
    }
    else {
        // add new machine
        obj = new MachineObject(acc_, dev_name, dev_id, dev_ip);
        localMachineList.insert(std::make_pair(dev_id, obj));

        /* insert a new machine */
    }
}

void DeviceManager::disconnect_all()
{
    std::map<std::string, MachineObject*>::iterator it;
    for (it = localMachineList.begin(); it != localMachineList.end(); it++) {
        it->second->disconnect();
    }
}

void DeviceManager::query_bind_status(AccountManager::CompletedFn cFn, AccountManager::ErrorFn errFn)
{
    std::lock_guard<std::mutex> lock(listMutex);
    std::map<std::string, MachineObject*>::iterator it;
    std::vector<std::string> query_list;
    for (it = localMachineList.begin(); it != localMachineList.end(); it++) {
        query_list.push_back(it->first);
    }

    acc_.query_bind_status(query_list,
        [this, query_list, cFn](std::string message) {
            std::vector<std::string> status_list;
            split_string(message, status_list);

            if (status_list.size() != query_list.size()) {
                BOOST_LOG_TRIVIAL(trace) << "query_bind_status, size is not matched, error.";
                return;
            }

            // update device bind status list
            for (int i = 0; i < query_list.size() && i < query_list.size(); i++) {
                std::map<std::string, MachineObject*>::iterator it = localMachineList.find(query_list[i]);
                if (it != localMachineList.end()) {
                    it->second->set_bind_status(status_list[i]);
                }
            }
            if (cFn) {
                cFn(message);
            }
        },
        [this, errFn](int status, std::string error, std::string body) {
            BOOST_LOG_TRIVIAL(trace) << "query_bind_status error=" << error << ", body=" << body << ", status=" << status;
            if (errFn) {
                errFn(status, error, body);
            }
        }
    );
}

MachineObject* DeviceManager::get_default()
{
    if (default_machine.empty())
        return nullptr;

    /* find in local list */
    std::map<std::string, MachineObject*>::iterator it = localMachineList.find(default_machine);
    if (it != localMachineList.end()) {
        return it->second;
    }

    return nullptr;
}

std::map<std::string ,MachineObject*> DeviceManager::get_all_machine_list()
{
    std::map<std::string, MachineObject*> result;
    std::map<std::string, MachineObject*>::iterator it;

    for (it = localMachineList.begin(); it != localMachineList.end(); it++) {
        if (it->second->is_alive) {
            result.insert(std::make_pair(it->first, it->second));
        }
    }
    
    return result;
}


void DeviceManager::check_alive()
{
    while (!m_check_alive_quit) {
        time_t curr = Slic3r::Utils::get_current_time_utc();
        double seconds;
        std::map<std::string, MachineObject*>::iterator it;
        for (it = localMachineList.begin(); it != localMachineList.end(); it++) {
            seconds = difftime(curr, it->second->last_alive);
            if (seconds > ALIVE_TIMEOUT) {
                if (it->second->conn_state != MachineObject::CONNECTION_STATE::STATE_DISCONNECTED) {
                    it->second->conn_state = MachineObject::CONNECTION_STATE::STATE_DISCONNECTED;
                    BOOST_LOG_TRIVIAL(trace) << "device id = " << it->first << " is offline!";
                }
            }
        }
        boost::this_thread::sleep_for(boost::chrono::milliseconds(1000));
    }
}

} // namespace Slic3r
