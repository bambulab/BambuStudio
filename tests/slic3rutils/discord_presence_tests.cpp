#include <catch2/catch.hpp>

#include <string>
#include <vector>

#include "slic3r/Utils/DiscordIPC.hpp"
#include "slic3r/Utils/DiscordPresence.hpp"

using namespace Slic3r;

TEST_CASE("Discord frame header is little-endian regardless of host", "[DiscordIPC]")
{
    const std::vector<char> frame = discord_encode_frame(DiscordOpcode::Frame, "ab");

    REQUIRE(frame.size() == 8 + 2);
    // opcode 1
    CHECK(static_cast<unsigned char>(frame[0]) == 0x01);
    CHECK(static_cast<unsigned char>(frame[1]) == 0x00);
    CHECK(static_cast<unsigned char>(frame[2]) == 0x00);
    CHECK(static_cast<unsigned char>(frame[3]) == 0x00);
    // length 2
    CHECK(static_cast<unsigned char>(frame[4]) == 0x02);
    CHECK(static_cast<unsigned char>(frame[5]) == 0x00);
    CHECK(static_cast<unsigned char>(frame[6]) == 0x00);
    CHECK(static_cast<unsigned char>(frame[7]) == 0x00);
    CHECK(frame[8] == 'a');
    CHECK(frame[9] == 'b');
}

TEST_CASE("Discord frame header round-trips", "[DiscordIPC]")
{
    const std::string payload(300, 'x');
    const std::vector<char> frame = discord_encode_frame(DiscordOpcode::Handshake, payload);

    DiscordOpcode opcode = DiscordOpcode::Pong;
    uint32_t      length = 0;
    REQUIRE(discord_decode_header(frame.data(), frame.size(), opcode, length));
    CHECK(opcode == DiscordOpcode::Handshake);
    CHECK(length == payload.size());
}

TEST_CASE("Discord header decoding rejects bad input", "[DiscordIPC]")
{
    DiscordOpcode opcode = DiscordOpcode::Frame;
    uint32_t      length = 0;

    SECTION("short buffer") {
        const char buf[4] = { 0, 0, 0, 0 };
        CHECK_FALSE(discord_decode_header(buf, sizeof(buf), opcode, length));
    }

    SECTION("null buffer") {
        CHECK_FALSE(discord_decode_header(nullptr, 8, opcode, length));
    }

    SECTION("unknown opcode") {
        const unsigned char buf[8] = { 0x63, 0, 0, 0, 0, 0, 0, 0 };
        CHECK_FALSE(discord_decode_header(reinterpret_cast<const char *>(buf), sizeof(buf), opcode, length));
    }

    SECTION("implausible length is refused rather than allocated") {
        const unsigned char buf[8] = { 0x01, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF };
        CHECK_FALSE(discord_decode_header(reinterpret_cast<const char *>(buf), sizeof(buf), opcode, length));
    }
}

TEST_CASE("Endpoint probing covers all ten sockets", "[DiscordIPC]")
{
    const std::vector<std::string> endpoints = discord_candidate_endpoints();
    REQUIRE_FALSE(endpoints.empty());

    for (int i = 0; i < 10; ++i) {
        const std::string suffix = "discord-ipc-" + std::to_string(i);
        bool found = false;
        for (const std::string &endpoint : endpoints)
            if (endpoint.size() >= suffix.size() && endpoint.compare(endpoint.size() - suffix.size(), suffix.size(), suffix) == 0) {
                found = true;
                break;
            }
        CHECK(found);
    }
}

TEST_CASE("UTF-8 truncation never splits a codepoint", "[DiscordIPC]")
{
    SECTION("short input is returned unchanged") {
        CHECK(discord_truncate_utf8("hello", 128) == "hello");
    }

    SECTION("multibyte input stays valid UTF-8") {
        // 3 bytes per character.
        std::string text;
        for (int i = 0; i < 100; ++i)
            text += "\xE6\xB5\x8B";

        const std::string cut = discord_truncate_utf8(text, 128);
        REQUIRE(cut.size() <= 128);

        size_t i = 0;
        while (i < cut.size()) {
            const unsigned char c = static_cast<unsigned char>(cut[i]);
            const size_t len = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : 4;
            REQUIRE(i + len <= cut.size());
            i += len;
        }
        CHECK(i == cut.size());
    }

    SECTION("truncated output is marked with an ellipsis") {
        const std::string cut = discord_truncate_utf8(std::string(200, 'a'), 128);
        CHECK(cut.size() <= 128);
        CHECK(cut.substr(cut.size() - 3) == "\xE2\x80\xA6");
    }
}

static PresenceSnapshot printing_snapshot()
{
    PresenceSnapshot snap;
    snap.activity   = PresenceSnapshot::Activity::Printing;
    snap.details    = "benchy.3mf";
    snap.state      = "Printing - 47%";
    snap.small_text = "P1S";
    snap.end_time   = 1756400000;
    return snap;
}

TEST_CASE("Activity payload carries the expected fields", "[DiscordPresence]")
{
    const nlohmann::json activity = discord_build_activity(printing_snapshot(), "Bambu Studio 1.0");

    CHECK(activity["details"] == "benchy.3mf");
    CHECK(activity["state"] == "Printing - 47%");
    CHECK(activity["timestamps"]["end"] == 1756400000);
    CHECK(activity["assets"]["large_image"] == "bambu_studio");
    CHECK(activity["assets"]["large_text"] == "Bambu Studio 1.0");
    CHECK(activity["assets"]["small_image"] == "state_printing");
    CHECK(activity["assets"]["small_text"] == "P1S");
}

