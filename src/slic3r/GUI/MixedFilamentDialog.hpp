#ifndef slic3r_MixedFilamentDialog_hpp_
#define slic3r_MixedFilamentDialog_hpp_

#include <array>
#include <functional>
#include <string>
#include <utility>
#include <vector>
#include <wx/bitmap.h>
#include <wx/panel.h>
#include <wx/tglbtn.h>
#include <wx/stattext.h>

#include "GUI_Utils.hpp"
#include "libslic3r/FilamentMixer.hpp"

class Button;
class CheckBox;
class ComboBox;
class ScalableButton;
class wxMouseEvent;
class wxScrolledWindow;
class wxTextCtrl;
class wxWrapSizer;

namespace Slic3r {
namespace GUI {

class GradientCurveEditor;
class RatioLabelPanel;

struct MixedFilamentResult {
    std::vector<unsigned int> components;   // 1-based physical filament indices
    std::vector<int>          ratios;       // percentages, sum = 100
    bool gradient_enabled   = false;
    int  gradient_direction = 0;            // 0 = A→B, 1 = B→A  (only for 2-color)
    bool per_part_gradient  = false;        // valid only when gradient_enabled == true
    // Optional Photoshop-style custom curve overriding the linear A→B gradient.
    // Empty -> use linear (gradient_direction). Non-empty -> cubic Hermite over [0,1]^2
    // with optional per-anchor tangent overrides (see GradientAnchor).
    std::vector<GradientAnchor> gradient_curve;
};

// Physical filament slots offered by the dialog, in the order of the 1-based component indices.
struct MixedFilamentPhysicalInfo {
    std::vector<std::string> colors;
    std::vector<std::string> names;
    std::vector<std::string> types;
    std::vector<std::string> diameters;
    std::vector<std::string> volume_shorts;
    std::vector<size_t>      config_indices;
};

// Re-reads the project's physical filaments, e.g. after the official mixing kit was added
// while the dialog is open.
using MixedFilamentPhysicalLoader = std::function<MixedFilamentPhysicalInfo()>;

class MixedFilamentDialog : public DPIDialog
{
public:
    // Add mode: offers the "Manual Mixing" and "Color Matching" tabs.
    // `initial` seeds the manual tab; it is ignored when it does not fit the current physical filaments.
    MixedFilamentDialog(wxWindow* parent,
                        MixedFilamentPhysicalLoader physical_loader,
                        bool enforce_nozzle_diameter = false,
                        const MixedFilamentResult& initial = {});

    MixedFilamentDialog(wxWindow* parent,
                        const MixedFilamentResult& existing,
                        const std::vector<std::string>& physical_colors,
                        const std::vector<std::string>& physical_names,
                        const std::vector<std::string>& physical_types = {},
                        const std::vector<std::string>& physical_diameters = {},
                        const std::vector<std::string>& physical_volume_shorts = {},
                        bool enforce_nozzle_diameter = false);

    MixedFilamentResult get_result() const { return m_result; }

    bool is_color_match() const { return m_match_mode; }
    // Project config index of the color-matching target filament, -1 outside color matching.
    int  match_target_config_index() const;
    // Whether the user asked to replace the target filament on the models with the new mix.
    bool replace_target_on_model() const;

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;

private:
    void build_ui();
    wxBoxSizer* create_preview_panel();
    wxBoxSizer* create_material_selection();
    wxBoxSizer* create_ratio_slider();
    wxBoxSizer* create_triangle_picker();
    wxBoxSizer* create_gradient_section();
    wxBoxSizer* create_recommendation_grid();
    wxBoxSizer* create_button_panel();

