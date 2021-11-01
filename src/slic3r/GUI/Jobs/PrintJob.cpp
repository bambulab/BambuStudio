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
    machine_sn = m_plater->get_prepared_machine_sn();
    _3mf_path = m_plater->get_prepared_3mf_path();
    plate = m_plater->get_partplate_list().get_curr_plate();
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

    /* 1 - lan print task*/
    std::map<std::string, MachineObject*>::iterator it = c->myBindMachineList.find(machine_sn);
    if (it == c->myBindMachineList.end()) {
        it = d->localMachineList.find(machine_sn);
        if (it == d->localMachineList.end()) {
            update_status(0, "can not find machine =" + machine_sn);
            return;
        }

        /* create a subtask */
        BBLSubTask* task = new BBLSubTask();
        task->task_file = encode_path(plate->get_tmp_gcode_path().c_str());
        it->second->send_print_subtask(task);
        return;
    }

    /* 2 - wan print task */
    // save project, profile, task, subtask info to local, default_output_file as project name
    // TODO set a 3mf name
    std::string project_name = boost::format("bbl_project_name").str();
    BBLProject* project = new BBLProject(project_name, BBLProject::ProjectType::PROJECT_3MF);
    project->project_3mf_file = _3mf_path.string();
    project->project_path = fs::path(project->project_3mf_file);

    res = c->request_project_id(project);

    if (res == 0 && !project->project_id.empty()) {
        update_status(5, "request project id ok!");
    } else {
        BOOST_LOG_TRIVIAL(trace) << "request project id failed!";
        update_status(0, "reqeust project id failed!");
        return;
    }

    /* TODO select a profile, use default now */
    BBLProfile* profile = new BBLProfile(project);
    profile->profile_name = "bbl_profile_name";

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
        [this](int percent) {
            // from 10 to 80
            int curr_percent = 10 + percent * 70 / 100;
            update_status(curr_percent, "3mf uploading...");
        },
        true);

    /* create Task */
    BBLTask* task = new BBLTask(profile);
    task->task_name = "bbl_task_name";

    /* rqeust task id */

    /* create subTask from current plate */
    plate->get_index();
    BBLSubTask* subTask = new BBLSubTask(task);
    subTask->task_gcode_in_3mf = (boost::format(GCODE_FILE_FORMAT) % (plate->get_index() + 1)).str();

    subTask->task_partplate_idx = plate->get_index();
    subTask->task_printer_dev_id = machine_sn;
    if (plate->get_slice_result()) {
        subTask->task_prediction = std::to_string((int)plate->get_slice_result()->print_statistics.modes[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)].time);
        const PrintStatistics& ps = m_plater->get_partplate_list().get_current_fff_print().print_statistics();
        if (ps.total_weight != 0.0) {
            subTask->task_weight = wxString::Format("%.2f", ps.total_weight).ToStdString();
        }
    }
    subTask->task_name = subTask->task_gcode_in_3mf;

    task->subtasks.push_back(subTask);

    c->request_task_id(task);

    if (!task->task_id.empty()) {
        update_status(85, "request task id ok!");
    }
    else {
        update_status(80, "request task id failed!");
        return;
    }


    c->request_subtask_id(subTask);

    if (!subTask->task_id.empty()) {
        update_status(90, "request subtask id ok!");
    }
    else {
        update_status(80, "request subtask id failed!");
        return;
    }

    res = c->poll_3mf(subTask);
    if (!subTask->task_url.empty() && !subTask->task_url_md5.empty()) {
        update_status(95, "poll 3mf of task ok!");
        BOOST_LOG_TRIVIAL(trace) << "get subtask url =" << subTask->task_url;
    }
    else {
        update_status(90, "poll 3mf of task failed!");
        return;
    }

    //subTask url
    MachineObject* obj = c->find_machine(machine_sn);
    if (obj) {
        // upload and send to machine
        obj->send_wan_print_subtask(subTask);
        update_status(100, "send task ok!");

        // add to user project
        c->myProjectList.insert(std::make_pair(project->project_id, project));

        m_plater->print_job_finished();
        finalize();
        return;
    }
}

void PrintJob::finalize() {
    // Ignore the arrange result if aborted.
    if (was_canceled()) return;

    Job::finalize();
}

}} // namespace Slic3r::GUI
