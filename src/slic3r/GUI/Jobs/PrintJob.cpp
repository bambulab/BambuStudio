#include "PrintJob.hpp"
#include "libslic3r/MTUtils.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"

#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"

namespace Slic3r {
namespace GUI {

static wxString creating_stage_str  = _L("Creating");
static wxString uploading_stage_str = _L("Uploading");
static wxString waiting_stage_str   = _L("Waiting");
static wxString sending_stage_str   = _L("Sending");
static wxString finish_stage_str    = _L("Finished");

static wxString check_gcode_failed_str  = _L("Internal error, no gcode file to upload.");
static wxString printjob_cancel_str = _L("Print job was cancelled.");


static wxString failed_to_create_str = _L("Failed to create the print job. Please try agian.");
static wxString failed_to_upload_str = _L("Failed to upload the print job. Please try agian.");
static wxString timeout_to_upload_str = _L("Uploading print job timed out. Please try again.");
static wxString failed_to_sending_str = _L("Failed to send the print job. Please try again.");
static wxString timeout_to_sending_str = _L("Sending print task timed out. Please try again.");

PrintJob::PrintJob(std::shared_ptr<ProgressIndicator> pri, Plater* plater, std::string dev_id)
: PlaterJob{ std::move(pri), plater },
    m_dev_id(dev_id)
{
    m_print_job_completed_id = plater->get_print_finished_event();
}

void PrintJob::prepare()
{
    m_plater->get_print_job_data(&job_data);
}

void PrintJob::on_exception(const std::exception_ptr &eptr)
{
    try {
        if (eptr)
            std::rethrow_exception(eptr);
    } catch (std::exception &e) {
        PlaterJob::on_exception(eptr);
    }
}

void PrintJob::on_success(std::function<void()> success) 
{ 
    m_success_fun = success;
}

void PrintJob::process()
{
    /* display info */
    wxString msg = creating_stage_str;
    int curr_percent = 10;
    update_status(curr_percent, msg);

    int result = -1;
    unsigned int http_code;
    std::string http_body;
    BBL::AccountManager* acc = Slic3r::GUI::wxGetApp().getAccountManager();

    int total_plate_num = m_plater->get_partplate_list().get_plate_count();

    PartPlate* plate = m_plater->get_partplate_list().get_plate(job_data.plate_idx);
    if (plate == nullptr) {
        plate = m_plater->get_partplate_list().get_curr_plate();
        if (plate == nullptr)
        return;
    }

    /* check gcode is valid */
    if (!plate->is_valid_gcode_file()) {
        update_status(curr_percent, check_gcode_failed_str);
        return;
    }

    if (was_canceled()) {
        update_status(curr_percent, printjob_cancel_str);
        return;
    }

    std::string project_name = wxGetApp().plater()->get_project_name().ToUTF8().data();
    int curr_plate_idx = 0;
    if (job_data.plate_idx >= 0)
        curr_plate_idx = job_data.plate_idx + 1;
    else if (job_data.plate_idx == PLATE_CURRENT_IDX)
        curr_plate_idx = m_plater->get_partplate_list().get_curr_plate_index() + 1;

    BBL::AccountManager::PrintParams params;
    params.dev_id = m_dev_id;
    params.project_name = wxGetApp().plater()->get_project_name().ToUTF8().data();
    params.preset_name = wxGetApp().preset_bundle->prints.get_selected_preset_name();
    params.filename = job_data._3mf_path.string();
    params.plate_index = curr_plate_idx;
    params.task_bed_leveling    = this->task_bed_leveling;
    params.task_flow_cali       = this->task_flow_cali;
    params.task_vibration_cali  = this->task_vibration_cali;
    params.task_layer_inspect   = this->task_layer_inspect;
    params.task_record_timelapse= this->task_record_timelapse;
    params.ams_mapping          = this->task_ams_mapping;
    params.connection_type      = this->connection_type;
    

    // local print access
    params.dev_ip = m_dev_ip;
    params.username = "root";
    params.password = m_access_code;

    auto update_fn = [this, &msg, &curr_percent](int stage, int code, std::string info) {
                        if (stage == BBL::SendingPrintJobStage::PrintingStageCreate) {
                            curr_percent = 25;
                            msg = creating_stage_str;
                        }
                        else if (stage == BBL::SendingPrintJobStage::PrintingStageUpload) {
                            curr_percent = 30;
                            if (code == 0) {
                                msg = wxString::Format("%s %s", uploading_stage_str, info);
                            }
                            else {
                                msg = uploading_stage_str;
                            }
                        }
                        else if (stage == BBL::SendingPrintJobStage::PrintingStageWaiting) {
                            curr_percent = 50;
                            msg = waiting_stage_str;
                        }
                        else if (stage == BBL::SendingPrintJobStage::PrintingStageSending) {
                            curr_percent = 90;
                            msg = sending_stage_str;
                        }
                        else if (stage == BBL::SendingPrintJobStage::PrintingStageFinished) {
                            curr_percent = 100;
                            msg = finish_stage_str;
                        }
                        this->update_status(curr_percent, msg);
                    };

    if (params.connection_type != "lan") {
        result = acc->start_print(params, update_fn,
            [this]() {
                return was_canceled();
            });
    } else {
        result = acc->start_local_print(params, update_fn,
            [this]() {
                return was_canceled();
            });
    }

    if (was_canceled()) {
        update_status(curr_percent, printjob_cancel_str);
        return;
    }

    if (result < 0) {
        update_status(curr_percent, failed_to_upload_str);
    } else {
        wxCommandEvent* evt = new wxCommandEvent(m_print_job_completed_id);
        evt->SetString(m_dev_id);
        wxQueueEvent(m_plater, evt);
        m_job_finished = true;
    }
}

void PrintJob::finalize() {
    if (was_canceled()) return;

    Job::finalize();
}

}} // namespace Slic3r::GUI
