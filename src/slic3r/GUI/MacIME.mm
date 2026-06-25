#include "MacIME.hpp"

#ifdef __APPLE__

#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#import <objc/message.h>

#include <utility>

// NOTE: this translation unit is compiled WITHOUT ARC (the rest of the project's
// .mm files use manual retain/release), so any object we create and return must
// be autoreleased.
//
// Why this exists: a wxGLCanvas uses a custom view class (wxNSCustomOpenGLView,
// a subclass of NSOpenGLView). wxWidgets only adds keyDown:/insertText:/
// doCommandBySelector: to that class; the NSTextInputClient "marked text"
// methods (setMarkedText:.../hasMarkedText/firstRectForCharacterRange:...) are
// implemented only as stubs on the *other* class wxNSView, which the GL canvas
// does not inherit. As a result the canvas view's -inputContext is nil and the
// macOS text system can never run a composition session over it, so CJK IME
// input silently fails while plain ASCII (legacy insertText:) keeps working.
//
// We add a full NSTextInputClient implementation to the concrete canvas class so
// composition works, and gate it through -inputContext so behavior is unchanged
// unless an imgui text field is actively requesting input. Committed text is
// still delivered through wxWidgets' existing insertText: -> wxEVT_CHAR path.

namespace {

// Shared predicate: true while an imgui text widget wants keyboard text input.
std::function<bool()> g_ime_is_active;

inline bool ime_active() { return g_ime_is_active && g_ime_is_active(); }

// Associated-object keys (their addresses are used as unique keys).
char kInstalledKey;
char kMarkedTextKey;   // NSString*           : composition string (nil == none)
char kSelRangeKey;     // NSValue*            : selected range inside marked text
char kCaretRectKey;    // NSValue*            : caret rect in screen coordinates
char kInputCtxKey;     // NSTextInputContext* : our own text input context

inline NSString *get_marked(id self)
{
    return (NSString *) objc_getAssociatedObject(self, &kMarkedTextKey);
}
inline void set_marked(id self, NSString *s)
{
    objc_setAssociatedObject(self, &kMarkedTextKey, s, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}
inline void set_sel_range(id self, NSValue *v)
{
    objc_setAssociatedObject(self, &kSelRangeKey, v, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

inline NSString *string_from(id aString)
{
    if ([aString isKindOfClass:[NSAttributedString class]])
        return [(NSAttributedString *) aString string];
    if ([aString isKindOfClass:[NSString class]])
        return (NSString *) aString;
    return nil;
}

// ---- gate ------------------------------------------------------------------

// Return a text input context only while an imgui text field is active, so that
// outside of text editing the view behaves exactly as before (nil context ==
// no composition, legacy ASCII path only).
//
// We do NOT defer to [super inputContext]: AppKit decides whether a view is a
// text-input client when the view is created, and adding NSTextInputClient
// conformance at runtime afterwards does not flip that decision, so the
// inherited NSOpenGLView/NSView -inputContext keeps returning nil and no
// composition session ever starts. Instead we lazily create and own our own
// NSTextInputContext bound to this view (the standard pattern for custom
// NSTextInputClient views, e.g. GLFW / the Dear ImGui macOS backend). The
// context is cached per-view via an associated object so the IME keeps a
// stable session across keystrokes.
NSTextInputContext *bbl_inputContext(id self, SEL)
{
    NSTextInputContext *ctx = (NSTextInputContext *) objc_getAssociatedObject(self, &kInputCtxKey);

    if (!ime_active()) {
        // Editing ended (or never started): make sure the IME is not capturing
        // plain keystrokes over the 3D canvas, then behave like a non-text view.
        if (ctx)
            [ctx deactivate];
        return nil;
    }

    if (ctx == nil) {
        ctx = [[NSTextInputContext alloc] initWithClient:(id<NSTextInputClient>) self];
        // The associated object takes its own +1 retain; release our alloc ref.
        objc_setAssociatedObject(self, &kInputCtxKey, ctx, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        [ctx release];
    }
    // The canvas is already first responder before editing begins, so AppKit
    // never activated this lazily-created context for us; do it explicitly so
    // -[NSTextInputContext handleEvent:] (driven by wx's interpretKeyEvents:)
    // routes keystrokes into the current input source. activate is idempotent.
    [ctx activate];
    return ctx;
}

// ---- NSTextInputClient ------------------------------------------------------

void bbl_insertText(id self, SEL, id aString, NSRange /*replacementRange*/)
{
    set_marked(self, nil);
    set_sel_range(self, nil);

    NSString *s = string_from(aString);
    if (s == nil || s.length == 0)
        return;
    // Reuse wxWidgets' own insertText: (single colon, installed on the view)
    // so the committed text flows through the normal wxEVT_CHAR pipeline.
    ((void (*)(id, SEL, id)) objc_msgSend)(self, @selector(insertText:), s);
}

void bbl_setMarkedText(id self, SEL, id aString, NSRange selectedRange, NSRange /*replacementRange*/)
{
    NSString *s = string_from(aString);
    if (s == nil || s.length == 0) {
        set_marked(self, nil);
        set_sel_range(self, nil);
        return;
    }
    set_marked(self, s);
    set_sel_range(self, [NSValue valueWithRange:selectedRange]);
}

void bbl_unmarkText(id self, SEL)
{
    // The committed string arrives separately via insertText:, so just drop the
    // in-progress composition state here.
    set_marked(self, nil);
    set_sel_range(self, nil);
}

BOOL bbl_hasMarkedText(id self, SEL)
{
    NSString *m = get_marked(self);
    return (m && m.length > 0) ? YES : NO;
}

NSRange bbl_markedRange(id self, SEL)
{
    NSString *m = get_marked(self);
    if (m && m.length > 0)
        return NSMakeRange(0, m.length);
    return NSMakeRange(NSNotFound, 0);
}

NSRange bbl_selectedRange(id self, SEL)
{
    NSValue *v = (NSValue *) objc_getAssociatedObject(self, &kSelRangeKey);
    if (v)
        return v.rangeValue;
    return NSMakeRange(NSNotFound, 0);
}

NSRect bbl_firstRect(id self, SEL, NSRange aRange, NSRangePointer actualRange)
{
    if (actualRange)
        *actualRange = aRange;

    NSValue *v = (NSValue *) objc_getAssociatedObject(self, &kCaretRectKey);
    if (v)
        return v.rectValue;

    // Fallback: anchor at the top-left of the view so the candidate window at
    // least appears near the canvas instead of off-screen.
    NSView *view = (NSView *) self;
    NSWindow *win = view.window;
    if (win == nil)
        return NSMakeRect(0, 0, 0, 0);
    NSRect inWin = [view convertRect:NSMakeRect(0, 0, 1, 16) toView:nil];
    return [win convertRectToScreen:inWin];
}

NSAttributedString *bbl_attributedSubstring(id self, SEL, NSRange aRange, NSRangePointer actualRange)
{
    NSString *m = get_marked(self);
    if (m == nil || m.length == 0)
        return nil;
    NSRange clamped = NSIntersectionRange(aRange, NSMakeRange(0, m.length));
    if (clamped.length == 0)
        return nil;
    if (actualRange)
        *actualRange = clamped;
    return [[[NSAttributedString alloc] initWithString:[m substringWithRange:clamped]] autorelease];
}

NSArray *bbl_validAttributes(id, SEL)
{
    return @[];
}

NSUInteger bbl_characterIndexForPoint(id, SEL, NSPoint)
{
    return NSNotFound;
}

} // anonymous namespace

namespace Slic3r { namespace GUI {

void mac_ime_install(void *ns_view, std::function<bool()> is_active)
{
    if (ns_view == nullptr)
        return;

    // Same predicate for every canvas; refresh it on each (re)install.
    g_ime_is_active = std::move(is_active);

    id view = (id) ns_view;

    // Use -class (KVO-transparent) and add the missing NSTextInputClient methods
    // *directly onto that class*. We deliberately do NOT isa-swizzle the live
    // view (object_setClass) because mutating the class of an in-use NSOpenGLView
    // fights with AppKit/KVO and can crash later during layout.
    Class cls = [(NSView *) view class];
    if (cls == nil)
        return;

    // Patch each class only once.
    if (objc_getAssociatedObject(cls, &kInstalledKey) != nil)
        return;
    objc_setAssociatedObject(cls, &kInstalledKey, @YES, OBJC_ASSOCIATION_RETAIN_NONATOMIC);

    // Declare conformance so -[NSView inputContext] recognizes the view as a
    // text input client.
    class_addProtocol(cls, @protocol(NSTextInputClient));

    // ObjC type encodings, resolved by the compiler for this platform.
    NSString *encBOOL  = @(@encode(BOOL));
    NSString *encRange = @(@encode(NSRange));
    NSString *encRect  = @(@encode(NSRect));
    NSString *encRPtr  = @(@encode(NSRangePointer));
    NSString *encPoint = @(@encode(NSPoint));
    NSString *encUInt  = @(@encode(NSUInteger));

    auto add = [&](SEL sel, IMP imp, NSString *types) {
        // class_addMethod is a no-op if the class already implements `sel`; none
        // of the selectors below are defined on the GL canvas view class.
        class_addMethod(cls, sel, imp, [types UTF8String]);
    };
    add(@selector(inputContext), (IMP) bbl_inputContext, @"@@:");
    add(@selector(insertText:replacementRange:), (IMP) bbl_insertText,
        [NSString stringWithFormat:@"v@:@%@", encRange]);
    add(@selector(setMarkedText:selectedRange:replacementRange:), (IMP) bbl_setMarkedText,
        [NSString stringWithFormat:@"v@:@%@%@", encRange, encRange]);
    add(@selector(unmarkText), (IMP) bbl_unmarkText, @"v@:");
    add(@selector(hasMarkedText), (IMP) bbl_hasMarkedText,
        [NSString stringWithFormat:@"%@@:", encBOOL]);
    add(@selector(markedRange), (IMP) bbl_markedRange,
        [NSString stringWithFormat:@"%@@:", encRange]);
    add(@selector(selectedRange), (IMP) bbl_selectedRange,
        [NSString stringWithFormat:@"%@@:", encRange]);
    add(@selector(firstRectForCharacterRange:actualRange:), (IMP) bbl_firstRect,
        [NSString stringWithFormat:@"%@@:%@%@", encRect, encRange, encRPtr]);
    add(@selector(attributedSubstringForProposedRange:actualRange:), (IMP) bbl_attributedSubstring,
        [NSString stringWithFormat:@"@@:%@%@", encRange, encRPtr]);
    add(@selector(validAttributesForMarkedText), (IMP) bbl_validAttributes, @"@@:");
    add(@selector(characterIndexForPoint:), (IMP) bbl_characterIndexForPoint,
        [NSString stringWithFormat:@"%@@:%@", encUInt, encPoint]);
}

void mac_ime_set_caret(void *ns_view, int x, int y, int height)
{
    if (ns_view == nullptr)
        return;
    NSView   *view = (NSView *) ns_view;
    NSWindow *win  = view.window;
    if (win == nil)
        return;

    CGFloat scale = win.backingScaleFactor;
    if (scale <= 0)
        scale = 1.0;

    // imgui coordinates are backing pixels with a top-left origin; convert to
    // device-independent points and build the caret rect in the view's (flipped)
    // coordinate space, then map to screen coordinates. convertRect:toView:nil
    // accounts for the view's flippedness automatically.
    CGFloat px = (CGFloat) x / scale;
    CGFloat py = (CGFloat) y / scale;
    CGFloat h  = (height > 0) ? (CGFloat) height / scale : 16.0;

    NSRect inWin    = [view convertRect:NSMakeRect(px, py, 1.0, h) toView:nil];
    NSRect inScreen = [win convertRectToScreen:inWin];
    objc_setAssociatedObject(view, &kCaretRectKey, [NSValue valueWithRect:inScreen], OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

} } // namespace Slic3r::GUI

#endif // __APPLE__
