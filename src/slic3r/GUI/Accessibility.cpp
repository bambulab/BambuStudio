#include "Accessibility.hpp"

#if wxUSE_ACCESSIBILITY

#include <wx/tglbtn.h>
#include "I18N.hpp"
#include "Widgets/Button.hpp"

// ---------------------------------------------------------------------------
// ButtonAccessible
// ---------------------------------------------------------------------------

ButtonAccessible::ButtonAccessible(wxWindow* win)
    : wxAccessible(win)
{}

wxAccStatus ButtonAccessible::GetRole(int childId, wxAccRole* role)
{
    if (childId == wxACC_SELF) {
        *role = wxROLE_SYSTEM_PUSHBUTTON;
        return wxACC_OK;
    }
    return wxACC_NOT_IMPLEMENTED;
}

wxAccStatus ButtonAccessible::GetName(int childId, wxString* name)
{
    if (childId == wxACC_SELF) {
        wxWindow* win = GetWindow();
        *name = win ? win->GetLabel() : wxString();
        if (name->empty()) {
            // fall back to tooltip text so icon-only buttons still have a name
            *name = win ? win->GetToolTipText() : wxString();
        }
        return wxACC_OK;
    }
    return wxACC_NOT_IMPLEMENTED;
}

wxAccStatus ButtonAccessible::GetState(int childId, long* state)
{
    if (childId == wxACC_SELF) {
        wxWindow* win = GetWindow();
        *state = wxACC_STATE_SYSTEM_FOCUSABLE;
        if (win) {
            if (!win->IsEnabled())
                *state |= wxACC_STATE_SYSTEM_UNAVAILABLE;
            if (win->HasFocus())
                *state |= wxACC_STATE_SYSTEM_FOCUSED;
        }
        return wxACC_OK;
    }
    return wxACC_NOT_IMPLEMENTED;
}

wxAccStatus ButtonAccessible::GetDefaultAction(int childId, wxString* actionName)
{
    if (childId == wxACC_SELF) {
        *actionName = _("Press");
        return wxACC_OK;
    }
    return wxACC_NOT_IMPLEMENTED;
}

wxAccStatus ButtonAccessible::DoDefaultAction(int childId)
{
    if (childId == wxACC_SELF) {
        wxWindow* win = GetWindow();
        if (win) {
            wxCommandEvent evt(wxEVT_COMMAND_BUTTON_CLICKED, win->GetId());
            evt.SetEventObject(win);
            win->GetEventHandler()->ProcessEvent(evt);
        }
        return wxACC_OK;
    }
    return wxACC_NOT_IMPLEMENTED;
}

wxAccStatus ButtonAccessible::GetDescription(int childId, wxString* description)
{
    if (childId == wxACC_SELF) {
        wxWindow* win = GetWindow();
        *description = win ? win->GetToolTipText() : wxString();
        return wxACC_OK;
    }
    return wxACC_NOT_IMPLEMENTED;
}

// ---------------------------------------------------------------------------
// ToggleAccessible
// ---------------------------------------------------------------------------

ToggleAccessible::ToggleAccessible(wxWindow* win, wxAccRole role)
    : wxAccessible(win)
    , m_role(role)
{}

wxAccStatus ToggleAccessible::GetRole(int childId, wxAccRole* role)
{
    if (childId == wxACC_SELF) {
        *role = m_role;
        return wxACC_OK;
    }
    return wxACC_NOT_IMPLEMENTED;
}

wxAccStatus ToggleAccessible::GetName(int childId, wxString* name)
{
    if (childId == wxACC_SELF) {
        wxWindow* win = GetWindow();
        *name = win ? win->GetLabel() : wxString();
        if (name->empty())
            *name = win ? win->GetToolTipText() : wxString();
        return wxACC_OK;
    }
    return wxACC_NOT_IMPLEMENTED;
}

