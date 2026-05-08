#ifndef __DEVICE_HTTP_SERVER_HPP__
#define __DEVICE_HTTP_SERVER_HPP__

#include <atomic>
#include <memory>
#include <string>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/thread.hpp>

#define DEVICE_LOCALHOST_PORT 13628
#define DEVICE_LOCALHOST_URL "http://localhost:"

namespace Slic3r { namespace GUI {

class DeviceHttpServer
{
public:
    DeviceHttpServer();
    ~DeviceHttpServer();

    bool is_started() const { return running_.load(); }
    void start();
    void stop();

private:
    void do_accept();

    using work_guard_t = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;

    std::atomic_bool               running_{false};
    boost::asio::io_context        io_{1};
    std::unique_ptr<work_guard_t>  guard_;
    boost::asio::ip::tcp::acceptor acceptor_{io_};
    boost::thread                  worker_;
};

}} // namespace Slic3r::GUI

#endif //__DEVICE_HTTP_SERVER_HPP__
