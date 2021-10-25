#ifndef __part_plate_hpp_
#define __part_plate_hpp_

#include <vector>
#include <set>
#include <array>
#include <thread>
#include <mutex>

#include "libslic3r/ObjectID.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/Slicing.hpp"
#include "libslic3r/Arrange.hpp"
#include "Plater.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "GLCanvas3D.hpp"
#include "GLTexture.hpp"
#include "3DScene.hpp"
#include "GLModel.hpp"
#include "3DBed.hpp"

class GLUquadric;
typedef class GLUquadric GLUquadricObject;

inline int compute_colum_count(int count)
{
    float value = sqrt((float)count);
    float round_value = round(value);
    int cols;

    if (value > round_value)
        cols = round_value +1;
    else
        cols = round_value;

    return cols;
}

namespace Slic3r {

class Model;
class ModelObject;
class ModelInstance;
class Print;
class SLAPrint; 

namespace GUI {
class Plater;
class GLCanvas3D;
struct Camera;
class PartPlateList;

static const constexpr double LOGICAL_PART_PLATE_GAP = 1. / 5.;
static const constexpr int PARTPLATE_ICON_SIZE = 10;
static const constexpr int PARTPLATE_ICON_GAP = 2;


using GCodeResult = GCodeProcessorResult;

class PartPlate : public ObjectBase
{
    PartPlateList* m_partplate_list;
    Plater* m_plater; //Plater reference, not own it
    Model* m_model; //Model reference, not own it
    PrinterTechnology  printer_technology;

    std::set<std::pair<int, int>> obj_to_instance_set;
    int m_plate_index;
    Vec3d m_origin;
    int m_width;
    int m_depth;
    int m_height;
    bool m_printable;
    bool m_locked;
    bool m_ready_for_slice;
    bool m_slice_result_valid;

    Print *m_print; //Print reference, not own it, no need to serialize
    GCodeProcessorResult *m_gcode_result;
    int m_print_index;

    std::string m_tmp_gcode_path; //use a temp path to store the gcode

    friend class PartPlateList;

    Pointfs m_shape;
    Pointfs m_exclude_area;
    BoundingBoxf3 m_bounding_box;
    BoundingBoxf3 m_extended_bounding_box;
    mutable std::vector<BoundingBoxf3> m_exclude_bounding_box;
    mutable BoundingBoxf3 m_grabber_box;
    Transform3d m_grabber_trans_matrix;
    Slic3r::Geometry::Transformation position;
    std::vector<Vec3f> positions;
    Polygon m_polygon;
    unsigned int m_vbo_id{ 0 };
    GeometryBuffer m_triangles;
    GeometryBuffer m_gridlines;
    GeometryBuffer m_del_icon;
    mutable unsigned int m_del_vbo_id{ 0 };
    GeometryBuffer m_arrange_icon;
    mutable unsigned int m_arrange_vbo_id{ 0 };
    GLTexture m_texture;
    std::array<float, 4> m_model_color{ 0.235f, 0.235f, 0.235f, 1.0f };
    mutable float m_grabber_color[4];
    float m_scale_factor{ 1.0f };
    GLUquadricObject* m_quadric;
    int m_hover_id;
    bool m_selected;

    void init();
    bool valid_instance(int obj_id, int instance_id);
    void calc_bounding_boxes() const;
    void calc_triangles(const ExPolygon& poly);
    void calc_gridlines(const ExPolygon& poly, const BoundingBox& pp_bbox);
    void calc_vertex_for_icons(int index, GeometryBuffer &buffer);
    void render_background() const;
    //void render_background_for_picking(const float* render_color) const;
    void render_grid(bool bottom) const;
    void render_default(bool bottom) const;
    void render_label(GLCanvas3D& canvas) const;
    void render_grabber(const float* render_color, bool use_lighting) const;
    void render_face(float x_size, float y_size) const;
    void render_arrows(const float* render_color, bool use_lighting) const;
    void render_left_arrow(const float* render_color, bool use_lighting) const;
    void render_right_arrow(const float* render_color, bool use_lighting) const;
    void render_icon_texture(int position_id, int tex_coords_id, const GeometryBuffer &buffer, GLTexture &texture, unsigned int &vbo_id) const;
    void render_icons(bool bottom) const;
    void render_rectangle_for_picking(const GeometryBuffer &buffer, const float* render_color) const;
    void on_render_for_picking() const;
    std::array<float, 4> picking_color_component(int idx) const;
    void release_opengl_resource();

public:
    static const unsigned int PLATE_BASE_ID = 255 * 255 * 253;
    static const unsigned int GRABBER_COUNT = 3;