wxAccStatus ToggleAccessible::GetState(int childId, long* state)
{
    if (childId == wxACC_SELF) {
        wxWindow* win = GetWindow();
        *state = wxACC_STATE_SYSTEM_FOCUSABLE;
        if (win) {
            if (!win->IsEnabled())
                *state |= wxACC_STATE_SYSTEM_UNAVAILABLE;
            if (win->HasFocus())
                *state |= wxACC_STATE_SYSTEM_FOCUSED;

            // read checked state from the underlying toggle button
            auto* toggle = dynamic_cast<wxToggleButton*>(win);
            if (toggle && toggle->GetValue())
                *state |= wxACC_STATE_SYSTEM_CHECKED;
        }
        return wxACC_OK;
    }
    return wxACC_NOT_IMPLEMENTED;
}

wxAccStatus ToggleAccessible::GetDefaultAction(int childId, wxString* actionName)
{
    if (childId == wxACC_SELF) {
        wxWindow* win = GetWindow();
        auto* toggle = win ? dynamic_cast<wxToggleButton*>(win) : nullptr;
        if (toggle)
            *actionName = toggle->GetValue() ? _("Uncheck") : _("Check");
        else
            *actionName = _("Press");
        return wxACC_OK;
    }
    return wxACC_NOT_IMPLEMENTED;
}

wxAccStatus ToggleAccessible::DoDefaultAction(int childId)
{
    if (childId == wxACC_SELF) {
        wxWindow* win = GetWindow();
        auto* toggle = win ? dynamic_cast<wxToggleButton*>(win) : nullptr;
        if (toggle) {
            toggle->SetValue(!toggle->GetValue());
            wxCommandEvent evt(wxEVT_TOGGLEBUTTON, win->GetId());
            evt.SetEventObject(win);
            evt.SetInt(toggle->GetValue() ? 1 : 0);
            win->GetEventHandler()->ProcessEvent(evt);
        }
        return wxACC_OK;
    }
    return wxACC_NOT_IMPLEMENTED;
}

// ---------------------------------------------------------------------------
// TabButtonAccessible
// ---------------------------------------------------------------------------

TabButtonAccessible::TabButtonAccessible(wxWindow* win)
    : wxAccessible(win)
{}

wxAccStatus TabButtonAccessible::GetRole(int childId, wxAccRole* role)
{
    if (childId == wxACC_SELF) {
        *role = wxROLE_SYSTEM_PAGETAB;
        return wxACC_OK;
    }
    return wxACC_NOT_IMPLEMENTED;
}

wxAccStatus TabButtonAccessible::GetName(int childId, wxString* name)
{
    if (childId == wxACC_SELF) {
        wxWindow* win = GetWindow();
        // strip leading space added by ButtonsListCtrl::InsertPage
        *name = win ? win->GetLabel().Trim(false).Trim(true) : wxString();
        return wxACC_OK;
    }
    return wxACC_NOT_IMPLEMENTED;
}

wxAccStatus TabButtonAccessible::GetState(int childId, long* state)
{
    if (childId == wxACC_SELF) {
        wxWindow* win = GetWindow();
        *state = wxACC_STATE_SYSTEM_FOCUSABLE | wxACC_STATE_SYSTEM_SELECTABLE;
        if (win) {
            if (!win->IsEnabled())
                *state |= wxACC_STATE_SYSTEM_UNAVAILABLE;
            if (win->HasFocus())
                *state |= wxACC_STATE_SYSTEM_FOCUSED;
            // Check the Button's selected (active-tab) state via GetValue()
            auto* btn = dynamic_cast<Button*>(win);
            if (btn && btn->GetValue())
                *state |= wxACC_STATE_SYSTEM_SELECTED;
        }
        return wxACC_OK;
    }
    return wxACC_NOT_IMPLEMENTED;
}

wxAccStatus TabButtonAccessible::GetDefaultAction(int childId, wxString* actionName)
{
    if (childId == wxACC_SELF) {
        *actionName = _("Switch");
        return wxACC_OK;
    }
    return wxACC_NOT_IMPLEMENTED;
}

// ---------------------------------------------------------------------------
// TextCtrlLabelAccessible
// ---------------------------------------------------------------------------

TextCtrlLabelAccessible::TextCtrlLabelAccessible(wxWindow* win, const wxString& label)
    : wxAccessible(win)
    , m_label(label)
{}

