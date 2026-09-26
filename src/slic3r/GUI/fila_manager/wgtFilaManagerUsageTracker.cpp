#include "wgtFilaManagerUsageTracker.h"
#include "wgtFilaManagerStore.h"
#include "wgtFilaManagerCloudDispatcher.h"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/DeviceCore/DevDefs.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/Utils/NetworkAgent.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Utils.hpp"

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>

#include <algorithm>
#include <cmath>
#include <thread>

namespace Slic3r { namespace GUI {

namespace {

constexpr int    kPageSize          = 20;
constexpr int    kMaxPages          = 5;
constexpr size_t kMaxProcessedIds   = 500;
constexpr size_t kMaxHistoryEntries = 200;

// Cloud task status (see TaskManager.cpp parse_task_status). 4 is reported for a
// short while after the printer finishes, before the cloud settles on 2 / 3.
constexpr int kTaskPrinting   = 1;
constexpr int kTaskSuccess    = 2;
constexpr int kTaskFailed     = 3;
constexpr int kTaskFinalizing = 4;

using Clock = std::chrono::steady_clock;

const std::chrono::seconds kIdleInterval(10 * 60);
const std::chrono::seconds kPendingInterval(60);
const std::chrono::seconds kErrorInterval(5 * 60);
const std::chrono::seconds kAfterPrintEnd(30);
const std::chrono::seconds kInflightTimeout(3 * 60);

std::string storage_path()
{
    return (boost::filesystem::path(data_dir()) / "filament_inventory" / "usage_tracker.json").string();
}

std::string now_iso_utc()
{
    return boost::posix_time::to_iso_extended_string(boost::posix_time::second_clock::universal_time()) + "Z";
}

// "2026-09-26T09:18:22Z" -> seconds since epoch, -1 on failure.
long long parse_iso_utc(std::string s)
{
    if (s.empty()) return -1;
    if (s.back() == 'Z') s.pop_back();
    const auto dot = s.find('.');
    if (dot != std::string::npos) s.erase(dot);
    try {
        const auto t = boost::posix_time::from_iso_extended_string(s);
        return (t - boost::posix_time::ptime(boost::gregorian::date(1970, 1, 1))).total_seconds();
    } catch (...) {
        return -1;
    }
}

std::string str_of(const nlohmann::json& j, const char* key)
{
    if (!j.contains(key)) return {};
    const auto& v = j.at(key);
    if (v.is_string()) return v.get<std::string>();
    if (v.is_number_integer()) return std::to_string(v.get<int64_t>());
    return {};
}

long long int_of(const nlohmann::json& j, const char* key, long long def = -1)
{
    if (!j.contains(key) || !j.at(key).is_number()) return def;
    return static_cast<long long>(j.at(key).get<double>());
}

double num_of(const nlohmann::json& j, const char* key)
{
    if (!j.contains(key) || !j.at(key).is_number()) return 0.0;
    return j.at(key).get<double>();
}

// "#808080FF" / "808080" -> "808080"
std::string norm_color(std::string c)
{
    if (!c.empty() && c[0] == '#') c.erase(0, 1);
    if (c.size() > 6) c.resize(6);
    for (auto& ch : c) ch = static_cast<char>(toupper(static_cast<unsigned char>(ch)));
    return c;
}

bool is_external_slot(int ams_id) { return ams_id == VIRTUAL_TRAY_MAIN_ID || ams_id == VIRTUAL_TRAY_DEPUTY_ID; }

// Resolve an amsDetailMapping entry to the physical (ams_id, slot_id). The
// entry's own amsId / slotId fields are unreliable (always 0 in practice); its
// "ams" value is the per-filament slot value from amsMapping, whose sibling
// amsMapping2 holds the explicit {amsId, slotId}.
bool resolve_slot(const nlohmann::json& task, const nlohmann::json& detail, int& ams_id, int& slot_id)
{
    const long long v = int_of(detail, "ams");
    if (v < 0) return false;

    if (task.contains("amsMapping") && task["amsMapping"].is_array() &&
        task.contains("amsMapping2") && task["amsMapping2"].is_array()) {
        const auto& m1 = task["amsMapping"];
        const auto& m2 = task["amsMapping2"];
        for (size_t k = 0; k < m1.size() && k < m2.size(); ++k) {
            if (!m1[k].is_number() || static_cast<long long>(m1[k].get<double>()) != v) continue;
            const long long a = int_of(m2[k], "amsId");
            const long long s = int_of(m2[k], "slotId");
            if (a < 0 || s < 0 || (a == 255 && s == 255)) return false;
            ams_id  = static_cast<int>(a);
            slot_id = static_cast<int>(s);
            return true;
        }
    }

    // Fallback for tasks without amsMapping2: standard 4-slot AMS encoding.
    if (v == VIRTUAL_TRAY_MAIN_ID || v == VIRTUAL_TRAY_DEPUTY_ID) {
        ams_id  = static_cast<int>(v);
        slot_id = 0;
        return true;
    }
    if (v < 4 * 64) {
        ams_id  = static_cast<int>(v / 4);
        slot_id = static_cast<int>(v % 4);
        return true;
    }
    return false;
}

const DevAmsTray* find_tray(MachineObject* obj, int ams_id, int slot_id)
{
    if (is_external_slot(ams_id)) {
        for (const auto& vt : obj->vt_slot)
            if (vt.id == std::to_string(ams_id)) return &vt;
        return obj->vt_slot.size() == 1 ? &obj->vt_slot.front() : nullptr;
    }
    auto fila_sys = obj->GetFilaSystem();
    if (!fila_sys) return nullptr;
    auto& ams_list = fila_sys->GetAmsList();
    auto  ams_it   = ams_list.find(std::to_string(ams_id));
    if (ams_it == ams_list.end() || !ams_it->second) return nullptr;
    auto& trays   = ams_it->second->GetTrays();
    auto  tray_it = trays.find(std::to_string(slot_id));
    return (tray_it != trays.end()) ? tray_it->second : nullptr;
}

const FilamentSpool* find_spool(wgtFilaManagerStore* store, const std::string& dev_id,
                                int ams_id, int slot_id, const DevAmsTray& tray)
{
    if (!is_external_slot(ams_id))
        return store->find_by_slot(dev_id, std::to_string(ams_id), std::to_string(slot_id));

    // External spools are mounted with ams_id -1 / ams_type EXT_SPOOL and the
    // virtual tray id as slot_id (see wgtFilaManagerSync::sync_all_trays).
    const FilamentSpool* match = nullptr;
    int                  count = 0;
    for (const auto& id : store->all_spool_ids()) {
        const FilamentSpool* sp = store->get_spool(id);
        if (!sp || !sp->in_printer || sp->dev_id != dev_id) continue;
        if (sp->ams_type != static_cast<int>(DevAmsType::EXT_SPOOL)) continue;
        if (sp->slot_id != tray.id) continue;
        match = sp;
        ++count;
    }
    return count == 1 ? match : nullptr;
}

std::string spool_label(const FilamentSpool& sp)
{
    std::string s = sp.brand;
    for (const std::string* part : {&sp.series, &sp.material_type, &sp.color_name}) {
        if (part->empty() || s.find(*part) != std::string::npos) continue;
        if (!s.empty()) s += " ";
        s += *part;
    }
    return s.empty() ? sp.spool_id : s;
}

double current_net_weight(const FilamentSpool& sp)
{
    if (sp.net_weight > 0.0) return sp.net_weight;
    const double total = sp.effective_total_net_weight();
    if (total <= 0.0) return 0.0;
    return total * std::max(0, std::min(100, sp.remain_percent)) / 100.0;
}

} // namespace

wgtFilaManagerUsageTracker::wgtFilaManagerUsageTracker() { load(); }

wgtFilaManagerUsageTracker::~wgtFilaManagerUsageTracker() = default;

void wgtFilaManagerUsageTracker::on_device_update(MachineObject* obj)
{
    if (!obj) return;
    if (wxGetApp().app_config && wxGetApp().app_config->get("fila_usage_tracking") == "0") return;

    const std::string dev_id = obj->get_dev_id();
    if (dev_id.empty()) return;

    DeviceState& st  = m_devices[dev_id];
    const auto   now = Clock::now();

    if (st.baseline.empty()) {
        // First sighting of this device: never deduct its existing history,
        // which the user's manually entered weights already account for.
        st.baseline = now_iso_utc();
        save();
        BOOST_LOG_TRIVIAL(info) << "[fila-usage] new device " << dev_id << ", baseline=" << st.baseline;
    }

    const std::string& status = obj->print_status;
    if (!st.prev_print_status.empty() && st.prev_print_status != status &&
        (status == "FINISH" || status == "FAILED")) {
        st.next_check = std::min(st.next_check, now + kAfterPrintEnd);
    }
    st.prev_print_status = status;

    if (st.fetching || now < st.next_check) return;

    if (m_inflight_pushes > 0) {
        if (now - m_inflight_since < kInflightTimeout) return;
        BOOST_LOG_TRIVIAL(warning) << "[fila-usage] " << m_inflight_pushes
                                   << " deduction push(es) never acknowledged; continuing";
        m_inflight_pushes = 0;
    }

    // Spool mount state comes from the cloud pull; wait for it to settle.
    auto* disp = wxGetApp().fila_manager_cloud_disp();
    if (!disp || disp->last_synced_at().empty() || disp->is_pulling()) return;

    start_fetch(dev_id);
}

void wgtFilaManagerUsageTracker::start_fetch(const std::string& dev_id)
{
    DeviceState& st    = m_devices[dev_id];
    NetworkAgent* agent = wxGetApp().getAgent();
    if (!agent || !agent->is_user_login()) {
        st.next_check = Clock::now() + kErrorInterval;
        return;
    }

    st.fetching = true;
    const long long             baseline = parse_iso_utc(st.baseline);
    const std::set<std::string> done(st.processed.begin(), st.processed.end());
    std::weak_ptr<int>          alive = m_alive;

    std::thread([this, agent, dev_id, baseline, done, alive]() {
        std::vector<nlohmann::json> hits;
        bool                        ok = true;
        for (int page = 0; page < kMaxPages; ++page) {
            BBL::TaskQueryParams params;
            params.dev_id = dev_id;
            params.limit  = kPageSize;
            params.offset = page * kPageSize;
            std::string body;
            if (agent->get_user_tasks(params, &body) != 0) { ok = false; break; }

            nlohmann::json j;
            try { j = nlohmann::json::parse(body); } catch (...) { ok = false; break; }
            if (!j.contains("hits") || !j["hits"].is_array()) break;

            // Newest first: stop paging once we reach tasks already handled or
            // older than the baseline.
            bool reached_known = false;
            for (auto& h : j["hits"]) {
                const long long end = parse_iso_utc(str_of(h, "endTime"));
                if (done.count(str_of(h, "id")) || (end >= 0 && end <= baseline)) reached_known = true;
                hits.push_back(std::move(h));
            }
            if (reached_known || j["hits"].size() < static_cast<size_t>(kPageSize)) break;
        }

        wxGetApp().CallAfter([this, alive, dev_id, ok, hits = std::move(hits)]() {
            if (alive.expired()) return;
            on_tasks_fetched(dev_id, ok, hits);
        });
    }).detach();
}

void wgtFilaManagerUsageTracker::on_tasks_fetched(const std::string& dev_id, bool ok,
                                                  const std::vector<nlohmann::json>& hits)
{
    DeviceState& st  = m_devices[dev_id];
    const auto   now = Clock::now();
    st.fetching      = false;

    if (!ok) {
        BOOST_LOG_TRIVIAL(warning) << "[fila-usage] get_user_tasks failed for " << dev_id;
        st.next_check = now + kErrorInterval;
        return;
    }

    auto*          store = wxGetApp().fila_manager_store();
    auto*          disp  = wxGetApp().fila_manager_cloud_disp();
    auto*          mgr   = wxGetApp().getDeviceManager();
    MachineObject* obj   = mgr ? mgr->get_my_machine(dev_id) : nullptr;
    if (!store || !disp || !obj) {
        st.next_check = now + kPendingInterval;
        return;
    }

    const long long baseline = parse_iso_utc(st.baseline);
    const long long now_utc  = parse_iso_utc(now_iso_utc());
    const bool      ams_ready = obj->GetFilaSystem() && !obj->GetFilaSystem()->GetAmsList().empty();

    struct Deduction {
        std::string    task_id;
        double         grams = 0;
        nlohmann::json entry;
    };
    std::map<std::string, std::vector<Deduction>> per_spool; // spool_id -> deductions, oldest first
    std::vector<std::string>                      handled_tasks;
    bool                                          pending = false;

    for (auto it = hits.rbegin(); it != hits.rend(); ++it) {
        const nlohmann::json& task    = *it;
        const std::string     task_id = str_of(task, "id");
        const long long       status  = int_of(task, "status");
        if (task_id.empty() || is_processed(st, task_id)) continue;

        if (status == kTaskPrinting || status == kTaskFinalizing) {
            // Finished and waiting for the cloud to settle: poll soon. A job the
            // printer is still running is picked up by the FINISH/FAILED trigger.
            const long long start = parse_iso_utc(str_of(task, "startTime"));
            const bool      settling = status == kTaskFinalizing || !obj->is_in_printing();
            if (settling && start > baseline && now_utc - start < 3 * 24 * 3600) pending = true;
            continue;
        }
        if (status != kTaskSuccess && status != kTaskFailed) continue;

        const long long end = parse_iso_utc(str_of(task, "endTime"));
        if (end < 0 || end <= baseline) continue;

        nlohmann::json base_entry = {
            {"time", now_iso_utc()},    {"dev_id", dev_id}, {"task_id", task_id},
            {"title", str_of(task, "title")}, {"status", status}, {"end_time", str_of(task, "endTime")},
        };

        double fraction = 1.0;
        if (status == kTaskFailed) {
            // No progress in the history: prorate by elapsed / estimated time.
            const long long start = parse_iso_utc(str_of(task, "startTime"));
            const double    cost  = num_of(task, "costTime");
            if (start < 0 || cost <= 0.0) {
                nlohmann::json e = base_entry;
                e["result"]      = "skipped: failed print without timing data";
                add_history(e);
                handled_tasks.push_back(task_id);
                continue;
            }
            fraction = std::clamp(static_cast<double>(end - start) / cost, 0.0, 1.0);
        }
        base_entry["fraction"] = fraction;

        // Sum grams per physical slot.
        std::map<std::pair<int, int>, SlotUsage> slots;
        if (task.contains("amsDetailMapping") && task["amsDetailMapping"].is_array()) {
            for (const auto& d : task["amsDetailMapping"]) {
                int ams_id = -1, slot_id = -1;
                if (!resolve_slot(task, d, ams_id, slot_id)) continue;
                SlotUsage& u  = slots[{ams_id, slot_id}];
                u.ams_id      = ams_id;
                u.slot_id     = slot_id;
                u.grams      += num_of(d, "weight") * fraction;
                u.filament_id = str_of(d, "filamentId");
                u.color       = str_of(d, "targetColor").empty() ? str_of(d, "sourceColor") : str_of(d, "targetColor");
            }
        }

        std::vector<std::pair<std::string, Deduction>> task_deductions;
        std::vector<nlohmann::json>                    task_skips;
        bool                                           defer = false;

        for (const auto& [key, u] : slots) {
            nlohmann::json e = base_entry;
            e["ams_id"]      = u.ams_id;
            e["slot_id"]     = u.slot_id;
            e["grams"]       = std::round(u.grams * 100.0) / 100.0;

            auto skip = [&](const std::string& why) {
                e["result"] = "skipped: " + why;
                task_skips.push_back(e);
            };

            if (u.grams <= 0.0) { skip("no usage recorded"); continue; }
            if (!is_external_slot(u.ams_id) && !ams_ready) { defer = true; break; }

            const DevAmsTray* tray = find_tray(obj, u.ams_id, u.slot_id);
            if (!tray || !tray->is_exists) { skip("slot is empty now"); continue; }
            if (tray->remain >= 0 || tray->remain_g >= 0) { skip("printer reports remaining for this spool"); continue; }
            if (!u.filament_id.empty() && !tray->setting_id.empty() && u.filament_id != tray->setting_id) {
                skip("slot filament changed since the print (" + u.filament_id + " -> " + tray->setting_id + ")");
                continue;
            }
            if (!u.color.empty() && !tray->color.empty() && norm_color(u.color) != norm_color(tray->color)) {
                skip("slot colour changed since the print");
                continue;
            }

            const FilamentSpool* sp = find_spool(store, dev_id, u.ams_id, u.slot_id, *tray);
            if (!sp) { skip("no Filament Manager spool in this slot"); continue; }
            e["spool_id"] = sp->spool_id;
            e["spool"]    = spool_label(*sp);
            if (FilamentSpool::is_valid_tag_uid(sp->tag_uid)) { skip("RFID spool"); continue; }
            if (current_net_weight(*sp) <= 0.0) { skip("spool has no remaining weight set"); continue; }

            task_deductions.push_back({sp->spool_id, Deduction{task_id, u.grams, e}});
        }

        if (defer) { pending = true; continue; }

        for (auto& e : task_skips) add_history(std::move(e));
        for (auto& [spool_id, d] : task_deductions) per_spool[spool_id].push_back(std::move(d));
        handled_tasks.push_back(task_id);
    }

    for (auto& [spool_id, deductions] : per_spool) {
        const FilamentSpool* sp = store->get_spool(spool_id);
        if (!sp) continue;

        double before = current_net_weight(*sp);
        double after  = before;
        for (auto& d : deductions) {
            const double next = std::max(0.0, after - d.grams);
            d.entry["before"] = std::round(after);
            d.entry["after"]  = std::round(next);
            d.entry["result"] = "deducted";
            after             = next;
        }

        const double total   = sp->effective_total_net_weight();
        const double rounded = std::round(after);
        int          pct     = sp->remain_percent;
        if (total > 0.0) pct = static_cast<int>(std::lround(std::clamp(rounded / total * 100.0, 0.0, 100.0)));
        std::string status = sp->status;
        if (status != "archived") status = (rounded <= 0.0) ? "empty" : (pct < 20) ? "low" : "active";

        nlohmann::json patch = {{"net_weight", rounded}, {"remain_percent", pct}, {"status", status}};

        BOOST_LOG_TRIVIAL(info) << "[fila-usage] spool " << spool_id << " (" << spool_label(*sp) << "): "
                                << before << "g -> " << rounded << "g over " << deductions.size() << " task(s)";
        for (auto& d : deductions) add_history(std::move(d.entry));

        if (m_inflight_pushes == 0) m_inflight_since = now;
        ++m_inflight_pushes;
        std::weak_ptr<int> alive = m_alive;
        disp->enqueue_push_update(spool_id, patch, [this, alive]() {
            if (alive.expired()) return;
            if (m_inflight_pushes > 0) --m_inflight_pushes;
        });
    }

    // Marked before the cloud acknowledges: a lost push under-counts once,
    // which is safer than deducting the same print twice.
    for (const auto& id : handled_tasks) mark_processed(st, id);
    if (!handled_tasks.empty()) save();

    st.next_check = now + (pending ? kPendingInterval : kIdleInterval);
}

bool wgtFilaManagerUsageTracker::is_processed(const DeviceState& st, const std::string& task_id) const
{
    return std::find(st.processed.begin(), st.processed.end(), task_id) != st.processed.end();
}

void wgtFilaManagerUsageTracker::mark_processed(DeviceState& st, const std::string& task_id)
{
    if (is_processed(st, task_id)) return;
    st.processed.push_back(task_id);
    if (st.processed.size() > kMaxProcessedIds)
        st.processed.erase(st.processed.begin(), st.processed.begin() + (st.processed.size() - kMaxProcessedIds));
}

void wgtFilaManagerUsageTracker::add_history(nlohmann::json entry)
{
    BOOST_LOG_TRIVIAL(info) << "[fila-usage] " << entry.dump();
    m_history.push_back(std::move(entry));
    if (m_history.size() > kMaxHistoryEntries)
        m_history.erase(m_history.begin(), m_history.begin() + (m_history.size() - kMaxHistoryEntries));
}

void wgtFilaManagerUsageTracker::load()
{
    try {
        boost::nowide::ifstream in(storage_path());
        if (!in) return;
        nlohmann::json j = nlohmann::json::parse(in);
        if (j.contains("devices") && j["devices"].is_object()) {
            for (auto& [dev_id, d] : j["devices"].items()) {
                DeviceState& st = m_devices[dev_id];
                st.baseline     = str_of(d, "baseline");
                if (d.contains("processed") && d["processed"].is_array())
                    for (const auto& id : d["processed"])
                        if (id.is_string()) st.processed.push_back(id.get<std::string>());
            }
        }
        if (j.contains("history") && j["history"].is_array()) m_history = j["history"];
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "[fila-usage] failed to load " << storage_path() << ": " << e.what();
    }
}

void wgtFilaManagerUsageTracker::save() const
{
    nlohmann::json j;
    j["devices"] = nlohmann::json::object();
    for (const auto& [dev_id, st] : m_devices)
        j["devices"][dev_id] = {{"baseline", st.baseline}, {"processed", st.processed}};
    j["history"] = m_history;

    try {
        const boost::filesystem::path path(storage_path());
        boost::filesystem::create_directories(path.parent_path());
        const std::string tmp = path.string() + ".tmp";
        {
            boost::nowide::ofstream out(tmp);
            out << j.dump(2);
        }
        boost::filesystem::rename(tmp, path);
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "[fila-usage] failed to save " << storage_path() << ": " << e.what();
    }
}

}} // namespace Slic3r::GUI
