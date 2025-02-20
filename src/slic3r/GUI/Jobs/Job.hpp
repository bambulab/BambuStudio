#ifndef JOB_HPP
#define JOB_HPP

#include <atomic>
#include <exception>

#include "libslic3r/libslic3r.h"

#include <slic3r/GUI/I18N.hpp>

#include "ProgressIndicator.hpp"

#include <wx/event.h>

#include <boost/thread.hpp>

namespace Slic3r { namespace GUI {

// A class to handle UI jobs like arranging and optimizing rotation.
// These are not instant jobs, the user has to be informed about their
// state in the status progress indicator. On the other hand they are
// separated from the background slicing process. Ideally, these jobs should
// run when the background process is not running.
//
// TODO: A mechanism would be useful for blocking the plater interactions:
// objects would be frozen for the user. In case of arrange, an animation
// could be shown, or with the optimize orientations, partial results
// could be displayed.
class Job : public wxEvtHandler
{
    int               m_range = 100;
    int               m_thread_evt_id = wxID_ANY;
    boost::thread     m_thread;
    std::atomic<bool> m_running{false}, m_canceled{false};
    bool              m_finalized = false, m_finalizing = false;
    std::shared_ptr<ProgressIndicator> m_progress;
    std::exception_ptr                 m_worker_error = nullptr;
    
    void run(std::exception_ptr &);
    
protected:
    // status range for a particular job
    virtual int status_range() const { return 100; }
    
    // status update, to be used from the work thread (process() method)
    void update_status(int st, const wxString &msg = "");

    void update_percent_finish();

    void show_error_info(wxString msg, int code, wxString description, wxString extra);

    bool was_canceled() const { return m_canceled.load(); }

    // Launched just before start(), a job can use it to prepare internals
    virtual void prepare() {}

    // The method where the actual work of the job should be defined.
    virtual void process() = 0;
    
    // Launched when the job is finished. It refreshes the 3Dscene by def.
    virtual void finalize() { m_finalized = true; }

    // Exceptions occuring in process() are redirected from the worker thread
    // into the main (UI) thread. This method is called from the main thread and
    // can be overriden to handle these exceptions.
    virtual void on_exception(const std::exception_ptr &eptr)
    {
        if (eptr) std::rethrow_exception(eptr);
    }
   
public:
    enum JobPrepareState {
        PREPARE_STATE_DEFAULT = 0,
        PREPARE_STATE_MENU = 1,
    };

    Job(std::shared_ptr<ProgressIndicator> pri);
    
    bool is_finalized() const { return m_finalized; }
    
    Job(const Job &) = delete;
    Job(Job &&)      = delete;
    Job &operator=(const Job &) = delete;
    Job &operator=(Job &&) = delete;
    
    void start();
    
    // To wait for the running job and join the threads. False is
    // returned if the timeout has been reached and the job is still
    // running. Call cancel() before this fn if you want to explicitly
    // end the job.
    bool join(int timeout_ms = 0);
    
    bool is_running() const { return m_running.load(); }
    void cancel() { m_canceled.store(true); }
};

// Jobs defined inside the group class will be managed so that only one can
// run at a time. Also, the background process will be stopped if a job is
// started.
class ExclusiveJobGroup
{
    static const int ABORT_WAIT_MAX_MS = 10000;
    
    std::vector<std::unique_ptr<GUI::Job>> m_jobs;
    
protected:
    virtual void before_start() {}
    
public:
    virtual ~ExclusiveJobGroup() = default;
    
    size_t add_job(std::unique_ptr<GUI::Job> &&job)
    {
        m_jobs.emplace_back(std::move(job));
        return m_jobs.size() - 1;
    }
    
    void start(size_t jid);
    
    void cancel_all() { for (auto& j : m_jobs) j->cancel(); }
    
    void join_all(int wait_ms = 0);
    
    void stop_all() { cancel_all(); join_all(ABORT_WAIT_MAX_MS); }
    
    bool is_any_running() const;
};

}} // namespace Slic3r::GUI

#endif // JOB_HPP
