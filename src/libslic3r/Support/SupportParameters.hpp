#pragma once
#include <boost/log/trivial.hpp>
#include "../libslic3r.h"
#include "../Flow.hpp"
#include "../PrintConfig.hpp"
#include "../Slicing.hpp"
#include "../Fill/FillBase.hpp"
#include "../Print.hpp"
#include "../Layer.hpp"
#include "SupportLayer.hpp"

namespace Slic3r {
struct SupportParameters {
    SupportParameters() = delete;
    SupportParameters(const PrintObject& object)
    {
        const PrintConfig& print_config = object.print()->config();
        const PrintObjectConfig& object_config = object.config();
        const SlicingParameters& slicing_params = object.slicing_parameters();

	    this->soluble_interface = slicing_params.soluble_interface;
	    this->soluble_interface_non_soluble_base =
	        // Zero z-gap between the overhangs and the support interface.
	        slicing_params.soluble_interface &&
	        // Interface extruder soluble.
	        object_config.support_interface_filament.value > 0 && print_config.filament_soluble.get_at(object_config.support_interface_filament.value - 1) &&
	        // Base extruder: Either "print with active extruder" not soluble.
	        (object_config.support_filament.value == 0 || ! print_config.filament_soluble.get_at(object_config.support_filament.value - 1));

	    {
	        this->num_top_interface_layers    = std::max(0, object_config.support_interface_top_layers.value);
	        this->num_bottom_interface_layers = object_config.support_interface_bottom_layers < 0 ?
	            num_top_interface_layers : object_config.support_interface_bottom_layers;
	        this->has_top_contacts              = num_top_interface_layers    > 0;
	        this->has_bottom_contacts           = num_bottom_interface_layers > 0;
            if (is_tree(object_config.support_type)) {
                if (this->soluble_interface_non_soluble_base) {
                    // Try to support soluble dense interfaces with non-soluble dense interfaces.
                    this->num_top_base_interface_layers    = size_t(std::min(int(num_top_interface_layers) / 2, 2));
                    this->num_bottom_base_interface_layers = size_t(std::min(int(num_bottom_interface_layers) / 2, 2));
                } else {
                    // BBS: if support interface and support base do not use the same filament, add a base layer to improve their adhesion
                    // Note: support materials (such as Supp.W) can't be used as support base now, so support interface and base are still using different filaments even if
                    // support_filament==0
                    bool differnt_support_interface_filament = object_config.support_interface_filament != 0 &&
                                                               object_config.support_interface_filament != object_config.support_filament;
                    this->num_top_base_interface_layers    = differnt_support_interface_filament ? 1 : 0;
                    this->num_bottom_base_interface_layers = differnt_support_interface_filament ? 1 : 0;
                }
            } else {
                if (this->soluble_interface_non_soluble_base) {
                    // Try to support soluble dense interfaces with non-soluble dense interfaces.
                    this->num_top_base_interface_layers    = num_top_interface_layers > 0 ? 2 : 0;
                    this->num_bottom_base_interface_layers = size_t(std::min(int(num_bottom_interface_layers) / 2, 2));
                } else {
                    // BBS: if support interface and support base do not use the same filament, add a base layer to improve their adhesion
                    // Note: support materials (such as Supp.W) can't be used as support base now, so support interface and base are still using different filaments even if
                    // support_filament==0
                    bool differnt_support_interface_filament = object_config.support_interface_filament != 0 &&
                                                               object_config.support_interface_filament != object_config.support_filament;
                    this->num_top_base_interface_layers    = num_top_interface_layers > 0 ? differnt_support_interface_filament ? 2 : 1 : 0;
                    this->num_bottom_base_interface_layers = differnt_support_interface_filament ? 1 : 0;
                }
            }
	    }
        this->first_layer_flow = Slic3r::support_material_1st_layer_flow(&object, float(slicing_params.first_print_layer_height));
        this->support_material_flow = Slic3r::support_material_flow(&object, float(slicing_params.layer_height));
        this->support_material_interface_flow = Slic3r::support_material_interface_flow(&object, float(slicing_params.layer_height));
    	this->raft_interface_flow                = support_material_interface_flow;

        // Calculate a minimum support layer height as a minimum over all extruders, but not smaller than 10um.
        this->support_layer_height_min = scaled<coord_t>(0.01);
        for (auto lh : print_config.min_layer_height.values)
            this->support_layer_height_min = std::min(this->support_layer_height_min, std::max(0.01, lh));
        for (auto layer : object.layers())
            this->support_layer_height_min = std::min(this->support_layer_height_min, std::max(0.01, layer->height));

        if (object_config.support_interface_top_layers.value == 0) {
            // No interface layers allowed, print everything with the base support pattern.
            this->support_material_interface_flow = this->support_material_flow;
        }

        // Evaluate the XY gap between the object outer perimeters and the support structures.
        // Evaluate the XY gap between the object outer perimeters and the support structures.
        coordf_t external_perimeter_width = 0.;
        coordf_t bridge_flow_ratio = 0;
        for (size_t region_id = 0; region_id < object.num_printing_regions(); ++region_id) {
            const PrintRegion& region = object.printing_region(region_id);
            external_perimeter_width = std::max(external_perimeter_width, coordf_t(region.flow(object, frExternalPerimeter, slicing_params.layer_height).width()));
            bridge_flow_ratio += region.config().bridge_flow;
        }
        this->gap_xy = object_config.support_object_xy_distance.value;
        this->gap_xy_first_layer = object_config.support_object_first_layer_gap.value;
        bridge_flow_ratio /= object.num_printing_regions();

        this->support_material_bottom_interface_flow = slicing_params.soluble_interface || !object_config.thick_bridges ?
            this->support_material_interface_flow.with_flow_ratio(bridge_flow_ratio) :
            Flow::bridging_flow(bridge_flow_ratio * this->support_material_interface_flow.nozzle_diameter(), this->support_material_interface_flow.nozzle_diameter());

        this->can_merge_support_regions = object_config.support_filament.value == object_config.support_interface_filament.value;
        if (!this->can_merge_support_regions && (object_config.support_filament.value == 0 || object_config.support_interface_filament.value == 0)) {
            // One of the support extruders is of "don't care" type.
            auto object_extruders = object.object_extruders();
            if (object_extruders.size() == 1 &&
                // object_extruders are 0-based but object_config.support_filament's are 1-based
                object_extruders[0] + 1 == std::max<unsigned int>(object_config.support_filament.value, object_config.support_interface_filament.value))
                // Object is printed with the same extruder as the support.
                this->can_merge_support_regions = true;
        }


        this->base_angle = Geometry::deg2rad(float(object_config.support_angle.value));
        this->interface_angle = Geometry::deg2rad(float(object_config.support_angle.value + 90.));
        this->interface_spacing = object_config.support_interface_spacing.value + this->support_material_interface_flow.spacing();
        this->interface_density = std::min(1., this->support_material_interface_flow.spacing() / this->interface_spacing);
	    double raft_interface_spacing = object_config.support_interface_spacing.value + this->raft_interface_flow.spacing();
	    this->raft_interface_density = std::min(1., this->raft_interface_flow.spacing() / raft_interface_spacing);
        this->support_spacing = object_config.support_base_pattern_spacing.value + this->support_material_flow.spacing();
        this->support_density = std::min(1., this->support_material_flow.spacing() / this->support_spacing);
        if (object_config.support_interface_top_layers.value == 0) {
            // No interface layers allowed, print everything with the base support pattern.
            this->interface_spacing = this->support_spacing;
            this->interface_density = this->support_density;
        }

        support_style = object_config.support_style;
        if (support_style != smsDefault) {
            if ((support_style == smsSnug || support_style == smsGrid) && is_tree(object_config.support_type)) support_style = smsDefault;
            if ((support_style == smsTreeSlim || support_style == smsTreeStrong || support_style == smsTreeHybrid || support_style == smsTreeOrganic) &&
                !is_tree(object_config.support_type))
                support_style = smsDefault;
        }
        if (support_style == smsDefault) {
            if (is_tree(object_config.support_type)) {
                // organic support doesn't work with variable layer heights (including adaptive layer height and height range modifier, see #4313)
                if (!object.has_variable_layer_heights && !slicing_params.soluble_interface) {
                    BOOST_LOG_TRIVIAL(warning) << "tree support default to organic support";
                    support_style = smsTreeOrganic;
                } else {
                    BOOST_LOG_TRIVIAL(warning) << "tree support default to hybrid tree due to adaptive layer height";
                    support_style = smsTreeHybrid;
                }
            } else {
                support_style = smsGrid;
            }
        }

        support_base_pattern = object_config.support_base_pattern;
        if (support_base_pattern == smpLightning && !is_tree(object_config.support_type)) support_base_pattern = smpRectilinear;
        if (support_base_pattern == smpDefault) {
            if (is_tree(object_config.support_type))
                support_base_pattern = support_style == smsTreeHybrid ? smpRectilinear : smpNone;
            else
                support_base_pattern = smpRectilinear;
        }

        this->with_sheath = object_config.tree_support_wall_count > 0;
        this->base_fill_pattern           = support_base_pattern == smpLightning              ? ipLightning :
                                            support_base_pattern == smpHoneycomb              ? ipHoneycomb :
                                            this->support_density > 0.95 || this->with_sheath ? ipRectilinear :
                                                                                                ipSupportBase;
        this->interface_fill_pattern = (this->interface_density > 0.95 ? ipRectilinear : ipSupportBase);
		this->raft_interface_fill_pattern = this->raft_interface_density > 0.95 ? ipRectilinear : ipSupportBase;
        if (object_config.support_interface_pattern == smipGrid)
            this->contact_fill_pattern = ipGrid;
        else if (object_config.support_interface_pattern == smipRectilinearInterlaced || object_config.support_interface_pattern == smipAuto)
            this->contact_fill_pattern = ipRectilinear;
        else
            this->contact_fill_pattern = object_config.support_interface_pattern == smipConcentric ?
            ipConcentric :
            (this->interface_density > 0.95 ? ipRectilinear : ipSupportBase);

		this->raft_angle_1st_layer  = 0.f;
	    this->raft_angle_base       = 0.f;
	    this->raft_angle_interface  = 0.f;
	    if (slicing_params.base_raft_layers > 1) {
	        assert(slicing_params.raft_layers() >= 4);
	        // There are all raft layer types (1st layer, base, interface & contact layers) available.
	        this->raft_angle_1st_layer  = this->interface_angle;
	        this->raft_angle_base       = this->base_angle;
	        this->raft_angle_interface  = this->interface_angle;
	        if ((slicing_params.interface_raft_layers & 1) == 0)
	            // Allign the 1st raft interface layer so that the object 1st layer is hatched perpendicularly to the raft contact interface.
	            this->raft_angle_interface += float(0.5 * M_PI);
	    } else if (slicing_params.base_raft_layers == 1 || slicing_params.interface_raft_layers > 1) {
	        assert(slicing_params.raft_layers() == 2 || slicing_params.raft_layers() == 3);
	        // 1st layer, interface & contact layers available.
	        this->raft_angle_1st_layer  = this->base_angle;
	        this->raft_angle_interface  = this->interface_angle + 0.5 * M_PI;
	    } else if (slicing_params.interface_raft_layers == 1) {
	        // Only the contact raft layer is non-empty, which will be printed as the 1st layer.
	        assert(slicing_params.base_raft_layers == 0);
	        assert(slicing_params.interface_raft_layers == 1);
	        assert(slicing_params.raft_layers() == 1);
	        this->raft_angle_1st_layer = float(0.5 * M_PI);
	        this->raft_angle_interface = this->raft_angle_1st_layer;
	    } else {
	        // No raft.
	        assert(slicing_params.base_raft_layers == 0);
	        assert(slicing_params.interface_raft_layers == 0);
	        assert(slicing_params.raft_layers() == 0);
	    }

        support_extrusion_width = object_config.support_line_width.value > 0 ? object_config.support_line_width : object_config.line_width;
        // Check if set to zero, use default if so.
        if (support_extrusion_width <= 0.0) {
            const auto nozzle_diameter               = print_config.nozzle_diameter.get_at(object_config.support_interface_filament - 1);
            support_extrusion_width = Flow::auto_extrusion_width(FlowRole::frSupportMaterial, (float) nozzle_diameter);
        }

        double tree_support_branch_diameter_double_wall = 3.0;
        // get support filament strength and decide the thresh of double wall area
        float support_filament_strength = print_config.impact_strength_z.get_at(object_config.support_filament-1);
        if(object_config.support_filament==0){
            // find the weakest filament
            support_filament_strength = std::numeric_limits<float>::max();
            for(auto extruder:object.object_extruders()){
                float strength = print_config.impact_strength_z.get_at(extruder);
                if(strength<support_filament_strength) support_filament_strength = strength;
            }
        }

        if(object_config.tree_support_wall_count.value==0){
            tree_support_branch_diameter_double_wall = support_filament_strength;
            this->tree_branch_diameter_double_wall_area_scaled = 0.25*sqr(scaled<double>(tree_support_branch_diameter_double_wall))*M_PI;
        }else{
        // force double walls everywhere if wall count is larger than 1
            this->tree_branch_diameter_double_wall_area_scaled = object_config.tree_support_wall_count.value>1? 0.1: std::numeric_limits<double>::max();
        }

        independent_layer_height = print_config.independent_support_layer_height;
    }
	// Both top / bottom contacts and interfaces are soluble.
    bool                    soluble_interface;
    // Support contact & interface are soluble, but support base is non-soluble.
    bool                    soluble_interface_non_soluble_base;

