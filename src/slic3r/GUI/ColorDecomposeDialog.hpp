#ifndef slic3r_ColorDecomposeDialog_hpp_
#define slic3r_ColorDecomposeDialog_hpp_

#include <array>
#include <string>
#include <vector>
#include <utility>
#include <wx/colour.h>
#include <wx/panel.h>
#include <wx/stattext.h>

#include "GUI_Utils.hpp"
#include "libslic3r/ColorDecomposeRecipe.hpp"

class Button;
class CheckBox;
class ComboBox;

namespace Slic3r {
namespace GUI {

using DecomposeMode = ColorDecomposeRecipeMode;

enum class DecomposeBaseColor {
    None,
    Cyan,
    Magenta,
    Yellow,
    White,
    Red,
    Green,
    Blue
};

struct DecomposeComponent {
    wxColour           colour;
    int                ratio{50};  // percentage
    int                filament_index{-1}; // 1-based physical filament index, -1 if standard base color
    DecomposeBaseColor base_color{DecomposeBaseColor::None};
};

struct ColorDecomposeResult {
    DecomposeMode                  mode{DecomposeMode::MaterialList};
    wxColour                       matched_color;
    std::vector<DecomposeComponent> components;
};

class ColorDecomposeDialog : public DPIDialog
{
public:
    ColorDecomposeDialog(wxWindow* parent,
                         int filament_idx,
                         const wxColour& target_color,
                         const std::vector<std::string>& physical_colors,
                         const std::vector<std::string>& filament_names,
                         const std::vector<std::string>& filament_types);

    ColorDecomposeResult get_result() const { return m_result; }

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;

private:
    void build_ui();
    wxBoxSizer* create_filament_selector();
    wxBoxSizer* create_target_color_section();
    wxBoxSizer* create_mode_selection_section();
    wxPanel*    create_mode_card(wxWindow* parent, DecomposeMode mode, const wxString& title);
    wxBoxSizer* create_button_panel();

    void select_mode(DecomposeMode mode);
    void update_card_styles();
    void update_card_visibility();
    void update_mode_card_content(DecomposeMode mode);
    void update_mode_card_contents();
    void update_matched_color_display();
    void update_ok_button_state();

    void compute_decomposition();

    struct ModeCardControls {
        wxPanel*    card{nullptr};
        wxBoxSizer* components_sizer{nullptr};
    };

    ColorDecomposeResult        m_result;
    std::array<ColorDecomposeResult, 3> m_mode_results;
    std::array<ModeCardControls, 3>     m_mode_cards;
    int                         m_filament_idx{-1};
    wxColour                    m_target_color;
    std::vector<std::string>    m_physical_colors;
    std::vector<std::string>    m_filament_names;
    std::vector<std::string>    m_filament_types;
    std::vector<std::string>    m_project_types;
    std::string                 m_preferred_type;
    // Dropdown selectable item index -> material type string
    std::vector<std::string>    m_combo_item_types;

    // UI controls
    ComboBox*                   m_type_combo{nullptr};
    wxPanel*                    m_target_swatch{nullptr};
    wxStaticText*               m_target_rgb_text{nullptr};
    wxPanel*                    m_matched_swatch{nullptr};
    wxStaticText*               m_matched_rgb_text{nullptr};

    // Mode cards
    wxPanel*                    m_card_material_list{nullptr};
    wxPanel*                    m_card_cmyw{nullptr};
    wxPanel*                    m_card_rybw{nullptr};
    wxPanel*                    m_arb_column_panel{nullptr};
    CheckBox*                   m_chk_material_list{nullptr};
    CheckBox*                   m_chk_cmyw{nullptr};
    CheckBox*                   m_chk_rybw{nullptr};
    DecomposeMode               m_selected_mode{DecomposeMode::MaterialList};

    // Hint shown when no mode card is visible
    wxStaticText*               m_no_card_hint{nullptr};

    Button*                     m_btn_ok{nullptr};
    Button*                     m_btn_cancel{nullptr};
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_ColorDecomposeDialog_hpp_
