#ifndef __part_plate_hpp_
#define __part_plate_hpp_

#include <vector>
#include <set>
#include <array>
#include <thread>
#include <mutex>

#include "libslic3r/ObjectID.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/Slicing.hpp"
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

static const constexpr double LOGICAL_PART_PLATE_GAP = 1. / 5.;

class PartPlate : public ObjectBase
{
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

    Print m_print;
    GCodeProcessor::Result m_gcode_result;

    std::string m_thumbnail_path; //use a temp path to store the thumbnail

    friend class PartPlateList;

    Pointfs m_shape;
    BoundingBoxf3 m_bounding_box;
    BoundingBoxf3 m_extended_bounding_box;
    mutable BoundingBoxf3 m_grabber_box;
    Transform3d m_grabber_trans_matrix;
    Slic3r::Geometry::Transformation position;
    std::vector<Vec3f> positions;
    Polygon m_polygon;
    unsigned int m_vbo_id{ 0 };
    GeometryBuffer m_triangles;
    GeometryBuffer m_gridlines;
    GLTexture m_texture;
    std::array<float, 4> m_model_color{ 0.235f, 0.235f, 0.235f, 1.0f };
    mutable float m_grabber_color[4];
    float m_scale_factor{ 1.0f };
    GLUquadricObject* m_quadric;
    int m_hover_id;
    bool m_selected;

    void init();
    bool valid_instance(int obj_id, int instance_id);
    void calc_bounding_boexes() const;
    void calc_triangles(const ExPolygon& poly);
    void calc_gridlines(const ExPolygon& poly, const BoundingBox& pp_bbox);
    void render_default(bool bottom) const;
    void render_label(GLCanvas3D& canvas) const;
    void render_grabber(const float* render_color, bool use_lighting) const;
    void render_face(float x_size, float y_size) const;
    void render_arrows(const float* render_color, bool use_lighting) const;
    void render_left_arrow(const float* render_color, bool use_lighting) const;
    void render_right_arrow(const float* render_color, bool use_lighting) const;
    void on_render_for_picking() const;
    std::array<float, 4> picking_color_component(int idx) const;
    void reset();

public:
    static const unsigned int PLATE_BASE_ID = 255 * 255 * 253;
    static const unsigned int GRABBER_COUNT = 3;

    PartPlate();
    PartPlate(Vec3d &origin, int width, int depth, int height, Plater* platerObj, Model* modelObj, bool printable=true, PrinterTechnology tech = ptFFF);
    ~PartPlate();

    bool operator<(PartPlate&) const;

    //clear alll the instances in plate
    void clear();

    static const int plate_x_offset = 20; //mm
    static const int plate_thumbnail_width = 1920; //pixels
    static const int plate_thumbnail_height = 1080; //pixels

    //set the plate's index
    void set_index(int index);

    //get the plate's index
    int get_index() { return m_plate_index; }

    //get the plate's current print
    Print& get_print();

    //get the plate's center point origin
    Vec3d get_center_origin();
    /* size and position related functions*/
    //set position and size
    void set_pos_and_size(Vec3d& origin, int width, int depth, int height, bool with_instance_move);


    /* instance related operations*/
    //judge whether instance is bound in plate or not
    bool contain_instance(int obj_id, int instance_id);

    //check whether instance is outside the plate or not
    bool check_outside(int obj_id, int instance_id);

    //judge whether instance is intesected with plate or not
    bool intersect_instance(int obj_id, int instance_id);

    //add an instance into plate
    int add_instance(int obj_id, int instance_id, bool move_position);

    //remove instance from plate
    int remove_instance(int obj_id, int instance_id);


    /*rendering related functions*/
    const Pointfs& get_shape() const { return m_shape; }
    bool set_shape(const Pointfs& shape, Vec2d position);
    bool contains(const Point& point) const;
    Point point_projection(const Point& point) const;
    void render(GLCanvas3D& canvas, bool bottom);
    void render_for_picking() const { on_render_for_picking(); }
    void set_selected();
    void set_unselected();
    void set_hover_id(int id) { m_hover_id = id; }
    const BoundingBoxf3& get_bounding_box(bool extended) { return extended ? m_extended_bounding_box : m_bounding_box; }


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

