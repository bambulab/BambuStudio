#include "libslic3r/libslic3r.h"
#include "DeviceManager.hpp"
#include "libslic3r/Time.hpp"
#include "libslic3r/Thread.hpp"
#include "slic3r/Utils/Http.hpp"
#include "slic3r/Utils/Sftp.hpp"

#include "GUI_App.hpp"
#include "libslic3r/PlaceholderParser.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "Plater.hpp"

#include "nlohmann/json.hpp"
#include "slic3r/Utils/minilzo_extension.hpp"
#include <thread>
#include <mutex>
#include <codecvt>
#include <boost/foreach.hpp>
#include <boost/typeof/typeof.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

using namespace nlohmann;

// json command const string
const std::string JSON_CMD_PRINT = "print";
const std::string JSON_CMD_SYSTEM = "system";

// json key const string
const std::string JSON_MC_REMAIN_TIME   = "mc_remaining_time";
const std::string JSON_MC_PERCENT = "mc_percent";
const std::string JSON_MC_PRINT_SUB_STAGE = "mc_print_sub_stage";

const int PRINTING_STAGE_COUNT = 15;
std::string PRINTING_STAGE_STR[PRINTING_STAGE_COUNT] = {
    "printing",
    "bed_leveling",
    "heatbed_preheating",
    "xy_mech_mode_sweep",
    "change_material",
    "m400_pause",
    "filament_runout_pause",
    "hotend_heating",
    "extrude_compensation_scan",
    "bed_scan",
    "first_layer_scan",
    "be_surface_typt_idetification",
    "scanner_extrinsic_para_cali",
    "toohead_homing",
    "nozzle_tip_cleaning"
    };

inline wxString get_stage_string(int stage)
{
    switch(stage) {
    case 0:
        return _L("Printing...");
    case 1:
        return _L("The bed is auto leveling...");
    case 2:
        return _L("The hot bed is preheating...");
    case 3:
        return _L("Frequncy sweeping...");
    case 4:
        return _L("Change the filament...");
    case 5:
        return _L("Pause(M400)");
    case 6:
        return _L("Pause(Lack of filament)");
    case 7:
        return _L("The nozzle is preheating...");
    case 8:
        return _L("Extruder compensation scanning...");
    case 9:
        return _L("Bed surface scanning...");
    case 10:
        return _L("First layer scanning...");
    case 11:
        return _L("Bed surface is auto identifying...");
    case 12:
        return _L("In the calibration of extrinsic parameters");
    case 13:
        return _L("The tool head is homing...");
    case 14:
        return _L("Nozzle cleaning...");
    case 15:
        return _L("In the calibration of temperature protection");
    default:
        ;
    }
    return "";
}

static uint64_t lzo_out_len = 5 * 1024;
const uint64_t LZO_OUT_MAX_LEN = 5 * 1024;
static unsigned char lzo_out[LZO_OUT_MAX_LEN];

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

    std::string json_str;
    if (obj) {
        try {
            json_str = msg->get_payload_str();
            BOOST_LOG_TRIVIAL(trace) << "message topic:" << msg->get_topic() << ", payload=" << json_str;
        }
        catch (...) {
            ;
        }
        if (json_str.empty()) return;
        obj->parse_json(msg->get_topic(), json_str);
    }
}

void AmsTray::update_color_from_str(std::string color)
{
    if (color.empty()) return;

    if (this->color.compare(color) == 0)
        return;

    wx_color = "#" + wxString::FromUTF8(color);
    this->color = color;
}

bool HMSItem::parse_hms_info(unsigned attr, unsigned code)
{
    bool result = true;
    unsigned int model_id_int = (attr >> 24) & 0xFF;
    if (model_id_int < (unsigned) MODULE_MAX)
        this->module_id = (ModuleID)model_id_int;
    else
        this->module_id = MODULE_UKNOWN;
    this->module_num = (attr >> 16) & 0xFF;
    this->part_id    = (attr >> 8) & 0xFF;
    this->reserved   = (attr >> 0) & 0xFF;
    unsigned msg_level_int = code >> 16;
    if (msg_level_int < (unsigned)HMS_MSG_LEVEL_MAX)
        this->msg_level = (HMSMessageLevel)msg_level_int;
    else
        this->msg_level = HMS_UNKNOWN;
    this->msg_code = code & 0xFFFF;
    return result;
}

wxString HMSItem::get_module_name(ModuleID module_id)
{
    switch (module_id)
    {
    case MODULE_MC:
        return _L("MC");
    case MODULE_MAINBOARD:
        return _L("MainBoard");
    case MODULE_AMS:
        return _L("AMS");
    case MODULE_TH:
        return _L("TH");
    case MODULE_XCAM:
        return _L("XCam");
    default:
        wxString text = _L("Unknown") + wxString::Format("0x%x", (unsigned)module_id);
        return text;
    }
    return "";
}

wxString HMSItem::get_hms_msg_level_str(HMSMessageLevel level)
{
    switch(level) {
    case HMS_FATAL:
        return _L("Fatal");
    case HMS_SERIOUS:
        return _L("Serious");
    case HMS_COMMON:
        return _L("Common");
    case HMS_INFO:
        return _L("Info");
    default:
        return _L("Unknown");
    }
    return "";
}

PRINTER_TYPE MachineObject::parse_printer_type(std::string type_str)
{
    if (type_str.compare("3DPrinter-P1") == 0) {
        return PRINTER_TYPE::PRINTER_3DPrinter_P1;
    } else if (type_str.compare("3DPrinter-X1") == 0) {
        return PRINTER_TYPE::PRINTER_3DPrinter_X1;
    } else if (type_str.compare("3DPrinter-X1-Carbon") == 0) {
        return PRINTER_TYPE::PRINTER_3DPrinter_X1_Carbon;
    }

    BOOST_LOG_TRIVIAL(trace) << "unknown printer type: " << type_str;
    return PRINTER_TYPE::PRINTER_3DPrinter_UKNOWN;
}

PRINTER_TYPE MachineObject::parse_iot_printer_type(std::string type_str)
{
    if (type_str.compare("BL-P003") == 0) {
        return PRINTER_TYPE::PRINTER_3DPrinter_P1;
    } else if (type_str.compare("BL-P002") == 0) {
        return PRINTER_TYPE::PRINTER_3DPrinter_X1;
    } else if (type_str.compare("BL-P001") == 0) {
        return PRINTER_TYPE::PRINTER_3DPrinter_X1_Carbon;
    }

    BOOST_LOG_TRIVIAL(trace) << "unknown printer type: " << type_str;
    return PRINTER_TYPE::PRINTER_3DPrinter_UKNOWN;
}

wxString MachineObject::get_printer_type_display_str()
{
    if (printer_type == PRINTER_TYPE::PRINTER_3DPrinter_P1)
        return "Bambu Lab P1";
    else if (printer_type == PRINTER_TYPE::PRINTER_3DPrinter_X1)
        return "Bambu Lab X1";
    else if (printer_type == PRINTER_TYPE::PRINTER_3DPrinter_X1_Carbon)
        return "Bambu Lab X1 Carbon";
    return _L("Unknown");
}

std::string MachineObject::get_printer_type_string()
{
    if (printer_type == PRINTER_TYPE::PRINTER_3DPrinter_P1)
        return "3DPrinter-P1";
    else if (printer_type == PRINTER_TYPE::PRINTER_3DPrinter_X1)
        return "3DPrinter-X1";
    else if (printer_type == PRINTER_TYPE::PRINTER_3DPrinter_X1_Carbon)
        return "3DPrinter-X1-Carbon";
    return "3DPrinter";
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
    conn_type(CONNECTION_LAN),
    project_(nullptr),
    profile_(nullptr),
    task_(nullptr),
    subtask_(nullptr),
    temptask_(nullptr),
    is_alive(false),
    m_is_online(false),
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

    reset();

    /* temprature fields */
    nozzle_temp = 0.0f;
    nozzle_temp_target = 0.0f;
    bed_temp = 0.0f;
    bed_temp_target = 0.0f;
    chamber_temp = 0.0f;
    frame_temp = 0.0f;

    /* ams fileds */
    ams_exist_bits = 0;
    tray_exist_bits = 0;
    tray_is_bbl_bits = 0;
    is_ams_need_update = false;

    /* signals */
    wifi_signal = "";

    /* upgrade */
    upgrade_force_upgrade = false;
    upgrade_new_version = false;
    upgrade_consistency_request = false;

    /* cooling */
    heatbreak_fan_speed = 0;
    cooling_fan_speed = 0;
    big_fan1_speed = 0;
    big_fan2_speed = 0;

    /* printing */
    mc_print_stage = 0;
    mc_print_error_code = 0;
    mc_print_line_number = 0;
    mc_print_percent = 0;
    mc_print_sub_stage = 0;
    mc_left_time = 0;
    printing_speed_lvl   = PrintingSpeedLevel::SPEED_LEVEL_INVALID;
}

