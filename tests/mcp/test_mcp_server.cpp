#include "slic3r/Utils/McpServer.hpp"
#include "slic3r/GUI/McpCredentials.hpp"
#include "slic3r/GUI/McpGcodeExport.hpp"
#include <filesystem>
#include <fstream>

#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <boost/asio.hpp>
#include <boost/beast.hpp>

namespace net = boost::asio;
namespace http = boost::beast::http;
using tcp = net::ip::tcp;
using Json = nlohmann::json;
using Response = http::response<http::string_body>;
using namespace std::chrono_literals;

namespace {
std::atomic<int> checks{0};
void check(bool condition, const char* expression, int line)
{
    ++checks;
    if (!condition) throw std::runtime_error(std::string("Line ") + std::to_string(line) + ": " + expression);
}
#define CHECK(expression) check(bool(expression), #expression, __LINE__)
const std::string token(64, 'a');

std::uint16_t free_port()
{
    net::io_context io;
    tcp::acceptor acceptor(io, tcp::endpoint(net::ip::make_address("127.0.0.1"), 0));
    return acceptor.local_endpoint().port();
}

Json modern_request(std::string method, Json params = Json::object(), int id = 1)
{
    params["_meta"] = {{"io.modelcontextprotocol/protocolVersion", "2026-07-28"},
                       {"io.modelcontextprotocol/clientCapabilities", Json::object()}};
    return {{"jsonrpc", "2.0"}, {"id", id}, {"method", std::move(method)}, {"params", std::move(params)}};
}

http::request<http::string_body> request(std::uint16_t port, const Json& body)
{
    http::request<http::string_body> req(http::verb::post, "/mcp", 11);
    req.set(http::field::host, "127.0.0.1:" + std::to_string(port));
    req.set(http::field::authorization, "Bearer " + token);
    req.set(http::field::content_type, "application/json");
    req.set(http::field::accept, "application/json, text/event-stream");
    if (body.is_object() && body.contains("params") && body["params"].is_object() && body["params"].contains("_meta")) {
        req.set("MCP-Protocol-Version", "2026-07-28");
        req.set("Mcp-Method", body.value("method", ""));
        if (body["params"].contains("name")) req.set("Mcp-Name", body["params"]["name"].get<std::string>());
    } else if (!body.is_object() || body.value("method", "") != "initialize") req.set("MCP-Protocol-Version", "2025-11-25");
    req.body() = body.dump();
    req.prepare_payload();
    return req;
}

Response http_exchange(std::uint16_t port, const http::request<http::string_body>& req)
{
    net::io_context io;
    tcp::socket socket(io);
    socket.connect(tcp::endpoint(net::ip::make_address("127.0.0.1"), port));
    http::write(socket, req);
    boost::beast::flat_buffer buffer;
    Response response;
    http::read(socket, buffer, response);
    return response;
}

Json json(const Response& response) { return Json::parse(response.body()); }

void protocol_tests()
{
    Slic3r::McpServer server;
    auto port = free_port();
    std::string error;
    std::atomic<int> calls{0};
    auto handler = [&](const std::string& method, const Json& params, auto ticket, auto complete) {
        CHECK(ticket->try_start());
        ++calls;
        if (params.value("name", "") == "invalid") complete({{"error", {{"code", -32602}, {"message", "Unknown tool"}}}});
        else if (method == "tools/list") complete({{"tools", Json::array()}});
        else complete({{"content", {{{"type", "text"}, {"text", "ok"}}}}, {"isError", false}});
    };
    CHECK(!server.start(80, token, handler, error));
    CHECK(!server.start(port, "short", handler, error));
    CHECK(server.start(port, token, handler, error));
    CHECK(server.is_running());
    CHECK(!server.start(port, token, handler, error));
    Slic3r::McpServer conflict;
    CHECK(!conflict.start(port, token, handler, error));

    auto discovery = http_exchange(port, request(port, modern_request("server/discover")));
    CHECK(discovery.result_int() == 200);
    CHECK(json(discovery)["result"]["supportedVersions"][0] == "2026-07-28");
    CHECK(json(discovery)["result"]["resultType"] == "complete");
    CHECK(!discovery.count("Mcp-Session-Id"));
    auto listed = http_exchange(port, request(port, modern_request("tools/list")));
    CHECK(listed.result_int() == 200);
    CHECK(json(listed)["result"]["tools"].is_array());
    auto call = modern_request("tools/call", {{"name", "scene.get"}, {"arguments", Json::object()}});
    auto called = http_exchange(port, request(port, call));
    CHECK(called.result_int() == 200);
    CHECK(json(called)["result"]["content"][0]["text"] == "ok");
    auto encoded = request(port, call);
    encoded.set("Mcp-Name", "=?base64?c2NlbmUuZ2V0?=");
    CHECK(http_exchange(port, encoded).result_int() == 200);
    encoded.set("Mcp-Name", "=?base64?!!!!?=");
    CHECK(json(http_exchange(port, encoded))["error"]["code"] == -32020);

    Json init = {{"jsonrpc", "2.0"}, {"id", "init"}, {"method", "initialize"},
                 {"params", {{"protocolVersion", "2025-11-25"}, {"capabilities", Json::object()},
                             {"clientInfo", {{"name", "test"}, {"version", "1"}}}}}};
    auto initialized = http_exchange(port, request(port, init));
    CHECK(initialized.result_int() == 200);
    CHECK(json(initialized)["result"]["protocolVersion"] == "2025-11-25");
    CHECK(!json(initialized)["result"].contains("resultType"));
    auto notification = request(port, {{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}});
    auto notified = http_exchange(port, notification);
    CHECK(notified.result_int() == 202);
    CHECK(notified.body().empty());
    CHECK(http_exchange(port, request(port, {{"jsonrpc", "2.0"}, {"id", 4}, {"method", "tools/list"}})).result_int() == 200);
    CHECK(http_exchange(port, request(port, {{"jsonrpc", "2.0"}, {"id", 5}, {"method", "ping"}})).result_int() == 200);

    auto invalid_tool = http_exchange(port, request(port, modern_request("tools/call", {{"name", "invalid"}})));
    CHECK(invalid_tool.result_int() == 400);
    CHECK(json(invalid_tool)["error"]["code"] == -32602);
    CHECK(!json(invalid_tool).contains("result"));
    const auto baseline = calls.load();
    auto unauthorized = request(port, call);
    unauthorized.erase(http::field::authorization);
    CHECK(http_exchange(port, unauthorized).result_int() == 401);
    unauthorized.set(http::field::authorization, "Bearer " + std::string(64, 'b'));
    CHECK(http_exchange(port, unauthorized).result_int() == 401);
    unauthorized = request(port, call);
    unauthorized.insert(http::field::authorization, "Bearer " + token);
    CHECK(http_exchange(port, unauthorized).result_int() == 401);
    auto host = request(port, call);
    host.set(http::field::host, "evil.example:" + std::to_string(port));
    CHECK(http_exchange(port, host).result_int() == 403);
    host = request(port, call);
    host.insert(http::field::host, "127.0.0.1:" + std::to_string(port));
    CHECK(http_exchange(port, host).result_int() == 403);
    auto origin = request(port, call);
    origin.set(http::field::origin, "https://evil.example");
    CHECK(http_exchange(port, origin).result_int() == 403);
    origin.set(http::field::origin, "null");
    CHECK(http_exchange(port, origin).result_int() == 403);
    origin.set(http::field::origin, "");
    CHECK(http_exchange(port, origin).result_int() == 403);
    auto get = request(port, call);
    get.method(http::verb::get);
    CHECK(http_exchange(port, get).result_int() == 405);
    get.method(http::verb::delete_);
    CHECK(http_exchange(port, get).result_int() == 405);
    auto path = request(port, call);
    path.target("/mcp?token=anything");
    CHECK(http_exchange(port, path).result_int() == 404);
    auto media = request(port, call);
    media.set(http::field::content_type, "text/plain");
    CHECK(http_exchange(port, media).result_int() == 415);
    media = request(port, call);
    media.set(http::field::accept, "application/json");
    CHECK(http_exchange(port, media).result_int() == 406);
    media.set(http::field::accept, "application/json, text/event-stream;q=0");
    CHECK(http_exchange(port, media).result_int() == 406);
    auto mismatch = request(port, call);
    mismatch.set("Mcp-Method", "tools/list");
    CHECK(json(http_exchange(port, mismatch))["error"]["code"] == -32020);
    mismatch = request(port, call);
    mismatch.erase("Mcp-Name");
    CHECK(json(http_exchange(port, mismatch))["error"]["code"] == -32020);
    mismatch = request(port, call);
    mismatch.erase("MCP-Protocol-Version");
    CHECK(json(http_exchange(port, mismatch))["error"]["code"] == -32020);
    auto missing_meta = call;
    missing_meta["params"]["_meta"].erase("io.modelcontextprotocol/clientCapabilities");
    CHECK(json(http_exchange(port, request(port, missing_meta)))["error"]["code"] == -32602);
    auto wrong_version = call;
    wrong_version["params"]["_meta"]["io.modelcontextprotocol/protocolVersion"] = "2099-01-01";
    auto wrong_version_req = request(port, wrong_version);
    wrong_version_req.set("MCP-Protocol-Version", "2099-01-01");
    CHECK(json(http_exchange(port, wrong_version_req))["error"]["code"] == -32022);
    CHECK(http_exchange(port, request(port, modern_request("unknown"))).result_int() == 404);
    auto invalid_args = call;
    invalid_args["params"]["arguments"] = Json::array();
    CHECK(json(http_exchange(port, request(port, invalid_args)))["error"]["code"] == -32602);
    auto invalid_id = call;
    invalid_id["id"] = nullptr;
    CHECK(json(http_exchange(port, request(port, invalid_id)))["error"]["code"] == -32600);
    CHECK(http_exchange(port, request(port, Json::array({call}))).result_int() == 400);
    auto malformed = request(port, call);
    malformed.body() = "{";
    malformed.prepare_payload();
    CHECK(json(http_exchange(port, malformed))["error"]["code"] == -32700);
    malformed.body() = std::string(65, '[') + "0" + std::string(65, ']');
    malformed.prepare_payload();
    CHECK(http_exchange(port, malformed).result_int() == 400);
    auto huge_header = request(port, call);
    huge_header.set("X-Padding", std::string(9000, 'x'));
    CHECK(http_exchange(port, huge_header).result_int() == 431);
    auto huge_body = request(port, call);
    huge_body.set(http::field::content_length, "1048577");
    CHECK(http_exchange(port, huge_body).result_int() == 413);
    CHECK(calls.load() == baseline);
    server.stop();
    CHECK(!server.is_running());
    CHECK(server.start(port, token, handler, error));
    server.stop();
    server.stop();
    boost::system::error_code ec;
    net::io_context io;
    tcp::socket socket(io);
    socket.connect(tcp::endpoint(net::ip::make_address("127.0.0.1"), port), ec);
    CHECK(bool(ec));
}

void credential_tests()
{
    namespace fs = std::filesystem;
    struct TemporaryProfile {
        fs::path path = fs::temp_directory_path() / ("bambu-mcp-credential-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        TemporaryProfile() { if (!fs::create_directory(path)) throw std::runtime_error("Cannot create test profile"); }
        ~TemporaryProfile() { std::error_code ec; fs::remove_all(path, ec); }
    } profile;
    std::string original, loaded, replacement, error;
    CHECK(Slic3r::GUI::load_or_create_mcp_token(profile.path.string(), original, error));
    CHECK(original.size() == 64);
    CHECK(Slic3r::GUI::load_or_create_mcp_token(profile.path.string(), loaded, error));
    CHECK(loaded == original);
#ifndef _WIN32
    CHECK(fs::status(profile.path / "mcp.token").permissions() == (fs::perms::owner_read | fs::perms::owner_write));
#endif
    CHECK(Slic3r::GUI::regenerate_mcp_token(profile.path.string(), replacement, error));
    CHECK(replacement.size() == 64 && replacement != original);
    CHECK(Slic3r::GUI::load_or_create_mcp_token(profile.path.string(), loaded, error));
    CHECK(loaded == replacement);
    Slic3r::McpServer server;
    auto port = free_port();
    CHECK(server.start(port, replacement, [](const auto&, const auto&, auto, auto) {}, error));
    auto req = request(port, modern_request("server/discover"));
    req.set(http::field::authorization, "Bearer " + original);
    CHECK(http_exchange(port, req).result_int() == 401);
    req.set(http::field::authorization, "Bearer " + replacement);
    CHECK(http_exchange(port, req).result_int() == 200);
    server.stop();
#ifndef _WIN32
    fs::rename(profile.path / "mcp.token", profile.path / "original.token");
    fs::create_symlink(profile.path / "original.token", profile.path / "mcp.token");
    CHECK(!Slic3r::GUI::load_or_create_mcp_token(profile.path.string(), loaded, error));
#endif
}

void gcode_export_tests()
{
    namespace fs = std::filesystem;
    struct TemporaryDirectory {
        fs::path path = fs::temp_directory_path() / ("bambu-mcp-export-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        TemporaryDirectory() { if (!fs::create_directory(path)) throw std::runtime_error("Cannot create export test directory"); }
        ~TemporaryDirectory() { std::error_code ec; fs::remove_all(path, ec); }
    } directory;
    const auto source = directory.path / "source.gcode";
    const auto temporary = directory.path / "temporary.gcode";
    const auto destination = directory.path / "export.gcode";
    { std::ofstream file(source, std::ios::binary); file << "G1 X10 Y20\n"; }
    CHECK(!Slic3r::GUI::mcp_gcode_identity(source.u8string()).empty());
    CHECK(Slic3r::GUI::mcp_copy_gcode(source.u8string(), temporary.u8string(), destination.u8string(), false,
        [] { return true; }) == 11);
    CHECK(fs::file_size(destination) == 11);
    CHECK(!fs::exists(temporary));
    bool rejected = false;
    try { Slic3r::GUI::mcp_copy_gcode(source.u8string(), temporary.u8string(), destination.u8string(), false,
        [] { return true; }); }
    catch (const std::runtime_error&) { rejected = true; }
    CHECK(rejected);
    { std::ofstream file(source, std::ios::binary | std::ios::trunc); file << "G1 X30\n"; }
    CHECK(Slic3r::GUI::mcp_copy_gcode(source.u8string(), temporary.u8string(), destination.u8string(), true,
        [] { return true; }) == 7);
    CHECK(fs::file_size(destination) == 7);
    rejected = false;
    try { Slic3r::GUI::mcp_copy_gcode(source.u8string(), temporary.u8string(), source.u8string(), true,
        [] { return true; }); }
    catch (const std::runtime_error&) { rejected = true; }
    CHECK(rejected);
    const auto stale = directory.path / "stale.gcode";
    rejected = false;
    try { Slic3r::GUI::mcp_copy_gcode(source.u8string(), temporary.u8string(), stale.u8string(), false,
        [] { return false; }); }
    catch (const std::runtime_error&) { rejected = true; }
    CHECK(rejected && !fs::exists(stale) && !fs::exists(temporary));
}

void cancellation_tests()
{
    auto port = free_port();
    std::string error;
    Slic3r::McpServer server(100ms);
    std::promise<std::shared_ptr<Slic3r::McpRequestTicket>> queued;
    Slic3r::McpServer::Completion late;
    CHECK(server.start(port, token, [&](const std::string&, const Json&, auto ticket, auto complete) {
        late = std::move(complete);
        queued.set_value(ticket);
    }, error));
    auto response = std::async(std::launch::async, [&] { return http_exchange(port, request(port, modern_request("tools/list"))); });
    auto ticket = queued.get_future().get();
    CHECK(response.get().result_int() == 504);
    CHECK(ticket->canceled());
    CHECK(!ticket->try_start());
    late({{"tools", Json::array()}});
    server.stop();
    late({{"tools", Json::array()}});

    Slic3r::McpServer pending;
    std::promise<std::shared_ptr<Slic3r::McpRequestTicket>> stopped;
    CHECK(pending.start(port, token, [&](const std::string&, const Json&, auto request_ticket, auto) {
        stopped.set_value(request_ticket);
    }, error));
    auto abandoned = std::async(std::launch::async, [&] {
        try { http_exchange(port, request(port, modern_request("tools/list"))); }
        catch (const boost::system::system_error&) { return true; }
        return false;
    });
    auto stopped_ticket = stopped.get_future().get();
    auto begin = std::chrono::steady_clock::now();
    pending.stop();
    CHECK(std::chrono::steady_clock::now() - begin < 1s);
    CHECK(stopped_ticket->canceled());
    CHECK(!stopped_ticket->try_start());
    CHECK(abandoned.get());
}
}

int main()
{
    try {
        protocol_tests();
        cancellation_tests();
        credential_tests();
        gcode_export_tests();
        std::cout << checks.load() << " MCP checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
