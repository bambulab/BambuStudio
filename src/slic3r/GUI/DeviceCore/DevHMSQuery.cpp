#include "DevHMSQuery.h"

#include <cassert>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <thread>
#include <utility>

#include <boost/filesystem.hpp>
#include <boost/format.hpp>

/* mac need the macro while including <boost/stacktrace.hpp>*/
#ifdef  __APPLE__
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#endif
#include <boost/stacktrace.hpp>

#include "libslic3r/Utils.hpp"
#include "slic3r/Utils/Http.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "DevUtil.h"
#include "libslic3r/Log.hpp"

namespace Slic3r {
namespace GUI {

namespace fs = boost::filesystem;

// HMS dictionary sources: INFO holds device_hms/device_error, ACTION holds image+action data.
static const char* HMS_QUERY_INFO   = "query_hms_info";
static const char* HMS_QUERY_ACTION = "query_hms_action";

static std::string hms_file_name(const std::string& hms_type, const std::string& lang, const std::string& dev_type)
{
    if (hms_type == HMS_QUERY_ACTION)
        return (boost::format("hms_action_%1%.json") % dev_type).str();
    return (boost::format("hms_%1%_%2%.json") % lang % dev_type).str();
}

static fs::path hms_dir()
{
    return fs::path(data_dir()) / "hms";
}

static fs::path hms_cloud_dir() { return hms_dir() / "cloud"; }
static fs::path hms_local_dir() { return hms_dir() / "local"; }

// Persist the raw cloud json to disk. Runs on an Http io_thread, never on main.
static void save_to_local(const std::string& hms_type, const std::string& lang, const std::string& dev_type, const json& save_json)
{
    if (data_dir().empty()) {
        BOOST_LOG_TRIVIAL(error) << "HMS: save_to_local, data_dir() is empty";
        return;
    }

    const fs::path folder = hms_cloud_dir();
    boost::system::error_code ec;
    if (!fs::exists(folder, ec))
        fs::create_directories(folder, ec); // ensure data_dir()/hms/cloud chain exists

    const std::string path = (folder / hms_file_name(hms_type, lang, dev_type)).make_preferred().string();
    std::ofstream     json_file(encode_path(path.c_str()));
    if (!json_file.is_open()) {
        BOOST_LOG_TRIVIAL(error) << "HMS: save_to_local failed for " << path;
        return;
    }
    json_file << std::setw(4) << save_json << std::endl;
    json_file.close();
}

static bool copy_bundled_dir_to_local(const fs::path& from_dir, const fs::path& to_dir)
{
    boost::system::error_code ec;
    if (!fs::exists(from_dir, ec) || !fs::create_directories(to_dir, ec))
        return false;

    bool ok = true;
    for (const auto& entry : fs::directory_iterator(from_dir, ec)) {
        boost::system::error_code e2; // fresh per entry: a prior failure must not leak into this one
        const fs::path& source = entry.path();
        const fs::path  dest   = to_dir / fs::relative(source, from_dir);
        if (fs::is_regular_file(source, e2)) {
            if (fs::exists(dest, e2))
                fs::remove(dest, e2);
            fs::copy_file(source, dest, e2);
        } else if (fs::is_directory(source, e2)) {
            if (!copy_bundled_dir_to_local(source, dest))
                ok = false;
        }
        if (e2)
            ok = false;
    }
    return ok && !ec; // ec set here => directory iteration failed
}

// Cheap metadata fingerprint of the bundled hms tree (relative name + size + mtime),
// computed from the read-only source so we can skip re-copying when nothing changed.
static std::string bundle_fingerprint(const fs::path& dir)
{
    uint64_t h = 1469598103934665603ULL; // FNV-1a 64-bit offset basis
    boost::system::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
        const fs::path& p = entry.path();
        if (!fs::is_regular_file(p, ec))
            continue;
        const std::string rel   = fs::relative(p, dir).string();
        const uint64_t    size  = static_cast<uint64_t>(fs::file_size(p, ec));
        const uint64_t    mtime = static_cast<uint64_t>(fs::last_write_time(p, ec));
        for (char c : rel) { h ^= static_cast<uint8_t>(c); h *= 1099511628211ULL; }
        for (int i = 0; i < 8; ++i) { h ^= static_cast<uint8_t>(size >> (8 * i)); h *= 1099511628211ULL; }
        for (int i = 0; i < 8; ++i) { h ^= static_cast<uint8_t>(mtime >> (8 * i)); h *= 1099511628211ULL; }
    }
    if (ec)
        return std::string(); // scan failed: force a copy (marker will never match)
    return (boost::format("%016llx") % static_cast<unsigned long long>(h)).str();
}

static fs::path hms_marker_path()
{
    return hms_local_dir() / ".hms_bundle_ver";
}

static std::string read_bundle_marker()
{
    const fs::path p = hms_marker_path();
    boost::system::error_code ec;
    if (!fs::exists(p, ec))
        return std::string();
    std::ifstream f(encode_path(p.string().c_str()));
    std::string   fp;
    std::getline(f, fp);
    return fp;
}

static void write_bundle_marker(const std::string& fp)
{
    boost::system::error_code ec;
    if (!fs::exists(hms_local_dir(), ec))
        fs::create_directories(hms_local_dir(), ec);
    std::ofstream f(encode_path(hms_marker_path().string().c_str()), std::ios::trunc);
    f << fp << std::endl;
}

static void sync_bundled_hms()
{
    if (resources_dir().empty() || data_dir().empty())
        return;
    const fs::path from_dir = fs::path(resources_dir()) / "hms";
    boost::system::error_code ec;
    if (!fs::exists(from_dir, ec))
        return;
    const std::string fp = bundle_fingerprint(from_dir);
    if (fp.empty() || fp == read_bundle_marker())
        return;

    if (copy_bundled_dir_to_local(from_dir, hms_local_dir()))
        write_bundle_marker(fp);
}

static std::once_flag s_bundle_sync_once;

static std::shared_ptr<const json> load_from_local(const std::string& hms_type, const std::string& lang, const std::string& dev_type, std::string& out_ver)
{
    out_ver.clear();
    if (data_dir().empty())
        return nullptr;

    const std::string file_name = hms_file_name(hms_type, lang, dev_type);
    auto try_read = [&file_name](const fs::path& dir, std::string& ver) -> std::shared_ptr<const json> {
        if (dir.empty())
            return nullptr;
        const std::string path = (dir / file_name).make_preferred().string();
        std::ifstream     json_file(encode_path(path.c_str()));
        if (!json_file.is_open())
            return nullptr;
        try {
            const json j = json::parse(json_file);
            if (j.contains("ver"))
                ver = DevJsonValParser::get_longlong_val(j["ver"]);
            else if (j.contains("version"))
                ver = DevJsonValParser::get_longlong_val(j["version"]);
            if (j.contains("data"))
                return std::make_shared<const json>(j["data"]);
        } catch (...) {
            BOOST_LOG_TRIVIAL(error) << "HMS: load_from_local failed for " << path;
        }
        return nullptr;
    };

    std::string ver;
    // Cloud sources win: the last sync is newer than the bundled dictionary.
    if (auto data = try_read(hms_cloud_dir(), ver)) { out_ver = ver; return data; }
    if (auto data = try_read(hms_dir(), ver))       { out_ver = ver; return data; } // legacy pre-split installs
    // Bundled fallback: the writable copy first, then the read-only resources tree.
    if (auto data = try_read(hms_local_dir(), ver)) { out_ver = ver; return data; }
    if (!resources_dir().empty())
        if (auto data = try_read(fs::path(resources_dir()) / "hms", ver)) { out_ver = ver; return data; }
    return nullptr;
}

// Build the incremental query URL (no IO: pure string work + in-memory AppConfig/version).
static std::string build_hms_url(const std::string& hms_type, const std::string& dev_type, const std::string& lang, const std::string& local_ver)
{
    AppConfig* config = wxGetApp().app_config;
    if (!config)
        return std::string();

    const std::string hms_host = config->get_hms_host();
    if (hms_host.empty())
        return std::string();

    std::string url;
    if (hms_type == HMS_QUERY_INFO)
        url = (boost::format("https://%1%/query.php?lang=%2%") % hms_host % lang).str();
    else if (hms_type == HMS_QUERY_ACTION)
        url = (boost::format("https://%1%/hms/GetActionImage.php?") % hms_host).str();
    else
        return std::string();

    if (!local_ver.empty())
        url += (url.find('?') != std::string::npos ? "&" : "?") + (boost::format("v=%1%") % local_ver).str();
    if (!dev_type.empty())
        url += (url.find('?') != std::string::npos ? "&" : "?") + (boost::format("d=%1%") % dev_type).str();
    return url;
}

static std::string error_code_to_ecode(int error_code)
{
    char buf[32];
    ::sprintf(buf, "%08X", error_code);
    return std::string(buf);
}

static wxString find_error_intro(const json& block, const std::string& ecode, const std::string& lang_code)
{
    auto scan = [&ecode](const json& lang_items) -> wxString {
        for (const auto& item : lang_items) {
            if (item.is_object() && item.contains("ecode") &&
                boost::to_upper_copy(item["ecode"].get<std::string>()) == ecode && item.contains("intro")) {
                return wxString::FromUTF8(item["intro"].get<std::string>());
            }
        }
        return wxEmptyString;
    };

    if (block.contains(lang_code)) {
        const wxString hit = scan(block[lang_code]);
        if (!hit.IsEmpty())
            return hit;
    }
    for (const auto& lang_items : block) {
        const wxString hit = scan(lang_items);
        if (!hit.IsEmpty())
            return hit;
    }
    return wxEmptyString;
}

// Mirrors the old _is_internal_error: an entry whose ecode exists in device_error but
// whose intro is EMPTY is an internal (suppressed) error. A missing ecode is NOT internal.
static bool is_internal_error(const json& block, const std::string& ecode, const std::string& lang_code)
{
    auto scan = [&ecode](const json& lang_items, bool& found) -> bool {
        for (const auto& item : lang_items) {
            if (item.is_object() && item.contains("ecode") &&
                boost::to_upper_copy(item["ecode"].get<std::string>()) == ecode && item.contains("intro")) {
                found = true;
                return wxString::FromUTF8(item["intro"].get<std::string>()).IsEmpty();
            }
        }
        return false;
    };

    bool found = false;
    if (block.contains(lang_code)) {
        const bool internal = scan(block[lang_code], found);
        if (found)
            return internal;
    }
    for (const auto& lang_items : block) {
        found = false;
        const bool internal = scan(lang_items, found);
        if (found)
            return internal;
    }
    return false;
}

HMSResult HMSQueryMgr::query_error(const std::string& dev_id, int error_code)
{
    if (dev_id.size() < 3)
        return HMSResult{};

    const std::string dev_type = dev_id.substr(0, 3);
    const std::string lang     = hms_language_code();
    const std::string key      = cache_key(dev_type, lang);

    std::shared_ptr<const json> snap;
    HMSStatus                   st;
    bool                        in_flight = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_entries.find(key);
        st       = (it != m_entries.end()) ? it->second.status : HMSStatus::Unknown;
        if (it != m_entries.end()) {
            snap      = it->second.info;
            in_flight = it->second.inflight;
        }
    }