TEST_CASE("Empty activity fields are omitted rather than sent blank", "[DiscordPresence]")
{
    PresenceSnapshot snap;
    snap.activity = PresenceSnapshot::Activity::Idle;
    snap.state    = "Idle";

    const nlohmann::json activity = discord_build_activity(snap, "Bambu Studio 1.0");

    CHECK(activity.find("details") == activity.end());
    CHECK(activity["state"] == "Idle");
    CHECK(activity.find("timestamps") == activity.end());
    CHECK(activity["assets"].find("small_text") == activity["assets"].end());
}

TEST_CASE("A countdown takes precedence over an elapsed timer", "[DiscordPresence]")
{
    PresenceSnapshot snap = printing_snapshot();
    snap.start_time = 1756300000;

    const nlohmann::json activity = discord_build_activity(snap, std::string());
    CHECK(activity["timestamps"]["end"] == 1756400000);
    CHECK(activity["timestamps"].find("start") == activity["timestamps"].end());
}

TEST_CASE("Each activity maps to its own state badge", "[DiscordPresence]")
{
    CHECK(std::string(discord_state_asset_key(PresenceSnapshot::Activity::Printing)) == "state_printing");
    CHECK(std::string(discord_state_asset_key(PresenceSnapshot::Activity::Paused)) == "state_paused");
    CHECK(std::string(discord_state_asset_key(PresenceSnapshot::Activity::Failed)) == "state_error");
    CHECK(std::string(discord_state_asset_key(PresenceSnapshot::Activity::Finished)) == "state_done");
    CHECK(std::string(discord_state_asset_key(PresenceSnapshot::Activity::Slicing)) == "state_slicing");
    CHECK(std::string(discord_state_asset_key(PresenceSnapshot::Activity::Editing)) == "state_idle");
}

TEST_CASE("Over-long fields are truncated before they reach Discord", "[DiscordPresence]")
{
    PresenceSnapshot snap;
    snap.details = std::string(400, 'a');
    snap.state   = std::string(400, 'b');

    const nlohmann::json activity = discord_build_activity(snap, std::string());
    CHECK(activity["details"].get<std::string>().size() <= 128);
    CHECK(activity["state"].get<std::string>().size() <= 128);
}

TEST_CASE("SET_ACTIVITY command is well formed", "[DiscordPresence]")
{
    const nlohmann::json activity = discord_build_activity(printing_snapshot(), std::string());
    const nlohmann::json command  = nlohmann::json::parse(discord_build_set_activity(activity, 4242, 7));

    CHECK(command["cmd"] == "SET_ACTIVITY");
    CHECK(command["nonce"] == "bambu-7");
    CHECK(command["args"]["pid"] == 4242);
    CHECK(command["args"]["activity"]["details"] == "benchy.3mf");
}

TEST_CASE("Clearing the presence sends a null activity", "[DiscordPresence]")
{
    const nlohmann::json command = nlohmann::json::parse(discord_build_set_activity(nlohmann::json(nullptr), 1, 1));
    CHECK(command["args"]["activity"].is_null());
}

TEST_CASE("A drifting time estimate is not treated as a change", "[DiscordPresence]")
{
    const PresenceSnapshot a = printing_snapshot();

    PresenceSnapshot b = a;
    b.end_time += 10;
    CHECK(a.equivalent(b));
    CHECK(a != b);

    PresenceSnapshot c = a;
    c.end_time += 600;
    CHECK_FALSE(a.equivalent(c));
}

TEST_CASE("Unchanged snapshots are not resent", "[DiscordPresence]")
{
    PresenceThrottle       throttle;
    const PresenceSnapshot snap = printing_snapshot();

    REQUIRE(throttle.should_send(snap, 0));
    throttle.record_sent(snap, 0);

    CHECK_FALSE(throttle.should_send(snap, 60'000));
}

TEST_CASE("A change inside the rate limit window is held, not dropped", "[DiscordPresence]")
{
    PresenceThrottle throttle;
    const PresenceSnapshot first = printing_snapshot();
    throttle.record_sent(first, 0);

    PresenceSnapshot second = first;
    second.state = "Printing - 48%";

    CHECK_FALSE(throttle.should_send(second, 1000));
    CHECK_FALSE(throttle.should_send(second, PresenceThrottle::MIN_SEND_INTERVAL_MS - 1));
    CHECK(throttle.should_send(second, PresenceThrottle::MIN_SEND_INTERVAL_MS));
}

TEST_CASE("The first snapshot is always sent", "[DiscordPresence]")
{
    PresenceThrottle throttle;
    CHECK(throttle.should_send(printing_snapshot(), 0));
}

TEST_CASE("Reset makes the next snapshot send again", "[DiscordPresence]")
{
    PresenceThrottle       throttle;
    const PresenceSnapshot snap = printing_snapshot();
    throttle.record_sent(snap, 0);
    REQUIRE_FALSE(throttle.should_send(snap, 60'000));

    throttle.reset();
    CHECK(throttle.should_send(snap, 60'000));
}

TEST_CASE("Presence stays disabled without an application id", "[DiscordPresence]")
{
    DiscordPresence presence("", "Bambu Studio");
    presence.set_enabled(true);
    CHECK_FALSE(presence.is_enabled());
}
