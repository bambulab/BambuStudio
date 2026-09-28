#include "DiscordIPC.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include <boost/log/trivial.hpp>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        // windows.h would otherwise macro-replace min/max. Not relying on the
        // PCH for this, since SLIC3R_PCH can be off.
        #define NOMINMAX
    #endif
    #include <windows.h>
#else
    #include <errno.h>
    #include <fcntl.h>
    #include <poll.h>
    #include <sys/socket.h>
    #include <sys/types.h>
    #include <sys/un.h>
    #include <unistd.h>
#endif

namespace Slic3r {

static const size_t DISCORD_HEADER_BYTES = 8;

std::vector<char> discord_encode_frame(DiscordOpcode opcode, const std::string &payload)
{
    const uint32_t op  = static_cast<uint32_t>(opcode);
    const uint32_t len = static_cast<uint32_t>(payload.size());

    std::vector<char> frame;
    frame.reserve(DISCORD_HEADER_BYTES + payload.size());
    for (int i = 0; i < 4; ++i)
        frame.push_back(static_cast<char>((op >> (8 * i)) & 0xFF));
    for (int i = 0; i < 4; ++i)
        frame.push_back(static_cast<char>((len >> (8 * i)) & 0xFF));
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

bool discord_decode_header(const char *data, size_t size, DiscordOpcode &opcode, uint32_t &length)
{
    if (data == nullptr || size < DISCORD_HEADER_BYTES)
        return false;

    uint32_t op = 0, len = 0;
    for (int i = 0; i < 4; ++i)
        op |= static_cast<uint32_t>(static_cast<unsigned char>(data[i])) << (8 * i);
    for (int i = 0; i < 4; ++i)
        len |= static_cast<uint32_t>(static_cast<unsigned char>(data[4 + i])) << (8 * i);

    if (op > static_cast<uint32_t>(DiscordOpcode::Pong))
        return false;
    if (len > DISCORD_MAX_FRAME_BYTES)
        return false;

    opcode = static_cast<DiscordOpcode>(op);
    length = len;
    return true;
}

std::string discord_truncate_utf8(const std::string &text, size_t max_bytes)
{
    if (text.size() <= max_bytes)
        return text;

    static const char * ellipsis       = "\xE2\x80\xA6"; // U+2026
    static const size_t ellipsis_bytes = 3;

    if (max_bytes <= ellipsis_bytes)
        return std::string();

    size_t cut = max_bytes - ellipsis_bytes;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80)
        --cut;

    return text.substr(0, cut) + ellipsis;
}

std::vector<std::string> discord_candidate_endpoints()
{
    std::vector<std::string> endpoints;

#ifdef _WIN32
    for (int i = 0; i < 10; ++i)
        endpoints.push_back("\\\\.\\pipe\\discord-ipc-" + std::to_string(i));
#else
    std::vector<std::string> bases;
    for (const char *var : { "XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP" }) {
        const char *value = std::getenv(var);
        if (value != nullptr && *value != '\0')
            bases.push_back(value);
    }
    bases.push_back("/tmp");

    // Sandboxed Discord builds nest the socket one level deeper.
    static const char *subdirs[] = { "", "app/com.discordapp.Discord/", "snap.discord/" };

    for (std::string base : bases) {
        while (!base.empty() && base.back() == '/')
            base.pop_back();
        for (const char *subdir : subdirs)
            for (int i = 0; i < 10; ++i)
                endpoints.push_back(base + "/" + subdir + "discord-ipc-" + std::to_string(i));
    }
#endif

    return endpoints;
}

#ifdef _WIN32

bool DiscordIPC::is_connected() const { return m_open; }

void DiscordIPC::close()
{
    if (m_open) {
        ::CloseHandle(static_cast<HANDLE>(m_pipe));
        ::CloseHandle(static_cast<HANDLE>(m_event));
        m_pipe  = nullptr;
        m_event = nullptr;
        m_open  = false;
    }
}

bool DiscordIPC::connect_endpoint(const std::string &path)
{
    // Overlapped, so a stalled Discord cannot block the worker thread forever.
    HANDLE pipe = ::CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                FILE_FLAG_OVERLAPPED, nullptr);
    if (pipe == INVALID_HANDLE_VALUE)
        return false;

    HANDLE event = ::CreateEventA(nullptr, TRUE, FALSE, nullptr);
    if (event == nullptr) {
        ::CloseHandle(pipe);
        return false;
    }

    m_pipe  = pipe;
    m_event = event;
    m_open  = true;
    return true;
}

