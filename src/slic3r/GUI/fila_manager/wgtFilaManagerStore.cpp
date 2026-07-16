#include "wgtFilaManagerStore.h"
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/GUI_App.hpp"

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace Slic3r { namespace GUI {

namespace fs = boost::filesystem;

static std::string generate_uuid()
{
    static boost::uuids::random_generator gen;
    return boost::uuids::to_string(gen());
}

static std::string now_iso8601()
{
    auto        now = std::chrono::system_clock::now();
    std::time_t t   = std::chrono::system_clock::to_time_t(now);
    std::tm     tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return ss.str();
}

// ---------- FilamentSpool serialization ----------

nlohmann::json FilamentSpool::to_json() const
{
    return nlohmann::json{
        {"spool_id",        spool_id},
        {"setting_id",      setting_id},
        {"tag_uid",         tag_uid},
        {"tray_id_name",    tray_id_name},
        {"brand",           brand},
        {"material_type",   material_type},
        {"series",          series},
        {"color_name",      color_name},
        {"color_code",      color_code},
        {"colors",          colors},        // STUDIO-17977
        {"color_type",      color_type},    // STUDIO-17977
        {"diameter",        diameter},
        {"initial_weight",  initial_weight},
        {"spool_weight",    spool_weight},
        {"remain_percent",  remain_percent},
        {"status",          status},
        {"entry_method",    entry_method},
        {"created_at",      created_at},
        {"updated_at",      updated_at},
        {"bound_dev_id",    bound_dev_id},
        {"bound_ams_id",    bound_ams_id},
        {"note",            note},
        {"favorite",        favorite},
        {"net_weight",      net_weight},
        {"cloud_synced",    cloud_synced},
        {"in_printer",      in_printer},
        {"dev_id",          dev_id},
        {"ams_sn",          ams_sn},
        {"ams_id",          ams_id},
        {"ams_type",        ams_type},
        {"slot_id",         slot_id},
        {"device_name",     device_name},
        {"tray_label",      [this]() -> std::string {
            if (ams_id < 0 || slot_id.empty()) return {};
            try {
                int tray_id = ams_id * 4 + std::stoi(slot_id);
                return wxGetApp().transition_tridid(tray_id).ToStdString();
            } catch (...) { return {}; }
        }()}
    };
}

FilamentSpool FilamentSpool::from_json(const nlohmann::json& j)
{
    FilamentSpool s;
    auto get = [&](const char* key, auto& dst) {
        if (j.contains(key)) j.at(key).get_to(dst);
    };
    get("spool_id",        s.spool_id);
    get("setting_id",      s.setting_id);
    get("tag_uid",         s.tag_uid);
    get("tray_id_name",    s.tray_id_name);
    get("brand",           s.brand);
    get("material_type",   s.material_type);
    get("series",          s.series);
    get("color_name",      s.color_name);
    get("color_code",      s.color_code);
    get("colors",          s.colors);       // STUDIO-17977
    get("color_type",      s.color_type);   // STUDIO-17977
    // STUDIO-17977 invariant: when colors is non-empty, color_code must be colors[0].
    // Repair silently for legacy data that may have drifted.
    if (!s.colors.empty() && s.color_code != s.colors.front()) {
        s.color_code = s.colors.front();
    }
    get("diameter",        s.diameter);
    get("initial_weight",  s.initial_weight);
    get("spool_weight",    s.spool_weight);
    get("remain_percent",  s.remain_percent);
    get("status",          s.status);
    get("entry_method",    s.entry_method);
    get("created_at",      s.created_at);
    get("updated_at",      s.updated_at);
    get("bound_dev_id",    s.bound_dev_id);
    get("bound_ams_id",    s.bound_ams_id);
    get("note",            s.note);
    get("favorite",        s.favorite);
    get("net_weight",      s.net_weight);
    get("cloud_synced",    s.cloud_synced);
    get("in_printer",      s.in_printer);
    get("dev_id",          s.dev_id);
    get("ams_sn",          s.ams_sn);
    get("ams_id",          s.ams_id);
    get("ams_type",        s.ams_type);
    get("slot_id",         s.slot_id);
    get("device_name",     s.device_name);
    return s;
}

nlohmann::json FilamentSpool::to_json_with_runtime() const
{
    // 在位字段已并入 to_json() 持久化，此处直接复用。
    return to_json();
}

bool FilamentSpool::is_valid_tag_uid(const std::string& tag_uid)
{
    if (tag_uid.empty()) return false;
    for (char c : tag_uid) {
        if (c != '0') return true;
    }
    return false;
}

// ---------- wgtFilaManagerStore ----------

std::string wgtFilaManagerStore::get_storage_path() const
{
    return (fs::path(data_dir()) / "filament_inventory" / "spools.json").string();
}

void wgtFilaManagerStore::load()
{
    // local persistence removed — store starts empty, populated by cloud pull after login
}

void wgtFilaManagerStore::save()
{
    // local persistence removed
    m_dirty = false;
}

// ---------- CRUD ----------

std::string wgtFilaManagerStore::add_spool(const FilamentSpool& spool)
{
    FilamentSpool s = spool;
    if (s.spool_id.empty())   s.spool_id   = generate_uuid();
    if (s.created_at.empty()) s.created_at  = now_iso8601();
    s.updated_at = now_iso8601();
    const std::string created_id = s.spool_id;
    m_spools[created_id] = std::move(s);
    m_dirty = true;
    return created_id;
}

void wgtFilaManagerStore::update_spool(const FilamentSpool& spool)
{
    auto it = m_spools.find(spool.spool_id);
    if (it == m_spools.end()) return;
    FilamentSpool s = spool;
    s.updated_at    = now_iso8601();
    it->second      = std::move(s);
    m_dirty         = true;
}

bool wgtFilaManagerStore::update_spool_if_changed(const FilamentSpool& sp)
{
    auto it = m_spools.find(sp.spool_id);
    if (it == m_spools.end()) {
        // STUDIO-18155 Q5：spool 不在 store 时**不**退化为 add——AMS 同步
        // 路径不允许新增 spool。竞态/越权调用走这里仅打 warn。
        BOOST_LOG_TRIVIAL(warning)
            << "[FilaManager] update_spool_if_changed missing spool_id="
            << sp.spool_id << " (auto-add disabled by design)";
        return false;
    }
    FilamentSpool& cur = it->second;

    // 仅比较"sync 关心字段"。identity 字段（spool_id / tag_uid / color_code /
    // setting_id / entry_method / created_at / cloud_synced）和元字段
    // （brand / material_type / series / color_name / diameter /
    // initial_weight / spool_weight / total_net_weight / note / favorite）
    // 由 sync 完全不动，比较时直接忽略输入 sp 的对应字段。
    //
    const bool changed =
           cur.net_weight     != sp.net_weight
        || cur.remain_percent != sp.remain_percent
        || cur.status         != sp.status
        || cur.bound_dev_id   != sp.bound_dev_id
        || cur.bound_ams_id   != sp.bound_ams_id;

    if (!changed) return false;

    // 只覆盖 sync 关心的字段，identity / 元字段强制保留 cur 既有值。
    cur.net_weight     = sp.net_weight;
    cur.remain_percent = sp.remain_percent;
    cur.status         = sp.status;
    cur.bound_dev_id   = sp.bound_dev_id;
    cur.bound_ams_id   = sp.bound_ams_id;
    cur.updated_at     = now_iso8601();
    m_dirty            = true;
    return true;
}

bool wgtFilaManagerStore::apply_patch(const std::string& spool_id, const nlohmann::json& patch)
{
    auto it = m_spools.find(spool_id);
    if (it == m_spools.end()) return false;
    FilamentSpool& s = it->second;

    // 仅合并用户可编辑字段；不接受 null（表示"未提供"）。系统字段 spool_id /
    // tag_uid / entry_method / created_at / bound_* / cloud_synced 故意不在此处
    // 合并，避免前端 patch 清掉它们（见 STUDIO-17964 Problem A）。
    auto get_if = [&](const char* key, auto& dst) {
        if (!patch.contains(key)) return;
        const auto& v = patch.at(key);
        if (v.is_null()) return;
        try { v.get_to(dst); } catch (...) {}
    };
    get_if("setting_id",      s.setting_id);
    get_if("brand",           s.brand);
    get_if("material_type",   s.material_type);
    get_if("series",          s.series);
    get_if("color_name",      s.color_name);
    get_if("color_code",      s.color_code);
    get_if("colors",          s.colors);
    get_if("color_type",      s.color_type);
    get_if("diameter",        s.diameter);
    get_if("initial_weight",  s.initial_weight);
    get_if("spool_weight",    s.spool_weight);
    get_if("remain_percent",  s.remain_percent);
    get_if("status",          s.status);
    get_if("note",            s.note);
    get_if("favorite",        s.favorite);
    get_if("net_weight",      s.net_weight);

    s.updated_at   = now_iso8601();
    s.cloud_synced = false;
    m_dirty        = true;
    return true;
}

void wgtFilaManagerStore::remove_spool(const std::string& spool_id)
{
    m_spools.erase(spool_id);
    m_dirty = true;
}

bool wgtFilaManagerStore::mark_synced(const std::string& spool_id, bool synced)
{
    auto it = m_spools.find(spool_id);
    if (it == m_spools.end()) return false;
    if (it->second.cloud_synced == synced) return false;
    it->second.cloud_synced = synced;
    m_dirty = true;
    return true;
}

const FilamentSpool* wgtFilaManagerStore::get_spool(const std::string& spool_id) const
{
    auto it = m_spools.find(spool_id);
    return it != m_spools.end() ? &it->second : nullptr;
}

// ---------- AMS matching ----------

const FilamentSpool* wgtFilaManagerStore::find_by_tag_uid(const std::string& tag_uid) const
{
    if (!FilamentSpool::is_valid_tag_uid(tag_uid)) return nullptr;
    for (auto& [id, spool] : m_spools) {
        if (spool.tag_uid == tag_uid)
            return &spool;
    }
    return nullptr;
}

std::vector<std::string> wgtFilaManagerStore::all_spool_ids() const
{
    std::vector<std::string> ids;
    ids.reserve(m_spools.size());
    for (auto& [id, _] : m_spools) ids.push_back(id);
    return ids;
}

const FilamentSpool* wgtFilaManagerStore::find_by_setting_and_color(
    const std::string& setting_id, const std::string& color) const
{
    if (setting_id.empty()) return nullptr;

    // 规范化 color 到不带 # 的 6 位大写 RRGGBB：
    //   AMS 上报格式为 "RRGGBBAA"（8位无#），store 存储格式为 "#RRGGBB"（6位带#）。
    auto normalize = [](const std::string& c) -> std::string {
        std::string s = c;
        if (!s.empty() && s[0] == '#') s = s.substr(1);
        if (s.size() == 8) s = s.substr(0, 6);
        for (auto& ch : s) ch = static_cast<char>(toupper(static_cast<unsigned char>(ch)));
        return s;
    };

    const std::string norm_color = normalize(color);

    const FilamentSpool* match = nullptr;
    int count = 0;
    for (auto& [id, spool] : m_spools) {
        if (spool.setting_id == setting_id && normalize(spool.color_code) == norm_color) {
            match = &spool;
            ++count;
        }
    }
    return count == 1 ? match : nullptr;
}

// ---------- JSON for WebView ----------

nlohmann::json wgtFilaManagerStore::spools_to_json() const
{
    nlohmann::json arr = nlohmann::json::array();
    for (auto& [id, spool] : m_spools)
        arr.push_back(spool.to_json_with_runtime());
    return arr;
}

bool wgtFilaManagerStore::force_mount_spool(const std::string& spool_id,
                                            const std::string& dev_id,
                                            const std::string& dev_name,
                                            int                ams_id,
                                            int                ams_type,
                                            const std::string& slot_id)
{
    auto it = m_spools.find(spool_id);
    if (it == m_spools.end()) {
        BOOST_LOG_TRIVIAL(warning) << "force_mount_spool: spool_id not found: " << spool_id;
        return false;
    }
    FilamentSpool& s = it->second;
    s.in_printer  = true;
    s.dev_id      = dev_id;
    s.device_name = dev_name;
    s.ams_id      = ams_id;
    s.ams_type    = ams_type;
    s.slot_id     = slot_id;
    set_dirty();
    return true;
}

bool wgtFilaManagerStore::apply_mount_diff(
    const std::string& dev_id,
    const std::string& dev_name,
    const std::map<std::string, MountUpdate>& present_now,
    std::vector<std::string>* out_changed_ids)
{
    bool changed = false;
    for (auto& [id, s] : m_spools) {
        auto it = present_now.find(id);
        const bool now_present  = (it != present_now.end());
        const bool was_our_hold = (s.in_printer && s.dev_id == dev_id);

        if (now_present) {
            // 本轮在本机上：只在字段实际变化时才写，避免每次 sync 都触发前端刷新。
            //   - 之前挂在别机 → 这里自然抢过所有权（用户换机场景）
            //   - 之前也挂在本机同槽 → same_state=true，字段一字不改
            const MountUpdate& u = it->second;
            const bool same_state =
                   s.in_printer  == true
                && s.dev_id      == dev_id
                && s.ams_id      == u.ams_id
                && s.ams_type    == u.ams_type
                && s.slot_id     == u.slot_id
                && s.ams_sn      == u.ams_sn
                && s.device_name == dev_name;
            if (!same_state) {
                s.in_printer  = true;
                s.dev_id      = dev_id;
                s.device_name = dev_name;
                s.ams_id      = u.ams_id;
                s.ams_type    = u.ams_type;
                s.slot_id     = u.slot_id;
                s.ams_sn      = u.ams_sn;
                changed = true;
                if (out_changed_ids) out_changed_ids->push_back(id);
            }
        } else if (was_our_hold) {
            // 本机上一次拥有它、这次没上报 → 本机拔出事件，清字段
            s.in_printer  = false;
            s.dev_id.clear();
            s.device_name.clear();
            s.ams_id      = -1;
            s.ams_type    = -1;
            s.slot_id.clear();
            s.ams_sn.clear();
            changed = true;
            if (out_changed_ids) out_changed_ids->push_back(id);
        }
        // 其余情况（挂在别机 / 从未在位）保持原状
    }
    return changed;
}

}} // namespace Slic3r::GUI
