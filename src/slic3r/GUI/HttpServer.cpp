#include "HttpServer.hpp"
#include <boost/log/trivial.hpp>
#include "GUI_App.hpp"
#include "slic3r/Utils/Http.hpp"
#include "slic3r/Utils/NetworkAgent.hpp"

namespace Slic3r {
namespace GUI {

    struct TokenResp {
        std::string accessToken;
        std::string refreshToken;
        double expiresIn;
        double refreshExpiresIn;
        std::string tfaKey;
        std::string accessMethod;
        std::string loginType;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(TokenResp, accessToken, refreshToken,
            expiresIn, refreshExpiresIn, tfaKey, accessMethod, loginType)
    };

    struct ProfileResp {
        std::string uidStr;
        std::string account;
        std::string name;
        std::string avatar;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(ProfileResp, uidStr, account,
            name, avatar)
    };

    inline unsigned char hex_to_byte(char hi, char lo) {
        auto hexval = [](char c) -> int {
            if ('0' <= c && c <= '9') return c - '0';
            if ('A' <= c && c <= 'F') return c - 'A' + 10;
            if ('a' <= c && c <= 'f') return c - 'a' + 10;
            return -1;
            };
        return static_cast<unsigned char>((hexval(hi) << 4) | hexval(lo));
    }

    std::string url_decode(const std::string& in)
    {
        std::string out;
        out.reserve(in.size());

        for (size_t i = 0; i < in.size(); ++i) {
            if (in[i] == '%' && i + 2 < in.size() &&
                std::isxdigit(static_cast<unsigned char>(in[i + 1])) &&
                std::isxdigit(static_cast<unsigned char>(in[i + 2]))) {
                out.push_back(static_cast<char>(hex_to_byte(in[i + 1], in[i + 2])));
                i += 2;
            }
            else if (in[i] == '+') {
                out.push_back(' ');
            }
            else {
                out.push_back(in[i]);
            }
        }
        return out;
    }

HttpServer::HttpServer()
{

}

 HttpServer::~HttpServer()
 {
     stop();
 }

void HttpServer::start()
{
    BOOST_LOG_TRIVIAL(info) << "start_http_service...";
    if (running_.exchange(true)) return;

    using tcp = boost::asio::ip::tcp;
    guard_    = std::make_unique<boost::asio::executor_work_guard<decltype(io_.get_executor())>>(io_.get_executor());

    tcp::endpoint ep{tcp::v4(), LOCALHOST_PORT};
    boost::system::error_code ec;
    acceptor_.open(ep.protocol(), ec);
    acceptor_.set_option(tcp::acceptor::reuse_address(true), ec);
    acceptor_.bind(ep, ec);
    acceptor_.listen(boost::asio::socket_base::max_listen_connections, ec);

    do_accept();
    worker_ = boost::thread([this] { io_.run(); });
}

void HttpServer::stop()
{
    if (!running_.exchange(false)) return;
    boost::system::error_code ec;
    acceptor_.close(ec);
    io_.stop();
    if (guard_) guard_.reset();
    if (worker_.joinable()) worker_.join();
    io_.restart();
}

void HttpServer::do_accept()
{
    auto handler = [this](boost::system::error_code ec, boost::asio::ip::tcp::socket s) {
        if (!ec && running_) { std::make_shared<session>(std::move(s))->run(); }
        if (running_) do_accept();
    };
    acceptor_.async_accept(handler);
}

static std::unordered_map<std::string, std::string> parse_query(const std::string& qs)
{
    std::unordered_map<std::string, std::string> result;
    size_t start = 0;
    while (start < qs.size()) {
        size_t eq = qs.find('=', start);
        size_t amp = qs.find('&', start);

        std::string key, val;
        if (eq != std::string::npos) {
            key = qs.substr(start, eq - start);
            if (amp != std::string::npos)
                val = qs.substr(eq + 1, amp - eq - 1);
            else
                val = qs.substr(eq + 1);
        }
        else {
            if (amp != std::string::npos)
                key = qs.substr(start, amp - start);
            else
                key = qs.substr(start);
        }
        result[key] = val;

        if (amp == std::string::npos) break;
        start = amp + 1;
    }
    return result;
}

static bool is_http_scheme(const std::string& url)
{
    return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
}

static std::optional<LoginParams>
parse_login_params(const boost::beast::http::request<boost::beast::http::string_body>& req)
{
    std::string target = req.target(); // "/path?ticket=...&redirect_url=..."

    size_t qpos = target.find('?');
    if (qpos == std::string::npos) {
        return std::nullopt;
    }
    std::string query = target.substr(qpos + 1);

    auto params = parse_query(query);

    LoginParams out;
    if (auto it = params.find("ticket"); it != params.end())
        out.ticket = url_decode(it->second);
    if (auto it = params.find("redirect_url"); it != params.end())
        out.redirect_url = url_decode(it->second);

    if (out.ticket.empty() || out.redirect_url.empty())
        return std::nullopt;

    if (!is_http_scheme(out.redirect_url))
        return std::nullopt;

    return out;
}

void session::do_write_404()
{
    using namespace boost::beast;
    using http::field;
    using http::status;
    using http::string_body;
    static const std::string body =
        "<html><body><h1>404 Not Found</h1><p>There's nothing here.</p></body></html>";

    auto res = std::make_shared<http::response<string_body>>(status::not_found, 11); // 11 = HTTP/1.1
    res->set(http::field::content_type, "text/html");
    res->keep_alive(false);
    res->body() = body;
    res->prepare_payload();

    auto self = shared_from_this();
    http::async_write(socket_, *res, [self, res](boost::beast::error_code ec, std::size_t) {
        self->socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_send, ec);
        });
}

void session::do_write_302(LoginParams params, bool result)
{
    using namespace boost::beast;
    using http::field;
    using http::status;
    using http::string_body;
    static const std::string body =
        "<html><body><p>redirect to url</p></body></html>";

    auto res = std::make_shared<http::response<string_body>>(status::found, 11);
    std::string dest_url = result
        ? (boost::format("%1%?result=success") % params.redirect_url).str()
        : (boost::format("%1%?result=fail") % params.redirect_url).str();

    res->set(field::location, dest_url);
    res->set(field::content_type, "text/html");
    res->body() = "<html><body><p>redirect to url</p></body></html>";
    res->prepare_payload();
    res->keep_alive(false);

    auto self = shared_from_this();
    http::async_write(self->socket_, *res,
        [self, res](boost::beast::error_code ec, std::size_t) {
            self->socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_send, ec);
        });
}

