#ifndef __PublishJob_HPP__
#define __PublishJob_HPP__

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include "PlaterJob.hpp"
#include "bambu_networking.hpp"

namespace fs = boost::filesystem;

namespace Slic3r {
namespace GUI {

class PublishJob : public PlaterJob
{
    std::function<void()> m_success_fun{nullptr};
    std::string         m_dev_id;
    bool                m_job_finished{ false };
    int                 m_publish_job_completed_id = 0;

protected:

    void prepare() override;

    void on_exception(const std::exception_ptr &) override;
public:
    PublishJob(std::shared_ptr<ProgressIndicator> pri, Plater *plater);

    struct BBL::PublishParams  publish_params;

    int  status_range() const override
    {
        return 100;
    }

    bool is_finished() { return m_job_finished;  }
    void set_publish_job_finished_event(int event_id) { m_publish_job_completed_id = event_id; }

    void on_success(std::function<void()> success);
    wxString get_http_error_msg(unsigned int status, std::string body);
    void process() override;
    void finalize() override;
};

}} // namespace Slic3r::GUI

#endif
