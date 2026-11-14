#ifndef slic3r_Http_App_hpp_
#define slic3r_Http_App_hpp_

#include <iostream>
#include <mutex>
#include <stack>

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/version.hpp>
#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <string>
#include <memory>
#include <optional>

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

    enum class HttpReqType {
        Login,
        RefreshToken,
        Unknown,
    };

    struct session : public std::enable_shared_from_this<session> {
        explicit session(boost::asio::ip::tcp::socket s) : socket_(std::move(s)) {}

        void run() { do_read(); }

        void send_text_response(const std::string& text_data);

        void set_scramble_key(std::string key);

    private:
        std::string m_scramble_key = "";
        // Browsers send every cookie set for "localhost" (on any port) with the login
        // callback, so people who run local web projects can easily go over Beast's
        // default 8 KB header limit. That used to make the read fail and the connection
        // close with no response (ERR_EMPTY_RESPONSE in the browser).
        static constexpr std::uint32_t max_header_bytes = 256 * 1024;

        static bool headers_too_large(const boost::beast::error_code& ec) {
            namespace http = boost::beast::http;
            if (ec == http::error::header_limit) return true;
#if BOOST_VERSION >= 108700
            // Boost 1.87+ also caps a single header line (e.g. Cookie) at 64 KB.
            if (ec == http::error::header_field_name_too_large || ec == http::error::header_field_value_too_large) return true;
#endif
            return false;
        }

        void do_read() {
            auto self = shared_from_this();
            req_ = {}; // reset
            parser_.emplace();
            parser_->header_limit(max_header_bytes);
            boost::beast::http::async_read(socket_, buffer_, *parser_,
                [self](boost::beast::error_code ec, std::size_t) {
                    if (headers_too_large(ec)) return self->do_write_431();
                    if (ec) return self->fail_close(ec);
                    self->req_ = self->parser_->release();
                    self->handle_request();
                });
        }

        void do_write_404();

        void do_write_431();

        void do_write_302(LoginParams params, bool result);

        void handle_login();

        void handle_refresh_token();

        void handle_request();

        void fail_close(const boost::beast::error_code& ec) {
            boost::beast::error_code ignored;
            socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored);
        }

        boost::asio::ip::tcp::socket socket_;
        boost::beast::flat_buffer buffer_;
        std::optional<boost::beast::http::request_parser<boost::beast::http::string_body>> parser_;
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
