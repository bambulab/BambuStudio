#ifndef slic3r_GLGizmoSimplifyDialog_hpp_
#define slic3r_GLGizmoSimplifyDialog_hpp_

#include "slic3r/GUI/GUI_Utils.hpp"

#include <wx/panel.h>
#include <wx/string.h>

class wxSizer;
class wxSlider;
class wxGauge;
class wxStaticText;

class Button;
class CheckBox;
class ComboBox;
class TextInput;

namespace Slic3r::GUI {

class GLGizmoSimplify;
class LevelSlider;
class WrappedText;

// Everything the dialog shows. Filled in by GLGizmoSimplify, the dialog does not read the gizmo.
struct SimplifyDialogView
{
    wxString mesh_name;
    size_t   triangle_count = 0;
    wxString result_count;       // "-" while the preview does not match the current settings
    bool     use_count      = false;
    int      detail_level   = 2;
    float    decimate_ratio = 50.f;
    bool     show_wireframe = false;
    bool     painted        = false; // shows the keep-painting checkbox and its warning
    bool     preserve_color = false;
    bool     worker_running = false;
    bool     cancelling     = false;
    bool     apply_enabled  = false;
    int      progress       = 0;
};

// Non-modal dialog for the Simplify gizmo. Created and destroyed by the gizmo.
class SimplifyDialog : public DPIDialog
{
public:
    explicit SimplifyDialog(GLGizmoSimplify &owner);

    // Sets every control from the view and lays the dialog out. Events raised by this call are ignored.
    void update(const SimplifyDialogView &view);
    // Only the progress bar and its percentage, without a new layout (called for each percent).
    void update_progress(int progress);
    // The gizmo is going away: no more calls into it, whatever events are still pending.
    void detach() { m_owner = nullptr; }

private:
    void create_controls();
    // Sizes that are given in design px and depend on the DPI.
    void apply_sizes();
    // Re-wraps the texts and sets the window to the design width and the height of the shown rows.
    void relayout();
    void on_close(wxCloseEvent &evt);
    void on_char_hook(wxKeyEvent &evt);
    void on_dpi_changed(const wxRect &suggested_rect) override;

    GLGizmoSimplify *m_owner;
    bool             m_updating = false;

    wxStaticText *m_method_label  = nullptr;
    ComboBox   *m_method_combo    = nullptr;
    wxPanel    *m_block           = nullptr;
    wxSizer    *m_detail_row      = nullptr; // slider row of the detail level method
    wxSizer    *m_ratio_row       = nullptr; // slider row of the decimate ratio method
    LevelSlider *m_detail_slider  = nullptr;
    wxStaticText *m_detail_caption = nullptr;
    wxSlider   *m_ratio_slider    = nullptr;
    TextInput  *m_ratio_input     = nullptr;
    wxStaticText *m_ratio_unit    = nullptr;
    wxStaticText *m_result_label  = nullptr;
    CheckBox   *m_wireframe_box   = nullptr;
    CheckBox   *m_preserve_box    = nullptr;
    wxSizer    *m_options         = nullptr; // column with the options (wireframe, painting, warning)
    wxSizer    *m_preserve_row    = nullptr; // checkbox and label of "Keep all painting"
    WrappedText *m_preserve_hint  = nullptr; // what keeping the painting does
    WrappedText *m_warning        = nullptr;
    wxGauge    *m_gauge           = nullptr;
    wxStaticText *m_gauge_text    = nullptr;
    Button     *m_apply_button    = nullptr;
    Button     *m_cancel_button   = nullptr;
    wxStaticText *m_mesh_name_value = nullptr;
    wxStaticText *m_triangle_value  = nullptr;
};

} // namespace Slic3r::GUI

#endif // slic3r_GLGizmoSimplifyDialog_hpp_
