#include <cstddef>
#include <algorithm>
#include <numeric>
#include <vector>
#include <string>
#include <regex>
#include <future>
#include <boost/algorithm/string.hpp>
#include <boost/optional.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/convert.hpp>


#include "libslic3r/libslic3r.h"
#include "libslic3r/GCode/ThumbnailData.hpp"
#include "libslic3r/Utils.hpp"

#include "BackgroundSlicingProcess.hpp"
#include "3DBed.hpp"
#include "PartPlate.hpp"

using boost::optional;
namespace fs = boost::filesystem;

namespace Slic3r {
namespace GUI {

class Bed3D;

PartPlate::PartPlate(Vec3d& origin, int width, int depth, int height, Plater* platerObj, Model* modelObj, bool printable, PrinterTechnology tech)
	:m_plater(platerObj), m_model(modelObj), printer_technology(tech), m_origin(origin), m_width(width), m_depth(depth), m_height(height),  m_printable(printable)
{
	init();
}


PartPlate::~PartPlate()
{
	clear();
}

void PartPlate::init()
{
	m_locked = false;
	m_ready_for_slice = false;
	m_slice_result_valid = false;
	m_locked = false;
}

bool PartPlate::valid_instance(int obj_id, int instance_id)
{
	if ((obj_id >= 0) && (obj_id < m_model->objects.size()))
	{
		ModelObject* object = m_model->objects[obj_id];
		if ((instance_id >= 0) && (instance_id < object->instances.size()))
			return true;
	}

	return false;
}

bool PartPlate::operator<(PartPlate& plate) const
{
	int index = plate.get_index();
	return (this->m_plate_index < index);
}

//set the plate's index
void PartPlate::set_index(int index)
{
	m_plate_index = index;

	BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": plate_id update from %1% to %2%") % m_plate_index % index;
}

void PartPlate::clear()
{
	obj_to_instance_set.clear();

	return;
}

/* size and position related functions*/
//set position and size
void PartPlate::set_pos_and_size(Vec3d& origin, int width, int depth, int height, bool with_instance_move)
{
	bool size_changed = false; //size changed means the machine changed
	bool pos_changed = false;

	BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": plate_id %1%, before, origin {%2%,%3%,%4%}, plate_width %5%, plate_depth %6%, plate_height %7%")\
		% m_plate_index % m_origin.x() % m_origin.y() % m_origin.z() % m_width % m_depth % m_height;
	BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": with_instance_move %1%, after, origin {%2%,%3%,%4%}, plate_width %5%, plate_depth %6%, plate_height %7%")\
		% with_instance_move % origin.x() % origin.y() % origin.z() % width % depth % height;
	size_changed = ((width != m_width) || (depth != m_depth) || (height != m_height));
	pos_changed = (m_origin != origin);

	if ((!size_changed) && (!pos_changed))
	{
		//size and position the same with before, just return
		return;
	}

	if (with_instance_move)
	{
		for (std::set<std::pair<int, int>>::iterator it = obj_to_instance_set.begin(); it != obj_to_instance_set.end(); ++it) {
			int obj_id = it->first;
			int instance_id = it->second;
			ModelObject* object = m_model->objects[obj_id];
			ModelInstance* instance = object->instances[instance_id];

			//move this instance into the new plate's same position
			Vec3d offset = instance->get_transformation().get_offset();
			int off_x, off_y;

			if (size_changed)
			{
				//change position due to the bed size changes
				//off_x = (width - m_width) * m_plate_index + (width - m_width) / 2;
				//off_y = (depth - m_depth) * m_plate_index + (depth - m_depth) / 2;
				off_x = origin.x() - m_origin.x() + (width - m_width) / 2;
				off_y = origin.y() - m_origin.y() + (depth - m_depth) / 2;
			}
			else
			{
				//change position due to the plate moves
				off_x = origin.x() - m_origin.x();
				off_y = origin.y() - m_origin.y();
			}
			offset.x() = offset.x() + off_x;
			offset.y() = offset.y() + off_y;

			instance->set_offset(offset);
			object->invalidate_bounding_box();
		}
	}
	else
	{
		clear();
	}
	m_origin = origin;
	m_width = width;
	m_depth = depth;
	m_height = height;

	return;
}

