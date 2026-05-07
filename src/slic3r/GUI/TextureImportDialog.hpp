#pragma once

#include "GUI_Utils.hpp"
#include "Widgets/ProgressDialog.hpp"
#include "libslic3r/TexturePainting.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include "Widgets/PopupWindow.hpp"
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/textctrl.h>
#include "Widgets/SpinInput.hpp"
#include <wx/checkbox.h>
#include <wx/button.h>
#include "Widgets/Button.hpp"
#include <wx/glcanvas.h>
#include <wx/event.h>

#include <array>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <string>

class GreenSlider;

namespace Slic3r { namespace GUI {

wxDECLARE_EVENT(EVT_TEXTURE_COMPUTE_DONE, wxCommandEvent);
wxDECLARE_EVENT(EVT_TEXTURE_COMPUTE_PROGRESS, wxCommandEvent);
wxDECLARE_EVENT(EVT_TEXTURE_COMPUTE_ERROR, wxCommandEvent);

enum class TextureImportState {
    Idle,
    Computing,
    Ready,
    Error
};

struct FilamentMappingRow {
    int                        cluster_id    = -1;
    std::array<std::size_t, 3> source_color  = {0, 0, 0};
    std::string                source_hex;
    int                        target_filament_idx = 0;
    wxPanel*                   source_panel  = nullptr;
    wxPanel*                   target_panel  = nullptr;
};

class FilamentSelectPopup;

// Lightweight 3D preview panel using wxGLCanvas.
// Renders: original textured, multi-color, or filament-mapped.
class TexturePreviewCanvas : public wxGLCanvas
{
public:
    enum class RenderMode { Original, MultiColor, FilamentMap };

    TexturePreviewCanvas(wxWindow* parent, const wxGLAttributes& attrs);
    ~TexturePreviewCanvas();

    void set_mesh_data(
        const std::vector<std::array<float, 3>>& vertices,
        const std::vector<std::array<int, 3>>&   indices);

    void set_texture_data(
        const std::vector<std::array<float, 2>>& uvs,
        const unsigned char* tex_data, int tex_w, int tex_h, int tex_channels);

    void set_texture_render_data(
        const std::vector<std::vector<unsigned char>>& tex_pixels_rgb,
        const std::vector<int>& tex_widths,
        const std::vector<int>& tex_heights,
        const std::vector<std::array<std::array<float,2>, 3>>& face_uvs,
        const std::vector<int>& face_tex_ids);

    void set_painted_mesh_data(
        const std::vector<std::array<float, 3>>& vertices,
        const std::vector<std::array<int, 3>>&   indices);
    void set_face_colors(const std::vector<std::array<std::size_t, 3>>& face_colors);
    void set_original_face_colors(const std::vector<std::array<std::size_t, 3>>& face_colors);
    void set_filament_color_map(const std::map<std::array<std::size_t, 3>, std::array<float, 3>>& color_map);

    void set_render_mode(RenderMode mode);
    RenderMode get_render_mode() const { return m_mode; }
    void set_computing_overlay(bool show);
    void reset_view();

private:
    void on_paint(wxPaintEvent& evt);
    void on_size(wxSizeEvent& evt);
    void on_mouse(wxMouseEvent& evt);
    void ensure_gl_ready();
    void render();
    void render_mesh();
    void render_textured_original();
    void upload_textures();
    void compute_smooth_normals();
    void update_bounding_box();

    wxGLContext*  m_context        = nullptr;
    bool          m_gl_initialized = false;
    RenderMode    m_mode           = RenderMode::Original;

    float   m_zoom     = 1.0f;
    float   m_rot_x    = -30.0f;
    float   m_rot_y    = 30.0f;
    wxPoint m_last_mouse_pos;
    bool    m_dragging = false;

    std::vector<std::array<float, 3>> m_vertices;
    std::vector<std::array<int, 3>>   m_indices;
    std::vector<std::array<float, 2>> m_uvs;
    std::vector<std::array<float, 3>> m_painted_vertices;
    std::vector<std::array<int, 3>>   m_painted_indices;
    std::vector<std::array<float, 3>> m_face_colors_rgb;
    std::vector<std::array<float, 3>> m_original_face_colors_rgb;
    std::vector<std::array<float, 3>> m_filament_colors_rgb;
    std::map<std::array<std::size_t, 3>, std::array<float, 3>> m_color_map;

    unsigned int m_tex_id       = 0;
    int          m_tex_w        = 0;
    int          m_tex_h        = 0;
    int          m_tex_channels = 3;
    bool         m_tex_dirty    = false;
    std::vector<unsigned char> m_tex_data;

    std::vector<unsigned int> m_gl_tex_ids;
    std::vector<std::vector<unsigned char>> m_tex_pixels_rgb;
    std::vector<int> m_tex_widths;
    std::vector<int> m_tex_heights;
    std::vector<std::array<std::array<float,2>, 3>> m_face_uvs;
    std::vector<int> m_face_tex_ids;
    bool m_multi_tex_dirty = false;

    std::vector<std::array<float, 3>> m_vertex_normals;

    std::array<float, 3> m_center = {0, 0, 0};
    float                m_radius = 1.0f;
};


class TextureImportDialog : public DPIDialog
{
public:
    TextureImportDialog(wxWindow*                        parent,
                        const Slic3r::TexturedMesh&      textured_mesh,
                        const std::vector<std::string>&  filament_color_strs,
                        const std::vector<std::string>&  filament_names);
    ~TextureImportDialog();

    void on_dpi_changed(const wxRect& suggested_rect) override;

