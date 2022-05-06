#include "PrintJob.hpp"
#include "libslic3r/MTUtils.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"

#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"

namespace Slic3r {
namespace GUI {

static wxString printjob_cancel_str = _L("print project cancelled!");
static wxString failed_to_create_str = _L("Failed to create the printing task. Please try again!");
static wxString failed_to_upload_str = _L("Failed to upload the printing task. Please try again!");
static wxString timeout_to_upload_str = _L("Uploading printing task timed out. Please try again!");
static wxString failed_to_sending_str = _L("Failed to send the printing task. Please try again!");
static wxString timeout_to_sending_str = _L("Sending the printing task has timed out. Please try again!");

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
    wxString msg = _L("Creating a print project...");
    int curr_percent = 25;
    update_status(curr_percent, msg);

    int res = -1;
    unsigned int http_code;
    std::string http_body;
    Slic3r::AccountManager* c = Slic3r::GUI::wxGetApp().getAccountManager();
    Slic3r::DeviceManager* d  = Slic3r::GUI::wxGetApp().getDeviceManager();

    int total_plate_num = m_plater->get_partplate_list().get_plate_count();

    PartPlate* plate = m_plater->get_partplate_list().get_plate(job_data.plate_idx);
    if (plate == nullptr) {
        plate = m_plater->get_partplate_list().get_curr_plate();
        if (plate == nullptr)
        return;
    }

    /* check gcode is valid */
    if (!plate->is_valid_gcode_file()) {
        update_status(curr_percent, "Internal error, no gcode in 3mf file!");
        return;
    }

    if (was_canceled()) {
        update_status(curr_percent, printjob_cancel_str);
        return;
    }

    // save project, profile, task, subtask info to local, default_output_file as project name
    std::string project_name = wxGetApp().plater()->get_project_name().ToUTF8().data();
    BBLProject* project = new BBLProject(project_name);
    project->project_3mf_file = job_data._3mf_path.string();
    project->project_path = fs::path(project->project_3mf_file);

    /* select a profile, use default now */
    BBLProfile *profile = new BBLProfile(project);
    // set current print preset to profile_name
    profile->profile_name = wxGetApp().preset_bundle->prints.get_selected_preset_name();

    BOOST_LOG_TRIVIAL(trace) << "print_job: request project id";
    res = c->request_project_profile_id(project, profile, http_code, http_body);

    if (res == 0 && !project->project_id.empty()) {
        BOOST_LOG_TRIVIAL(trace) << "print_job: get project id = " << project->project_id;
    } else {
        wxString error_msg = wxString::Format(_L("\nreq_pro,err:code=%u,msg=%s"), http_code, http_body);
        update_status(curr_percent, failed_to_create_str + error_msg);
        BOOST_LOG_TRIVIAL(trace) << "print_job: request project id failed! error_msg=" << error_msg.ToStdString();
        return;
    }

    if (res == 0 && !profile->profile_id.empty() && !profile->upload_ticket.empty() && !profile->upload_url.empty()) {
        BOOST_LOG_TRIVIAL(trace) << "print_job: get profile id = " << profile->profile_id;
    } else {
        wxString error_msg = wxString::Format(_L("\nreq_pro,err:code=%u,msg=%s"), http_code, http_body);
        update_status(curr_percent, failed_to_create_str + error_msg);
        BOOST_LOG_TRIVIAL(trace) << "print_job: request project id failed! error_msg=" << error_msg.ToStdString();
        return;
    }

    if (was_canceled()) {
        update_status(curr_percent, printjob_cancel_str);
        return;
    }


    msg = _L("The printing project is uploading...");
    curr_percent = 25;
    update_status(curr_percent, msg);

    wxString cancel_str = printjob_cancel_str;

    /* upload and poll */
    BOOST_LOG_TRIVIAL(trace) << "print_job: start to uploading...";
    res = c->upload_3mf_to_oss(profile, http_code, http_body,
        [this, curr_percent, cancel_str, &msg](Http::Progress progress, bool &cancel) {
            int percent = 0;
            if (progress.ultotal != 0) {
                percent = progress.ulnow / progress.ultotal;
            }
            if (was_canceled()) {
                cancel = true;
                update_status(percent, cancel_str);
                return;
            }
            percent = curr_percent + percent * 50 / 100;
            update_status(percent, msg);
        });