//get the plate's center point origin
Vec3d PartPlate::get_center_origin()
{
	Vec3d origin;

	origin(0) = m_origin.x() + m_width / 2;
	origin(1) = m_origin.y() + m_depth / 2;
	origin(2) = m_origin.z();

	return origin;
}

Print& PartPlate::get_print()
{
	if (printer_technology == PrinterTechnology::ptFFF)
		return m_print;

	//todo, use fff print currently
	return m_print;
}

/* instance related operations*/
//judge whether instance is bound in plate or not
bool PartPlate::contain_instance(int obj_id, int instance_id)
{
	bool result = false;
	std::set<std::pair<int, int>>::iterator it;

	it = obj_to_instance_set.find(std::pair(obj_id, instance_id));
	if (it != obj_to_instance_set.end()) {
		result = true;
	}

	return result;
}

//check whether instance is outside the plate or not
bool PartPlate::check_outside(int obj_id, int instance_id)
{
	bool outside = true;

	ModelObject* object = m_model->objects[obj_id];
	ModelInstance* instance = object->instances[instance_id];

	BoundingBoxf3 instance_box = object->instance_bounding_box(instance_id);
	Vec3d up_point(m_origin.x() + m_width, m_origin.y() + m_depth, m_origin.z() + m_height);
	BoundingBoxf3 plate_box(m_origin, up_point);

	if (plate_box.contains(instance_box))
		outside = false;

	return outside;
}

//judge whether instance is intesected with plate or not
bool PartPlate::intersect_instance(int obj_id, int instance_id)
{
	bool result = false;

	if (!valid_instance(obj_id, instance_id))
	{
		BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": plate_id %1%, invalid obj_id %2%, instance_id %3%") % m_plate_index % obj_id % instance_id;
		return false;
	}

	ModelObject* object = m_model->objects[obj_id];
	ModelInstance* instance = object->instances[instance_id];
	std::pair<int, int> pair(obj_id, instance_id);
	BoundingBoxf3 instance_box = object->instance_bounding_box(instance_id);
	Vec3d up_point(m_origin.x() + m_width, m_origin.y() + m_depth, m_origin.z() + m_height);
	BoundingBoxf3 plate_box(m_origin, up_point);

	result = plate_box.intersects(instance_box);
	return result;
}

//add an instance into plate
int PartPlate::add_instance(int obj_id, int instance_id, bool move_position)
{
	if (!valid_instance(obj_id, instance_id))
	{
		BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": plate_id %1%, invalid obj_id %2%, instance_id %3%, move_position %4%") % m_plate_index % obj_id % instance_id % move_position;
		return -1;
	}

	ModelObject* object = m_model->objects[obj_id];
	ModelInstance* instance = object->instances[instance_id];
	std::pair<int, int> pair(obj_id, instance_id);

	obj_to_instance_set.insert(pair);

	BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": plate_id %1%, add instance obj_id %2%, instance_id %3%, move_position %4%") % m_plate_index % obj_id % instance_id % move_position;

	if (move_position)
	{
		//move this instance into the new position
		Vec3d center = get_center_origin();
		center.z() = instance->get_transformation().get_offset(Z);

		instance->set_offset(center);
		object->invalidate_bounding_box();
	}

	//need to judge whether this instance has an outer part
	if (m_ready_for_slice&&check_outside(obj_id, instance_id))
	{
		m_ready_for_slice = false;
	}

	return 0;
}

//remove instance from plate
int PartPlate::remove_instance(int obj_id, int instance_id)
{
	bool result;
	std::set<std::pair<int, int>>::iterator it;

	it = obj_to_instance_set.find(std::pair(obj_id, instance_id));
	if (it != obj_to_instance_set.end()) {
		obj_to_instance_set.erase(it);
		if (!m_ready_for_slice)
			update_states();
		BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(":plate_id %1%, found obj_id %2%, instance_id %3%") % m_plate_index % obj_id % instance_id;
		result = 0;
	}
	else {
		BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": plate_id %1%, can not find obj_id %2%, instance_id %3%") % m_plate_index % obj_id % instance_id;
		result = -1;
	}

	return result;
}


/*rendering related functions*/
//render
void PartPlate::render(GLCanvas3D& canvas, float scale_factor, bool current) const
{

}


