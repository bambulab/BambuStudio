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

class BBLProject;
class BBLProfile;
class BBLTask;

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

    BBLSubTask(BBLTask* task = nullptr);

    std::string     task_id;            /* plate id */
    std::string     task_name;          /* task name, generally filename as task name */
    std::string     task_file;          /* local full file path of 3mf or gcode */
    fs::path        task_path;          /* local path of 3mf or gcode */
    std::string     task_gcode_in_3mf;  /* gcode in 3mf */
    std::string     task_create_time;   /* time created by cloud */
    std::string     task_update_time;   /* time updated by cloud */
    std::string     task_start_time;    /* time created by machine, seconds from 1970-01-01 */
    std::string     task_duration;      /* duration created by machine, unit seconds */

    // task of plate info
    std::string     task_prediction;    /* prediction printing time of plate, unit seconds */
    std::string     task_weight;        /* weight create by slicer */
    std::string     task_partplate_idx; /* partplate_idx, start at 1, 2, etc. */

    SubTaskStatus   task_status;
    std::string     task_printer_dev_id;/* dev_id of machine */
    int             task_progress;      /* task running progress, update by machine */
    std::string     printing_status;    /* task status, update by machine */
    std::string     task_url;           /* post task to this url */
    std::string     task_url_md5;       /* md5 of task file */
    BBLTask*        parent_task_;
    std::string     parent_id;

    std::string build_content_json();
    int parse_content_json(std::string json);
    static BBLSubTask::SubTaskStatus parse_status(std::string status);
};

class BBLTask {
public:
    enum TaskStatus {
        TASK_ACTIVE = 0,
        TASK_INACTIVE = 1,
    };

    BBLTask(BBLProfile* profile = nullptr);

    /* properties */
    std::string                 task_id;
    std::string                 task_name;
    std::string                 task_create_time;
    std::string                 task_update_time;
    TaskStatus                  task_status;
    std::wstring                task_file;          /* local task file */
    std::string                 task_url;           /* cloud task url */
    std::string                 task_url_md5;       /* md5 of cloud task url file */
    std::wstring                task_dst_url;       /* put task to dest url in machine */
    BBLProfile*                 profile_;
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
    int parse_content_json(std::string json);
};

class BBLSliceInfo {
public:
    BBLSliceInfo(BBLProfile* profile = nullptr)
    {
        profile_ = profile;
        prediction = 0;
    }

    std::string     index;
    std::string     title;
    std::string     thumbnail_dir;
    std::string     thumbnail_name;
    std::string     thumbnail_url;
    std::string     gcode_name;
    std::string     gcode_url;
    std::string     gcode_dir;
    std::string     config_url;
    std::string     weight;
    int             prediction;
    BBLProfile*     profile_;
};

class BBLProfile {
public:
    BBLProfile(BBLProject* project = nullptr);

    std::vector<BBLTask*>   tasks;
    std::string             profile_id;
    std::string             profile_name;
    std::string             profile_content;
    std::string             project_id;         /* parent project_id */
    BBLProject*             project_;
    std::map<std::string, BBLSliceInfo*>    slice_info; /* key: plate_idx, start at 1, 2, 3, etc. */
    BBLSliceInfo* get_slice_info(std::string plate_idx);
};

class BBLProject {
public:
    enum ProjectType {
        PROJECT_3MF = 0,
        PROJECT_GCODE = 1,
    };
    BBLProject() {
        /* give a default project name */
        project_name = "Untitled";
    }
    BBLProject(std::string name, ProjectType type = PROJECT_3MF) {
        project_type = type;
        project_name = name;
    }

    ProjectType     project_type;
    std::string     project_id;
    std::string     project_model_id;
    std::string     project_status;
    std::string     project_create_time;    /* created by cloud */
    std::string     project_url;            /* url storage on cloud */
    std::string     project_url_md5;        /* md5 of project url file */
    std::string     project_name;
    std::string     project_3mf_file;
    fs::path        project_path;
    std::string     project_content;


    std::vector<BBLProfile*>   profiles;

    /* deprecated apis */
    std::string build_content_json();
    void set_name(std::string name) { project_name = name; }
};

} // namespace Slic3r

#endif //  slic3r_ProjectTask_hpp_
