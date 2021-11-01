#include "libslic3r/libslic3r.h"
#include "DeviceManager.hpp"
#include "libslic3r/Time.hpp"
#include "libslic3r/Thread.hpp"
#include "slic3r/Utils/Http.hpp"


#include <thread>
#include <mutex>
#include <codecvt>

#include <boost/random.hpp>
#include <boost/generator_iterator.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

namespace pt = boost::property_tree;

namespace Slic3r {

    BBLProfile::BBLProfile(BBLProject* project)
    {
        project_ = nullptr;
        if (project) {
            project_ = project;
            project_id = project_->project_id;
        }

        profile_name = "N/A";
    }

    BBLTask::BBLTask(BBLProfile* profile)
    {
        profile_ = nullptr;
        if (profile) {
            profile_ = profile;
            task_profile_id = profile->profile_id;
            task_project_id = profile->project_id;
        }

        /* get create time */
        long t = wxGetUTCTime();
        wxDateTime now = wxDateTime::Now().MakeUTC();
        task_create_time = now.FormatISOCombined(' ').ToStdString();
    }

    BBLSubTask::BBLSubTask(BBLTask* task) {
        parent_task_ = task;

        /* get create time */
        long t = wxGetUTCTime();
        wxDateTime now = wxDateTime::Now().MakeUTC();
        task_create_time = now.FormatISOCombined(' ').ToStdString();

        task_progress = 0;
    }

    std::string BBLSubTask::build_content_json()
    {
        pt::ptree root, info;
        info.put("name", task_name);
        info.put("create_time", task_create_time);
        info.put("plate_idx", task_partplate_idx);
        info.put("printer", task_printer_dev_id);
        info.put("prediction", task_prediction);
        info.put("weight", task_weight);
        root.put_child("info", info);

        std::stringstream oss;
        pt::write_json(oss, root, false);
        return oss.str();
    }

    int BBLSubTask::parse_content_json(std::string json)
    {
        try {
            std::stringstream ss(json);
            pt::ptree root, info;
            pt::read_json(ss, root);

            info = root.get_child("info");

            /* create subtasks */
            boost::optional<std::string> subtask_name = info.get_optional<std::string>("name");
            if (subtask_name.has_value()) task_name = subtask_name.value();

            boost::optional<std::string> subtask_create_time = info.get_optional<std::string>("create_time");
            if (subtask_create_time.has_value()) task_create_time = subtask_create_time.value();

            boost::optional<std::string> subtask_plate_idx = info.get_optional<std::string>("plate_idx");
            if (subtask_plate_idx.has_value()) task_partplate_idx = std::stoi(subtask_plate_idx.value());

            boost::optional<std::string> subtask_printer = info.get_optional<std::string>("printer");
            if (subtask_printer.has_value()) task_printer_dev_id = subtask_printer.value();

            boost::optional<std::string> subtask_prediction = info.get_optional<std::string>("prediction");
            if (subtask_prediction.has_value()) task_prediction = subtask_prediction.value();

            boost::optional<std::string> subtask_weight = info.get_optional<std::string>("weight");
            if (subtask_weight.has_value()) task_weight = subtask_weight.value();
        }
        catch (...) {
            BOOST_LOG_TRIVIAL(trace) << "parse_content_json failed! json=" << json;
            return -1;
        }
        return 0;
    }

    BBLSubTask::SubTaskStatus BBLSubTask::parse_status(std::string status)
    {
        if (status.compare("CREATED") == 0) {
            return BBLSubTask::SubTaskStatus::TASK_CREATED;
        }
        else if (status.compare("READY") == 0) {
            return BBLSubTask::SubTaskStatus::TASK_READY;
        }
        else if (status.compare("RUNNING") == 0) {
            return BBLSubTask::SubTaskStatus::TASK_RUNNING;
        }
        else if (status.compare("PAUSE") == 0) {
            return BBLSubTask::SubTaskStatus::TASK_PAUSE;
        }
        else if (status.compare("FAILED") == 0) {
            return BBLSubTask::SubTaskStatus::TASK_FAILED;
        }
        else if (status.compare("FINISHED") == 0) {
            return BBLSubTask::SubTaskStatus::TASK_FINISHED;
        }
        else {
            return BBLSubTask::SubTaskStatus::TASK_CREATED;
        }
    }

