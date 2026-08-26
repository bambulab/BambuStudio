#pragma once

class wxWindow;
class wxTopLevelWindow;

namespace Slic3r::GUI {

/// Appearance of a WindowShadow. Lengths are in DIP (scaled to the target window's
/// DPI when the shadow is painted). The defaults are a soft, neutral ambient shadow
/// (also the parameter-tooltip look, Figma node 6949:2771).
struct WindowShadowParams
{
    int    sigma    = 6;    ///< Gaussian blur std-dev; larger is softer with a wider penumbra.
    int    offset_y = 0;    ///< Vertical drop offset; 0 is a symmetric ambient shadow.
    int    radius   = 4;    ///< Corner radius of the shadowed window, so the shadow hugs its shape.
    double opacity  = 0.24; ///< Shadow alpha at full coverage, in [0, 1] (black).
};

/// A soft drop shadow for a **top-level** window (popup, frame, or dialog).
///
/// The shadow is a separate, click-through, per-pixel-alpha window painted just
/// beneath the target in the z-order. It cannot live inside the target itself
/// because Win32's UpdateLayeredWindow bypasses child-control painting. Backends:
/// Windows paints the blur itself; macOS turns on the native NSWindow shadow;
/// other platforms are a no-op (the window keeps whatever shape it already has).
///
/// Only top-level windows qualify — the effect relies on a sibling window in the
/// z-order, which a child control does not have.
///
/// Two ways to use it:
///  - WindowShadow::Attach() — fire-and-forget; binds the target's show/move/size.
///  - Own an instance and call Sync()/Invalidate() yourself when you need precise
///    control over when the shadow appears (e.g. a delayed-show tooltip).
class WindowShadow
{
public:
    /// @param params Appearance; defaults to the soft ambient shadow above.
    explicit WindowShadow(const WindowShadowParams &params = {});
    ~WindowShadow();

    WindowShadow(const WindowShadow &)            = delete;
    WindowShadow &operator=(const WindowShadow &) = delete;

    /// Show the shadow behind @p target, or hide it.
    /// @param target Top-level window to shadow; its current screen rect is used.
    /// @param shown  true to show/reposition beneath @p target, false to hide.
    void Sync(wxWindow *target, bool shown);

    /// Drop the cached render so the next Sync() repaints. Call on a theme or DPI change.
    void Invalidate();

    /// Give @p win a self-managing shadow: binds its show/move/size to keep the shadow
    /// glued, and deletes itself when @p win is destroyed. Use when the window's own
    /// visibility events are enough; otherwise own a WindowShadow and drive Sync().
    /// @param win    The top-level window to shadow.
    /// @param params Appearance; defaults to the soft ambient shadow above.
    static void Attach(wxTopLevelWindow *win, const WindowShadowParams &params = {});

private:
    struct Impl; // platform state (Win32 layered window + DIB); unused on other backends
    Impl              *m_impl = nullptr;
    WindowShadowParams m_params;
};

} // namespace Slic3r::GUI
