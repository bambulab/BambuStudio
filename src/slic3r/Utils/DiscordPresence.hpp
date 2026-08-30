#ifndef slic3r_DiscordPresence_hpp_
#define slic3r_DiscordPresence_hpp_

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include <boost/thread.hpp>

#include "nlohmann/json.hpp"

namespace Slic3r {

class DiscordIPC;

// Plain data so it can cross to the worker thread by value. The display
// strings are built by the GUI layer, which owns translation and the privacy
// filter; nothing below this point does more than serialise them.
struct PresenceSnapshot
{
    enum class Activity { Idle, Editing, Previewing, Slicing, Printing, Paused, Failed, Finished };

    Activity    activity { Activity::Idle };
    std::string details;                 // first line, e.g. the project name
    std::string state;                   // second line, e.g. "Printing - 47%"
    std::string small_text;              // small icon tooltip, e.g. the printer model
    int64_t     start_time { 0 };        // unix seconds; Discord counts up from here
    int64_t     end_time { 0 };          // unix seconds; Discord counts down to here

    bool operator==(const PresenceSnapshot &other) const;
    bool operator!=(const PresenceSnapshot &other) const { return !(*this == other); }

    // Equal as far as the viewer is concerned. end_time is compared with a
    // tolerance, because a remaining-time estimate drifts constantly and
    // treating that as a change would push an update every cycle.
    bool equivalent(const PresenceSnapshot &other) const;
};

// Empty unless the build sets SLIC3R_DISCORD_APP_ID.
std::string discord_default_application_id();

// Must match a key uploaded to the Discord application's art assets.
const char *discord_state_asset_key(PresenceSnapshot::Activity activity);

nlohmann::json discord_build_activity(const PresenceSnapshot &snapshot, const std::string &large_text);

// A null activity clears the presence.
std::string discord_build_set_activity(const nlohmann::json &activity, int pid, uint64_t nonce);

// Split out from the worker so the policy can be tested against an explicit
// clock rather than wall time.
class PresenceThrottle
{
public:
    // Discord allows 5 presence updates per 20 seconds; stay comfortably under.
    static const int64_t MIN_SEND_INTERVAL_MS = 5000;

    bool should_send(const PresenceSnapshot &snapshot, int64_t now_ms) const;
    void record_sent(const PresenceSnapshot &snapshot, int64_t now_ms);
    void reset();

private:
    PresenceSnapshot m_last;
    bool             m_has_last { false };
    int64_t          m_last_sent_ms { 0 };
};

// All socket work happens on the worker thread; update() is the only method
// the GUI thread calls while running.
class DiscordPresence
{
public:
    DiscordPresence(std::string application_id, std::string large_text);
    ~DiscordPresence();

    DiscordPresence(const DiscordPresence &) = delete;
    DiscordPresence &operator=(const DiscordPresence &) = delete;

    void set_enabled(bool enabled);
    bool is_enabled() const { return m_enabled.load(); }

    const std::string &application_id() const { return m_application_id; }

    // GUI thread. Non-blocking.
    void update(const PresenceSnapshot &snapshot);

private:
    void start();
    void stop();
    void worker();
    bool connect_and_handshake();
    void clear_presence();

    const std::string m_application_id;
    const std::string m_large_text;

    std::atomic<bool> m_enabled { false };
    std::atomic<bool> m_stop { false };

    boost::thread               m_thread;
    std::mutex                  m_mutex;
    std::condition_variable     m_cv;
    PresenceSnapshot            m_pending;
    bool                        m_pending_valid { false };

    // Worker-thread state only.
    std::unique_ptr<DiscordIPC> m_ipc;
    PresenceThrottle            m_throttle;
    uint64_t                    m_nonce { 0 };
    int64_t                     m_next_connect_ms { 0 };
    int64_t                     m_backoff_ms { 0 };
    bool                        m_sent_anything { false };
};

} // namespace Slic3r

#endif // slic3r_DiscordPresence_hpp_