    /*slice related functions*/
    //update current slice context into backgroud slicing process
    void update_slice_context(BackgroundSlicingProcess& process, const DynamicPrintConfig& config);
    //return the fff print object
    Print& fff_print() { return m_print; }
    //return the slice result
    GCodeProcessor::Result* get_slice_result() { return &m_gcode_result; }
    //load gcode from file
    int load_gcode_from_file(const std::string& filename);

    void print() const;

    friend class cereal::access;
    friend class UndoRedo::StackImpl;

    template<class Archive> void load(Archive& ar) {
        std::vector<std::pair<int, int>>	objects_and_instances;

        ar(m_plate_index, m_origin, m_width, m_depth, m_height, m_locked, m_ready_for_slice, m_printable, objects_and_instances);

        for (std::vector<std::pair<int, int>>::iterator it = objects_and_instances.begin(); it != objects_and_instances.end(); ++it)
            obj_to_instance_set.insert(std::pair(it->first, it->second));
    }
    template<class Archive> void save(Archive& ar) const {
        std::vector<std::pair<int, int>>	objects_and_instances;

        for (std::set<std::pair<int, int>>::iterator it = obj_to_instance_set.begin(); it != obj_to_instance_set.end(); ++it)
            objects_and_instances.emplace_back(it->first, it->second);

        ar(m_plate_index, m_origin, m_width, m_depth, m_height, m_locked, m_ready_for_slice, m_printable, objects_and_instances);
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

    std::vector <PartPlate*> m_plate_list;
    std::mutex m_plates_mutex;
    int m_plate_count;
    int m_current_plate;

    int m_plate_width;
    int m_plate_depth;
    int m_plate_height;

    PartPlate unprintable_plate;
    Pointfs m_shape;
    BoundingBoxf3 m_bounding_box;

    void init();
    //compute the origin for printable plate with index i
    Vec3d compute_origin(int index);
    //compute the origin for unprintable plate
    Vec3d compute_origin_for_unprintable();
    double plate_stride();

    friend class cereal::access;
    friend class UndoRedo::StackImpl;

public:
    static const unsigned int MAX_PLATES_COUNT = 50;

    PartPlateList(int width, int depth, int height, Plater* platerObj, Model* modelObj, PrinterTechnology tech = ptFFF);
    PartPlateList(Plater* platerObj, Model* modelObj, PrinterTechnology tech = ptFFF);
    ~PartPlateList();

    //this may be happened after machine changed
    void reset_size(int width, int depth, int height);
    //clear all the instances in the plate, but keep the plates
    void clear();
    //clear all the instances in the plate, and delete the plates, only keep the first default plate
    void reset(bool do_init);

    /*basic plate operations*/
   //create an empty plate and return its index
    int create_plate();

    //delete a plate by index
    int delete_plate(int index);

    //delete a plate by pointer
    //int delete_plate(PartPlate* plate);
    // 
    //get a plate pointer by index
    PartPlate* get_plate(int index);

    int get_curr_plate() { return m_current_plate; }

    //select plate
    int select_plate(int index);

    //get the plate counts, not including the invalid plate
    int get_plate_count();

    //move the plate to position index
    int move_plate_to_index(int old_index, int new_index);

    //lock plate
    int lock_plate(int index, bool state);


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

    /*rendering related functions*/
    void render(GLCanvas3D& canvas, bool bottom, float scale_factor);
    void render_for_picking_pass();
    BoundingBoxf3& get_bounding_box() { return m_bounding_box; }
    int select_plate_by_hover_id(int hover_id);
    void calc_bounding_boexes();
    void select_plate_view();
    bool set_shapes(const Pointfs& shape);
    void set_hover_id(int id);
    void reset_hover_id();

    /*slice related functions*/
    //update current slice context into backgroud slicing process
    void update_slice_context_to_current_plate(BackgroundSlicingProcess& process, const DynamicPrintConfig& config);
    //return the current fff print object
    Print& get_current_fff_print() const;
    //return the slice result
    GCodeProcessor::Result* get_current_slice_result() const;
    //will create a plate and load gcode, return the plate index
    int create_plate_from_gcode_file(const std::string& filename);

    void print() const;

    //retruct plates structures after de-serialize
    int rebuild_plates_after_deserialize();

    template<class Archive> void serialize(Archive& ar)
    {
        //ar(cereal::base_class<ObjectBase>(this));
        ar(m_plate_width, m_plate_depth, m_plate_height, m_plate_count, m_current_plate, m_plate_list, unprintable_plate);
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
