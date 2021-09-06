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

    BBLTask::BBLTask()
    {
        std::default_random_engine generator;
        std::uniform_int_distribution<int> distribution(1e5, 1e6);
        task_id = distribution(generator);
    }

    std::string BBLProject::build_content_json()
    {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;

        pt::ptree root, js_tasks;
        for (int i = 0; i < profiles.size(); i++) {
            for (int j = 0; j < profiles[i]->tasks.size(); j++) {
                pt::ptree js_task, js_subtasks;
                BBLTask* task = profiles[i]->tasks[j];
                std::string task_name_str = converter.to_bytes(task->task_name);
                std::string task_create_time_str = converter.to_bytes(task->task_create_time);

                js_task.put("id", task->task_id);
                js_task.put("name", task_name_str);
                js_task.put("create_time", task_create_time_str);
                js_task.put("status", task->task_status_str());

                for (int k = 0; k < task->subtasks.size(); k++) {
                    pt::ptree js_subtask;
                    BBLSubTask* subtask = task->subtasks[k];
                    std::string task_name_str = converter.to_bytes(subtask->task_name);
                    std::string task_create_time_str = converter.to_bytes(subtask->task_create_time);

                    js_subtask.put("id", subtask->task_id);
                    js_subtask.put("name", task_name_str);
                    js_subtask.put("create_time", task_create_time_str);
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
        pt::write_json(oss, root);
        return oss.str();
    }


} // namespace Slic3r