    if (st == HMSStatus::Unknown) {
        ensure_loaded(dev_type);
        return { HMSStatus::Loading };
    }
    if (st == HMSStatus::Failed && !snap)
        return { HMSStatus::Failed };
    if (st == HMSStatus::Ready && !snap)
        return in_flight ? HMSResult{HMSStatus::Loading} : HMSResult{HMSStatus::Failed};
    if (!snap || st == HMSStatus::Loading)
        return { HMSStatus::Loading };

    HMSResult r;
    r.status = HMSStatus::Ready;
    if (snap->contains("device_error")) {
        const std::string ecode = error_code_to_ecode(error_code);
        r.text = find_error_intro((*snap)["device_error"], ecode, lang);
        r.is_internal = is_internal_error((*snap)["device_error"], ecode, lang);
    }
    return r; // empty text on Ready => unknown code
}

HMSResult HMSQueryMgr::query_hms(const std::string& dev_id, const std::string& long_error_code)
{
    if (dev_id.size() < 3 || long_error_code.empty())
        return HMSResult{};

    const std::string dev_type = dev_id.substr(0, 3);
    const std::string lang     = hms_language_code();
    const std::string key      = cache_key(dev_type, lang);

    std::shared_ptr<const json> snap;
    HMSStatus                   st;
    bool                        in_flight = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_entries.find(key);
        st       = (it != m_entries.end()) ? it->second.status : HMSStatus::Unknown;
        if (it != m_entries.end()) {
            snap      = it->second.info;
            in_flight = it->second.inflight;
        }
    }

    if (st == HMSStatus::Unknown) {
        ensure_loaded(dev_type);
        return { HMSStatus::Loading };
    }
    if (st == HMSStatus::Failed && !snap)
        return { HMSStatus::Failed };
    if (st == HMSStatus::Ready && !snap)
        return in_flight ? HMSResult{HMSStatus::Loading} : HMSResult{HMSStatus::Failed}; // never stuck in Loading
    if (!snap || st == HMSStatus::Loading)
        return { HMSStatus::Loading };

    HMSResult r;
    r.status = HMSStatus::Ready;
    if (snap->contains("device_hms"))
        r.text = find_error_intro((*snap)["device_hms"], boost::to_upper_copy(long_error_code), lang);
    return r;
}

