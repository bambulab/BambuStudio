#pragma once

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace Slic3r { namespace GUI {

// Shared by all ticket login entry points. An active request never expires;
// completed attempts stay blocked for five minutes, including failed attempts
// whose ticket may already have been consumed by the server.
class TicketLoginGuard
{
public:
    using Clock = std::chrono::steady_clock;

    bool try_begin(const std::string& ticket, Clock::time_point now = Clock::now())
    {
        if (ticket.empty())
            return false;

        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto it = m_tickets.begin(); it != m_tickets.end();) {
            if (it->second && *it->second <= now)
                it = m_tickets.erase(it);
            else
                ++it;
        }
        return m_tickets.emplace(ticket, std::nullopt).second;
    }

    void finish(const std::string& ticket, Clock::time_point now = Clock::now())
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_tickets.find(ticket);
        if (it != m_tickets.end())
            it->second = now + std::chrono::minutes(5);
    }

private:
    std::mutex m_mutex;
    std::unordered_map<std::string, std::optional<Clock::time_point>> m_tickets;
};

}} // namespace Slic3r::GUI
