#ifndef PrintJOB_HPP
#define PrintJOB_HPP

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include "PlaterJob.hpp"

namespace fs = boost::filesystem;

namespace Slic3r { namespace GUI {

class PrintPrepareData
{
public:
    int             plate_idx;
    fs::path        _3mf_path;
    PrintPrepareData() {
        plate_idx = 0;
    }
};

class PrintJob : public PlaterJob
{
    PrintPrepareData    job_data;
    std::string         m_dev_id;
    bool                m_job_finished{ false };
    int                 m_print_job_completed_id = 0;

protected:

    void prepare() override;

    void on_exception(const std::exception_ptr &) override;
public:
    PrintJob(std::shared_ptr<ProgressIndicator> pri, Plater *plater, std::string dev_id = "");

    int status_range() const override
    {
        return 100;
    }

    bool is_finished() { return m_job_finished;  }
    void set_print_job_finished_event(int event_id) { m_print_job_completed_id = event_id; }

    void process() override;
    void finalize() override;
};

}} // namespace Slic3r::GUI

#endif // ARRANGEJOB_HPP