    std::string BBLTask::build_content_json()
    {
        /*
        { 
            # Only for task
            "config": {
                "key1" : "value1",
                "key2" : "value2",
                "key3" : "value3"
                ...
                }
            }
        }
        */
        pt::ptree root, config;
        root.put_child("config", config);
        std::stringstream oss;
        pt::write_json(oss, root, false);
        return oss.str();
    }

    int BBLTask::parse_content_json(std::string json)
    {
        try {
            std::stringstream ss(json);
            pt::ptree root;
            pt::read_json(ss, root);

            for (int i = 0; i < subtasks.size(); i++) {
                delete subtasks[i];
            }
            subtasks.clear();
            if (root.get_child_optional("subtasks")!= boost::none) {
                pt::ptree subtask_list = root.get_child("subtasks");
                for (auto subtask = subtask_list.begin(); subtask != subtask_list.end(); ++subtask) {
                    BBLSubTask* new_subtask = new BBLSubTask(this);
                    /* create subtasks */
                    boost::optional<std::string> subtask_id = subtask->second.get_optional<std::string>("id");
                    if (subtask_id.has_value()) new_subtask->task_id = subtask_id.value();

                    boost::optional<std::string> subtask_name = subtask->second.get_optional<std::string>("name");
                    if (subtask_name.has_value()) new_subtask->task_name = subtask_name.value();

                    boost::optional<std::string> subtask_create_time = subtask->second.get_optional<std::string>("create_time");
                    if (subtask_create_time.has_value()) new_subtask->task_create_time = subtask_create_time.value();

                    boost::optional<std::string> subtask_plate_idx = subtask->second.get_optional<std::string>("plate_idx");
                    if (subtask_plate_idx.has_value()) new_subtask->task_partplate_idx = std::stoi(subtask_plate_idx.value());

                    boost::optional<std::string> subtask_printer = subtask->second.get_optional<std::string>("printer");
                    if (subtask_printer.has_value()) new_subtask->task_printer_dev_id = subtask_printer.value();

                    boost::optional<std::string> subtask_prediction = subtask->second.get_optional<std::string>("prediction");
                    if (subtask_prediction.has_value()) new_subtask->task_prediction = subtask_prediction.value();

                    boost::optional<std::string> subtask_weight = subtask->second.get_optional<std::string>("weight");
                    if (subtask_weight.has_value()) new_subtask->task_weight = subtask_weight.value();
                    subtasks.push_back(new_subtask);
                }
            }
        }
        catch (...) {
            BOOST_LOG_TRIVIAL(trace) << "parse_content_json failed! json=" << json;
        }
        return 0;
    }

    std::string BBLProject::build_content_json()
    {
        pt::ptree root, js_tasks;
        for (int i = 0; i < profiles.size(); i++) {
            for (int j = 0; j < profiles[i]->tasks.size(); j++) {
                pt::ptree js_task, js_subtasks;
                BBLTask* task = profiles[i]->tasks[j];
                std::string task_create_time_str = task->task_create_time;

                js_task.put("id", task->task_id);
                js_task.put("name", task->task_name);
                js_task.put("create_time", task_create_time_str);
                js_task.put("status", task->task_status_str());

                for (int k = 0; k < task->subtasks.size(); k++) {
                    pt::ptree js_subtask;
                    BBLSubTask* subtask = task->subtasks[k];
                    js_subtask.put("id", subtask->task_id);
                    js_subtask.put("name", subtask->task_name);
                    js_subtask.put("create_time", subtask->task_create_time);
                    js_subtask.put("plane", subtask->task_partplate_idx);
                    js_subtask.put("printer", subtask->task_printer_dev_id);
                    /* status, progress updated by printer */
                    js_subtasks.push_back(std::make_pair("", js_subtask));
                }
                js_task.put_child("subtasks", js_subtasks); 
                js_tasks.push_back(std::make_pair("", js_task));
            }
        }
        root.put_child("tasks", js_tasks);
        
        std::stringstream oss;
        pt::write_json(oss, root, false);
        return oss.str();
    }


} // namespace Slic3r