    Slic3r::PaintedMesh               get_painted_mesh() const;
    std::vector<Slic3r::FilamentMatch> get_matches() const;
    bool                               was_skipped() const { return m_skipped; }
    // Colors of virtual filaments that need to be created after dialog confirmation.
    // Index i corresponds to filament index (m_existing_filament_count + i).
    const std::vector<std::array<float, 4>>& get_new_filament_colors() const { return m_new_filament_colors; }
    size_t get_existing_filament_count() const { return m_existing_filament_count; }
    const std::map<size_t, std::string>& get_changed_existing_colors() const { return m_changed_existing_colors; }

private:
    void build_ui();
    void build_preview_panel(wxWindow* parent, wxSizer* sizer);
    void build_params_panel(wxWindow* parent, wxSizer* sizer);
    void build_mapping_panel(wxWindow* parent, wxSizer* sizer);
    void build_bottom_buttons(wxSizer* sizer);

    void set_state(TextureImportState new_state);
    void update_ui_for_state();

    void start_computation(bool auto_color = false);
    void cancel_computation();
    void on_computation_complete(wxCommandEvent& evt);
    void on_computation_progress(wxCommandEvent& evt);
    void on_computation_error(wxCommandEvent& evt);

    void rebuild_mapping_rows();
    void do_auto_match();
    std::vector<Slic3r::FilamentMatch> build_matches_from_rows() const;
    void update_filament_color_map();
    void show_filament_popup(size_t row_index);
    int  add_virtual_filament(const std::array<float, 4>& rgba, const std::string& hex);

    void on_color_preset_clicked(wxCommandEvent& evt);
    void on_color_slider_changed(wxCommandEvent& evt);
    void on_color_spin_changed(wxCommandEvent& evt);
    void on_smooth_slider_changed(wxCommandEvent& evt);
    void on_smooth_spin_changed(wxCommandEvent& evt);
    void on_apply_clicked(wxCommandEvent& evt);
    void on_auto_merge_toggled(wxCommandEvent& evt);
    void on_view_button_clicked(wxCommandEvent& evt);
    void highlight_view_button(int view_index);
    void on_skip_clicked(wxCommandEvent& evt);
    void on_ok_clicked(wxCommandEvent& evt);

    Slic3r::TexturedMesh               m_textured_mesh;
    std::vector<std::string>           m_filament_color_strs;   // existing + virtual
    std::vector<std::string>           m_filament_names;        // existing + virtual
    std::vector<std::array<float, 4>>  m_filament_colors_rgba;  // existing + virtual
    size_t                             m_existing_filament_count = 0;
    std::vector<std::array<float, 4>>  m_new_filament_colors;   // only virtual (to be created)
    std::map<size_t, std::string>      m_changed_existing_colors; // idx -> new hex for existing filaments modified by user
    std::vector<std::string>           m_filament_preset_ids;    // per existing filament, e.g. "GFA00"
    std::vector<std::string>           m_filament_preset_types;  // per existing filament, e.g. "Bambu PLA Basic"

    TextureImportState                 m_state = TextureImportState::Idle;
    bool                               m_skipped = false;

    Slic3r::PaintedMesh               m_painted;
    std::vector<Slic3r::FilamentMatch> m_current_matches;

    std::unique_ptr<std::thread>       m_worker;
    std::atomic<bool>                  m_cancel_flag{false};
    std::mutex                         m_result_mutex;
    Slic3r::PaintedMesh               m_pending_result;

    wxButton*    m_btn_color_4    = nullptr;
    wxButton*    m_btn_color_8    = nullptr;
    wxButton*    m_btn_color_16   = nullptr;
    Button*      m_btn_color_auto = nullptr;
    GreenSlider* m_color_slider   = nullptr;
    SpinInput*   m_color_spin     = nullptr;
    GreenSlider* m_smooth_slider  = nullptr;
    SpinInput*   m_smooth_spin    = nullptr;
    Button*      m_btn_apply      = nullptr;

    wxCheckBox*           m_auto_merge_cb = nullptr;
    wxScrolledWindow*     m_mapping_scroll = nullptr;
    wxBoxSizer*           m_mapping_sizer  = nullptr;
    std::vector<FilamentMappingRow> m_mapping_rows;

    TexturePreviewCanvas* m_preview_canvas      = nullptr;
    wxPanel*              m_tab_panel           = nullptr;
    Button*               m_btn_view_original   = nullptr;
    Button*               m_btn_view_multicolor = nullptr;
    Button*               m_btn_view_filament   = nullptr;

    ProgressDialog* m_progress_dlg = nullptr;

    Button*       m_btn_skip = nullptr;
    Button*       m_btn_ok   = nullptr;

    int   m_param_color_count = 4;
    int   m_param_smooth      = 5;

    static const int ID_COLOR_4     = wxID_HIGHEST + 200;
    static const int ID_COLOR_8     = wxID_HIGHEST + 201;
    static const int ID_COLOR_16    = wxID_HIGHEST + 202;
    static const int ID_COLOR_AUTO  = wxID_HIGHEST + 203;
    static const int ID_BTN_APPLY   = wxID_HIGHEST + 204;
    static const int ID_BTN_SKIP    = wxID_HIGHEST + 205;
    static const int ID_VIEW_ORIGINAL   = wxID_HIGHEST + 206;
    static const int ID_VIEW_MULTICOLOR = wxID_HIGHEST + 207;
    static const int ID_VIEW_FILAMENT   = wxID_HIGHEST + 208;

    wxDECLARE_EVENT_TABLE();
};

}} // namespace Slic3r::GUI
