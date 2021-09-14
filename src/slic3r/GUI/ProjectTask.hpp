#ifndef slic3r_ProjectTask_hpp_
#define slic3r_ProjectTask_hpp_

#include <map>
#include <vector>
#include <string>
#include <memory>
#include <boost/thread.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;

namespace Slic3r {

class BBLSubTask {
public:
    enum SubTaskStatus {
        TASK_CREATED = 0,
        TASK_READY = 1,
        TASK_RUNNING = 2,
        TASK_PAUSE = 3,
        TASK_FAILED = 4,
        TASK_FINISHED = 5
    };

    BBLSubTask() {}

    std::string     parent_task_id;
    std::string     task_id;            /* created by cloud */
    std::wstring    task_name;          /* task name, generally filename as task name */
    std::wstring    task_file;          /* local full file path of 3mf or gcode */
    fs::path        task_path;          /* local path of 3mf or gcode */
    std::string     task_create_time;   /* time created by slicer */
    int             task_partplate_idx; 
    SubTaskStatus   task_status;
    std::string     task_printer_dev_id;/* dev_id of machine */
    int             task_progress;      /* task running progress */
    std::string     task_url;           /* post task to this url */
    std::string     task_url_md5;       /* md5 of task file */
    std::string     task_project_id;
    std::string     task_profile_id;
};

class BBLTask {
public:
    enum TaskStatus {
        TASK_ACTIVE = 0,
        TASK_INACTIVE = 1,
    };

    BBLTask();

    /* properties */
    std::string                 task_id;
    std::wstring                task_name;
    std::wstring                task_create_time;
    TaskStatus                  task_status;
    std::wstring                task_file;          /* local task file */
    std::string                 task_url;           /* cloud task url */
    std::string                 task_url_md5;       /* md5 of cloud task url file */
    std::wstring                task_dst_url;       /* put task to dest url in machine */
    std::string                 task_project_id;
    std::string                 task_profile_id;
    std::vector<BBLSubTask*>    subtasks;

    std::string task_status_str() {
        if (task_status == TASK_ACTIVE) {
            return "active";
        }
        else if (task_status == TASK_INACTIVE) {
            return "inactive";
        }
        else {
            return "inactive";
        }
    }

    std::string build_content_json();
};

class BBLProfile {
public:
    BBLProfile() {}

    std::vector<BBLTask*>   tasks;
    std::string             profile_id;
    std::wstring            profile_name;
    std::string             profile_content;
};

class BBLProject {
public:
    enum ProjectType {
        PROJECT_3MF = 0,
        PROJECT_GCODE = 1,
    };

    BBLProject(std::wstring name, ProjectType type = PROJECT_3MF) {
        project_type = type;
        project_name = name;
    }

    ProjectType     project_type;
    std::string     project_id;
    std::string     project_model_id;
    std::string     project_url;        /* url storage on cloud */
    std::string     project_url_md5;    /* md5 of project url file */
    std::wstring    project_name;
    std::wstring    project_3mf_file;
    fs::path        project_path;

    std::vector<BBLProfile*>   profiles;

    /* deprecated apis */
    std::string build_content_json();
};

} // namespace Slic3r

#endif //  slic3r_ProjectTask_hpp_
