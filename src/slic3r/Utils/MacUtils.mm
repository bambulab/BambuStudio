#import "MacUtils.hpp"

#import <Cocoa/Cocoa.h>

#include <objc/runtime.h>

namespace Slic3r {
namespace GUI {

// ---------------------------------------------------------------------------
// 标题栏双击缩放失效的绕过（macOS 27.2 起的 AppKit bug）
//
// 触发条件：窗口设置了 titlebarAppearsTransparent = YES（见 MacDarkMode.mm 的
//       set_miniaturizable()，主窗口在那里把标题栏设成透明）。
// 现象：双击事件本身正常送达（clickCount == 2），但 AppKit 内部「标题栏双击 ->
//       performZoom:」的派发丢失，performZoom: / zoom: 从未被调用。
// 对策：在窗口层接管标题栏区域的双击，自己按系统偏好执行对应动作。
//
// 生效范围：install_titlebar_zoom_workaround() 里已按 macOS >= 27.2 做了下限判断，
// 低版本系统完全不安装。待系统修复后在同一处补上限（< 修复版本）即可整体关掉，
// 避免与恢复后的系统行为重复。
// ---------------------------------------------------------------------------

static void (*s_orig_send_event)(id, SEL, NSEvent *) = nullptr;

// AppleActionOnDoubleClick 的全部取值：Fill / Maximize（即「缩放」）/ Minimize / None
static void perform_titlebar_double_click_action(NSWindow * win)
{
    NSString * action = [[NSUserDefaults standardUserDefaults] stringForKey:@"AppleActionOnDoubleClick"];
    if (action == nil)
        action = @"Maximize";

    if ([action isEqualToString:@"None"])
        return;

    if ([action isEqualToString:@"Minimize"]) {
        [win miniaturize:nil];
        return;
    }

    if ([action isEqualToString:@"Fill"]) {
        SEL zoom_fill = NSSelectorFromString(@"_zoomFill:");
        if ([win respondsToSelector:zoom_fill]) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-performSelector-leaks"
            [win performSelector:zoom_fill withObject:nil];
#pragma clang diagnostic pop
            return;
        }

        NSScreen * scr = win.screen ? win.screen : [NSScreen mainScreen];
        if (scr == nil)
            return;
        // 开了平铺边距的用户要留出同样的空隙，否则窗口贴边与系统行为不一致
        NSUserDefaults * wm     = [[NSUserDefaults alloc] initWithSuiteName:@"com.apple.WindowManager"];
        CGFloat          margin = [wm boolForKey:@"EnableTiledWindowMargins"] ? 8.0 : 0.0;
        [wm release];
        [win setFrame:NSInsetRect(scr.visibleFrame, margin, margin) display:YES animate:YES];
        return;
    }

    [win zoom:nil]; // "Maximize"
}

static void bs_send_event(id self_, SEL _cmd, NSEvent * event)
{
    // 热路径：sendEvent: 承接本窗口的全部事件（鼠标移动、拖拽、滚轮、键盘），
    // 所以先用最便宜、筛除率最高的事件类型挡住绝大多数调用，窗口属性留到最后再问。
    // 注意顺序也关系到正确性：NSEvent.clickCount 只对鼠标类事件有效，必须由前面的
    // type 判断短路保护。
    if (event.type != NSEventTypeLeftMouseDown || event.clickCount != 2) {
        s_orig_send_event(self_, _cmd, event);
        return;
    }

    NSWindow * win = (NSWindow *) self_;
    // 只在触发条件成立时介入：透明标题栏 + 可缩放，不会误伤普通窗口
    if (win.titlebarAppearsTransparent && (win.styleMask & NSWindowStyleMaskResizable)) {
        CGFloat titlebar_h = NSHeight(win.frame) - NSHeight(win.contentLayoutRect);
        if (titlebar_h <= 0)
            titlebar_h = 28; // fullSizeContentView 下 contentLayoutRect 等于整窗，兜底
        if (event.locationInWindow.y > NSHeight(win.frame) - titlebar_h) {
            perform_titlebar_double_click_action(win);
            return;
        }
    }
    s_orig_send_event(self_, _cmd, event);
}

void install_titlebar_zoom_workaround(void * window)
{
    // 只在出问题的系统上安装。低于该版本的 macOS，AppKit 自己的标题栏双击派发是好的，
    // 没必要插手；系统修复后在这里补一个上限（&& 低于修复版本）就能整体关掉本绕过。
    // 用 isOperatingSystemAtLeastVersion: 而非 @available，后者要求编译用的 SDK
    // 认识这个版本号，旧 SDK 下会编不过。
    static const NSOperatingSystemVersion k_broken_since = {27, 2, 0};
    if (![[NSProcessInfo processInfo] isOperatingSystemAtLeastVersion:k_broken_since])
        return;

    if (window == nullptr)
        return;
    NSWindow * win = [(NSView *) window window];
    if (win == nil)
        return;

    static dispatch_once_t once;
    dispatch_once(&once, ^{
        Class  cls = [win class]; // wx 下是 wxNSWindow，不会波及 NSWindow 本身
        Method m   = class_getInstanceMethod(cls, @selector(sendEvent:));
        if (m == NULL)
            return;
        // 该类自己没实现 sendEvent: 时先补一个同名方法，避免改到父类实现、影响所有窗口
        if (class_addMethod(cls, @selector(sendEvent:), (IMP) bs_send_event, method_getTypeEncoding(m)))
            s_orig_send_event = (void (*)(id, SEL, NSEvent *)) method_getImplementation(m);
        else
            s_orig_send_event = (void (*)(id, SEL, NSEvent *)) method_setImplementation(m, (IMP) bs_send_event);
    });
}

} // namespace GUI
} // namespace Slic3r
