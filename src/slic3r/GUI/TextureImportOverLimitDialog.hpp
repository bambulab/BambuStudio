#ifndef slic3r_GUI_TextureImportOverLimitDialog_hpp_
#define slic3r_GUI_TextureImportOverLimitDialog_hpp_

#include "TextureImportDialog.hpp"

#include <array>
#include <vector>

class Button;

namespace Slic3r {
namespace GUI {

class RadioBox;
class PreviewTagPanel;

enum class TextureOverLimitMode {
    MergeSimilar,
    DiscardSmallArea
};

struct TextureOverLimitChip {
    int                  dialog_index    = -1;
    int                  display_number  = 0;
    std::array<float, 4> rgba            = {0.f, 0.f, 0.f, 1.f};
    double               area_ratio      = 0.0;
};

struct TextureOverLimitPlan {
    std::vector<Slic3r::FilamentMatch> matches;
    std::vector<TextureOverLimitChip>  before_chips;
    std::vector<TextureOverLimitChip>  after_chips;
    std::vector<TextureOverLimitChip>  kept_chips;
    std::vector<TextureOverLimitChip>  discarded_chips;
    int  remaining_count = 0;
    bool fully_resolved  = false;
};

struct TextureOverLimitInput {
    std::vector<TextureFilamentEntry>  entries;
    std::vector<std::array<float, 4>>  colors_rgba;
    std::vector<Slic3r::FilamentMatch> matches;
    std::vector<int>                   display_numbers;
    Slic3r::PaintedMesh                painted;
    TexturePreviewCanvas::ViewState    view;
    size_t                             max_count      = 32;
};

TextureOverLimitPlan compute_texture_overlimit_merge_plan(const TextureOverLimitInput& input);
TextureOverLimitPlan compute_texture_overlimit_discard_plan(const TextureOverLimitInput& input);

class TextureImportOverLimitDialog : public DPIDialog
{
public:
    TextureImportOverLimitDialog(wxWindow* parent, TextureOverLimitInput input);

    TextureOverLimitMode selected_mode() const { return m_mode; }
    const std::vector<Slic3r::FilamentMatch>& selected_matches() const;

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;
    void on_sys_color_changed() override;

private:
    void build_ui();
    // Apply the DIP client size after the native window exists (macOS needs
    // wxEVT_SHOW). When center is true, also CenterOnParent().
    void apply_dialog_geometry(bool center);
    void apply_theme();
    void select_mode(TextureOverLimitMode mode);
    wxWindow* create_option_block(wxWindow* parent,
                                  TextureOverLimitMode mode,
                                  const wxString& title,
                                  RadioBox*& radio);
    wxPanel* create_merge_card(wxWindow* parent);
    wxPanel* create_discard_card(wxWindow* parent);
    wxWindow* create_preview_card(wxWindow* parent,
                                  const wxString& tag,
                                  TexturePreviewCanvas*& canvas,
                                  const TextureOverLimitPlan& plan);
    void style_primary_button(Button* btn);
    void style_secondary_button(Button* btn);
    void populate_preview(TexturePreviewCanvas* canvas, const TextureOverLimitPlan& plan);
    void bind_preview_cameras();
    void refresh_previews();

    TextureOverLimitInput m_input;
    TextureOverLimitPlan  m_merge_plan;
    TextureOverLimitPlan  m_discard_plan;
    TextureOverLimitMode  m_mode = TextureOverLimitMode::MergeSimilar;

    RadioBox* m_radio_merge   = nullptr;
    RadioBox* m_radio_discard = nullptr;
    Label*    m_hint_label    = nullptr;
    Label*    m_merge_status  = nullptr;
    Label*    m_discard_status = nullptr;
    wxStaticText* m_merge_title   = nullptr;
    wxStaticText* m_discard_title = nullptr;
    wxPanel*  m_merge_card    = nullptr;
    wxPanel*  m_discard_card  = nullptr;
    wxScrolledWindow* m_merge_scroll   = nullptr;
    wxScrolledWindow* m_discard_scroll = nullptr;
    TexturePreviewCanvas* m_preview_merge   = nullptr;
    TexturePreviewCanvas* m_preview_discard = nullptr;
    PreviewTagPanel*      m_tag_merge       = nullptr;
    PreviewTagPanel*      m_tag_discard     = nullptr;
    Button* m_btn_ok     = nullptr;
    Button* m_btn_cancel = nullptr;
};

}} // namespace Slic3r::GUI

#endif