/*status related functions*/
//update status
void PartPlate::update_states()
{
	m_ready_for_slice = true;
	for (std::set<std::pair<int, int>>::iterator it = obj_to_instance_set.begin(); it != obj_to_instance_set.end(); ++it) {
		int obj_id = it->first;
		int instance_id = it->second;

		if (check_outside(obj_id, instance_id))
		{
			m_ready_for_slice = false;
			//currently only check whether ready to slice
			break;
		}
	}

	return;
}


/*slice related functions*/
//update current slice context into backgroud slicing process
void PartPlate::update_slice_context(BackgroundSlicingProcess & process, const DynamicPrintConfig& config)
{
	process.set_fff_print(&m_print);
	process.set_gcode_result(&m_gcode_result);
}

//load gcode from file
int PartPlate::load_gcode_from_file(const std::string& filename)
{
	int ret = 0;

	return ret;
}

void PartPlate::print() const
{
	unsigned int count=0;

	BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": plate index %1%, pointer %2%") % m_plate_index % this;
	BOOST_LOG_TRIVIAL(debug) << boost::format("origin {%1%,%2%,%3%}, width %4%,  depth %5%, height %6%") % m_origin.x() % m_origin.y() % m_origin.z() % m_width % m_depth % m_height;
	BOOST_LOG_TRIVIAL(debug) << boost::format("m_printable %1%, m_locked %2%, m_ready_for_slice %3%, m_slice_result_valid %4%,  m_thumbnail_path %5%, set size %6%")\
		% m_printable % m_locked % m_ready_for_slice % m_slice_result_valid % m_thumbnail_path % obj_to_instance_set.size();
	for (std::set<std::pair<int, int>>::iterator it = obj_to_instance_set.begin(); it != obj_to_instance_set.end(); ++it) {
		int obj_id = it->first;
		int instance_id = it->second;

		BOOST_LOG_TRIVIAL(debug) << boost::format("the %1%th instance, obj_id %2%, instance id %3%") % count % obj_id % instance_id;
	}

	return;
}

/* PartPlate List related functions*/
PartPlateList::PartPlateList(int width, int depth, int height, Plater* platerObj, Model* modelObj, PrinterTechnology tech)
	:m_plate_width(width), m_plate_depth(depth), m_plate_height(height), m_plater(platerObj), m_model(modelObj), printer_technology(tech),
	unprintable_plate(Vec3d(0.0 - width - PartPlate::plate_x_offset, 0.0, 0.0), width, depth, height, platerObj, modelObj, false, tech)
{
	BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(":plate_width %1%, plate_depth %2%, plate_height %3%") % width % depth % height;

	init();
}
PartPlateList::PartPlateList(Plater* platerObj, Model* modelObj, PrinterTechnology tech)
	:m_plate_width(0), m_plate_depth(0), m_plate_height(0), m_plater(platerObj), m_model(modelObj), printer_technology(tech),
	unprintable_plate(Vec3d(0.0, 0.0, 0.0), m_plate_width, m_plate_depth, m_plate_height, platerObj, modelObj, false, tech)
{
	init();
}

PartPlateList::~PartPlateList()
{
	clear();
}

void PartPlateList::init()
{
	PartPlate* first_plate = NULL;

	first_plate = new PartPlate(Vec3d(0.0, 0.0, 0.0), m_plate_width, m_plate_depth, m_plate_height, m_plater, m_model, true, printer_technology);
	assert(first_plate != NULL);
	first_plate->set_index(0);

	m_plate_list.push_back(first_plate);
	m_plate_count = 1;
	m_current_plate = 0;
	unprintable_plate.set_index(1);
}

//compute the origin for printable plate with index i
Vec3d PartPlateList::compute_origin(int i)
{
	Vec3d origin;

	origin(0) = i * (PartPlate::plate_x_offset + m_plate_width);
	origin(1) = 0;
	origin(2) = 0;

	return origin;
}

//compute the origin for printable plate with index i
Vec3d PartPlateList::compute_origin_for_unprintable()
{
	Vec3d origin;

	origin(0) = m_plate_list.size() * (PartPlate::plate_x_offset + m_plate_width);
	origin(1) = 0;
	origin(2) = 0;

	return origin;
}