void session::handle_request()
{
    using namespace boost::beast;
    using http::field;
    using http::status;
    using http::string_body;

    auto login_params = parse_login_params(req_);

    if (login_params) {
        auto login_info = TicketLoginTask::perform_sync(login_params->ticket);
        if (login_info.empty())
            do_write_302(*login_params, false);
        else {
            NetworkAgent *agent = wxGetApp().getAgent();
            agent->change_user(login_info);
            if (agent->is_user_login()) {
                wxGetApp().request_user_login(1);
                do_write_302(*login_params, true);
            } else
                do_write_302(*login_params, false);
            GUI::wxGetApp().CallAfter([this] { wxGetApp().ShowUserLogin(false); });
        }
    }
    else
        do_write_404();
}

std::string TicketLoginTask::perform_sync(const std::string &ticket)
{
    auto login_info = do_request_login_info(ticket);
    if (login_info)
        return (*login_info);
    else
        return {};
}

void TicketLoginTask::perform_async(const std::string &ticket, std::function<void(std::string)> cb)
{
    boost::thread([cb = std::move(cb), ticket]() {
        auto login_info = do_request_login_info(ticket);
        auto result     = login_info ? *login_info : std::string();
        if (wxTheApp) {
            wxTheApp->CallAfter([cb, result = std::move(result)]() mutable { cb(std::move(result)); });
        } else {
            cb(result);
        }
    }).detach();
}

std::optional<std::string> TicketLoginTask::do_request_login_info(const std::string &ticket)
{
    try
    {
        NetworkAgent *agent = wxGetApp().getAgent();
        unsigned int  http_code;
        std::string   http_body;
        if (agent->get_my_token(ticket, &http_code, &http_body) < 0)
            return std::nullopt;
        else {
            auto token_resp = nlohmann::json::parse(http_body);
            auto token_data = token_resp.get<TokenResp>();
            if (agent->get_my_profile(token_data.accessToken, &http_code, &http_body) < 0)
                return std::nullopt;
            else {
                auto profile_resp = nlohmann::json::parse(http_body);
                auto profile_data = profile_resp.get<ProfileResp>();

                json j;
                j["data"]["refresh_token"]      = token_data.refreshToken;
                j["data"]["token"]              = token_data.accessToken;
                j["data"]["expires_in"]         = std::to_string(token_data.expiresIn);
                j["data"]["refresh_expires_in"] = std::to_string(token_data.refreshExpiresIn);
                j["data"]["user"]["uid"]        = profile_data.uidStr;
                j["data"]["user"]["name"]       = profile_data.name;
                j["data"]["user"]["account"]    = profile_data.account;
                j["data"]["user"]["avatar"]     = profile_data.avatar;
                return std::string(j.dump());
            }
        }
    }
    catch (...)
    {
        return std::nullopt;
    }
}

} // GUI
} //Slic3r
