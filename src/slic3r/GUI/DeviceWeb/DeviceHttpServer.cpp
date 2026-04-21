#include "DeviceHttpServer.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <unordered_map>

#include <boost/asio/read.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/log/trivial.hpp>

#include "libslic3r/Utils.hpp"

namespace Slic3r { namespace GUI {

// ---------------------------------------------------------------------------
// HTTP helpers (file-local)
// ---------------------------------------------------------------------------

static std::string mime_type(const std::string &ext)
{
    static const std::unordered_map<std::string, std::string> tbl = {
        {".html","text/html"}, {".htm","text/html"},
        {".css","text/css"},
        {".js","application/javascript"},
        {".png","image/png"}, {".jpg","image/jpeg"}, {".jpeg","image/jpeg"},
        {".gif","image/gif"}, {".svg","image/svg+xml"}
    };
    auto it = tbl.find(ext);
    return (it != tbl.end()) ? it->second : "application/octet-stream";
}

static std::string response_404()
{
    std::string body = "<html><body><h1>404 Not Found</h1></body></html>";
    std::ostringstream ss;
    ss << "HTTP/1.1 404 Not Found\r\n"
       << "Content-Type: text/html\r\n"
       << "Content-Length: " << body.size() << "\r\n\r\n"
       << body;
    return ss.str();
}

// ---------------------------------------------------------------------------
// DeviceHttpHeaders — simple HTTP/1.1 request parser
// ---------------------------------------------------------------------------

class DeviceHttpHeaders
{
    std::string method;
    std::string url;
    std::string version;
    std::map<std::string, std::string> headers;

public:
    std::string get_response()
    {
        std::filesystem::path root = resources_dir() + "/web/device_page/dist";

        if (url.empty() || url.find("..") != std::string::npos)
            return response_404();

        // Strip query string before resolving file path
        std::string path = url;
        auto qpos = path.find('?');
        if (qpos != std::string::npos)
            path = path.substr(0, qpos);

        std::filesystem::path rel  = (path == "/") ? "/index.html" : path;
        std::filesystem::path full = std::filesystem::weakly_canonical(root / rel.relative_path());

        if (full.generic_string().rfind(std::filesystem::weakly_canonical(root).generic_string(), 0) != 0)
            return response_404();

        std::ifstream file(full, std::ios::binary);
        if (!file)
            return response_404();

        std::string body((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        std::ostringstream ss;
        ss << "HTTP/1.1 200 OK\r\n"
           << "Content-Type: " << mime_type(full.extension().string()) << "\r\n"
           << "Content-Length: " << body.size() << "\r\n\r\n"
           << body;
        return ss.str();
    }

    int content_length()
    {
        auto it = headers.find("content-length");
        if (it != headers.end()) {
            int len = 0;
            std::istringstream(it->second) >> len;
            return len;
        }
        return 0;
    }

    void on_read_header(const std::string &line)
    {
        auto pos = line.find(':');
        if (pos != std::string::npos)
            headers[line.substr(0, pos)] = line.substr(pos + 1);
    }

    void on_read_request_line(const std::string &line)
    {
        std::istringstream ss(line);
        ss >> method >> url >> version;
    }
};

// ---------------------------------------------------------------------------
// DeviceSession — one accepted connection
// ---------------------------------------------------------------------------

class DeviceSession : public std::enable_shared_from_this<DeviceSession>
{
    boost::asio::ip::tcp::socket socket_;
    boost::asio::streambuf       buff_;
    DeviceHttpHeaders            headers_;

    void read_first_line()
    {
        auto self = shared_from_this();
        boost::asio::async_read_until(socket_, buff_, '\r',
            [self](const boost::system::error_code &ec, std::size_t) {
                if (ec) return;
                std::istream stream{&self->buff_};
                std::string line, ignore;
                std::getline(stream, line, '\r');
                std::getline(stream, ignore, '\n');
                self->headers_.on_read_request_line(line);
                self->read_next_line();
            });
    }

    void read_next_line()
    {
        auto self = shared_from_this();
        boost::asio::async_read_until(socket_, buff_, '\r',
            [self](const boost::system::error_code &ec, std::size_t) {
                if (ec) return;
                std::istream stream{&self->buff_};
                std::string line, ignore;
                std::getline(stream, line, '\r');
                std::getline(stream, ignore, '\n');
                self->headers_.on_read_header(line);

                if (line.empty()) {
                    self->send_response();
                } else {
                    self->read_next_line();
                }
            });
    }

    void send_response()
    {
        auto self = shared_from_this();
        auto resp = std::make_shared<std::string>(headers_.get_response());
        boost::asio::async_write(socket_, boost::asio::buffer(*resp),
            [self, resp](const boost::system::error_code &, std::size_t) {
                boost::system::error_code ec;
                self->socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
            });
    }

public:
    explicit DeviceSession(boost::asio::ip::tcp::socket socket)
        : socket_(std::move(socket)) {}

    void start() { read_first_line(); }
};

// ---------------------------------------------------------------------------
// DeviceHttpServer
// ---------------------------------------------------------------------------

DeviceHttpServer::DeviceHttpServer() = default;

DeviceHttpServer::~DeviceHttpServer()
{
    stop();
}

void DeviceHttpServer::start()
{
    BOOST_LOG_TRIVIAL(info) << "[DeviceHttpServer] starting on port " << DEVICE_LOCALHOST_PORT;
    if (running_.exchange(true))
        return; // already running

    using tcp = boost::asio::ip::tcp;
    guard_ = std::make_unique<work_guard_t>(io_.get_executor());

    boost::system::error_code ec;
    tcp::endpoint ep{tcp::v4(), DEVICE_LOCALHOST_PORT};
    acceptor_.open(ep.protocol(), ec);
    if (ec) { BOOST_LOG_TRIVIAL(error) << "[DeviceHttpServer] open failed: " << ec.message(); running_ = false; return; }
    acceptor_.set_option(tcp::acceptor::reuse_address(true), ec);
    acceptor_.bind(ep, ec);
    if (ec) { BOOST_LOG_TRIVIAL(error) << "[DeviceHttpServer] bind failed: " << ec.message(); acceptor_.close(); running_ = false; return; }
    acceptor_.listen(boost::asio::socket_base::max_listen_connections, ec);
    if (ec) { BOOST_LOG_TRIVIAL(error) << "[DeviceHttpServer] listen failed: " << ec.message(); acceptor_.close(); running_ = false; return; }

    do_accept();
    worker_ = boost::thread([this] { io_.run(); });
    BOOST_LOG_TRIVIAL(info) << "[DeviceHttpServer] started";
}

void DeviceHttpServer::stop()
{
    if (!running_.exchange(false))
        return; // not running

    BOOST_LOG_TRIVIAL(info) << "[DeviceHttpServer] stopping";
    boost::system::error_code ec;
    acceptor_.close(ec);
    io_.stop();
    if (guard_) guard_.reset();
    if (worker_.joinable()) worker_.join();
    io_.restart(); // allow re-start if needed
    BOOST_LOG_TRIVIAL(info) << "[DeviceHttpServer] stopped";
}

void DeviceHttpServer::do_accept()
{
    acceptor_.async_accept(
        [this](boost::system::error_code ec, boost::asio::ip::tcp::socket socket) {
            if (!ec && running_) {
                std::make_shared<DeviceSession>(std::move(socket))->start();
            }
            if (running_) {
                do_accept();
            }
        });
}

}} // namespace Slic3r::GUI
