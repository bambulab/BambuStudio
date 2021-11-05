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
    std::string     machine_sn;
    int             plate_idx;
    fs::path        _3mf_path;
    PrintPrepareData() {
        plate_idx = 0;
    }
};

class PrintJob : public PlaterJob
{
    PrintPrepareData job_data;

protected:

    void prepare() override;

    void on_exception(const std::exception_ptr &) override;
public:
    PrintJob(std::shared_ptr<ProgressIndicator> pri, Plater *plater)
        : PlaterJob{std::move(pri), plater}
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