HMSResult HMSQueryMgr::query_action(const std::string& dev_id, int error_code)
{
    if (dev_id.size() < 3)
        return HMSResult{};

    const std::string dev_type = dev_id.substr(0, 3);
    const std::string lang     = hms_language_code();
    const std::string key      = cache_key(dev_type, lang);

    std::shared_ptr<const json> snap;
    HMSStatus                   st;
    bool                        in_flight = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_entries.find(key);
        st       = (it != m_entries.end()) ? it->second.status : HMSStatus::Unknown;
        if (it != m_entries.end()) {
            snap      = it->second.action;
            in_flight = it->second.inflight;
        }
    }

    if (st == HMSStatus::Unknown) {
        ensure_loaded(dev_type);
        return { HMSStatus::Loading };
    }
    if (st == HMSStatus::Failed && !snap)
        return { HMSStatus::Failed };
    if (st == HMSStatus::Ready && !snap)
        return in_flight ? HMSResult{HMSStatus::Loading} : HMSResult{HMSStatus::Failed}; // never stuck in Loading
    if (!snap || st == HMSStatus::Loading)
        return { HMSStatus::Loading };

    HMSResult r;
    r.status = HMSStatus::Ready;
    const std::string ecode = error_code_to_ecode(error_code);
    if (snap->is_array()) {
        for (const auto& item : *snap) {
            if (!item.is_object() || !item.contains("ecode"))
                continue;
            if (boost::to_upper_copy(item["ecode"].get<std::string>()) != ecode)
                continue;
            const std::string device = item.contains("device") ? item["device"].get<std::string>() : std::string();
            if (boost::to_upper_copy(device) != dev_type && device != "default")
                continue;
            if (item.contains("actions")) {
                for (const auto& act : item["actions"])
                    r.actions.emplace_back(act.get<int>());
            }
            if (item.contains("image"))
                r.image_url = wxString::FromUTF8(item["image"].get<std::string>());
        }
    }
    return r;
}

