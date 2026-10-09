#include "McpServer.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <utility>
#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <openssl/crypto.h>
#include <openssl/evp.h>

namespace Slic3r {
namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = net::ip::tcp;
using Json = McpServer::Json;

struct McpServerAccess
{
    static void cancel(const std::shared_ptr<McpRequestTicket>& ticket)
    {
        if (!ticket) return;
        ticket->m_abandoned.store(true);
        auto expected = McpRequestTicket::State::Pending;
        ticket->m_state.compare_exchange_strong(expected, McpRequestTicket::State::Canceled);
    }
};

bool McpRequestTicket::try_start()
{
    if (m_abandoned.load()) return false;
    auto expected = State::Pending;
    return m_state.compare_exchange_strong(expected, State::Started) && !m_abandoned.load();
}

bool McpRequestTicket::canceled() const { return m_abandoned.load(); }

namespace {
constexpr const char* modern_version = "2026-07-28";
constexpr const char* legacy_version = "2025-11-25";
constexpr std::size_t max_connections = 16;
constexpr std::size_t max_body = 1024 * 1024;
constexpr std::size_t max_header = 8192;

Json rpc_error(const Json& id, int code, const std::string& message, Json data = nullptr)
{
    Json result = {{"jsonrpc", "2.0"}, {"error", {{"code", code}, {"message", message}}}};
    if (!id.is_null()) result["id"] = id;
    if (!data.is_null()) result["error"]["data"] = std::move(data);
    return result;
}

bool acceptable_depth(const std::string& body)
{
    bool quoted = false, escaped = false;
    unsigned depth = 0;
    for (char c : body) {
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') { if (++depth > 64) return false; }
        else if ((c == '}' || c == ']') && depth > 0) --depth;
    }
    return true;
}

std::string trim(std::string value)
{
    const auto first = value.find_first_not_of(" \t");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t");
    return value.substr(first, last - first + 1);
}

bool has_media_type(const std::string& header, const std::string& type)
{
    std::size_t pos = 0;
    while (pos < header.size()) {
        auto end = header.find(',', pos);
        auto item = trim(header.substr(pos, end == std::string::npos ? end : end - pos));
        auto semi = item.find(';');
        auto media = trim(item.substr(0, semi));
        if (beast::iequals(media, type)) {
            bool enabled = true;
            while (semi != std::string::npos) {
                auto next = item.find(';', semi + 1);
                auto parameter = trim(item.substr(semi + 1, next == std::string::npos ? next : next - semi - 1));
                if (parameter.size() >= 2 && beast::iequals(parameter.substr(0, 2), "q=")) {
                    try { enabled = std::stod(parameter.substr(2)) > 0; }
                    catch (...) { enabled = false; }
                }
                semi = next;
            }
            if (enabled) return true;
        }
        if (end == std::string::npos) break;
        pos = end + 1;
    }
    return false;
}

bool decode_name(const std::string& value, std::string& decoded)
{
    constexpr const char* prefix = "=?base64?";
    if (value.compare(0, 9, prefix) != 0 || value.size() < 11 || value.compare(value.size() - 2, 2, "?=") != 0) {
        decoded = value;
        return true;
    }
    auto encoded = value.substr(9, value.size() - 11);
    if (encoded.empty() || encoded.size() % 4 != 0) return false;
    auto padding = encoded.back() == '=' ? (encoded.size() > 1 && encoded[encoded.size() - 2] == '=' ? 2 : 1) : 0;
    for (std::size_t i = 0; i < encoded.size() - padding; ++i) {
        unsigned char c = static_cast<unsigned char>(encoded[i]);
        if (!(std::isalnum(c) || c == '+' || c == '/')) return false;
    }
    decoded.resize(encoded.size() / 4 * 3);
    int length = EVP_DecodeBlock(reinterpret_cast<unsigned char*>(&decoded[0]),
                                 reinterpret_cast<const unsigned char*>(encoded.data()), static_cast<int>(encoded.size()));
    if (length < padding) return false;
    decoded.resize(static_cast<std::size_t>(length - padding));
    std::string canonical(encoded.size() + 1, '\0');
    EVP_EncodeBlock(reinterpret_cast<unsigned char*>(&canonical[0]),
                    reinterpret_cast<const unsigned char*>(decoded.data()), static_cast<int>(decoded.size()));
    canonical.resize(encoded.size());
    return canonical == encoded;
}
}

struct McpServer::Impl : std::enable_shared_from_this<McpServer::Impl>
{
    struct Connection;
    net::io_context io{1};
    tcp::acceptor acceptor{io};
    std::map<std::size_t, std::shared_ptr<Connection>> connections;
    std::size_t next_connection = 0;
    std::atomic<bool> running{false};
    std::uint16_t port;
    std::string token;
    Handler handler;
    std::chrono::milliseconds request_timeout;

