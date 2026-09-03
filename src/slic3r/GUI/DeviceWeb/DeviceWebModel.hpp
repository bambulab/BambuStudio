#ifndef DEVICEWEBMODEL_H
#define DEVICEWEBMODEL_H

#include <nlohmann/json.hpp>
#include <string>
#include <cstdint>

namespace Slic3r {
namespace GUI {

enum class MsgType { Request, Response, Report };

/**
 * Message header — the "envelope" wrapping every C++ <-> Web packet.
 *
 * Wire format (JSON):
 *   { "version": "1.0", "type": "request", "seq": 42, "ts": 1713300000000 }
 *
 * Fields:
 *   version  Protocol version string. Both sides must match.
 *   type     Message direction:
 *              "request"  — Web  -> C++  (frontend initiates a command)
 *              "response" — C++  -> Web  (reply to a specific request, matched by seq)
 *              "report"   — C++  -> Web  (unsolicited push, e.g. state update)
 *   seq      Sequence number. Assigned by the sender; responses echo the
 *            request's seq so the frontend can match Promise callbacks.
 *   ts       Timestamp in milliseconds since epoch. Informational only.
 */
struct Header
{
    std::string   version;
    MsgType       type{MsgType::Request};
    std::uint64_t seq{0};
    std::uint64_t ts{0};
};

inline void to_json(nlohmann::json& j, const Header& h)
{
    std::string type_str;
    switch (h.type) {
        case MsgType::Request:  type_str = "request";  break;
        case MsgType::Response: type_str = "response"; break;
        case MsgType::Report:   type_str = "report";   break;
        default: throw nlohmann::detail::other_error::create(501, "invalid MsgType value", nlohmann::json{});
    }
    j = nlohmann::json{
        {"version", h.version},
        {"type",    type_str},
        {"seq",     h.seq},
        {"ts",      h.ts}
    };
}

inline void from_json(const nlohmann::json& j, Header& h)
{
    j.at("version").get_to(h.version);
    j.at("seq").get_to(h.seq);
    j.at("ts").get_to(h.ts);

    std::string type_str;
    j.at("type").get_to(type_str);
    if (type_str == "response")       h.type = MsgType::Response;
    else if (type_str == "report")    h.type = MsgType::Report;
    else if (type_str == "request")   h.type = MsgType::Request;
    else throw nlohmann::detail::other_error::create(501, "unknown MsgType: " + type_str, j);
}

}} // namespace Slic3r::GUI

#endif // DEVICEWEBMODEL_H
