#ifndef slic3r_wgtFilaManagerCloudDispatcher_h_
#define slic3r_wgtFilaManagerCloudDispatcher_h_

#include <deque>
#include <functional>
#include <string>
#include <vector>
#include "nlohmann/json.hpp"

namespace Slic3r { namespace GUI {

class wgtFilaManagerCloudSync;
class wgtFilaManagerCloudClient;
struct FilamentSpool;

// Single-slot dispatcher that serializes pull / push calls on top of
// wgtFilaManagerCloudSync.  All public methods must be called on the UI thread.
// Completion signalling is the caller's responsibility: each op ultimately
// calls on_op_done() via CallAfter once its success/error callback returns.
class wgtFilaManagerCloudDispatcher {
public:
    // Observer callbacks are invoked on the UI thread.
    using StateChangedFn = std::function<void()>;           // syncing status toggled
    using PullDoneFn     = std::function<void(int added, int updated)>;
    using PushFailedFn   = std::function<void(const std::string& spool_id,
                                              const std::string& op,
                                              int code,
                                              const std::string& msg)>;
    // Fires on the UI thread after a successful push_create / push_update
    // has been acknowledged by the cloud and the local store's cloud_synced
    // flag has already been flipped to true.  Useful for observers that want
    // to rebroadcast the spool list so the UI can re-render sync dots.
    using PushDoneFn     = std::function<void(const std::string& spool_id,
                                              const std::string& op)>;

    wgtFilaManagerCloudDispatcher(wgtFilaManagerCloudSync*   sync,
                                  wgtFilaManagerCloudClient* client);
    ~wgtFilaManagerCloudDispatcher() = default;

    // Observer hookup.  Pass nullptr to clear.
    void set_on_state_changed(StateChangedFn fn) { m_on_state = std::move(fn); }
    void set_on_pull_done(PullDoneFn fn)         { m_on_pull = std::move(fn); }
    void set_on_push_failed(PushFailedFn fn)     { m_on_push_failed = std::move(fn); }
    void set_on_push_done(PushDoneFn fn)         { m_on_push_done = std::move(fn); }

    // --- Queue entry points (UI thread) -------------------------------------

    // Trigger a full pull from cloud.  If already syncing, no-op.
    void enqueue_pull();

    // Push a single spool create body. Local store is updated only after the
    // cloud accepts the operation and the follow-up pull reconciles the list.
    void enqueue_push_create(const FilamentSpool& spool);
    // Push a single spool id (update, with 404 fallback to create).
    // `local_patch` carries the fields the user actually edited this time
    // (local schema names); Cloud only receives whitelisted/changed fields.
    void enqueue_push_update(const std::string& spool_id,
                             const nlohmann::json& local_patch);
    // Push a batch delete for multiple spool ids.
    void enqueue_push_delete(const std::vector<std::string>& spool_ids);

    // Clear any not-yet-started push ops.  Does not cancel the in-flight op.
    void clear_pending();

    // Observable state.
    bool        is_busy()            const { return m_busy; }
    bool        is_pulling()         const { return m_pulling; }
    std::string last_synced_at()     const { return m_last_synced_at; }
    int         last_error_code()    const { return m_last_error_code; }
    std::string last_error_message() const { return m_last_error_message; }

private:
    using Op = std::function<void()>;

    void schedule_next();   // UI thread
    void on_op_done();      // UI thread, dispatched via CallAfter from callbacks

    void run_pull_op();
    void run_push_create_op(const FilamentSpool& spool);
    void run_push_update_op(const std::string& spool_id,
                            const nlohmann::json& local_patch);
    void run_push_delete_op(const std::vector<std::string>& spool_ids);

    void update_last_synced_now();
    void record_error(int code, const std::string& msg);
    void notify_state();

    wgtFilaManagerCloudSync*   m_sync   = nullptr;
    wgtFilaManagerCloudClient* m_client = nullptr;

    std::deque<Op> m_queue;
    bool           m_busy     = false;
    bool           m_pulling  = false;
    // STUDIO-17956: collapse redundant post-push reconciliation pulls. Each
    // push_done (batch add 5 spools -> 5 push_done callbacks) enqueues a pull
    // for "cloud is source of truth", but the single-slot dispatcher would
    // then run pull 5 times. We only need one. This flag is true while a
    // not-yet-started pull sits in m_queue; additional enqueue_pull() calls
    // are skipped until run_pull_op() pops it off and clears the flag.
    bool           m_pending_pull_queued = false;

    std::string m_last_synced_at;
    int         m_last_error_code    = 0;
    std::string m_last_error_message;

    StateChangedFn m_on_state;
    PullDoneFn     m_on_pull;
    PushFailedFn   m_on_push_failed;
    PushDoneFn     m_on_push_done;
};

}} // namespace Slic3r::GUI

#endif // slic3r_wgtFilaManagerCloudDispatcher_h_
