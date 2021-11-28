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

protected:

    void prepare() override;

    void on_exception(const std::exception_ptr &) override;
public:
    PrintJob(std::shared_ptr<ProgressIndicator> pri, Plater *plater, std::string dev_id = "")
        : PlaterJob{std::move(pri), plater},
        m_dev_id(dev_id)
    {}

    int status_range() const override
    {
        return 100;
    }

    void process() override;
    void finalize() override;
};

}} // namespace Slic3r::GUI

#endif // ARRANGEJOB_HPP