// Runs one overlapped operation to completion, cancelling it if the deadline
// passes. Only ever called from the worker thread, so sharing m_event is safe.
static bool await_overlapped(HANDLE pipe, OVERLAPPED &ov, bool pending, DWORD &transferred, ULONGLONG deadline)
{
    if (pending) {
        const ULONGLONG now     = ::GetTickCount64();
        const DWORD     wait_ms = now >= deadline ? 0 : static_cast<DWORD>(deadline - now);
        if (::WaitForSingleObject(ov.hEvent, wait_ms) != WAIT_OBJECT_0) {
            // The blocking wait lets the cancel settle before ov leaves scope.
            ::CancelIoEx(pipe, &ov);
            ::GetOverlappedResult(pipe, &ov, &transferred, TRUE);
            return false;
        }
    }
    // Authoritative even when the call completed synchronously, which is why
    // the byte count is not taken from the WriteFile/ReadFile out parameter.
    return ::GetOverlappedResult(pipe, &ov, &transferred, FALSE) != FALSE && transferred > 0;
}

bool DiscordIPC::write_all(const char *src, size_t count)
{
    const ULONGLONG deadline = ::GetTickCount64() + 2000;

    size_t written = 0;
    while (written < count) {
        OVERLAPPED ov {};
        ov.hEvent = static_cast<HANDLE>(m_event);
        ::ResetEvent(ov.hEvent);

        DWORD      chunk   = 0;
        const BOOL ok      = ::WriteFile(static_cast<HANDLE>(m_pipe), src + written,
                                         static_cast<DWORD>(count - written), &chunk, &ov);
        const bool pending = !ok && ::GetLastError() == ERROR_IO_PENDING;
        if (!ok && !pending)
            return false;
        if (!await_overlapped(static_cast<HANDLE>(m_pipe), ov, pending, chunk, deadline))
            return false;

        written += chunk;
        if (written < count && ::GetTickCount64() >= deadline)
            return false;
    }
    return true;
}

DiscordIPC::ReadResult DiscordIPC::wait_readable(int timeout_ms)
{
    const ULONGLONG deadline = ::GetTickCount64() + static_cast<ULONGLONG>(std::max(0, timeout_ms));
    for (;;) {
        DWORD available = 0;
        if (!::PeekNamedPipe(static_cast<HANDLE>(m_pipe), nullptr, 0, nullptr, &available, nullptr))
            return ReadResult::Closed;
        if (available >= DISCORD_HEADER_BYTES)
            return ReadResult::Frame;
        if (::GetTickCount64() >= deadline)
            return ReadResult::NoData;
        ::Sleep(10);
    }
}

bool DiscordIPC::read_exactly(char *dst, size_t count, int timeout_ms)
{
    const ULONGLONG deadline = ::GetTickCount64() + static_cast<ULONGLONG>(std::max(0, timeout_ms));
    size_t got = 0;
    while (got < count) {
        DWORD available = 0;
        if (!::PeekNamedPipe(static_cast<HANDLE>(m_pipe), nullptr, 0, nullptr, &available, nullptr))
            return false;
        if (available == 0) {
            if (::GetTickCount64() >= deadline)
                return false;
            ::Sleep(10);
            continue;
        }
        OVERLAPPED ov {};
        ov.hEvent = static_cast<HANDLE>(m_event);
        ::ResetEvent(ov.hEvent);

        DWORD       chunk   = 0;
        const DWORD want    = static_cast<DWORD>(std::min<size_t>(count - got, available));
        const BOOL  ok      = ::ReadFile(static_cast<HANDLE>(m_pipe), dst + got, want, &chunk, &ov);
        const bool  pending = !ok && ::GetLastError() == ERROR_IO_PENDING;
        if (!ok && !pending)
            return false;
        if (!await_overlapped(static_cast<HANDLE>(m_pipe), ov, pending, chunk, deadline))
            return false;
        got += chunk;
    }
    return true;
}

#else // POSIX

bool DiscordIPC::is_connected() const { return m_fd >= 0; }

void DiscordIPC::close()
{
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

bool DiscordIPC::connect_endpoint(const std::string &path)
{
    sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path))
        return false;
    std::memcpy(addr.sun_path, path.c_str(), path.size());

    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return false;

#ifdef FD_CLOEXEC
    // Do not leak the socket into child processes.
    const int fd_flags = ::fcntl(fd, F_GETFD, 0);
    if (fd_flags >= 0)
        ::fcntl(fd, F_SETFD, fd_flags | FD_CLOEXEC);
#endif

    if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return false;
    }

