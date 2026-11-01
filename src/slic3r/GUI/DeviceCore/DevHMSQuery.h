#ifndef slic3r_GUI_DevHMSQuery_hpp_
#define slic3r_GUI_DevHMSQuery_hpp_

// Global HMS error-code dictionary. Distinct from DevHMS/DevHMSItem (one machine's
// current MQTT-pushed HMS items).

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "nlohmann/json.hpp"

#include <wx/image.h>
#include <wx/string.h>

namespace Slic3r {

class MachineObject;

namespace GUI {

using json = nlohmann::json;

// Per-(dev_type + lang) state. A Ready snapshot missing a code means "unknown code".
enum class HMSStatus { Unknown, Loading, Ready, Failed };

struct HMSResult
{
    HMSStatus        status = HMSStatus::Unknown;
    wxString         text;                 // empty on Ready => unknown code
    wxString         image_url;
    std::vector<int> actions;
    bool             is_internal = false;  // only meaningful when status == Ready
};

class HMSQueryMgr;

// Move-only RAII handle: destroying it auto-unsubscribes.
class HMSSubscription
{
public:
    HMSSubscription() = default;
    HMSSubscription(HMSQueryMgr* mgr, int id) : m_mgr(mgr), m_id(id) {}
    ~HMSSubscription() { reset(); }

    HMSSubscription(const HMSSubscription&)            = delete;
    HMSSubscription& operator=(const HMSSubscription&) = delete;
    HMSSubscription(HMSSubscription&& o) noexcept;
    HMSSubscription& operator=(HMSSubscription&& o) noexcept;

    void reset();

private:
    HMSQueryMgr* m_mgr = nullptr;
    int          m_id  = 0;
};

class HMSQueryMgr
{
    friend class HMSSubscription;   // may call unsubscribe() via its RAII reset

public:
    using OnResult = std::function<void(const HMSResult&)>;

    HMSQueryMgr()  = default;
    ~HMSQueryMgr() { clear(); }
    HMSQueryMgr(const HMSQueryMgr&)            = delete;
    HMSQueryMgr& operator=(const HMSQueryMgr&) = delete;

    // Async overloads write the subscription into `sub`; on_changed fires on every later
    // change until `sub` dies. Assign `sub` before acting on the returned result.
    HMSResult query_error(const std::string& dev_id, int error_code);
    HMSResult query_error(const std::string& dev_id, int error_code, OnResult on_changed, HMSSubscription& sub);
    HMSResult query_hms(const std::string& dev_id, const std::string& long_error_code);
    HMSResult query_hms(const std::string& dev_id, const std::string& long_error_code, OnResult on_changed, HMSSubscription& sub);
    HMSResult query_action(const std::string& dev_id, int error_code);
    HMSResult query_action(const std::string& dev_id, int error_code, OnResult on_changed, HMSSubscription& sub);

    // Persistent: on_changed fires on every later change to dev_type's dictionary.
    HMSSubscription subscribe_device(const std::string& dev_type, std::function<void()> on_changed);

    wxImage query_image_from_local(const wxString& image_name);

private:
    // Cached dictionary for one (dev_type + lang). shared_ptr lets a reader copy the pointer
    // under a tiny lock and traverse it while the main thread swaps in a new one.
    struct HMSEntry
    {
        std::shared_ptr<const json> info;
        std::shared_ptr<const json> action;
        HMSStatus                   status = HMSStatus::Unknown;
        time_t                      last_update = 0;
        std::string                 info_ver;    // in-memory version, avoids disk read on request
        std::string                 action_ver;
        bool                        inflight = false;
    };

    // Transient state joining the two parallel downloads (INFO + ACTION) of one request.
    // Filled on Http io_threads, consumed once on the main thread.
    struct DownloadCtx
    {
        std::string                 dev_type;
        std::string                 lang;
        std::atomic<int>            pending{2};
        std::atomic<bool>           ok{true};
        std::shared_ptr<const json> info;         // parsed INFO "data" (nullptr if 201/unchanged)
        std::shared_ptr<const json> action;       // parsed ACTION "data"
        std::string                 info_ver;      // new remote version, empty if unchanged
        std::string                 action_ver;
    };

    static std::string cache_key(const std::string& dev_type, const std::string& lang);
    static std::string hms_language_code();

    // Shared body of the async overloads: subscribes only while Loading, re-running requery.
    HMSResult subscribe_query(const std::string& dev_id, OnResult on_changed, std::function<HMSResult()> requery, HMSSubscription& sub);

    void ensure_loaded(const std::string& dev_type);
    void start_download(const std::string& dev_type, const std::string& lang);
    void fetch_leg(const std::shared_ptr<DownloadCtx>& ctx, bool is_info, const std::string& url, const std::string& local_ver);
    void finish_leg(const std::shared_ptr<DownloadCtx>& ctx);
    void apply_download(const std::shared_ptr<DownloadCtx>& ctx);
    bool need_update(const HMSEntry& entry);

    void unsubscribe(int id);
    // Iterates ids (not callbacks) and re-checks existence per call, so a callback that
    // destroys another subscriber is safe.
    void notify_subscribers(const std::string& dev_type);

    void clear();

    std::unordered_map<std::string, HMSEntry> m_entries;
    std::mutex                                m_mutex;    // guards m_entries only

    std::unordered_map<wxString, wxImage>     m_local_images;

    // No mutex: m_subs is accessed on the main thread only (public entry points assert this).
    struct DevSub
    {
        std::string           dev_type;
        std::function<void()> on_changed;
    };
    std::unordered_map<int, DevSub> m_subs;
    int                             m_next_sub_id = 1;
};

}} // namespace Slic3r::GUI

#endif
