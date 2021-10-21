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


void machine_conn_callback::connected(const std::string& cause)
{
    BOOST_LOG_TRIVIAL(trace) << "client_conn_callback::connected!";
    /* subscribe current device reqeust and report */
    try {
        MachineObject* obj = (MachineObject*)context_;
        if (obj && obj->successFn) {
            obj->successFn(cli_.get_client_id());
        }
        for (int i = 0; i < sub_topics.size(); i++) {
            sub_action_listener* sub_listener = new sub_action_listener("LanSubscriber_" + sub_topics[i]);
            cli_.subscribe(sub_topics[i], 0, nullptr, *sub_listener);
        }
        
        if (obj) {
            obj->set_connect_state(MachineObject::CONNECTION_STATE::STATE_CONNECTED);
        }
    }
    catch (mqtt::exception& e) {
        BOOST_LOG_TRIVIAL(trace) << "client_conn_callback::connected, exception=" << e.what();
    }
}

void machine_conn_callback::on_failure(const mqtt::token& tok)
{
    BOOST_LOG_TRIVIAL(trace) << "client_conn_callback::on_failure, Connection(mqtt) failed! retry=" << nretry_;
    MachineObject* obj = (MachineObject*)context_;
    if (obj) {
        /* mqtt connect failed tips */
        if (obj->failedFn) {
            obj->failedFn(cli_.get_client_id());
        }
        obj->set_connect_state(MachineObject::CONNECTION_STATE::STATE_DISCONNECTED);
    }
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
    MachineObject* obj = (MachineObject*)context_;
    if (obj) {
        if (obj->lostFn) {
            obj->lostFn(cli_.get_client_id());
        }
        obj->set_connect_state(MachineObject::CONNECTION_STATE::STATE_DISCONNECTED);
    }
    ++nretry_;
}

void machine_conn_callback::message_arrived(mqtt::const_message_ptr msg)
{
    MachineObject* obj = (MachineObject*)context_;
    if (obj) {
        obj->parse_json(msg->get_topic(), msg->get_payload_str());
    }
}