    Impl(std::uint16_t port_, std::string token_, Handler handler_, std::chrono::milliseconds timeout)
        : port(port_), token(std::move(token_)), handler(std::move(handler_)), request_timeout(timeout) {}
    void accept();
    void shutdown();
};

struct McpServer::Impl::Connection : std::enable_shared_from_this<Connection>
{
    std::weak_ptr<Impl> owner;
    std::size_t key;
    beast::tcp_stream stream;
    beast::flat_buffer buffer{max_header + 4096};
    http::request_parser<http::string_body> parser;
    net::steady_timer deadline;
    std::shared_ptr<McpRequestTicket> ticket;
    Json request_id;
    bool modern = false;
    bool writing = false;
    bool closed = false;

    Connection(const std::shared_ptr<Impl>& owner_, std::size_t key_, tcp::socket socket)
        : owner(owner_), key(key_), stream(std::move(socket)), deadline(stream.get_executor())
    {
        parser.body_limit(max_body);
        parser.header_limit(max_header);
    }

    std::string header(beast::string_view name) const
    {
        auto it = parser.get().find(name);
        return it == parser.get().end() ? std::string() : std::string(it->value().data(), it->value().size());
    }

    bool duplicate(beast::string_view name) const { return parser.get().count(name) > 1; }

    void close()
    {
        if (closed) return;
        closed = true;
        McpServerAccess::cancel(ticket);
        boost::system::error_code ignored;
        deadline.cancel();
        stream.socket().cancel(ignored);
        stream.socket().shutdown(tcp::socket::shutdown_both, ignored);
        stream.socket().close(ignored);
        if (auto p = owner.lock()) p->connections.erase(key);
    }

    void send(http::status status, Json body = nullptr)
    {
        if (closed || writing) return;
        writing = true;
        deadline.cancel();
        auto response = std::make_shared<http::response<http::string_body>>(status, 11);
        response->set(http::field::cache_control, "no-store");
        response->set(http::field::server, "BambuStudio-MCP");
        response->keep_alive(false);
        if (status == http::status::method_not_allowed) response->set(http::field::allow, "POST");
        if (status == http::status::unauthorized) response->set(http::field::www_authenticate, "Bearer realm=\"BambuStudio MCP\"");
        if (!body.is_null()) {
            response->set(http::field::content_type, "application/json");
            response->body() = body.dump(-1, ' ', false, Json::error_handler_t::replace);
        }
        response->prepare_payload();
        stream.expires_after(std::chrono::seconds(10));
        http::async_write(stream, *response, [self = shared_from_this(), response](boost::system::error_code, std::size_t) { self->close(); });
    }

    void fail(http::status status, int code, const std::string& message, Json data = nullptr)
    {
        send(status, rpc_error(request_id, code, message, std::move(data)));
    }

    void result(Json value)
    {
        if (closed || writing || (ticket && ticket->canceled())) return;
        if (!value.is_object()) return fail(http::status::internal_server_error, -32603, "Invalid tool response");
        if (value.contains("error")) {
            const auto& error = value["error"];
            if (value.size() != 1 || !error.is_object() || !error.contains("code") || error["code"] != -32602
                || !error.contains("message") || !error["message"].is_string())
                return fail(http::status::internal_server_error, -32603, "Invalid tool error response");
            return fail(http::status::bad_request, -32602, error["message"].get<std::string>());
        }
        if (modern) {
            value["resultType"] = "complete";
            if (!value.contains("_meta") || !value["_meta"].is_object()) value["_meta"] = Json::object();
            value["_meta"]["io.modelcontextprotocol/serverInfo"] = {{"name", "BambuStudio"}, {"version", "1.0"}};
        }
        send(http::status::ok, {{"jsonrpc", "2.0"}, {"id", request_id}, {"result", std::move(value)}});
    }

