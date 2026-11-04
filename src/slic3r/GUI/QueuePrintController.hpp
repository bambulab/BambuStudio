#ifndef slic3r_GUI_QueuePrintController_hpp_
#define slic3r_GUI_QueuePrintController_hpp_

#include <functional>
#include <memory>
#include <string>

namespace Slic3r { namespace GUI {

constexpr int kMaxQueuePlateIndex = 255;

struct QueuePrintRequest
{
    std::string queue_plate_id;
    std::string model_id;
    std::string profile_id;
    int         plate_index = 1;
    std::string title;
    std::string device_id;
    std::string cover_url;
};

class QueuePrintController
{
public:
    using Completion = std::function<void(int, const std::string&)>;

    static QueuePrintController& Get();
    void PrepareAndShow(QueuePrintRequest request, Completion completion = {});

private:
    QueuePrintController() = default;

    std::shared_ptr<int> m_alive = std::make_shared<int>(0);
};

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_QueuePrintController_hpp_
