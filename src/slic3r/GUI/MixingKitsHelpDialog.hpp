#ifndef slic3r_MixingKitsHelpDialog_hpp_
#define slic3r_MixingKitsHelpDialog_hpp_

#include "ColorDecomposeSupport.hpp"
#include "GUI_Utils.hpp"

#include <utility>
#include <vector>
#include <wx/colour.h>

class Button;
class Label;

namespace Slic3r {
namespace GUI {

class MixingKitsHelpDialog : public DPIDialog
{
public:
    explicit MixingKitsHelpDialog(wxWindow* parent, const OfficialMixingKitAddPlan& plan = {});

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;

private:
    void build_ui();
    wxWindow* create_capacity_warning(wxWindow* parent);
    void wrap_body_labels();
    wxWindow* create_kit_card(wxWindow* parent,
                              const std::vector<wxColour>& colors,
                              const wxString& title,
                              const wxString& subtitle);

    Label*  m_intro{nullptr};
    Label*  m_closing{nullptr};
    Label*  m_capacity_warning{nullptr};
    Button* m_btn_add{nullptr};
    Button* m_btn_close{nullptr};
    std::vector<std::pair<wxWindow*, Label*>> m_kit_subtitles;
    OfficialMixingKitAddPlan m_plan;
};

} // namespace GUI
} // namespace Slic3r

#endif
