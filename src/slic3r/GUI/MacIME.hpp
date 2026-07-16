#ifndef slic3r_GUI_MacIME_hpp_
#define slic3r_GUI_MacIME_hpp_

// macOS only: enable CJK input-method (IME) composition over a wxGLCanvas.
//
// wxWidgets only ships stub implementations of the NSTextInputClient "marked
// text" methods (setMarkedText:/hasMarkedText/markedRange/firstRectForCharacterRange:)
// for its custom (non-native) views. A GL canvas uses such a custom view, so the
// macOS text system can never run a composition session over it and Chinese /
// Japanese / Korean IME input silently fails, while plain ASCII (which goes
// straight through insertText:) keeps working. See src/osx/cocoa/window.mm
// wxNSView(TextInput). This bridge supplies a working composition implementation
// on the concrete canvas view so the IME can compose and commit. Committed text
// is still delivered through wxWidgets' existing insertText: -> wxEVT_CHAR path.

#ifdef __APPLE__

#include <functional>

namespace Slic3r { namespace GUI {

// Install the IME composition handler on the given canvas NSView handle
// (wxWindow::GetHandle()). Idempotent: only installs once per view.
// is_active is queried on every NSTextInputClient callback; while it returns
// false the view keeps wxWidgets' original (inert) behavior, so nothing changes
// outside of an active imgui text field.
void mac_ime_install(void *ns_view, std::function<bool()> is_active);

// Update the caret rectangle used to anchor the IME candidate window.
// (x, y) is the top-left of the input cursor in imgui canvas coordinates
// (backing pixels, top-left origin); height is the line height in pixels.
// Hooked from imgui's io.ImeSetInputScreenPosFn.
void mac_ime_set_caret(void *ns_view, int x, int y, int height);

// Keep the view's text input context activated (i.e. the system's *current*
// input context) for the whole time an imgui text field is focused, and
// deactivate it otherwise. Call once per frame with want == io.WantTextInput.
//
// This is what makes the macOS Chinese/English toggle (Caps Lock, or the IME's
// internal 中/英 mode) and other modifier-only events reach the field: those
// events are delivered to the current input context, not through keyDown:, so a
// context that is only activated transiently during interpretKeyEvents: never
// receives them (a native text field / browser input keeps its context current
// while focused, which is why the toggle works there).
void mac_ime_sync_active(void *ns_view, bool want);

} } // namespace Slic3r::GUI

#endif // __APPLE__

#endif // slic3r_GUI_MacIME_hpp_
