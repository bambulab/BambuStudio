#include "PrintJob.hpp"
#include <regex>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <condition_variable>
#include <mutex>
#include "libslic3r/MTUtils.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "bambu_networking.hpp"

#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/DeviceCore/DevUtil.h"

#include "slic3r/Utils/FileTransferUtils.hpp"

namespace Slic3r {
namespace GUI {

#define CHECK_GCODE_FAILED_STR      _L("Abnormal print file data. Please slice again.")
#define PRINTJOB_CANCEL_STR         _L("Task canceled.")
#define TIMEOUT_TO_UPLOAD_STR       _L("Upload task timed out. Please check the network status and try again.")
#define FAILED_IN_CLOUD_SERVICE_STR _L("Cloud service connection failed. Please try again.")
#define FILE_IS_NOT_EXISTS_STR      _L("Print file not found. please slice again.")
#define FILE_OVER_SIZE_STR          _L("The print file exceeds the maximum allowable size (1GB). Please simplify the model and slice again.")
#define PRINT_CANCELED_STR          _L("Task canceled.")
#define SEND_PRINT_FAILED_STR       _L("Failed to send the print job. Please try again.")
#define UPLOAD_FTP_FAILED_STR       _L("Failed to upload file to ftp. Please try again.")
#define PRINT_SIGNED_STR            _L("Your software is not signed, and some printing functions have been restricted. Please use the officially signed software version.")

#define DESC_NETWORK_ERROR      _L("Check the current status of the bambu server by clicking on the link above.")
#define DESC_FILE_TOO_LARGE     _L("The size of the print file is too large. Please adjust the file size and try again.")
#define DESC_FAIL_NOT_EXIST     _L("Print file not found, Please slice it again and send it for printing.")
#define DESC_UPLOAD_FTP_FAILED  _L("Failed to upload print file to FTP. Please check the network status and try again.")

#define SENDING_OVER_LAN_STR    _L("Sending print job over LAN")
#define SENDING_OVER_CLOUD_STR  _L("Sending print job through cloud service")

#define wait_sending_finish     _L("Print task sending times out.")
//static wxString desc_wait_sending_finish    = _L("The printer timed out while receiving a print job. Please check if the network is functioning properly and send the print again.");
//static wxString desc_wait_sending_finish    = _L("The printer timed out while receiving a print job. Please check if the network is functioning properly.");

PrintJob::PrintJob(std::shared_ptr<ProgressIndicator> pri, Plater* plater, std::string dev_id)
: PlaterJob{ std::move(pri), plater },
    m_dev_id(dev_id),
    m_is_calibration_task(false)
{
    m_print_job_completed_id = plater->get_print_finished_event();
}

void PrintJob::prepare()
{
    if (job_data.is_from_plater)
        m_plater->get_print_job_data(&job_data);
    if (&job_data) {
        std::string temp_file = Slic3r::resources_dir() + "/check_access_code.txt";
        auto check_access_code_path = temp_file.c_str();
        job_data._temp_path = fs::path(check_access_code_path);
    }

    m_print_stage = BBL::SendingPrintJobStage::PrintingStageLimit;
}

void PrintJob::on_exception(const std::exception_ptr &eptr)
{
    try {
        if (eptr)
            std::rethrow_exception(eptr);
    } catch (std::exception &/*e*/) {
        PlaterJob::on_exception(eptr);
    }
}

void PrintJob::on_success(std::function<void()> success)
{
    m_success_fun = success;
}

std::string PrintJob::truncate_string(const std::string& str, size_t maxLength)
{
    if (str.length() <= maxLength)
    {
        return str;
    }

    wxString local_str = wxString::FromUTF8(str);
    wxString truncatedStr;

    for (auto i = 1; i < local_str.Length(); i++) {
        wxString tagStr = local_str.Mid(0, i);
        if (tagStr.ToUTF8().length() >= maxLength) {
            truncatedStr = local_str.Mid(0, i - 1);
            break;
        }
    }
    return truncatedStr.utf8_string();
}


wxString PrintJob::get_http_error_msg(unsigned int status, std::string body)
{
    try {
        int code = 0;
        std::string error;
        std::string message;
        wxString result;
        if (status >= 400 && status < 500)
            try {
            json j = json::parse(body);
            if (j.contains("code")) {
                if (!j["code"].is_null())
                    code = j["code"].get<int>();
            }
            if (j.contains("error")) {
                if (!j["error"].is_null())
                    error = j["error"].get<std::string>();
            }
            if (j.contains("message")) {
                if (!j["message"].is_null())
                    message = j["message"].get<std::string>();
            }
            //switch (status) {
            //    ;
            //}
        }
        catch (...) {
            ;
        }
        else if (status == 503) {
            return _L("Service Unavailable");
        }
        else {
            wxString unkown_text = _L("Unknown Error.");
            unkown_text += wxString::Format("status=%u, body=%s", status, body);
            BOOST_LOG_TRIVIAL(error) << "http_error: status=" << status << ", code=" << code << ", error=" << error;
            return unkown_text;
        }

        BOOST_LOG_TRIVIAL(error) << "http_error: status=" << status << ", code=" << code << ", error=" << error;

        result = wxString::Format("code=%u, error=%s", code, from_u8(error));
        return result;
    } catch(...) {
        ;
    }
    return wxEmptyString;
}

void PrintJob::process()
{
    /* display info */
    wxString error_str;
    int curr_percent = 10;
    NetworkAgent* m_agent = wxGetApp().getAgent();

    int result = -1;
    //unsigned int http_code;
    std::string http_body;

    int total_plate_num = plate_data.plate_count;
    if (!plate_data.is_valid) {
        total_plate_num =  m_plater->get_partplate_list().get_plate_count();
        PartPlate *plate = m_plater->get_partplate_list().get_plate(job_data.plate_idx);
        if (plate == nullptr) {
            plate = m_plater->get_partplate_list().get_curr_plate();
            if (plate == nullptr) return;
        }

        /* check gcode is valid */
        if (!plate->is_valid_gcode_file() && m_print_type == "from_normal") {
            update_status(curr_percent, CHECK_GCODE_FAILED_STR);
            return;
        }

        if (was_canceled()) {
            update_status(curr_percent, PRINTJOB_CANCEL_STR);
            return;
        }
    }

    m_project_name = truncate_string(m_project_name, 100);
    int curr_plate_idx = 0;

    if (m_print_type == "from_normal") {
        if (plate_data.is_valid)
            curr_plate_idx = plate_data.cur_plate_index;
        if (job_data.plate_idx >= 0)
            curr_plate_idx = job_data.plate_idx + 1;
        else if (job_data.plate_idx == PLATE_CURRENT_IDX)
            curr_plate_idx = m_plater->get_partplate_list().get_curr_plate_index() + 1;
        else if (job_data.plate_idx == PLATE_ALL_IDX)
            curr_plate_idx = m_plater->get_partplate_list().get_curr_plate_index() + 1;
        else
            curr_plate_idx = m_plater->get_partplate_list().get_curr_plate_index() + 1;
    }
    else if(m_print_type == "from_sdcard_view" || m_print_type == "from_sdcard_transfer") {
        curr_plate_idx = m_print_from_sdc_plate_idx;
    }

    PartPlate* curr_plate = m_plater->get_partplate_list().get_curr_plate();
    if (curr_plate) {
        this->task_bed_type = bed_type_to_gcode_string(plate_data.is_valid ? plate_data.bed_type : curr_plate->get_bed_type(true));
    }

    BBL::PrintParams params;

    // local print access
    params.dev_ip = m_dev_ip;
    params.use_ssl_for_ftp  = m_local_use_ssl_for_ftp;
    params.use_ssl_for_mqtt  = m_local_use_ssl_for_mqtt;
    params.username = "bblp";
    params.password = m_access_code;

    // check access code and ip address
    if (this->connection_type == "lan" && (m_print_type == "from_normal" || m_print_type == "from_sdcard_transfer")) {
        bool emmc_ok = false;
        bool ftp_ok = false;
        if (could_emmc_print) {
            std::string devIP = m_dev_ip;
            std::string accessCode = m_access_code;
            std::string url = "bambu:///local/" + devIP + "?port=6000&user=" + "bblp" + "&passwd=" + accessCode;
            std::unique_ptr<FileTransferTunnel> tunnel = std::make_unique<FileTransferTunnel>(module(), url);
            emmc_ok = tunnel->sync_start_connect();
        }
        {
            params.dev_id = m_dev_id;
            params.project_name = "verify_job";
            params.filename = job_data._temp_path.string();
            params.connection_type = this->connection_type;

            result = m_agent->start_send_gcode_to_sdcard(params, nullptr, nullptr, nullptr);

            ftp_ok = result == 0;
        }
        if (!emmc_ok && !ftp_ok) {
            BOOST_LOG_TRIVIAL(error) << "access code is invalid";
            m_enter_ip_address_fun_fail();
            m_job_finished = true;
            return;
        }

        params.project_name = "";
        params.filename = "";
    }

    params.dev_id               = m_dev_id;
    params.ftp_folder           = m_ftp_folder;
    params.filename             = job_data._3mf_path.string();
    params.config_filename      = job_data._3mf_config_path.string();
    params.plate_index          = curr_plate_idx;
    params.task_bed_leveling    = this->task_bed_leveling;
    params.task_flow_cali       = this->task_flow_cali;
    params.task_vibration_cali  = this->task_vibration_cali;
    params.task_layer_inspect   = this->task_layer_inspect;
    params.task_record_timelapse= this->task_record_timelapse;
    params.task_timelapse_use_internal = this->task_timelapse_use_internal;
    params.nozzle_mapping       = this->task_nozzle_mapping;
    params.ams_mapping          = this->task_ams_mapping;
    params.ams_mapping2         = this->task_ams_mapping2;
    params.ams_mapping_info     = this->task_ams_mapping_info;
    params.nozzles_info         = this->task_nozzles_info;
    params.connection_type      = this->connection_type;
    params.task_use_ams         = this->task_use_ams;
    params.task_bed_type        = this->task_bed_type;
    // the network plugin only knows the built-in types; a transferred sdcard file is uploaded like a normal print
    params.print_type           = (m_print_type == "from_sdcard_transfer") ? "from_normal" : this->m_print_type;
    params.auto_bed_leveling    = this->auto_bed_leveling;
    params.auto_flow_cali       = this->auto_flow_cali;
    params.auto_offset_cali     = this->auto_offset_cali;
    params.extruder_cali_manual_mode = this->extruder_cali_manual_mode;
    params.task_ext_change_assist = this->task_ext_change_assist;
    params.try_emmc_print         = this->could_emmc_print;

    if (m_print_type == "from_sdcard_view") {
        params.dst_file = m_dst_path;
    }

    if (wxGetApp().model().model_info && wxGetApp().model().model_info.get()) {
        ModelInfo* model_info = wxGetApp().model().model_info.get();
        auto origin_profile_id = model_info->metadata_items.find(BBL_DESIGNER_PROFILE_ID_TAG);
        if (origin_profile_id != model_info->metadata_items.end()) {
            try {
                params.origin_profile_id    = stoi(origin_profile_id->second.c_str());
            }
            catch(...) {}
        }
        auto origin_model_id = model_info->metadata_items.find(BBL_DESIGNER_MODEL_ID_TAG);
        if (origin_model_id != model_info->metadata_items.end()) {
            try {
                params.origin_model_id = origin_model_id->second;
            }
            catch(...) {}
        }

        auto profile_name = model_info->metadata_items.find(BBL_DESIGNER_PROFILE_TITLE_TAG);
        if (profile_name != model_info->metadata_items.end()) {
            try {
                params.preset_name = profile_name->second;
            }
            catch (...) {}
        }

         if (m_print_type != "from_sdcard_view" && m_print_type != "from_sdcard_transfer") {
            auto model_name = model_info->metadata_items.find(BBL_DESIGNER_MODEL_TITLE_TAG);
            if (model_name != model_info->metadata_items.end()) {
                try {
                    std::string mall_model_name = model_name->second;
                    std::replace(mall_model_name.begin(), mall_model_name.end(), ' ', '_');
                    const char *unusable_symbols = "<>[]:/\\|?*\" ";
                    for (const char *symbol = unusable_symbols; *symbol != '\0'; ++symbol) { std::replace(mall_model_name.begin(), mall_model_name.end(), *symbol, '_'); }

                    std::regex pattern("_+");
                    params.project_name = std::regex_replace(mall_model_name, pattern, "_");
                    params.project_name = truncate_string(params.project_name, 100);
                } catch (...) {}
            }
        }

        auto svc_context = model_info->metadata_items.find(BBL_SVC_CONTEXT_TAG);
        if (svc_context != model_info->metadata_items.end()) {
            params.svc_context = svc_context->second;
        }
    }

    params.stl_design_id = 0;

    if (!wxGetApp().model().stl_design_id.empty()) {

        auto country_code = wxGetApp().app_config->get_country_code();
        bool match_code = false;

        if (wxGetApp().model().stl_design_country == "DEV" && (country_code == "ENV_CN_DEV" || country_code == "NEW_ENV_DEV_HOST")) {
            match_code = true;
        }

        if (wxGetApp().model().stl_design_country == "QA" && (country_code == "ENV_CN_QA" || country_code == "NEW_ENV_QAT_HOST")) {
            match_code = true;
        }

        if (wxGetApp().model().stl_design_country == "CN_PRE" && (country_code == "ENV_CN_PRE" || country_code == "NEW_ENV_PRE_HOST")) {
            match_code = true;
        }

        if (wxGetApp().model().stl_design_country == "US_PRE" && country_code == "ENV_US_PRE") {
            match_code = true;
        }

        if (country_code == wxGetApp().model().stl_design_country) {
            match_code = true;
        }

        if (match_code) {
            int stl_design_id = 0;
            try {
                stl_design_id = std::stoi(wxGetApp().model().stl_design_id);
            }
            catch (...) {
                stl_design_id = 0;
            }
            params.stl_design_id = stl_design_id;
        }
    }

    const auto& model_design_id = wxGetApp().model().design_id;
    if (params.stl_design_id == 0 || !model_design_id.empty()) {
        if (model_design_id.empty()) {
            params.stl_design_id = 0;
        } else {
            try {
                params.stl_design_id = std::stoi(model_design_id);
            } catch (...) {
                params.stl_design_id = 0;
            }
        }
    }

    if (params.preset_name.empty() && m_print_type == "from_normal") { params.preset_name = wxString::Format("%s_plate_%d", m_project_name, curr_plate_idx).ToStdString(); }
    if (params.project_name.empty()) {params.project_name = m_project_name;}

    if (m_is_calibration_task) {
        params.project_name = m_project_name;
        params.origin_model_id = "";
    }

    wxString error_text;
    wxString msg_text;


    const int StagePercentPoint[(int)PrintingStageFinished + 1] = {
        20,     // PrintingStageCreate
        30,     // PrintingStageUpload
        70,     // PrintingStageWaiting
        75,     // PrintingStageRecord
        97,     // PrintingStageSending
        100,    // PrintingStageFinished
        100     // PrintingStageFinished
    };

    enum class SendAttemptRoute {
        Default,
        LanWithRecord,
        Cloud,
        LanDirect,
    };

    struct PrintErrorSnapshot {
        bool        valid { false };
        int         code { 0 };
        std::string desc;
        std::string extra;
    };

    bool is_try_lan_mode = false;
    bool is_try_lan_mode_failed = false;
    bool use_cloud_error_snapshot = false;
    SendAttemptRoute attempt_route = SendAttemptRoute::Default;
    PrintErrorSnapshot cloud_error_snapshot;

    auto get_route_status_msg = [this, &attempt_route]() -> wxString {
        switch (attempt_route) {
        case SendAttemptRoute::LanWithRecord:
            return _L("Sending print job with LAN acceleration");
        case SendAttemptRoute::Cloud:
            return _L("Sending print job through cloud service");
        case SendAttemptRoute::LanDirect:
            return _L("Cloud service sending failed. Sending print job over LAN without print history.");
        default:
            return this->connection_type == "lan" ? _L("Sending print job over LAN") :
                                                     _L("Sending print job through cloud service");
        }
    };

    auto map_route_progress = [&attempt_route](int percent) -> int {
        percent = std::max(0, std::min(100, percent));
        switch (attempt_route) {
        case SendAttemptRoute::LanWithRecord:
            return 10 + percent * 45 / 100;
        case SendAttemptRoute::Cloud:
            return 55 + percent * 25 / 100;
        case SendAttemptRoute::LanDirect:
            return 85 + percent * 12 / 100;
        default:
            return percent;
        }
    };

    auto error_desc_for_code = [](int code) -> std::string {
        if (code == BAMBU_NETWORK_ERR_PRINT_WR_FILE_OVER_SIZE || code == BAMBU_NETWORK_ERR_PRINT_SP_FILE_OVER_SIZE) {
            return DESC_FILE_TOO_LARGE.ToStdString();
        }
        if (code == BAMBU_NETWORK_ERR_PRINT_WR_FILE_NOT_EXIST || code == BAMBU_NETWORK_ERR_PRINT_SP_FILE_NOT_EXIST) {
            return DESC_FAIL_NOT_EXIST.ToStdString();
        }
        if (code == BAMBU_NETWORK_ERR_PRINT_LP_UPLOAD_FTP_FAILED || code == BAMBU_NETWORK_ERR_PRINT_SG_UPLOAD_FTP_FAILED) {
            return DESC_UPLOAD_FTP_FAILED.ToStdString();
        }
        return DESC_NETWORK_ERROR.ToStdString();
    };

    auto remember_cloud_error = [&cloud_error_snapshot, &error_desc_for_code](int code, const std::string& info) {
        cloud_error_snapshot.valid = true;
        cloud_error_snapshot.code = code;
        cloud_error_snapshot.desc = error_desc_for_code(code);
        cloud_error_snapshot.extra = info;
    };

    auto restore_cloud_error_info = [this, &cloud_error_snapshot]() {
        if (cloud_error_snapshot.valid) {
            m_plater->update_print_error_info(cloud_error_snapshot.code, cloud_error_snapshot.desc, cloud_error_snapshot.extra);
        }
    };


    auto update_fn = [this,
        &is_try_lan_mode,
        &is_try_lan_mode_failed,
        &error_str,
        &curr_percent,
        &error_text,
        StagePercentPoint,
        &attempt_route,
        &use_cloud_error_snapshot,
        &get_route_status_msg,
        &map_route_progress,
        &error_desc_for_code,
        &remember_cloud_error
    ](int stage, int code, std::string info) {
                        wxString msg = get_route_status_msg();
                        m_print_stage = stage;
                        if (stage == BBL::SendingPrintJobStage::PrintingStageCreate) {
                            msg = get_route_status_msg();
                        }
                        else if (stage == BBL::SendingPrintJobStage::PrintingStageUpload) {
                            if (code >= 0 && code <= 100 && !info.empty()) {
                                msg = get_route_status_msg();
                                msg += wxString::Format("(%s)", info);
                            }
                        }
                        else if (stage == BBL::SendingPrintJobStage::PrintingStageWaiting) {
                            msg = get_route_status_msg();
                        }
                        else  if (stage == BBL::SendingPrintJobStage::PrintingStageRecord && !is_try_lan_mode) {
                            msg = _L("Sending print configuration");
                        }
                        else if (stage == BBL::SendingPrintJobStage::PrintingStageSending && !is_try_lan_mode) {
                            msg = get_route_status_msg();
                        }
                        else if (stage == BBL::SendingPrintJobStage::PrintingStageFinished) {
                            msg = wxString::Format(_L("Successfully sent. Will automatically jump to the device page in %ss"), info);
                            if (m_print_job_completed_id == wxGetApp().plater()->get_send_calibration_finished_event()) {
                                msg = wxString::Format(_L("Successfully sent. Will automatically jump to the next page in %ss"), info);
                            }
                        } else {
                            msg = get_route_status_msg();
                        }

                        // update current percnet
                        if (stage >= 0 && stage <= (int) PrintingStageFinished) {
                            curr_percent = StagePercentPoint[stage];
                            if ((stage == BBL::SendingPrintJobStage::PrintingStageUpload
                                || stage == BBL::SendingPrintJobStage::PrintingStageRecord)
                                && (code > 0 && code <= 100)) {
                                curr_percent = (StagePercentPoint[stage + 1] - StagePercentPoint[stage]) * code / 100 + StagePercentPoint[stage];
                            }
                        }
                        curr_percent = map_route_progress(curr_percent);

                        //get errors
                        if (code > 100 || code < 0 || stage == BBL::SendingPrintJobStage::PrintingStageERROR) {
                            if (use_cloud_error_snapshot) {
                                if (attempt_route == SendAttemptRoute::Cloud) {
                                    remember_cloud_error(code, info);
                                    m_plater->update_print_error_info(code, error_desc_for_code(code), info);
                                } else {
                                    BOOST_LOG_TRIVIAL(warning) << "print_job: non-cloud attempt error, route=" << static_cast<int>(attempt_route)
                                        << ", code=" << code << ", info=" << info;
                                }
                            } else {
                                m_plater->update_print_error_info(code, error_desc_for_code(code), info);
                            }
                        }
                        else {
                             this->update_status(curr_percent, msg);
                        }
                    };

    auto cancel_fn = [this]() {
            return was_canceled();
        };


    DeviceManager* dev = wxGetApp().getDeviceManager();
    MachineObject* obj = dev->get_selected_machine();

    auto wait_fn = [this, curr_percent, &obj](int state, std::string job_info) {
            BOOST_LOG_TRIVIAL(info) << "print_job: get_job_info = " << job_info;

            if (!obj->is_support_wait_sending_finish) {
                return true;
            }

            std::string curr_job_id;
            json job_info_j;
            try {
                job_info_j.parse(job_info);
                if (job_info_j.contains("job_id")) {
                    curr_job_id = DevJsonValParser::get_longlong_val(job_info_j["job_id"]);
                }
                BOOST_LOG_TRIVIAL(trace) << "print_job: curr_obj_id=" << curr_job_id;

            } catch(...) {
                ;
            }

            if (obj) {
                int time_out = 0;
                while (time_out < PRINT_JOB_SENDING_TIMEOUT) {
                    BOOST_LOG_TRIVIAL(trace) << "print_job: obj job_id = " << obj->job_id_;
                    if (!obj->job_id_.empty() && obj->job_id_.compare(curr_job_id) == 0) {
                        BOOST_LOG_TRIVIAL(info) << "print_job: got job_id = " << obj->job_id_ << ", time_out=" << time_out;
                        return true;
                    }
                    if (obj->is_in_printing_status(obj->print_status)) {
                        BOOST_LOG_TRIVIAL(info) << "print_job: printer has enter printing status, s = " << obj->print_status;
                        return true;
                    }

                    if (this->was_canceled()) {
                        BOOST_LOG_TRIVIAL(info) << "print_job: user cancel the job" << obj->job_id_;
                        return true;
                    }

                    time_out++;
                    boost::this_thread::sleep_for(boost::chrono::milliseconds(1000));
                }
                //this->update_status(curr_percent, _L("Print task sending times out."));
                //m_plater->update_print_error_info(BAMBU_NETWORK_ERR_TIMEOUT, wait_sending_finish.ToStdString(), desc_wait_sending_finish.ToStdString());
                BOOST_LOG_TRIVIAL(info) << "print_job: timeout, cancel the job" << obj->job_id_;
                /* handle tiemout */
                //obj->command_task_cancel(curr_job_id);
                //return false;
                return true;
            }
            BOOST_LOG_TRIVIAL(info) << "print_job: obj is null";
            return true;
    };

    struct AccessCodeRefreshState {
        std::mutex mutex;
        std::condition_variable cv;
        bool done { false };
        bool success { false };
        int send_result { 0 };
        std::string access_code;
        std::string print_status;
        std::string reason;
        std::string sequence_id;
    };

    auto request_fresh_access_code = [this, &obj, &curr_percent]() {
        constexpr int ACCESS_CODE_REFRESH_TIMEOUT_MS = 5000;
        auto state = std::make_shared<AccessCodeRefreshState>();
        curr_percent = 80;
        this->update_status(curr_percent, _L("Refreshing printer access code"));

        wxGetApp().CallAfter([state, obj]() {
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->done)
                    return;
            }

            if (!obj) {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->done = true;
                state->success = false;
                state->reason = "machine object is null";
                state->cv.notify_all();
                return;
            }

            const std::string sequence_id = obj->request_access_code(
                [state](bool success, std::string access_code, std::string print_status) {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    if (state->done)
                        return;
                    state->done = true;
                    state->access_code = std::move(access_code);
                    state->print_status = std::move(print_status);
                    state->success = success && !state->access_code.empty();
                    if (!state->success)
                        state->reason = "empty access_code";
                    state->cv.notify_all();
                });

            bool cancel_request = false;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->done)
                    cancel_request = !sequence_id.empty();
                else if (sequence_id.empty()) {
                    state->done = true;
                    state->success = false;
                    state->reason = "failed to request access code";
                    state->cv.notify_all();
                } else {
                    state->sequence_id = sequence_id;
                }
            }
            if (cancel_request)
                obj->cancel_access_code_request(sequence_id);
        });

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ACCESS_CODE_REFRESH_TIMEOUT_MS);
        std::unique_lock<std::mutex> lock(state->mutex);
        while (!state->done) {
            if (this->was_canceled()) {
                state->done = true;
                state->success = false;
                state->reason = "canceled";
                const std::string sequence_id = state->sequence_id;
                lock.unlock();
                if (!sequence_id.empty())
                    wxGetApp().CallAfter([obj, sequence_id]() { obj->cancel_access_code_request(sequence_id); });
                return state;
            }
            if (state->cv.wait_until(lock, deadline) == std::cv_status::timeout && !state->done) {
                state->done = true;
                state->success = false;
                state->reason = "get_access_code timeout";
                const std::string sequence_id = state->sequence_id;
                lock.unlock();
                if (!sequence_id.empty())
                    wxGetApp().CallAfter([obj, sequence_id]() { obj->cancel_access_code_request(sequence_id); });
                return state;
            }
        }
        return state;
    };

    auto should_stop_without_fallback = [](int ret) {
        return ret == BAMBU_NETWORK_ERR_CANCELED
            || ret == BAMBU_NETWORK_SIGNED_ERROR
            || ret == BAMBU_NETWORK_ERR_PRINT_WR_FILE_NOT_EXIST
            || ret == BAMBU_NETWORK_ERR_PRINT_SP_FILE_NOT_EXIST
            || ret == BAMBU_NETWORK_ERR_PRINT_WR_FILE_OVER_SIZE
            || ret == BAMBU_NETWORK_ERR_PRINT_SP_FILE_OVER_SIZE
            || ret == BAMBU_NETWORK_ERR_PRINT_LP_FILE_OVER_SIZE;
    };

    auto is_cached_status_printable = [](const std::string& status) {
        std::string normalized_status = status;
        std::transform(normalized_status.begin(), normalized_status.end(), normalized_status.begin(),
            [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
        return normalized_status == "IDLE" || normalized_status == "FINISH" || normalized_status == "FAILED";
    };

    if (m_print_type == "from_sdcard_view") {
        BOOST_LOG_TRIVIAL(info) << "print_job: try to send with cloud, model is sdcard view";
        attempt_route = SendAttemptRoute::Default;
        this->update_status(curr_percent, _L("Sending print job through cloud service"));
        result = m_agent->start_sdcard_print(params, update_fn, cancel_fn);
    } else if (params.connection_type != "lan") {
        if (params.dev_ip.empty())
            params.comments = "no_ip";
        else if (this->cloud_print_only)
            params.comments = "low_version";
        else if (!this->has_sdcard)
            params.comments = "no_sdcard";
        else if (params.password.empty())
            params.comments = "no_password";


        //use ftp only
        if (!wxGetApp().app_config->get("lan_mode_only").empty() && wxGetApp().app_config->get("lan_mode_only") == "1") {

            if (params.password.empty() || params.dev_ip.empty()) {
                error_text = wxString::Format("Ip address:%s", params.dev_ip);
                result = BAMBU_NETWORK_ERR_FTP_UPLOAD_FAILED;
            }
            else {
                BOOST_LOG_TRIVIAL(info) << "print_job: use ftp send print only";
                attempt_route = SendAttemptRoute::Default;
                this->update_status(curr_percent, _L("Sending print job over LAN"));
                is_try_lan_mode = true;
                result = m_agent->start_local_print_with_record(params, update_fn, cancel_fn, wait_fn);
                if (result < 0) {
                    error_text = wxString::Format("Ip address:%s", params.dev_ip);
                    // try to send with cloud
                    BOOST_LOG_TRIVIAL(warning) << "print_job: use ftp send print failed";
                }
            }
        }
        else {
            use_cloud_error_snapshot = true;
            if (!this->cloud_print_only
                && !params.password.empty()
                && !params.dev_ip.empty()
                && this->has_sdcard) {
                // try to send local with record
                BOOST_LOG_TRIVIAL(info) << "print_job: try to start local print with record";
                attempt_route = SendAttemptRoute::LanWithRecord;
                this->update_status(map_route_progress(curr_percent), get_route_status_msg());
                result = m_agent->start_local_print_with_record(params, update_fn, cancel_fn, wait_fn);
                if (result == 0) {
                    params.comments = "";
                }
                else if (result == BAMBU_NETWORK_ERR_PRINT_WR_UPLOAD_FTP_FAILED) {
                    params.comments = "upload_failed";
                }
                else {
                    params.comments = (boost::format("failed(%1%)") % result).str();
                }
                if (result < 0) {
                    is_try_lan_mode_failed = true;
                    // try to send with cloud
                    BOOST_LOG_TRIVIAL(warning) << "print_job: local with record failed, ret=" << result << ", try to send with cloud";
                    attempt_route = SendAttemptRoute::Cloud;
                    this->update_status(map_route_progress(curr_percent), get_route_status_msg());
                    result = m_agent->start_print(params, update_fn, cancel_fn, wait_fn);
                }
            }
            else {
                BOOST_LOG_TRIVIAL(info) << "print_job: send with cloud";
                attempt_route = SendAttemptRoute::Cloud;
                this->update_status(map_route_progress(curr_percent), get_route_status_msg());
                result = m_agent->start_print(params, update_fn, cancel_fn, wait_fn);
            }

            if (result < 0) {
                if (attempt_route == SendAttemptRoute::Cloud && !cloud_error_snapshot.valid)
                    remember_cloud_error(result, "");

                if (!should_stop_without_fallback(result)) {
                    if (this->cloud_print_only || params.dev_ip.empty() || (!this->has_sdcard && !this->could_emmc_print)) {
                        BOOST_LOG_TRIVIAL(warning) << "print_job: skip lan direct fallback, cloud_print_only=" << this->cloud_print_only
                            << ", dev_ip_empty=" << params.dev_ip.empty() << ", has_sdcard=" << this->has_sdcard
                            << ", could_emmc_print=" << this->could_emmc_print;
                    } else {
                        auto refresh_state = request_fresh_access_code();
                        if (was_canceled()) {
                            result = BAMBU_NETWORK_ERR_CANCELED;
                        } else if (!refresh_state->success) {
                            BOOST_LOG_TRIVIAL(warning) << "print_job: skip lan direct fallback, refresh access code failed, reason=" << refresh_state->reason;
                        } else if (!is_cached_status_printable(refresh_state->print_status)) {
                            BOOST_LOG_TRIVIAL(warning) << "print_job: skip lan direct fallback, cached print_status=" << refresh_state->print_status;
                        } else {
                            BBL::PrintParams local_params = params;
                            local_params.password = refresh_state->access_code;
                            local_params.connection_type = "lan";
                            attempt_route = SendAttemptRoute::LanDirect;
                            curr_percent = 85;
                            this->update_status(curr_percent, get_route_status_msg());
                            BOOST_LOG_TRIVIAL(info) << "print_job: try lan direct fallback after cloud failure";
                            result = m_agent->start_local_print(local_params, update_fn, cancel_fn);
                            if (result < 0) {
                                BOOST_LOG_TRIVIAL(warning) << "print_job: lan direct fallback failed, ret=" << result;
                                restore_cloud_error_info();
                            }
                        }
                    }
                }
            }
        }
    } else {
        if (this->has_sdcard || this->could_emmc_print) {
            attempt_route = SendAttemptRoute::Default;
            this->update_status(curr_percent, _L("Sending print job over LAN"));
            result = m_agent->start_local_print(params, update_fn, cancel_fn);
        } else {
            this->update_status(curr_percent, _L("Storage needs to be inserted before printing via LAN."));
            return;
        }
    }

    if (result < 0) {
        curr_percent = -1;
        if (result == BAMBU_NETOWRK_ERR_PRINT_SP_ENC_FLAG_NOT_READY) {
            msg_text = _L("Retrieving printer information, please try again later.");
        }
        else if (result == BAMBU_NETWORK_ERR_PRINT_WR_FILE_NOT_EXIST || result == BAMBU_NETWORK_ERR_PRINT_SP_FILE_NOT_EXIST) {
            msg_text = FILE_IS_NOT_EXISTS_STR;
        } else if (result == BAMBU_NETWORK_ERR_PRINT_SP_FILE_OVER_SIZE || result == BAMBU_NETWORK_ERR_PRINT_WR_FILE_OVER_SIZE) {
            msg_text = FILE_OVER_SIZE_STR;
        } else if (result == BAMBU_NETWORK_ERR_PRINT_WR_CHECK_MD5_FAILED || result == BAMBU_NETWORK_ERR_PRINT_SP_CHECK_MD5_FAILED) {
            msg_text = FAILED_IN_CLOUD_SERVICE_STR;
        } else if (result == BAMBU_NETWORK_ERR_PRINT_WR_GET_NOTIFICATION_TIMEOUT || result == BAMBU_NETWORK_ERR_PRINT_SP_GET_NOTIFICATION_TIMEOUT) {
            msg_text = TIMEOUT_TO_UPLOAD_STR;
        } else if (result == BAMBU_NETWORK_ERR_PRINT_LP_UPLOAD_FTP_FAILED || result == BAMBU_NETWORK_ERR_PRINT_SG_UPLOAD_FTP_FAILED) {
            msg_text = UPLOAD_FTP_FAILED_STR;
        } else if (result == BAMBU_NETWORK_ERR_CANCELED) {
            msg_text = PRINT_CANCELED_STR;
            this->update_status(0, msg_text);
        } else if (result == BAMBU_NETWORK_SIGNED_ERROR) {
            msg_text = PRINT_SIGNED_STR;
        } else {
            msg_text = SEND_PRINT_FAILED_STR;
        }

        if (use_cloud_error_snapshot && result != BAMBU_NETWORK_ERR_CANCELED)
            restore_cloud_error_info();

        if (result != BAMBU_NETWORK_ERR_CANCELED) {
            this->show_error_info(msg_text, 0, "", "");
        }

        BOOST_LOG_TRIVIAL(error) << "print_job: failed, result = " << result;
    } else {
        // wait for printer mqtt ready the same job id

        wxGetApp().plater()->record_slice_preset("print");

        BOOST_LOG_TRIVIAL(error) << "print_job: send ok.";
        wxCommandEvent* evt = new wxCommandEvent(m_print_job_completed_id);
        if (!m_completed_evt_data.empty())
            evt->SetString(m_completed_evt_data);
        else
            evt->SetString(m_dev_id);
        if (m_print_job_completed_id == wxGetApp().plater()->get_send_calibration_finished_event()) {
            int sel = wxGetApp().mainframe->get_calibration_curr_tab();
            if (sel >= 0) {
                evt->SetInt(sel);
            }
        }
        wxQueueEvent(m_plater, evt);
        m_job_finished = true;
    }
}

void PrintJob::finalize() {
    if (was_canceled()) return;

    Job::finalize();
}

void PrintJob::set_project_name(std::string name)
{
    m_project_name = name;
}

void PrintJob::set_dst_name(std::string path)
{
    m_dst_path = path;
}


void PrintJob::on_check_ip_address_fail(std::function<void()> func)
{
    m_enter_ip_address_fun_fail = func;
}

void PrintJob::on_check_ip_address_success(std::function<void()> func)
{
    m_enter_ip_address_fun_success = func;
}

void PrintJob::connect_to_local_mqtt()
{
    this->update_status(0, wxEmptyString);
}

void PrintJob::set_calibration_task(bool is_calibration)
{
    m_is_calibration_task = is_calibration;
}

}} // namespace Slic3r::GUI