void HMSQueryMgr::ensure_loaded(const std::string& dev_type)
{
    if (dev_type.empty())
        return;

    start_bundle_sync();

    const std::string lang = hms_language_code();
    const std::string key  = cache_key(dev_type, lang);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        HMSEntry& e = m_entries[key];
        if (e.inflight)
            return; // one request per (dev_type + lang) in flight
        if (e.status == HMSStatus::Ready && !need_update(e))
            return; // fresh enough
        if (e.status == HMSStatus::Unknown)
            seed_from_local(e, dev_type, lang);
        if (e.status != HMSStatus::Ready)
            e.status = HMSStatus::Loading;
        e.inflight = true;
    }

    start_download(dev_type, lang);
}

void HMSQueryMgr::seed_from_local(HMSEntry& e, const std::string& dev_type, const std::string& lang)
{
    auto info = load_from_local(HMS_QUERY_INFO, lang, dev_type, e.info_ver);
    if (info) { e.info = info; }

    auto action = load_from_local(HMS_QUERY_ACTION, lang, dev_type, e.action_ver);
    if (action) { e.action = action; }

    if (e.info || e.action)
        e.status = HMSStatus::Ready;
}

void HMSQueryMgr::start_bundle_sync()
{
    std::call_once(s_bundle_sync_once, [] { std::thread(sync_bundled_hms).detach(); });
}