    // Is there at least a top contact layer extruded above support base?
    bool has_top_contacts;
    // Is there at least a bottom contact layer extruded below support base?
    bool has_bottom_contacts;

    // Number of top interface layers without counting the contact layer.
    size_t num_top_interface_layers;
    // Number of bottom interface layers without counting the contact layer.
    size_t num_bottom_interface_layers;
    // Number of top base interface layers. Zero if not soluble_interface_non_soluble_base.
    size_t num_top_base_interface_layers;
    // Number of bottom base interface layers. Zero if not soluble_interface_non_soluble_base.
    size_t num_bottom_base_interface_layers;

    bool                    has_contacts() const { return this->has_top_contacts || this->has_bottom_contacts; }
    bool                    has_interfaces() const { return this->num_top_interface_layers + this->num_bottom_interface_layers > 0; }
    bool                    has_base_interfaces() const { return this->num_top_base_interface_layers + this->num_bottom_base_interface_layers > 0; }
    size_t                  num_top_interface_layers_only() const { return std::max(0, int(this->num_top_interface_layers) - int(this->num_top_base_interface_layers)); }
    size_t                  num_bottom_interface_layers_only() const { return this->num_bottom_interface_layers - this->num_bottom_base_interface_layers; }
    Flow 		first_layer_flow;
    Flow 		support_material_flow;
    Flow 		support_material_interface_flow;
    Flow 		support_material_bottom_interface_flow;
	// Flow at raft inteface & contact layers.
	Flow    				raft_interface_flow;
    coordf_t support_extrusion_width;
    // Is merging of regions allowed? Could the interface & base support regions be printed with the same extruder?
    bool 		can_merge_support_regions;

