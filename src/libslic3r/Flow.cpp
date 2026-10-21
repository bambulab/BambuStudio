#include "Flow.hpp"
#include "I18N.hpp"
#include "Print.hpp"
#include <cmath>
#include <assert.h>

#include <boost/algorithm/string/predicate.hpp>
#include "libslic3r/Log.hpp"

// Mark string for localization and translate.
#define L(s) Slic3r::I18N::translate(s)

namespace Slic3r {

FlowErrorNegativeSpacing::FlowErrorNegativeSpacing() : 
	FlowError("Flow::spacing() produced negative spacing. Did you set some extrusion width too small?") {}

FlowErrorNegativeFlow::FlowErrorNegativeFlow() :
    FlowError("Flow::mm3_per_mm() produced negative flow. Did you set some extrusion width too small?") {}

// This static method returns a sane extrusion width default.
float Flow::auto_extrusion_width(FlowRole role, float nozzle_diameter)
{
    switch (role) {
    case frSupportMaterial:
    case frSupportMaterialInterface:
    case frSupportTransition:
    case frTopSolidInfill:
        return nozzle_diameter;
    default:
    case frExternalPerimeter:
    case frPerimeter:
    case frSolidInfill:
    case frInfill:
        return 1.125f * nozzle_diameter;
    }
}

// Used by the Flow::extrusion_width() funtion to provide hints to the user on default extrusion width values,
// and to provide reasonable values to the PlaceholderParser.
static inline FlowRole opt_key_to_flow_role(const std::string &opt_key)
{
 	if (opt_key == "inner_wall_line_width" || 
 		// or all the defaults:
 		opt_key == "line_width" || opt_key == "initial_layer_line_width")
        return frPerimeter;
    else if (opt_key == "outer_wall_line_width")
        return frExternalPerimeter;
    else if (opt_key == "sparse_infill_line_width" || opt_key == "skeleton_infill_line_width" ||
             opt_key == "initial_layer_infill_line_width")
        return frInfill;
    else if (opt_key == "internal_solid_infill_line_width" || opt_key == "skin_infill_line_width")
        return frSolidInfill;
	else if (opt_key == "top_surface_line_width")
		return frTopSolidInfill;
	else if (opt_key == "support_line_width")
    	return frSupportMaterial;
    else 
    	throw Slic3r::RuntimeError("opt_key_to_flow_role: invalid argument");
};

static inline void throw_on_missing_variable(const std::string &opt_key, const char *dependent_opt_key) 
{
	throw FlowErrorMissingVariable((boost::format(L("Failed to calculate line width of %1%. Can not get value of \"%2%\" ")) % opt_key % dependent_opt_key).str());
}

// Used to provide hints to the user on default extrusion width values, and to provide reasonable values to the PlaceholderParser.
double Flow::extrusion_width(const std::string& opt_key, const ConfigOptionFloat* opt, const ConfigOptionResolver& config, const unsigned int first_printing_extruder)
{
	assert(opt != nullptr);

	if (opt->value == 0.) {
		// The role specific extrusion width value was set to zero, try the role non-specific extrusion width.
		opt = config.option<ConfigOptionFloat>("line_width");
		if (opt == nullptr)
    		throw_on_missing_variable(opt_key, "line_width");
	}

	if (opt->value == 0.) {
        // If user left option to 0, calculate a sane default width.
    	auto opt_nozzle_diameters = config.option<ConfigOptionFloatsNullable>("nozzle_diameter");
    	if (opt_nozzle_diameters == nullptr)
    		throw_on_missing_variable(opt_key, "nozzle_diameter");
        return auto_extrusion_width(opt_key_to_flow_role(opt_key), float(opt_nozzle_diameters->get_at(first_printing_extruder)));
    }

	return opt->value;
}

// Used to provide hints to the user on default extrusion width values, and to provide reasonable values to the PlaceholderParser.
double Flow::extrusion_width(const std::string& opt_key, const ConfigOptionResolver &config, const unsigned int first_printing_extruder)
{
    return extrusion_width(opt_key, config.option<ConfigOptionFloat>(opt_key), config, first_printing_extruder);
}

double Flow::extrusion_width_for_nozzle(
    const ConfigOptionFloat &width, float preset_nozzle_diameter, float nozzle_diameter,
    bool multi_nozzle_diameter_mode)
{
    if (!multi_nozzle_diameter_mode || width.value == 0. ||
        preset_nozzle_diameter <= 0.f || nozzle_diameter <= 0.f)
        return width.value;

    // The GUI keeps showing the absolute width authored by the process preset. Convert it to an
    // intermediate percentage and reuse the existing percent-to-absolute conversion for the actual nozzle.
    const ConfigOptionFloatOrPercent relative_width(100. * width.value / preset_nozzle_diameter, true);
    return relative_width.get_abs_value(nozzle_diameter);
}

double Flow::resolve_extrusion_width_for_nozzle(
    const std::string &opt_key,
    const ConfigOptionResolver &config,
    float preset_nozzle_diameter,
    float nozzle_diameter,
    bool multi_nozzle_diameter_mode)
{
    const ConfigOptionFloat *width = config.option<ConfigOptionFloat>(opt_key);
    if (width == nullptr)
        throw_on_missing_variable(opt_key, opt_key.c_str());

    if (width->value == 0.) {
        width = config.option<ConfigOptionFloat>("line_width");
        if (width == nullptr)
            throw_on_missing_variable(opt_key, "line_width");
    }

    if (width->value == 0.)
        return auto_extrusion_width(opt_key_to_flow_role(opt_key), nozzle_diameter);

    return extrusion_width_for_nozzle(
        *width, preset_nozzle_diameter, nozzle_diameter, multi_nozzle_diameter_mode);
}

// This constructor builds a Flow object from an extrusion width config setting
// and other context properties.
Flow Flow::new_from_config_width(
    FlowRole role,
    const ConfigOptionFloat &width,
    float preset_nozzle_diameter,
    float nozzle_diameter,
    float height,
    bool multi_nozzle_diameter_mode)
{
    if (height <= 0)
        throw Slic3r::InvalidArgument("Invalid flow height supplied to new_from_config_width()");

    float w;
    if (width.value == 0.) {
        // If user left option to 0, calculate a sane default width.
        w = auto_extrusion_width(role, nozzle_diameter);
    } else {
        w = float(extrusion_width_for_nozzle(
            width, preset_nozzle_diameter, nozzle_diameter, multi_nozzle_diameter_mode));
    }
    
    return Flow(w, height, rounded_rectangle_extrusion_spacing(w, height), nozzle_diameter, false);
}

// Adjust extrusion flow for new extrusion line spacing, maintaining the old spacing between extrusions.
Flow Flow::with_spacing(float new_spacing) const
{
    Flow out = *this;
    if (m_bridge) {
        // Diameter of the rounded extrusion.
        assert(m_width == m_height);
        float gap          = m_spacing - m_width;
        auto  new_diameter = new_spacing - gap;
        out.m_width        = out.m_height = new_diameter;
    } else {
        assert(m_width >= m_height);
        out.m_width += new_spacing - m_spacing;
        if (out.m_width < out.m_height)
            throw Slic3r::SlicingError(L("Slicing failed because the extrusion line width is too small or the layer height is too large.\n\n"
                                         "Please increase the line width or reduce the layer height, then slice again."));
    }
    out.m_spacing = new_spacing;
    return out;
}

// Adjust the width / height of a rounded extrusion model to reach the prescribed cross section area while maintaining extrusion spacing.
Flow Flow::with_cross_section(float area_new) const
{
    assert(! m_bridge);
    assert(m_width >= m_height);

    // Adjust for bridge_flow, maintain the extrusion spacing.
    float area = this->mm3_per_mm();
    if (area_new > area + EPSILON) {
        // Increasing the flow rate.
        float new_full_spacing = area_new / m_height;
        if (new_full_spacing > m_spacing) {
            // Filling up the spacing without an air gap. Grow the extrusion in height.
            float height = area_new / m_spacing;
            return Flow(rounded_rectangle_extrusion_width_from_spacing(m_spacing, height), height, m_spacing, m_nozzle_diameter, false);
        } else {
            return this->with_width(rounded_rectangle_extrusion_width_from_spacing(area / m_height, m_height));
        }
    } else if (area_new < area - EPSILON) {
        // Decreasing the flow rate.
        float width_new = m_width - (area - area_new) / m_height;
        assert(width_new > 0);
        if (width_new > m_height) {
            // Shrink the extrusion width.
            return this->with_width(width_new);
        } else {
            // Create a rounded extrusion.
            auto dmr = float(sqrt(area_new / M_PI));
            return Flow(dmr, dmr, m_spacing, m_nozzle_diameter, false);
        }
    } else
        return *this;
}

float Flow::rounded_rectangle_extrusion_spacing(float width, float height)
{
    auto out = width - height * float(1. - 0.25 * PI);
    if (out <= 0.f) {
        BOOST_LOG_TRIVIAL(error)<< boost::format("negative extrusion : width %1%   height %2%") % width % height;
        throw FlowErrorNegativeSpacing();
    }
    return out;
}

float Flow::rounded_rectangle_extrusion_width_from_spacing(float spacing, float height)
{
    return float(spacing + height * (1. - 0.25 * PI));
}

float Flow::bridge_extrusion_spacing(float dmr)
{
    return dmr + BRIDGE_EXTRA_SPACING;
}

// This method returns extrusion volume per head move unit.
double Flow::mm3_per_mm() const
{
    float res = m_bridge ?
        // Area of a circle with dmr of this->width.
        float((m_width * m_width) * 0.25 * PI) :
        // Rectangle with semicircles at the ends. ~ h (w - 0.215 h)
        float(m_height * (m_width - m_height * (1. - 0.25 * PI)));
    //assert(res > 0.);
	if (res <= 0.)
		throw FlowErrorNegativeFlow();
    return res;
}

Flow support_material_flow(const PrintObject *object, float layer_height)
{
    const float preset_nozzle_diameter = float(primary_nozzle_diameter(object->print()->full_print_config()));
    // Width conversion follows the selected nozzle even when one diameter is sliced directly and needs no merge plan.
    const bool multi_diameter_mode = multi_nozzle_diameter_mode_enabled(object->print()->full_print_config());
    return Flow::new_from_config_width(
        frSupportMaterial,
        (object->config().support_line_width.value > 0) ? object->config().support_line_width : object->config().line_width,
        preset_nozzle_diameter,
        support_nozzle_diameter(object, object->config().support_filament),
        (layer_height > 0.f) ? layer_height : float(object->config().layer_height.value),
        multi_diameter_mode);
}
//BBS
Flow support_transition_flow(const PrintObject* object)
{
    //BBS: support transition of tree support is bridge flow
    float dmr = nozzle_diameter_for_filament(object->print()->config(), object->config().support_filament);
    return Flow::bridging_flow(dmr, dmr);
}

Flow support_material_1st_layer_flow(const PrintObject *object, float layer_height)
{
    const PrintConfig &print_config = object->print()->config();
    const auto &width = (print_config.initial_layer_line_width.value > 0) ? print_config.initial_layer_line_width : object->config().support_line_width;
    return Flow::new_from_config_width(
        frSupportMaterial,
        (width.value > 0) ? width : object->config().line_width,
        float(primary_nozzle_diameter(object->print()->full_print_config())),
        support_nozzle_diameter(object, object->config().support_filament),
        (layer_height > 0.f) ? layer_height : float(print_config.initial_layer_print_height.value),
        multi_nozzle_diameter_mode_enabled(object->print()->full_print_config()));
}

Flow support_material_interface_flow(const PrintObject *object, float layer_height)
{
    return Flow::new_from_config_width(
        frSupportMaterialInterface,
        (object->config().support_line_width > 0) ? object->config().support_line_width : object->config().line_width,
        float(primary_nozzle_diameter(object->print()->full_print_config())),
        support_nozzle_diameter(object, object->config().support_interface_filament),
        (layer_height > 0.f) ? layer_height : float(object->config().layer_height.value),
        multi_nozzle_diameter_mode_enabled(object->print()->full_print_config()));
}

float support_nozzle_diameter(const PrintObject *object, unsigned int filament_id)
{
    if (filament_id == 0) {
        const double single_nozzle = object->print()->single_diameter_layering_nozzle();
        if (single_nozzle > EPSILON)
            return float(single_nozzle);
    }
    return nozzle_diameter_for_filament(object->print()->config(), filament_id);
}

}
