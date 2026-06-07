#include "Exception.hpp"
#include "Print.hpp"
#include "BoundingBox.hpp"
#include "ClipperUtils.hpp"
#include "ElephantFootCompensation.hpp"
#include "Geometry.hpp"
#include "I18N.hpp"
#include "Layer.hpp"
#include "MutablePolygon.hpp"
#include "Surface.hpp"
#include "Slicing.hpp"
#include "Tesselate.hpp"
#include "TriangleMeshSlicer.hpp"
#include "Utils.hpp"
#include "Fill/FillAdaptive.hpp"
#include "Fill/FillLightning.hpp"
#include "Format/STL.hpp"
#include "InternalBridgeDetector.hpp"
#include "AABBTreeLines.hpp"

#include <float.h>
#include <string_view>
#include <utility>

#include <boost/log/trivial.hpp>

#include <tbb/parallel_for.h>
#include <tbb/concurrent_vector.h>
#include <tbb/concurrent_unordered_set.h>
#include <tbb/spin_mutex.h>
#include <tbb/task_group.h>  // tbb::is_current_task_group_canceling()

#include <Shiny/Shiny.h>

#include "format.hpp"

using namespace std::literals;

//! macro used to mark string used at localization,
//! return same string
#define L(s) Slic3r::I18N::translate(s)

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
#define SLIC3R_DEBUG
#endif

// #define SLIC3R_DEBUG
// Make assert active if SLIC3R_DEBUG
#ifdef SLIC3R_DEBUG
    #undef NDEBUG
    #define DEBUG
    #define _DEBUG
    #include "SVG.hpp"
    #undef assert
    #include <cassert>
#endif

#define USE_TBB_IN_INFILL 1

namespace Slic3r {

// Constructor is called from the main thread, therefore all Model / ModelObject / ModelIntance data are valid.
PrintObject::PrintObject(Print* print, ModelObject* model_object, const Transform3d& trafo, PrintInstances&& instances) :
    PrintObjectBaseWithState(print, model_object),
    m_trafo(trafo),
    // BBS
    m_tree_support_preview_cache(nullptr)
{
    // Compute centering offet to be applied to our meshes so that we work with smaller coordinates
    // requiring less bits to represent Clipper coordinates.

	// Snug bounding box of a rotated and scaled object by the 1st instantion, without the instance translation applied.
	// All the instances share the transformation matrix with the exception of translation in XY and rotation by Z,
	// therefore a bounding box from 1st instance of a ModelObject is good enough for calculating the object center,
	// snug height and an approximate bounding box in XY.
    BoundingBoxf3  bbox        = model_object->raw_bounding_box();
    Vec3d 		   bbox_center = bbox.center();

	// We may need to rotate the bbox / bbox_center from the original instance to the current instance.
	double z_diff = Geometry::rotation_diff_z(model_object->instances.front()->get_rotation(), instances.front().model_instance->get_rotation());
	if (std::abs(z_diff) > EPSILON) {
		auto z_rot  = Eigen::AngleAxisd(z_diff, Vec3d::UnitZ());
		bbox 		= bbox.transformed(Transform3d(z_rot));
		bbox_center = (z_rot * bbox_center).eval();
	}

    // Center of the transformed mesh (without translation).
    m_center_offset = Point::new_scale(bbox_center.x(), bbox_center.y());
    // Size of the transformed mesh. This bounding may not be snug in XY plane, but it is snug in Z.
    m_size = (bbox.size() * (1. / SCALING_FACTOR)).cast<coord_t>();
    m_max_z = scaled(model_object->instance_bounding_box(0).max(2));

    this->set_instances(std::move(instances));
}

PrintObject::~PrintObject()
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": this=%1%, m_shared_object %2%")%this%m_shared_object;
    if (m_shared_regions && -- m_shared_regions->m_ref_cnt == 0) delete m_shared_regions;
    clear_layers();
    clear_support_layers();
}

PrintBase::ApplyStatus PrintObject::set_instances(PrintInstances &&instances)
{
    for (PrintInstance &i : instances)
    	// Add the center offset, which will be subtracted from the mesh when slicing.
    	i.shift += m_center_offset;
    // Invalidate and set copies.
    PrintBase::ApplyStatus status = PrintBase::APPLY_STATUS_UNCHANGED;
    bool equal_length = instances.size() == m_instances.size();
    bool equal = equal_length && std::equal(instances.begin(), instances.end(), m_instances.begin(),
    	[](const PrintInstance& lhs, const PrintInstance& rhs) { return lhs.model_instance == rhs.model_instance && lhs.shift == rhs.shift; });
    if (! equal) {
        status = PrintBase::APPLY_STATUS_CHANGED;
        if (m_print->invalidate_steps({psWipeTower,psSkirtBrim, psGCodeExport}) ||
            (! equal_length && m_print->invalidate_step(psWipeTower)))
            status = PrintBase::APPLY_STATUS_INVALIDATED;
        m_instances = std::move(instances);
	    for (PrintInstance &i : m_instances)
	    	i.print_object = this;
    }
    return status;
}

//BBS:
void PrintObject::get_certain_layers(float start, float end, std::vector<LayerPtrs> &out, std::vector<BoundingBox> &boundingbox_objects)
{
    BoundingBox temp;
    LayerPtrs   out_temp;
    for (const auto &layer : layers()) {
        if (layer->print_z < start) continue;

        if (layer->print_z > end + EPSILON) break;
        temp.merge(layer->loverhangs_bbox);
        out_temp.emplace_back(layer);
    }
    boundingbox_objects.emplace_back(std::move(temp));
    out.emplace_back(std::move(out_temp));
};

std::vector<Point> PrintObject::get_instances_shift_without_plate_offset() const
{
    std::vector<Point> out;
    out.reserve(m_instances.size());
    for (const auto& instance : m_instances)
        out.push_back(instance.shift_without_plate_offset());

    return out;
}


// BBS
BoundingBox PrintObject::get_first_layer_bbox(float &a, float &layer_height, std::string &name)
{
    BoundingBox bbox;
    a    = 0;
    name = this->model_object()->name;
    if (layer_count() > 0) {
        auto layer   = get_layer(0);
        layer_height = layer->height;
        auto shift   = instances()[0].shift_without_plate_offset();
        for (auto bb : layer->lslices_bboxes) {
            bb.translate(shift.x(), shift.y());
            bbox.merge(bb);
        }
        for (auto slice : layer->lslices)
            a += area(slice);
    }
    if (has_brim())
        bbox = firstLayerObjectBrimBoundingBox;
    return bbox;
}

std::vector<std::reference_wrapper<const PrintRegion>> PrintObject::all_regions() const
{
    std::vector<std::reference_wrapper<const PrintRegion>> out;
    out.reserve(m_shared_regions->all_regions.size());
    for (const std::unique_ptr<Slic3r::PrintRegion> &region : m_shared_regions->all_regions)
        out.emplace_back(*region.get());
    return out;
}
} // namespace Slic3r
