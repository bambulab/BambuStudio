#include "wgtFilaManagerCloudSync.h"
#include "wgtFilaManagerCloudClient.h"
#include "wgtFilaManagerStore.h"

#include "slic3r/Utils/NetworkAgent.hpp"
#include "slic3r/GUI/GUI_App.hpp"

#include <wx/app.h>
#include <boost/log/trivial.hpp>
#include <set>

namespace Slic3r { namespace GUI {

namespace {

nlohmann::json extract_cloud_list(const nlohmann::json& data)
{
    if (data.contains("hits") && data["hits"].is_array())
        return data["hits"];
    if (data.is_array())
        return data;
    if (data.contains("data")) {
        const auto& d = data["data"];
        if (d.is_array())
            return d;
        if (d.is_object() && d.contains("hits") && d["hits"].is_array())
            return d["hits"];
    }
    if (data.contains("filaments") && data["filaments"].is_array())
        return data["filaments"];
    return nlohmann::json::array();
}

// Cloud error callbacks hand us the raw HTTP response body in `err` (see
// wgtFilaManagerCloudClient: `response_body.empty() ? "network request failed"
// : response_body`). That body can carry user-scoped or server-internal
// context, so we only append it to the shipped BambuStudio.log in internal
// builds; external builds keep just the HTTP return code.
inline std::string err_body_tail(const std::string& err)
{
#if !BBL_RELEASE_TO_PUBLIC
    return " " + err;
#else
    (void)err;
    return std::string();
#endif
}

} // namespace

wgtFilaManagerCloudSync::wgtFilaManagerCloudSync(
    wgtFilaManagerStore* store, wgtFilaManagerCloudClient* client)
    : m_store(store)
    , m_client(client)
{}

// ---------------------------------------------------------------------------
// Data conversion helpers
// ---------------------------------------------------------------------------

FilamentSpool wgtFilaManagerCloudSync::cloud_json_to_spool(const nlohmann::json& j)
{
    FilamentSpool s;

    // Accept both cloud camelCase and legacy snake_case keys. Cloud wins when
    // both are present.
    auto str_any = [&](std::initializer_list<const char*> keys) -> std::string {
        for (const char* key : keys) {
            if (!j.contains(key)) continue;
            const auto& v = j[key];
            if (v.is_null()) continue;
            if (v.is_string())          return v.get<std::string>();
            if (v.is_number_integer())  return std::to_string(v.get<int64_t>());
            if (v.is_number_unsigned()) return std::to_string(v.get<uint64_t>());
            if (v.is_number_float())    return std::to_string(v.get<double>());
            if (v.is_boolean())         return v.get<bool>() ? "true" : "false";
        }
        return {};
    };
    auto num_f_any = [&](std::initializer_list<const char*> keys, float def = 0.f) -> float {
        for (const char* key : keys) {
            if (!j.contains(key)) continue;
            const auto& v = j[key];
            if (v.is_null()) continue;
            if (v.is_number()) return v.get<float>();
        }
        return def;
    };
    auto num_i_any = [&](std::initializer_list<const char*> keys, int def = 0) -> int {
        for (const char* key : keys) {
            if (!j.contains(key)) continue;
            const auto& v = j[key];
            if (v.is_null()) continue;
            if (v.is_number()) return v.get<int>();
        }
        return def;
    };

    // Identity / preset linkage (cloud FilamentV2 uses int64 id + camelCase).
    s.spool_id      = str_any({"id", "spool_id"});
    s.setting_id    = str_any({"filamentId", "setting_id"});
    s.tag_uid       = str_any({"RFID", "rfid", "tag_uid"});

    // Descriptive fields.
    s.brand         = str_any({"filamentVendor", "brand"});
    s.material_type = str_any({"filamentType", "material_type"});
    s.series        = str_any({"filamentName", "series"});
    s.color_code    = str_any({"color", "color_code"});
    s.color_name    = str_any({"color_name"}); // cloud has no direct counterpart

    s.diameter        = num_f_any({"diameter"}, 1.75f);

    // Weights: cloud uses totalNetWeight / netWeight (nullable int64 grams).
    s.initial_weight = num_f_any({"totalNetWeight", "initial_weight"});
    s.net_weight     = num_f_any({"netWeight", "net_weight"});
    s.spool_weight   = num_f_any({"spool_weight"});

    // remain_percent is not provided by cloud; derive from weights when we can,
    // otherwise honor any locally-provided value (+0.5f for simple rounding).
    if (s.initial_weight > 0.f && s.net_weight >= 0.f) {
        float pct = s.net_weight / s.initial_weight * 100.0f;
        if (pct < 0.f)   pct = 0.f;
        if (pct > 100.f) pct = 100.f;
        s.remain_percent = static_cast<int>(pct + 0.5f);
    } else {
        s.remain_percent = num_i_any({"remain_percent"}, 100);
    }

    // Cloud status is int64: 0=active, 1=info_needed. Legacy payloads may send
    // the string directly.
    if (j.contains("status")) {
        const auto& v = j["status"];
        if (v.is_number_integer()) {
            s.status = (v.get<int>() == 1) ? "info_needed" : "active";
        } else if (v.is_string()) {
            s.status = v.get<std::string>();
        }
    }
    if (s.status.empty()) s.status = "active";

    // Cloud uses "manual" | "ams"; the local UI still distinguishes AMS-origin
    // entries with the legacy "ams_sync" marker.
    s.entry_method = str_any({"createType", "entry_method"});
    if (s.entry_method == "ams")
        s.entry_method = "ams_sync";

    // createdAt / updatedAt are unix seconds (int64) on the cloud. Keep them
    // stringified so the local schema and on-disk cache don't need to change.
    s.created_at = str_any({"createdAt", "created_at"});
    s.updated_at = str_any({"updatedAt", "updated_at"});

    s.bound_dev_id = str_any({"bound_dev_id"});
    s.bound_ams_id = str_any({"trayIdName", "bound_ams_id"});
    s.note         = str_any({"note"});

    s.favorite        = j.contains("favorite") && j["favorite"].is_boolean() && j["favorite"].get<bool>();

    return s;
}

nlohmann::json wgtFilaManagerCloudSync::spool_to_cloud_json(const FilamentSpool& s)
{
    // Cloud CreateFilamentV2Req / UpdateFilamentV2Req schema (camelCase).
    nlohmann::json j;

    // createType: "manual" (手动) | "ams" (AMS 自动). Normalize legacy value
    // "ams_sync" that the old local format used.
    std::string create_type = s.entry_method;
    if (create_type == "ams_sync") create_type = "ams";
    if (create_type == "rfid")     create_type = "ams";
    if (create_type.empty())       create_type = "manual";
    j["createType"]     = create_type;

    j["filamentVendor"] = s.brand;
    j["filamentType"]   = s.material_type;
    // Swagger marks filamentName / colorType as required. Manual add currently
    // models only a single-colour spool and may leave `series` empty, so fall
    // back to the material type for the display name and encode monochrome as 2.
    j["filamentName"]   = s.series.empty() ? s.material_type : s.series;
    j["filamentId"]     = s.setting_id;
    j["isSupport"]      = false; // local schema has no equivalent yet
    if (!s.tag_uid.empty())
        j["RFID"]       = s.tag_uid;
    j["color"]          = s.color_code;
    j["colorType"]      = 2; // 2 = 单色, per CreateFilamentV2Req swagger
    if (create_type == "ams" && !s.bound_ams_id.empty()) {
        j["trayIdName"] = s.bound_ams_id;
        j["rolls"]      = 1;
    }

    // Cloud schema (CreateFilamentV2Req): netWeight = 当前净重 (current
    // material remaining), totalNetWeight = 整卷净重 (the spool's full
    // net weight when new). After STUDIO-17991 the local store keeps both
    // in net-weight units with spool_weight==0, so initial_weight is the
    // pure 整卷净重 and maps verbatim onto totalNetWeight. Legacy rows
    // still in the 毛重/料盘 shape (spool_weight > 0, initial_weight = 毛重)
    // collapse "initial - spool" to recover the 整卷净重 the cloud expects.
    //
    // STUDIO-18115: previously the manual branch mirrored totalNetWeight
    // from material_weight, which silently truncated the user-entered
    // 总净重 to the 当前净重 whenever the two differed (e.g. 当前=200,
    // 总=1500 was pushed as netWeight=200, totalNetWeight=200 and the
    // row came back from cloud showing 200/200 instead of 200/1500).
    const float material_weight = s.net_weight > 0.f
        ? s.net_weight
        : ((s.initial_weight > s.spool_weight) ? (s.initial_weight - s.spool_weight) : 0.f);
    const float total_net_weight = (s.spool_weight > 0.f && s.initial_weight > s.spool_weight)
        ? (s.initial_weight - s.spool_weight)
        : s.initial_weight;
    if (material_weight > 0.f)
        j["netWeight"] = static_cast<int64_t>(material_weight + 0.5f);
    if (total_net_weight > 0.f)
        j["totalNetWeight"] = static_cast<int64_t>(total_net_weight + 0.5f);

    if (!s.note.empty())
        j["note"] = s.note;

    return j;
}

// ---------------------------------------------------------------------------
// Pull: cloud → local
// ---------------------------------------------------------------------------

void wgtFilaManagerCloudSync::pull_from_cloud()
{
    if (m_syncing) return;

    NetworkAgent* agent = wxGetApp().getAgent();
    if (!agent || !agent->is_user_login()) return;

    m_syncing = true;
    m_last_pull_succeeded = false;
    m_last_pull_error_code = 0;
    m_last_pull_error_message.clear();
    BOOST_LOG_TRIVIAL(info) << "[FilaCloudSync] pull_from_cloud started";
    wxGetApp().emit_fila_debug_log("data", "info", "Cloud pull started",
                                   "Starting pull_from_cloud merge",
                                   nlohmann::json::object());

    m_client->list_spools({},
        [this](const nlohmann::json& data) {
            wxTheApp->CallAfter([this, data]() {
                try {
                    // Cloud ListFilamentV2Resp returns { total, hits: [...] }
                    // at the root; tolerate a few alternative shapes for
                    // forward/backward compatibility.
                    nlohmann::json list = extract_cloud_list(data);

                    // Cloud is the source of truth: collect every cloud id we
                    // are about to keep, then rewrite the local store to match
                    // exactly that set. Local-only entries (e.g. pushes that
                    // never succeeded) are dropped on purpose so a pull always
                    // leaves the local list in sync with the latest cloud
                    // snapshot.
                    std::set<std::string> cloud_ids;
                    int dropped_local_only = 0;

                    for (const auto& item : list) {
                        FilamentSpool spool = cloud_json_to_spool(item);
                        if (spool.spool_id.empty()) continue;
                        spool.cloud_synced = true;
                        cloud_ids.insert(spool.spool_id);

                        if (m_store->get_spool(spool.spool_id))
                            m_store->update_spool(spool);
                        else
                            m_store->add_spool(spool);
                    }

                    for (const auto& existing : m_store->spools_to_json()) {
                        const std::string existing_id = existing.value("spool_id", "");
                        if (existing_id.empty()) continue;
                        if (cloud_ids.count(existing_id) == 0) {
                            m_store->remove_spool(existing_id);
                            ++dropped_local_only;
                        }
                    }

                    m_store->save();
                    m_last_pull_succeeded = true;
                    BOOST_LOG_TRIVIAL(info) << "[FilaCloudSync] pull_from_cloud completed, "
                                            << list.size() << " items kept, "
                                            << dropped_local_only << " local-only entries dropped";
                    wxGetApp().emit_fila_debug_log("data", "info", "Cloud pull merged",
                                                   "Cloud pull overwrote local store",
                                                   {{"count", static_cast<int>(list.size())},
                                                    {"dropped_local_only", dropped_local_only}});
                } catch (const std::exception& e) {
                    m_last_pull_succeeded = false;
                    m_last_pull_error_code = -1;
                    m_last_pull_error_message = e.what();
                    BOOST_LOG_TRIVIAL(error) << "[FilaCloudSync] pull_from_cloud merge error: " << e.what();
                    wxGetApp().emit_fila_debug_log("data", "error", "Cloud pull merge error",
                                                   "Merging cloud pull result into local store failed",
                                                   {{"error", e.what()}});
                }
                m_syncing = false;
            });
        },
        [this](int code, const std::string& err) {
            wxTheApp->CallAfter([this, code, err]() {
                m_last_pull_succeeded = false;
                m_last_pull_error_code = code;
                m_last_pull_error_message = err;
                BOOST_LOG_TRIVIAL(error) << "[FilaCloudSync] pull_from_cloud failed: " << code << err_body_tail(err);
                wxGetApp().emit_fila_debug_log("http", "error", "Cloud pull failed",
                                               "list_spools failed during pull_from_cloud",
                                               {{"code", code}, {"error", err}});
                m_syncing = false;
            });
        });
}

// ---------------------------------------------------------------------------
// Push: local → cloud
// ---------------------------------------------------------------------------

void wgtFilaManagerCloudSync::push_spool_to_cloud(const std::string& spool_id)
{
    const FilamentSpool* spool = m_store->get_spool(spool_id);
    if (!spool) return;

    nlohmann::json body = spool_to_cloud_json(*spool);

    m_client->create_spool(body,
        [spool_id](const nlohmann::json& /*resp*/) {
            BOOST_LOG_TRIVIAL(info) << "[FilaCloudSync] push_spool_to_cloud ok: " << spool_id;
        },
        [spool_id](int code, const std::string& err) {
            BOOST_LOG_TRIVIAL(error) << "[FilaCloudSync] push_spool_to_cloud failed ("
                                     << spool_id << "): " << code << err_body_tail(err);
        });
}

nlohmann::json wgtFilaManagerCloudSync::spool_to_cloud_update_patch(const nlohmann::json& p)
{
    // UpdateFilamentV2Req（swagger 权威定义，设计于 design-user.api / design-user-swagger.json）：
    //   id              int64   required  ← 由 client 负责塞入，本函数不处理
    //   filamentVendor  string  optional
    //   filamentType    string  optional
    //   filamentName    string  optional
    //   filamentId      string  optional
    //   isSupport       bool    optional
    //   color           string  optional
    //   colorType       int64   optional   0=渐变 / 1=拼色 / 2=单色
    //   colors          []string optional
    //   netWeight       int64   optional
    //   totalNetWeight  int64   optional
    //   note            string  optional
    //
    // 规则：只输出 swagger 白名单字段；本地 patch 里未出现或 null 的字段不发，
    // 服务端"只更新提供的字段"。系统专属字段（createType / RFID / trayIdName /
    // rolls 等 Create/AmsSync 专属）严禁出现在 Update body，否则 go-zero 严格
    // 校验会把请求打回 400（对应到本地 circuit breaker 就是 -29 internal blocking）。
    nlohmann::json j = nlohmann::json::object();
    if (!p.is_object()) return j;

    auto take_str = [&](const char* local_key, const char* cloud_key) {
        if (!p.contains(local_key)) return;
        const auto& v = p.at(local_key);
        if (v.is_null()) return;
        if (v.is_string()) j[cloud_key] = v.get<std::string>();
    };
    auto take_int = [&](const char* local_key, const char* cloud_key) {
        if (!p.contains(local_key)) return;
        const auto& v = p.at(local_key);
        if (v.is_null()) return;
        if (v.is_number())
            j[cloud_key] = static_cast<int64_t>(v.get<double>() + 0.5);
    };
    auto take_bool = [&](const char* local_key, const char* cloud_key) {
        if (!p.contains(local_key)) return;
        const auto& v = p.at(local_key);
        if (v.is_null()) return;
        if (v.is_boolean()) j[cloud_key] = v.get<bool>();
    };

    // 本地字段 → swagger 字段（只映射本地有对应概念的那些）：
    take_str("brand",           "filamentVendor");
    take_str("material_type",   "filamentType");
    take_str("series",          "filamentName");    // UI 的 Material Type 实际对应云端 filamentName
    take_str("filament_name",   "filamentName");    // 兼容未来显式字段
    take_str("setting_id",      "filamentId");
    take_bool("is_support",     "isSupport");       // 同上
    take_str("color_code",      "color");
    take_int("color_type",      "colorType");       // 同上
    // colors[] 目前本地没有拼/渐变色概念，若前端 patch 里主动带 colors 数组就透传。
    if (p.contains("colors") && p.at("colors").is_array())
        j["colors"] = p.at("colors");
    take_int("net_weight",      "netWeight");
    take_int("total_net_weight","totalNetWeight");
    take_str("note",            "note");

    // swagger 白名单之外的常见本地字段（series / color_name / initial_weight /
    // spool_weight / diameter / status / remain_percent / favorite ...）不映射，
    // 它们只存在于本地 store，不上报给云端 Update 接口。

    return j;
}

nlohmann::json wgtFilaManagerCloudSync::spool_to_cloud_update_json(const FilamentSpool& s,
                                                                   const nlohmann::json& local_patch)
{
    nlohmann::json j = spool_to_cloud_update_patch(local_patch);
    if (j.empty())
        return j;

    auto patch_str = [&](const char* key) -> std::string {
        if (!local_patch.is_object() || !local_patch.contains(key)) return {};
        const auto& v = local_patch.at(key);
        return v.is_string() ? v.get<std::string>() : std::string();
    };

    // Edit requests must include filamentName even when the user only changes
    // color/weight/note. The visible "Material Type" field is cloud
    // filamentName, stored locally as `series`; filamentType is hidden.
    if (!j.contains("filamentName")) {
        std::string filament_name = patch_str("series");
        if (filament_name.empty()) filament_name = patch_str("filament_name");
        if (filament_name.empty()) filament_name = s.series.empty() ? s.material_type : s.series;
        j["filamentName"] = filament_name;
    }

    return j;
}

void wgtFilaManagerCloudSync::push_update_to_cloud(const std::string& spool_id,
                                                   const nlohmann::json& local_patch)
{
    const FilamentSpool* spool = m_store->get_spool(spool_id);
    if (!spool) return;

    nlohmann::json body = spool_to_cloud_update_json(*spool, local_patch);

    // 全量 body 仅用于 404 fallback：服务端没记录这条时补一次 create。
    nlohmann::json create_body = spool_to_cloud_json(*spool);

    if (body.empty()) {
        // patch 映射后空：本次没有任何云端认识的变化字段，直接标记已同步即可，
        // 不再发空 PUT（有些服务端会拒 0 字段更新）。
        BOOST_LOG_TRIVIAL(info) << "[FilaCloudSync] push_update_to_cloud skipped (empty patch): " << spool_id;
        return;
    }

    auto fallback_create = [this, create_body, spool_id]() {
        m_client->create_spool(create_body,
            [spool_id](const nlohmann::json&) {
                BOOST_LOG_TRIVIAL(info) << "[FilaCloudSync] push_update fallback create ok: " << spool_id;
            },
            [spool_id](int c, const std::string& e) {
                BOOST_LOG_TRIVIAL(error) << "[FilaCloudSync] push_update fallback create failed ("
                                         << spool_id << "): " << c << err_body_tail(e);
            });
    };

    m_client->update_spool(spool_id, body,
        [spool_id](const nlohmann::json& /*resp*/) {
            BOOST_LOG_TRIVIAL(info) << "[FilaCloudSync] push_update_to_cloud ok: " << spool_id;
        },
        [spool_id, fallback_create](int code, const std::string& err) {
            if (code == 404) {
                BOOST_LOG_TRIVIAL(info) << "[FilaCloudSync] push_update 404, fallback to create: " << spool_id;
                fallback_create();
            } else {
                BOOST_LOG_TRIVIAL(error) << "[FilaCloudSync] push_update_to_cloud failed ("
                                         << spool_id << "): " << code << err_body_tail(err);
            }
        });
}

void wgtFilaManagerCloudSync::push_delete_to_cloud(const std::vector<std::string>& spool_ids)
{
    if (spool_ids.empty()) return;

    // Cloud BatchDeleteFilamentV2Req uses int64 ids. Convert any numeric-looking
    // string ids; anything non-numeric is treated as an RFID fallback, matching
    // the other request field defined by the schema.
    nlohmann::json body;
    nlohmann::json id_array   = nlohmann::json::array();
    nlohmann::json rfid_array = nlohmann::json::array();
    for (const std::string& key : spool_ids) {
        if (key.empty()) continue;
        try {
            size_t pos = 0;
            int64_t n = std::stoll(key, &pos);
            if (pos == key.size()) {
                id_array.push_back(n);
                continue;
            }
        } catch (const std::exception&) {
        }
        rfid_array.push_back(key);
    }
    if (!id_array.empty())
        body["ids"]   = id_array;
    if (!rfid_array.empty())
        body["RFIDs"] = rfid_array;

    m_client->batch_delete(body,
        [](const nlohmann::json& /*resp*/) {
            BOOST_LOG_TRIVIAL(info) << "[FilaCloudSync] push_delete_to_cloud ok";
        },
        [](int code, const std::string& err) {
            BOOST_LOG_TRIVIAL(error) << "[FilaCloudSync] push_delete_to_cloud failed: " << code << err_body_tail(err);
        });
}

// ---------------------------------------------------------------------------
// Filament config
// ---------------------------------------------------------------------------

void wgtFilaManagerCloudSync::fetch_filament_config(
    std::function<void(const nlohmann::json&)> on_done)
{
    wxGetApp().emit_fila_debug_log("http", "info", "Cloud config request",
                                   "Requesting filament config from cloud",
                                   nlohmann::json::object());
    m_client->get_filament_config(
        [on_done](const nlohmann::json& data) {
            wxTheApp->CallAfter([on_done, data]() {
                if (on_done) on_done(data);
            });
        },
        [](int code, const std::string& err) {
            BOOST_LOG_TRIVIAL(error) << "[FilaCloudSync] fetch_filament_config failed: " << code << err_body_tail(err);
        });
}

}} // namespace Slic3r::GUI
