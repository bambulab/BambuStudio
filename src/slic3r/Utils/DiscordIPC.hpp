#ifndef slic3r_DiscordIPC_hpp_
#define slic3r_DiscordIPC_hpp_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Slic3r {

enum class DiscordOpcode : uint32_t {
    Handshake = 0,
    Frame     = 1,
    Close     = 2,
    Ping      = 3,
    Pong      = 4,
};

// A larger declared length means the stream has desynchronised.
static const uint32_t DISCORD_MAX_FRAME_BYTES = 64 * 1024;

std::vector<char> discord_encode_frame(DiscordOpcode opcode, const std::string &payload);

bool discord_decode_header(const char *data, size_t size, DiscordOpcode &opcode, uint32_t &length);

// Discord rejects presence fields that are over-long or not valid UTF-8, and
// project names are frequently multibyte.
std::string discord_truncate_utf8(const std::string &text, size_t max_bytes);

std::vector<std::string> discord_candidate_endpoints();

// Every failure is a return value rather than an exception: "Discord is not
// running" is the ordinary case, not an error.
class DiscordIPC
{
public:
    enum class ReadResult { NoData, Frame, Closed };

    DiscordIPC() = default;
    ~DiscordIPC();

    DiscordIPC(const DiscordIPC &) = delete;
    DiscordIPC &operator=(const DiscordIPC &) = delete;

    bool try_connect();
    void close();
    bool is_connected() const;

    bool write_frame(DiscordOpcode opcode, const std::string &payload);

    // Closed means the caller must close() and reconnect.
    ReadResult read_frame(int timeout_ms, DiscordOpcode &opcode, std::string &payload);

private:
    bool       connect_endpoint(const std::string &path);
    bool       write_all(const char *src, size_t count);
    ReadResult wait_readable(int timeout_ms);
    bool       read_exactly(char *dst, size_t count, int timeout_ms);

#ifdef _WIN32
    void *m_pipe { nullptr };   // HANDLE
    bool  m_open { false };
#else
    int   m_fd { -1 };
#endif
};

} // namespace Slic3r

#endif // slic3r_DiscordIPC_hpp_
