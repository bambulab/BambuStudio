#ifndef slic3r_MacDarkMode_hpp_
#define slic3r_MacDarkMode_hpp_

#include <wx/event.h>

namespace Slic3r {
namespace GUI {

#if __APPLE__
extern bool mac_dark_mode();
extern double mac_max_scaling_factor();
extern void set_miniaturizable(void * window);
void WKWebView_evaluateJavaScript(void * web, wxString const & script, void (*callback)(wxString const &));
void WKWebView_setTransparentBackground(void * web);
// 允许通过 Safari「开发」菜单远程审查该 WKWebView。macOS 13.3 / Safari 16.4 起
// WKWebView 默认不可审查，必须显式 setInspectable:YES，否则 EnableAccessToDevTools 无效。
void WKWebView_setInspectable(void * web, bool enable);
void WKWebView_clearBambulabTokenCookies();
// 为指定 WKWebView 注册 Web Content 进程崩溃回调。
// 进程终止时在主线程调用 callback(context)。传 nullptr callback 表示注销。
void WKWebView_setCrashHandler(void* web, void (*callback)(void*), void* context);
void set_tag_when_enter_full_screen(bool isfullscreen);
void set_title_colour_after_set_title(void * window);
void initGestures(void * view,  wxEvtHandler * handler);
void openFolderForFile(wxString const & file);
void StaticGroup_layoutBadge(void * group, void * badge);
#endif


} // namespace GUI
} // namespace Slic3r

#endif // MacDarkMode_h