    PartPlate();
    PartPlate(PartPlateList *partplate_list, Vec3d origin, int width, int depth, int height, Plater* platerObj, Model* modelObj, bool printable=true, PrinterTechnology tech = ptFFF);
    ~PartPlate();

    bool operator<(PartPlate&) const;

    //clear alll the instances in plate
    void clear();

    //static const int plate_x_offset = 20; //mm
    //static const double plate_x_gap = 0.2;
    static const int plate_thumbnail_width = 1920; //pixels
    static const int plate_thumbnail_height = 1080; //pixels

    //set the plate's index
    void set_index(int index);

    //get the plate's index
    int get_index() { return m_plate_index; }

    //get the print's object, result and index
    void get_print(PrintBase **print, GCodeResult **result, int *index);

    //set the print object, result and it's index
    void set_print(PrintBase *print, GCodeResult* result = nullptr, int index = -1);

    //get the plate's center point origin
    Vec3d get_center_origin();
    /* size and position related functions*/
    //set position and size
    void set_pos_and_size(Vec3d& origin, int width, int depth, int height, bool with_instance_move);

    /* instance related operations*/
    //judge whether instance is bound in plate or not
    bool contain_instance(int obj_id, int instance_id);

    //judge whether the plate's origin is at the left of instance or not
    bool is_left_top_of(int obj_id, int instance_id);

    //check whether instance is outside the plate or not
    bool check_outside(int obj_id, int instance_id);

    //judge whether instance is intesected with plate or not
    bool intersect_instance(int obj_id, int instance_id);

    //add an instance into plate
    int add_instance(int obj_id, int instance_id, bool move_position);

    //remove instance from plate
    int remove_instance(int obj_id, int instance_id);

    //whether it is empty
    bool empty() { return obj_to_instance_set.empty(); }

    //whether it is has printable instances
    bool has_printable_instances();

    //move instances to left or right PartPlate
    void move_instances_to(PartPlate& left_plate, PartPlate& right_plate);

    /*rendering related functions*/
    const Pointfs& get_shape() const { return m_shape; }
    bool set_shape(const Pointfs& shape, const Pointfs& exclude_areas, Vec2d position);
    bool contains(const Point& point) const;
    bool contains(const GLVolume& v) const;
    bool contains(const BoundingBoxf3& bb) const;
    bool intersects(const BoundingBoxf3& bb) const;
    
    Point point_projection(const Point& point) const;
    void render(GLCanvas3D& canvas, bool bottom, bool with_label = true, bool only_body = false);
    void render_for_picking() const { on_render_for_picking(); }
    void set_selected();
    void set_unselected();
    void set_hover_id(int id) { m_hover_id = id; }
    const BoundingBoxf3& get_bounding_box(bool extended) { return extended ? m_extended_bounding_box : m_bounding_box; }
    const BoundingBox get_bounding_box_crd();

    const std::vector<BoundingBoxf3>& get_exclude_areas() { return m_exclude_bounding_box; }


    /*status related functions*/
    //update status
    void update_states();

    //is locked or not
    bool is_locked() const { return m_locked; }
    void lock(bool state) { m_locked = state; }

    //is a printable plate or not
    bool is_printable() const { return m_printable; }

    //can be sliced or not
    bool can_slice() const { return m_ready_for_slice; }

    //is slice result valid or not
    bool is_slice_result_valid() const { return m_slice_result_valid; }

    //invalid sliced result
    void update_slice_result_valid_state(bool valid = false) { m_slice_result_valid = valid; }

    /*slice related functions*/
    //update current slice context into backgroud slicing process
    void update_slice_context(BackgroundSlicingProcess& process);
    //return the fff print object
    Print* fff_print() { return m_print; }
    //return the slice result
    GCodeProcessorResult* get_slice_result() { return m_gcode_result; }
    std::string get_tmp_gcode_path() { return m_tmp_gcode_path; }
    //load gcode from file
    int load_gcode_from_file(const std::string& filename);

    void print() const;

    friend class cereal::access;
    friend class UndoRedo::StackImpl;

