#include "DiscordPresence.hpp"
#include "DiscordIPC.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>

#include <boost/log/trivial.hpp>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        // See the guard in DiscordIPC.cpp.
        #define NOMINMAX
    #endif
    #include <windows.h>
#else
    #include <unistd.h>
#endif

namespace Slic3r {

// Contributor-registered application, so the feature can be evaluated as-is.
// The owning account controls the name and artwork every user sees, so this
// should be re-pointed at an application owned by Bambu Lab before release.
static const char *DISCORD_APPLICATION_ID = "1542693447339745472";

// Discord silently rejects presence fields longer than this.
static const size_t DISCORD_FIELD_MAX_BYTES = 128;

// A remaining-time estimate moving by less than this is the same estimate.
static const int64_t END_TIME_TOLERANCE_S = 30;

static const int64_t RECONNECT_BACKOFF_INITIAL_MS = 15 * 1000;
static const int64_t RECONNECT_BACKOFF_MAX_MS     = 5 * 60 * 1000;

static int64_t steady_now_ms()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

static int current_process_id()
{
#ifdef _WIN32
    return static_cast<int>(::GetCurrentProcessId());
#else
    return static_cast<int>(::getpid());
#endif
}

bool PresenceSnapshot::operator==(const PresenceSnapshot &other) const
{
    return activity == other.activity && details == other.details && state == other.state &&
           small_text == other.small_text && start_time == other.start_time && end_time == other.end_time;
}

bool PresenceSnapshot::equivalent(const PresenceSnapshot &other) const
{
    if (activity != other.activity || details != other.details || state != other.state ||
        small_text != other.small_text || start_time != other.start_time)
        return false;

    const int64_t delta = end_time > other.end_time ? end_time - other.end_time : other.end_time - end_time;
    const bool    both_set = end_time != 0 && other.end_time != 0;
    return both_set ? delta <= END_TIME_TOLERANCE_S : end_time == other.end_time;
}

const char *discord_state_asset_key(PresenceSnapshot::Activity activity)
{
    switch (activity) {
    case PresenceSnapshot::Activity::Printing:   return "state_printing";
    case PresenceSnapshot::Activity::Paused:     return "state_paused";
    case PresenceSnapshot::Activity::Failed:     return "state_error";
    case PresenceSnapshot::Activity::Finished:   return "state_done";
    case PresenceSnapshot::Activity::Slicing:    return "state_slicing";
    case PresenceSnapshot::Activity::Idle:
    case PresenceSnapshot::Activity::Editing:
    case PresenceSnapshot::Activity::Previewing:
    default:                                     return "state_idle";
    }
}

nlohmann::json discord_build_activity(const PresenceSnapshot &snapshot, const std::string &large_text)
{
    nlohmann::json activity = nlohmann::json::object();

    // Discord renders an empty string as a blank line, so omit instead.
    const std::string details = discord_truncate_utf8(snapshot.details, DISCORD_FIELD_MAX_BYTES);
    if (!details.empty())
        activity["details"] = details;

    const std::string state = discord_truncate_utf8(snapshot.state, DISCORD_FIELD_MAX_BYTES);
    if (!state.empty())
        activity["state"] = state;

    nlohmann::json timestamps = nlohmann::json::object();
    // Discord shows only one timer, so a countdown wins over elapsed.
    if (snapshot.end_time > 0)
        timestamps["end"] = snapshot.end_time;
    else if (snapshot.start_time > 0)
        timestamps["start"] = snapshot.start_time;
    if (!timestamps.empty())
        activity["timestamps"] = timestamps;

    nlohmann::json assets = nlohmann::json::object();
    assets["large_image"] = "bambu_studio";
    if (!large_text.empty())
        assets["large_text"] = discord_truncate_utf8(large_text, DISCORD_FIELD_MAX_BYTES);
    assets["small_image"] = discord_state_asset_key(snapshot.activity);
    const std::string small_text = discord_truncate_utf8(snapshot.small_text, DISCORD_FIELD_MAX_BYTES);
    if (!small_text.empty())
        assets["small_text"] = small_text;
    activity["assets"] = assets;

    activity["instance"] = false;
    return activity;
}

std::string discord_build_set_activity(const nlohmann::json &activity, int pid, uint64_t nonce)
{
    nlohmann::json args = nlohmann::json::object();
    args["pid"]         = pid;
    args["activity"]    = activity;

    nlohmann::json command = nlohmann::json::object();
    command["cmd"]        = "SET_ACTIVITY";
    command["nonce"]      = "bambu-" + std::to_string(nonce);
    command["args"]       = args;
    return command.dump();
}

bool PresenceThrottle::should_send(const PresenceSnapshot &snapshot, int64_t now_ms) const
{
    if (m_has_last && m_last.equivalent(snapshot))
        return false;
    if (m_has_last && now_ms - m_last_sent_ms < MIN_SEND_INTERVAL_MS)
        return false;
    return true;
}

void PresenceThrottle::record_sent(const PresenceSnapshot &snapshot, int64_t now_ms)
{
    m_last         = snapshot;
    m_has_last     = true;
    m_last_sent_ms = now_ms;
}

void PresenceThrottle::reset()
{
    m_has_last     = false;
    m_last_sent_ms = 0;
    m_last         = PresenceSnapshot();
}

DiscordPresence::DiscordPresence(std::string large_text) : m_large_text(std::move(large_text)) {}

DiscordPresence::~DiscordPresence() { stop(); }

void DiscordPresence::set_enabled(bool enabled)
{
    if (enabled == m_enabled.load())
        return;

    m_enabled.store(enabled);
    if (enabled)
        start();
    else
        stop();
}

void DiscordPresence::update(const PresenceSnapshot &snapshot)
{
    if (!m_enabled.load())
        return;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending       = snapshot;
        m_pending_valid = true;
    }
    m_cv.notify_one();
}

