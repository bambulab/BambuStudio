#include "PrintJob.hpp"

#include "libslic3r/MTUtils.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"

#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"


namespace Slic3r { namespace GUI {


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
    int res = -1;
    Slic3r::AccountManager* c = Slic3r::GUI::wxGetApp().getAccountManager();
    Slic3r::DeviceManager* d  = Slic3r::GUI::wxGetApp().getDeviceManager();

    int total_plate_num = m_plater->get_partplate_list().get_plate_count();

    PartPlate* plate = m_plater->get_partplate_list().get_plate(job_data.plate_idx);
    if (plate == nullptr) {
        plate = m_plater->get_partplate_list().get_curr_plate();
        if (plate == nullptr)
        return;
    }

    if (was_canceled()) {
        update_status(0, "Sending Task canceled");
        return;
    }

    std::map<std::string, MachineObject*>::iterator it = c->myBindMachineList.find(m_dev_id);
    if (it == c->myBindMachineList.end()) {
        update_status(0, "can not find machine = " + m_dev_id);
        return;
    }

    // save project, profile, task, subtask info to local, default_output_file as project name
    std::string project_name;
    if (c->get_default_project())
        std::string project_name = c->get_default_project()->project_name;
    BBLProject* project = new BBLProject(project_name, BBLProject::ProjectType::PROJECT_3MF);
    project->project_3mf_file = job_data._3mf_path.string();
    project->project_path = fs::path(project->project_3mf_file);

    BOOST_LOG_TRIVIAL(trace) << "print_job: request project id";
    res = c->request_project_id(project);

    if (res == 0 && !project->project_id.empty()) {
        update_status(5, "request project id ok!");
    } else {
        BOOST_LOG_TRIVIAL(trace) << "request project id failed!";
        update_status(0, "reqeust project id failed!");
        return;
    }

    /* select a profile, use default now */
    BBLProfile* profile = new BBLProfile(project);
    // set current print preset to profile_name
    profile->profile_name = wxGetApp().preset_bundle->prints.get_selected_preset_name();
    
    BOOST_LOG_TRIVIAL(trace) << "print_job: request profile id";
    res = c->request_profile_id(profile,
        [this](int result, std::string info) {
            if (result == 0) {
                update_status(10, "request profile id ok!");
            }
            else {
                update_status(5, "request profile id failed!");
            }
        }
        );

    if (res == 0 && !profile->profile_id.empty()) {
        update_status(10, "request profile id ok!");
    }
    else {
        update_status(5, "request profile id failed!");
        return;
    }

    /* upload and poll */
    BOOST_LOG_TRIVIAL(trace) << "print_job: start to uploading...";
    res = c->upload_3mf(profile,
        //ResultFn
        [this](int result, std::string info) {
            if (result == 0) {
                update_status(80, "upload 3mf ok!");
            }
            else {
                update_status(10, "upload 3mf failed!");
            }
        },
        [this](Http::Progress progress, bool &cancel) {
            int percent = 0;
            if (progress.ultotal != 0) {
                percent = progress.ulnow / progress.ultotal;
            }
            if (was_canceled()) {
                cancel = true;
                update_status(percent, "3mf uploading canceled");
                return;
            }
            percent = 10 + percent * 70 / 100;
            update_status(percent, "3mf uploading...");
        },
        true);

    if (was_canceled()) {
        update_status(10, "Printing Task is canceled in uploading...");
        BOOST_LOG_TRIVIAL(trace) << "print_job: subtask is canceled when uploading...";
        return;
    }

    if (res < 0) {
        update_status(10, "3mf uploading failed");
        return;
    }

    /* create Task */
    BBLTask* task = new BBLTask(profile);
    task->task_name = (boost::format("%1%_%2%_P%3%") % project->project_name % profile->profile_name % total_plate_num).str();

    /* rqeust task id */
    BOOST_LOG_TRIVIAL(trace) << "print_job: start to request_task_id";
    c->request_task_id(task);

    if (!task->task_id.empty()) {
        update_status(85, "request task id ok!");
    }
    else {
        update_status(80, "request task id failed!");
        return;
    }
    
    BBLSubTask* curr_subtask = nullptr;
    int curr_plate_idx = 0;
    if (job_data.plate_idx == PLATE_ALL_IDX) {
        for (int i = 0; i < total_plate_num; i++) {
            int subtask_percent_duration = 5;
            curr_plate_idx = i + 1;

            /* create subTask from current plate */
            BBLSubTask* subTask = new BBLSubTask(task);
            subTask->task_gcode_in_3mf = (boost::format(GCODE_FILE_FORMAT) % (curr_plate_idx)).str();

            subTask->task_partplate_idx = std::to_string(curr_plate_idx);
            subTask->task_printer_dev_id = m_dev_id;
            subTask->task_name = (boost::format("%1%_P%2%_T%3%") % profile->profile_name % curr_plate_idx %total_plate_num).str();

            task->subtasks.push_back(subTask);

            BOOST_LOG_TRIVIAL(trace) << "print_job: start to request_subtask_id, index = " << i;
            c->request_subtask_id(subTask);
            if (!subTask->task_id.empty()) {
                update_status(90, "request subtask id ok!");
            }
            else {
                update_status(80, "request subtask id failed!");
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
        subTask->task_name = (boost::format("%1%_P%2%_T%3%") % profile->profile_name % curr_plate_idx % total_subtask_num).str();

        task->subtasks.push_back(subTask);

        BOOST_LOG_TRIVIAL(trace) << "print_job: start to request_subtask_id";
        c->request_subtask_id(subTask);

        if (!subTask->task_id.empty()) {
            update_status(90, "request subtask id ok!");
        }
        else {
            update_status(80, "request subtask id failed!");
            return;
        }

        /* set curr_subtask to subtask */
        curr_subtask = subTask;
    }

    if (!curr_subtask) return;

    BOOST_LOG_TRIVIAL(trace) << "print_job: poll task 3mf";
    res = c->poll_3mf(curr_subtask,
        [this]() {
            return was_canceled();
        }
    );

    if (was_canceled()) {
        update_status(90, "Printing Task is canceled in poll 3mf (subtask)");
        BOOST_LOG_TRIVIAL(trace) << "print_job: subtask is canceled in poll 3mf (subtask)";
        return;
    }

    if (!curr_subtask->task_url.empty()
        && curr_subtask->task_url.compare("null") != 0
        && !curr_subtask->task_url_md5.empty()) {
        update_status(95, "poll 3mf of task ok!");
        BOOST_LOG_TRIVIAL(trace) << "get subtask url =" << curr_subtask->task_url;
    }
    else {
        update_status(90, "poll 3mf of task failed!");
        return;
    }

    //subTask url
    MachineObject* obj = c->find_machine(m_dev_id);
    if (obj) {
        BOOST_LOG_TRIVIAL(trace) << "print_job: send subtask";
        // upload and send to machine
        obj->send_wan_print_subtask(curr_subtask);
        update_status(100, "send task ok!");

        // add to user project
        c->myProjectList.insert(std::make_pair(project->project_id, project));

        m_plater->print_job_finished();
    }
}

void PrintJob::finalize() {
    // Ignore the arrange result if aborted.
    if (was_canceled()) return;

    Job::finalize();
}

}} // namespace Slic3r::GUI
