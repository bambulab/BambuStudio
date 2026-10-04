#include "MCPServer.hpp"
#include <boost/log/trivial.hpp>
#include <boost/algorithm/string.hpp>

namespace Slic3r {
namespace GUI {

MCPSession::MCPSession(boost::asio::ip::tcp::socket socket, MCPServer& server)
    : socket_(std::move(socket)), server_(server)
{
}

MCPSession::~MCPSession()
{
    if (is_sse_ && !session_id_.empty()) {
        server_.unregister_sse_session(session_id_);
    }
}

void MCPSession::run()
{
    do_read();
}

void MCPSession::close()
{
    boost::beast::error_code ec;
    socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
    socket_.close(ec);
}

void MCPSession::fail_close(const boost::beast::error_code& ec)
{
    if (is_sse_ && !session_id_.empty()) {
        server_.unregister_sse_session(session_id_);
    }
    boost::beast::error_code ignored;
    socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored);
    socket_.close(ignored);
}

void MCPSession::do_read()
{
    auto self = shared_from_this();
    req_ = {};
    boost::beast::http::async_read(socket_, buffer_, req_,
        [self](boost::beast::error_code ec, std::size_t) {
            if (ec) {
                return self->fail_close(ec);
            }
            self->handle_request();
        });
}

void MCPSession::handle_request()
{
    if (req_.method() == boost::beast::http::verb::options) {
        handle_options();
        return;
    }

    std::string target = std::string(req_.target());
    if (req_.method() == boost::beast::http::verb::get) {
        if (boost::starts_with(target, "/sse")) {
            handle_sse_get();
        } else if (target == "/health" || target == "/") {
            handle_health_get();
        } else {
            do_write_response(boost::beast::http::status::not_found, "{\"error\":\"Not Found\"}");
        }
    } else if (req_.method() == boost::beast::http::verb::post) {
        if (boost::starts_with(target, "/messages")) {
            handle_messages_post();
        } else {
            do_write_response(boost::beast::http::status::not_found, "{\"error\":\"Not Found\"}");
        }
    } else {
        do_write_response(boost::beast::http::status::method_not_allowed, "{\"error\":\"Method Not Allowed\"}");
    }
}

void MCPSession::handle_options()
{
    using namespace boost::beast::http;
    auto res = std::make_shared<response<string_body>>(status::ok, req_.version());
    res->set(field::access_control_allow_origin, "*");
    res->set(field::access_control_allow_methods, "GET, POST, OPTIONS");
    res->set(field::access_control_allow_headers, "Content-Type, Authorization, X-Requested-With");
    res->prepare_payload();

    auto self = shared_from_this();
    async_write(socket_, *res, [self, res](boost::beast::error_code ec, std::size_t) {
        if (!self->req_.keep_alive() || ec) {
            self->fail_close(ec);
        } else {
            self->do_read();
        }
    });
}

void MCPSession::handle_health_get()
{
    nlohmann::json health = {
        {"status", "ok"},
        {"server", "bambu-studio-mcp"},
        {"version", "1.0.0"},
        {"transport", "sse"},
        {"endpoints", {
            {"sse", "/sse"},
            {"messages", "/messages"},
            {"health", "/health"}
        }}
    };
    do_write_response(boost::beast::http::status::ok, health.dump(2));
}

void MCPSession::handle_sse_get()
{
    static std::atomic<uint64_t> s_session_counter{1};
    session_id_ = "mcp_" + std::to_string(s_session_counter++);
    is_sse_ = true;

    server_.register_sse_session(session_id_, shared_from_this());
    BOOST_LOG_TRIVIAL(info) << "[MCP] New SSE client connection registered: " << session_id_;

    using namespace boost::beast::http;
    auto res = std::make_shared<response<string_body>>(status::ok, req_.version());
    res->set(field::content_type, "text/event-stream");
    res->set(field::cache_control, "no-cache");
    res->set(field::connection, "keep-alive");
    res->set(field::access_control_allow_origin, "*");
    res->set(field::access_control_allow_headers, "*");

    // MCP initial event specifying message submission endpoint
    std::string endpoint_event = "event: endpoint\r\ndata: /messages?sessionId=" + session_id_ + "\r\n\r\n";
    res->body() = endpoint_event;
    res->prepare_payload();

    auto self = shared_from_this();
    async_write(socket_, *res, [self, res](boost::beast::error_code ec, std::size_t) {
        if (ec) {
            self->fail_close(ec);
            return;
        }
        self->do_listen_for_sse_disconnect();
    });
}

void MCPSession::do_listen_for_sse_disconnect()
{
    auto self = shared_from_this();
    socket_.async_read_some(boost::asio::buffer(disconnect_buf_), [self](boost::beast::error_code ec, std::size_t) {
        BOOST_LOG_TRIVIAL(info) << "[MCP] SSE client disconnected: " << self->session_id_;
        self->fail_close(ec);
    });
}

void MCPSession::send_sse_event(const std::string& event, const std::string& data)
{
    std::string msg = "event: " + event + "\r\ndata: " + data + "\r\n\r\n";
    auto self = shared_from_this();
    boost::asio::post(socket_.get_executor(), [self, msg = std::move(msg)]() mutable {
        bool should_start_write = false;
        {
            std::unique_lock<std::mutex> lock(self->write_mutex_);
            self->pending_events_.push_back(std::move(msg));
            if (!self->writing_sse_) {
                self->writing_sse_ = true;
                should_start_write = true;
            }
        }
        if (should_start_write) {
            self->write_next_sse_event();
        }
    });
}

void MCPSession::write_next_sse_event()
{
    std::shared_ptr<std::string> payload;
    {
        std::unique_lock<std::mutex> lock(write_mutex_);
        if (pending_events_.empty()) {
            writing_sse_ = false;
            return;
        }
        payload = std::make_shared<std::string>(std::move(pending_events_.front()));
        pending_events_.erase(pending_events_.begin());
    }

    auto self = shared_from_this();
    boost::asio::async_write(socket_, boost::asio::buffer(*payload),
        [self, payload](boost::beast::error_code ec, std::size_t) {
            if (ec) {
                self->fail_close(ec);
                return;
            }
            self->write_next_sse_event();
        });
}

void MCPSession::handle_messages_post()
{
    std::string target = std::string(req_.target());
    std::string target_session_id;

    size_t qpos = target.find('?');
    if (qpos != std::string::npos) {
        std::string query = target.substr(qpos + 1);
        size_t spos = query.find("sessionId=");
        if (spos != std::string::npos) {
            target_session_id = query.substr(spos + 10);
            size_t amp = target_session_id.find('&');
            if (amp != std::string::npos) {
                target_session_id = target_session_id.substr(0, amp);
            }
        }
    }

    nlohmann::json req_json;
    try {
        req_json = nlohmann::json::parse(req_.body());
    } catch (const std::exception& e) {
        do_write_response(boost::beast::http::status::bad_request,
            "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32700,\"message\":\"Parse error\"}}");
        return;
    }

    nlohmann::json res_json = server_.dispatcher().dispatch(req_json);
    std::string res_str;
    if (!res_json.is_null()) {
        res_str = res_json.dump();
        if (!target_session_id.empty()) {
            server_.send_sse_event_to(target_session_id, "message", res_str);
        }
    }

    do_write_response(boost::beast::http::status::ok, res_str.empty() ? "{}" : res_str);
}

void MCPSession::do_write_response(boost::beast::http::status status, const std::string& body, const std::string& content_type)
{
    using namespace boost::beast::http;
    auto res = std::make_shared<response<string_body>>(status, req_.version());
    res->set(field::content_type, content_type);
    res->set(field::access_control_allow_origin, "*");
    res->set(field::access_control_allow_headers, "*");
    res->keep_alive(req_.keep_alive());
    res->body() = body;
    res->prepare_payload();

    auto self = shared_from_this();
    async_write(socket_, *res, [self, res](boost::beast::error_code ec, std::size_t) {
        if (ec || !res->keep_alive()) {
            self->fail_close(ec);
        } else {
            self->do_read();
        }
    });
}

// MCPServer Implementation
MCPServer::MCPServer()
{
}

MCPServer::~MCPServer()
{
    stop();
}

bool MCPServer::start(int port)
{
    if (running_.load()) return true;

    port_ = port;
    guard_ = std::make_unique<boost::asio::executor_work_guard<decltype(io_.get_executor())>>(io_.get_executor());

    boost::asio::ip::tcp::endpoint ep{boost::asio::ip::make_address("127.0.0.1"), static_cast<unsigned short>(port_)};
    boost::system::error_code ec;

    acceptor_.open(ep.protocol(), ec);
    if (ec) {
        BOOST_LOG_TRIVIAL(error) << "[MCP] Server failed to open socket: " << ec.message();
        return false;
    }

    acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true), ec);
    acceptor_.bind(ep, ec);
    if (ec) {
        BOOST_LOG_TRIVIAL(error) << "[MCP] Server failed to bind to port " << port_ << ": " << ec.message();
        return false;
    }

    acceptor_.listen(boost::asio::socket_base::max_listen_connections, ec);
    if (ec) {
        BOOST_LOG_TRIVIAL(error) << "[MCP] Server failed to listen on port " << port_ << ": " << ec.message();
        return false;
    }

    running_.store(true);
    do_accept();
    worker_ = boost::thread([this] { io_.run(); });

    BOOST_LOG_TRIVIAL(info) << "[MCP] Native Model Context Protocol (MCP) server listening on http://127.0.0.1:" << port_;
    return true;
}