    void read()
    {
        stream.expires_after(std::chrono::seconds(10));
        http::async_read_header(stream, buffer, parser, [self = shared_from_this()](boost::system::error_code ec, std::size_t) {
            if (ec) return self->read_error(ec);
            if (!self->validate_http()) return;
            if (self->parser.is_done()) return self->dispatch();
            http::async_read(self->stream, self->buffer, self->parser,
                [self](boost::system::error_code body_ec, std::size_t) {
                    if (body_ec) return self->read_error(body_ec);
                    self->dispatch();
                });
        });
    }

    void read_error(boost::system::error_code ec)
    {
        if (ec == http::error::body_limit) return fail(http::status::payload_too_large, -32600, "Request body exceeds limit");
        if (ec == http::error::header_limit) return fail(http::status::request_header_fields_too_large, -32600, "Request headers exceed limit");
        if (ec == http::error::end_of_stream || ec == net::error::operation_aborted || ec == beast::error::timeout) return close();
        fail(http::status::bad_request, -32600, "Malformed HTTP request");
    }

    bool validate_http()
    {
        auto p = owner.lock();
        if (!p || !p->running.load()) { close(); return false; }
        const auto host = header("Host");
        const auto suffix = ":" + std::to_string(p->port);
        if (duplicate("Host") || (host != "127.0.0.1" + suffix && host != "localhost" + suffix)) {
            fail(http::status::forbidden, -32600, "Invalid local Host"); return false;
        }
        auto origin = header("Origin");
        if (duplicate("Origin") || (parser.get().count("Origin") && origin != "http://127.0.0.1" + suffix && origin != "http://localhost" + suffix)) {
            fail(http::status::forbidden, -32600, "Origin is not allowed"); return false;
        }
        auto authorization = header("Authorization");
        bool authenticated = !duplicate("Authorization") && authorization.size() == p->token.size() + 7
            && beast::iequals(authorization.substr(0, 7), "Bearer ")
            && CRYPTO_memcmp(authorization.data() + 7, p->token.data(), p->token.size()) == 0;
        if (!authenticated) { fail(http::status::unauthorized, -32600, "Local bearer credential required"); return false; }
        if (parser.get().target() != "/mcp") { fail(http::status::not_found, -32600, "Unknown endpoint"); return false; }
        if (parser.get().method() != http::verb::post) { fail(http::status::method_not_allowed, -32600, "Use POST /mcp"); return false; }
        if (duplicate("Content-Type") || !beast::iequals(trim(header("Content-Type").substr(0, header("Content-Type").find(';'))), "application/json")) {
            fail(http::status::unsupported_media_type, -32600, "Content-Type must be application/json"); return false;
        }
        if (!has_media_type(header("Accept"), "application/json") || !has_media_type(header("Accept"), "text/event-stream")) {
            fail(http::status::not_acceptable, -32600, "Accept must include application/json and text/event-stream"); return false;
        }
        if (!header("Content-Encoding").empty()) { fail(http::status::unsupported_media_type, -32600, "Content encoding is not supported"); return false; }
        if (!header("Expect").empty()) { fail(http::status::expectation_failed, -32600, "Expect is not supported"); return false; }
        return true;
    }