    void on_filament_changed();
    void on_ratio_changed(int new_ratio_a);
    void on_gradient_toggled();
    void on_gradient_direction_changed();
    void on_gradient_curve_changed();
    void on_per_part_gradient_toggled();
    void on_add_material();
    void on_remove_material();
    void on_recommendation_clicked(unsigned int comp_a, unsigned int comp_b);
    void on_recommendation_clicked_triple(unsigned int a, unsigned int b, unsigned int c);
    void apply_manual_ratio(size_t idx, int value);
    void apply_dragged_triangle_ratio(int r0, int r1, int r2);
    void reset_manual_ratio_state();
    void refresh_ratio_labels();
    void sync_triangle_weights_from_ratios();
    void start_ratio_editor(size_t idx, wxWindow* anchor, const wxRect& anchor_rect);
    void commit_ratio_editor(bool apply);
    void commit_ratio_editor_from_background(wxMouseEvent& e);
    void update_preview();
    void update_ok_button_state();
    void update_gradient_direction_items();
    void update_component_count_ui();
    // Picks dialog (width, height) based on current state so the gradient curve
    // editor, recommendation list, and filament combo labels stay visible.
    wxSize compute_dialog_size() const;
    wxString combo_item_name(size_t physical_idx) const;
    wxString combo_item_suffix(size_t physical_idx) const;
    int measure_filament_label_width() const;
    int measure_suffix_column_width() const;
    int measure_needed_combo_width() const;
    void refresh_combo_suffixes();
    // keep_top: keep the top edge in place instead of re-centering (used by tab switching).
    void apply_dialog_size(bool keep_top = false);
    void rebuild_all_combos();
    void rebuild_recommendation_items();
    void refresh_curve_editor_colors();
    void paint_warning_panel(wxPaintEvent& evt);
    wxString current_warning_message() const;
    // Grows the warning panel so every wrapped line of the current message fits.
    void update_warning_panel_height();

    wxBitmap make_swatch_bitmap(size_t idx);

    // Reserves the same width on every material row label so the combo boxes line up.
    static void apply_uniform_label_width(wxStaticText* lbl);
    // Appends one "Filament N" label + combo row to m_material_rows_sizer.  N follows the
    // number of rows already there, so callers must not renumber anything themselves.
    void append_material_row();
    void remove_last_material_row();

    // ---- Color matching tab ----
    wxBoxSizer* create_mode_tabs();
    wxBoxSizer* create_match_header();
    wxBoxSizer* create_replace_target_option();
    void set_physical_info(const MixedFilamentPhysicalInfo& info);
    void select_mode(bool match_mode);
    void update_mode_ui();
    void update_rematch_button();
    void update_tab_styles();
    void rebuild_target_combo();
    void on_target_changed();
    void on_use_official_kit();
    void set_match_status(const wxString& text);
    // Fills m_result from the best recipe for the selected target color.
    void run_color_match();
    // Replaces m_result and resyncs every control (rows, ratios, gradient) with it.
    void apply_result(const MixedFilamentResult& result);
    wxColour match_target_colour() const;
    wxColour matched_colour() const;

    // Helpers for component/ratio access
    size_t          num_components() const { return m_result.components.size(); }
    unsigned int    comp(size_t i) const { return (i < m_result.components.size()) ? m_result.components[i] : 1; }
    int             ratio(size_t i) const { return (i < m_result.ratios.size()) ? m_result.ratios[i] : 0; }
    wxColour        comp_colour(size_t i) const;

    MixedFilamentResult         m_result;
    bool                        m_edit_mode{false};
    std::vector<std::string>    m_physical_colors;
    std::vector<std::string>    m_physical_names;
    std::vector<std::string>    m_physical_types;
    std::vector<std::string>    m_physical_diameters;
    std::vector<std::string>    m_physical_volume_shorts;
    bool                        m_enforce_nozzle_diameter{false};
    bool                        m_can_confirm{false};
    wxString                    m_type_mismatch_msg;
    wxString                    m_match_error_msg;

    // Combo item index -> 1-based physical filament index (per combo)
    std::vector<std::vector<unsigned int>> m_combo_to_physical;

    // UI controls
    wxPanel*                    m_preview_canvas{nullptr};
    wxPanel*                    m_summary_panel{nullptr};
    std::vector<ComboBox*>      m_combo_filaments;
    wxBoxSizer*                 m_material_rows_sizer{nullptr};
    wxPanel*                    m_ratio_bar{nullptr};
    wxPanel*                    m_triangle_panel{nullptr};
    RatioLabelPanel*            m_label_ratio_a{nullptr};
    RatioLabelPanel*            m_label_ratio_b{nullptr};
    wxPanel*                    m_ratio_editor_panel{nullptr};
    wxTextCtrl*                 m_ratio_editor{nullptr};
    CheckBox*                   m_chk_gradient{nullptr};
    wxStaticText*               m_label_gradient{nullptr};
    ComboBox*                   m_combo_gradient_dir{nullptr};
    wxBoxSizer*                 m_gradient_sizer{nullptr};
    GradientCurveEditor*        m_curve_editor{nullptr};
    wxBoxSizer*                 m_curve_sizer{nullptr};
    CheckBox*                   m_chk_per_part_gradient{nullptr};
    wxStaticText*               m_label_per_part_gradient{nullptr};
    wxBoxSizer*                 m_per_part_gradient_sizer{nullptr};
    Button*                     m_btn_add_material{nullptr};
    Button*                     m_btn_remove_material{nullptr};
    Button*                     m_btn_ok{nullptr};
    Button*                     m_btn_cancel{nullptr};
    wxBoxSizer*                 m_warning_sizer{nullptr};
    wxPanel*                    m_warning_panel{nullptr};