    if (was_canceled()) {
        update_status(curr_percent, printjob_cancel_str);
        return;
    }

    if (res < 0) {
        wxString error_msg = wxString::Format(_L("\nupload,err:code=%u,msg=%s"), http_code, http_body);
        update_status(curr_percent, failed_to_upload_str + error_msg);
        BOOST_LOG_TRIVIAL(trace) << "print_job: uploading is failed!";
        return;
    }

    /* put notifications */
    res = c->put_notification(profile, project->project_path.filename().string(), http_code, http_body);
    if (res < 0) {
        wxString error_msg = wxString::Format(_L("\nput_no,err:code=%u,msg=%s"), http_code, http_body);
        update_status(curr_percent, failed_to_upload_str + error_msg);
        return;
    }

    /* get notifications */
    bool cancel = false;
    res = c->get_notification(profile, http_code, http_body,
        [this]() {
            return was_canceled();
        }
        );

    if (res == RET_POLLING_TIMEOUT) {
        update_status(curr_percent, timeout_to_upload_str);
        return;
    } else if (res == RET_POLLING_CANEL) {
        update_status(curr_percent, printjob_cancel_str);
        BOOST_LOG_TRIVIAL(trace) << "print_job: subtask is canceled when uploading...";
        return;
    } else if (res < 0) {
        wxString error_msg = wxString::Format(_L("\nget_no,err:code=%u,msg=%s"), http_code, http_body);
        update_status(curr_percent, failed_to_upload_str + error_msg);
        return;
    }

    curr_percent = 75;
    msg = _L("The printing project is being sent...");
    update_status(curr_percent, msg);


    /* post task */
    int curr_plate_idx = 0;
    if (job_data.plate_idx >= 0)
        curr_plate_idx = job_data.plate_idx + 1;
    else if (job_data.plate_idx == PLATE_CURRENT_IDX)
        curr_plate_idx = m_plater->get_partplate_list().get_curr_plate_index() + 1;
    else {
        msg = wxString::Format(_L("Invalid plate index %d"), job_data.plate_idx);
        update_status(curr_percent, msg);
        return;
    }

    BBLSubTask* subTask = new BBLSubTask();

    //BBS hide bed choice
    //subTask->task_bed_type = this->task_bed_type;
    subTask->task_bed_leveling = this->task_bed_leveling;
    subTask->task_flow_cali = this->task_flow_cali;
    subTask->task_vabration_cali = this->task_vabration_cali;
    subTask->task_record_timelapse = this->task_record_timelapse;

    subTask->task_gcode_in_3mf = (boost::format(GCODE_FILE_FORMAT) % (curr_plate_idx)).str();
    subTask->task_partplate_idx = std::to_string(curr_plate_idx);
    subTask->task_printer_dev_id = m_dev_id;
    if (project->project_name.empty())
        subTask->task_name = wxString::Format(_L("Plate %d"), curr_plate_idx).ToUTF8().data();
    else
        subTask->task_name = wxString::Format(_L("%s"), from_u8(project->project_name), curr_plate_idx).ToUTF8().data();

    res = c->post_task(project, profile, subTask, http_code, http_body);
    if (res < 0) {
        wxString error_msg = wxString::Format(_L("\npos_task,err:code=%u,msg=%s"), http_code, http_body);
        update_status(curr_percent, failed_to_sending_str + error_msg);
        return;
    } else {
        curr_percent = 100;
        msg = _L("The printing task was sent successfully!");
        update_status(curr_percent, msg);
    }

    wxCommandEvent evt(m_print_job_completed_id);
    evt.SetString(m_dev_id);
    wxQueueEvent(m_plater, evt.Clone());
    m_job_finished = true;
    if (m_success_fun != nullptr) { m_success_fun(); }
}

void PrintJob::finalize() {
    if (was_canceled()) return;

    Job::finalize();
}

}} // namespace Slic3r::GUI
