#ifndef __part_plate_hpp_
#define __part_plate_hpp_

#include <vector>
#include <set>

#include "libslic3r/ObjectID.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/Slicing.hpp"
#include "Plater.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "GLCanvas3D.hpp"

namespace Slic3r {

class Model;
class ModelObject;
class ModelInstance;
class Print;
class SLAPrint; 

namespace GUI {
class Plater;

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

    void init();
    bool valid_instance(int obj_id, int instance_id);

    friend class PartPlateList;

public:
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
    //render
    void render(GLCanvas3D& canvas, float scale_factor, bool current) const;


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
    // Used for deserialization
    PartPlate() : ObjectBase(-1), m_plater(nullptr), m_model(nullptr) { assert(this->id().invalid()); }
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
    int m_plate_count;
    int m_current_plate;

    int m_plate_width;
    int m_plate_depth;
    int m_plate_height;

    PartPlate unprintable_plate;

    void init();
    //compute the origin for printable plate with index i
    Vec3d compute_origin(int index);
    //compute the origin for unprintable plate
    Vec3d compute_origin_for_unprintable();

    friend class cereal::access;
    friend class UndoRedo::StackImpl;

public:
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
    //render
    void render(GLCanvas3D& canvas, float scale_factor) const;

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