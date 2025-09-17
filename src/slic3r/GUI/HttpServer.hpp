#ifndef slic3r_Http_App_hpp_
#define slic3r_Http_App_hpp_

#include <iostream>
#include <mutex>
#include <stack>

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <string>
#include <memory>

using namespace boost::system;
using namespace boost::asio;

#define LOCALHOST_PORT      13618
#define LOCALHOST_URL       "http://localhost:"

namespace Slic3r {
namespace GUI {

    class TicketLoginTask {
    public:
        static std::string perform_sync(const std::string &ticket);
        static void perform_async(const std::string &ticket, std::function<void(std::string)> cb);
    private:
        static std::optional<std::string> do_request_login_info(const std::string &ticket);
    };

    struct LoginParams {
        std::string ticket;
        std::string redirect_url;
    };

    struct session : public std::enable_shared_from_this<session> {
        explicit session(boost::asio::ip::tcp::socket s) : socket_(std::move(s)) {}

        void run() { do_read(); }

    private:
        void do_read() {
            auto self = shared_from_this();
            req_ = {}; // reset
            boost::beast::http::async_read(socket_, buffer_, req_,
                [self](boost::beast::error_code ec, std::size_t) {
                    if (ec) return self->fail_close(ec);
                    self->handle_request();
                });
        }

        void do_write_404();

        void do_write_302(LoginParams params, bool result);

        void handle_request();

        void fail_close(const boost::beast::error_code& ec) {
            boost::beast::error_code ignored;
            socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored);
        }

        boost::asio::ip::tcp::socket socket_;
        boost::beast::flat_buffer buffer_;
        boost::beast::http::request<boost::beast::http::string_body> req_;
    };

class HttpServer
{
public:
    HttpServer();
    ~HttpServer();

    bool is_started() const { return running_.load(); }
    void start();
    void stop();

private:
    void do_accept();

    std::atomic_bool                                                                          running_{false};
    boost::asio::io_context                                                                   io_{1};
    std::unique_ptr<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> guard_;
    boost::asio::ip::tcp::acceptor                                                            acceptor_{io_};
    boost::thread                                                                             worker_;
};
}
};

#endif
