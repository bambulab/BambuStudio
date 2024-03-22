#ifndef slic3r_GLGizmoMeasure_hpp_
#define slic3r_GLGizmoMeasure_hpp_

#include "GLGizmoBase.hpp"
#include "slic3r/GUI/GLModel.hpp"
#include "slic3r/GUI/GUI_Utils.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "libslic3r/Measure.hpp"
#include "libslic3r/Model.hpp"

namespace Slic3r {

enum class ModelVolumeType : int;
namespace Measure { class Measuring; }
namespace GUI {

enum class SLAGizmoEventType : unsigned char;

class GLGizmoMeasure : public GLGizmoBase
{
    enum class EMode : unsigned char
    {
        FeatureSelection,
        PointSelection
    };

    struct SelectedFeatures
    {
        struct Item
        {
            bool is_center{ false };
            std::optional<Measure::SurfaceFeature> source;
            std::optional<Measure::SurfaceFeature> feature;

            bool operator == (const Item& other) const {
                return this->is_center == other.is_center && this->source == other.source && this->feature == other.feature;
            }

            bool operator != (const Item& other) const {
                return !operator == (other);
            }

            void reset() {
                is_center = false;
                source.reset();
                feature.reset();
            }
        };

        Item first;
        Item second;

        void reset() {
            first.reset();
            second.reset();
        }

        bool operator == (const SelectedFeatures & other) const {
            if (this->first != other.first) return false;
            return this->second == other.second;
        }

        bool operator != (const SelectedFeatures & other) const {
            return !operator == (other);
        }
    };

    struct VolumeCacheItem
    {
        const ModelObject* object{ nullptr };
        const ModelInstance* instance{ nullptr };
        const ModelVolume* volume{ nullptr };
        Transform3d world_trafo;

        bool operator == (const VolumeCacheItem& other) const {
            return this->object == other.object && this->instance == other.instance && this->volume == other.volume &&
                this->world_trafo.isApprox(other.world_trafo);
        }
    };

    std::vector<VolumeCacheItem> m_volumes_cache;

    EMode m_mode{ EMode::FeatureSelection };
    Measure::MeasurementResult m_measurement_result;

    std::map<GLVolume*, std::shared_ptr<Measure::Measuring>> m_mesh_measure_map;
    std::shared_ptr<Measure::Measuring>                      m_curr_measuring{nullptr};

    //first feature
    std::shared_ptr<GLModel>   m_sphere{nullptr};
    std::shared_ptr<GLModel>   m_cylinder{nullptr};
    struct CircleGLModel
    {
        std::shared_ptr<GLModel> circle{nullptr};
        Measure::SurfaceFeature *last_circle_feature{nullptr};
        float                    inv_zoom{0};
    };
    CircleGLModel  m_curr_circle;
    CircleGLModel  m_feature_circle_first;
    CircleGLModel  m_feature_circle_second;
    void           init_circle_glmodel(GripperType gripper_type, const Measure::SurfaceFeature &feature, CircleGLModel &circle_gl_model, float inv_zoom);

    struct PlaneGLModel {
        int                      plane_idx{0};
        std::shared_ptr<GLModel> plane{nullptr};
    };
    PlaneGLModel m_curr_plane;
    PlaneGLModel m_feature_plane_first;
    PlaneGLModel m_feature_plane_second;
    void  init_plane_glmodel(GripperType gripper_type, const Measure::SurfaceFeature &feature, PlaneGLModel &plane_gl_model);

    struct Dimensioning
    {
        GLModel line;
        GLModel triangle;
        GLModel arc;
    };
    Dimensioning m_dimensioning;
    bool         m_show_reset_first_tip{false};


    std::map<GLVolume*, std::shared_ptr<PickRaycaster>>   m_mesh_raycaster_map;
    std::vector<GLVolume*>                                m_hit_different_volumes;
    std::vector<GLVolume*>                                m_hit_order_volumes;
    GLVolume*                                             m_last_hit_volume;
    //std::vector<std::shared_ptr<GLModel>>                 m_plane_models_cache;
    unsigned int                                          m_last_active_item_imgui{0};
    Vec3d                                                 m_buffered_distance;
    Vec3d                                                 m_distance;
    // used to keep the raycasters for point/center spheres
    //std::vector<std::shared_ptr<PickRaycaster>> m_selected_sphere_raycasters;
    std::optional<Measure::SurfaceFeature> m_curr_feature;
    std::optional<Vec3d> m_curr_point_on_feature_position;

    // These hold information to decide whether recalculation is necessary:
    float m_last_inv_zoom{ 0.0f };
    std::optional<Measure::SurfaceFeature> m_last_circle_feature;
    int m_last_plane_idx{ -1 };

    bool m_mouse_left_down{ false }; // for detection left_up of this gizmo
    bool m_mouse_left_down_mesh_deal{false};//for pick mesh

    KeyAutoRepeatFilter m_shift_kar_filter;

    SelectedFeatures m_selected_features;
    bool m_pending_scale{ false };
    bool m_editing_distance{ false };
    bool m_is_editing_distance_first_frame{ true };

    void update_if_needed();

    void disable_scene_raycasters();
    void restore_scene_raycasters_state();

    void render_dimensioning();

#if ENABLE_MEASURE_GIZMO_DEBUG
    void render_debug_dialog();
#endif // ENABLE_MEASURE_GIZMO_DEBUG

public:
    GLGizmoMeasure(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
    /// <summary>
    /// Apply rotation on select plane
    /// </summary>
    /// <param name="mouse_event">Keep information about mouse click</param>
    /// <returns>Return True when use the information otherwise False.</returns>
    bool on_mouse(const wxMouseEvent &mouse_event) override;

    void data_changed(bool is_serializing) override;

    bool gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down);

    bool wants_enter_leave_snapshots() const override { return true; }
    std::string get_gizmo_entering_text() const override { return _u8L("Entering Measure gizmo"); }
    std::string get_gizmo_leaving_text() const override { return _u8L("Leaving Measure gizmo"); }
    //std::string get_action_snapshot_name() const override { return _u8L("Measure gizmo editing"); }

protected:
    bool on_init() override;
    std::string on_get_name() const override;
    bool on_is_activable() const override;
    void on_render() override;
    void on_set_state() override;

    virtual void on_render_for_picking() override;
    virtual void on_render_input_window(float x, float y, float bottom_limit) override;

    void remove_selected_sphere_raycaster(int id);
    void update_measurement_result();

    void show_tooltip_information(float caption_max, float x, float y);
    void reset_all_pick();
    void reset_gripper_pick(GripperType id,bool is_all = false);
    void register_single_mesh_pick();
    void update_single_mesh_pick(GLVolume* v);

    void reset_all_feature();
    void reset_feature1();
    void reset_feature2();
    bool is_two_volume_in_same_model_object();
    Measure::Measuring* get_measuring_of_mesh(GLVolume *v, Transform3d &tran);
    void update_world_plane_features(Measure::Measuring *cur_measuring, Measure::SurfaceFeature &feautre);
    void update_feature_by_tran(Measure::SurfaceFeature & feature);
 private:
    // This map holds all translated description texts, so they can be easily referenced during layout calculations
    // etc. When language changes, GUI is recreated and this class constructed again, so the change takes effect.
    std::map<std::string, wxString> m_desc;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoMeasure_hpp_
