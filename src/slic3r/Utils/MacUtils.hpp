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

// 让弹出窗口能跟随用户进入原生全屏的 Space。
// 弹窗是 NSPopUpMenuWindowLevel 的 NSPanel，层级远高于主窗口的 0，但窗口只在自己所属的
// Space 上绘制，跨 Space 时层级不起作用。弹窗在它的 ComboBox 构造时就创建好，绑定在当时
// 的 Space 上，而之后 toggleFullScreen: 会把主窗口搬到新建的 Space，弹窗留在原地，用户
// 就看不见了。macOS 13.4.1 实测：主窗口全屏后在 Space 467，下拉框仍在 Space 1。
// window 为 wxWindow::GetHandle() 返回的 NSView*，内部取其所属 NSWindow。
// 新版 macOS 会自行迁移这类面板，在那些系统上本调用不改变行为。
void mac_popup_follow_active_space(void * window);

#endif

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_MacUtils_hpp_
