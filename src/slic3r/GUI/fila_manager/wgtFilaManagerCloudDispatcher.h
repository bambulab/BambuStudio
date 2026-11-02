#ifndef slic3r_wgtFilaManagerCloudDispatcher_h_
#define slic3r_wgtFilaManagerCloudDispatcher_h_

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include "nlohmann/json.hpp"

namespace BBL { struct AmsSyncParams; }

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
    // Sync AMS tray weights to cloud via POST /my/filament/v2/ams/sync.
    // Used for device-reported remainder updates (auto and manual push-all).
    void enqueue_sync_ams(BBL::AmsSyncParams params,
                          std::function<void()> on_cloud_ok = nullptr);

    // 开始新的账号会话。排队操作直接丢弃；已经发出的请求继续等待网络返回，
    // 但旧会话代次禁止它发布任何结果。
    void clear_pending();

    // Observable state.
    bool        is_busy()            const { return m_busy; }
    bool        is_pulling()         const { return m_pulling; }
    std::string last_synced_at()     const { return m_last_synced_at; }
    int         last_error_code()    const { return m_last_error_code; }
    std::string last_error_message() const { return m_last_error_message; }

private:
    using Op = std::function<void()>;
    using SessionGeneration = std::uint64_t;

    void schedule_next();   // UI thread
    void on_op_done(SessionGeneration generation, bool was_pull = false);

    bool is_session_current(SessionGeneration generation) const;

    void run_pull_op(SessionGeneration generation);
    void run_push_create_op(SessionGeneration generation, const FilamentSpool& spool);
    void run_push_update_op(SessionGeneration generation, const std::string& spool_id,
                            const nlohmann::json& local_patch);
    void run_push_delete_op(SessionGeneration generation,
                            const std::vector<std::string>& spool_ids);
    void run_sync_ams_op(SessionGeneration generation,
                         BBL::AmsSyncParams params,
                         std::function<void()> on_cloud_ok);

    void update_last_synced_now();
    void record_error(int code, const std::string& msg);
    void notify_state();

    wgtFilaManagerCloudSync*   m_sync   = nullptr;
    wgtFilaManagerCloudClient* m_client = nullptr;

    std::deque<Op> m_queue;
    bool           m_busy     = false;
    bool           m_pulling  = false;
    std::optional<SessionGeneration> m_pull_generation;

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