// MAIN thread: kicks off two parallel perform() legs; no disk IO (versions in-memory).
void HMSQueryMgr::start_download(const std::string& dev_type, const std::string& lang)
{
    const std::string key = cache_key(dev_type, lang);

    auto ctx      = std::make_shared<DownloadCtx>();
    ctx->dev_type = dev_type;
    ctx->lang     = lang;

    std::string info_ver, action_ver;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_entries.find(key);
        if (it != m_entries.end()) {
            info_ver   = it->second.info_ver;
            action_ver = it->second.action_ver;
        }
    }

    const std::string info_url   = build_hms_url(HMS_QUERY_INFO, dev_type, lang, info_ver);
    const std::string action_url = build_hms_url(HMS_QUERY_ACTION, dev_type, lang, action_ver);
    if (info_url.empty() || action_url.empty()) {
        // hms_host unavailable: fail the whole request so UI leaves Loading.
        ctx->ok.store(false);
        apply_download(ctx);
        return;
    }

    fetch_leg(ctx, true,  info_url,   info_ver);
    fetch_leg(ctx, false, action_url, action_ver);
}

void HMSQueryMgr::fetch_leg(const std::shared_ptr<DownloadCtx>& ctx, bool is_info, const std::string& url, const std::string& local_ver)
{
    const std::string hms_type = is_info ? HMS_QUERY_INFO : HMS_QUERY_ACTION;

    Slic3r::Http::get(url)
        .timeout_max(20)
        .on_complete([this, ctx, is_info, hms_type, local_ver](std::string body, unsigned /*status*/) {
            try {
                json j = json::parse(body);
                if (j.contains("result") && j["result"] == 0 && j.contains("data") && j.contains("ver")) {
                    const std::string remote_ver = DevJsonValParser::get_longlong_val(j["ver"]);
                    if (remote_ver > local_ver) {
                        auto data = std::make_shared<const json>(j["data"]);
                        if (is_info) { ctx->info = data; ctx->info_ver = remote_ver; }
                        else         { ctx->action = data; ctx->action_ver = remote_ver; }
                        save_to_local(hms_type, ctx->lang, ctx->dev_type, j); // disk write, background
                    }
                }
                // result == 201 (unchanged) or any non-fatal case: keep the existing snapshot.
            } catch (...) {
                BOOST_LOG_TRIVIAL(warning) << current_exception_message();
                ctx->ok.store(false);
            }
            finish_leg(ctx);
        })
        .on_error([this, ctx, hms_type](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << "HMS: " << hms_type << " download error = " << error << ", status = " << status;
            ctx->ok.store(false);
            finish_leg(ctx);
        })
        .perform();
}

