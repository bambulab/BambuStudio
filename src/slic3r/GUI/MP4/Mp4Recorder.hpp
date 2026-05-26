#ifndef slic3r_Mp4Recorder_hpp_
#define slic3r_Mp4Recorder_hpp_

#include <cstdint>
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>

namespace Slic3r {
namespace GUI {

// Records a sequence of RGBA frames into an H.264/MP4 file.
// Uses minih264 for encoding and minimp4 for muxing (no FFmpeg encoder needed).
// Encoding runs on a background thread; call push_frame() from the GL thread.
class Mp4Recorder
{
public:
    Mp4Recorder();
    ~Mp4Recorder();

    Mp4Recorder(const Mp4Recorder&) = delete;
    Mp4Recorder& operator=(const Mp4Recorder&) = delete;

    bool start(uint32_t width, uint32_t height, int fps, const std::string& path);
    void push_frame(const uint8_t* rgba_data, size_t size);
    void stop();

    bool is_recording() const { return m_recording.load(); }

private:
    void encode_thread_func();
    void encode_one_frame(const std::vector<uint8_t>& rgba);

    // minih264 encoder (opaque, allocated in cpp)
    void*    m_h264_enc     = nullptr;
    void*    m_h264_scratch = nullptr;

    // RGBA -> YUV420P is done by an in-house BT.601 limited-range converter
    // in the .cpp; no external context is needed (kept libswscale-free so
    // this header is portable to platforms where libslic3r_gui doesn't link
    // ffmpeg, such as macOS).

    // YUV420P frame buffer
    std::vector<uint8_t> m_yuv_buf;
    uint8_t* m_yuv_planes[3] = {};
    int      m_yuv_strides[3] = {};

    uint32_t m_width  = 0;
    uint32_t m_height = 0;
    int      m_fps    = 30;

    // minimp4 muxer state (void* to avoid exposing types in header)
    std::string  m_mp4_path;
    FILE*        m_mp4_file   = nullptr;
    void*        m_mux        = nullptr;   // MP4E_mux_t*
    void*        m_mp4_writer = nullptr;   // mp4_h26x_writer_t*

    // Background thread & queue
    std::atomic<bool>         m_recording{false};
    std::atomic<bool>         m_stop_requested{false};
    std::thread               m_thread;
    std::mutex                m_queue_mutex;
    std::condition_variable   m_queue_cv;
    std::queue<std::vector<uint8_t>> m_frame_queue;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_Mp4Recorder_hpp_
