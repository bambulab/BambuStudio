#include "WindowShadow.hpp"

#include <wx/window.h>
#include <wx/toplevel.h>
#include <wx/gdicmn.h>

#ifdef __WIN32__
#include <wx/msw/wrapwin.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#endif

namespace Slic3r::GUI {

#ifdef __APPLE__
void mac_enable_window_shadow(void *nsview); // WindowShadowMac.mm
#endif

#ifdef __WIN32__
namespace {
// Separable box blur over a single-channel coverage buffer; 3 passes ≈ Gaussian.
void box_blur(std::vector<float> &buf, int W, int H, int r)
{
    if (r < 1) return;
    std::vector<float> tmp((size_t) W * H);
    const float        norm = 1.f / (2 * r + 1);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            float s = 0.f;
            for (int k = -r; k <= r; ++k) s += buf[(size_t) y * W + std::clamp(x + k, 0, W - 1)];
            tmp[(size_t) y * W + x] = s * norm;
        }
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            float s = 0.f;
            for (int k = -r; k <= r; ++k) s += tmp[(size_t) std::clamp(y + k, 0, H - 1) * W + x];
            buf[(size_t) y * W + x] = s * norm;
        }
}

// Mark the rounded-rect silhouette (coverage 1.0) inside the padded buffer.
void fill_round_rect(std::vector<float> &cov, int W, int H, int x0, int y0, int w, int h, int r)
{
    const int x1 = x0 + w, y1 = y0 + h;
    for (int y = std::max(0, y0); y < std::min(H, y1); ++y)
        for (int x = std::max(0, x0); x < std::min(W, x1); ++x) {
            int dx = 0, dy = 0;
            if (x < x0 + r && y < y0 + r) { dx = x0 + r - x; dy = y0 + r - y; }
            else if (x >= x1 - r && y < y0 + r) { dx = x - (x1 - r - 1); dy = y0 + r - y; }
            else if (x < x0 + r && y >= y1 - r) { dx = x0 + r - x; dy = y - (y1 - r - 1); }
            else if (x >= x1 - r && y >= y1 - r) { dx = x - (x1 - r - 1); dy = y - (y1 - r - 1); }
            if (dx * dx + dy * dy > r * r) continue;
            cov[(size_t) y * W + x] = 1.f;
        }
}

// Box-blur radius whose 3-pass cascade approximates a Gaussian of the given sigma.
int blur_radius_for_sigma(int sigma) { return std::max(1, (int) std::lround((std::sqrt(1.0 + 4.0 * (double) sigma * sigma) - 1.0) / 2.0)); }
} // namespace

struct WindowShadow::Impl
{
    HWND    hwnd  = nullptr;
    HBITMAP dib   = nullptr;
    void   *bits  = nullptr;
    int     dib_w = 0, dib_h = 0;
    int     last_w = -1, last_h = -1;
    bool    rendered = false;

    ~Impl()
    {
        if (dib) ::DeleteObject(dib);
        if (hwnd) ::DestroyWindow(hwnd);
    }

    void ensure_created();
    bool ensure_dib(int W, int H);
    void render(int w, int h, int margin, int blur_r, int off_y, double opacity, int radius);
    void show(HWND card, const wxRect &rect, int sigma, int off_y, double opacity, int radius);
    void hide() { if (hwnd) ::ShowWindow(hwnd, SW_HIDE); }
};

void WindowShadow::Impl::ensure_created()
{
    if (hwnd) return;
    HINSTANCE   inst       = ::GetModuleHandle(nullptr);
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc   = {sizeof(WNDCLASSEXW)};
        wc.lpfnWndProc   = ::DefWindowProcW;
        wc.hInstance     = inst;
        wc.lpszClassName = L"Slic3rWindowShadow";
        ::RegisterClassExW(&wc);
        registered = true;
    }
    // Click-through (WS_EX_TRANSPARENT), never-activated layered popup, kept out of the taskbar.
    hwnd = ::CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"Slic3rWindowShadow", L"", WS_POPUP, 0, 0, 0, 0, nullptr,
                             nullptr, inst, nullptr);
}

bool WindowShadow::Impl::ensure_dib(int W, int H)
{
    if (dib && dib_w == W && dib_h == H) return true;
    if (dib) { ::DeleteObject(dib); dib = nullptr; bits = nullptr; }
    BITMAPINFO bi              = {};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = W;
    bi.bmiHeader.biHeight      = -H; // top-down
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC screen                 = ::GetDC(nullptr);
    dib                        = ::CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ::ReleaseDC(nullptr, screen);
    dib_w = W;
    dib_h = H;
    return dib != nullptr;
}