bool MachineObject::check_valid_ip()
{
    if (dev_ip.empty()) {
        return false;
    }

    return true;
}

void MachineObject::_parse_tray_now(std::string tray_now)
{
    m_tray_now = tray_now;
    if (tray_now.empty()) {
        return;
    } else {
        try {
            int tray_now_int = atoi(tray_now.c_str());
            if (tray_now_int >= 0 && tray_now_int < 16) {
                m_ams_id = std::to_string(tray_now_int >> 2);
                m_tray_id = std::to_string(tray_now_int & 0x3);
            }
            else if (tray_now_int == 255) {
                m_ams_id = "0";
                m_tray_id = "0";
            }
        }
        catch(...) {
        }
    }
}

Ams *MachineObject::get_curr_Ams()
{
    auto it = amsList.find(m_ams_id);
    if (it != amsList.end())
        return it->second;
    return nullptr;
}

AmsTray *MachineObject::get_curr_tray()
{
    Ams* curr_ams = get_curr_Ams();
    if (!curr_ams) return nullptr;
    
    auto it = curr_ams->trayList.find(m_tray_now);
    if (it != curr_ams->trayList.end())
        return it->second;
    return nullptr;
}

AmsTray *MachineObject::get_ams_tray(std::string ams_id, std::string tray_id)
{
    auto it = amsList.find(ams_id);
    if (it == amsList.end()) return nullptr;
    if (!it->second) return nullptr;

    auto iter = it->second->trayList.find(tray_id);
    if (iter != it->second->trayList.end())
        return iter->second;
    else
        return nullptr;
}

void MachineObject::_parse_ams_status(int ams_status)
{
    ams_status_sub = ams_status & 0xFF;
    int ams_status_main_int = (ams_status & 0xFF00) >> 8;
    if (ams_status_main_int == (int)AmsStatusMain::AMS_STATUS_MAIN_IDLE) {
        ams_status_main = AmsStatusMain::AMS_STATUS_MAIN_IDLE;
    } else if (ams_status_main_int == (int) AmsStatusMain::AMS_STATUS_MAIN_FILAMENT_CHANGE) {
        ams_status_main = AmsStatusMain::AMS_STATUS_MAIN_FILAMENT_CHANGE;
    } else if (ams_status_main_int == (int) AmsStatusMain::AMS_STATUS_MAIN_RFID_IDENTIFYING) {
        ams_status_main = AmsStatusMain::AMS_STATUS_MAIN_RFID_IDENTIFYING;
    } else if (ams_status_main_int == (int) AmsStatusMain::AMS_STATUS_MAIN_ASSIST) {
        ams_status_main = AmsStatusMain::AMS_STATUS_MAIN_ASSIST;
    } else if (ams_status_main_int == (int) AmsStatusMain::AMS_STATUS_MAIN_CALIBRATION) {
        ams_status_main = AmsStatusMain::AMS_STATUS_MAIN_CALIBRATION;
    } else if (ams_status_main_int == (int) AmsStatusMain::AMS_STATUS_MAIN_SELF_CHECK) {
        ams_status_main = AmsStatusMain::AMS_STATUS_MAIN_SELF_CHECK;
    } else if (ams_status_main_int == (int) AmsStatusMain::AMS_STATUS_MAIN_DEBUG) {
        ams_status_main = AmsStatusMain::AMS_STATUS_MAIN_DEBUG;
    } else {
        ams_status_main = AmsStatusMain::AMS_STATUS_MAIN_UNKNOWN;
    }

    BOOST_LOG_TRIVIAL(trace) << "ams_debug: main = " << ams_status_main_int << ", sub = " << ams_status_sub;
}

bool MachineObject::is_bbl_filament(std::string tag_uid)
{
    if (tag_uid.empty())
        return false;

    for (int i = 0; i < tag_uid.length(); i++) {
        if (tag_uid[i] != '0')
            return true;
    }

    return false;
}

std::string MachineObject::get_firmware_type_str()
{
    if (firmware_type == PrinterFirmwareType::FIRMWARE_TYPE_ENGINEER)
        return "engineer";
    else if (firmware_type == PrinterFirmwareType::FIRMWARE_TYPE_PRODUCTION)
        return "product";
    
    // return engineer by default;
    return "engineer";
}

bool MachineObject::is_in_upgrading()
{
    return upgrade_display_state == (int)UpgradingInProgress;
}

bool MachineObject::is_upgrading_avalable()
{
    return upgrade_display_state == (int)UpgradingAvaliable;
}

int MachineObject::get_upgrade_percent()
{
    if (upgrade_progress.empty())
        return 0;
    try {
        int result = atoi(upgrade_progress.c_str());
        return result;
    } catch(...) {
        ;
    }
    return 0;
}

std::string MachineObject::get_ota_version()
{
    auto it = module_vers.find("ota");
    if (it != module_vers.end()) {
        //double check name
        if (it->second.name == "ota") {
            return it->second.sw_ver;
        }
    }
    return "";
}

wxString MachineObject::get_upgrade_result_str(int err_code)
{
    switch(err_code) {
    case UpgradeNoError:
        return _L("Update successful.");
    case UpgradeDownloadFailed:
        return _L("Downloading failed.");
    case UpgradeVerfifyFailed:
        return _L("Verification failed.");
    case UpgradeFlashFailed:
        return _L("Update failed.");
    case UpgradePrinting:
        return _L("Update failed.");
    default:
        return _L("Update failed.");
    }
    return "";
}

std::map<int, MachineObject::ModuleVersionInfo> MachineObject::get_ams_version()
{
    std::map<int, ModuleVersionInfo> result;
    for (int i = 0; i < 4; i++) {
        std::string ams_id = "ams/" + std::to_string(i);
        auto it = module_vers.find(ams_id);
        if (it != module_vers.end()) {
            result.emplace(std::pair(i, it->second));
        }
    }
    return result;
}

wxString MachineObject::get_curr_stage()
{
    if (stage_list_info.empty()) {
        return "";
    }
    return get_stage_string(stage_curr);
}

PrintingSpeedLevel MachineObject::_parse_printing_speed_lvl(int lvl)
{
    if (lvl < (int)SPEED_LEVEL_COUNT)
        return PrintingSpeedLevel(lvl);

    return PrintingSpeedLevel::SPEED_LEVEL_INVALID;
}

bool MachineObject::is_sdcard_printing()
{
    if (can_abort() && obj_subtask_id.compare("0") == 0)
        return true;
    else
        return false;
}

