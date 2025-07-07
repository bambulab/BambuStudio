#ifndef slic3r_GLGizmoFuzzySkin_hpp_
#define slic3r_GLGizmoFuzzySkin_hpp_

#include "GLGizmoPainterBase.hpp"
//BBS
#include "libslic3r/Print.hpp"
#include "libslic3r/ObjectID.hpp"

#include <boost/thread.hpp>

namespace Slic3r::GUI {

class GLGizmoFuzzySkin : public GLGizmoPainterBase
{
public:
    GLGizmoFuzzySkin(GLCanvas3D& parent, unsigned int sprite_id);
    void data_changed(bool is_serializing) override;
    void render_painter_gizmo() const override;

    //BBS: add edit state
    enum EditState {
        state_idle = 0,
        state_generating = 1,
        state_ready
    };

    //BBS
    bool on_key_down_select_tool_type(int keyCode);

    std::string get_icon_filename(bool is_dark_mode) const override;

protected:
    void on_render_input_window(float x, float y, float bottom_limit) override;
    std::string on_get_name() const override;
    std::string on_get_name_str() override { return "FuzzySkin Painting"; }

    // BBS
    void render_triangles(const Selection& selection) const override;
    void on_set_state() override;
    void show_tooltip_information(float caption_max, float x, float y);
    wxString handle_snapshot_action_name(bool shift_down, Button button_down) const override;

    std::string get_gizmo_entering_text() const override { return "Entering Paint-on supports"; }
    std::string get_gizmo_leaving_text() const override { return "Leaving Paint-on supports"; }
    std::string get_action_snapshot_name() override { return "Paint-on supports editing"; }
    EnforcerBlockerType get_left_button_state_type() const override { return EnforcerBlockerType::FUZZY_SKIN; }
    EnforcerBlockerType get_right_button_state_type() const override;
    // BBS
    wchar_t                           m_current_tool = 0;

private:
    bool on_init() override;

    //BBS: remove const
    void update_model_object() override;
    //BBS: add logic to distinguish the first_time_update and later_update
    void update_from_model_object(bool first_update) override;
    void tool_changed(wchar_t old_tool, wchar_t new_tool);
    void on_opening() override {}
    void on_shutdown() override;
    PainterGizmoType get_painter_type() const override;

    bool m_volume_valid = false;

    bool m_is_tree_support = false;
    bool m_cancel = false;
    size_t m_object_id;
    std::vector<ObjectBase::Timestamp> m_volume_timestamps;
    PrintInstance m_print_instance;
    mutable EditState m_edit_state;
    //thread
    boost::thread   m_thread;
    // Mutex and condition variable to synchronize m_thread with the UI thread.
    std::mutex      m_mutex;
    int m_generate_count;

    // This map holds all translated description texts, so they can be easily referenced during layout calculations
    // etc. When language changes, GUI is recreated and this class constructed again, so the change takes effect.
    std::map<std::string, wxString> m_desc;
};
} // namespace Slic3r::GUI
#endif // slic3r_GLGizmoFuzzySkin_hpp_
