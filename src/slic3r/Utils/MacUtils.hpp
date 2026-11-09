#ifndef slic3r_MacUtils_hpp_
#define slic3r_MacUtils_hpp_

namespace Slic3r {
namespace GUI {

#if __APPLE__

// 绕过 macOS 27.2 beta3 起的 AppKit bug：设置了 titlebarAppearsTransparent 的窗口，
// 「标题栏双击 -> performZoom:」的内部派发丢失，双击不再缩放窗口。
// 在窗口层接管标题栏区域的双击，按系统偏好 AppleActionOnDoubleClick 自行执行动作。
// window 为 wxWindow::GetHandle() 返回的 NSView*，内部取其所属 NSWindow。
// 仅在 macOS 27.2 及以上安装，低版本系统直接返回。幂等，重复调用只安装一次。
void install_titlebar_zoom_workaround(void * window);

#endif

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_MacUtils_hpp_
