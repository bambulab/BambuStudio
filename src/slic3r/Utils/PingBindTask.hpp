#ifndef slic3r_PingBindTask_hpp_
#define slic3r_PingBindTask_hpp_

#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <utility>

namespace Slic3r {

// Shared with the dialog, but contains no GUI objects. result is read only
// after completed becomes true (the atomic publishes the worker's write).
struct PingBindRequest
{
    std::atomic<bool> canceled{false};
    std::atomic<bool> completed{false};
    int result = -1;

    // Skip queued work or discard its result; an active DLL call runs to completion.
    void cancel() { canceled.store(true); }
};

// Owned by GUI_App, so closing a dialog never has to join a network thread.
class PingBindTask
{
public:
    using RunFn = std::function<int()>;

    PingBindTask(RunFn run, std::shared_ptr<PingBindRequest> request)
        : m_request(std::move(request)),
          m_thread([request = m_request, run = std::move(run)] {
              try {
                  if (!request->canceled.load())
                      request->result = run();
              } catch (...) {
                  request->result = -1;
              }
              request->completed.store(true);
          })
    {}

    ~PingBindTask()
    {
        if (!completed())
            m_request->cancel();
        if (m_thread.joinable())
            m_thread.join();
    }

    PingBindTask(const PingBindTask&) = delete;
    PingBindTask& operator=(const PingBindTask&) = delete;

    bool completed() const { return m_request->completed.load(); }
    void cancel() { m_request->cancel(); }

private:
    std::shared_ptr<PingBindRequest> m_request;
    std::thread m_thread;
};

} // namespace Slic3r

#endif