void MCPServer::stop()
{
    if (!running_.exchange(false)) return;

    boost::system::error_code ec;
    acceptor_.close(ec);

    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        for (auto& pair : sse_sessions_) {
            pair.second->close();
        }
        sse_sessions_.clear();
    }

    io_.stop();
    if (guard_) guard_.reset();
    if (worker_.joinable()) worker_.join();
    io_.restart();

    BOOST_LOG_TRIVIAL(info) << "[MCP] Native MCP server stopped";
}

void MCPServer::do_accept()
{
    auto handler = [this](boost::system::error_code ec, boost::asio::ip::tcp::socket s) {
        if (!ec && running_.load()) {
            std::make_shared<MCPSession>(std::move(s), *this)->run();
        }
        if (running_.load()) {
            do_accept();
        }
    };
    acceptor_.async_accept(handler);
}

void MCPServer::register_sse_session(const std::string& session_id, std::shared_ptr<MCPSession> session)
{
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    sse_sessions_[session_id] = session;
}

void MCPServer::unregister_sse_session(const std::string& session_id)
{
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    sse_sessions_.erase(session_id);
}

void MCPServer::broadcast_sse_event(const std::string& event, const std::string& data)
{
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (auto& pair : sse_sessions_) {
        pair.second->send_sse_event(event, data);
    }
}

bool MCPServer::send_sse_event_to(const std::string& session_id, const std::string& event, const std::string& data)
{
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto it = sse_sessions_.find(session_id);
    if (it != sse_sessions_.end()) {
        it->second->send_sse_event(event, data);
        return true;
    }
    return false;
}

} // namespace GUI
} // namespace Slic3r
