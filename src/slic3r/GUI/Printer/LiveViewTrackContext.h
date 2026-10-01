#ifndef _LIVE_VIEW_TRACK_CONTEXT_H_
#define _LIVE_VIEW_TRACK_CONTEXT_H_

#include <cstdint>
#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

namespace BambuLiveViewTrack {

inline constexpr const char* EVENT_VERSION = "1.0";

struct EmitParams {
    std::string module;
    std::string phase;
    std::string result;
    std::string error_code;
    std::string error_message;
    nlohmann::json event_data = nlohmann::json::object();
};

struct ClientInfo {
    std::string client_ver;
    std::string client_id;
    std::string platform;
    std::string os_version;
    std::string network_type;
    std::string region;
};

struct SessionInfo {
    std::string session_id;
    std::string trace_id;
    std::string service_type;
    int64_t     start_time_ms = 0;
};

struct ChannelInfo {
    std::string protocol;
    std::string channel;
    std::string local_uid;
    std::string remote_uid;
    std::string region;
};

namespace detail {
    inline bool present(const char* s) { return s != nullptr && *s != '\0'; }

    inline void put_str(nlohmann::json& obj, const char* key, const char* value) {
        if (present(value)) obj[key] = value;
    }

    inline void put_str(nlohmann::json& obj, const char* key, const std::string& value) {
        if (!value.empty()) obj[key] = value;
    }
}

inline nlohmann::json build_envelope_json(
    const char*        event_name,
    const ClientInfo&  client,
    const SessionInfo& session,
    const ChannelInfo* channel,
    const EmitParams&  params)
{
    using nlohmann::json;
    using detail::put_str;

    json env = json::object();
    env["event_name"]    = event_name;
    env["event_version"] = EVENT_VERSION;

    json client_obj = json::object();
    put_str(client_obj, "client_ver",   client.client_ver);
    put_str(client_obj, "client_id",    client.client_id);
    put_str(client_obj, "platform",     client.platform);
    put_str(client_obj, "os_version",   client.os_version);
    put_str(client_obj, "network_type", client.network_type);
    const std::string& region = (channel && !channel->region.empty()) ? channel->region : client.region;
    put_str(client_obj, "region", region);
    if (!client_obj.empty()) env["client"] = std::move(client_obj);

    json session_obj = json::object();
    put_str(session_obj, "session_id",   session.session_id);
    put_str(session_obj, "trace_id",     session.trace_id);
    put_str(session_obj, "service_type", session.service_type);
    if (channel) put_str(session_obj, "protocol", channel->protocol);
    if (session.start_time_ms != 0) session_obj["start_time_ms"] = session.start_time_ms;
    if (!session_obj.empty()) env["session"] = std::move(session_obj);

    if (channel) {
        json ch = json::object();
        put_str(ch, "channel",    channel->channel);
        put_str(ch, "local_uid",  channel->local_uid);
        put_str(ch, "remote_uid", channel->remote_uid);
        if (!ch.empty()) env["channel_info"] = std::move(ch);
    }

    json context = json::object();
    context["source"] = "client";
    put_str(context, "module",        params.module);
    put_str(context, "phase",         params.phase);
    put_str(context, "result",        params.result);
    put_str(context, "error_code",    params.error_code);
    put_str(context, "error_message", params.error_message);
    env["context"] = std::move(context);

    env["event_data"] = params.event_data;
    return env;
}

class LiveViewTrackContext {
public:
    static LiveViewTrackContext& instance();

    void init_client(const ClientInfo& info);
    void init_session(const SessionInfo& info);

    void emit(const char* event_name,
              const EmitParams& params,
              const ChannelInfo* channel = nullptr);

private:
    LiveViewTrackContext() = default;
    LiveViewTrackContext(const LiveViewTrackContext&) = delete;
    LiveViewTrackContext& operator=(const LiveViewTrackContext&) = delete;

    static void sink_emit(const char* event_name,
                          const std::string& envelope_json);

    mutable std::mutex  mutex_;
    ClientInfo          client_;
    SessionInfo         session_;
};

}  // namespace BambuLiveViewTrack

#endif  // _LIVE_VIEW_TRACK_CONTEXT_H_