#ifdef SO_NOSIGPIPE
    // Discord quitting mid-write would otherwise raise SIGPIPE and kill the
    // process. Linux has no SO_NOSIGPIPE and uses MSG_NOSIGNAL below.
    const int nosigpipe = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &nosigpipe, sizeof(nosigpipe));
#endif

    // Non-blocking from here on, so a stalled Discord cannot wedge the worker.
    const int fl = ::fcntl(fd, F_GETFL, 0);
    if (fl >= 0)
        ::fcntl(fd, F_SETFL, fl | O_NONBLOCK);

    m_fd = fd;
    return true;
}

bool DiscordIPC::write_all(const char *src, size_t count)
{
    size_t written   = 0;
    int    budget_ms = 2000;

#ifdef MSG_NOSIGNAL
    const int flags = MSG_NOSIGNAL;
#else
    const int flags = 0;
#endif

    while (written < count) {
        const ssize_t n = ::send(m_fd, src + written, count - written, flags);
        if (n > 0) {
            written += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            pollfd    pfd { m_fd, POLLOUT, 0 };
            const int step  = 100;
            const int ready = ::poll(&pfd, 1, step);
            if (ready < 0 && errno != EINTR)
                return false;
            budget_ms -= step;
            if (budget_ms <= 0)
                return false;
            continue;
        }
        return false;
    }
    return true;
}

DiscordIPC::ReadResult DiscordIPC::wait_readable(int timeout_ms)
{
    pollfd    pfd { m_fd, POLLIN, 0 };
    const int ready = ::poll(&pfd, 1, timeout_ms);
    if (ready == 0)
        return ReadResult::NoData;
    if (ready < 0)
        return errno == EINTR ? ReadResult::NoData : ReadResult::Closed;
    if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
        return ReadResult::Closed;
    return ReadResult::Frame;
}

bool DiscordIPC::read_exactly(char *dst, size_t count, int timeout_ms)
{
    size_t got       = 0;
    int    budget_ms = std::max(0, timeout_ms);

    while (got < count) {
        const ssize_t n = ::read(m_fd, dst + got, count - got);
        if (n > 0) {
            got += static_cast<size_t>(n);
            continue;
        }
        if (n == 0)
            return false;
        if (errno == EINTR)
            continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            pollfd    pfd { m_fd, POLLIN, 0 };
            const int step  = 50;
            const int ready = ::poll(&pfd, 1, step);
            if (ready < 0 && errno != EINTR)
                return false;
            budget_ms -= step;
            if (budget_ms <= 0)
                return false;
            continue;
        }
        return false;
    }
    return true;
}

#endif // _WIN32

DiscordIPC::~DiscordIPC() { close(); }

bool DiscordIPC::try_connect()
{
    if (is_connected())
        return true;

    for (const std::string &endpoint : discord_candidate_endpoints()) {
        if (connect_endpoint(endpoint)) {
            BOOST_LOG_TRIVIAL(info) << "DiscordIPC: connected to " << endpoint;
            return true;
        }
    }
    return false;
}

bool DiscordIPC::write_frame(DiscordOpcode opcode, const std::string &payload)
{
    if (!is_connected())
        return false;

    const std::vector<char> frame = discord_encode_frame(opcode, payload);
    if (!write_all(frame.data(), frame.size())) {
        BOOST_LOG_TRIVIAL(debug) << "DiscordIPC: write failed, dropping connection";
        close();
        return false;
    }
    return true;
}

DiscordIPC::ReadResult DiscordIPC::read_frame(int timeout_ms, DiscordOpcode &opcode, std::string &payload)
{
    if (!is_connected())
        return ReadResult::Closed;

    const ReadResult waited = wait_readable(timeout_ms);
    if (waited == ReadResult::NoData)
        return ReadResult::NoData;
    if (waited == ReadResult::Closed) {
        close();
        return ReadResult::Closed;
    }

    char header[DISCORD_HEADER_BYTES];
    if (!read_exactly(header, DISCORD_HEADER_BYTES, 1000)) {
        close();
        return ReadResult::Closed;
    }

    uint32_t length = 0;
    if (!discord_decode_header(header, DISCORD_HEADER_BYTES, opcode, length)) {
        BOOST_LOG_TRIVIAL(debug) << "DiscordIPC: bad frame header, dropping connection";
        close();
        return ReadResult::Closed;
    }

    payload.clear();
    if (length > 0) {
        payload.resize(length);
        if (!read_exactly(&payload[0], length, 2000)) {
            close();
            return ReadResult::Closed;
        }
    }

    if (opcode == DiscordOpcode::Close) {
        close();
        return ReadResult::Closed;
    }
    return ReadResult::Frame;
}

} // namespace Slic3r
