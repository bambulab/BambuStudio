#ifndef SendJOB_HPP
#define SendJOB_HPP

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include "PlaterJob.hpp"
#include "PrintJob.hpp"

namespace fs = boost::filesystem;

namespace Slic3r {
namespace GUI {

typedef std::function<void(int status, int code, std::string msg)> OnUpdateStatusFn;
typedef std::function<bool()>                       WasCancelledFn;

class SendJob : public PlaterJob
{
    PrintPrepareData    job_data;
    std::string         m_dev_id;
    bool                m_job_finished{ false };
    int                 m_print_job_completed_id = 0;
    std::function<void()> m_success_fun{nullptr};

protected:

    void prepare() override;

    void on_exception(const std::exception_ptr &) override;
public:
    SendJob(std::shared_ptr<ProgressIndicator> pri, Plater *plater, std::string dev_id = "");

    std::string m_project_name;
    std::string m_dev_ip;
    std::string m_access_code;
    std::string task_bed_type;
	std::string task_ams_mapping;
	std::string connection_type;

    bool        cloud_print_only { false };
    bool        has_sdcard { false };
    bool        task_use_ams { true };

    wxWindow*   m_parent{nullptr};

    int  status_range() const override
    {
        return 100;
    }

    wxString get_http_error_msg(unsigned int status, std::string body);
    bool is_finished() { return m_job_finished;  }
    void process() override;
    void on_success(std::function<void()> success);
    void finalize() override;
    void set_project_name(std::string name);
};

}}

#endif