// io_thread: last leg to finish hands the join off to the main thread.
void HMSQueryMgr::finish_leg(const std::shared_ptr<DownloadCtx>& ctx)
{
    if (ctx->pending.fetch_sub(1) != 1)
        return; // not the last leg yet

    wxTheApp->CallAfter([this, ctx]() { apply_download(ctx); });
}

// Runs on the MAIN thread: swap the freshly parsed in-memory snapshots (zero disk read).
void HMSQueryMgr::apply_download(const std::shared_ptr<DownloadCtx>& ctx)
{
    const std::string key = cache_key(ctx->dev_type, ctx->lang);
    const bool        ok  = ctx->ok.load();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        HMSEntry& e     = m_entries[key];
        e.last_update   = time(nullptr);
        e.inflight      = false;

        if (ok) {
            if (ctx->info)   { e.info   = ctx->info;   e.info_ver   = ctx->info_ver; }
            if (ctx->action) { e.action = ctx->action; e.action_ver = ctx->action_ver; }
            e.status = HMSStatus::Ready;
        } else if (e.info || e.action) {
            e.status = HMSStatus::Ready; // a prior snapshot is still usable
        } else {
            e.status = HMSStatus::Failed;
        }
    }

    notify_subscribers(ctx->dev_type);
}

wxImage HMSQueryMgr::query_image_from_local(const wxString& image_name)
{
    if (image_name.empty() || image_name.Contains("http"))
        return wxImage();

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_local_images.empty()) {
        const fs::path local_img_dir = hms_local_dir() / "local_image";
        boost::system::error_code ec;
        if (fs::exists(local_img_dir, ec)) {
            for (const auto& entry : fs::directory_iterator(local_img_dir)) {
                const fs::path& image_path = entry.path();
                const fs::path  rel        = fs::relative(image_path, local_img_dir);
                m_local_images[wxString::FromUTF8(rel.string())] = wxImage(wxString::FromUTF8(image_path.string()));
            }
        }

        if (!resources_dir().empty()) {
            const fs::path bundled_img_dir = fs::path(resources_dir()) / "hms" / "local_image";
            if (fs::exists(bundled_img_dir, ec)) {
                for (const auto& entry : fs::directory_iterator(bundled_img_dir)) {
                    const fs::path& image_path = entry.path();
                    const fs::path  rel        = fs::relative(image_path, bundled_img_dir);
                    const wxString  key        = wxString::FromUTF8(rel.string());
                    if (m_local_images.find(key) == m_local_images.end())
                        m_local_images[key] = wxImage(wxString::FromUTF8(image_path.string()));
                }
            }
        }
    }

    auto iter = m_local_images.find(image_name);
    if (iter != m_local_images.end())
        return iter->second;
    return wxImage();
}

void HMSQueryMgr::clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries.clear();
    m_local_images.clear();
}

std::string HMSQueryMgr::hms_language_code()
{
    AppConfig* config = wxGetApp().app_config;
    if (!config)
        return "en";

    const std::string lang_code = config->get_language_code();
    if (lang_code.empty() || lang_code == "uk" || lang_code == "cs" || lang_code == "ru" ||
        lang_code == "tr" || lang_code == "pt" || lang_code == "ko") {
        return "en";
    }
    return lang_code;
}

std::string HMSQueryMgr::cache_key(const std::string& dev_type, const std::string& lang)
{
    return dev_type + "_" + lang;
}

// Caller must hold m_mutex. Re-download at most once per staleness window (24h).
bool HMSQueryMgr::need_update(const HMSEntry& entry)
{
    if (entry.last_update == 0)
        return true;
    return time(nullptr) - entry.last_update > (60 * 60 * 24);
}

HMSSubscription::HMSSubscription(HMSSubscription&& o) noexcept
    : m_mgr(std::exchange(o.m_mgr, nullptr)), m_id(std::exchange(o.m_id, 0))
{}