void DiscordPresence::start()
{
    if (m_thread.joinable())
        return;
    m_stop.store(false);
    m_thread = boost::thread(&DiscordPresence::worker, this);
}

void DiscordPresence::stop()
{
    if (!m_thread.joinable())
        return;
    m_stop.store(true);
    m_cv.notify_all();
    m_thread.join();
}

bool DiscordPresence::connect_and_handshake()
{
    if (!m_ipc)
        m_ipc.reset(new DiscordIPC());

    if (!m_ipc->try_connect())
        return false;

    nlohmann::json handshake = nlohmann::json::object();
    handshake["v"]           = 1;
    handshake["client_id"]   = DISCORD_APPLICATION_ID;
    if (!m_ipc->write_frame(DiscordOpcode::Handshake, handshake.dump()))
        return false;

    // Waiting for READY avoids reporting success against a socket that will
    // reject our commands, usually because the application id is wrong. m_stop
    // is checked so quitting does not block OnExit for the full timeout.
    const int64_t deadline = steady_now_ms() + 5000;
    while (steady_now_ms() < deadline && !m_stop.load()) {
        DiscordOpcode opcode  = DiscordOpcode::Frame;
        std::string   payload;
        const DiscordIPC::ReadResult result = m_ipc->read_frame(500, opcode, payload);
        if (result == DiscordIPC::ReadResult::Closed)
            return false;
        if (result == DiscordIPC::ReadResult::NoData)
            continue;

        try {
            const nlohmann::json parsed = nlohmann::json::parse(payload);
            const auto           evt    = parsed.find("evt");
            if (evt != parsed.end() && evt->is_string() && evt->get<std::string>() == "READY") {
                BOOST_LOG_TRIVIAL(info) << "DiscordPresence: handshake complete";
                return true;
            }
        } catch (const std::exception &) {
            // Not fatal on its own; keep waiting for READY.
        }
    }

    BOOST_LOG_TRIVIAL(debug) << "DiscordPresence: no READY within timeout";
    m_ipc->close();
    return false;
}

void DiscordPresence::clear_presence()
{
    if (m_ipc && m_ipc->is_connected() && m_sent_anything)
        m_ipc->write_frame(DiscordOpcode::Frame,
                           discord_build_set_activity(nlohmann::json(nullptr), current_process_id(), ++m_nonce));
}

void DiscordPresence::worker()
{
    BOOST_LOG_TRIVIAL(info) << "DiscordPresence: worker started";
    m_throttle.reset();
    m_backoff_ms      = 0;
    m_next_connect_ms = 0;
    m_sent_anything   = false;

    // Kept rather than consumed, so a reconnect can republish without waiting
    // for the state to change.
    PresenceSnapshot latest;
    bool             have_latest = false;

    while (!m_stop.load()) {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (!m_pending_valid && !m_stop.load())
                m_cv.wait_for(lock, std::chrono::seconds(1));
            if (m_pending_valid) {
                latest          = m_pending;
                have_latest     = true;
                m_pending_valid = false;
            }
        }

        if (m_stop.load())
            break;

        const int64_t now = steady_now_ms();

        if (!m_ipc || !m_ipc->is_connected()) {
            if (now < m_next_connect_ms)
                continue;
            if (!connect_and_handshake()) {
                m_backoff_ms      = m_backoff_ms == 0 ? RECONNECT_BACKOFF_INITIAL_MS
                                                      : std::min<int64_t>(m_backoff_ms * 2, RECONNECT_BACKOFF_MAX_MS);
                m_next_connect_ms = now + m_backoff_ms;
                continue;
            }
            // A fresh connection shows no presence, so resend unconditionally.
            m_backoff_ms    = 0;
            m_sent_anything = false;
            m_throttle.reset();
        }

        // Discord replies to every command; draining stops the buffer filling
        // and is how we notice the client going away.
        for (;;) {
            DiscordOpcode opcode = DiscordOpcode::Frame;
            std::string   payload;
            const DiscordIPC::ReadResult result = m_ipc->read_frame(0, opcode, payload);
            if (result == DiscordIPC::ReadResult::Closed) {
                BOOST_LOG_TRIVIAL(info) << "DiscordPresence: connection closed by peer";
                break;
            }
            if (result == DiscordIPC::ReadResult::NoData)
                break;
            if (opcode == DiscordOpcode::Ping)
                m_ipc->write_frame(DiscordOpcode::Pong, payload);
        }

        if (!m_ipc->is_connected()) {
            m_backoff_ms      = RECONNECT_BACKOFF_INITIAL_MS;
            m_next_connect_ms = steady_now_ms() + m_backoff_ms;
            continue;
        }

        if (have_latest && m_throttle.should_send(latest, now)) {
            const nlohmann::json activity = discord_build_activity(latest, m_large_text);
            if (m_ipc->write_frame(DiscordOpcode::Frame,
                                   discord_build_set_activity(activity, current_process_id(), ++m_nonce))) {
                m_throttle.record_sent(latest, now);
                m_sent_anything = true;
            }
        }
    }

    clear_presence();
    if (m_ipc)
        m_ipc->close();
    BOOST_LOG_TRIVIAL(info) << "DiscordPresence: worker stopped";
}

} // namespace Slic3r
