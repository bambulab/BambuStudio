#ifndef slic3r_GLGizmoSimplify_hpp_
#define slic3r_GLGizmoSimplify_hpp_

// Include GLGizmoBase.hpp before I18N.hpp as it includes some libigl code,
// which overrides our localization "L" macro.
#include "GLGizmoBase.hpp"
#include "slic3r/GUI/3DScene.hpp"
#include "admesh/stl.h" // indexed_triangle_set
#include <mutex>
#include <thread>
#include <vector>

namespace Slic3r {
class ModelVolume;
class ModelObject;
class Model;

namespace GUI {
class NotificationManager; // for simplify suggestion
class Selection;

class GLGizmoSimplify: public GLGizmoBase
{
public:
    GLGizmoSimplify(GLCanvas3D& parent, unsigned int sprite_id);
    virtual ~GLGizmoSimplify();
    bool on_esc_key_down();
    static void add_simplify_suggestion_notification(
        const std::vector<size_t> &object_ids,
        const std::vector<ModelObject*> &    objects,
        NotificationManager &      manager);

    // True when the selection is a full object with two or more non-empty model parts.
    // Used by the context menu label ("Simplify parts" vs "Simplify Model").
    static bool selection_is_simplify_parts(const Selection &selection);

    std::string get_icon_filename(bool is_dark_mode) const override;

protected:
    virtual std::string on_get_name() const override;
    virtual std::string on_get_name_str() override { return "Simplify"; }
    virtual void on_render_input_window(float x, float y, float bottom_limit) override;
    virtual bool on_is_activable() const override;
    virtual bool on_is_selectable() const override { return false; }
    virtual void on_set_state() override;

    // must implement
    virtual bool on_init() override { return true;};
    virtual void on_render() override;
    virtual void on_render_for_picking() override{};    

    CommonGizmosDataID on_get_requirements() const override;

private:
    static void push_simplify_suggestion(const ModelObject &object, NotificationManager &manager);
    void apply_simplify();
    void close();

    void process();
    void stop_worker_thread_request();
    void worker_finished();

    void create_gui_cfg();
    void request_rerender(bool force = false);
    void init_preview_models();
    void set_center_position();

    // Solid model parts (or the single selected volume) for this Simplify session.
    static std::vector<ModelVolume *> collect_volumes(const Selection &selection, Model &model);

    struct Configuration
    {
        bool use_count = false;
        float    decimate_ratio = 50.f; // in percent
        uint32_t wanted_count = 0; // initialize by percents (single-part UI / config identity)
        float max_error = 0.1f; // maximal quadric error, matches the default detail level "Medium"

        void fix_count_by_ratio(size_t triangle_count);
        bool operator==(const Configuration& rhs) const {
            return (use_count == rhs.use_count && decimate_ratio == rhs.decimate_ratio
                && wanted_count == rhs.wanted_count && max_error == rhs.max_error);
        }
        bool operator!=(const Configuration& rhs) const {
            return ! (*this == rhs);
        }
    };

    Configuration m_configuration;
    // Detail level slider position (0 = extra high .. 4 = extra low), kept per opened model so
    // the slider always matches the max_error the preview is computed with.
    int m_detail_level = 2;

    bool m_move_to_center; // opening gizmo

    std::vector<ModelVolume *> m_volumes;

    bool m_show_wireframe;
    // Preserve painted color/MMU through the decimation. Only offered (checkbox shown) when
    // any working volume actually carries painted color; defaults to off (drop paint) so
    // simplifying an unpainted-intent model stays fast.
    bool m_preserve_color = false;
    // Set when Apply was clicked but the work is deferred to run outside the render frame
    // (apply may pop a modal dialog). Prevents re-triggering Apply while it is pending.
    bool m_apply_pending = false;
    // Objects whose simplify suggestion was hidden on selection here; shown again when the
    // gizmo closes if the object still needs simplifying.
    std::vector<ObjectID> m_hidden_suggestions;
    std::vector<GLModel> m_glmodels;
    // Triangle counts of the preview meshes currently shown (one per volume). Empty / zero
    // until a matching preview result exists for that volume.
    std::vector<size_t> m_triangle_counts;

    // Timestamp of the last rerender request. Only accessed from UI thread.
    int64_t m_last_rerender_timestamp = std::numeric_limits<int64_t>::min();

    // Following struct is accessed by both UI and worker thread.
    // Accesses protected by a mutex.
    struct State {
        enum Status {
            idle,
            running,
            cancelling
        };

        Status status = idle;
        int progress = 0; // percent of done work
        Configuration config; // Configuration we started with.
        std::vector<ModelVolume *> volumes;
        // One result per volume; null entries are still computing or were cancelled.
        std::vector<std::unique_ptr<indexed_triangle_set>> results;
    };

    std::thread m_worker;
    std::mutex m_state_mutex; // guards m_state
    State m_state; // accessed by both threads


    // This configs holds GUI layout size given by translated texts.
    // etc. When language changes, GUI is recreated and this class constructed again,
    // so the change takes effect. (info by GLGizmoFdmSupports.hpp)
    struct GuiCfg
    {
        int top_left_width    = 100;
        int bottom_left_width = 100;
        int input_width       = 100;
        int window_offset_x   = 100;
        int window_offset_y   = 100;
        int window_padding    = 0;

        // trunc model name when longer
        size_t max_char_in_name = 30;

        // to prevent freezing when move in gui
        // delay before process in [ms]
        std::chrono::duration<long int, std::milli> prcess_delay = std::chrono::milliseconds(250);
    };
    std::optional<GuiCfg> m_gui_cfg;

    // translations used for calc window size
    const std::string tr_mesh_name;
    const std::string tr_triangles;
    const std::string tr_detail_level;
    const std::string tr_decimate_ratio;

    // cancel exception
    class SimplifyCanceledException: public std::exception
    {
    public:
        const char *what() const throw()
        {
            return L("Model simplification has been canceled");
        }
    };
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoSimplify_hpp_