    template<class Archive> void load(Archive& ar) {
        std::vector<std::pair<int, int>>	objects_and_instances;

        ar(m_plate_index, m_print_index, m_origin, m_width, m_depth, m_height, m_locked, m_selected, m_ready_for_slice, m_printable, m_tmp_gcode_path, objects_and_instances);

        for (std::vector<std::pair<int, int>>::iterator it = objects_and_instances.begin(); it != objects_and_instances.end(); ++it)
            obj_to_instance_set.insert(std::pair(it->first, it->second));
    }
    template<class Archive> void save(Archive& ar) const {
        std::vector<std::pair<int, int>>	objects_and_instances;

        for (std::set<std::pair<int, int>>::iterator it = obj_to_instance_set.begin(); it != obj_to_instance_set.end(); ++it)
            objects_and_instances.emplace_back(it->first, it->second);

        ar(m_plate_index, m_print_index, m_origin, m_width, m_depth, m_height, m_locked, m_selected, m_ready_for_slice, m_printable, m_tmp_gcode_path, objects_and_instances);
    }
    /*template<class Archive> void serialize(Archive& ar)
    {
        std::vector<std::pair<int, int>> objects_and_instances;
        for (std::set<std::pair<int, int>>::iterator it = obj_to_instance_set.begin(); it != obj_to_instance_set.end(); ++it)
            objects_and_instances.emplace_back(it->first, it->second);
        ar(m_plate_index, m_origin, m_width, m_depth, m_height, m_locked, m_ready_for_slice, m_printable, objects_and_instances);
    }*/
};

class PartPlateList : public ObjectBase
{
    Plater* m_plater; //Plater reference, not own it
    Model* m_model; //Model reference, not own it
    PrinterTechnology  printer_technology;

    std::vector<PartPlate*> m_plate_list;
    std::map<int, PrintBase*> m_print_list;
    std::map<int, GCodeResult*> m_gcode_result_list;
    std::mutex m_plates_mutex;
    int m_plate_count;
    int m_plate_cols;
    int m_current_plate;
    int m_print_index;

    int m_plate_width;
    int m_plate_depth;
    int m_plate_height;

    PartPlate unprintable_plate;
    Pointfs m_shape;
    Pointfs m_exclude_areas;
    BoundingBoxf3 m_bounding_box;
    bool m_intialized;
    GLTexture m_del_texture;
    GLTexture m_arrange_texture;

    void init();
    //compute the origin for printable plate with index i
    Vec3d compute_origin(int index, int column_count);
    //compute the origin for unprintable plate
    Vec3d compute_origin_for_unprintable();
    //compute shape position
    Vec2d compute_shape_position(int index, int cols);
    //generate icon textures
    void generate_icon_textures();
    void release_icon_textures();

    friend class cereal::access;
    friend class UndoRedo::StackImpl;
    friend class PartPlate;

public:
    static const unsigned int MAX_PLATES_COUNT = 50;

    PartPlateList(int width, int depth, int height, Plater* platerObj, Model* modelObj, PrinterTechnology tech = ptFFF);
    PartPlateList(Plater* platerObj, Model* modelObj, PrinterTechnology tech = ptFFF);
    ~PartPlateList();

    //this may be happened after machine changed
    void reset_size(int width, int depth, int height);
    //clear all the instances in the plate, but keep the plates
    void clear(bool delete_plates = false, bool release_print_list = false);
    //clear all the instances in the plate, and delete the plates, only keep the first default plate
    void reset(bool do_init);

    //get the plate stride
    double plate_stride_x();
    double plate_stride_y();

    /*basic plate operations*/
    //create an empty plate and return its index
    int create_plate();

    //destroy print which has the index of print_index
    int destroy_print(int print_index);

    //delete a plate by index
    int delete_plate(int index);

    //delete a plate by pointer
    //int delete_plate(PartPlate* plate);
    void delete_selected_plate();

    //get a plate pointer by index
    PartPlate* get_plate(int index);

    int get_curr_plate_index() { return m_current_plate; }
    PartPlate* get_curr_plate() { return m_plate_list[m_current_plate]; }

    PartPlate* get_selected_plate();

    Vec3d get_current_plate_origin() { return compute_origin(m_current_plate, m_plate_cols); }

    //select plate
    int select_plate(int index);

    //get the plate counts, not including the invalid plate
    int get_plate_count();

    //update the plate cols due to plate count change
    void update_plate_cols();

