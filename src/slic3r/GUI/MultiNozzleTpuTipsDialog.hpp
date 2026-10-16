#ifndef slic3r_GUI_MultiNozzleTpuTipsDialog_hpp_
#define slic3r_GUI_MultiNozzleTpuTipsDialog_hpp_

#include <string>

#include "GUI_Utils.hpp"

class wxStaticBitmap;
class Label;
class Button;
class CheckBox;

namespace Slic3r {

namespace GUI {

// TPU usage notes shown when a filament slot is switched to TPU on a single-extruder multi-nozzle printer (e.g. H3C).
class MultiNozzleTpuTipsDialog : public DPIDialog
{
public:
    explicit MultiNozzleTpuTipsDialog(wxWindow *parent);

    // True when picking new_preset for a filament slot should show the dialog.
    static bool should_show(const std::string &new_preset);

protected:
    void on_dpi_changed(const wxRect &suggested_rect) override;

private:
    void save_dont_show_choice();

    wxStaticBitmap *m_image{nullptr};
    wxStaticBitmap *m_warn_icon{nullptr};
    Label          *m_tips_label{nullptr};
    ::CheckBox     *m_dont_show_checkbox{nullptr};
    Button         *m_guide_btn{nullptr};
    Button         *m_confirm_btn{nullptr};
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_MultiNozzleTpuTipsDialog_hpp_
