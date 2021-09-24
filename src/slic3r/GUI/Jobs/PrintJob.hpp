#ifndef PrintJOB_HPP
#define PrintJOB_HPP

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include "PlaterJob.hpp"
#include "slic3r/GUI/PartPlate.hpp"


namespace fs = boost::filesystem;

namespace Slic3r { namespace GUI {

class PrintJob : public PlaterJob
{
    std::string     machine_sn;
    fs::path        _3mf_path;
    PartPlate*      plate;
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