void WindowShadow::Impl::render(int w, int h, int margin, int blur_r, int off_y, double opacity, int radius)
{
    const int W = w + 2 * margin, H = h + 2 * margin;
    if (!ensure_dib(W, H)) return;

    std::vector<float> cov((size_t) W * H, 0.f);
    fill_round_rect(cov, W, H, margin, margin + off_y, w, h, radius);
    box_blur(cov, W, H, blur_r);
    box_blur(cov, W, H, blur_r);
    box_blur(cov, W, H, blur_r);

    uint8_t *px = static_cast<uint8_t *>(bits);
    for (size_t i = 0; i < (size_t) W * H; ++i) {
        const uint8_t a = static_cast<uint8_t>(std::lround(std::min(1.f, cov[i]) * opacity * 255.0));
        px[i * 4 + 0]   = 0; // premultiplied black, BGRA (RGB stay 0)
        px[i * 4 + 1]   = 0;
        px[i * 4 + 2]   = 0;
        px[i * 4 + 3]   = a;
    }
    last_w   = w;
    last_h   = h;
    rendered = true;
}

void WindowShadow::Impl::show(HWND card, const wxRect &rect, int sigma, int off_y, double opacity, int radius)
{
    ensure_created();
    if (!hwnd) return;

    const int blur_r = blur_radius_for_sigma(sigma);
    const int off    = off_y < 0 ? -off_y : off_y;
    const int margin = 3 * blur_r + off + 2; // contain the full blur spread so no hard edge shows
    if (!(rendered && rect.width == last_w && rect.height == last_h)) render(rect.width, rect.height, margin, blur_r, off_y, opacity, radius);
    if (!dib) return;

    POINT         dst    = {rect.x - margin, rect.y - margin};
    SIZE          sz     = {rect.width + 2 * margin, rect.height + 2 * margin};
    POINT         src    = {0, 0};
    BLENDFUNCTION bf     = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    HDC           screen = ::GetDC(nullptr);
    HDC           mem    = ::CreateCompatibleDC(screen);
    HGDIOBJ       old    = ::SelectObject(mem, dib);
    ::UpdateLayeredWindow(hwnd, screen, &dst, &sz, mem, &src, 0, &bf, ULW_ALPHA);
    ::SelectObject(mem, old);
    ::DeleteDC(mem);
    ::ReleaseDC(nullptr, screen);

    ::ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    ::SetWindowPos(hwnd, card, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE); // sit directly beneath the card
}
#endif // __WIN32__

WindowShadow::WindowShadow(const WindowShadowParams &params) : m_params(params) {}

WindowShadow::~WindowShadow()
{
#ifdef __WIN32__
    delete m_impl;
#endif
}

void WindowShadow::Sync(wxWindow *target, bool shown)
{
    if (target == nullptr) return;
#if defined(__WIN32__)
    if (!shown) {
        if (m_impl) m_impl->hide();
        return;
    }
    if (m_impl == nullptr) m_impl = new Impl();
    m_impl->show((HWND) target->GetHWND(), target->GetScreenRect(), target->FromDIP(m_params.sigma), target->FromDIP(m_params.offset_y), m_params.opacity,
                 target->FromDIP(m_params.radius));
#elif defined(__APPLE__)
    if (shown) mac_enable_window_shadow((void *) target->GetHandle());
#else
    (void) shown;
#endif
}

void WindowShadow::Invalidate()
{
#ifdef __WIN32__
    if (m_impl) m_impl->rendered = false;
#endif
}

void WindowShadow::Attach(wxTopLevelWindow *win, const WindowShadowParams &params)
{
    if (win == nullptr) return;
    auto *shadow = new WindowShadow(params);
    win->Bind(wxEVT_SHOW, [shadow, win](wxShowEvent &e) {
        shadow->Sync(win, e.IsShown());
        e.Skip();
    });
    win->Bind(wxEVT_MOVE, [shadow, win](wxMoveEvent &e) {
        if (win->IsShown()) shadow->Sync(win, true);
        e.Skip();
    });
    win->Bind(wxEVT_SIZE, [shadow, win](wxSizeEvent &e) {
        if (win->IsShown()) shadow->Sync(win, true);
        e.Skip();
    });
    // Tie the shadow's lifetime to the window; ignore child-destroy events bubbling up.
    win->Bind(wxEVT_DESTROY, [shadow, win](wxWindowDestroyEvent &e) {
        if (e.GetEventObject() == win) delete shadow;
        e.Skip();
    });
}

} // namespace Slic3r::GUI
