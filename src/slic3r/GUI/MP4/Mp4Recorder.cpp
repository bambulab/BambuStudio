#include "Mp4Recorder.hpp"
#include <boost/log/trivial.hpp>
#include <boost/nowide/cstdio.hpp>
#include <cstring>
#include <cstdlib>

#include "minimp4/minimp4.h"
#include "minimp4/minih264e.h"

namespace Slic3r {
namespace GUI {

namespace {

// Pure-C++ RGBA -> YUV420P (BT.601, limited / "TV" range) with vertical
// flip. Replaces the previous libswscale call so this translation unit
// compiles on every platform — libslic3r_gui only links ffmpeg / libswscale
// on Linux, but the assembly-view MP4 export needs to work on macOS and
// Windows as well, and this class is the only place that ever needed the
// swscale dependency.
//
// Coefficients are the standard ITU-R BT.601 8-bit fixed-point form, which
// is also what swscale's default RGBA -> YUV420P path produces:
//
//     Y' = (( 66*R + 129*G +  25*B + 128) >> 8) + 16
//     U  = ((-38*R -  74*G + 112*B + 128) >> 8) + 128
//     V  = ((112*R -  94*G -  18*B + 128) >> 8) + 128
//
// Result range: Y' in [16,235], U/V in [16,240]. This is what minih264 (and
// any default H.264 stream that doesn't override colorimetry) expects.
//
// Chroma subsampling averages the RGB inside each 2x2 block before deriving
// UV — same visual quality bracket as swscale's SWS_FAST_BILINEAR for a
// non-rescaling conversion. The destination YUV plane is allowed to be
// larger than width x height (minih264 demands enc_w/enc_h aligned to 16);
// only the source-sized region is filled, the remainder stays at the
// caller's pre-zeroed buffer (= solid black border <16 px wide).
inline uint8_t clip_byte(int v)
{
    return v < 0 ? 0 : v > 255 ? 255 : static_cast<uint8_t>(v);
}

void rgba_to_yuv420p_flipped(const uint8_t *rgba, int rgba_stride_bytes,
                             uint8_t *y_plane, int y_stride,
                             uint8_t *u_plane, int u_stride,
                             uint8_t *v_plane, int v_stride,
                             int width, int height)
{
    if (!rgba || width <= 0 || height <= 0)
        return;

    auto rgb_at = [rgba, rgba_stride_bytes, height](int x, int src_y_row) {
        // src_y_row is already flipped (height - 1 - logical_y).
        return rgba + src_y_row * rgba_stride_bytes + x * 4;
    };

    for (int j = 0; j < height; j += 2) {
        const int j1            = j + 1;
        const int row0_src      = height - 1 - j;
        const int row1_src      = (j1 < height) ? (height - 1 - j1) : -1;
        uint8_t * y0_dst        = y_plane + j  * y_stride;
        uint8_t * y1_dst        = (j1 < height) ? (y_plane + j1 * y_stride) : nullptr;
        uint8_t * u_dst         = u_plane + (j >> 1) * u_stride;
        uint8_t * v_dst         = v_plane + (j >> 1) * v_stride;

        for (int i = 0; i < width; i += 2) {
            const int i1     = i + 1;
            const bool has_x = (i1 < width);

            const uint8_t *p00 = rgb_at(i, row0_src);
            const int r00 = p00[0], g00 = p00[1], b00 = p00[2];
            y0_dst[i] = clip_byte(((66 * r00 + 129 * g00 + 25 * b00 + 128) >> 8) + 16);

            int sum_r = r00, sum_g = g00, sum_b = b00, count = 1;

            if (has_x) {
                const uint8_t *p01 = p00 + 4;
                const int r01 = p01[0], g01 = p01[1], b01 = p01[2];
                y0_dst[i1] = clip_byte(((66 * r01 + 129 * g01 + 25 * b01 + 128) >> 8) + 16);
                sum_r += r01; sum_g += g01; sum_b += b01; ++count;
            }

            if (y1_dst) {
                const uint8_t *p10 = rgb_at(i, row1_src);
                const int r10 = p10[0], g10 = p10[1], b10 = p10[2];
                y1_dst[i] = clip_byte(((66 * r10 + 129 * g10 + 25 * b10 + 128) >> 8) + 16);
                sum_r += r10; sum_g += g10; sum_b += b10; ++count;

                if (has_x) {
                    const uint8_t *p11 = p10 + 4;
                    const int r11 = p11[0], g11 = p11[1], b11 = p11[2];
                    y1_dst[i1] = clip_byte(((66 * r11 + 129 * g11 + 25 * b11 + 128) >> 8) + 16);
                    sum_r += r11; sum_g += g11; sum_b += b11; ++count;
                }
            }

            const int avg_r = sum_r / count;
            const int avg_g = sum_g / count;
            const int avg_b = sum_b / count;
            const int U     = ((-38 * avg_r -  74 * avg_g + 112 * avg_b + 128) >> 8) + 128;
            const int V     = (( 112 * avg_r -  94 * avg_g -  18 * avg_b + 128) >> 8) + 128;
            u_dst[i >> 1] = clip_byte(U);
            v_dst[i >> 1] = clip_byte(V);
        }
    }
}

} // anonymous namespace

// ---- minimp4 helpers ----
static int mp4_write_cb(int64_t offset, const void* buffer, size_t size, void* token)
{
    FILE* f = static_cast<FILE*>(token);
    fseek(f, (long)offset, SEEK_SET);
    return fwrite(buffer, 1, size, f) != size;
}
static MP4E_mux_t*        mux_ptr(void* p) { return static_cast<MP4E_mux_t*>(p); }
static mp4_h26x_writer_t* writer_ptr(void* p) { return static_cast<mp4_h26x_writer_t*>(p); }

// ---- minih264 helpers ----
static H264E_persist_t*  enc_ptr(void* p) { return static_cast<H264E_persist_t*>(p); }
static H264E_scratch_t*  scratch_ptr(void* p) { return static_cast<H264E_scratch_t*>(p); }

Mp4Recorder::Mp4Recorder() = default;

Mp4Recorder::~Mp4Recorder()
{
    if (m_recording.load())
        stop();
}

bool Mp4Recorder::start(uint32_t width, uint32_t height, int fps, const std::string& path)
{
    if (m_recording.load())
        return false;

    // minih264 requires dimensions to be multiples of 16
    uint32_t enc_w = (width  + 15) & ~15u;
    uint32_t enc_h = (height + 15) & ~15u;

    m_width    = width;
    m_height   = height;
    m_fps      = fps;
    m_mp4_path = path;

    // ---- open output file ----
    m_mp4_file = boost::nowide::fopen(path.c_str(), "wb");
    if (!m_mp4_file) {
        BOOST_LOG_TRIVIAL(error) << "Mp4Recorder: cannot open " << path;
        return false;
    }

    // ---- minimp4 muxer ----
    m_mux = MP4E_open(0, 0, m_mp4_file, mp4_write_cb);
    if (!m_mux) {
        BOOST_LOG_TRIVIAL(error) << "Mp4Recorder: MP4E_open failed";
        fclose(m_mp4_file); m_mp4_file = nullptr;
        return false;
    }
    auto* wr = new mp4_h26x_writer_t();
    memset(wr, 0, sizeof(mp4_h26x_writer_t));
    if (MP4E_STATUS_OK != mp4_h26x_write_init(wr, mux_ptr(m_mux), enc_w, enc_h, 0)) {
        BOOST_LOG_TRIVIAL(error) << "Mp4Recorder: mp4_h26x_write_init failed";
        MP4E_close(mux_ptr(m_mux)); m_mux = nullptr;
        delete wr;
        fclose(m_mp4_file); m_mp4_file = nullptr;
        return false;
    }
    m_mp4_writer = wr;

    // ---- minih264 encoder ----
    H264E_create_param_t create_param;
    memset(&create_param, 0, sizeof(create_param));
    create_param.width  = enc_w;
    create_param.height = enc_h;
    create_param.gop    = fps * 2;
    create_param.const_input_flag = 1;
#if H264E_MAX_THREADS
    create_param.max_threads = 0;
#endif

    int sizeof_persist = 0, sizeof_scratch = 0;
    if (H264E_STATUS_SUCCESS != H264E_sizeof(&create_param, &sizeof_persist, &sizeof_scratch)) {
        BOOST_LOG_TRIVIAL(error) << "Mp4Recorder: H264E_sizeof failed";
        mp4_h26x_write_close(writer_ptr(m_mp4_writer));
        MP4E_close(mux_ptr(m_mux)); m_mux = nullptr;
        delete writer_ptr(m_mp4_writer); m_mp4_writer = nullptr;
        fclose(m_mp4_file); m_mp4_file = nullptr;
        return false;
    }

    m_h264_enc     = malloc(sizeof_persist);
    m_h264_scratch = malloc(sizeof_scratch);
    if (H264E_STATUS_SUCCESS != H264E_init(enc_ptr(m_h264_enc), &create_param)) {
        BOOST_LOG_TRIVIAL(error) << "Mp4Recorder: H264E_init failed";
        free(m_h264_enc); m_h264_enc = nullptr;
        free(m_h264_scratch); m_h264_scratch = nullptr;
        mp4_h26x_write_close(writer_ptr(m_mp4_writer));
        MP4E_close(mux_ptr(m_mux)); m_mux = nullptr;
        delete writer_ptr(m_mp4_writer); m_mp4_writer = nullptr;
        fclose(m_mp4_file); m_mp4_file = nullptr;
        return false;
    }

    // ---- YUV420P buffer (padded to enc_w x enc_h) ----
    size_t y_size  = static_cast<size_t>(enc_w) * enc_h;
    size_t uv_size = (enc_w / 2) * (enc_h / 2);
    m_yuv_buf.resize(y_size + uv_size * 2, 0);
    m_yuv_planes[0]  = m_yuv_buf.data();
    m_yuv_planes[1]  = m_yuv_buf.data() + y_size;
    m_yuv_planes[2]  = m_yuv_buf.data() + y_size + uv_size;
    m_yuv_strides[0] = enc_w;
    m_yuv_strides[1] = enc_w / 2;
    m_yuv_strides[2] = enc_w / 2;

    // RGBA -> YUV420P is done inline by rgba_to_yuv420p_flipped() in the
    // anonymous namespace above; nothing to allocate here. We keep the
    // (width, height) we'll pass into the converter on the worker thread
    // via m_width/m_height.

    m_stop_requested.store(false);
    m_recording.store(true);
    m_thread = std::thread(&Mp4Recorder::encode_thread_func, this);

    BOOST_LOG_TRIVIAL(info) << "Mp4Recorder: started " << width << "x" << height
                            << " (enc " << enc_w << "x" << enc_h << ")"
                            << " @" << fps << "fps -> " << path;
    return true;
}

void Mp4Recorder::push_frame(const uint8_t* rgba_data, size_t size)
{
    if (!m_recording.load())
        return;

    std::vector<uint8_t> copy(rgba_data, rgba_data + size);
    {
        std::lock_guard<std::mutex> lk(m_queue_mutex);
        if (m_frame_queue.size() < 30) {
            m_frame_queue.push(std::move(copy));
            BOOST_LOG_TRIVIAL(debug) << "Mp4Recorder::push_frame: queued, queue_size=" << m_frame_queue.size();
        }
    }
    m_queue_cv.notify_one();
}

void Mp4Recorder::stop()
{
    if (!m_recording.load())
        return;

    m_stop_requested.store(true);
    m_queue_cv.notify_one();

    if (m_thread.joinable())
        m_thread.join();

    m_recording.store(false);
    BOOST_LOG_TRIVIAL(info) << "Mp4Recorder: stopped -> " << m_mp4_path;
}

void Mp4Recorder::encode_thread_func()
{
    int frame_count = 0;

    while (true) {
        std::vector<uint8_t> frame_data;
        {
            std::unique_lock<std::mutex> lk(m_queue_mutex);
            m_queue_cv.wait(lk, [this] {
                return !m_frame_queue.empty() || m_stop_requested.load();
            });
            if (m_frame_queue.empty() && m_stop_requested.load())
                break;
            if (!m_frame_queue.empty()) {
                frame_data = std::move(m_frame_queue.front());
                m_frame_queue.pop();
            }
        }

        if (!frame_data.empty()) {
            encode_one_frame(frame_data);
            ++frame_count;
        }
    }

    BOOST_LOG_TRIVIAL(info) << "Mp4Recorder: encoded " << frame_count << " frames, finalizing...";

    // Finalize
    if (m_mp4_writer) {
        mp4_h26x_write_close(writer_ptr(m_mp4_writer));
        delete writer_ptr(m_mp4_writer);
        m_mp4_writer = nullptr;
    }
    if (m_mux) {
        MP4E_close(mux_ptr(m_mux));
        m_mux = nullptr;
    }
    if (m_mp4_file) {
        fclose(m_mp4_file);
        m_mp4_file = nullptr;
    }
    if (m_h264_enc)     { free(m_h264_enc);     m_h264_enc     = nullptr; }
    if (m_h264_scratch) { free(m_h264_scratch); m_h264_scratch = nullptr; }
    m_yuv_buf.clear();
}

void Mp4Recorder::encode_one_frame(const std::vector<uint8_t>& rgba)
{
    // minih264 padded dimensions
    uint32_t enc_w = (m_width  + 15) & ~15u;
    uint32_t enc_h = (m_height + 15) & ~15u;

    // RGBA (bottom-to-top GL) -> YUV420P with vertical flip, all in C++.
    // Only the m_width x m_height area of the (potentially padded to a
    // 16-aligned enc_w x enc_h) YUV plane is written; the surrounding
    // padding stays zero-initialised black, which the H.264 encoder is
    // happy with for tiny right/bottom borders.
    const int rgba_stride = static_cast<int>(m_width) * 4;
    rgba_to_yuv420p_flipped(rgba.data(), rgba_stride,
                            m_yuv_planes[0], m_yuv_strides[0],
                            m_yuv_planes[1], m_yuv_strides[1],
                            m_yuv_planes[2], m_yuv_strides[2],
                            static_cast<int>(m_width),
                            static_cast<int>(m_height));

    // Encode with minih264
    H264E_run_param_t run_param;
    memset(&run_param, 0, sizeof(run_param));
    run_param.encode_speed = 10;    // fastest
    run_param.frame_type   = 0;     // auto (GOP controlled)
    run_param.desired_frame_bytes = m_width * m_height * 4 / (8 * m_fps);
    run_param.qp_min = 10;
    run_param.qp_max = 40;

    H264E_io_yuv_t yuv;
    yuv.yuv[0]    = m_yuv_planes[0];
    yuv.yuv[1]    = m_yuv_planes[1];
    yuv.yuv[2]    = m_yuv_planes[2];
    yuv.stride[0] = m_yuv_strides[0];
    yuv.stride[1] = m_yuv_strides[1];
    yuv.stride[2] = m_yuv_strides[2];

    unsigned char* coded_data = nullptr;
    int            coded_size = 0;
    int rc = H264E_encode(enc_ptr(m_h264_enc), scratch_ptr(m_h264_scratch),
                          &run_param, &yuv, &coded_data, &coded_size);
    if (rc != H264E_STATUS_SUCCESS || !coded_data || coded_size <= 0) {
        BOOST_LOG_TRIVIAL(warning) << "Mp4Recorder: H264E_encode failed rc=" << rc;
        return;
    }

    BOOST_LOG_TRIVIAL(debug) << "Mp4Recorder: encoded frame, nalu_size=" << coded_size;

    // Write Annex-B NALUs to minimp4
    const unsigned ts_delta = 90000 / m_fps;
    mp4_h26x_write_nal(writer_ptr(m_mp4_writer), coded_data, coded_size, ts_delta);
}

} // namespace GUI
} // namespace Slic3r