    bool modern_headers(const Json& params, const std::string& method)
    {
        if (!params.contains("_meta") || !params["_meta"].is_object()) {
            fail(http::status::bad_request, -32602, "Required request metadata is missing"); return false;
        }
        const auto& meta = params["_meta"];
        if (!meta.contains("io.modelcontextprotocol/protocolVersion") || !meta["io.modelcontextprotocol/protocolVersion"].is_string()
            || !meta.contains("io.modelcontextprotocol/clientCapabilities") || !meta["io.modelcontextprotocol/clientCapabilities"].is_object()) {
            fail(http::status::bad_request, -32602, "Required request metadata is invalid"); return false;
        }
        const auto version = meta["io.modelcontextprotocol/protocolVersion"].get<std::string>();
        if (duplicate("MCP-Protocol-Version") || header("MCP-Protocol-Version") != version
            || duplicate("Mcp-Method") || header("Mcp-Method") != method) {
            fail(http::status::bad_request, -32020, "Required headers do not match request metadata"); return false;
        }
        if (version != modern_version) {
            fail(http::status::bad_request, -32022, "Unsupported protocol version", {{"supported", {modern_version, legacy_version}}, {"requested", version}}); return false;
        }
        if (method == "tools/call" || method == "prompts/get" || method == "resources/read") {
            const auto field = method == "resources/read" ? "uri" : "name";
            if (!params.contains(field) || !params[field].is_string()) { fail(http::status::bad_request, -32602, "Missing request name"); return false; }
            std::string decoded;
            if (duplicate("Mcp-Name") || !parser.get().count("Mcp-Name") || !decode_name(header("Mcp-Name"), decoded) || decoded != params[field].get<std::string>()) {
                fail(http::status::bad_request, -32020, "Mcp-Name does not match request name"); return false;
            }
        }
        return true;
    }