void AmsTray::update_color_from_str(std::string color)
{
    if (last_color.compare(color) == 0)
        return;
    
    wxUint32 rgba;
    try {
        rgba = stoi(color);
    }
    catch (...) {
        return;
    }

    wx_color.SetRGBA(rgba);
    last_color = color;
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
    subtask_(nullptr),
    temptask_(nullptr),
    is_alive(false),
    is_online(false),
    successFn(nullptr),
    failedFn(nullptr),
    lostFn(nullptr),
    mqtt_uuid_bytes(4),
    mqtt_opt(mqtt::connect_options_builder()
        .clean_session()
        .finalize())
{
    boost::uuids::uuid uuid = boost::uuids::random_generator()();
    mqtt_uuid = to_string(uuid).substr(0, mqtt_uuid_bytes);
    mqtt_opt.set_automatic_reconnect(3, 10);
    mqtt_opt.set_max_inflight(1000);

    /* create a dummy task to store info */
    temptask_ = new BBLSubTask(nullptr);

    /* temprature fields */
    nozzle_temp = 0.0f;
    nozzle_temp_target = 0.0f;
    bed_temp = 0.0f;
    bed_temp_target = 0.0f;

    /* ams fileds */
    ams_exist_bits = 0;
    tray_exist_bits = 0;
    tray_is_bbl_bits = 0;
    is_ams_need_update = false;

    /* signals */
    wifi_signal = "";

    /* upgrade */
    force_upgrade = false;
}

bool MachineObject::check_valid_ip()
{
    if (dev_ip.empty()) {
        return false;
    }

    return true;
}

int MachineObject::command_xyz_abs()
{
    return this->publish_gcode("G90 \n");
}

int MachineObject::command_auto_leveling()
{
    return this->publish_gcode("G29 \n");
}

int MachineObject::command_go_home()
{
    return this->publish_gcode("G28 \n");
}

int MachineObject::command_fan_on()
{
    return this->publish_gcode("M106 S255 \n");
}

int MachineObject::command_fan_off()
{
    return this->publish_gcode("M106 S0 \n");
}

int MachineObject::command_task_abort()
{
    return this->publish_gcode("M0\n");
}

int MachineObject::command_task_pause()
{
    return this->publish_gcode("M400 W1\n");
}

int MachineObject::command_task_resume()
{
    return this->publish_gcode("M400 W0\n");
}

int MachineObject::command_axis_control(std::string axis, double unit, double value, int speed)
{
    char cmd[64];
    if (axis.compare("X") == 0
        || axis.compare("Y") == 0
        || axis.compare("Z") == 0) {
        sprintf(cmd, "G91 \nG0 %s%0.1f F%d\n", axis.c_str(), value * unit, speed);
    }
    else if (axis.compare("E") == 0) {
        sprintf(cmd, "M83 \nG0 %s%0.1f F%d\n", axis.c_str(), value * unit, speed);
    }
    else {
        return -1;
    }
    return this->publish_gcode(cmd);
}


void MachineObject::set_callbacks(SuccessFn sFn, FailedFn fFn, LostFn lFn)
{
    successFn = sFn;
    failedFn = fFn;
    lostFn = lFn;
}

int MachineObject::connect()
{
    if (!check_valid_ip()) {
        if (failedFn) {
            failedFn("Invalid IP!");
        }
        return -1;
    }

    try {
        if (acc_.is_user_login()) {
            if (is_connected()) {
                if (successFn) {
                    successFn("Already Connected!");
                }
                return 0;
            }
            if (mqtt_cli != nullptr) {
                if (failedFn) {
                    failedFn("Connecting state!");
                    return -1;
                }
            }

            /* lan mqtt connection */
            std::string client_id = (boost::format("%1%:%2%") % acc_.get_user_id() % mqtt_uuid).str();
            std::string report_topic = build_report_topic(dev_id);
            mqtt_cli = new mqtt::async_client(dev_ip, client_id);
            mqtt_cb = new machine_conn_callback(*mqtt_cli, mqtt_opt, this);
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
            delete mqtt_cb;
            mqtt_cb = nullptr;
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

int MachineObject::reconnect()
{
    if (conn_state == MachineObject::CONNECTION_STATE::STATE_CONNECTING)
        return 0;
    disconnect();
    connect();
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
    if (mqtt_cli == nullptr)
        conn_type = CONNECTION_WAN;
    else
        conn_type = CONNECTION_TYPE::CONNECTION_LAN;


    mqtt::async_client* client = nullptr;
    if (conn_type == CONNECTION_LAN) {
        client = mqtt_cli;
    }
    else if (conn_type == CONNECTION_WAN) {
        client = acc_.get_client();
    }
    else {
        client = nullptr;
    }

    if (client) {
        if (client->is_connected()) {
            std::string topic = (boost::format("device/%1%/request") % dev_id).str();
            json_str += '\0';
            BOOST_LOG_TRIVIAL(trace) << "publish_json topic=" << topic << ", payload=" << json_str;
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

int MachineObject::parse_json(std::string topic, std::string payload)
{
    try {
        std::stringstream ss(payload);
        pt::ptree root;
        pt::read_json(ss, root);
        if (root.empty()) {
            BOOST_LOG_TRIVIAL(trace) << "parse_json failed! topic=" << topic << ", payload = " << payload;
            return -1;
        }
        // print command
        if (root.get_child_optional("print") != boost::none) {
            pt::ptree print = root.get_child("print");
            boost::optional<std::string> command = print.get_optional<std::string>("command");
            if (!command.has_value()) return 0;
            // push_status
            if (command.value().compare("push_status") == 0) {
                /* upgrade */
                boost::optional<std::string> force_upgrade      = print.get_optional<std::string>("force_upgrade");
                if (force_upgrade.has_value()) {
                    this->force_upgrade = force_upgrade.value().compare("true") == 0 ? true : false;
                    // TODO push notification
                }
                /* gcode */
                boost::optional<std::string> gcode_start_time   = print.get_optional<std::string>("gcode_start_time");
                boost::optional<std::string> gcode_duration     = print.get_optional<std::string>("gcode_duration");
                boost::optional<std::string> gcode_file         = print.get_optional<std::string>("gcode_file");
                boost::optional<std::string> progress           = print.get_optional<std::string>("progress");
                boost::optional<std::string> gcode_state        = print.get_optional<std::string>("gcode_state");

                /* task */
                boost::optional<std::string> project_id         = print.get_optional<std::string>("project_id");
                boost::optional<std::string> profile_id         = print.get_optional<std::string>("profile_id");
                boost::optional<std::string> task_id            = print.get_optional<std::string>("task_id");
                boost::optional<std::string> subtask_id         = print.get_optional<std::string>("subtask_id");

                /* valid subtask */
                if (subtask_id.has_value() && task_id.has_value()
                    && !task_id.value().empty()
                    && (task_id.value().compare("0") != 0)) {
                    update_subtask(subtask_id.value());
                }

                BBLSubTask* curr_task = get_subtask();

                if (curr_task) {
                    if (progress.has_value())
                        curr_task->task_progress = stoi(progress.value());
                    if (gcode_start_time.has_value())
                        curr_task->task_start_time = gcode_start_time.value();
                    if (gcode_duration.has_value())
                        curr_task->task_duration = gcode_duration.value();

                    if (gcode_state.has_value())
                        curr_task->printing_status = gcode_state.value();

                    // update default subtask fields
                    if (subtask_id.has_value()) {
                        curr_task->task_id = subtask_id.value();
                    }
                    if (gcode_file.has_value()) {
                        if (curr_task == temptask_) {
                            curr_task->task_name = gcode_file.value();
                        }
                    }
                }


                /* temperature */
                boost::optional<std::string> nozzle_temp_raw        = print.get_optional<std::string>("nozzle_temp_raw");
                boost::optional<std::string> nozzle_temp_target_raw = print.get_optional<std::string>("nozzle_target_temp_raw");
                boost::optional<std::string> bed_temp_raw           = print.get_optional<std::string>("bed_temp_raw");
                boost::optional<std::string> bed_temp_target_raw    = print.get_optional<std::string>("bed_target_temp_raw");
                double temp_scale = 32.0f;
                if (nozzle_temp_raw.has_value())
                    nozzle_temp = (float)std::stoi(nozzle_temp_raw.value()) / temp_scale;

                if (nozzle_temp_target_raw.has_value())
                    nozzle_temp_target = (float)std::stoi(nozzle_temp_target_raw.value()) / temp_scale;

                if (bed_temp_raw.has_value())
                    bed_temp = (float)std::stoi(bed_temp_raw.value()) / temp_scale;

                if (bed_temp_target_raw.has_value())
                    bed_temp_target = (float)std::stoi(bed_temp_target_raw.value()) / temp_scale;

                /* deprecated protocol field
                boost::optional<std::string> nozzle_temp = print.get_optional<std::string>("nozzle_temp");
                boost::optional<std::string> nozzle_temp_target = print.get_optional<std::string>("nozzle_target_temp");
                boost::optional<std::string> bed_temp = print.get_optional<std::string>("bed_temp");
                boost::optional<std::string> bed_temp_target = print.get_optional<std::string>("bed_target_temp");
                */

                /* positions */
                boost::optional<std::string> pos_x = print.get_optional<std::string>("pos_x");
                boost::optional<std::string> pos_y = print.get_optional<std::string>("pos_y");
                boost::optional<std::string> pos_z = print.get_optional<std::string>("pos_z");
                boost::optional<std::string> pos_e = print.get_optional<std::string>("pos_e");

                /* signals */
                boost::optional<std::string> link_th        = print.get_optional<std::string>("link_th_state");
                boost::optional<std::string> link_ams       = print.get_optional<std::string>("link_ams_state");
                boost::optional<std::string> signal         = print.get_optional<std::string>("wifi_signal");
                if (signal.has_value()) {
                    wifi_signal = signal.value();
                }

                /* ams */
                try {
                    if (print.get_child_optional("ams") != boost::none) {
                        // reconnect amsList.clear();

                        // for ams changed event
                        boost::optional<std::string> ams_exist_bits_str     = print.get_optional<std::string>("ams_exist_bits");
                        boost::optional<std::string> tray_exist_bits_str    = print.get_optional<std::string>("tray_exist_bits");
                        boost::optional<std::string> tray_is_bbl_bits_str   = print.get_optional<std::string>("tray_is_bbl_bits");

                        int last_ams_exist_bits = ams_exist_bits;
                        int last_tray_exist_bits = tray_exist_bits;
                        if (ams_exist_bits_str.has_value())
                            ams_exist_bits = stoi(ams_exist_bits_str.value());
                        if (tray_exist_bits_str.has_value())
                            tray_exist_bits = stoi(tray_exist_bits_str.value());
                        if (tray_is_bbl_bits_str.has_value())
                            tray_is_bbl_bits = stoi(tray_is_bbl_bits_str.value());

                        if (ams_exist_bits != last_ams_exist_bits
                            || last_tray_exist_bits != last_tray_exist_bits) {
                            is_ams_need_update = true;
                        }
                        else {
                            is_ams_need_update = false;
                        }

                        pt::ptree ams_list = print.get_child("ams");
                        // compare ams_list
                        for (auto ams = ams_list.begin(); ams != ams_list.end(); ++ams) {
                            std::string ams_id = ams->second.get_optional<std::string>("id").value();
                            pt::ptree tray_list = ams->second.get_child("tray");

                            if (ams_id.empty()) continue;

                            Ams* curr_ams = nullptr;
                            std::map<std::string, Ams*>::iterator it = amsList.find(ams_id);
                            if (it == amsList.end()) {
                                // check valid id
                                Ams* new_ams = new Ams(ams_id);
                                amsList.insert(std::make_pair(ams_id, new_ams));
                                // new ams added event
                                curr_ams = new_ams;
                            }
                            else {
                                curr_ams = it->second;
                            }

                            if (!curr_ams) continue;

                            for (auto tray = tray_list.begin(); tray != tray_list.end(); ++tray) {
                                std::string tray_id     = tray->second.get_optional<std::string>("id").value();
                                std::string color       = tray->second.get_optional<std::string>("color").value();
                                bool is_bbl             = tray->second.get_optional<std::string>("is_bbl").value().compare("true") ? true : false;

                                boost::optional<std::string> rfid_id            = tray->second.get_optional<std::string>("rfid_id");
                                boost::optional<std::string> tray_diameter      = tray->second.get_optional<std::string>("tray_diameter");
                                boost::optional<std::string> tray_manufacturer  = tray->second.get_optional<std::string>("tray_manufacturer");
                                boost::optional<std::string> tray_meterial      = tray->second.get_optional<std::string>("tray_meterial");
                                boost::optional<std::string> tray_saturability  = tray->second.get_optional<std::string>("tray_saturability");
                                boost::optional<std::string> tray_smooth        = tray->second.get_optional<std::string>("tray_smooth");
                                boost::optional<std::string> tray_sn            = tray->second.get_optional<std::string>("tray_sn");
                                boost::optional<std::string> tray_time          = tray->second.get_optional<std::string>("tray_time");
                                boost::optional<std::string> tray_transmittance = tray->second.get_optional<std::string>("tray_transmittance");
                                boost::optional<std::string> tray_weight        = tray->second.get_optional<std::string>("tray_weight");

                                if (tray_id.empty()) continue;

                                // compare tray_list
                                AmsTray* curr_tray = nullptr;
                                std::map<std::string, AmsTray*>::iterator tray_it = curr_ams->trayList.find(tray_id);
                                if (tray_it == curr_ams->trayList.end()) {
                                    AmsTray* new_tray = new AmsTray(tray_id);
                                    curr_ams->trayList.insert(std::make_pair(tray_id, new_tray));
                                    curr_tray = new_tray;
                                }
                                else {
                                    curr_tray = tray_it->second;
                                }

                                // update properties
                                if (curr_tray) {
                                    curr_tray->update_color_from_str(color);
                                    curr_tray->sn           = tray_sn.has_value() ? tray_sn.value() : "";
                                    curr_tray->is_bbl       = is_bbl;
                                    curr_tray->meterial     = tray_meterial.has_value() ? tray_meterial.value() : "";
                                    curr_tray->saturability = tray_saturability.has_value() ? tray_saturability.value() : "";
                                    curr_tray->smooth       = tray_smooth.has_value() ? tray_smooth.value() : "";
                                    curr_tray->time         = tray_time.has_value() ? tray_time.value() : "";
                                    curr_tray->transmittance= tray_transmittance.has_value() ? tray_transmittance.value() : "";
                                    curr_tray->weight       = tray_weight.has_value() ? tray_weight.value() : "";
                                    curr_tray->manufacturer = tray_manufacturer.has_value() ? tray_manufacturer.value() : "";
                                    try {
                                        curr_tray->diameter = std::stod(tray_diameter.has_value() ? tray_diameter.value() : "0.0");
                                    }
                                    catch (...) {
                                        ;
                                    }
                                }
                            }
                        }
                    }
                }
                catch (...) {
                    ;
                }
            }
            // ack of gcode_line
            else if (command.value().compare("gcode_line") == 0) {
                boost::optional<std::string> sequence_id = print.get_optional<std::string>("sequence_id");
            }
            else if (command.value().compare("project_file") == 0) {
                boost::optional<std::string> sequence_id = print.get_optional<std::string>("sequence_id");
                BOOST_LOG_TRIVIAL(trace) << "ack of project_file " << payload;
            }
            // ack of get_version
            else if (command.value().compare("get_version") == 0) {
                pt::ptree version = root.get_child("sw_ver");
                BOOST_LOG_TRIVIAL(trace) << "parse_json, get_version topic=" << topic << ", payload = " << payload;
            }
        }
        // info command
        else if (root.get_child_optional("info") != boost::none) {
            pt::ptree info = root.get_child("info");
        }
        // upgrade push info
        else if (root.get_child_optional("upgrade") != boost::none) {
            pt::ptree upgrade = root.get_child("upgrade");
            boost::optional<std::string> upgrade_module = upgrade.get_optional<std::string>("module");
            boost::optional<std::string> upgrade_status = upgrade.get_optional<std::string>("status");
            boost::optional<std::string> upgrade_progress = upgrade.get_optional<std::string>("progress");
            boost::optional<std::string> upgrade_message = upgrade.get_optional<std::string>("message");
        }
        // event info
        else if (root.get_child_optional("event") != boost::none) {
            pt::ptree event_node = root.get_child("event");
            boost::optional<std::string> event_str = event_node.get_optional<std::string>("event");
            if (event_str.has_value()) {
                if (event_str.value().compare("client.disconnected") == 0) {
                    acc_.request_bind_list();
                }
                else if (event_str.value().compare("client.connected") == 0) {
                    acc_.request_bind_list();
                }
                else {
                    ;
                }
            }
            /* fields: client_id, username, peername, proto_name, proto_ver, connected_at, timestamp, etc */
            BOOST_LOG_TRIVIAL(trace) << "parse_json, event topic=" << topic << ", payload = " << payload;
        }
    }
    catch (...) {
        BOOST_LOG_TRIVIAL(trace) << "parse_json failed! topic=" << topic <<", payload = " << payload;
    }


    if (msg_recv_fn) {
        msg_recv_fn(topic, payload);
    }
    return 0;
}

int MachineObject::publish_gcode(std::string gcode_str)
{
    pt::ptree root, print;
    print.put("command", "gcode_line");
    print.put("param", gcode_str);
    print.put("sequence_id", MachineObject::m_sequence_id++);
    root.put_child("print", print);
    std::stringstream oss;
    pt::write_json(oss, root, false);
    std::string json_str = oss.str();

    return this->publish_json(json_str);
}

std::string get_printer_dest_file(std::string file)
{
    std::string result;

    result = "/data/";

    int name_start = file.find_last_of("\\");
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
    pt::write_json(oss, root, false);
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
    std::string src_file = task->task_file;
    std::string dst_file = get_printer_dest_file(task->task_file);
    std::string dst_file_str = dst_file;

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
            pt::write_json(oss, root, false);
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
    /* update subtask */
    subtask_ = task;

    // url is ready
    if (!task->task_url.empty()) {
        if (!task->parent_task_) return -1;
        pt::ptree root, print;
        print.put("sequence_id", MachineObject::m_sequence_id++);
        print.put("command", "project_file");
        print.put("param", task->task_gcode_in_3mf);
        print.put("url", task->task_url);   /* 3mf or gcode */
        print.put("md5", task->task_url_md5);
        /* project */
        print.put("project_id", task->parent_task_->task_project_id);
        print.put("task_id", task->parent_task_->task_id);
        print.put("subtask_id", task->task_id);
        root.put_child("print", print);
        std::stringstream oss;
        pt::write_json(oss, root, false);
        std::string json_str = oss.str();
        /* !!! remove '\' !!!! */
        json_str.erase(std::remove(json_str.begin(), json_str.end(), '\\'), json_str.end());
        this->publish_json(json_str, nullptr, CONNECTION_WAN);
        return 0;
    }

    /* upload local gcode file */
    acc_.post_task(task,
        [this, task, uploadedFn, errFn](int result, std::string info) {
            if (result == 0) {
                if (uploadedFn) {
                    uploadedFn();
                }

                pt::ptree root, print;
                print.put("sequence_id", MachineObject::m_sequence_id++);
                print.put("command", "project_file");
                print.put("param", task->task_gcode_in_3mf);
                print.put("url", task->task_url);       /* 3mf or gcode */
                print.put("md5", task->task_url_md5);
                print.put("project_id", "0");
                print.put("profile_id", "0");
                print.put("task_id", "0");
                print.put("subtask_id", task->task_id);
                root.put_child("print", print);
                std::stringstream oss;
                pt::write_json(oss, root, false);
                std::string json_str = oss.str();
                this->publish_json(json_str);
            }
            else {
                if (errFn) {
                    errFn(info);
                }
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

BBLSubTask* MachineObject::get_subtask()
{
    if (subtask_) {
        return subtask_;
    }
    else {
        return temptask_;
    }
}

void MachineObject::update_subtask(std::string subtask_id)
{
    /* create a new subtask */
    if (!subtask_) {
        acc_.get_subtask(subtask_id, subtask_);
    }
    else {
        // update to new subtask
        if (subtask_->task_id.compare(subtask_id) != 0) {
            acc_.get_subtask(subtask_id, subtask_);
        }
    }
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
        pt::write_json(oss, root, false);
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
        if (obj->dev_ip.compare(dev_ip) != 0 && !obj->dev_ip.empty()) {
            BOOST_LOG_TRIVIAL(info) << "MachineObject IP changed from " << obj->dev_ip << " to " << dev_ip;
            obj->dev_ip = dev_ip;
            /* ip changed reconnect mqtt */
            if (obj->mqtt_cli) {
                obj->reconnect();
            }
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

std::map<std::string, MachineObject*> DeviceManager::get_free_machine_list()
{
    std::map<std::string, MachineObject*> result;
    std::map<std::string, MachineObject*>::iterator it;

    for (it = localMachineList.begin(); it != localMachineList.end(); it++) {
        if (it->second->is_alive && it->second->dev_bind_status == MachineObject::MACHINE_BIND_FREE) {
            result.insert(std::make_pair(it->first, it->second));
        }
    }

    return result;
}

std::map<std::string, MachineObject*> DeviceManager::get_user_machine_list()
{
    std::map<std::string, MachineObject*> result;
    std::map<std::string, MachineObject*>::iterator it;

    for (it = localMachineList.begin(); it != localMachineList.end(); it++) {
        if (it->second->is_alive && it->second->owner.compare(acc_.get_user_name()) == 0 && !it->second->owner.empty()) {
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