    void update_all_plates_pos_and_size(bool with_unprintable_move = true);

    //get the plate cols
    int get_plate_cols() { return m_plate_cols; }

    //move the plate to position index
    int move_plate_to_index(int old_index, int new_index);

    //lock plate
    int lock_plate(int index, bool state);

    //find plate by print index, return -1 if not found
    int find_plate_by_print_index(int index);

    /*instance related operations*/
    //find instance in which plate, return -1 when not found
    int find_instance(int obj_id, int instance_id);

    //notify instance's update, need to refresh the instance in plates
    int notify_instance_update(int obj_id, int instance_id);

    //notify instance is removed
    int notify_instance_removed(int obj_id, int instance_id);

    //add instance to special plate, need to remove from the original plate
    int add_to_plate(int obj_id, int instance_id, int plate_id);

    //reload all objects
    int reload_all_objects();

    /* arrangement related functions */
    //compute the plate index
    int compute_plate_index(arrangement::ArrangePolygon& arrange_polygon);
    //preprocess an arrangement::ArrangePolygon, return true if it is in a locked plate
    bool preprocess_arrange_polygon(int obj_index, int instance_index, arrangement::ArrangePolygon& arrange_polygon, bool selected);
    bool preprocess_arrange_polygon_other_locked(int obj_index, int instance_index, arrangement::ArrangePolygon& arrange_polygon, bool selected);
    bool preprocess_exclude_areas(arrangement::ArrangePolygons& unselected);

    void postprocess_bed_index_for_selected(arrangement::ArrangePolygon& arrange_polygon);
    void postprocess_bed_index_for_unselected(arrangement::ArrangePolygon& arrange_polygon);
    void postprocess_bed_index_for_current_plate(arrangement::ArrangePolygon& arrange_polygon);

    //postprocess an arrangement:;ArrangePolygon
    void postprocess_arrange_polygon(arrangement::ArrangePolygon& arrange_polygon, bool selected);

    /*rendering related functions*/
    void render(GLCanvas3D& canvas, bool bottom, float scale_factor, bool only_current = false, bool only_body = false);
    void render_for_picking_pass();
    BoundingBoxf3& get_bounding_box() { return m_bounding_box; }
    //int select_plate_by_hover_id(int hover_id);
    int select_plate_by_obj(int obj_index, int instance_index);
    void calc_bounding_boxes();
    void select_plate_view();
    bool set_shapes(const Pointfs& shape, const Pointfs& exclude_areas);
    void set_hover_id(int id);
    void reset_hover_id();
    bool intersects(const BoundingBoxf3 &bb);
    bool contains(const BoundingBoxf3 &bb);

    /*slice related functions*/
    //update current slice context into backgroud slicing process
    void update_slice_context_to_current_plate(BackgroundSlicingProcess& process);
    //return the current fff print object
    Print& get_current_fff_print() const;
    //return the slice result
    GCodeProcessorResult* get_current_slice_result() const;
    //will create a plate and load gcode, return the plate index
    int create_plate_from_gcode_file(const std::string& filename);

    //invalid all the plater's slice result
    void invalid_all_slice_result();
    //set current plater's slice result to valid
    void update_current_slice_result_state(bool valid) { m_plate_list[m_current_plate]->update_slice_result_valid_state(valid); }

    void print() const;

    //retruct plates structures after de-serialize
    int rebuild_plates_after_deserialize();

    //retruct plates structures after auto-arrangement
    int rebuild_plates_after_arrangement(bool recycle_plates = true);

    /*load/store releted functions*/
    int store_to_3mf_structure(PlateDataPtrs& plate_data_list, bool with_gcode = true);
    int load_from_3mf_structure(PlateDataPtrs& plate_data_list);

    template<class Archive> void serialize(Archive& ar)
    {
        //ar(cereal::base_class<ObjectBase>(this));
        ar(m_shape, m_plate_width, m_plate_depth, m_plate_height, m_plate_count, m_current_plate, m_plate_list, unprintable_plate);
        //ar(m_plate_width, m_plate_depth, m_plate_height, m_plate_count, m_current_plate);
    }
};

} // namespace GUI
} // namespace Slic3r

namespace cereal
{
    template <class Archive> struct specialize<Archive, Slic3r::GUI::PartPlate, cereal::specialization::member_load_save> {};
}
#endif //__part_plate_hpp_
