#ifndef SLAIMPORTJOB_HPP
#define SLAIMPORTJOB_HPP

#include "PlaterJob.hpp"

namespace Slic3r { namespace GUI {

class SLAImportJob : public PlaterJob {
    class priv;

    std::unique_ptr<priv> p;

protected:
    void prepare() override;
    void process() override;
    void finalize() override;

public:
    SLAImportJob(std::shared_ptr<ProgressIndicator> pri, Plater *plater);
    ~SLAImportJob();

    void reset();
};

}}     // namespace Slic3r::GUI

#endif // SLAIMPORTJOB_HPP
