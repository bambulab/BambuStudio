#include "wgtFilaManagerCloudDispatcher.h"

#include "wgtFilaManagerCloudClient.h"
#include "wgtFilaManagerCloudSync.h"
#include "wgtFilaManagerStore.h"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/Utils/NetworkAgent.hpp"

#include <wx/app.h>
#include <boost/log/trivial.hpp>

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <memory>

namespace Slic3r { namespace GUI {

namespace {

// ISO-8601 UTC timestamp: "2026-04-17T10:23:45Z".
std::string now_iso_utc()
{
    using namespace std::chrono;
    auto t = system_clock::to_time_t(system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream os;
    os << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return os.str();
}

bool is_user_logged_in()
{
    NetworkAgent* agent = wxGetApp().getAgent();
    return agent && agent->is_user_login();
}

std::string cloud_spool_id_from_response(const nlohmann::json& resp)
{
    auto read_id = [](const nlohmann::json& obj) -> std::string {
        if (!obj.is_object()) return {};
        if (!obj.contains("id")) return {};
        const auto& id = obj["id"];
        if (id.is_string()) return id.get<std::string>();
        if (id.is_number_integer()) return std::to_string(id.get<int64_t>());
        if (id.is_number_unsigned()) return std::to_string(id.get<uint64_t>());
        return {};
    };

    if (auto id = read_id(resp); !id.empty()) return id;
    if (resp.contains("data")) {
        if (auto id = read_id(resp["data"]); !id.empty()) return id;
    }
    return {};
}

} // namespace

// ---------------------------------------------------------------------------
// ctor
// ---------------------------------------------------------------------------

wgtFilaManagerCloudDispatcher::wgtFilaManagerCloudDispatcher(
    wgtFilaManagerCloudSync*   sync,
    wgtFilaManagerCloudClient* client)
    : m_sync(sync)
    , m_client(client)
{}

// ---------------------------------------------------------------------------
// Public entry points (UI thread)
// ---------------------------------------------------------------------------

void wgtFilaManagerCloudDispatcher::enqueue_pull()
{
    if (m_pulling) {
        BOOST_LOG_TRIVIAL(info) << "[CloudDispatcher] enqueue_pull skipped: already pulling";
        return;
    }
    // STUDIO-17956: a batch add of N spools fires N push_done callbacks,
    // each of which enqueues a reconciliation pull. Without this check the
    // dispatcher would run `list_spools` N times in a row (the first pull
    // already gets all the fresh data, the rest are wasted HTTP calls and
    // produce duplicate "+0 added, M up-to-date" toasts). Skip when a pull
    // is already waiting in the queue; run_pull_op() clears the flag when
    // the pending pull starts.
    if (m_pending_pull_queued) {
        BOOST_LOG_TRIVIAL(info) << "[CloudDispatcher] enqueue_pull skipped: pull already queued";
        return;
    }
    wxGetApp().emit_fila_debug_log("data", "info", "Dispatcher enqueue pull",
                                   "A cloud pull operation was queued",
                                   nlohmann::json::object());
    m_pending_pull_queued = true;
    m_queue.push_back([this]() { run_pull_op(); });
    schedule_next();
}

void wgtFilaManagerCloudDispatcher::enqueue_push_create(const FilamentSpool& spool)
{
    wxGetApp().emit_fila_debug_log("data", "info", "Dispatcher enqueue push_create",
                                   "A create push operation was queued",
                                   {{"setting_id", spool.setting_id}});
    m_queue.push_back([this, spool]() { run_push_create_op(spool); });
    schedule_next();
}

void wgtFilaManagerCloudDispatcher::enqueue_push_update(const std::string& spool_id,
                                                        const nlohmann::json& local_patch)
{
    if (spool_id.empty()) return;
    wxGetApp().emit_fila_debug_log("data", "info", "Dispatcher enqueue push_update",
                                   "An update push operation was queued",
                                   {{"spool_id", spool_id},
                                    {"patch_keys", local_patch.is_object() ?
                                        static_cast<int>(local_patch.size()) : 0}});
    nlohmann::json patch_copy = local_patch.is_object() ? local_patch : nlohmann::json::object();
    m_queue.push_back([this, spool_id, patch_copy]() {
        run_push_update_op(spool_id, patch_copy);
    });
    schedule_next();
}

void wgtFilaManagerCloudDispatcher::enqueue_push_delete(const std::vector<std::string>& spool_ids)
{
    if (spool_ids.empty()) return;
    wxGetApp().emit_fila_debug_log("data", "info", "Dispatcher enqueue push_delete",
                                   "A delete push operation was queued",
                                   {{"spool_ids", spool_ids}, {"count", static_cast<int>(spool_ids.size())}});
    m_queue.push_back([this, spool_ids]() { run_push_delete_op(spool_ids); });
    schedule_next();
}

void wgtFilaManagerCloudDispatcher::clear_pending()
{
    // Keep the in-flight op running; caller is responsible for ignoring its
    // eventual outcome via observer state.
    const size_t n = m_queue.size();
    m_queue.clear();
    // Any pending pull we were holding was just dropped; allow future
    // enqueue_pull() calls to queue a fresh one again.
    m_pending_pull_queued = false;
    if (n > 0) {
        BOOST_LOG_TRIVIAL(info) << "[CloudDispatcher] clear_pending dropped " << n << " op(s)";
    }
}

// ---------------------------------------------------------------------------
// Scheduling
// ---------------------------------------------------------------------------

void wgtFilaManagerCloudDispatcher::schedule_next()
{
    if (m_busy) return;
    if (m_queue.empty()) return;

    Op op = std::move(m_queue.front());
    m_queue.pop_front();

    m_busy = true;
    notify_state();

    // Run the op; each op is expected to call on_op_done() via CallAfter from
    // its Success / Error callback.
    op();
}

void wgtFilaManagerCloudDispatcher::on_op_done()
{
    m_busy    = false;
    m_pulling = false;
    notify_state();
    schedule_next();
}

// ---------------------------------------------------------------------------
// Ops
// ---------------------------------------------------------------------------

void wgtFilaManagerCloudDispatcher::run_pull_op()
{
    // Clear the "queue already has a pending pull" guard as soon as we pop
    // the pull off the queue, so a new enqueue_pull() during this run (e.g.
    // a later push_done) can schedule the next pull instead of being dropped.
    m_pending_pull_queued = false;

    if (!m_sync) {
        on_op_done();
        return;
    }
    if (!is_user_logged_in()) {
        BOOST_LOG_TRIVIAL(info) << "[CloudDispatcher] pull skipped: not logged in";
        on_op_done();
        return;
    }

    m_pulling = true;
    BOOST_LOG_TRIVIAL(info) << "[CloudDispatcher] pull started";
    wxGetApp().emit_fila_debug_log("data", "info", "Dispatcher pull started",
                                   "Queued pull operation started running",
                                   nlohmann::json::object());

    // wgtFilaManagerCloudSync::pull_from_cloud runs list_spools internally and
    // merges into the Store on the UI thread.  We don't know added/updated
    // counts from it directly; use snapshot before/after to estimate.
    auto* store_before = wxGetApp().fila_manager_store();
    size_t before_sz = store_before ? store_before->spools_to_json().size() : 0;

    m_sync->pull_from_cloud();

    // pull_from_cloud completes asynchronously and is not observable here;
    // poll on the sync object's is_syncing() via CallAfter loop.  Simpler:
    // mark pulling complete after a short settle delay and publish pull_done.
    // NOTE: this is a best-effort; CloudSync schedules CallAfter internally,
    // so we wait until is_syncing() returns false on the UI thread.
    auto check = std::make_shared<std::function<void()>>();
    *check = [this, check, before_sz]() {
        if (m_sync && m_sync->is_syncing()) {
            wxTheApp->CallAfter(*check);
            return;
        }
        if (m_sync && m_sync->last_pull_succeeded()) {
            update_last_synced_now();
            auto* store_after = wxGetApp().fila_manager_store();
            size_t after_sz = store_after ? store_after->spools_to_json().size() : 0;
            int added_estimate = after_sz > before_sz ? static_cast<int>(after_sz - before_sz) : 0;
            int updated_estimate = static_cast<int>(std::min(before_sz, after_sz));
            BOOST_LOG_TRIVIAL(info) << "[CloudDispatcher] pull done, size " << before_sz << " -> " << after_sz;
            wxGetApp().emit_fila_debug_log("data", "info", "Dispatcher pull finished",
                                           "Queued pull operation completed",
                                           {{"before", static_cast<int>(before_sz)},
                                            {"after", static_cast<int>(after_sz)},
                                            {"added", added_estimate},
                                            {"updated", updated_estimate}});
            if (m_on_pull) m_on_pull(added_estimate, updated_estimate);
        } else if (m_sync) {
            record_error(m_sync->last_pull_error_code(), m_sync->last_pull_error_message());
            wxGetApp().emit_fila_debug_log("data", "error", "Dispatcher pull finished with error",
                                           "Queued pull operation completed with failure state",
                                           {{"code", m_sync->last_pull_error_code()},
                                            {"error", m_sync->last_pull_error_message()}});
        }
        on_op_done();
    };
    wxTheApp->CallAfter(*check);
}

void wgtFilaManagerCloudDispatcher::run_push_create_op(const FilamentSpool& spool)
{
    if (!m_sync || !is_user_logged_in()) {
        if (m_sync && !is_user_logged_in()) {
            wxGetApp().emit_fila_debug_log("data", "info", "Dispatcher push_create skipped",
                                           "User not logged in — no cloud HTTP",
                                           {{"op", "create"}});
        }
        on_op_done();
        return;
    }
    BOOST_LOG_TRIVIAL(info) << "[CloudDispatcher] push_create";
    wxGetApp().emit_fila_debug_log("data", "info", "Dispatcher push_create started",
                                   "Queued create push started running",
                                   {{"setting_id", spool.setting_id}});
    if (!m_client) { on_op_done(); return; }

    nlohmann::json body = wgtFilaManagerCloudSync::spool_to_cloud_json(spool);
    m_client->create_spool(body,
        [this, spool](const nlohmann::json& resp) {
            wxTheApp->CallAfter([this, spool, resp]() {
                const std::string spool_id = cloud_spool_id_from_response(resp);
                BOOST_LOG_TRIVIAL(info) << "[CloudDispatcher] push_create ok " << spool_id;
                if (!spool_id.empty()) {
                    if (auto* store = wxGetApp().fila_manager_store()) {
                        FilamentSpool local = spool;
                        local.spool_id = spool_id;
                        local.cloud_synced = true;
                        store->add_spool(local);
                        store->save();
                    }
                }
                update_last_synced_now();
                wxGetApp().emit_fila_debug_log("data", "info", "Dispatcher push_create finished",
                                               "Queued create push completed successfully",
                                               {{"spool_id", spool_id}});
                if (m_on_push_done) m_on_push_done(spool_id, "create");
                on_op_done();
            });
        },
        [this](int code, const std::string& err) {
            wxTheApp->CallAfter([this, code, err]() {
                record_error(code, err);
                wxGetApp().emit_fila_debug_log("data", "error", "Dispatcher push_create failed",
                                               "Queued create push failed",
                                               {{"code", code}, {"error", err}});
                if (m_on_push_failed) m_on_push_failed(std::string(), "create", code, err);
                on_op_done();
            });
        });
}

void wgtFilaManagerCloudDispatcher::run_push_update_op(const std::string& spool_id,
                                                       const nlohmann::json& local_patch)
{
    if (!m_sync || !is_user_logged_in()) {
        if (m_sync && !is_user_logged_in()) {
            wxGetApp().emit_fila_debug_log("data", "info", "Dispatcher push_update skipped",
                                           "User not logged in — no cloud HTTP",
                                           {{"spool_id", spool_id}, {"op", "update"}});
        }
        on_op_done();
        return;
    }
    BOOST_LOG_TRIVIAL(info) << "[CloudDispatcher] push_update " << spool_id;
    wxGetApp().emit_fila_debug_log("data", "info", "Dispatcher push_update started",
                                   "Queued update push started running",
                                   {{"spool_id", spool_id}});
    auto* store = wxGetApp().fila_manager_store();
    if (!store) { on_op_done(); return; }
    const FilamentSpool* spool = store->get_spool(spool_id);
    if (!spool || !m_client) { on_op_done(); return; }

    // Update 走 patch 白名单；404 fallback 才用到全量 create body。
    nlohmann::json body        = wgtFilaManagerCloudSync::spool_to_cloud_update_patch(local_patch);
    nlohmann::json create_body = wgtFilaManagerCloudSync::spool_to_cloud_json(*spool);

    if (body.empty()) {
        // patch 里没有任何云端认识的字段（比如用户只改了 favorite 这种仅本地字段）
        // — 不打扰云端，直接把本条标记已同步即可。
        BOOST_LOG_TRIVIAL(info) << "[CloudDispatcher] push_update skipped (empty cloud patch) " << spool_id;
        if (store->mark_synced(spool_id, true)) store->save();
        update_last_synced_now();
        if (m_on_push_done) m_on_push_done(spool_id, "update");
        on_op_done();
        return;
    }

    m_client->update_spool(spool_id, body,
        [this, spool_id, local_patch](const nlohmann::json& /*resp*/) {
            wxTheApp->CallAfter([this, spool_id, local_patch]() {
                BOOST_LOG_TRIVIAL(info) << "[CloudDispatcher] push_update ok " << spool_id;
                if (auto* store = wxGetApp().fila_manager_store()) {
                    store->apply_patch(spool_id, local_patch);
                    if (store->mark_synced(spool_id, true))
                        store->save();
                    else
                        store->save();
                }
                update_last_synced_now();
                wxGetApp().emit_fila_debug_log("data", "info", "Dispatcher push_update finished",
                                               "Queued update push completed successfully",
                                               {{"spool_id", spool_id}});
                if (m_on_push_done) m_on_push_done(spool_id, "update");
                on_op_done();
            });
        },
        [this, spool_id, create_body](int code, const std::string& err) {
            if (code == 404) {
                // Fallback to create for the common "cloud has no record of
                // this local spool yet" case (e.g. the local row was created
                // while offline and the first update is effectively a create).
                BOOST_LOG_TRIVIAL(info) << "[CloudDispatcher] push_update 404, fallback create " << spool_id;
                m_client->create_spool(create_body,
                    [this, spool_id](const nlohmann::json&) {
                        wxTheApp->CallAfter([this, spool_id]() {
                            BOOST_LOG_TRIVIAL(info) << "[CloudDispatcher] push_update fallback create ok " << spool_id;
                            if (auto* store = wxGetApp().fila_manager_store()) {
                                if (store->mark_synced(spool_id, true))
                                    store->save();
                            }
                            update_last_synced_now();
                            wxGetApp().emit_fila_debug_log("data", "info", "Dispatcher push_update fallback create finished",
                                                           "Update push fell back to create and completed successfully",
                                                           {{"spool_id", spool_id}});
                            if (m_on_push_done) m_on_push_done(spool_id, "update");
                            on_op_done();
                        });
                    },
                    [this, spool_id](int c, const std::string& e) {
                        wxTheApp->CallAfter([this, spool_id, c, e]() {
                            record_error(c, e);
                            wxGetApp().emit_fila_debug_log("data", "error", "Dispatcher push_update fallback create failed",
                                                           "Update push fell back to create but still failed",
                                                           {{"spool_id", spool_id}, {"code", c}, {"error", e}});
                            if (m_on_push_failed) m_on_push_failed(spool_id, "create", c, e);
                            on_op_done();
                        });
                    });
                return;
            }
            wxTheApp->CallAfter([this, spool_id, code, err]() {
                record_error(code, err);
                wxGetApp().emit_fila_debug_log("data", "error", "Dispatcher push_update failed",
                                               "Queued update push failed",
                                               {{"spool_id", spool_id}, {"code", code}, {"error", err}});
                if (m_on_push_failed) m_on_push_failed(spool_id, "update", code, err);
                on_op_done();
            });
        });
}

void wgtFilaManagerCloudDispatcher::run_push_delete_op(const std::vector<std::string>& spool_ids)
{
    if (!m_sync || !is_user_logged_in() || !m_client) {
        if (m_sync && m_client && !is_user_logged_in() && !spool_ids.empty()) {
            wxGetApp().emit_fila_debug_log("data", "info", "Dispatcher push_delete skipped",
                                           "User not logged in — local delete kept, no cloud HTTP",
                                           {{"spool_ids", spool_ids}, {"count", static_cast<int>(spool_ids.size())}});
        }
        on_op_done();
        return;
    }
    BOOST_LOG_TRIVIAL(info) << "[CloudDispatcher] push_delete (" << spool_ids.size() << ")";
    wxGetApp().emit_fila_debug_log("data", "info", "Dispatcher push_delete started",
                                   "Queued delete push started running",
                                   {{"spool_ids", spool_ids}, {"count", static_cast<int>(spool_ids.size())}});
    nlohmann::json body;
    body["ids"] = spool_ids;

    // Capture first id as representative for error reports.
    std::string rep = spool_ids.empty() ? std::string() : spool_ids.front();

    m_client->batch_delete(body,
        [this, spool_ids](const nlohmann::json& /*resp*/) {
            wxTheApp->CallAfter([this, spool_ids]() {
                BOOST_LOG_TRIVIAL(info) << "[CloudDispatcher] push_delete ok";
                if (auto* store = wxGetApp().fila_manager_store()) {
                    for (const auto& spool_id : spool_ids)
                        store->remove_spool(spool_id);
                    store->save();
                }
                update_last_synced_now();
                wxGetApp().emit_fila_debug_log("data", "info", "Dispatcher push_delete finished",
                                               "Queued delete push completed successfully",
                                               {{"spool_ids", spool_ids},
                                                {"count", static_cast<int>(spool_ids.size())}});
                if (m_on_push_done) m_on_push_done(std::string(), "delete");
                on_op_done();
            });
        },
        [this, rep](int code, const std::string& err) {
            wxTheApp->CallAfter([this, rep, code, err]() {
                record_error(code, err);
                wxGetApp().emit_fila_debug_log("data", "error", "Dispatcher push_delete failed",
                                               "Queued delete push failed",
                                               {{"spool_id", rep}, {"code", code}, {"error", err}});
                if (m_on_push_failed) m_on_push_failed(rep, "delete", code, err);
                on_op_done();
            });
        });
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void wgtFilaManagerCloudDispatcher::update_last_synced_now()
{
    m_last_synced_at     = now_iso_utc();
    m_last_error_code    = 0;
    m_last_error_message.clear();
}

void wgtFilaManagerCloudDispatcher::record_error(int code, const std::string& msg)
{
    m_last_error_code    = code;
    m_last_error_message = msg;
}

void wgtFilaManagerCloudDispatcher::notify_state()
{
    if (m_on_state) m_on_state();
}

}} // namespace Slic3r::GUI
