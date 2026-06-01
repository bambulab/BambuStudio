#ifndef slic3r_GUI_Accessibility_hpp_
#define slic3r_GUI_Accessibility_hpp_

// NVDA / VoiceOver / Windows Narrator accessibility support for Bambu Studio
// custom widgets. All code is guarded by wxUSE_ACCESSIBILITY so it compiles
// away cleanly when the flag is not set.

#if wxUSE_ACCESSIBILITY
#include <wx/access.h>
#include <wx/window.h>

// ---------------------------------------------------------------------------
// ButtonAccessible
// Provides MSAA role "push button", name and state for the custom Button
// widget (which inherits wxWindow, not wxButton, so has no built-in MSAA).
// ---------------------------------------------------------------------------
class ButtonAccessible : public wxAccessible
{
public:
    explicit ButtonAccessible(wxWindow* win);

    wxAccStatus GetRole(int childId, wxAccRole* role) override;
    wxAccStatus GetName(int childId, wxString* name) override;
    wxAccStatus GetState(int childId, long* state) override;
    wxAccStatus GetDefaultAction(int childId, wxString* actionName) override;
    wxAccStatus DoDefaultAction(int childId) override;
    wxAccStatus GetDescription(int childId, wxString* description) override;
};

// ---------------------------------------------------------------------------
// ToggleAccessible
// Generic accessible for bitmap-only toggle buttons (CheckBox, SwitchButton,
// RadioBox) that inherit wxBitmapToggleButton. The Win32 BUTTON control is
// readable by NVDA already, but without a text label NVDA announces nothing.
// This class supplies name, role and checked-state correctly.
// ---------------------------------------------------------------------------
class ToggleAccessible : public wxAccessible
{
public:
    // role should be wxROLE_SYSTEM_CHECKBUTTON or wxROLE_SYSTEM_RADIOBUTTON
    // or wxROLE_SYSTEM_PUSHBUTTON for a generic toggle.
    ToggleAccessible(wxWindow* win, wxAccRole role);

    wxAccStatus GetRole(int childId, wxAccRole* role) override;
    wxAccStatus GetName(int childId, wxString* name) override;
    wxAccStatus GetState(int childId, long* state) override;
    wxAccStatus GetDefaultAction(int childId, wxString* actionName) override;
    wxAccStatus DoDefaultAction(int childId) override;

private:
    wxAccRole m_role;
};

// ---------------------------------------------------------------------------
// TabButtonAccessible
// Gives notebook tab buttons the correct MSAA role (PAGE_TAB) and selected
// state so NVDA announces "Prepare, tab, 1 of 4" instead of just "Prepare,
// button". Install this after Button::Create() via SetAccessible().
// ---------------------------------------------------------------------------
class TabButtonAccessible : public wxAccessible
{
public:
    explicit TabButtonAccessible(wxWindow* win);

    wxAccStatus GetRole(int childId, wxAccRole* role) override;
    wxAccStatus GetName(int childId, wxString* name) override;
    wxAccStatus GetState(int childId, long* state) override;
    wxAccStatus GetDefaultAction(int childId, wxString* actionName) override;
};

// ---------------------------------------------------------------------------
// TextCtrlLabelAccessible
// Attaches a static field-name label to an inner wxTextCtrl so NVDA says
// "Nozzle temperature: 200, edit text" instead of just "200, edit text".
// Install via text_ctrl->SetAccessible(new TextCtrlLabelAccessible(tc, name)).
// Update name at runtime by calling SetFieldLabel() when context changes.
// ---------------------------------------------------------------------------
class TextCtrlLabelAccessible : public wxAccessible
{
public:
    TextCtrlLabelAccessible(wxWindow* win, const wxString& label);

    void SetFieldLabel(const wxString& label) { m_label = label; }

    wxAccStatus GetRole(int childId, wxAccRole* role) override;
    wxAccStatus GetName(int childId, wxString* name) override;
    wxAccStatus GetState(int childId, long* state) override;
    wxAccStatus GetValue(int childId, wxString* value) override;

private:
    wxString m_label;
};

// ---------------------------------------------------------------------------
// PrintOptionItemAccessible
// Gives the custom "radio group" print-option selector an MSAA role of
// RADIOGROUP and reports the currently-selected option as its value.
// ---------------------------------------------------------------------------
class PrintOptionItemAccessible : public wxAccessible
{
public:
    explicit PrintOptionItemAccessible(wxWindow* win);