int MachineObject::command_get_version()
{
    json j;
    j["info"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
    j["info"]["command"] = "get_version";
    return this->publish_json(j.dump());
}

int MachineObject::command_request_push_all()
{
    BOOST_LOG_TRIVIAL(trace) << "command_request_push_all";
    json j;
    j["pushing"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
    j["pushing"]["command"]     = "pushall";
    return this->publish_json(j.dump());
}

int MachineObject::command_upgrade_confirm()
{
    BOOST_LOG_TRIVIAL(trace) << "command_upgrade_confirm";
    json j;
    j["upgrade"]["command"] = "upgrade_confirm";
    j["upgrade"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
    j["upgrade"]["src_id"] = 1; // 1 for slicer
    return this->publish_json(j.dump());
}

int MachineObject::command_upgrade_firmware(FirmwareInfo info)
{
    std::string version     = info.version;
    std::string dst_url     = info.url;
    std::string module_name = info.module_type;

    json j;
    j["upgrade"]["command"]     = "start";
    j["upgrade"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
    j["upgrade"]["url"]         = info.url;
    j["upgrade"]["module"]      = info.module_type;
    j["upgrade"]["version"]     = info.version;
    j["upgrade"]["src_id"]      = 1;

    return this->publish_json(j.dump());
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

int MachineObject::command_control_fan(FanType fan_type, bool on_off)
{
    std::string gcode = (boost::format("M106 P%1% S%2% \n") % (int)fan_type % (on_off ? 255 : 0)).str();
    return this->publish_gcode(gcode);
}

int MachineObject::command_task_abort()
{
    json j;
    j["print"]["command"] = "stop";
    j["print"]["param"] = "";
    j["print"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);

    return this->publish_json(j.dump(), nullptr, 1);
}

int MachineObject::command_task_pause()
{
    json j;
    j["print"]["command"] = "pause";
    j["print"]["param"] = "";
    j["print"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);

    return this->publish_json(j.dump(), nullptr, 1);
}

int MachineObject::command_task_resume()
{
    json j;
    j["print"]["command"] = "resume";
    j["print"]["param"] = "";
    j["print"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);

    return this->publish_json(j.dump(), nullptr, 1);
}

int MachineObject::command_set_bed(int temp)
{
    std::string gcode_str = (boost::format("M140 S%1%\n") % temp).str();
    return this->publish_gcode(gcode_str);
}

int MachineObject::command_set_nozzle(int temp)
{
    std::string gcode_str = (boost::format("M104 S%1%\n") % temp).str();
    return this->publish_gcode(gcode_str);
}

int MachineObject::command_ams_switch(std::string tray_id, int old_temp, int new_temp)
{
    BOOST_LOG_TRIVIAL(trace) << "ams_switch to " << tray_id << " with temp: " << old_temp << ", " << new_temp;
    if (old_temp < 0) old_temp = FILAMENT_DEF_TEMP;
    if (new_temp < 0) new_temp = FILAMENT_DEF_TEMP;
    int tray_id_int = 0;
    try {
        tray_id_int = atoi(tray_id.c_str());
    }catch(...){
        return -1;
    }
    //TODO get print_config.change_filament_gcode from iot-service, get dyn_config from iot-service?
    std::string gcode = "";
    Slic3r::Print &   print = Slic3r::GUI::wxGetApp().plater()->get_partplate_list().get_current_fff_print();
    const PrintConfig &print_config = print.config();

    PlaceholderParser m_placeholder_parser;
    m_placeholder_parser = print.placeholder_parser();
    PlaceholderParser::ContextData m_placeholder_parser_context;
    DynamicConfig      dyn_config;

    int                old_filament_temp = old_temp;
    int                new_filament_temp = new_temp;
    old_filament_temp = correct_filament_temperature(old_filament_temp);
    new_filament_temp = correct_filament_temperature(new_filament_temp);
    dyn_config.set_key_value("previous_extruder", new ConfigOptionInt(-1));
    dyn_config.set_key_value("next_extruder", new ConfigOptionInt(tray_id_int));
    dyn_config.set_key_value("layer_num", new ConfigOptionInt(0));
    dyn_config.set_key_value("layer_z", new ConfigOptionFloat(0.3));
    dyn_config.set_key_value("max_layer_z", new ConfigOptionFloat(10.));
    dyn_config.set_key_value("relative_e_axis", new ConfigOptionBool(RELATIVE_E_AXIS));
    dyn_config.set_key_value("toolchange_count", new ConfigOptionInt(1));
    dyn_config.set_key_value("fan_speed", new ConfigOptionInt(0));
    dyn_config.set_key_value("old_retract_length", new ConfigOptionFloat(2.));
    dyn_config.set_key_value("new_retract_length", new ConfigOptionFloat(2.));
    dyn_config.set_key_value("old_retract_length_toolchange", new ConfigOptionFloat(3.0));
    dyn_config.set_key_value("new_retract_length_toolchange", new ConfigOptionFloat(3.0));
    dyn_config.set_key_value("old_filament_temp", new ConfigOptionInt(old_filament_temp));
    dyn_config.set_key_value("new_filament_temp", new ConfigOptionInt(new_filament_temp));
    dyn_config.set_key_value("x_after_toolchange", new ConfigOptionFloat(50.));
    dyn_config.set_key_value("y_after_toolchange", new ConfigOptionFloat(50.));
    dyn_config.set_key_value("z_after_toolchange", new ConfigOptionFloat(10.));
    dyn_config.set_key_value("first_flush_volume", new ConfigOptionFloat(5.f));
    dyn_config.set_key_value("second_flush_volume", new ConfigOptionFloat(5.f));

    try {
        std::string parsed_command = m_placeholder_parser.process(print_config.change_filament_gcode.value, tray_id_int, &dyn_config, &m_placeholder_parser_context);
        // config xyz coordinate mode
        std::string auto_home_command = "G28 X\n";
        parsed_command                = "G90\n" + auto_home_command + parsed_command;
        std::regex  match_pattern(";.*\n");
        std::string replace_pattern = "\n";
        char        result[1024]    = {0};
        std::regex_replace(result, parsed_command.begin(), parsed_command.end(), match_pattern, replace_pattern);
        result[1023] = 0;
        gcode = std::string(result);
    } catch (Exception &e) {
        BOOST_LOG_TRIVIAL(trace) << "exception, e=" << e.what();
        return -1;
    }

    return this->publish_gcode(gcode);
}

int MachineObject::command_ams_user_settings(int ams_id, bool start_read_opt, bool tray_read_opt)
{
    json j;
    j["print"]["command"] = "ams_user_setting";
    j["print"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
    j["print"]["ams_id"] = ams_id;
    j["print"]["startup_read_option"] = start_read_opt;
    j["print"]["tray_read_option"]    = tray_read_opt;

    return this->publish_json(j.dump());
}

int MachineObject::command_ams_calibrate(int ams_id)
{
    std::string gcode_cmd = (boost::format("M620 C%1% \n") % ams_id).str();
    BOOST_LOG_TRIVIAL(trace) << "ams_debug: gcode_cmd" << gcode_cmd;
    return this->publish_gcode(gcode_cmd);
}

int MachineObject::command_ams_filament_settings(int ams_id, int tray_id, std::string setting_id, std::string tray_color, int nozzle_temp)
{
    json j;
    j["print"]["command"] = "ams_filament_setting";
    j["print"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
    j["print"]["ams_id"]      = ams_id;
    j["print"]["tray_id"]     = tray_id;
    j["print"]["tray_info_idx"] = setting_id;
    // format "FFFFFFFF"   RGBA
    j["print"]["tray_color"]    = tray_color;
    j["print"]["nozzle_temp"]   = nozzle_temp;

    return this->publish_json(j.dump());
}

int MachineObject::command_ams_refresh_rfid(std::string tray_id)
{
    std::string gcode_cmd = (boost::format("M620 R%1% \n") % tray_id).str();
    BOOST_LOG_TRIVIAL(trace) << "ams_debug: gcode_cmd" << gcode_cmd;
    return this->publish_gcode(gcode_cmd);
}

int MachineObject::command_ams_select_tray(std::string tray_id)
{
    std::string gcode_cmd = (boost::format("M620 P%1% \n") % tray_id).str();
    BOOST_LOG_TRIVIAL(trace) << "ams_debug: gcode_cmd" << gcode_cmd;
    return this->publish_gcode(gcode_cmd);
}


int MachineObject::command_set_chamber_light(LIGHT_EFFECT effect, int on_time, int off_time, int loops, int interval)
{
    json j;
    j["system"]["command"] = "ledctrl";
    j["system"]["led_node"] = "chamber_light";
    j["system"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
    j["system"]["led_mode"] = light_effect_str(effect);
    j["system"]["led_on_time"] = on_time;
    j["system"]["led_off_time"] = off_time;
    j["system"]["loop_times"] = loops;
    j["system"]["interval_time"] = interval;

    return this->publish_json(j.dump());
}

int MachineObject::command_set_work_light(LIGHT_EFFECT effect, int on_time, int off_time, int loops, int interval)
{
    json j;
    j["system"]["command"] = "ledctrl";
    j["system"]["led_node"] = "work_light";
    j["system"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
    j["system"]["led_mode"] = light_effect_str(effect);
    j["system"]["led_on_time"] = on_time;
    j["system"]["led_off_time"] = off_time;
    j["system"]["loop_times"] = loops;
    j["system"]["interval_time"] = interval;

    return this->publish_json(j.dump());
}

int MachineObject::command_set_printing_speed(PrintingSpeedLevel lvl)
{
    json j;
    j["print"]["command"] = "print_speed";
    j["print"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
    j["print"]["param"] = std::to_string((int)lvl);

    return this->publish_json(j.dump());
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

int MachineObject::command_bind()
{
    std::string user_id = acc_.get_user_id();
    if (user_id.empty()) {
        return -1;
    }
    pt::ptree root, bind;
    bind.put("command", "bind");
    bind.put("dev_id", dev_id);
    bind.put("user_id", user_id);
    bind.put("sequence_id", MachineObject::m_sequence_id++);
    root.put_child("bind", bind);
    std::stringstream oss;
    pt::write_json(oss, root, false);
    std::string json_str = oss.str();

    return this->publish_json(json_str);
}

int MachineObject::_parse_login_report(std::string json_str, std::string fail_reason)
{
    try {
        json j = json::parse(json_str);
        if (j["login"]["command"].get<std::string>() == "login_report") {
            std::string status      = j["login"]["status"].get<std::string>();
            if (status == "SUCCESS") {
                return 0;
            } else if (status == "wait_auth") {
                // continue to wait
                return 1;
            } else {
                fail_reason = j["login"]["reason"].get<std::string>();
                return -1;
            }
        }
    }
    catch(...) {
    }
    return -1;
}



int MachineObject::command_new_bind()
{
    int result = 0;

    login_ticket = "";
    result = local_connect();
    if (result < 0) {
        BOOST_LOG_TRIVIAL(trace) << "login_bind: local connect failed!";
        return result;
    }

    std::string login_request = build_login_request();
    result = local_client->publish(login_request);
    if (result < 0) {
        BOOST_LOG_TRIVIAL(trace) << "login_bind: send login request failed, str = " << login_request;
        local_disconnect();
        return result;
    }

    std::string json_str;
    bool timeout = false;
    int recv_count = 0;
    while (!timeout) {
        result = local_client->recv(json_str);
        if (!json_str.empty() && result >= 0) {
            try {
                BOOST_LOG_TRIVIAL(trace) << "login_bind: json_str = " << json_str;
                json j = json::parse(json_str);
                if (j.contains("login") && !j["login"].is_null()) {
                    if (j["login"]["command"].get<std::string>() == "login_report") {
                        login_ticket       = j["login"]["ticket"].get<std::string>();
                        std::string status = j["login"]["status"].get<std::string>();
                        if (status.compare("wait_auth") == 0 && !login_ticket.empty()) {
                            break;
                        }
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
        local_disconnect();
        return -1;
    }

    unsigned int http_code = 0;
    std::string http_body;

    result = acc_.get_ticket(login_ticket, http_code, http_body);
    if (result < 0) {
        BOOST_LOG_TRIVIAL(trace) << "login_bind: http_code = " << http_code << ", http_body = " << http_body;
        local_disconnect();
        return -1;
    }

    result = acc_.post_ticket(login_ticket, http_code, http_body);
    if (result < 0) {
        BOOST_LOG_TRIVIAL(trace) << "login_bind: http_code = " << http_code << ", http_body = " << http_body;
        local_disconnect();
        return -1;
    }

    timeout = false;
    recv_count = 0;
    while (!timeout) {
        boost::this_thread::sleep_for(boost::chrono::milliseconds(1000));
        result = local_client->recv(json_str);
        if (result >= 0) {
            BOOST_LOG_TRIVIAL(trace) << "login_bind: json_str = " << json_str;
            std::string fail_reason;
            result = _parse_login_report(json_str, fail_reason);
            if (result < 0) {
                break;
            } else if (result == 0) {
                break;
            } else if (result == 1) {
                ;// continue
            }
        }
        recv_count++;
        if (recv_count > 20) { timeout = true; }
    }
    if (timeout) {
        local_disconnect();
        return -1;
    }

    local_disconnect();
    return 0;
}

std::string MachineObject::build_login_request()
{
    if (acc_.is_region_config_ready) {
        json j;
        j["login"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
        j["login"]["command"]     = "login";
        j["login"]["wifi"]        = acc_.user_region_server.wifi_code;
        j["login"]["tutk"]        = acc_.user_region_server.tutk_server_host;
        j["login"]["iot"]         = acc_.get_host();
        j["login"]["emqx"]        = acc_.get_emqx_server_host();
        return j.dump();
    } else {
        // default request
        json j;
        j["login"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
        j["login"]["command"]     = "login";
        j["login"]["wifi"]        = "DE";
        j["login"]["tutk"]        = "EU";
        j["login"]["iot"]         = acc_.get_host();
        j["login"]["emqx"]        = acc_.MQTT_HOST;
        return j.dump();
    }
}


int MachineObject::command_unbind()
{
    std::string user_id = acc_.get_user_id();
    if (user_id.empty()) {
        return -1;
    }
    pt::ptree root, bind;
    bind.put("command", "unbind");
    bind.put("dev_id", dev_id);
    bind.put("user_id", user_id);
    bind.put("sequence_id", MachineObject::m_sequence_id++);
    root.put_child("bind", bind);
    std::stringstream oss;
    pt::write_json(oss, root, false);
    std::string json_str = oss.str();

    return this->publish_json(json_str);
}


void MachineObject::set_callbacks(SuccessFn sFn, FailedFn fFn, LostFn lFn)
{
    successFn = sFn;
    failedFn = fFn;
    lostFn = lFn;
}

int MachineObject::local_connect()
{
    int result = 0;
    if (!check_valid_ip()) {
        if (failedFn) { failedFn("Invalid IP!"); }
        return -1;
    }

    local_client = new LocalClient();
    if (!local_client)
        return -1;

    result = local_client->connect(dev_ip, LOCAL_COMMU_PORT);

    return result;
}

int MachineObject::local_disconnect()
{
    int result = 0;
    if (local_client)
        result = local_client->disconnect();
    return result;
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
            if (mqtt_cli != nullptr) {
                if (mqtt_cli->is_connected()) {
                    if (successFn) {
                        successFn("Already Connected!");
                    }
                } else {
                    if (failedFn) {
                        failedFn("Connecting state!");
                        return -1;
                    }
                }
                return 0;
            }

            /* lan mqtt connection */
            std::string client_id = (boost::format("%1%:%2%") % acc_.get_user_id() % mqtt_uuid).str();
            std::string report_topic = build_report_topic(dev_id);
            mqtt_cli = new mqtt::async_client(dev_ip, client_id);
            mqtt_cb = new machine_conn_callback(*mqtt_cli, mqtt_opt, this);
            mqtt_cb->add_topics(report_topic);
            mqtt_cli->set_callback(*mqtt_cb);
            mqtt_cli->connect(mqtt_opt, this, *mqtt_cb);
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
    std::chrono::system_clock::time_point curr_time = std::chrono::system_clock::now();
    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(curr_time - last_update_time);
    if (diff.count() > DISCONNECT_TIMEOUT) {
        BOOST_LOG_TRIVIAL(trace) << "machine_object: diff count = " << diff.count();
        return false;
    }
    return true;
}

void MachineObject::set_online_state(bool on_off)
{
    m_is_online = on_off;
}

int MachineObject::publish_json(std::string json_str, ResultFn resFn, int qos)
{
    if (mqtt_cli == nullptr)
        conn_type = CONNECTION_TYPE::CONNECTION_WAN;
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
        return -1;
    }

    if (!client->is_connected()) {
        if (resFn) {
            resFn(-1, "Not connected now");
        }
        return -1;
    }

    std::string topic = (boost::format("device/%1%/request") % dev_id).str();
    json_str += '\0';
    BOOST_LOG_TRIVIAL(trace) << "publish_json topic=" << topic << ", payload=" << json_str;
    auto msg = mqtt::message::create(topic, json_str, qos, false);
    client->publish(msg);
    if (msg_send_fn) {
        msg_send_fn(topic, json_str);
    }

    return 0;
}

int MachineObject::parse_json(std::string topic, std::string payload)
{
    try {
        bool restored_json = false;
        json j;
        json j_pre = json::parse(payload);

        if (j_pre.contains(JSON_CMD_PRINT)) {
            if (j_pre["print"].contains("command")) {
                if (j_pre["print"]["command"].get<std::string>() == "push_status") {
                    if (j_pre["print"].contains("msg")) {
                        if (j_pre["print"]["msg"].get<int>() == 0) {           //all message
                            print_json.diff2all_base_reset(j_pre);
                        } else if (j_pre["print"]["msg"].get<int>() == 1) {    //diff message
                            if (print_json.diff2all(j_pre, j) == 0) {
                                restored_json = true;
                            } else {
                                BOOST_LOG_TRIVIAL(trace) << "restore failed!";
                                if (print_json.is_need_request()) {
                                    BOOST_LOG_TRIVIAL(trace) << "parse_json: need request pushall";
                                    // request new push
                                    GUI::wxGetApp().CallAfter([this] { this->command_request_push_all(); });
                                    return -1;
                                }
                            }
                        } else {
                            BOOST_LOG_TRIVIAL(warning) << "unsupported msg_type=" << j_pre["print"]["msg_type"].get<std::string>();
                        }
                    }
                }
            }
        }

        if (!restored_json) {
            j = json::parse(payload);
        }


        if (j.contains(JSON_CMD_PRINT)) {
            /* update last received time */
            last_update_time = std::chrono::system_clock::now();

            json jj = j[JSON_CMD_PRINT];
            if (jj.contains(JSON_MC_REMAIN_TIME)) {
                if (jj[JSON_MC_REMAIN_TIME].is_string())
                    mc_left_time = stoi(j[JSON_CMD_PRINT][JSON_MC_REMAIN_TIME].get<std::string>()) * 60;
                else if (jj[JSON_MC_REMAIN_TIME].is_number_integer())
                    mc_left_time = j[JSON_CMD_PRINT][JSON_MC_REMAIN_TIME].get<int>() * 60;
            }
            if (jj.contains(JSON_MC_PERCENT)) {
                if (jj[JSON_MC_PERCENT].is_string())
                    mc_print_percent = stoi(j[JSON_CMD_PRINT][JSON_MC_PERCENT].get<std::string>());
                else if (jj[JSON_MC_PERCENT].is_number_integer())
                    mc_print_percent = j[JSON_CMD_PRINT][JSON_MC_PERCENT].get<int>();
            }
            if (jj.contains(JSON_MC_PRINT_SUB_STAGE)) {
                if (jj[JSON_MC_PRINT_SUB_STAGE].is_number_integer())
                    mc_print_sub_stage = j[JSON_CMD_PRINT][JSON_MC_PRINT_SUB_STAGE].get<int>();
            }

            /* temperature */
            if (jj.contains("bed_temper")) {
                if (jj["bed_temper"].is_number_float()) {
                    bed_temp = jj["bed_temper"].get<float>();
                }
            }
            if (jj.contains("bed_target_temper")) {
                if (jj["bed_target_temper"].is_number_float()) {
                    bed_temp_target = jj["bed_target_temper"].get<float>();
                }
            }
            if (jj.contains("frame_temper")) {
                if (jj["frame_temper"].is_number_float()) {
                    frame_temp = jj["frame_temper"].get<float>();
                }
            }
            if (jj.contains("nozzle_temper")) {
                if (jj["nozzle_temper"].is_number_float()) {
                    nozzle_temp = jj["nozzle_temper"].get<float>();
                }
            }
            if (jj.contains("nozzle_target_temper")) {
                if (jj["nozzle_target_temper"].is_number_float()) {
                    nozzle_temp_target = jj["nozzle_target_temper"].get<float>();
                }
            }
            if (jj.contains("chamber_temper")) {
                if (jj["chamber_temper"].is_number_float()) {
                    chamber_temp = jj["chamber_temper"].get<float>();
                }
            }

            if (jj.contains("printer_type")) {
                printer_type = parse_printer_type(jj["printer_type"].get<std::string>());
            }

            if (jj.contains("subtask_name")) {
                subtask_name = jj["subtask_name"].get<std::string>();
            }

            /* cooling */
            if (jj.contains("cooling_fan_speed")) {
                cooling_fan_speed = stoi(jj["cooling_fan_speed"].get<std::string>());
            }
            if (jj.contains("big_fan1_speed")) {
                big_fan1_speed = stoi(jj["big_fan1_speed"].get<std::string>());
            }
            if (jj.contains("big_fan2_speed")) {
                big_fan2_speed = stoi(jj["big_fan2_speed"].get<std::string>());
            }
            if (jj.contains("heatbreak_fan_speed")) {
                heatbreak_fan_speed = stoi(jj["heatbreak_fan_speed"].get<std::string>());
            }

            /* ams status */
            try {
                if (jj.contains("ams_status")) {
                    int ams_status = jj["ams_status"].get<int>();
                    this->_parse_ams_status(ams_status);
                }
            }
            catch(...) {
                ;
            }

            /* parse speed */
            try {
                if (jj.contains("spd_lvl")) {

                    printing_speed_lvl = (PrintingSpeedLevel)jj["spd_lvl"].get<int>();
                }
                if (jj.contains("spd_mag")) {
                    printing_speed_mag = jj["spd_mag"].get<int>();
                }
            }
            catch(...) {
                ;
            }

            try {
                if (jj.contains("stg")) {
                    stage_list_info.clear();
                    if (jj["stg"].is_array()) {
                        for (auto it = jj["stg"].begin(); it != jj["stg"].end(); it++) {
                            for (auto kv = (*it).begin(); kv != (*it).end(); kv++) {
                                stage_list_info.push_back(kv.value().get<int>());
                            }
                        }
                    }
                }
                if (jj.contains("stg_cur")) {
                    stage_curr = jj["stg_cur"].get<int>();
                }
            } catch(...) {
                ;
            }

            /* get fimware type */
            try {
                if (jj.contains("lifecycle")) {
                    if (jj["lifecycle"].get<std::string>() == "engineer")
                        firmware_type = PrinterFirmwareType::FIRMWARE_TYPE_ENGINEER;
                    else if (jj["lifecycle"].get<std::string>() == "product")
                        firmware_type = PrinterFirmwareType::FIRMWARE_TYPE_PRODUCTION;
                }
            } catch(...) {
                ;
            }

            try {
                if (jj.contains("lights_report") && jj["lights_report"].is_array()) {
                    for (auto it = jj["lights_report"].begin(); it != jj["lights_report"].end(); it++) {
                        if ((*it)["node"].get<std::string>().compare("chamber_light") == 0)
                            chamber_light = light_effect_parse((*it)["mode"].get<std::string>());
                        if ((*it)["node"].get<std::string>().compare("work_light") == 0)
                            work_light = light_effect_parse((*it)["mode"].get<std::string>());
                    }
                }
            } catch (...) {
                ;
            }

            try {
                if (jj.contains("upgrade_state")) {
                    if (jj["upgrade_state"].contains("status"))
                        upgrade_status = jj["upgrade_state"]["status"].get<std::string>();
                    if (jj["upgrade_state"].contains("progress")) {
                        upgrade_progress = jj["upgrade_state"]["progress"].get<std::string>();
                    } if (jj["upgrade_state"].contains("new_version_state"))
                        upgrade_new_version = jj["upgrade_state"]["new_version_state"].get<int>() == 1 ? true : false;
                    if (jj["upgrade_state"].contains("ams_new_version_number"))
                        ams_new_version_number = jj["upgrade_state"]["ams_new_version_number"].get<std::string>();
                    if (jj["upgrade_state"].contains("ota_new_version_number"))
                        ota_new_version_number = jj["upgrade_state"]["ota_new_version_number"].get<std::string>();
                    if (jj["upgrade_state"].contains("module"))
                        upgrade_module = jj["upgrade_state"]["module"].get<std::string>();
                    if (jj["upgrade_state"].contains("message"))
                        upgrade_message = jj["upgrade_state"]["message"].get<std::string>();
                    if (jj["upgrade_state"].contains("consistency_request"))
                        upgrade_consistency_request = jj["upgrade_state"]["consistency_request"].get<bool>();
                    if (jj["upgrade_state"].contains("force_upgrade"))
                        upgrade_force_upgrade = jj["upgrade_state"]["force_upgrade"].get<bool>();
                    if (jj["upgrade_state"].contains("err_code"))
                        upgrade_err_code = jj["upgrade_state"]["err_code"].get<int>();
                    if (jj["upgrade_state"].contains("dis_state"))
                        upgrade_display_state = jj["upgrade_state"]["dis_state"].get<int>();
                    else {
                        //BBS compatibility with old version
                        if (upgrade_status == "DOWNLOADING"
                            || upgrade_status == "FLASHING"
                            || upgrade_status == "UPGRADE_REQUEST"
                            || upgrade_status == "PRE_FLASH_START"
                            || upgrade_status == "PRE_FLASH_SUCCESS") {
                            upgrade_display_state = (int) UpgradingDisplayState::UpgradingInProgress;
                        } else if (upgrade_status == "UPGRADE_SUCCESS"
                                    || upgrade_status == "DOWNLOAD_FAIL"
                                    || upgrade_status == "FLASH_FAIL"
                                    || upgrade_status == "PRE_FLASH_FAIL"
                                    || upgrade_status == "UPGRADE_FAIL") {
                            upgrade_display_state = (int) UpgradingDisplayState::UpgradingFinished;
                        } else {
                            if (upgrade_new_version) {
                                upgrade_display_state = (int) UpgradingDisplayState::UpgradingAvaliable;
                            } else {
                                upgrade_display_state = (int) UpgradingDisplayState::UpgradingUnavaliable;
                            }
                        }
                    }
                }
            } catch (...) {
                ;
            }

            try {
                hms_list.clear();
                if (jj.contains("hms")) {
                    if (jj["hms"].is_array()) {
                        for (auto it = jj["hms"].begin(); it != jj["hms"].end(); it++) {
                            HMSItem item;
                            if ((*it).contains("attr") && (*it).contains("code")) {
                                unsigned attr = (*it)["attr"].get<unsigned>();
                                unsigned code = (*it)["code"].get<unsigned>();
                                item.parse_hms_info(attr, code);
                            }
                            hms_list.push_back(item);
                        }
                    }
                }
            } catch (...) {
                ;
            }
        }
        try {
            if (j.contains("info")) {
                if (j["info"].contains("command") && j["info"]["command"].get<std::string>() == "get_version") {
                    json j_module = j["info"]["module"];
                    module_vers.clear();
                    for (auto it = j_module.begin(); it != j_module.end(); it++) {
                        ModuleVersionInfo ver_info;
                        ver_info.name = (*it)["name"].get<std::string>();
                        if ((*it).contains("sw_ver"))
                            ver_info.sw_ver = (*it)["sw_ver"].get<std::string>();
                        if ((*it).contains("sn"))
                            ver_info.sn = (*it)["sn"].get<std::string>();
                        if ((*it).contains("hw_ver"))
                            ver_info.hw_ver = (*it)["hw_ver"].get<std::string>();
                        module_vers.emplace(ver_info.name, ver_info);
                    }
                }
            }
        } catch (...) {}

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
                    this->upgrade_force_upgrade = force_upgrade.value().compare("true") == 0 ? true : false;
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

                // can query users info
                bool query_user = true;
                if (acc_.get_curr_user() && is_local()) {
                    if (!bind_user_id.empty() && bind_user_id.compare(acc_.get_curr_user()->get_user_id()) != 0)
                        query_user = false;
                }

                if (query_user) {
                    /* sync project and profile info */
                    if (project_id.has_value() && !project_id.value().empty() && (project_id.value().compare("0") != 0)
                        && profile_id.has_value() && !profile_id.value().empty() && (profile_id.value().compare("0") != 0)
                        )
                    {
                        update_profile(project_id.value(), profile_id.value());
                    }

                    /* sync task info */
                    if (task_id.has_value() && !task_id.value().empty() && (task_id.value().compare("0") != 0))
                    {
                        update_task(task_id.value());
                    }

                    /* valid subtask */
                    if (subtask_id.has_value() && !subtask_id.value().empty()) {
                        if (subtask_id.value().compare("0") != 0) {
                            update_subtask(subtask_id.value());
                        }
                        obj_subtask_id = subtask_id.value();
                    }

                    BBLSubTask* curr_task = get_subtask();

                    if (curr_task) {
                        if (mc_left_time == 0 && mc_print_percent == 0)
                            curr_task->task_progress = stoi(progress.has_value() ? progress.value() : "0");
                        else
                            curr_task->task_progress = mc_print_percent;

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
                }


                /* printing */
                boost::optional<int> mc_print_stage_str        = print.get_optional<int>("mc_print_stage");
                boost::optional<int> mc_print_error_code_str   = print.get_optional<int>("mc_print_error_code");
                boost::optional<int> mc_print_line_number_str  = print.get_optional<int>("mc_print_line_number");
                if (mc_print_stage_str.has_value()) {
                    mc_print_stage = mc_print_stage_str.value();
                }
                if (mc_print_error_code_str.has_value()) {
                    mc_print_error_code = mc_print_error_code_str.value();
                }
                if (mc_print_line_number_str.has_value()) {
                    mc_print_line_number = mc_print_line_number_str.value();
                }

                if (gcode_state.has_value()) {
                    this->set_print_state(gcode_state.value());
                }

                /* signals */
                boost::optional<std::string> link_th        = print.get_optional<std::string>("link_th_state");
                boost::optional<std::string> link_ams       = print.get_optional<std::string>("link_ams_state");
                boost::optional<std::string> signal         = print.get_optional<std::string>("wifi_signal");
                if (signal.has_value()) {
                    wifi_signal = signal.value();
                }

                /* ams */
                try {
                    auto ams = print.get_child_optional("ams");
                    if (ams != boost::none && ams.value().get_child_optional("ams").has_value()) {
                        auto &print = ams.value();
                        // reconnect amsList.clear();

                        // for ams changed event
                        boost::optional<std::string> ams_exist_bits_str     = print.get_optional<std::string>("ams_exist_bits");
                        boost::optional<std::string> tray_exist_bits_str    = print.get_optional<std::string>("tray_exist_bits");
                        boost::optional<std::string> tray_read_done_bits_str= print.get_optional<std::string>("tray_read_done_bits");
                        boost::optional<std::string> tray_is_bbl_bits_str   = print.get_optional<std::string>("tray_is_bbl_bits");
                        boost::optional<std::string> tray_now_str           = print.get_optional<std::string>("tray_now");
                        boost::optional<std::string> tray_tar_str           = print.get_optional<std::string>("tray_tar");

                        long int last_ams_exist_bits = ams_exist_bits;
                        long int last_tray_exist_bits = tray_exist_bits;
                        long int last_is_bbl_bits     = tray_is_bbl_bits;
                        long int last_read_done_bits  = tray_read_done_bits;
                        if (ams_exist_bits_str.has_value())
                            ams_exist_bits = stol(ams_exist_bits_str.value(), nullptr, 16);
                        if (tray_exist_bits_str.has_value())
                            tray_exist_bits = stol(tray_exist_bits_str.value(), nullptr, 16);
                        if (tray_is_bbl_bits_str.has_value())
                            tray_is_bbl_bits = stol(tray_is_bbl_bits_str.value(), nullptr, 16);
                        if (tray_read_done_bits_str.has_value())
                            tray_read_done_bits = stol(tray_read_done_bits_str.value(), nullptr, 16);

                        if (tray_now_str.has_value()) {
                            this->_parse_tray_now(tray_now_str.value());
                            
                        }
                        if (tray_tar_str.has_value())
                            m_tray_tar = tray_tar_str.value();

                        if (ams_exist_bits != last_ams_exist_bits
                            || last_tray_exist_bits != last_tray_exist_bits
                            || tray_is_bbl_bits != last_is_bbl_bits ||
                            tray_read_done_bits != last_read_done_bits) {
                            is_ams_need_update = true;
                        }
                        else {
                            is_ams_need_update = false;
                        }

                        pt::ptree ams_list = print.get_child("ams");
                        // compare ams_list
                        for (auto ams = ams_list.begin(); ams != ams_list.end(); ++ams) {
                            std::string ams_id = ams->second.get_optional<std::string>("id").value();
                            if (ams_id.empty()) continue;

                            Ams* curr_ams = nullptr;
                            std::map<std::string, Ams*>::iterator it = amsList.find(ams_id);
                            if (it == amsList.end()) {
                                // check valid id
                                Ams* new_ams = new Ams(ams_id);
                                try {
                                    if (!ams_id.empty()) {
                                        int ams_id_int       = atoi(ams_id.c_str());
                                        new_ams->is_exists   = (ams_exist_bits & (1 << ams_id_int)) != 0 ? true : false;
                                    }
                                } catch (...) {
                                    ;
                                }
                                amsList.insert(std::make_pair(ams_id, new_ams));
                                // new ams added event
                                curr_ams = new_ams;
                            }
                            else {
                                curr_ams = it->second;
                            }

                            if (!curr_ams) continue;

                            if (!ams->second.get_child_optional("tray").has_value()) continue;

                            pt::ptree tray_list = ams->second.get_child("tray");
                            for (auto tray = tray_list.begin(); tray != tray_list.end(); ++tray) {
                                std::string tray_id     = tray->second.get_optional<std::string>("id").value();
                                boost::optional<std::string> id                 = tray->second.get_optional<std::string>("id");
                                boost::optional<std::string> tag_uid            = tray->second.get_optional<std::string>("tag_uid");
                                boost::optional<std::string> tray_info_idx      = tray->second.get_optional<std::string>("tray_info_idx");
                                boost::optional<std::string> tray_type          = tray->second.get_optional<std::string>("tray_type");
                                boost::optional<std::string> tray_sub_brands    = tray->second.get_optional<std::string>("tray_sub_brands");
                                boost::optional<std::string> tray_color         = tray->second.get_optional<std::string>("tray_color");
                                boost::optional<std::string> tray_weight        = tray->second.get_optional<std::string>("tray_weight");
                                boost::optional<std::string> tray_diameter      = tray->second.get_optional<std::string>("tray_diameter");
                                boost::optional<std::string> tray_temp          = tray->second.get_optional<std::string>("tray_temp");
                                boost::optional<std::string> tray_time          = tray->second.get_optional<std::string>("tray_time");
                                boost::optional<std::string> bed_temp_type      = tray->second.get_optional<std::string>("bed_temp_type");
                                boost::optional<std::string> bed_temp           = tray->second.get_optional<std::string>("bed_temp");
                                boost::optional<std::string> hot_end_temp_max   = tray->second.get_optional<std::string>("hot_end_temp_max");
                                boost::optional<std::string> hot_end_temp_limit = tray->second.get_optional<std::string>("hot_end_temp_limit");
                                boost::optional<std::string> xcam_info          = tray->second.get_optional<std::string>("xcam_info");
                                boost::optional<std::string> tray_uuid          = tray->second.get_optional<std::string>("tray_uuid");

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
                                    curr_tray->id           = id.has_value() ? id.value() : "";
                                    curr_tray->tag_uid      = tag_uid.has_value() ? tag_uid.value() : "";
                                    curr_tray->setting_id   = tray_info_idx.has_value() ? tray_info_idx.value() : "";
                                    curr_tray->type         = tray_type.has_value() ? tray_type.value() : "";
                                    curr_tray->sub_brands   = tray_sub_brands.has_value() ? tray_sub_brands.value() : "";
                                    curr_tray->weight       = tray_weight.has_value() ? tray_weight.value() : "";
                                    curr_tray->diameter     = tray_diameter.has_value() ? tray_diameter.value() : "";
                                    curr_tray->temp         = tray_temp.has_value() ? tray_temp.value() : "";
                                    curr_tray->time         = tray_time.has_value() ? tray_time.value() : "";
                                    curr_tray->bed_temp_type = bed_temp_type.has_value() ? bed_temp_type.value() : "";
                                    curr_tray->bed_temp      = bed_temp.has_value() ? bed_temp.value() : "";
                                    curr_tray->hot_end_temp_max = hot_end_temp_max.has_value() ? hot_end_temp_max.value() : "";
                                    curr_tray->hot_end_temp_limit = hot_end_temp_limit.has_value() ? hot_end_temp_limit.value() : "";
                                    curr_tray->xcam_info          = xcam_info.has_value() ? xcam_info.value() : "";
                                    curr_tray->uuid               = tray_uuid.has_value() ? tray_uuid.value() : "";
                                    auto color = tray_color.has_value() ? tray_color.value() : "";
                                    curr_tray->update_color_from_str(color);
                                    try {
                                        if (!ams_id.empty() && !curr_tray->id.empty()) {
                                            int ams_id_int = atoi(ams_id.c_str());
                                            int tray_id_int = atoi(curr_tray->id.c_str());
                                            curr_tray->is_exists = (tray_exist_bits & (1 << (ams_id_int * 4 + tray_id_int))) != 0 ? true : false;
                                        }
                                    } catch(...) {
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
        }
        // upgrade push info move to print push status
        /*else if (root.get_child_optional("upgrade") != boost::none) {
            pt::ptree upgrade = root.get_child("upgrade");
            boost::optional<std::string> upgrade_module         = upgrade.get_optional<std::string>("module");
            boost::optional<std::string> upgrade_status_val     = upgrade.get_optional<std::string>("status");
            boost::optional<std::string> upgrade_progress_val   = upgrade.get_optional<std::string>("progress");
            boost::optional<std::string> upgrade_message_val    = upgrade.get_optional<std::string>("message");
            boost::optional<bool> new_version                   = upgrade.get_optional<bool>("new_version");
            boost::optional<bool> consistency_request           = upgrade.get_optional<bool>("consistency_request");
            if (new_version.has_value())
                upgrade_new_version = new_version.value();
            if (upgrade_progress_val.has_value())
                upgrade_progress = upgrade_progress_val.value();
            if (upgrade_message_val.has_value())
                upgrade_message = upgrade_message_val.value();
            if (upgrade_status_val.has_value())
                upgrade_status = upgrade_status_val.value();
            if (consistency_request.has_value())
                upgrade_consistency_request = consistency_request.value();

        }*/
        // event info
        else if (root.get_child_optional("event") != boost::none) {
            pt::ptree event_node = root.get_child("event");
            boost::optional<std::string> event_str = event_node.get_optional<std::string>("event");
            if (event_str.has_value()) {
                if (event_str.value().compare("client.disconnected") == 0) {
                    set_online_state(true);
                }
                else if (event_str.value().compare("client.connected") == 0) {
                    set_online_state(false);
                }
                else {
                    ;
                }
            }
            /* fields: client_id, username, peername, proto_name, proto_ver, connected_at, timestamp, etc */
            BOOST_LOG_TRIVIAL(trace) << "parse_json, event topic=" << topic << ", payload = " << payload;
        }

        else if (root.get_child_optional("bind") != boost::none) {
            pt::ptree bind = root.get_child("bind");
            boost::optional<std::string> command = bind.get_optional<std::string>("command");
            boost::optional<std::string> result = bind.get_optional<std::string>("result");
            boost::optional<std::string> reason = bind.get_optional<std::string>("reason");
            boost::optional<std::string> dev_id = bind.get_optional<std::string>("dev_id");
            boost::optional<std::string> user_id = bind.get_optional<std::string>("user_id");
            if (command.has_value())
            {
                if (command.value().compare("bind") == 0) {
                    ;
                }
                else if (command.value().compare("unbind") == 0) {
                    ;
                }
            }

            BOOST_LOG_TRIVIAL(trace) << "parse_json, bind topic=" << topic << ", payload = " << payload;
        }
        else if (root.get_child_optional("system") != boost::none) {
            pt::ptree system = root.get_child("system");
            try {
                if (system.get_child_optional("lights") != boost::none) {
                    pt::ptree light_list = system.get_child("lights");
                    for (auto light_node = light_list.begin(); light_node != light_list.end(); ++light_node) {
                        boost::optional<std::string> led_node = light_node->second.get_optional<std::string>("node");
                        boost::optional<std::string> led_mode = light_node->second.get_optional<std::string>("mode");
                        if (led_node.has_value() && led_mode.has_value()) {
                            if (led_node.value().compare("chamber_light") == 0)
                                chamber_light = light_effect_parse(led_mode.value());
                            else if (led_node.value().compare("work_light") == 0)
                                work_light = light_effect_parse(led_mode.value());
                        }
                    }
                }
            }
            catch (...) {
                ;
            }
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
    //can not publish gcode when logout
    if (!acc_.is_user_login()) {
        return -1;
    }
    Slic3r::AccountInfo* info = acc_.get_curr_user();
    if (!info) return -1;

    pt::ptree root, print;
    print.put("command", "gcode_line");
    print.put("param", gcode_str);
    print.put("sequence_id", MachineObject::m_sequence_id++);
    print.put("user_id", info->get_user_id());
    root.put_child("print", print);
    std::stringstream oss;
    pt::write_json(oss, root, false);
    std::string json_str = oss.str();

    return publish_json(json_str);
}

std::string get_printer_dest_file(std::string file)
{
    std::string result = "/data/";
    boost::filesystem::path path(file);
    return result + path.filename().string();
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
    json j;
    j["print"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
    j["print"]["command"] = "gcode_file";
    j["print"]["project_id"] = task->task_project_id;
    j["print"]["profile_id"] = task->task_profile_id;
    j["print"]["url"] = task->task_url;
    j["print"]["md5"] = task->task_url_md5;
    j["print"]["task_id"] = task->task_id;
    j["print"]["subtask_id"] = "0";

    this->publish_json(j.dump(), nullptr, 1);
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

    if (!boost::filesystem::exists(src_file)) {
        BOOST_LOG_TRIVIAL(trace) << "src_file=" << src_file << "is not exist";
        return -1;
    }

    BOOST_LOG_TRIVIAL(trace) << "sftp upload dep_ip = " << dev_ip << ", src_file:" << src_file << ", dst_file:" << dst_file;
    Sftp sftp = Sftp::upload(dev_ip, src_file, dst_file, "root", "root");

    sftp.on_complete(
        [this, src_file, dst_file_str, task, uploadedFn](std::string body) {
            /* boost::filesystem::file_size not right */
            if (uploadedFn) {
                uploadedFn();
            }

            pt::ptree root, print;
            if (boost::iends_with(src_file, ".3mf")) {
                BOOST_LOG_TRIVIAL(trace) << "transform 3mf ok!";
                print.put("sequence_id", MachineObject::m_sequence_id++);
                print.put("command", "project_file");
                print.put("param", task->task_gcode_in_3mf);
                print.put("url", task->task_url);   /* 3mf or gcode */
                print.put("md5", task->task_url_md5);
                /* project */
                print.put("project_id", "0");
                print.put("profile_id", "0");
                print.put("task_id", "0");
                print.put("subtask_id", "0");
                root.put_child("print", print);
            }
            else {
                BOOST_LOG_TRIVIAL(trace) << "transform gcode ok!";
                print.put("sequence_id", MachineObject::m_sequence_id++);
                print.put("command", "gcode_file");
                print.put("param", dst_file_str);
                /* project */
                root.put_child("print", print);
            }
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

    if (task->task_url.empty()) {
        BOOST_LOG_TRIVIAL(trace) << "task_url is empty!";
        return -1;
    }
    if (!task->parent_task_) return -1;

    json j;
    j["print"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
    j["print"]["command"]       = "project_file";
    j["print"]["param"]         = task->task_gcode_in_3mf;
    j["print"]["url"]           = task->task_url;
    j["print"]["md5"]           = task->task_url_md5;
    j["print"]["project_id"]    = task->parent_task_->task_project_id;
    j["print"]["profile_id"]    = task->parent_task_->task_profile_id;
    j["print"]["task_id"]       = task->parent_task_->task_id;
    j["print"]["subtask_id"]    = task->task_id;
    j["print"]["bed_leveling"]      = task->task_bed_leveling;
    j["print"]["bed_type"]          = task->task_bed_type;
    j["print"]["flow_cali"]         = task->task_flow_cali;
    j["print"]["vibration_cali"]    = task->task_vibration_cali;

    std::string json_str = j.dump();
    json_str.erase(std::remove(json_str.begin(), json_str.end(), '\\'), json_str.end());
    return this->publish_json(json_str, nullptr, 1);
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

BBLSliceInfo* MachineObject::get_slice_info(std::string plate_idx)
{
    if (!profile_)
        return nullptr;

    return profile_->get_slice_info(plate_idx);
}

void MachineObject::update_profile(std::string project_id, std::string profile_id)
{
    if (project_id.empty() || profile_id.empty()) return;

    if (project_ && profile_) {
        if (project_->project_id.compare(project_id) == 0 && 
            profile_->profile_id.compare(profile_id) == 0) {
            return;
        }
    }

    /* create new project and profile */
    project_ = new BBLProject();
    project_->project_id = project_id;
    profile_ = new BBLProfile(project_);
    profile_->profile_id = profile_id;
    acc_.get_profile(project_, profile_);
}

void MachineObject::update_task(std::string task_id)
{
    if (task_id.empty()) return;

    if (task_) {
        if (task_->task_id.compare(task_id) == 0) return;
    }

    /* create new task */
    task_ = new BBLTask();
    task_->task_id = task_id;
    acc_.get_task(task_);
}

void MachineObject::update_subtask(std::string subtask_id)
{
    if (subtask_id.empty()) return;

    if (subtask_ && subtask_->task_id.compare(subtask_id) == 0) {
        return;
    }

    /* create a new subtask */
    subtask_ = new BBLSubTask();
    subtask_->task_id = subtask_id;

    acc_.get_subtask(subtask_);

    // modify to user-service api
    /*unsigned http_code;
    std::string http_body;
    unsigned limit = 1;
    int         result = acc_.get_tasks(this->dev_id, limit, http_code, http_body);
    if (result == 0) {
        BOOST_LOG_TRIVIAL(trace) << "parse_task_info: " << http_body;
        try {
            json j = json::parse(http_body);
            if (j.contains("hits") && !j["hits"].is_null() && j["hits"].is_array()) {
                for (auto task = j["hits"].begin(); task != j["hits"].end(); task++) {
                    int task_id = (*task)["title"].get<int>();
                    if (std::to_string(task_id).compare(subtask_->task_id) == 0) {
                        subtask_->task_name          = (*task)["title"].get<std::string>();
                        subtask_->task_partplate_idx = std::to_string((*task)["plateIndex"].get<int>());
                        subtask_->task_weightF       = (*task)["weight"].get<double>();
                        subtask_->task_thumbnail_url = (*task)["cover"].get<std::string>();
                        subtask_->task_status        = BBLSubTask::parse_user_service_task_status((*task)["status"].get<int>());
                        subtask_->task_start_time    = (*task)["startTime"].get<std::string>();
                        subtask_->task_end_time      = (*task)["endTime"].get<std::string>();
                    } else {
                        BOOST_LOG_TRIVIAL(trace) << "parse_task_info: task_id mismatch curr = " << subtask_->task_id;
                    }
                }
            }
        }
        catch (...) {
            ;
        }
    }
    else {
        BOOST_LOG_TRIVIAL(trace) << "parse_task_info: failed, status = " << http_code << ", body = " << http_body;
    }*/
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

bool MachineObject::get_firmware_info()
{
    int          result = 0;
    unsigned int http_code;
    std::string  http_body;
    result = acc_.get_machine_version(dev_id, http_code, http_body);
    if (result < 0) {
        // get upgrade list failed
        return false;
    }
    try {
        json j = json::parse(http_body);
        if (j.contains("devices") && !j["devices"].is_null()) {
            firmware_list.clear();
            for (json::iterator it = j["devices"].begin(); it != j["devices"].end(); it++) {
                if ((*it)["dev_id"].get<std::string>() == this->dev_id) {
                    try {
                        json firmware = (*it)["firmware"];
                        for (json::iterator firmware_it = firmware.begin(); firmware_it != firmware.end(); firmware_it++) {
                            FirmwareInfo item;
                            item.version     = (*firmware_it)["version"].get<std::string>();
                            item.url         = (*firmware_it)["url"].get<std::string>();
                            if ((*firmware_it).contains("description"))
                                item.description = (*firmware_it)["description"].get<std::string>();
                            item.module_type = "ota";
                            int name_start   = item.url.find_last_of('/') + 1;
                            if (name_start > 0) {
                                item.name = item.url.substr(name_start, item.url.length() - name_start);
                                firmware_list.push_back(item);
                            } else {
                                BOOST_LOG_TRIVIAL(trace) << "skip";
                            }
                        }
                    } catch (...) {}
                    try {
                        if ((*it).contains("ams")) {
                            json ams_list = (*it)["ams"];
                            if (ams_list.size() > 0) {
                                auto ams_front    = ams_list.front();
                                json firmware_ams = (ams_front)["firmware"];
                                for (json::iterator ams_it = firmware_ams.begin(); ams_it != firmware_ams.end(); ams_it++) {
                                    FirmwareInfo item;
                                    item.version   = (*ams_it)["version"].get<std::string>();
                                    item.url       = (*ams_it)["url"].get<std::string>();
                                    if ((*ams_it).contains("description"))
                                        item.description = (*ams_it)["description"].get<std::string>();
                                    item.module_type = "ams";
                                    int name_start = item.url.find_last_of('/') + 1;
                                    if (name_start > 0) {
                                        item.name = item.url.substr(name_start, item.url.length() - name_start);
                                        firmware_list.push_back(item);
                                    } else {
                                        BOOST_LOG_TRIVIAL(trace) << "skip";
                                    }
                                }
                            }
                        }
                    } catch (...) {
                        ;
                    }
                }
            }
        }
    }
    catch(...) {
        return false;
    }
    return true;
}

void MachineObject::set_bind_status(std::string status)
{
    bind_user_name = status;
}

void MachineObject::set_connect_state(CONNECTION_STATE state)
{
    conn_state = state;
}

std::string MachineObject::get_bind_str()
{
    std::string default_result = "N/A";
    if (bind_user_name.compare("null") == 0) {
        return "Free";
    }
    else if (!bind_user_name.empty()) {
        return bind_user_name;
    }
    return default_result;
}

bool MachineObject::can_print()
{
    if (print_status.compare("RUNNING") == 0) {
        return false;
    }
    if (print_status.compare("IDLE") == 0 || print_status.compare("FINISH") == 0) {
        return true;
    }
    return true;
}

bool MachineObject::can_resume()
{
    if (print_status.compare("PAUSE") == 0)
        return true;
    return false;
}

bool MachineObject::can_pause()
{
    if (print_status.compare("RUNNING") == 0)
        return true;
    return false;
}

bool MachineObject::can_abort()
{
    if (print_status.compare("PAUSE") == 0
        || print_status.compare("RUNNING") == 0
        || print_status.compare("PREPARE") == 0) {
        return true;
    }
    return false;
}

bool MachineObject::is_printing_finished()
{
    if (print_status.compare("FINISH") == 0
        || print_status.compare("FAILED") == 0) {
        return true;
    }
    return false;
}

void MachineObject::reset()
{
    last_update_time = std::chrono::system_clock::now();
}

void MachineObject::set_print_state(std::string status)
{
    print_status = status;
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

void DeviceManager::on_machine_alive(std::string dev_name, std::string dev_id, std::string dev_ip, std::string printer_type_str, std::string printer_signal)
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
        obj->wifi_signal = printer_signal;
        BOOST_LOG_TRIVIAL(info) << "SsdpDiscovery:: Update Machine Info, printer_sn = " << dev_id << ", signal = " << printer_signal;
        obj->last_alive = Slic3r::Utils::get_current_time_utc();
        obj->is_alive = true;
    }
    else {
        /* insert a new machine */
        obj = new MachineObject(acc_, dev_name, dev_id, dev_ip);
        obj->printer_type = MachineObject::parse_printer_type(printer_type_str);
        obj->wifi_signal = printer_signal;
        localMachineList.insert(std::make_pair(dev_id, obj));

        BOOST_LOG_TRIVIAL(info) << "SsdpDiscovery::New Machine, ip = " << dev_ip << ", printer_name= " << dev_name << ", printer_type = " << printer_type_str << ", signal = " << printer_signal;
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

    acc_.query_bind_status(query_list, cFn,
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

std::map<std::string, MachineObject*> DeviceManager::get_user_machine_list()
{
    std::map<std::string, MachineObject*> result;
    std::map<std::string, MachineObject*>::iterator it;

    for (it = localMachineList.begin(); it != localMachineList.end(); it++) {
        if (it->second->is_alive && it->second->bind_user_name.compare(acc_.get_user_name()) == 0 && !it->second->bind_user_name.empty()) {
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
                it->second->is_alive = false;
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
