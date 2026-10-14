#ifndef slic3r_McpServer_hpp_
#define slic3r_McpServer_hpp_

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <nlohmann/json.hpp>

namespace Slic3r {

struct McpServerAccess;

class McpRequestTicket
{
public:
    bool try_start();
    bool canceled() const;

private:
    friend struct McpServerAccess;
    enum class State { Pending, Started, Canceled };
    std::atomic<State> m_state{State::Pending};
    std::atomic<bool> m_abandoned{false};
};

class McpServer
{
public:
    using Json = nlohmann::json;
    // Complete with a raw MCP result or {"error":{"code":-32602,"message":"..."}} for invalid tool parameters.
    using Completion = std::function<void(Json)>;
    using Handler = std::function<void(const std::string&, const Json&,
                                      std::shared_ptr<McpRequestTicket>, Completion)>;

    explicit McpServer(std::chrono::milliseconds request_timeout = std::chrono::seconds(30));
    ~McpServer();
    McpServer(const McpServer&) = delete;
    McpServer& operator=(const McpServer&) = delete;

    bool start(std::uint16_t port, const std::string& token, Handler handler, std::string& error);
    void stop();
    bool is_running() const;

private:
    struct Impl;
    mutable std::mutex m_mutex;
    std::shared_ptr<Impl> m_impl;
    std::thread m_thread;
    std::chrono::milliseconds m_request_timeout;
};

}

#endif