//this may be happened after machine changed
void PartPlateList::reset_size(int width, int depth, int height)
{
	Vec3d origin1, origin2;

	BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(":before size: plate_width %1%, plate_depth %2%, plate_height %3%") % m_plate_width % m_plate_depth % m_plate_height;
	BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(":after size: plate_width %1%, plate_depth %2%, plate_height %3%") % width % depth % height;
	m_plate_width = width;
	m_plate_depth = depth;
	m_plate_height = height;

	for (unsigned int i = 0; i < (unsigned int)m_plate_list.size(); ++i)
	{
		PartPlate* plate = m_plate_list[i];
		assert(plate != NULL);

		//compute origin1 for PartPlate
		origin1 = compute_origin(i);
		plate->set_pos_and_size(origin1, width, depth, height, true);
	}

	origin2 = compute_origin_for_unprintable();
	unprintable_plate.set_pos_and_size(origin2, width, depth, height, true);

	return;
}

//clear all the instances in the plate, but keep the plates
void PartPlateList::clear()
{
	for (unsigned int i = 0; i < (unsigned int)m_plate_list.size(); ++i)
	{
		PartPlate* plate = m_plate_list[i];
		assert(plate != NULL);

		plate->clear();
	}

	unprintable_plate.clear();
}

//clear all the instances in the plate, and delete the plates, only keep the first default plate
void PartPlateList::reset(bool do_init)
{
	clear();

	m_plate_list.clear();

	if (do_init)
		init();

	return;
}

/*basic plate operations*/
//create an empty plate, and return its index
//these model instances which are not in any plates should not be affected also
int PartPlateList::create_plate()
{
	PartPlate* plate = NULL;
	Vec3d origin;
	int new_index;

	new_index = m_plate_list.size();
	origin = compute_origin(new_index);
	plate = new PartPlate(origin, m_plate_width, m_plate_depth, m_plate_height, m_plater, m_model, true, printer_technology);
	assert(plate != NULL);

	plate->set_index(new_index);
	m_plate_list.emplace_back(plate);

	unprintable_plate.set_index(new_index+1);

	return new_index;
}

//delete a plate by index
//keep its instance at origin position and add them into next plate if have
//update the plate index and position after it
int PartPlateList::delete_plate(int index)
{
	int ret = 0;
	PartPlate* plate = NULL;

	if (index >= m_plate_list.size())
	{
		BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(":can not find index %1%, size %2%") % index % m_plate_list.size();
		return -1;
	}

	plate = m_plate_list[index];
	if (index != plate->get_index())
	{
		BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(":plate %1%, has an invalid index %2%") % index % plate->get_index();
		return -1;
	}

	//update this plate
	plate->clear();
	m_plate_list.erase(m_plate_list.begin() + index);
	//TODO, lhwei

	//update the plates after it
	for (unsigned int i = index; i < (unsigned int)m_plate_list.size(); ++i)
	{
		PartPlate* plate = m_plate_list[i];
		assert(plate != NULL);

		Vec3d origin = compute_origin(i);
		plate->set_pos_and_size(origin, m_plate_width, m_plate_depth, m_plate_height, true);
		plate->set_index(i);
	}
	unprintable_plate.set_index(m_plate_list.size());

	return ret;
}

//get a plate pointer by index
PartPlate* PartPlateList::get_plate(int index)
{
	PartPlate* plate = NULL;

	if (index >= m_plate_list.size())
	{
		BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(":can not find index %1%, size %2%") % index % m_plate_list.size();
		return NULL;
	}

	plate = m_plate_list[index];
	assert(plate != NULL);

	return plate;
}

//select plate
int PartPlateList::select_plate(int index)
{
	int ret = 0;

	m_current_plate = index;

	//TODO
	//need to set camera related setttings

	return ret;
}

//get the plate counts, not including the invalid plate
int PartPlateList::get_plate_count()
{
	int ret = 0;

	ret = m_plate_list.size();

	return ret;
}

//move the plate to position index
int PartPlateList::move_plate_to_index(int old_index, int new_index)
{
	int ret = 0, delta;
	Vec3d origin;


	if (old_index == new_index)
	{
		BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(":should not happen, the same index %1%") % old_index;
		return -1;
	}

	if (old_index < new_index)
	{
		delta = 1;
	}
	else
	{
		delta = -1;
	}

	PartPlate* plate = m_plate_list[old_index];
	//update the plates between old_index and new_index
	for (unsigned int i = (unsigned int)old_index; i != (unsigned int)new_index; i = i + delta)
	{
		m_plate_list[i] = m_plate_list[i + delta];
		m_plate_list[i]->set_index(i);

		origin = compute_origin(i);
		m_plate_list[i]->set_pos_and_size(origin, m_plate_width, m_plate_depth, m_plate_height, true);
	}
	origin = compute_origin(new_index);
	m_plate_list[new_index] = plate;
	plate->set_index(new_index);
	plate->set_pos_and_size(origin, m_plate_width, m_plate_depth, m_plate_height, true);

	//update the new plate index
	m_current_plate = new_index;

	return ret;
}

