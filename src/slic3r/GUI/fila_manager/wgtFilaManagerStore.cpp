#include "wgtFilaManagerStore.h"
#include "libslic3r/Utils.hpp"

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
        {"brand",           brand},
        {"material_type",   material_type},
        {"series",          series},
        {"color_name",      color_name},
        {"color_code",      color_code},
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
        {"cloud_synced",    cloud_synced}
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
    get("brand",           s.brand);
    get("material_type",   s.material_type);
    get("series",          s.series);
    get("color_name",      s.color_name);
    get("color_code",      s.color_code);
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
    return s;
}

// ---------- wgtFilaManagerStore ----------

std::string wgtFilaManagerStore::get_storage_path() const
{
    return (fs::path(data_dir()) / "filament_inventory" / "spools.json").string();
}

void wgtFilaManagerStore::load()
{
    std::string path = get_storage_path();
    fs::path dir = fs::path(path).parent_path();
    if (!fs::exists(dir))
        fs::create_directories(dir);

    if (!fs::exists(path)) return;

    try {
        boost::nowide::ifstream ifs(path);
        nlohmann::json j;
        ifs >> j;
        m_spools.clear();
        nlohmann::json spool_list = nlohmann::json::array();
        if (j.is_array()) {
            spool_list = j;
        } else if (j.is_object()) {
            if (j.contains("spools") && j["spools"].is_array())
                spool_list = j["spools"];
        }
        if (spool_list.is_array()) {
            for (auto& item : spool_list) {
                FilamentSpool spool = FilamentSpool::from_json(item);
                m_spools[spool.spool_id] = std::move(spool);
            }
        }
        BOOST_LOG_TRIVIAL(info) << "[FilaManager] Loaded " << m_spools.size() << " spools";
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "[FilaManager] Failed to load spools: " << e.what();
    }

    m_dirty = false;
}

void wgtFilaManagerStore::save()
{
    std::string path = get_storage_path();
    fs::path dir = fs::path(path).parent_path();
    if (!fs::exists(dir))
        fs::create_directories(dir);

    nlohmann::json arr = nlohmann::json::array();
    for (auto& [id, spool] : m_spools)
        arr.push_back(spool.to_json());
    nlohmann::json root = {
        {"spools", arr}
    };

    std::string tmp = path + ".tmp";
    {
        boost::nowide::ofstream ofs(tmp);
        ofs << root.dump(2);
    }

    boost::system::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec)
        BOOST_LOG_TRIVIAL(error) << "[FilaManager] Failed to save: " << ec.message();

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
    if (tag_uid.empty()) return nullptr;
    for (auto& [id, spool] : m_spools) {
        if (spool.tag_uid == tag_uid)
            return &spool;
    }
    return nullptr;
}

const FilamentSpool* wgtFilaManagerStore::find_by_setting_and_color(
    const std::string& setting_id, const std::string& color) const
{
    if (setting_id.empty()) return nullptr;

    const FilamentSpool* match = nullptr;
    int count = 0;
    for (auto& [id, spool] : m_spools) {
        if (spool.setting_id == setting_id && spool.color_code == color) {
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
        arr.push_back(spool.to_json());
    return arr;
}

}} // namespace Slic3r::GUI
