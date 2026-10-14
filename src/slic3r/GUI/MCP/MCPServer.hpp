#ifndef slic3r_MCP_Server_hpp_
#define slic3r_MCP_Server_hpp_

#include <iostream>
#include <mutex>
#include <unordered_map>
#include <string>
#include <memory>
#include <atomic>
#include <vector>

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio.hpp>
#include <boost/thread.hpp>

#include "MCPDispatcher.hpp"

namespace Slic3r {
namespace GUI {

class MCPServer;

class MCPSession : public std::enable_shared_from_this<MCPSession> {
public:
    explicit MCPSession(boost::asio::ip::tcp::socket socket, MCPServer& server);
    ~MCPSession();

    void run();
    void send_sse_event(const std::string& event, const std::string& data);
    void close();

    const std::string& session_id() const { return session_id_; }
    bool is_sse() const { return is_sse_; }

private:
    void do_read();
    void handle_request();
    void handle_sse_get();
    void handle_messages_post();
    void handle_health_get();
    void handle_options();
    void do_write_response(boost::beast::http::status status, const std::string& body, const std::string& content_type = "application/json");
    void fail_close(const boost::beast::error_code& ec);
    void write_next_sse_event();
    void do_listen_for_sse_disconnect();

    boost::asio::ip::tcp::socket socket_;
    MCPServer& server_;
    boost::beast::flat_buffer buffer_;
    boost::beast::http::request<boost::beast::http::string_body> req_;
    std::string session_id_;
    bool is_sse_{ false };

    std::mutex write_mutex_;
    std::vector<std::string> pending_events_;
    bool writing_sse_{ false };
    char disconnect_buf_[64];
};

class MCPServer {
public:
    MCPServer();
    ~MCPServer();

    bool is_started() const { return running_.load(); }
    bool start(int port = 27183);
    void stop();
    int get_port() const { return port_; }

    MCPDispatcher& dispatcher() { return dispatcher_; }

    void register_sse_session(const std::string& session_id, std::shared_ptr<MCPSession> session);
    void unregister_sse_session(const std::string& session_id);
    void broadcast_sse_event(const std::string& event, const std::string& data);
    bool send_sse_event_to(const std::string& session_id, const std::string& event, const std::string& data);

private:
    void do_accept();

    std::atomic_bool running_{ false };
    int port_{ 27183 };
    boost::asio::io_context io_{ 1 };
    std::unique_ptr<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> guard_;
    boost::asio::ip::tcp::acceptor acceptor_{ io_ };
    boost::thread worker_;
    MCPDispatcher dispatcher_;

    std::mutex sessions_mutex_;
    std::unordered_map<std::string, std::shared_ptr<MCPSession>> sse_sessions_;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_MCP_Server_hpp_