wxAccStatus TextCtrlLabelAccessible::GetRole(int childId, wxAccRole* role)
{
    if (childId == wxACC_SELF) {
        *role = wxROLE_SYSTEM_TEXT;
        return wxACC_OK;
    }
    return wxACC_NOT_IMPLEMENTED;
}

wxAccStatus TextCtrlLabelAccessible::GetName(int childId, wxString* name)
{
    if (childId == wxACC_SELF) {
        *name = m_label;
        return wxACC_OK;
    }
    return wxACC_NOT_IMPLEMENTED;
}

wxAccStatus TextCtrlLabelAccessible::GetState(int childId, long* state)
{
    if (childId == wxACC_SELF) {
        wxWindow* win = GetWindow();
        *state = wxACC_STATE_SYSTEM_FOCUSABLE;
        if (win) {
            if (!win->IsEnabled())
                *state |= wxACC_STATE_SYSTEM_UNAVAILABLE;
            if (win->HasFocus())
                *state |= wxACC_STATE_SYSTEM_FOCUSED;
        }
        return wxACC_OK;
    }
    return wxACC_NOT_IMPLEMENTED;
}

wxAccStatus TextCtrlLabelAccessible::GetValue(int childId, wxString* value)
{
    if (childId == wxACC_SELF) {
        wxWindow* win = GetWindow();
        // Return the current text content as the value
        *value = win ? win->GetLabel() : wxString();
        return wxACC_OK;
    }
    return wxACC_NOT_IMPLEMENTED;
}

// ---------------------------------------------------------------------------
// PrintOptionItemAccessible
// ---------------------------------------------------------------------------

PrintOptionItemAccessible::PrintOptionItemAccessible(wxWindow* win)
    : wxAccessible(win)
{}

wxAccStatus PrintOptionItemAccessible::GetRole(int childId, wxAccRole* role)
{
    if (childId == wxACC_SELF) {
        *role = wxROLE_SYSTEM_RADIOBUTTON;
        return wxACC_OK;
    }
    return wxACC_NOT_IMPLEMENTED;
}

wxAccStatus PrintOptionItemAccessible::GetName(int childId, wxString* name)
{
    if (childId == wxACC_SELF) {
        wxWindow* win = GetWindow();
        // The tooltip is set to "Title: option1 / option2 / ..."
        *name = win ? win->GetToolTipText() : wxString();
        return wxACC_OK;
    }
    return wxACC_NOT_IMPLEMENTED;
}

wxAccStatus PrintOptionItemAccessible::GetState(int childId, long* state)
{
    if (childId == wxACC_SELF) {
        wxWindow* win = GetWindow();
        *state = wxACC_STATE_SYSTEM_FOCUSABLE;
        if (win) {
            if (!win->IsEnabled())
                *state |= wxACC_STATE_SYSTEM_UNAVAILABLE;
            if (win->HasFocus())
                *state |= wxACC_STATE_SYSTEM_FOCUSED;
        }
        return wxACC_OK;
    }
    return wxACC_NOT_IMPLEMENTED;
}

wxAccStatus PrintOptionItemAccessible::GetValue(int childId, wxString* value)
{
    if (childId == wxACC_SELF) {
        wxWindow* win = GetWindow();
        // The label is updated to the selected option text on each selection change.
        *value = win ? win->GetLabel() : wxString();
        return wxACC_OK;
    }
    return wxACC_NOT_IMPLEMENTED;
}

// ---------------------------------------------------------------------------
// ValueButtonAccessible
// ---------------------------------------------------------------------------

ValueButtonAccessible::ValueButtonAccessible(wxWindow* win, const wxString& name)
    : wxAccessible(win), m_name(name)
{}

wxAccStatus ValueButtonAccessible::GetRole(int childId, wxAccRole* role)
{
    *role = wxROLE_SYSTEM_PUSHBUTTON;
    return wxACC_OK;
}

wxAccStatus ValueButtonAccessible::GetName(int childId, wxString* name)
{
    *name = m_name;
    return wxACC_OK;
}