HMSSubscription& HMSSubscription::operator=(HMSSubscription&& o) noexcept
{
    if (this != &o) {
        reset();
        m_mgr = std::exchange(o.m_mgr, nullptr);
        m_id  = std::exchange(o.m_id, 0);
    }
    return *this;
}

void HMSSubscription::reset()
{
    if (m_mgr) {
        m_mgr->unsubscribe(m_id);
        m_mgr = nullptr;
        m_id  = 0;
    }
}

HMSSubscription HMSQueryMgr::subscribe_device(const std::string& dev_type, std::function<void()> on_changed)
{
    if (!wxThread::IsMain()) {
        assert(false && "HMS subscriptions must run on the main thread");
        BOOST_LOG_TRIVIAL(error) << "called from non-main thread, callstack: " << boost::stacktrace::stacktrace();
        return {};
    }

    const int id = m_next_sub_id++;
    m_subs.emplace(id, DevSub{dev_type, std::move(on_changed)});
    return HMSSubscription(this, id);
}

void HMSQueryMgr::unsubscribe(int id)
{
    m_subs.erase(id);
}

void HMSQueryMgr::notify_subscribers(const std::string& dev_type)
{
    std::vector<int> ids;
    for (const auto& [id, sub] : m_subs)
        if (sub.dev_type == dev_type)
            ids.push_back(id);

    // Re-check each id and copy the callback to the stack before calling: a callback may
    // erase another subscriber, or destroy its own subscription (e.g. dialog Close).
    for (int id : ids) {
        auto it = m_subs.find(id);
        if (it == m_subs.end())
            continue;
        auto on_changed = it->second.on_changed;
        on_changed();
    }
}

HMSResult HMSQueryMgr::subscribe_query(const std::string& dev_id, OnResult on_changed, std::function<HMSResult()> requery, HMSSubscription& sub)
{
    HMSResult result = requery();
    if (result.status != HMSStatus::Loading) {
        sub = {};   // terminal now: no subscription (drops any previous one held in sub)
        return result;
    }

    sub = subscribe_device(dev_id.substr(0, 3), [requery, on_changed]() {
        HMSResult r = requery();
        if (r.status != HMSStatus::Loading)
            on_changed(r);
    });
    return result;
}

HMSResult HMSQueryMgr::query_error(const std::string& dev_id, int error_code, OnResult on_changed, HMSSubscription& sub)
{
    if (!wxThread::IsMain()) {
        assert(false && "HMS subscriptions must run on the main thread");
        BOOST_LOG_TRIVIAL(error) << "called from non-main thread, callstack: " << boost::stacktrace::stacktrace();
        return {};
    }
    return subscribe_query(dev_id, std::move(on_changed), [this, dev_id, error_code] { return query_error(dev_id, error_code); }, sub);
}

HMSResult HMSQueryMgr::query_hms(const std::string& dev_id, const std::string& long_error_code, OnResult on_changed, HMSSubscription& sub)
{
    if (!wxThread::IsMain()) {
        assert(false && "HMS subscriptions must run on the main thread");
        BOOST_LOG_TRIVIAL(error) << "called from non-main thread, callstack: " << boost::stacktrace::stacktrace();
        return {};
    }
    return subscribe_query(dev_id, std::move(on_changed), [this, dev_id, long_error_code] { return query_hms(dev_id, long_error_code); }, sub);
}

HMSResult HMSQueryMgr::query_action(const std::string& dev_id, int error_code, OnResult on_changed, HMSSubscription& sub)
{
    if (!wxThread::IsMain()) {
        assert(false && "HMS subscriptions must run on the main thread");
        BOOST_LOG_TRIVIAL(error) << "called from non-main thread, callstack: " << boost::stacktrace::stacktrace();
        return {};
    }
    return subscribe_query(dev_id, std::move(on_changed), [this, dev_id, error_code] { return query_action(dev_id, error_code); }, sub);
}

}} // namespace Slic3r::GUI