    void dispatch()
    {
        const auto& body = parser.get().body();
        if (!acceptable_depth(body)) return fail(http::status::bad_request, -32600, "JSON nesting exceeds limit");
        Json request = Json::parse(body, nullptr, false);
        if (request.is_discarded()) return fail(http::status::bad_request, -32700, "Invalid JSON");
        if (!request.is_object() || !request.contains("jsonrpc") || request["jsonrpc"] != "2.0"
            || !request.contains("method") || !request["method"].is_string())
            return fail(http::status::bad_request, -32600, "Invalid JSON-RPC request");
        if (request.contains("id")) {
            if (!request["id"].is_string() && !request["id"].is_number_integer()) return fail(http::status::bad_request, -32600, "Request ID must be a string or integer");
            request_id = request["id"];
        }
        const std::string method = request["method"].get<std::string>();
        Json params = request.value("params", Json::object());
        if (!params.is_object()) return fail(http::status::bad_request, -32602, "Params must be an object");
        modern = header("MCP-Protocol-Version") == modern_version
            || (params.contains("_meta") && params["_meta"].is_object() && params["_meta"].contains("io.modelcontextprotocol/protocolVersion"));
        if (modern) {
            if (!modern_headers(params, method)) return;
        } else if (method != "initialize") {
            const auto version = header("MCP-Protocol-Version");
            if (duplicate("MCP-Protocol-Version") || version != legacy_version)
                return fail(http::status::bad_request, -32022, "Unsupported protocol version", {{"supported", {modern_version, legacy_version}}, {"requested", version}});
        }
        if (request_id.is_null()) {
            if (!modern && (method == "notifications/initialized" || method == "notifications/cancelled")) return send(http::status::accepted);
            if (method.compare(0, 14, "notifications/") == 0) return send(http::status::accepted);
            return send(http::status::bad_request);
        }
        if (modern && method == "server/discover")
            return result({{"supportedVersions", {modern_version, legacy_version}}, {"capabilities", {{"tools", Json::object()}}},
                {"instructions", "Local Bambu Studio preparation tools. Operations may update the open project."}});
        if (!modern && method == "initialize") {
            if (!params.contains("protocolVersion") || !params["protocolVersion"].is_string()
                || !params.contains("capabilities") || !params["capabilities"].is_object()
                || !params.contains("clientInfo") || !params["clientInfo"].is_object()
                || !params["clientInfo"].contains("name") || !params["clientInfo"]["name"].is_string()
                || !params["clientInfo"].contains("version") || !params["clientInfo"]["version"].is_string())
                return fail(http::status::bad_request, -32602, "Invalid initialization parameters");
            return result({{"protocolVersion", legacy_version}, {"capabilities", {{"tools", Json::object()}}},
                {"serverInfo", {{"name", "BambuStudio"}, {"version", "1.0"}}}});
        }
        if (!modern && method == "ping") return result(Json::object());
        if (method != "tools/list" && method != "tools/call")
            return fail(modern ? http::status::not_found : http::status::ok, -32601, "Method not found");
        if (method == "tools/call" && (!params.contains("name") || !params["name"].is_string()
            || (params.contains("arguments") && !params["arguments"].is_object())))
            return fail(http::status::bad_request, -32602, "Invalid tool arguments");
        if (method == "tools/list" && params.contains("cursor")) return fail(http::status::bad_request, -32602, "Pagination cursor is not supported");
        auto p = owner.lock();
        if (!p || !p->running.load()) return close();
        ticket = std::make_shared<McpRequestTicket>();
        deadline.expires_after(p->request_timeout);
        deadline.async_wait([self = shared_from_this()](boost::system::error_code ec) {
            if (ec || self->closed || self->writing) return;
            McpServerAccess::cancel(self->ticket);
            self->fail(http::status::gateway_timeout, -32603, "Request timed out; inspect operation state before retrying");
        });
        auto weak = std::weak_ptr<Connection>(shared_from_this());
        auto weak_owner = owner;
        try {
            p->handler(method, params, ticket, [weak, weak_owner](Json value) mutable {
                auto owner = weak_owner.lock();
                if (!owner || !owner->running.load()) return;
                net::post(owner->io, [weak, value = std::move(value)]() mutable {
                    if (auto self = weak.lock()) self->result(std::move(value));
                });
            });
        } catch (...) {
            McpServerAccess::cancel(ticket);
            fail(http::status::internal_server_error, -32603, "Tool dispatch failed");
        }
    }
};

void McpServer::Impl::accept()
{
    acceptor.async_accept([self = shared_from_this()](boost::system::error_code ec, tcp::socket socket) {
        if (!self->running.load()) return;
        if (!ec && self->connections.size() < max_connections) {
            const auto key = ++self->next_connection;
            auto connection = std::make_shared<Connection>(self, key, std::move(socket));
            self->connections.emplace(key, connection);
            connection->read();
        }
        if (!ec) self->accept();
        else self->shutdown();
    });
}

void McpServer::Impl::shutdown()
{
    running.store(false);
    boost::system::error_code ignored;
    acceptor.cancel(ignored);
    acceptor.close(ignored);
    auto active = std::move(connections);
    for (auto& entry : active) entry.second->close();
}

McpServer::McpServer(std::chrono::milliseconds timeout) : m_request_timeout(timeout) {}
McpServer::~McpServer() { stop(); }

bool McpServer::start(std::uint16_t port, const std::string& token, Handler handler, std::string& error)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    error.clear();
    if (m_impl || m_thread.joinable()) { error = "MCP server is already started"; return false; }
    if (port < 1024) { error = "MCP port must be between 1024 and 65535"; return false; }
    if (token.size() < 32 || token.size() > 256 || !std::all_of(token.begin(), token.end(), [](unsigned char c) { return c >= 0x21 && c <= 0x7e; })) {
        error = "MCP requires a valid local access token"; return false;
    }
    if (!handler || m_request_timeout.count() <= 0) { error = "Invalid MCP server configuration"; return false; }
    try {
        auto impl = std::make_shared<Impl>(port, token, std::move(handler), m_request_timeout);
        const tcp::endpoint endpoint(net::ip::make_address_v4("127.0.0.1"), port);
        impl->acceptor.open(endpoint.protocol());
#ifdef _WIN32
        BOOL exclusive = TRUE;
        if (::setsockopt(impl->acceptor.native_handle(), SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                         reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)) == SOCKET_ERROR)
            throw boost::system::system_error(::WSAGetLastError(), boost::system::system_category());
#else
        impl->acceptor.set_option(tcp::acceptor::reuse_address(true));
#endif
        impl->acceptor.bind(endpoint);
        impl->acceptor.listen(16);
        impl->running.store(true);
        impl->accept();
        m_thread = std::thread([impl] {
            try { impl->io.run(); }
            catch (...) { impl->shutdown(); }
        });
        m_impl = std::move(impl);
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

void McpServer::stop()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_impl) {
        auto impl = std::move(m_impl);
        impl->running.store(false);
        net::post(impl->io, [impl] { impl->shutdown(); });
    }
    if (m_thread.joinable()) {
        if (m_thread.get_id() == std::this_thread::get_id()) m_thread.detach();
        else m_thread.join();
    }
}

bool McpServer::is_running() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_impl && m_impl->running.load();
}

}