wxAccStatus ValueButtonAccessible::GetState(int childId, long* state)
{
    *state = GetWindow()->IsEnabled() ? wxACC_STATE_SYSTEM_FOCUSABLE
                                      : wxACC_STATE_SYSTEM_UNAVAILABLE;
    return wxACC_OK;
}

wxAccStatus ValueButtonAccessible::GetValue(int childId, wxString* value)
{
    *value = GetWindow()->GetToolTipText();
    return wxACC_OK;
}

// ---------------------------------------------------------------------------
// ComboBoxAccessible
// ---------------------------------------------------------------------------

ComboBoxAccessible::ComboBoxAccessible(wxWindow* win)
    : wxAccessible(win)
{}

wxAccStatus ComboBoxAccessible::GetRole(int childId, wxAccRole* role)
{
    if (childId == wxACC_SELF) {
        *role = wxROLE_SYSTEM_COMBOBOX;
        return wxACC_OK;
    }
    return wxACC_NOT_IMPLEMENTED;
}

wxAccStatus ComboBoxAccessible::GetName(int childId, wxString* name)
{
    if (childId == wxACC_SELF) {
        // Use the tooltip as the static accessible name (e.g. "Printer").
        // The label changes on every selection, so it's used as the value.
        wxWindow* win = GetWindow();
        *name = win ? win->GetToolTipText() : wxString();
        return wxACC_OK;
    }
    return wxACC_NOT_IMPLEMENTED;
}

wxAccStatus ComboBoxAccessible::GetState(int childId, long* state)
{
    if (childId == wxACC_SELF) {
        wxWindow* win = GetWindow();
        *state = wxACC_STATE_SYSTEM_FOCUSABLE;
        if (win) {
            if (!win->IsEnabled())
                *state |= wxACC_STATE_SYSTEM_UNAVAILABLE;
            if (win->HasFocus())
                *state |= wxACC_STATE_SYSTEM_FOCUSED;
        }
        return wxACC_OK;
    }
    return wxACC_NOT_IMPLEMENTED;
}

wxAccStatus ComboBoxAccessible::GetValue(int childId, wxString* value)
{
    if (childId == wxACC_SELF) {
        wxWindow* win = GetWindow();
        // ComboBox stores current text in the window label via SetLabel()
        *value = win ? win->GetLabel() : wxString();
        return wxACC_OK;
    }
    return wxACC_NOT_IMPLEMENTED;
}

// ---------------------------------------------------------------------------
// ProgressBarAccessible
// ---------------------------------------------------------------------------

ProgressBarAccessible::ProgressBarAccessible(wxWindow* win)
    : wxAccessible(win)
{}

wxAccStatus ProgressBarAccessible::GetRole(int childId, wxAccRole* role)
{
    if (childId == wxACC_SELF) {
        *role = wxROLE_SYSTEM_PROGRESSBAR;
        return wxACC_OK;
    }
    return wxACC_NOT_IMPLEMENTED;
}

wxAccStatus ProgressBarAccessible::GetName(int childId, wxString* name)
{
    if (childId == wxACC_SELF) {
        wxWindow* win = GetWindow();
        *name = win ? win->GetLabel() : wxString();
        if (name->empty())
            *name = _("Progress");
        return wxACC_OK;
    }
    return wxACC_NOT_IMPLEMENTED;
}

wxAccStatus ProgressBarAccessible::GetState(int childId, long* state)
{
    if (childId == wxACC_SELF) {
        wxWindow* win = GetWindow();
        *state = wxACC_STATE_SYSTEM_READONLY;
        if (win && !win->IsEnabled())
            *state |= wxACC_STATE_SYSTEM_UNAVAILABLE;
        return wxACC_OK;
    }
    return wxACC_NOT_IMPLEMENTED;
}

wxAccStatus ProgressBarAccessible::GetValue(int childId, wxString* value)
{
    if (childId == wxACC_SELF) {
        // ProgressBar stores m_step and m_max as public members.
        // We access them via the window label mechanism: callers should call
        // SetLabel(wxString::Format("%d%%", pct)) when updating the bar so
        // NVDA reads the new value. As a fallback we just return empty.
        wxWindow* win = GetWindow();
        *value = win ? win->GetLabel() : wxString();
        return wxACC_OK;
    }
    return wxACC_NOT_IMPLEMENTED;
}

