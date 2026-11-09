#ifndef slic3r_GUI_QueuePrintContext_hpp_
#define slic3r_GUI_QueuePrintContext_hpp_

#include <memory>
#include <string>

#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/ProjectTask.hpp"

namespace Slic3r { namespace GUI {

// The queue controller owns how cloud metadata becomes print data. SelectMachine
// only consumes this immutable, already-validated context.
struct QueuePrintContext
{
    std::string queue_plate_id;
    std::string model_id;
    std::string profile_id;
    std::string device_id;
    std::string title;
    std::string cover_url;
    // Queue plateIndex already matches slice_info.config and the V1 task's
    // one-based plateIndex convention.
    int         queue_plate_index = 1;
    int         cloud_plate_index = 1;

    DynamicPrintConfig          config;
    std::shared_ptr<PlateData>  plate_data;
};

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_QueuePrintContext_hpp_