//lock plate
int PartPlateList::lock_plate(int index, bool state)
{
	int ret = 0;
	PartPlate* plate = NULL;

	plate = get_plate(index);
	if (!plate) 
	{
		BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(":can not get plate for index %1%, size %2%") % index % m_plate_list.size();
		return -1;
	}
	BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(":lock plate %1%, to state %2%") % index % state;

	plate->lock(state);

	return ret;
}

/*instance related operations*/
//find instance in which plate, return -1 when not found
int PartPlateList::find_instance(int obj_id, int instance_id)
{
	int ret = -1;

	//update the plates after it
	for (unsigned int i = 0; i < (unsigned int)m_plate_list.size(); ++i)
	{
		PartPlate* plate = m_plate_list[i];
		assert(plate != NULL);

		if (plate->contain_instance(obj_id, instance_id))
			return i;
	}

	//return -1 for not found
	return ret;
}

//notify instance's update, need to refresh the instance in plates
//newly added or modified
int PartPlateList::notify_instance_update(int obj_id, int instance_id)
{
	int ret = 0, index;
	PartPlate* plate = NULL;

	BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": obj_id %1%, instance_id %2%") % obj_id % instance_id;
	index = find_instance(obj_id, instance_id);
	if (index != -1)
	{
		//found it added before
		BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": found it in previous plate %1%") % index;
		plate = m_plate_list[index];
		if (!plate->intersect_instance(obj_id, instance_id))
		{
			//not include anymore, remove it from original plate
			plate->remove_instance(obj_id, instance_id);
		}
		else
		{
			BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": still in original plate, no need to be updated");
			plate->update_states();
			return 0;
		}
	}

	//try to find a new plate
	for (unsigned int i = 0; i < (unsigned int)m_plate_list.size(); ++i)
	{
		PartPlate* plate = m_plate_list[i];
		assert(plate != NULL);

		if (plate->intersect_instance(obj_id, instance_id))
		{
			//found a new plate, add it to plate
			plate->add_instance(obj_id, instance_id, false);
			BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": add it to new plate %1%") % i;
			return 0;
		}
	}

	return 0;
}

//notify instance is removed
int PartPlateList::notify_instance_removed(int obj_id, int instance_id)
{
	int ret = 0, index;
	PartPlate* plate = NULL;

	BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": obj_id %1%, instance_id %2%") % obj_id % instance_id;
	index = find_instance(obj_id, instance_id);
	if (index != -1)
	{
		//found it added before
		BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": found it in plate %1%") % index;
		plate = m_plate_list[index];
		plate->remove_instance(obj_id, instance_id);
	}

	return 0;
}

//add instance to special plate, need to remove from the original plate
//called from the right-mouse menu when a instance selected
int PartPlateList::add_to_plate(int obj_id, int instance_id, int plate_id)
{
	int ret = 0, index;
	PartPlate* plate = NULL;

	BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": plate_id %1%, found obj_id %2%, instance_id %3%") % plate_id % obj_id % instance_id;
	index = find_instance(obj_id, instance_id);
	if (index != -1)
	{
		//found it added before
		BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": found it in previous plate %1%") % index;
		if (index != plate_id)
		{
			//remove it from original plate first
			plate = m_plate_list[index];
			plate->remove_instance(obj_id, instance_id);
		}
		else
		{
			BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": already in this plate, no need to be added");
			return 0;
		}
	}
	else
	{
		BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": not added to plate before, add it to center");
	}
	plate = get_plate(plate_id);
	if (!plate)
	{
		BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(":can not get plate for index %1%, size %2%") % index % m_plate_list.size();
		return -1;
	}
	ret = plate->add_instance(obj_id, instance_id, true);

	return ret;
}