    wxBoxSizer*                 m_ratio_sizer{nullptr};
    wxBoxSizer*                 m_triangle_sizer{nullptr};
    wxBoxSizer*                 m_right_sizer{nullptr};

    wxScrolledWindow*           m_recommendation_scroll{nullptr};
    wxWrapSizer*                m_recommendation_grid{nullptr};
    // Inputs the current recommendation swatches were built from.
    std::string                 m_recommendation_key;

    // Cached preview bitmaps (loaded once at construction)
    wxBitmap                    m_preview_bmp_two;
    wxBitmap                    m_preview_bmp_three;

    // Drag state
    bool   m_dragging{false};
    std::vector<size_t> m_ratio_manual_order;
    size_t m_ratio_editor_idx{0};
    bool   m_ratio_editor_committing{false};
    wxWindow* m_ratio_editor_anchor{nullptr};
    // Triangle picker drag point (barycentric weights)
    double m_tri_wx{0.333}, m_tri_wy{0.333}, m_tri_wz{0.334};

    // Cached triangle color bitmap (invalidated when colors or size change)
    wxBitmap m_tri_cache_bmp;
    wxColour m_tri_cache_c0, m_tri_cache_c1, m_tri_cache_c2;
    wxSize   m_tri_cache_size;
    std::array<RatioLabelPanel*, 3> m_triangle_ratio_labels{nullptr, nullptr, nullptr};

    // ---- Color matching tab ----
    MixedFilamentPhysicalLoader m_physical_loader;
    std::vector<size_t>         m_physical_config_indices;
    // Series-aware types ("PLA Basic" / "PLA Palette" / "PETG Basic" / plain type) per slot.
    std::vector<std::string>    m_match_types;
    // Slots that may be picked as the target (opaque single-color filaments).
    std::vector<bool>           m_match_target_allowed;
    // Slots omitted from the target list entirely (support filaments).
    std::vector<bool>           m_match_target_hidden;
    bool                        m_match_mode{false};
    // While switching tabs, resizing is postponed until the target page is fully laid out.
    bool                        m_defer_dialog_resize{false};
    bool                        m_dialog_resize_pending{false};
    bool                        m_match_valid{false};
    bool                        m_has_match_result{false};
    MixedFilamentResult         m_manual_result;
    MixedFilamentResult         m_match_result;
    MixedFilamentResult         m_match_recommended;
    int                         m_match_target{-1}; // 0-based physical index
    std::vector<int>            m_target_combo_to_physical;

    Button*                     m_btn_tab_manual{nullptr};
    Button*                     m_btn_tab_match{nullptr};
    wxBoxSizer*                 m_mode_tabs_sizer{nullptr};
    wxBoxSizer*                 m_match_header_sizer{nullptr};
    wxBoxSizer*                 m_match_empty_sizer{nullptr};
    wxBoxSizer*                 m_content_sizer{nullptr};
    wxPanel*                    m_target_preview_canvas{nullptr};
    ComboBox*                   m_combo_target{nullptr};
    ScalableButton*             m_btn_rematch{nullptr};
    wxStaticText*               m_lbl_official_kit{nullptr};
    wxStaticText*               m_lbl_wiki{nullptr};
    wxStaticText*               m_match_status{nullptr};
    wxStaticText*               m_sel_label{nullptr};
    wxBoxSizer*                 m_recommendation_sizer{nullptr};
    wxBoxSizer*                 m_replace_sizer{nullptr};
    CheckBox*                   m_chk_replace_target{nullptr};
    wxStaticText*               m_lbl_replace_target{nullptr};
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_MixedFilamentDialog_hpp_
