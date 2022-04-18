#ifndef __BindJob_HPP__
#define __BindJob_HPP__

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include "PlaterJob.hpp"

namespace fs = boost::filesystem;

namespace Slic3r {
namespace GUI {

class BindJob : public PlaterJob
{
    std::function<void()> m_success_fun{nullptr};
    std::string         m_dev_id;
    bool                m_job_finished{ false };
    int                 m_print_job_completed_id = 0;

protected:
    void on_exception(const std::exception_ptr &) override;
public:
    BindJob(std::shared_ptr<ProgressIndicator> pri, Plater *plater, std::string dev_id = "");

    int  status_range() const override
    {
        return 100;
    }

    bool is_finished() { return m_job_finished;  }

    void on_success(std::function<void()> success);
    void process() override;
    void finalize() override;
};

}} // namespace Slic3r::GUI

#endif // ARRANGEJOB_HPP