// ---------------------------------------------------------------------------
// GLCanvasAccessible
// ---------------------------------------------------------------------------

GLCanvasAccessible::GLCanvasAccessible(wxWindow* win,
                                       const wxString& canvas_name,
                                       const wxString& description)
    : wxAccessible(win)
    , m_canvas_name(canvas_name)
    , m_description(description)
{}

void GLCanvasAccessible::set_toolbar_items(std::vector<GLToolbarEntry> items)
{
    m_items = std::move(items);
}

wxAccStatus GLCanvasAccessible::GetRole(int childId, wxAccRole* role)
{
    if (childId == wxACC_SELF) {
        *role = wxROLE_SYSTEM_CLIENT;
        return wxACC_OK;
    }
    if (childId > 0 && childId <= (int)m_items.size()) {
        *role = m_items[childId - 1].role;
        return wxACC_OK;
    }
    return wxACC_NOT_IMPLEMENTED;
}

wxAccStatus GLCanvasAccessible::GetName(int childId, wxString* name)
{
    if (childId == wxACC_SELF) {
        *name = m_canvas_name;
        return wxACC_OK;
    }
    if (childId > 0 && childId <= (int)m_items.size()) {
        *name = m_items[childId - 1].name;
        return wxACC_OK;
    }
    return wxACC_NOT_IMPLEMENTED;
}

wxAccStatus GLCanvasAccessible::GetState(int childId, long* state)
{
    if (childId == wxACC_SELF) {
        wxWindow* win = GetWindow();
        *state = wxACC_STATE_SYSTEM_FOCUSABLE;
        if (win && win->IsEnabled())
            *state |= 0; // enabled — no extra flag needed
        return wxACC_OK;
    }
    if (childId > 0 && childId <= (int)m_items.size()) {
        *state = wxACC_STATE_SYSTEM_FOCUSABLE;
        return wxACC_OK;
    }
    return wxACC_NOT_IMPLEMENTED;
}

wxAccStatus GLCanvasAccessible::GetValue(int childId, wxString* value)
{
    if (childId > 0 && childId <= (int)m_items.size()) {
        const auto& entry = m_items[childId - 1];
        if (entry.value_fn) {
            *value = entry.value_fn();
            return wxACC_OK;
        }
    }
    return wxACC_NOT_IMPLEMENTED;
}

wxAccStatus GLCanvasAccessible::GetDescription(int childId, wxString* description)
{
    if (childId == wxACC_SELF && !m_description.IsEmpty()) {
        *description = m_description;
        return wxACC_OK;
    }
    return wxACC_NOT_IMPLEMENTED;
}

wxAccStatus GLCanvasAccessible::GetChildCount(int* childCount)
{
    *childCount = (int)m_items.size();
    return wxACC_OK;
}

wxAccStatus GLCanvasAccessible::GetChild(int childId, wxAccessible** child)
{
    // Virtual children: no separate wxAccessible object — MSAA uses parent
    // methods with the childId integer to represent them.
    if (childId == wxACC_SELF) {
        *child = this;
        return wxACC_OK;
    }
    if (childId > 0 && childId <= (int)m_items.size()) {
        *child = nullptr; // virtual child: no separate accessible object
        return wxACC_OK;
    }
    return wxACC_NOT_IMPLEMENTED;
}

wxAccStatus GLCanvasAccessible::DoDefaultAction(int childId)
{
    if (childId > 0 && childId <= (int)m_items.size()) {
        const auto& entry = m_items[childId - 1];
        if (entry.action)
            entry.action();
        return wxACC_OK;
    }
    return wxACC_NOT_IMPLEMENTED;
}

wxAccStatus GLCanvasAccessible::GetDefaultAction(int childId, wxString* actionName)
{
    if (childId > 0 && childId <= (int)m_items.size()) {
        *actionName = (m_items[childId - 1].role == wxROLE_SYSTEM_SLIDER) ? "Set value" : "Press";
        return wxACC_OK;
    }
    return wxACC_NOT_IMPLEMENTED;
}

#endif // wxUSE_ACCESSIBILITY