//reload all objects
int PartPlateList::reload_all_objects()
{
	int ret = 0;
	unsigned int i, j, k;

	clear();

	BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": m_model->objects.size() is %1%") % m_model->objects.size();
	//try to find a new plate
	for (i = 0; i < (unsigned int)m_model->objects.size(); ++i)
	{
		ModelObject* object = m_model->objects[i];
		for (j = 0; j < (unsigned int)object->instances.size(); ++j)
		{
			//ModelInstance* instance = object->instances[j];
			for (k = 0; k < (unsigned int)m_plate_list.size(); ++k)
			{
				PartPlate* plate = m_plate_list[k];
				assert(plate != NULL);

				if (plate->intersect_instance(i, j))
				{
					//found a new plate, add it to plate
					plate->add_instance(i, j, false);
					BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": found plate_id %1%, for obj_id %2%, instance_id %3%") % k % i % j;

					//need to judge whether this instance has an outer part
					if (plate->check_outside(i, j))
					{
						plate->m_ready_for_slice = false;
					}
					break;
				}
			}

			if ((k == m_plate_list.size()) && (unprintable_plate.intersect_instance(i, j)))
			{
				//found in unprintable plate, add it to plate
				unprintable_plate.add_instance(i, j, false);
				BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": found in unprintable plate, obj_id %1%, instance_id %2%") % i % j;
			}
		}

	}

	return ret;
}

/*rendering related functions*/
//render
void PartPlateList::render(GLCanvas3D& canvas, float scale_factor) const
{

}

/*slice related functions*/
//update current slice context into backgroud slicing process
void PartPlateList::update_slice_context_to_current_plate(BackgroundSlicingProcess& process, const DynamicPrintConfig& config)
{
	PartPlate* current_plate;

	current_plate = m_plate_list[m_current_plate];
	assert(current_plate != NULL);

	current_plate->update_slice_context(process, config);

	return;
}

//return the current fff print object
Print& PartPlateList::get_current_fff_print() const
{
	PartPlate* current_plate;

	current_plate = m_plate_list[m_current_plate];
	BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(":m_current_plate %1%, current_plate %2%") % m_current_plate % current_plate;
	assert(current_plate != NULL);

	return current_plate->get_print();
}

//return the slice result
GCodeProcessor::Result* PartPlateList::get_current_slice_result() const
{
	PartPlate* current_plate;

	current_plate = m_plate_list[m_current_plate];
	BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(":m_current_plate %1%, current_plate %2%") % m_current_plate % current_plate;
	assert(current_plate != NULL);

	return current_plate->get_slice_result();
}

//will create a plate and load gcode, return the plate index
int PartPlateList::create_plate_from_gcode_file(const std::string& filename)
{
	int ret = 0;

	return ret;
}

int PartPlateList::rebuild_plates_after_deserialize()
{
	int ret = 0;

	BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": plates count %1%") % m_plate_list.size();
	for (unsigned int i = 0; i < (unsigned int)m_plate_list.size(); ++i)
	{
		m_plate_list[i]->m_plater = this->m_plater;
		m_plate_list[i]->m_model = this->m_model;
	}

	if (m_plate_width == 0)
	{
		BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": jump to the first init state, need to re-set size!");
		Vec3d max = m_plater->get_bed().get_bounding_box(false).max;
		Vec3d min = m_plater->get_bed().get_bounding_box(false).min;
		double z = m_plater->config()->opt_float("max_print_height");
		reset_size(max.x() - min.x(), max.y() - min.y(), z);
	}
	return ret;
}

void PartPlateList::print() const
{
	BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format("PartPlateList %1%, m_plate_count %2%, current_plate %3%") % this % m_plate_count % m_current_plate;
	BOOST_LOG_TRIVIAL(debug) << boost::format("m_plate_width %1%, m_plate_depth %2%, m_plate_height %3%, plate count %4%\nplate list:") % m_plate_width % m_plate_depth % m_plate_height % m_plate_list.size();
	for (unsigned int i = 0; i < (unsigned int)m_plate_list.size(); ++i)
	{
		BOOST_LOG_TRIVIAL(debug) << boost::format("the %1%th plate") % i;
		m_plate_list[i]->print();
	}
	BOOST_LOG_TRIVIAL(debug) << boost::format("the unprintable plate:");
	unprintable_plate.print();

	flush_logs();
	return;
}

}//end namespace GUI
}//end namespace slic3r