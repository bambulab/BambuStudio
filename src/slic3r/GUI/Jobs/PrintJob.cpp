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
    if (plate->get_gcode_filename().empty()) {
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

    BOOST_LOG_TRIVIAL(trace) << "print_job: request project id";
    res = c->request_project_id(project, http_code, http_body);

    if (res == 0 && !project->project_id.empty()) {
        BOOST_LOG_TRIVIAL(trace) << "print_job: get project id = " << project->project_id;
    } else {
        wxString error_msg = wxString::Format(_L("req_pro,err:code=%u,msg=%s"), http_code, http_body);
        update_status(curr_percent, failed_to_create_str + error_msg);
        BOOST_LOG_TRIVIAL(trace) << "print_job: request project id failed! error_msg=" << error_msg.ToStdString();
        return;
    }

    if (was_canceled()) {
        update_status(curr_percent, printjob_cancel_str);
        return;
    }

    /* select a profile, use default now */
    BBLProfile* profile = new BBLProfile(project);
    // set current print preset to profile_name
    profile->profile_name = wxGetApp().preset_bundle->prints.get_selected_preset_name();
    
    BOOST_LOG_TRIVIAL(trace) << "print_job: request profile id";
    res = c->request_profile_id(profile, http_code, http_body);
    if (res == 0 && !profile->profile_id.empty()
        && !profile->upload_ticket.empty()
        && !profile->upload_url.empty()) {
        BOOST_LOG_TRIVIAL(trace) << "print_job: get profile id = " << profile->profile_id;
    } else {
        wxString error_msg = wxString::Format(_L("req_pro,err:code=%u,msg=%s"), http_code, http_body);
        update_status(curr_percent, failed_to_create_str + error_msg);
        BOOST_LOG_TRIVIAL(trace) << "print_job: request project id failed! error_msg=" << error_msg.ToStdString();
        return;
    }

    if (was_canceled()) {
        update_status(curr_percent, printjob_cancel_str);
        return;
    }


    msg = _L("The printing project is being uploaded... 0%%");
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
            msg = wxString::Format(_L("The printing project is being uploaded... %d%%"), percent);
            update_status(percent, msg);
        });

    if (was_canceled()) {
        update_status(curr_percent, printjob_cancel_str);
        return;
    }

    if (res < 0) {
        wxString error_msg = wxString::Format(_L("upload,err:code=%u,msg=%s"), http_code, http_body);
        update_status(curr_percent, failed_to_upload_str + error_msg);
        BOOST_LOG_TRIVIAL(trace) << "print_job: uploading is failed!";
        return;
    }

    /* put notifications */
    res = c->put_notification(profile, project->project_path.filename().string(), http_code, http_body);
    if (res < 0) {
        wxString error_msg = wxString::Format(_L("put_no,err:code=%u,msg=%s"), http_code, http_body);
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
        wxString error_msg = wxString::Format(_L("get_no,err:code=%u,msg=%s"), http_code, http_body);
        update_status(curr_percent, failed_to_upload_str + error_msg);
        return;
    }

    curr_percent = 75;
    msg = _L("The printing project is being sent...");
    update_status(curr_percent, msg);

    /* create task */
    BBLTask* task = new BBLTask(profile);
    if (project->project_name.empty())
        task->task_name = wxString::Format(_L("%d plate(s) printing project"), total_plate_num).ToUTF8().data();
    else
        task->task_name = wxString::Format(_L("%s - %d plate(s)"), project->project_name, total_plate_num).ToUTF8().data();

    /* reqeust task id */
    BOOST_LOG_TRIVIAL(trace) << "print_job: request_task id";
    res = c->request_task_id(task, http_code, http_body);
    if (res < 0 || task->task_id.empty()) {
        wxString error_msg = wxString::Format(_L("req_tak,err:code=%u,msg=%s"), http_code, http_body);
        update_status(curr_percent, failed_to_sending_str + error_msg);
        return;
    }

    if (was_canceled()) {
        update_status(curr_percent, printjob_cancel_str);
        BOOST_LOG_TRIVIAL(trace) << "print_job: subtask is canceled after reqeust_task_id ...";
        return;
    }

    BBLSubTask* curr_subtask = nullptr;
    int curr_plate_idx = 0;
    if (job_data.plate_idx == PLATE_ALL_IDX) {
        for (int i = 0; i < total_plate_num; i++) {
            curr_plate_idx = i + 1;
            /* create subTask from current plate */
            BBLSubTask* subTask = new BBLSubTask(task);
            subTask->task_gcode_in_3mf = (boost::format(GCODE_FILE_FORMAT) % (curr_plate_idx)).str();

            subTask->task_partplate_idx = std::to_string(curr_plate_idx);
            subTask->task_printer_dev_id = m_dev_id;

            if (project->project_name.empty())
                subTask->task_name = wxString::Format(_L("Plate %d"), curr_plate_idx).ToUTF8().data();
            else
                subTask->task_name = wxString::Format(_L("%s (Plate %d)"), from_u8(project->project_name), curr_plate_idx).ToUTF8().data();

            task->subtasks.push_back(subTask);
            BOOST_LOG_TRIVIAL(trace) << "print_job: start to request_subtask_id, index = " << i;
            res = c->request_subtask_id(subTask, http_code, http_body);
            if (res != 0 && subTask->task_id.empty()) {
                wxString error_msg = wxString::Format(_L("req_sub,err:code=%u,msg=%s"), http_code, http_body);
                update_status(curr_percent, failed_to_sending_str + error_msg);
                return;
            }

            /* set curr_subtask to first task */
            if (i == 0) {
                curr_subtask = subTask;
            }
        }
    }
    else {
        int total_subtask_num = 1;
        if (job_data.plate_idx >= 0)
            curr_plate_idx = job_data.plate_idx + 1;
        else if (job_data.plate_idx == PLATE_CURRENT_IDX)
            curr_plate_idx = m_plater->get_partplate_list().get_curr_plate_index() + 1;

        /* create subTask from current plate */
        BBLSubTask* subTask = new BBLSubTask(task);
        subTask->task_gcode_in_3mf = (boost::format(GCODE_FILE_FORMAT) % (curr_plate_idx)).str();
        subTask->task_partplate_idx = std::to_string(curr_plate_idx);
        subTask->task_printer_dev_id = m_dev_id;
        if (project->project_name.empty())
            subTask->task_name = wxString::Format(_L("Plate %d"), curr_plate_idx).ToUTF8().data();
        else
            subTask->task_name = wxString::Format(_L("%s (Plate %d)"), from_u8(project->project_name), curr_plate_idx).ToUTF8().data();

        task->subtasks.push_back(subTask);

        BOOST_LOG_TRIVIAL(trace) << "print_job: start to request_subtask_id";
        res = c->request_subtask_id(subTask, http_code, http_body);
        if (res < 0 && subTask->task_id.empty()) {
            wxString error_msg = wxString::Format(_L("req_sub,err:code=%u,msg=%s"), http_code, http_body);
            update_status(curr_percent, failed_to_sending_str + error_msg);
            return;
        }

        /* set curr_subtask to subtask */
        curr_subtask = subTask;
    }

    if (was_canceled()) {
        update_status(curr_percent, printjob_cancel_str);
        BOOST_LOG_TRIVIAL(trace) << "print_job: subtask is canceled after reqeust_sub_task_id ...";
        return;
    }

    if (!curr_subtask) {
        BOOST_LOG_TRIVIAL(trace) << "print_job: curr_subtask is null";
        return;
    }

    BOOST_LOG_TRIVIAL(trace) << "print_job: poll task 3mf";
    res = c->get_subtask_3mf(curr_subtask,
        [this]() {
            return was_canceled();
        }
    );
    if (res == RET_POLLING_CANEL) {
        update_status(curr_percent, printjob_cancel_str);
        return;
    } else if (res == RET_POLLING_TIMEOUT) {
        update_status(curr_percent, timeout_to_sending_str);
        return;
    }

    if (res== 0 && !curr_subtask->task_url.empty()
        && curr_subtask->task_url.compare("null") != 0
        && !curr_subtask->task_url_md5.empty()) {
        curr_percent = 95;
        update_status(curr_percent, msg);
        BOOST_LOG_TRIVIAL(trace) << "get subtask url =" << curr_subtask->task_url;
    }
    else {
        wxString error_msg = wxString::Format(_L("pol_3mf,err:code=%u,msg=%s"), http_code, http_body);
        update_status(curr_percent, failed_to_sending_str + error_msg);
        return;
    }

    //subTask url
    MachineObject* obj = c->find_machine(m_dev_id);
    if (obj) {
        BOOST_LOG_TRIVIAL(trace) << "print_job: send subtask";
        // upload and send to machine
        res = obj->send_wan_print_subtask(curr_subtask);
        if (res < 0) {
            update_status(curr_percent, failed_to_sending_str);
            return;
        }
        curr_percent = 100;
        msg = _L("The printing task was sent successfully!");
        update_status(100, msg);

        // add to user project
        c->myProjectList.insert(std::make_pair(project->project_id, project));

        wxCommandEvent evt(m_print_job_completed_id);
        evt.SetString(m_dev_id);
        wxQueueEvent(m_plater, evt.Clone());
        m_job_finished = true;
    } else {
        msg = _L("Internal Error, Please try again!");
        update_status(curr_percent, msg);
    }
}

void PrintJob::finalize() {
    if (was_canceled()) return;

    Job::finalize();
}

}} // namespace Slic3r::GUI