    coordf_t 	support_layer_height_min;
    //	coordf_t	support_layer_height_max;

    coordf_t	gap_xy;
    coordf_t	gap_xy_first_layer;

    float    				base_angle;
    float    				interface_angle;
    coordf_t 				interface_spacing;
    coordf_t				support_expansion=0;
    coordf_t 				interface_density;
    // Density of the raft interface and contact layers.
    coordf_t 				raft_interface_density;
    coordf_t 				support_spacing;
    coordf_t 				support_density;
    SupportMaterialStyle    support_style = smsDefault;
    SupportMaterialPattern  support_base_pattern = smpDefault;

    InfillPattern           base_fill_pattern;
    InfillPattern           interface_fill_pattern;
    // Pattern of the raft interface and contact layers.
    InfillPattern           raft_interface_fill_pattern;
    InfillPattern 			contact_fill_pattern;
    bool                    with_sheath;
    // Branches of organic supports with area larger than this threshold will be extruded with double lines.
    double                  tree_branch_diameter_double_wall_area_scaled = 0.25 * sqr(scaled<double>(5.0)) * M_PI;;

    float 					raft_angle_1st_layer;
    float 					raft_angle_base;
    float 					raft_angle_interface;

    // Produce a raft interface angle for a given SupportLayer::interface_id()
    float 					raft_interface_angle(size_t interface_id) const
    	{ return this->raft_angle_interface + ((interface_id & 1) ? float(- M_PI / 4.) : float(+ M_PI / 4.)); }

    bool independent_layer_height = false;
    const double thresh_big_overhang = /*Slic3r::sqr(scale_(10))*/scale_(10);
};
} // namespace Slic3r