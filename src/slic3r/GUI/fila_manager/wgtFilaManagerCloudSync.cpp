#include "wgtFilaManagerCloudSync.h"
#include "wgtFilaManagerCloudClient.h"
#include "wgtFilaManagerCloudDispatcher.h"
#include "wgtFilaManagerColorType.h"
#include "wgtFilaManagerStore.h"

#include "slic3r/Utils/NetworkAgent.hpp"
#include "slic3r/GUI/GUI_App.hpp"

#include <wx/app.h>
#include <boost/log/trivial.hpp>
#include <chrono>
#include <cmath>
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
    // STUDIO-17959: weight fields go through the double-precision parser so a
    // cloud-provided netWeight / totalNetWeight close to MAX_NET_WEIGHT_GRAMS
    // (999_999_999) survives the JSON -> in-memory hop intact. Going through
    // num_f_any here used to snap 999_999_999 -> 1.0e9 at v.get<float>().
    auto num_d_any = [&](std::initializer_list<const char*> keys, double def = 0.0) -> double {
        for (const char* key : keys) {
            if (!j.contains(key)) continue;
            const auto& v = j[key];
            if (v.is_null()) continue;
            if (v.is_number()) return v.get<double>();
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
    s.tray_id_name  = str_any({"trayIdName", "tray_id_name"});
    if (!FilamentSpool::is_valid_tag_uid(s.tag_uid))
        s.tag_uid.clear();

    // Descriptive fields.
    s.brand         = str_any({"filamentVendor", "brand"});
    s.material_type = str_any({"filamentType", "material_type"});
    s.series        = str_any({"filamentName", "series"});
    s.color_code    = str_any({"color", "color_code"});
    s.color_name    = str_any({"color_name"}); // cloud has no direct counterpart

    // STUDIO-17977 (pull-side): cloud carries `colorType` (int 0/1/2) and
    // `colors` (string[]) when the spool was pushed up as a multicolor /
    // gradient entry — `spool_to_cloud_json` below has been emitting them
    // since 17977's first patch. Without these reads here a `pull_done`
    // would silently overwrite a local multicolor spool's `colors` and
    // `color_type` with the schema defaults (empty + 2), which downstream
    // breaks SpoolTable row tail's reverse-lookup of the official BBL
    // candidate (e.g. "马卡龙 / 13906") and leaves the row with no colour
    // name. Round-trip the two fields here so cloud-stored multicolor
    // data survives a sync.
    const int raw_color_type = num_i_any({"colorType", "color_type"}, 2);
    if (j.contains("colors") && j["colors"].is_array()) {
        for (const auto& hex : j["colors"]) {
            if (hex.is_string()) s.colors.push_back(hex.get<std::string>());
        }
    }
    s.color_type = to_fila_manager_color_type_int(
        normalize_fila_manager_color_type(raw_color_type, s.colors.size()));

    // STUDIO-18355 (defensive): align with FilamentSpool::from_json's invariant
    // (`!colors.empty() ⇒ color_code == colors.front()`). If the cloud ever
    // returns a record where the top-level `color` and `colors[0]` disagree
    // (e.g. a stale record from before the PUT-side guard above was rolled
    // out), the pull merge would otherwise leak the disagreement into the
    // local store and SpoolColorChip would render `colors[0]`, not `color`.
    // Snap to `colors.front()` to mirror the disk-load path so a restart and
    // a fresh pull surface the same colour.
    if (!s.colors.empty() && s.color_code != s.colors.front()) {
        s.color_code = s.colors.front();
    }

    s.diameter        = num_f_any({"diameter"}, 1.75f);

    // Weights: cloud uses totalNetWeight / netWeight (nullable int64 grams).
    // STUDIO-17959: parse as double so values up to 999_999_999 don't lose
    // precision (float32 would snap them to 1.0e9 here).
    s.initial_weight = num_d_any({"totalNetWeight", "initial_weight"});
    s.net_weight     = num_d_any({"netWeight", "net_weight"});
    s.spool_weight   = num_d_any({"spool_weight"});

    // remain_percent is not provided by cloud; derive from weights when we can,
    // otherwise honor any locally-provided value (+0.5 for simple rounding).
    if (s.initial_weight > 0.0 && s.net_weight >= 0.0) {
        double pct = s.net_weight / s.initial_weight * 100.0;
        if (pct < 0.0)   pct = 0.0;
        if (pct > 100.0) pct = 100.0;
        s.remain_percent = static_cast<int>(pct + 0.5);
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
    s.bound_ams_id = str_any({"bound_ams_id"});
    s.note         = str_any({"note"});

    s.favorite        = j.contains("favorite") && j["favorite"].is_boolean() && j["favorite"].get<bool>();

    return s;
}

nlohmann::json wgtFilaManagerCloudSync::spool_to_cloud_json(const FilamentSpool& s)
{
    // Cloud CreateFilamentV2Req / UpdateFilamentV2Req schema (camelCase).
    nlohmann::json j;

    const bool has_valid_rfid = FilamentSpool::is_valid_tag_uid(s.tag_uid);
    // Cloud createType tracks whether this spool has a real RFID, not which UI
    // flow created it. Non-RFID AMS reads use placeholder tags and must stay
    // manual to avoid cloud-side AMS/RFID semantics.
    std::string create_type = has_valid_rfid ? "ams" : "manual";
    j["createType"]     = create_type;

    j["filamentVendor"] = s.brand;
    j["filamentType"]   = s.material_type;
    // Swagger marks filamentName / colorType as required. Manual add currently
    // models only a single-colour spool and may leave `series` empty, so fall
    // back to the material type for the display name and encode monochrome as 2.
    j["filamentName"]   = s.series.empty() ? s.material_type : s.series;
    j["filamentId"]     = s.setting_id;
    j["isSupport"]      = false; // local schema has no equivalent yet
    if (has_valid_rfid)
        j["RFID"]       = s.tag_uid;
    j["color"]          = s.color_code;
    j["colorType"]      = to_fila_manager_color_type_int(
        normalize_fila_manager_color_type(s.color_type, s.colors.size())); // STUDIO-17977: was hardcoded 2
    if (!s.colors.empty())                       // STUDIO-17977: surface colors[] when multicolor
        j["colors"]     = s.colors;
    if (has_valid_rfid && !s.tray_id_name.empty()) {
        j["trayIdName"] = s.tray_id_name;
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
    const double material_weight = s.net_weight > 0.0
        ? s.net_weight
        : ((s.initial_weight > s.spool_weight) ? (s.initial_weight - s.spool_weight) : 0.0);
    const double total_net_weight = (s.spool_weight > 0.0 && s.initial_weight > s.spool_weight)
        ? (s.initial_weight - s.spool_weight)
        : s.initial_weight;
    if (material_weight > 0.0)
        j["netWeight"] = static_cast<int64_t>(material_weight + 0.5);
    if (total_net_weight > 0.0)
        j["totalNetWeight"] = static_cast<int64_t>(total_net_weight + 0.5);

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
    // STUDIO-17977: colors[] is now a first-class local field; pass it through
    // when the patch carries an explicit colors array.
    if (p.contains("colors") && p.at("colors").is_array())
        j["colors"] = p.at("colors");
    // STUDIO-18355: cloud `[]string optional` treats an empty array as
    // "field not provided" and silently keeps the previously stored colors.
    // Combined with FilamentSpool's `color_code == colors[0]` invariant on
    // the pull side that meant a single→single colour edit (frontend ships
    // `{color_code: <new>, colors: []}` per AddEditDialog handleSubmit /
    // STUDIO-18340) round-tripped to a stale colours[] on the next pull and
    // the SpoolColorChip rendered the OLD hex.
    //
    // Whenever the cloud body carries `color`, ensure `colors` is non-empty
    // and that its primary entry equals `color`. That keeps the cloud
    // record self-consistent without changing the local "single = empty
    // colors[]" canonical shape (fila_manager_tests_main.cpp / apply_patch
    // / on-disk JSON keep emitting empty colors[] for single).
    if (j.contains("color") && j.at("color").is_string()) {
        const std::string primary = j["color"].get<std::string>();
        const bool colors_missing_or_empty = !j.contains("colors")
            || !j["colors"].is_array() || j["colors"].empty();
        if (colors_missing_or_empty) {
            j["colors"] = nlohmann::json::array({primary});
        } else if (j["colors"].size() == 1 && j["colors"][0].is_string()
                   && j["colors"][0].get<std::string>() != primary) {
            // Single-element colors[] disagreeing with `color` is the same
            // class of inconsistency; align to the user-confirmed primary.
            // Multi-element colors[] is left untouched — that came from a
            // multicolor edit and `color` is just the primary preview hex.
            j["colors"] = nlohmann::json::array({primary});
        }
    }
    if (p.contains("color_type") && p.at("color_type").is_number()) {
        const std::size_t color_count = j.contains("colors") && j["colors"].is_array() ? j["colors"].size() : 1;
        j["colorType"] = to_fila_manager_color_type_int(
            normalize_fila_manager_color_type(p.at("color_type").get<int>(), color_count));
    }
    // NOTE on STUDIO-18355: deliberately do NOT auto-fill `colorType` when
    // the patch is silent about it. The bug only manifests on the user-driven
    // single→single edit path, which always sets `color_type` (AddEditDialog
    // handleSubmit / commitCustomColorSelection). Synthesising `colorType`
    // for partial patches that only touch the primary hex would risk
    // collapsing a multicolor cloud row to "single" without user intent.
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
// STUDIO-18155 / openspec 20260506耗材管理器AMS自动同步云端
//
// AMS 同步完成本地写入 → 节流 → 云端 PUT。
// 详细决策见 design § 1 数据流 / § 2 throttle / § 3 sync 改造。
// ---------------------------------------------------------------------------

namespace {

// 把 throttle 决策转成统计字段名，方便摘要聚合。
struct AutoPushTally {
    int pushed           = 0;
    int skipped_cooldown = 0;
    int skipped_no_diff  = 0;
    int skipped_no_rfid  = 0;
};

const char* device_state_label(AmsAutoPushThrottle::DeviceState s)
{
    return s == AmsAutoPushThrottle::DeviceState::Busy ? "busy" : "idle";
}

} // namespace

void wgtFilaManagerCloudSync::notify_ams_synced(
    const std::vector<AmsChangedSpool>&     changed,
    AmsAutoPushThrottle::DeviceState        device_state)
{
    if (changed.empty()) return;

    // STUDIO-18155 follow-up：未登录 / LAN-only 模式下不能进 throttle.record_success，
    // 否则 cooldown 表会被锁死，等用户后续登录第一波同步会全部 SkipNoDiff，必须等
    // AMS 余量再次变化才会真正推一次到云端。与 request_user_logout 中
    // throttle().clear_all() 形成对称防御。
    NetworkAgent* agent = wxGetApp().getAgent();
    if (!agent || !agent->is_user_login()) return;

    auto* disp = wxGetApp().fila_manager_cloud_disp();
    if (!disp) {
        BOOST_LOG_TRIVIAL(warning)
            << "[FilaCloudSync] notify_ams_synced: dispatcher unavailable, skip";
        return;
    }

    const auto    now = std::chrono::steady_clock::now();
    AutoPushTally tally;

    for (const auto& item : changed) {
        if (!FilamentSpool::is_valid_tag_uid(item.tag_uid)) {
            ++tally.skipped_no_rfid;
            continue;
        }
        const auto decision = m_throttle.evaluate(
            item.tag_uid, item.net_weight, device_state, now);

        switch (decision) {
        case AmsAutoPushThrottle::Decision::Push: {
            // PUT body 只放 net_weight，其余字段留给 spool_to_cloud_update_json
            // 兜底（filamentName 防 STUDIO-18117 重现）。其余 sync 关心字段
            // (status / bound_*) 云端 PUT 不接受 → 不放进 patch。
            nlohmann::json patch = {
                {"net_weight", static_cast<double>(item.net_weight)}
            };
            disp->enqueue_push_update(item.spool_id, patch);
            // 乐观 record：先记 cooldown 起点。设计权衡：失败时下次 sync 仍
            // 等 10 min cooldown 才重试，把"网络抽风时无意义请求"的攻击面
            // 关掉。用户着急可以用"推送本地到云端"按钮绕过 throttle。
            m_throttle.record_success(item.tag_uid, item.net_weight, now);
            ++tally.pushed;
            break;
        }
        case AmsAutoPushThrottle::Decision::SkipCooldown:
            ++tally.skipped_cooldown; break;
        case AmsAutoPushThrottle::Decision::SkipNoDiff:
            ++tally.skipped_no_diff;  break;
        case AmsAutoPushThrottle::Decision::SkipNoRfid:
            ++tally.skipped_no_rfid;  break;
        }
    }

    BOOST_LOG_TRIVIAL(info)
        << "[FilaCloudSync] auto_push device_state=" << device_state_label(device_state)
        << " pushed=" << tally.pushed
        << " skipped_cooldown=" << tally.skipped_cooldown
        << " skipped_no_diff="  << tally.skipped_no_diff
        << " skipped_no_rfid="  << tally.skipped_no_rfid;

    // Q4 决策：仅在实际产生 push 时刷前端摘要；全 skipped 不通知
    if (tally.pushed > 0 && m_on_auto_push_summary) {
        m_on_auto_push_summary({
            {"trigger",          "auto"},
            {"device_state",     device_state_label(device_state)},
            {"pushed",           tally.pushed},
            {"skipped_cooldown", tally.skipped_cooldown},
            {"skipped_no_diff",  tally.skipped_no_diff},
            {"skipped_no_rfid",  tally.skipped_no_rfid},
        });
    }
}

void wgtFilaManagerCloudSync::push_all_now()
{
    auto* disp = wxGetApp().fila_manager_cloud_disp();
    if (!disp) {
        BOOST_LOG_TRIVIAL(warning)
            << "[FilaCloudSync] push_all_now: dispatcher unavailable, skip";
        return;
    }

    const auto now      = std::chrono::steady_clock::now();
    int        enqueued = 0;
    int        skipped_no_rfid       = 0;
    int        skipped_no_total_nw   = 0;

    for (const auto& spool_id : m_store->all_spool_ids()) {
        const FilamentSpool* sp = m_store->get_spool(spool_id);
        if (!sp) continue;

        if (!FilamentSpool::is_valid_tag_uid(sp->tag_uid)) {
            ++skipped_no_rfid;
            continue;
        }
        if (sp->effective_total_net_weight() <= 0.0) {
            ++skipped_no_total_nw;
            continue;
        }

        const int64_t nw = static_cast<int64_t>(std::round(sp->net_weight));
        nlohmann::json patch = {
            {"net_weight", static_cast<double>(nw)}
        };
        disp->enqueue_push_update(spool_id, patch);
        // 绕过 throttle.evaluate，但仍 record_success：保持 cooldown 状态
        // 一致，避免手动按钮触发后下一次 AMS sync 又重复推一遍。
        m_throttle.record_success(sp->tag_uid, nw, now);
        ++enqueued;
    }

    BOOST_LOG_TRIVIAL(info)
        << "[FilaCloudSync] push_all_now enqueued=" << enqueued
        << " skipped_no_rfid="     << skipped_no_rfid
        << " skipped_no_total_nw=" << skipped_no_total_nw;

    // 手动按钮：summary **总是**发送（含 enqueued=0），让 toast 能显示"推送 0 卷"
    if (m_on_auto_push_summary) {
        m_on_auto_push_summary({
            {"trigger",             "manual"},
            {"device_state",        "manual"},
            {"pushed",              enqueued},
            {"skipped_cooldown",    0},
            {"skipped_no_diff",     0},
            {"skipped_no_rfid",     skipped_no_rfid},
            {"skipped_no_total_nw", skipped_no_total_nw},
        });
    }
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