    wxAccStatus GetRole(int childId, wxAccRole* role) override;
    wxAccStatus GetName(int childId, wxString* name) override;
    wxAccStatus GetState(int childId, long* state) override;
    wxAccStatus GetValue(int childId, wxString* value) override;
};

// ---------------------------------------------------------------------------
// ValueButtonAccessible
// For toggle/switch widgets (ImageSwitchButton, FanSwitchButton) that store
// their current value in the tooltip text (set dynamically by SetLabels).
// GetName() returns the stable semantic name passed at construction;
// GetValue() returns the current tooltip text (e.g. "100%").
// NVDA reads: "Print speed: 100%, button".
// ---------------------------------------------------------------------------
class ValueButtonAccessible : public wxAccessible
{
public:
    ValueButtonAccessible(wxWindow* win, const wxString& name);

    wxAccStatus GetRole(int childId, wxAccRole* role) override;
    wxAccStatus GetName(int childId, wxString* name) override;
    wxAccStatus GetState(int childId, long* state) override;
    wxAccStatus GetValue(int childId, wxString* value) override;

private:
    wxString m_name;
};

// ---------------------------------------------------------------------------
// ComboBoxAccessible
// Gives ComboBox (TextInput-based custom widget) the correct MSAA role and
// announces value changes so NVDA reads "label: value, combo box".
// ---------------------------------------------------------------------------
class ComboBoxAccessible : public wxAccessible
{
public:
    explicit ComboBoxAccessible(wxWindow* win);

    wxAccStatus GetRole(int childId, wxAccRole* role) override;
    wxAccStatus GetName(int childId, wxString* name) override;
    wxAccStatus GetState(int childId, long* state) override;
    wxAccStatus GetValue(int childId, wxString* value) override;
};

// ---------------------------------------------------------------------------
// ProgressBarAccessible
// Exposes value (0-100) and role "progress bar" for the custom ProgressBar
// widget, which also inherits bare wxWindow.
// ---------------------------------------------------------------------------
class ProgressBarAccessible : public wxAccessible
{
public:
    // value / max are read from the ProgressBar pointer at query time.
    explicit ProgressBarAccessible(wxWindow* win);

    wxAccStatus GetRole(int childId, wxAccRole* role) override;
    wxAccStatus GetName(int childId, wxString* name) override;
    wxAccStatus GetState(int childId, long* state) override;
    wxAccStatus GetValue(int childId, wxString* value) override;
};

// ---------------------------------------------------------------------------
// GLCanvasAccessible
// Gives the OpenGL canvas a proper MSAA role, name and keyboard-shortcut
// description so NVDA/Narrator announce the 3D view correctly.
// Optionally exposes a flat list of toolbar actions as virtual MSAA children
// (role=PUSHBUTTON) that fire on DoDefaultAction.
// ---------------------------------------------------------------------------
struct GLToolbarEntry {
    wxString                  name;
    std::function<void()>     action;                            // DoDefaultAction ("Press")
    std::function<wxString()> value_fn;                         // optional: current value getter
    wxAccRole                 role = wxROLE_SYSTEM_PUSHBUTTON;  // PUSHBUTTON or SLIDER etc.
};

class GLCanvasAccessible : public wxAccessible
{
public:
    GLCanvasAccessible(wxWindow* win, const wxString& canvas_name,
                       const wxString& description = wxEmptyString);

    void set_toolbar_items(std::vector<GLToolbarEntry> items);

    wxAccStatus GetRole(int childId, wxAccRole* role) override;
    wxAccStatus GetName(int childId, wxString* name) override;
    wxAccStatus GetState(int childId, long* state) override;
    wxAccStatus GetValue(int childId, wxString* value) override;
    wxAccStatus GetDescription(int childId, wxString* description) override;
    wxAccStatus GetChildCount(int* childCount) override;
    wxAccStatus GetChild(int childId, wxAccessible** child) override;
    wxAccStatus DoDefaultAction(int childId) override;
    wxAccStatus GetDefaultAction(int childId, wxString* actionName) override;

private:
    wxString m_canvas_name;
    wxString m_description;
    std::vector<GLToolbarEntry> m_items;
};

#endif // wxUSE_ACCESSIBILITY
#endif // slic3r_GUI_Accessibility_hpp_
