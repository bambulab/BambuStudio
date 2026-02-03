#include "Print.hpp"
#include "ToolOrdering.hpp"
#include "Layer.hpp"
#include "ClipperUtils.hpp"
#include "ParameterUtils.hpp"
#include "GCode/ToolOrderUtils.hpp"
#include "FilamentGroupUtils.hpp"
#include "I18N.hpp"
#include "Preset.hpp"
#include "PresetBundle.hpp"
#include "MultiNozzleUtils.hpp"

// #define SLIC3R_DEBUG

// Make assert active if SLIC3R_DEBUG
#ifdef SLIC3R_DEBUG
    #define DEBUG
    #define _DEBUG
    #undef NDEBUG
#endif

#include <cassert>
#include <limits>
#include <algorithm>
#include <unordered_map>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <chrono>
#include <iomanip>
#include <sstream>

#include <libslic3r.h>
#include <boost/log/trivial.hpp>
#include <boost/filesystem.hpp>
#include <boost/dll/runtime_symbol_info.hpp>

namespace Slic3r {

    //! macro used to mark string used at localization,
    //! return same string

#ifndef _L
#define _L(s) Slic3r::I18N::translate(s)
#endif

const static bool g_wipe_into_objects = false;
constexpr double similar_color_threshold_de2000 = 20.0;

static std::set<int>get_filament_by_type(const std::vector<unsigned int>& used_filaments, const PrintConfig* print_config, const std::string& type)
{
    std::set<int> target_filaments;
    for (unsigned int filament_id : used_filaments) {
        std::string filament_type = print_config->filament_type.get_at(filament_id);
        if (filament_type == type)
            target_filaments.insert(filament_id);
    }
    return target_filaments;
}


// Returns true in case that extruder a comes before b (b does not have to be present). False otherwise.
bool LayerTools::is_extruder_order(unsigned int a, unsigned int b) const
{
    if (a == b)
        return false;

    for (auto extruder : extruders) {
        if (extruder == a)
            return true;
        if (extruder == b)
            return false;
    }

    return false;
}

bool check_filament_printable_after_group(const std::vector<unsigned int> &used_filaments, const std::vector<int> &filament_maps, std::unordered_map<int, std::vector<std::string>>& filament_variants, const PrintConfig *print_config)
{
    // TODO(shancang): Generic filament of XP printers
    auto diameters = print_config->nozzle_diameter;
    std::unordered_map<std::string, int> nozzle_fils;
    for (unsigned int filament_id : used_filaments) {
        std::string filament_type = print_config->filament_type.get_at(filament_id);
        int printable_status = print_config->filament_printable.get_at(filament_id);
        int extruder_idx = filament_maps[filament_id];
        if (!(printable_status >> extruder_idx & 1)) {
            std::string extruder_name = extruder_idx == 0 ? _L("left") : _L("right");
            std::string error_msg     = _L("Grouping error: filament") + std::to_string(filament_id + 1) + _L(" can not be placed in the ") + extruder_name + _L(" nozzle");
            throw Slic3r::RuntimeError(error_msg);
        }

        if (diameters.size() < 2)
            continue;

        std::string extruder_variant = print_config->option<ConfigOptionStrings>("printer_extruder_variant")->values.at(extruder_idx);
        std::unordered_set<std::string> filament_variants_set(filament_variants[filament_id].begin(), filament_variants[filament_id].end());
        if (filament_variants_set.count(extruder_variant) == 0){
            NozzleVolumeType variant_name = convert_to_nvt_type(extruder_variant);
            auto             volume_names = ConfigOptionEnum<NozzleVolumeType>::get_enum_names();
            std::string      volume       = volume_names.at(variant_name);
            std::string error_msg = _L("Filament Grouping Error: Filament ") + std::to_string(filament_id + 1) + _L(" is not supported by the ") + _L(volume) +
                                    _L(" nozzle. ") + _L("Please change the nozzle type or reassign the filament and try again.");

            throw Slic3r::RuntimeError(error_msg);
        }

        nozzle_fils[extruder_variant]++;
    }
    return true;
}

// Return a zero based extruder from the region, or extruder_override if overriden.
unsigned int LayerTools::wall_filament(const PrintRegion &region) const
{
	assert(region.config().wall_filament.value > 0);
	return ((this->extruder_override == 0) ? region.config().wall_filament.value : this->extruder_override) - 1;
}

unsigned int LayerTools::sparse_infill_filament(const PrintRegion &region) const
{
	assert(region.config().sparse_infill_filament.value > 0);
	return ((this->extruder_override == 0) ? region.config().sparse_infill_filament.value : this->extruder_override) - 1;
}

unsigned int LayerTools::solid_infill_filament(const PrintRegion &region) const
{
	assert(region.config().solid_infill_filament.value > 0);
	return ((this->extruder_override == 0) ? region.config().solid_infill_filament.value : this->extruder_override) - 1;
}

// Returns a zero based extruder this eec should be printed with, according to PrintRegion config or extruder_override if overriden.
unsigned int LayerTools::extruder(const ExtrusionEntityCollection &extrusions, const PrintRegion &region) const
{
	assert(region.config().wall_filament.value >= 0);
	assert(region.config().sparse_infill_filament.value >= 0);
	assert(region.config().solid_infill_filament.value >= 0);
	// 1 based extruder ID.
    unsigned int extruder = 0;
    // use separate filament for different features only if the potion was open
    if (this->extruder_override == 0) {
        ExtrusionRole extrusions_role = extrusions.role();
        if (is_perimeter(extrusions_role)) {
            extruder = region.config().wall_filament.value;
        } else if (is_solid_infill(extrusions_role)) {
            extruder = region.config().solid_infill_filament.value;
        } else if (is_infill(extrusions_role)) {
            extruder = region.config().sparse_infill_filament.value;
        } else if (extrusions_role == ExtrusionRole::erMixed) {
            //this set of extrusions contain multi roles, follow a Priority to decide which to use
            int curr_priority = 0;
            for (const ExtrusionEntity *ee : extrusions.entities)
                if (ee->role() == ExtrusionRole::erTopSolidInfill) {
                    if (curr_priority < 100) { // top surface 1st priority
                        extruder      = region.config().solid_infill_filament.value;
                        curr_priority = 100;
                    }
                } else if (is_perimeter(ee->role())) {
                    if (curr_priority < 90) { // perimeter 2st priority
                        extruder      = region.config().wall_filament.value;
                        curr_priority = 90;
                    }
                } else if (is_solid_infill(extrusions_role)) {
                    if (curr_priority < 70) { // solid infill 4st priority
                        extruder      = region.config().solid_infill_filament.value;
                        curr_priority = 70;
                    }
                } else if (is_infill(extrusions_role)) {
                    if (curr_priority < 60) { // infill 5st priority
                        extruder      = region.config().sparse_infill_filament.value;
                        curr_priority = 60;
                    }
                }
            if (curr_priority == 0) // default
                extruder = region.config().wall_filament.value;
        }else
            extruder = region.config().wall_filament.value;
    }else
        extruder = this->extruder_override;
	return (extruder == 0) ? 0 : extruder - 1;
}

static double calc_max_layer_height(const PrintConfig &config, double max_object_layer_height)
{
    double max_layer_height = std::numeric_limits<double>::max();
    for (size_t i = 0; i < config.nozzle_diameter.values.size(); ++ i) {
        double mlh = config.max_layer_height.values[i];
        if (mlh == 0.)
            mlh = 0.75 * config.nozzle_diameter.values[i];
        max_layer_height = std::min(max_layer_height, mlh);
    }
    // The Prusa3D Fast (0.35mm layer height) print profile sets a higher layer height than what is normally allowed
    // by the nozzle. This is a hack and it works by increasing extrusion width. See GH #3919.
    return std::max(max_layer_height, max_object_layer_height);
}

//calculate the flush weight (first value) and filament change count(second value)
static FilamentChangeStats calc_filament_change_info_by_toolorder(const PrintConfig& config, const MultiNozzleUtils::LayeredNozzleGroupResult& group_result, const std::vector<FlushMatrix>& flush_matrix, const std::vector<std::vector<unsigned int>>& layer_sequences)
{
    FilamentChangeStats ret;
    std::unordered_map<int, int> flush_volume_per_filament;

    MultiNozzleUtils::NozzleStatusRecorder recorder;
    int total_filament_change_count = 0;
    int total_flush_filament_change_count = 0;
    float total_filament_flush_weight = 0;

    int old_filament_id = -1;
    for(size_t layer_idx = 0; layer_idx< layer_sequences.size(); ++layer_idx){
        const auto& ls = layer_sequences[layer_idx];
        for (const auto& filament : ls) {
            auto nozzle = group_result.get_nozzle_for_filament(filament, layer_idx);
            if (!nozzle)
                continue;

            int new_extruder_id = nozzle->extruder_id;
            int new_nozzle_id_in_extruder = nozzle->group_id;
            int new_filament_id_in_nozzle = filament;
            int old_filament_id_in_nozzle = recorder.get_filament_in_nozzle(new_nozzle_id_in_extruder);

            bool filament_in_nozzle_change = old_filament_id_in_nozzle != -1 && new_filament_id_in_nozzle != old_filament_id_in_nozzle;
            bool filament_change           = old_filament_id != -1 && old_filament_id != new_filament_id_in_nozzle;

            if (filament_in_nozzle_change) {
                total_flush_filament_change_count++;
                int flush_volume = flush_matrix[new_extruder_id][old_filament_id_in_nozzle][new_filament_id_in_nozzle];
                flush_volume_per_filament[filament] += flush_volume;
            }
            if (filament_change)
                total_filament_change_count++;
            old_filament_id = new_filament_id_in_nozzle;
            recorder.set_nozzle_status(new_nozzle_id_in_extruder, new_filament_id_in_nozzle, new_extruder_id);
        }
    }

    for (auto& fv : flush_volume_per_filament) {
        float weight = config.filament_density.get_at(fv.first) * 0.001 * fv.second;
        total_filament_flush_weight += weight;
    }

    ret.filament_change_count = total_filament_change_count;
    ret.flush_filament_change_count = total_flush_filament_change_count;
    ret.filament_flush_weight = (int)total_filament_flush_weight;

    return ret;
}

void ToolOrdering::handle_dontcare_extruder(const std::vector<unsigned int>& tool_order_layer0)
{
    if(m_layer_tools.empty() || tool_order_layer0.empty())
        return;

    // Reorder the extruders of first layer
    {
        LayerTools& lt = m_layer_tools[0];
        std::vector<unsigned int> layer0_extruders = lt.extruders;
        lt.extruders.clear();
        for (unsigned int extruder_id : tool_order_layer0) {
            auto iter = std::find(layer0_extruders.begin(), layer0_extruders.end(), extruder_id);
            if (iter != layer0_extruders.end()) {
                lt.extruders.push_back(extruder_id);
                *iter = (unsigned int)-1;
            }
        }

        for (unsigned int extruder_id : layer0_extruders) {
            if (extruder_id == 0)
                continue;

            if (extruder_id != (unsigned int)-1)
                lt.extruders.push_back(extruder_id);
        }

        // all extruders are zero
        if (lt.extruders.empty()) {
            lt.extruders.push_back(tool_order_layer0[0]);
        }
    }

    int last_extruder_id = m_layer_tools[0].extruders.back();
    for (int i = 1; i < m_layer_tools.size(); i++) {
        LayerTools& lt = m_layer_tools[i];

        if (lt.extruders.empty())
            continue;
        if (lt.extruders.size() == 1 && lt.extruders.front() == 0)
            lt.extruders.front() = last_extruder_id;
        else {
            if (lt.extruders.front() == 0)
                // Pop the "don't care" extruder, the "don't care" region will be merged with the next one.
                lt.extruders.erase(lt.extruders.begin());
            // Reorder the extruders to start with the last one.
            for (size_t i = 1; i < lt.extruders.size(); ++i)
                if (lt.extruders[i] == last_extruder_id) {
                    // Move the last extruder to the front.
                    memmove(lt.extruders.data() + 1, lt.extruders.data(), i * sizeof(unsigned int));
                    lt.extruders.front() = last_extruder_id;
                    break;
                }
        }
        last_extruder_id = lt.extruders.back();
    }

    // Reindex the extruders, so they are zero based, not 1 based.
    for (LayerTools& lt : m_layer_tools){
        for (unsigned int& extruder_id : lt.extruders) {
            assert(extruder_id > 0);
            --extruder_id;
        }
    }
}

void ToolOrdering::handle_dontcare_extruder(unsigned int last_extruder_id)
{
    if(m_layer_tools.empty())
        return;
    if(last_extruder_id == (unsigned int)-1){
        // The initial print extruder has not been decided yet.
        // Initialize the last_extruder_id with the first non-zero extruder id used for the print.
        last_extruder_id = 0;
        for (size_t i = 0; i < m_layer_tools.size() && last_extruder_id == 0; ++ i) {
            const LayerTools &lt = m_layer_tools[i];
            for (unsigned int extruder_id : lt.extruders)
                if (extruder_id > 0) {
                    last_extruder_id = extruder_id;
                    break;
                }
        }
        if (last_extruder_id == 0)
            // Nothing to extrude.
            return;
    }else{
        // 1 based idx
        ++ last_extruder_id;
    }

    for (LayerTools &lt : m_layer_tools) {
        if (lt.extruders.empty())
            continue;
        if (lt.extruders.size() == 1 && lt.extruders.front() == 0)
            lt.extruders.front() = last_extruder_id;
        else {
            if (lt.extruders.front() == 0)
                // Pop the "don't care" extruder, the "don't care" region will be merged with the next one.
                lt.extruders.erase(lt.extruders.begin());
            // Reorder the extruders to start with the last one.
            for (size_t i = 1; i < lt.extruders.size(); ++ i)
                if (lt.extruders[i] == last_extruder_id) {
                    // Move the last extruder to the front.
                    memmove(lt.extruders.data() + 1, lt.extruders.data(), i * sizeof(unsigned int));
                    lt.extruders.front() = last_extruder_id;
                    break;
                }

            // On first layer with wipe tower, prefer a soluble extruder
            // at the beginning, so it is not wiped on the first layer.
            if (lt == m_layer_tools[0] && m_print_config_ptr && m_print_config_ptr->enable_prime_tower) {
                for (size_t i = 0; i<lt.extruders.size(); ++i)
                    if (m_print_config_ptr->filament_soluble.get_at(lt.extruders[i]-1)) { // 1-based...
                        std::swap(lt.extruders[i], lt.extruders.front());
                        break;
                    }
            }
        }
        last_extruder_id = lt.extruders.back();
    }

    // Reindex the extruders, so they are zero based, not 1 based.
    for (LayerTools &lt : m_layer_tools){
        for (unsigned int &extruder_id : lt.extruders) {
            assert(extruder_id > 0);
            -- extruder_id;
        }
    }
}

void ToolOrdering::sort_and_build_data(const Print& print, unsigned int first_extruder, bool prime_multi_material)
{
    // if first extruder is -1, we can decide the first layer tool order before doing reorder function
    // so we shouldn't reorder first layer in reorder function
    bool reorder_first_layer = (first_extruder != (unsigned int)(-1));
    reorder_extruders_for_minimum_flush_volume(reorder_first_layer);
    m_sorted = true;

    double max_layer_height = 0.;
    double object_bottom_z = 0.;
    for (const auto& object : print.objects()) {
        for (const Layer* layer : object->layers()) {
            if (layer->has_extrusions()) {
                object_bottom_z = layer->print_z - layer->height;
                break;
            }
        }
        max_layer_height = std::max(max_layer_height, object->config().layer_height.value);
    }

    max_layer_height = calc_max_layer_height(print.config(), max_layer_height);

    this->collect_extruder_statistics(prime_multi_material);

    this->fill_wipe_tower_partitions(print.config(), object_bottom_z, max_layer_height);

    this->calc_most_used_extruder(print.config());
}

void ToolOrdering::sort_and_build_data(const PrintObject& object , unsigned int first_extruder, bool prime_multi_material)
{
    // if first extruder is -1, we can decide the first layer tool order before doing reorder function
    // so we shouldn't reorder first layer in reorder function
    //bool reorder_first_layer = (first_extruder != (unsigned int)(-1));
    bool reorder_first_layer = false; // when by object , the first layer of each object should use the best order, regard less of the filament change for better bed contect
    reorder_extruders_for_minimum_flush_volume(reorder_first_layer);
    m_sorted = true;

    double max_layer_height = calc_max_layer_height(object.print()->config(), object.config().layer_height);

    this->collect_extruder_statistics(prime_multi_material);

    this->fill_wipe_tower_partitions(object.print()->config(), object.layers().front()->print_z - object.layers().front()->height, max_layer_height);

    this->calc_most_used_extruder(object.print()->config());
}


// For the use case when each object is printed separately
// (print->config().print_sequence == PrintSequence::ByObject is true).
ToolOrdering::ToolOrdering(const PrintObject &object, unsigned int first_extruder, bool prime_multi_material)
{
    m_print_object_ptr = &object;
    m_print = const_cast<Print*>(object.print());
    if (object.layers().empty())
        return;

    // Initialize the print layers for just a single object.
    {
        // construct layer tools by z height
        std::vector<coordf_t> zs;
        zs.reserve(zs.size() + object.layers().size() + object.support_layers().size());
        for (auto layer : object.layers())
            zs.emplace_back(layer->print_z);
        for (auto layer : object.support_layers())
            zs.emplace_back(layer->print_z);
        this->initialize_layers(zs);
    }

    // Collect extruders reuqired to print the layers. Add dontcare extruders
    this->collect_extruders(object, std::vector<std::pair<double, unsigned int>>());

    // BBS
    // Reorder the extruders to minimize tool switches.
    const ConfigOptionInts *first_layer_print_sequence_op = object.print()->full_print_config().option<ConfigOptionInts>("first_layer_print_sequence");
    if (first_extruder == (unsigned int) -1 || (first_layer_print_sequence_op && first_layer_print_sequence_op->size()>1))
        this->handle_dontcare_extruder(generate_first_layer_tool_order(object));
    else
        this->handle_dontcare_extruder(first_extruder);

    this->collect_extruder_statistics(prime_multi_material);

    double max_layer_height = calc_max_layer_height(object.print()->config(), object.config().layer_height);

    this->mark_skirt_layers(object.print()->config(), max_layer_height);
}

// For the use case when all objects are printed at once.
// (print->config().print_sequence == PrintSequence::ByObject is false).
ToolOrdering::ToolOrdering(const Print &print, unsigned int first_extruder, bool prime_multi_material)
{
    m_print = const_cast<Print *>(&print);  // for update the context of print
    m_print_config_ptr = &print.config();

    // Initialize the print layers for all objects and all layers.
    coordf_t max_layer_height = 0.;
    {
        std::vector<coordf_t> zs;
        for (auto object : print.objects()) {
            zs.reserve(zs.size() + object->layers().size() + object->support_layers().size());
            for (auto layer : object->layers())
                zs.emplace_back(layer->print_z);
            for (auto layer : object->support_layers())
                zs.emplace_back(layer->print_z);

            max_layer_height = std::max(max_layer_height, object->config().layer_height.value);
        }
        this->initialize_layers(zs);
    }
    max_layer_height = calc_max_layer_height(print.config(), max_layer_height);

	// Use the extruder switches from Model::custom_gcode_per_print_z to override the extruder to print the object.
	// Do it only if all the objects were configured to be printed with a single extruder.
	std::vector<std::pair<double, unsigned int>> per_layer_extruder_switches;

    // BBS
	if (auto num_filaments = unsigned(print.config().filament_diameter.size());
		num_filaments > 1 && print.object_extruders().size() == 1 && // the current Print's configuration is CustomGCode::MultiAsSingle
        //BBS: replace model custom gcode with current plate custom gcode
        print.model().get_curr_plate_custom_gcodes().mode == CustomGCode::MultiAsSingle) {
		// Printing a single extruder platter on a printer with more than 1 extruder (or single-extruder multi-material).
		// There may be custom per-layer tool changes available at the model.
        per_layer_extruder_switches = custom_tool_changes(print.model().get_curr_plate_custom_gcodes(), num_filaments);
	}

    // Collect extruders reuqired to print the layers.
    for (auto object : print.objects())
        this->collect_extruders(*object, per_layer_extruder_switches);

    // Reorder the extruders to minimize tool switches.
    std::vector<unsigned int> first_layer_tool_order;
    if (first_extruder == (unsigned int)-1) {
        first_layer_tool_order = generate_first_layer_tool_order(print);
    }

    if(!first_layer_tool_order.empty())
        this->handle_dontcare_extruder(first_layer_tool_order);
    else
        this->handle_dontcare_extruder(first_extruder);

    this->collect_extruder_statistics(prime_multi_material);

    this->mark_skirt_layers(print.config(), max_layer_height);
}

// BBS
std::vector<unsigned int> ToolOrdering::generate_first_layer_tool_order(const Print& print)
{
    std::vector<unsigned int> tool_order;
    int initial_extruder_id = -1;
    std::map<int, double> min_areas_per_extruder;

    for (auto object : print.objects()) {
        const Layer* target_layer = nullptr;
        for(auto layer : object->layers()){
            for(auto layerm : layer->regions()){
                for(auto& expoly : layerm->raw_slices){
                    if (!offset_ex(expoly, -0.2 * scale_(print.config().initial_layer_line_width)).empty()) {
                        target_layer = layer;
                        break;
                    }
                }
                if(target_layer)
                    break;
            }
            if(target_layer)
                break;
        }

        if(!target_layer)
            return tool_order;

        for (auto layerm : target_layer->regions()) {
            int extruder_id = layerm->region().config().option("wall_filament")->getInt();

            for (auto expoly : layerm->raw_slices) {
                if (offset_ex(expoly, -0.2 * scale_(print.config().initial_layer_line_width)).empty())
                    continue;

                double contour_area = expoly.contour.area();
                auto iter = min_areas_per_extruder.find(extruder_id);
                if (iter == min_areas_per_extruder.end()) {
                    min_areas_per_extruder.insert({ extruder_id, contour_area });
                }
                else {
                    if (contour_area < min_areas_per_extruder.at(extruder_id)) {
                        min_areas_per_extruder[extruder_id] = contour_area;
                    }
                }
            }
        }
    }

    double max_minimal_area = 0.;
    for (auto ape : min_areas_per_extruder) {
        auto iter = tool_order.begin();
        for (; iter != tool_order.end(); iter++) {
            if (min_areas_per_extruder.at(*iter) < min_areas_per_extruder.at(ape.first))
                break;
        }

        tool_order.insert(iter, ape.first);
    }

    const ConfigOptionInts* first_layer_print_sequence_op = print.full_print_config().option<ConfigOptionInts>("first_layer_print_sequence");
    if (first_layer_print_sequence_op) {
        const std::vector<int>& print_sequence_1st = first_layer_print_sequence_op->values;
        if (print_sequence_1st.size() >= tool_order.size()) {
            std::sort(tool_order.begin(), tool_order.end(), [&print_sequence_1st](int lh, int rh) {
                auto lh_it = std::find(print_sequence_1st.begin(), print_sequence_1st.end(), lh);
                auto rh_it = std::find(print_sequence_1st.begin(), print_sequence_1st.end(), rh);

                if (lh_it == print_sequence_1st.end() || rh_it == print_sequence_1st.end())
                    return false;

                return lh_it < rh_it;
            });
        }
    }

    return tool_order;
}

std::vector<unsigned int> ToolOrdering::generate_first_layer_tool_order(const PrintObject& object)
{
    std::vector<unsigned int> tool_order;
    int initial_extruder_id = -1;
    std::map<int, double> min_areas_per_extruder;
    const Layer* target_layer = nullptr;
    for(auto layer : object.layers()){
        for(auto layerm : layer->regions()){
            for(auto& expoly : layerm->raw_slices){
                if (!offset_ex(expoly, -0.2 * scale_(object.config().line_width)).empty()) {
                    target_layer = layer;
                    break;
                }
            }
            if(target_layer)
                break;
        }
        if(target_layer)
            break;
    }

    if(!target_layer)
        return tool_order;

    for (auto layerm : target_layer->regions()) {
        int extruder_id = layerm->region().config().option("wall_filament")->getInt();
        for (auto expoly : layerm->raw_slices) {
            if (offset_ex(expoly, -0.2 * scale_(object.config().line_width)).empty())
                continue;

            double contour_area = expoly.contour.area();
            auto iter = min_areas_per_extruder.find(extruder_id);
            if (iter == min_areas_per_extruder.end()) {
                min_areas_per_extruder.insert({ extruder_id, contour_area });
            }
            else {
                if (contour_area < min_areas_per_extruder.at(extruder_id)) {
                    min_areas_per_extruder[extruder_id] = contour_area;
                }
            }
        }
    }

    double max_minimal_area = 0.;
    for (auto ape : min_areas_per_extruder) {
        auto iter = tool_order.begin();
        for (; iter != tool_order.end(); iter++) {
            if (min_areas_per_extruder.at(*iter) < min_areas_per_extruder.at(ape.first))
                break;
        }

        tool_order.insert(iter, ape.first);
    }

    const ConfigOptionInts* first_layer_print_sequence_op = object.print()->full_print_config().option<ConfigOptionInts>("first_layer_print_sequence");
    if (first_layer_print_sequence_op) {
        const std::vector<int>& print_sequence_1st = first_layer_print_sequence_op->values;
        if (print_sequence_1st.size() >= tool_order.size()) {
            std::sort(tool_order.begin(), tool_order.end(), [&print_sequence_1st](int lh, int rh) {
                auto lh_it = std::find(print_sequence_1st.begin(), print_sequence_1st.end(), lh);
                auto rh_it = std::find(print_sequence_1st.begin(), print_sequence_1st.end(), rh);

                if (lh_it == print_sequence_1st.end() || rh_it == print_sequence_1st.end())
                    return false;

                return lh_it < rh_it;
            });
        }
    }

    return tool_order;
}

void ToolOrdering::initialize_layers(std::vector<coordf_t> &zs)
{
    sort_remove_duplicates(zs);
    // Merge numerically very close Z values.
    for (size_t i = 0; i < zs.size();) {
        // Find the last layer with roughly the same print_z.
        size_t j = i + 1;
        coordf_t zmax = zs[i] + EPSILON;
        for (; j < zs.size() && zs[j] <= zmax; ++ j) ;
        // Assign an average print_z to the set of layers with nearly equal print_z.
        m_layer_tools.emplace_back(LayerTools(0.5 * (zs[i] + zs[j-1])));
        i = j;
    }
}

// Collect extruders reuqired to print layers.
void ToolOrdering::collect_extruders(const PrintObject &object, const std::vector<std::pair<double, unsigned int>> &per_layer_extruder_switches)
{
    // Extruder overrides are ordered by print_z.
    std::vector<std::pair<double, unsigned int>>::const_iterator it_per_layer_extruder_override;
	it_per_layer_extruder_override = per_layer_extruder_switches.begin();
    unsigned int extruder_override = 0;

    // BBS: collect first layer extruders of an object's wall, which will be used by brim generator
    int layerCount = 0;
    std::vector<int> firstLayerExtruders;
    firstLayerExtruders.clear();

    // Collect the object extruders.
    for (auto layer : object.layers()) {
        LayerTools &layer_tools = this->tools_for_layer(layer->print_z);

        // Override extruder with the next
    	for (; it_per_layer_extruder_override != per_layer_extruder_switches.end() && it_per_layer_extruder_override->first < layer->print_z + EPSILON; ++ it_per_layer_extruder_override)
    		extruder_override = (int)it_per_layer_extruder_override->second;

        // Store the current extruder override (set to zero if no overriden), so that layer_tools.wiping_extrusions().is_overridable_and_mark() will use it.
        layer_tools.extruder_override = extruder_override;

        // What extruders are required to print this object layer?
        for (const LayerRegion *layerm : layer->regions()) {
            const PrintRegion &region = layerm->region();

            if (! layerm->perimeters.entities.empty()) {
                bool something_nonoverriddable = true;

                if (m_print_config_ptr) { // in this case print->config().print_sequence != PrintSequence::ByObject (see ToolOrdering constructors)
                    something_nonoverriddable = false;
                    for (const auto& eec : layerm->perimeters.entities) // let's check if there are nonoverriddable entities
                        if (!layer_tools.wiping_extrusions().is_overriddable_and_mark(dynamic_cast<const ExtrusionEntityCollection&>(*eec), *m_print_config_ptr, object, region))
                            something_nonoverriddable = true;
                }else{
                    something_nonoverriddable = false;
                    for (const auto &eec : layerm->perimeters.entities) // let's check if there are nonoverriddable entities
                        if (!layer_tools.wiping_extrusions().is_obj_overriddable_and_mark(dynamic_cast<const ExtrusionEntityCollection &>(*eec), object))
                            something_nonoverriddable = true;
                }

                if (something_nonoverriddable){
               		layer_tools.extruders.emplace_back((extruder_override == 0) ? region.config().wall_filament.value : extruder_override);
                    if (layerCount == 0) {
                        firstLayerExtruders.emplace_back((extruder_override == 0) ? region.config().wall_filament.value : extruder_override);
                    }
                }

                layer_tools.has_object = true;
            }

            bool has_infill       = false;
            bool has_solid_infill = false;
            bool something_nonoverriddable = false;
            for (const ExtrusionEntity *ee : layerm->fills.entities) {
                // fill represents infill extrusions of a single island.
                const auto *fill = dynamic_cast<const ExtrusionEntityCollection*>(ee);
                ExtrusionRole role = fill->entities.empty() ? erNone : fill->entities.front()->role();
                if (is_solid_infill(role))
                    has_solid_infill = true;
                else if (is_infill(role))
                    has_infill = true;

                if (m_print_config_ptr) {
                    if (! layer_tools.wiping_extrusions().is_overriddable_and_mark(*fill, *m_print_config_ptr, object, region))
                        something_nonoverriddable = true;
                }else{
                    if (!layer_tools.wiping_extrusions().is_obj_overriddable_and_mark(*fill, object))
                        something_nonoverriddable = true;
                }
            }

            if (something_nonoverriddable) {
            	if (extruder_override == 0) {
	                if (has_solid_infill)
	                    layer_tools.extruders.emplace_back(region.config().solid_infill_filament);
	                if (has_infill)
	                    layer_tools.extruders.emplace_back(region.config().sparse_infill_filament);
            	} else if (has_solid_infill || has_infill)
            		layer_tools.extruders.emplace_back(extruder_override);
            }
            if (has_solid_infill || has_infill)
                layer_tools.has_object = true;
        }
        layerCount++;
    }

    sort_remove_duplicates(firstLayerExtruders);
    const_cast<PrintObject&>(object).object_first_layer_wall_extruders = firstLayerExtruders;

    // Collect the support extruders.
    for (auto support_layer : object.support_layers()) {
        LayerTools   &layer_tools   = this->tools_for_layer(support_layer->print_z);
        ExtrusionRole role          = support_layer->support_fills.role();
        bool          has_support   = false;
        bool          has_interface = false;
        for (const ExtrusionEntity *ee : support_layer->support_fills.entities) {
            ExtrusionRole er = ee->role();
            if (er == erSupportMaterial || er == erSupportTransition) has_support = true;
            if (er == erSupportMaterialInterface) has_interface = true;
            if (has_support && has_interface) break;
        }
        unsigned int extruder_support   = object.config().support_filament.value;
        unsigned int extruder_interface = object.config().support_interface_filament.value;
        if (has_support) {
            if (extruder_support > 0 || !has_interface || extruder_interface == 0 || layer_tools.has_object)
                layer_tools.extruders.push_back(extruder_support);
            else {
                auto all_extruders     = object.print()->extruders();
                auto get_next_extruder = [&](int current_extruder, const std::vector<unsigned int> &extruders) {
                    std::vector<float> flush_matrix(
                        cast<float>(get_flush_volumes_matrix(object.print()->config().flush_volumes_matrix.values, 0, object.print()->config().nozzle_diameter.values.size())));
                    const unsigned int number_of_extruders = (unsigned int) (sqrt(flush_matrix.size()) + EPSILON);
                    // Extract purging volumes for each extruder pair:
                    std::vector<std::vector<float>> wipe_volumes;
                    for (unsigned int i = 0; i < number_of_extruders; ++i)
                        wipe_volumes.push_back(std::vector<float>(flush_matrix.begin() + i * number_of_extruders, flush_matrix.begin() + (i + 1) * number_of_extruders));
                    int   next_extruder = current_extruder;
                    float min_flush     = std::numeric_limits<float>::max();
                    for (auto extruder_id : extruders) {
                        if (object.print()->config().filament_soluble.get_at(extruder_id) || extruder_id == current_extruder) continue;
                        if (wipe_volumes[extruder_interface - 1][extruder_id] < min_flush) {
                            next_extruder = extruder_id;
                            min_flush     = wipe_volumes[extruder_interface - 1][extruder_id];
                        }
                    }
                    return next_extruder;
                };
                bool interface_not_for_body = object.config().support_interface_not_for_body;
                layer_tools.extruders.push_back(get_next_extruder(interface_not_for_body ? extruder_interface - 1 : -1, all_extruders) + 1);
            }
        }
        if (has_interface) layer_tools.extruders.push_back(extruder_interface);
        if (has_support || has_interface) {
            layer_tools.has_support = true;
            layer_tools.wiping_extrusions().is_support_overriddable_and_mark(role, object);
        }
    }

    for (auto& layer : m_layer_tools) {
        // Sort and remove duplicates
        sort_remove_duplicates(layer.extruders);

        // make sure that there are some tools for each object layer (e.g. tall wiping object will result in empty extruders vector)
        if (layer.extruders.empty() && layer.has_object)
            layer.extruders.emplace_back(0); // 0="dontcare" extruder - it will be taken care of in reorder_extruders
    }
}


void ToolOrdering::fill_wipe_tower_partitions(const PrintConfig &config, coordf_t object_bottom_z, coordf_t max_layer_height)
{
    if (m_layer_tools.empty())
        return;

    // Count the minimum number of tool changes per layer.
    size_t last_extruder = size_t(-1);
    for (LayerTools &lt : m_layer_tools) {
        lt.wipe_tower_partitions = lt.extruders.size();
        if (! lt.extruders.empty()) {
            if (last_extruder == size_t(-1) || last_extruder == lt.extruders.front())
                // The first extruder on this layer is equal to the current one, no need to do an initial tool change.
                -- lt.wipe_tower_partitions;
            last_extruder = lt.extruders.back();
        }
    }

    // Propagate the wipe tower partitions down to support the upper partitions by the lower partitions.
    for (int i = int(m_layer_tools.size()) - 2; i >= 0; -- i)
        m_layer_tools[i].wipe_tower_partitions = std::max(m_layer_tools[i + 1].wipe_tower_partitions, m_layer_tools[i].wipe_tower_partitions);


    int wrapping_layer_nums = config.wrapping_detection_layers;
    for (size_t i = 0; i < wrapping_layer_nums; ++i) {
        if (i >= m_layer_tools.size())
            break;
        LayerTools &lt    = m_layer_tools[i];
        lt.has_wipe_tower = config.enable_wrapping_detection;
    }

    //FIXME this is a hack to get the ball rolling.
    for (LayerTools &lt : m_layer_tools)
        lt.has_wipe_tower |= (lt.has_object && (config.timelapse_type == TimelapseType::tlSmooth || lt.wipe_tower_partitions > 0))
            || lt.print_z < object_bottom_z + EPSILON;

    // Test for a raft, insert additional wipe tower layer to fill in the raft separation gap.
    for (size_t i = 0; i + 1 < m_layer_tools.size(); ++ i) {
        const LayerTools &lt      = m_layer_tools[i];
        const LayerTools &lt_next = m_layer_tools[i + 1];
        if (lt.print_z < object_bottom_z + EPSILON && lt_next.print_z >= object_bottom_z + EPSILON) {
            // lt is the last raft layer. Find the 1st object layer.
            size_t j = i + 1;
            for (; j < m_layer_tools.size() && ! m_layer_tools[j].has_wipe_tower; ++ j);
            if (j < m_layer_tools.size()) {
                const LayerTools &lt_object = m_layer_tools[j];
                coordf_t gap = lt_object.print_z - lt.print_z;
                assert(gap > 0.f);
                if (gap > max_layer_height + EPSILON) {
                    // Insert one additional wipe tower layer between lh.print_z and lt_object.print_z.
                    LayerTools lt_new(0.5f * (lt.print_z + lt_object.print_z));
                    // Find the 1st layer above lt_new.
                    for (j = i + 1; j < m_layer_tools.size() && m_layer_tools[j].print_z < lt_new.print_z - EPSILON; ++ j);
                    if (std::abs(m_layer_tools[j].print_z - lt_new.print_z) < EPSILON) {
						m_layer_tools[j].has_wipe_tower = true;
					} else {
						LayerTools &lt_extra = *m_layer_tools.insert(m_layer_tools.begin() + j, lt_new);
                        //LayerTools &lt_prev  = m_layer_tools[j];
                        LayerTools &lt_next  = m_layer_tools[j + 1];
                        assert(! m_layer_tools[j - 1].extruders.empty() && ! lt_next.extruders.empty());
                        // FIXME: Following assert tripped when running combine_infill.t. I decided to comment it out for now.
                        // If it is a bug, it's likely not critical, because this code is unchanged for a long time. It might
                        // still be worth looking into it more and decide if it is a bug or an obsolete assert.
                        //assert(lt_prev.extruders.back() == lt_next.extruders.front());
                        lt_extra.has_wipe_tower = true;
                        lt_extra.extruders.push_back(lt_next.extruders.front());
                        lt_extra.wipe_tower_partitions = lt_next.wipe_tower_partitions;
                    }
                }
            }
            break;
        }
    }

    // If the model contains empty layers (such as https://github.com/prusa3d/Slic3r/issues/1266), there might be layers
    // that were not marked as has_wipe_tower, even when they should have been. This produces a crash with soluble supports
    // and maybe other problems. We will therefore go through layer_tools and detect and fix this.
    // So, if there is a non-object layer starting with different extruder than the last one ended with (or containing more than one extruder),
    // we'll mark it with has_wipe tower.
    for (unsigned int i=0; i+1<m_layer_tools.size(); ++i) {
        LayerTools& lt = m_layer_tools[i];
        LayerTools& lt_next = m_layer_tools[i+1];
        if (lt.extruders.empty() || lt_next.extruders.empty())
            break;
        if (!lt_next.has_wipe_tower && (lt_next.extruders.front() != lt.extruders.back() || lt_next.extruders.size() > 1))
            lt_next.has_wipe_tower = true;
        // We should also check that the next wipe tower layer is no further than max_layer_height:
        unsigned int j = i+1;
        double last_wipe_tower_print_z = lt_next.print_z;
        while (++j < m_layer_tools.size()-1 && !m_layer_tools[j].has_wipe_tower)
            if (m_layer_tools[j+1].print_z - last_wipe_tower_print_z > max_layer_height + EPSILON) {
                if (!config.enable_wrapping_detection)
                    m_layer_tools[j].has_wipe_tower = true;
                last_wipe_tower_print_z = m_layer_tools[j].print_z;
            }
    }

    // Calculate the wipe_tower_layer_height values.
    coordf_t wipe_tower_print_z_last = 0.;
    for (LayerTools &lt : m_layer_tools)
        if (lt.has_wipe_tower) {
            lt.wipe_tower_layer_height = lt.print_z - wipe_tower_print_z_last;
            wipe_tower_print_z_last = lt.print_z;
        }
}

void ToolOrdering::collect_extruder_statistics(bool prime_multi_material)
{
    m_first_printing_extruder = (unsigned int)-1;
    for (const auto &lt : m_layer_tools)
        if (! lt.extruders.empty()) {
            m_first_printing_extruder = lt.extruders.front();
            break;
        }

    m_last_printing_extruder = (unsigned int)-1;
    for (auto lt_it = m_layer_tools.rbegin(); lt_it != m_layer_tools.rend(); ++ lt_it)
        if (! lt_it->extruders.empty()) {
            m_last_printing_extruder = lt_it->extruders.back();
            break;
        }

    m_all_printing_extruders.clear();
    for (const auto &lt : m_layer_tools) {
        append(m_all_printing_extruders, lt.extruders);
        sort_remove_duplicates(m_all_printing_extruders);
    }

    if (prime_multi_material && ! m_all_printing_extruders.empty()) {
        // Reorder m_all_printing_extruders in the sequence they will be primed, the last one will be m_first_printing_extruder.
        // Then set m_first_printing_extruder to the 1st extruder primed.
        m_all_printing_extruders.erase(
            std::remove_if(m_all_printing_extruders.begin(), m_all_printing_extruders.end(),
                [ this ](const unsigned int eid) { return eid == m_first_printing_extruder; }),
            m_all_printing_extruders.end());
        m_all_printing_extruders.emplace_back(m_first_printing_extruder);
        m_first_printing_extruder = m_all_printing_extruders.front();
    }
}

void ToolOrdering::calc_most_used_extruder(const PrintConfig &config)
{
    // record
    std::vector<int> extruder_count;
    extruder_count.resize(config.nozzle_diameter.size(), 0);
    auto group_result = m_print->get_layered_nozzle_group_result();
    if (!group_result) {
        return;
    }
    int  layer_idx    = 0;
    for (LayerTools &layer_tools : m_layer_tools) {
        std::vector<unsigned int> filaments = layer_tools.extruders;
        std::set<int> layer_extruder_count;
        //count once only
        for (unsigned int &filament : filaments) {
            layer_extruder_count.insert(group_result->get_extruder_id(filament,layer_idx));
        }
        layer_idx++;

        //record
        for (int extruder_id : layer_extruder_count) {
            extruder_count[extruder_id]++;
        }
    }

    // set key for most used extruder
    // count most used extruder
    int most_used_extruder = 0;
    for (int extruder_id = 1; extruder_id < extruder_count.size(); extruder_id++) {
        if (extruder_count[extruder_id] >= extruder_count[most_used_extruder])
            most_used_extruder = extruder_id;
    }

    m_most_used_extruder = most_used_extruder;
}

float ToolOrdering::cal_max_additional_fan(const PrintConfig &config)
{
    // record
    float max_fan = 0;
    for (LayerTools &layer_tools : m_layer_tools) {
        std::vector<unsigned int> filaments = layer_tools.extruders;
        std::set<int>             layer_extruder_count;
        // count once only
        for (unsigned int &filament : filaments)
            if (max_fan < config.additional_cooling_fan_speed.get_at(filament))
                max_fan = config.additional_cooling_fan_speed.get_at(filament);
    }
    return max_fan;
}


//BBS: find first non support filament
bool ToolOrdering::cal_non_support_filaments(const PrintConfig &config,
                                                         unsigned int &     first_non_support_filament,
                                                         std::vector<int> & initial_non_support_filaments,
                                                         std::vector<int> & initial_filaments)
{
    int find_count = 0;
    int find_first_filaments_count = 0;
    bool has_non_support = has_non_support_filament(config);
    for (const LayerTools &layer_tool : m_layer_tools) {
        for (const unsigned int &filament : layer_tool.extruders) {
            //check first filament
            if (!config.filament_map.values.empty() && initial_filaments[config.filament_map.values[filament] - 1] == -1) {
                initial_filaments[config.filament_map.values[filament] - 1] = filament;
                find_first_filaments_count++;
            }

            if (has_non_support) {
                // check first non support filaments
                if (config.filament_is_support.get_at(filament))
                    continue;

                if (first_non_support_filament == (unsigned int) -1) first_non_support_filament = filament;

                // params missing, add protection
                // filament map missing means single nozzle, no need to set initial_non_support_filaments
                if (config.filament_map.values.empty())
                    return true;

                if (initial_non_support_filaments[config.filament_map.values[filament] - 1] == -1) {
                    initial_non_support_filaments[config.filament_map.values[filament] - 1] = filament;
                    find_count++;
                }

                if (find_count == initial_non_support_filaments.size())
                    return true;
            } else if (find_first_filaments_count == initial_filaments.size() || config.filament_map.values.empty()){
                    return false;
            }

        }
    }

    return false;
}

bool ToolOrdering::has_non_support_filament(const PrintConfig &config) {
    for (const unsigned int &filament : m_all_printing_extruders) {
        if (!config.filament_is_support.get_at(filament)) {
            return true;
        }
    }

    return false;
}


namespace GroupReorder{
std::function<bool(int, std::vector<int>&)> create_custom_seq_function(
    const PrintConfig& print_config,
    bool include_first_layer,
    const std::vector<unsigned int>& first_layer_filaments)
{
    std::vector<LayerPrintSequence> other_layers_seqs = get_other_layers_print_sequence(
        print_config.other_layers_print_sequence_nums.value,
        print_config.other_layers_print_sequence.values);

    return [other_layers_seqs, include_first_layer, first_layer_filaments](int layer_idx, std::vector<int>& out_seq) ->bool{
        if(include_first_layer && layer_idx == 0 && !first_layer_filaments.empty()){
            out_seq.resize(first_layer_filaments.size());
            std::transform(first_layer_filaments.begin(), first_layer_filaments.end(), out_seq.begin(), [](int v) { return v + 1; });
            return true;
        }

        for (size_t idx = other_layers_seqs.size() - 1; idx != size_t(-1); --idx) {
            const auto& other_layers_seq = other_layers_seqs[idx];
            if (layer_idx + 1 >= other_layers_seq.first.first && layer_idx + 1 <= other_layers_seq.first.second) {
                out_seq = other_layers_seq.second;
                return true;
            }
        }
        return false;
    };
}


std::vector<MultiNozzleUtils::NozzleGroupInfo> build_nozzle_groups(
    const PrintConfig& print_config,
    size_t extruder_nums)
{
    std::vector<MultiNozzleUtils::NozzleGroupInfo> nozzle_groups;
    auto extruder_nozzle_counts = get_extruder_nozzle_stats(print_config.extruder_nozzle_stats.values);
    auto nozzle_volume_types = print_config.nozzle_volume_type.values;
    for(size_t idx = 0; idx < extruder_nums; ++idx){
        if (idx >= extruder_nozzle_counts.size() || extruder_nozzle_counts[idx].empty()) {
            nozzle_groups.emplace_back(format_diameter_to_str(print_config.nozzle_diameter.values[idx]), NozzleVolumeType(print_config.nozzle_volume_type.values[idx]), idx,
                                       print_config.extruder_max_nozzle_count.values[idx]);
        }
        else {
            NozzleVolumeType type = NozzleVolumeType(nozzle_volume_types[idx]);
            if (type == nvtHybrid) {
                for (auto [volume_type, count] : extruder_nozzle_counts[idx]) {
                    nozzle_groups.emplace_back(format_diameter_to_str(print_config.nozzle_diameter.values[idx]), volume_type, idx, count);
                }
            }
            else
                nozzle_groups.emplace_back(format_diameter_to_str(print_config.nozzle_diameter.values[idx]), type, idx, extruder_nozzle_counts[idx][type]);
        }
    }
    return nozzle_groups;
}

std::vector<MultiNozzleUtils::NozzleInfo> build_default_nozzle_list(const PrintConfig &print_config, size_t extruder_nums)
{
    using namespace MultiNozzleUtils;
    std::vector<NozzleInfo> nozzle_list;
    for(size_t idx = 0 ; idx < extruder_nums; ++idx){
        NozzleInfo tmp;
        tmp.diameter = format_diameter_to_str(print_config.nozzle_diameter.values[idx]);
        tmp.group_id = idx;
        tmp.extruder_id = idx;
        tmp.volume_type = NozzleVolumeType(print_config.nozzle_volume_type.values[idx]);

        nozzle_list.emplace_back(std::move(tmp));
    }
    return nozzle_list;
}

FilamentGroupContext build_filament_group_context(
    Print* print,
    const std::vector<std::vector<unsigned int>>& layer_filaments,
    const std::vector<std::set<int>>& physical_unprintables,
    const std::vector<std::set<int>>& geometric_unprintables,
    const std::map<int, std::set<NozzleVolumeType>>& unprintable_volumes,
    const std::unordered_map<int, int>& nozzle_status)
{
    using namespace MultiNozzleUtils;
    using namespace FilamentGroupUtils;

    FilamentGroupContext context;

    const auto& print_config = print->config();
    const size_t filament_nums = print_config.filament_colour.values.size();
    const size_t extruder_nums = print_config.nozzle_diameter.values.size();
    bool         has_multiple_nozzle = std::any_of(print_config.extruder_max_nozzle_count.values.begin(), print_config.extruder_max_nozzle_count.values.end(),
                                                   [](int v) { return v > 1; });

    auto nozzle_flush_mtx = prepare_flush_matrices(print_config);

    // 构造可用喷嘴列表
    auto nozzle_groups = build_nozzle_groups(print_config, extruder_nums);

    // 收集不可打印限制
    std::vector<std::set<int>> ext_unprintable_filaments(2);
    collect_unprintable_limits(physical_unprintables, geometric_unprintables, ext_unprintable_filaments);

    // 生成机器信息
    bool ignore_ext_filament = false;
    std::vector<std::string> extruder_ams_count_str = print_config.extruder_ams_count.values;
    auto extruder_ams_counts = get_extruder_ams_count(extruder_ams_count_str);
    std::vector<int> group_size = calc_max_group_size(extruder_ams_counts, ignore_ext_filament);

    std::vector<bool> prefer_non_model_filament(extruder_nums);
    for (size_t idx = 0; idx < extruder_nums; ++idx)
        prefer_non_model_filament[idx] = (print_config.extruder_type.values[idx] == ExtruderType::etBowden);

    auto machine_filament_info = build_machine_filaments(print->get_extruder_filament_info(), extruder_ams_counts, ignore_ext_filament);

    std::vector<std::string> filament_types = print_config.filament_type.values;
    std::vector<std::string> filament_colours = print_config.filament_colour.values;
    std::vector<unsigned char> filament_is_support = print_config.filament_is_support.values;
    std::vector<std::string> filament_ids = print_config.filament_ids.values;
    std::vector<FilamentUsageType> filament_usage_types = print->get_filament_usage_type();

    FGMode fg_mode = print_config.filament_map_mode.value == FilamentMapMode::fmmAutoForMatch ? FGMode::MatchMode: FGMode::FlushMode;
    context.model_info.flush_matrix = std::move(nozzle_flush_mtx);
    context.model_info.unprintable_filaments = ext_unprintable_filaments;
    context.model_info.layer_filaments = layer_filaments;
    context.model_info.filament_ids = filament_ids;
    context.model_info.unprintable_volumes = unprintable_volumes;

    for (size_t idx = 0; idx < filament_types.size(); ++idx) {
        FilamentGroupUtils::FilamentInfo info;
        info.color = filament_colours[idx];
        info.type = filament_types[idx];
        info.is_support = filament_is_support[idx];
        info.usage_type = filament_usage_types[idx];
        context.model_info.filament_info.emplace_back(std::move(info));
    }

    context.speed_info.filament_print_time = print->get_filament_print_time();
    context.speed_info.group_with_time = print->config().group_algo_with_time;
    context.speed_info.filament_change_time  = print->config().machine_load_filament_time + print->config().machine_unload_filament_time;
    context.speed_info.extruder_change_time = print->config().machine_switch_extruder_time;

    context.machine_info.machine_filament_info = machine_filament_info;
    context.machine_info.max_group_size = std::move(group_size);
    context.machine_info.master_extruder_id = print_config.master_extruder_id.value - 1;
    context.machine_info.prefer_non_model_filament = prefer_non_model_filament;

    context.group_info.total_filament_num = (int)(filament_nums);
    context.group_info.max_gap_threshold = 0.01;
    context.group_info.strategy = FGStrategy::BestCost;
    context.group_info.mode = fg_mode;
    context.group_info.ignore_ext_filament = ignore_ext_filament;

    if(print_config.filament_map_mode == FilamentMapMode::fmmManual)
        context.group_info.filament_volume_map = print_config.filament_volume_map.values;
    else    // hrybid flow means no special request
        context.group_info.filament_volume_map = std::vector<int>(filament_nums,(int)(NozzleVolumeType::nvtHybrid));

    context.nozzle_info.nozzle_list = build_nozzle_list(nozzle_groups);
    context.nozzle_info.extruder_nozzle_list = build_extruder_nozzle_list(context.nozzle_info.nozzle_list);

    if(context.nozzle_info.nozzle_list.empty())
        throw Slic3r::RuntimeError(_L("No valid nozzle found. Please check nozzle count."));

    if (!nozzle_status.empty())
        context.nozzle_info.nozzle_status = nozzle_status;

    auto used_filaments = collect_sorted_used_filaments(layer_filaments);

        // add_volume_type_limits, only for o1d
    if (!has_multiple_nozzle) {
        std::vector<std::set<int>> ext_unprintable_filaments_with_volume = ext_unprintable_filaments;

        for (auto &nozzle : context.nozzle_info.nozzle_list) {
            for (auto fil_id : used_filaments) {
                auto unprintable_volumes = context.model_info.unprintable_volumes[fil_id];
                if (unprintable_volumes.count(nozzle.volume_type))
                    ext_unprintable_filaments_with_volume[nozzle.extruder_id].insert(fil_id);
            }
        }

        for (auto fil_id : used_filaments) {
            if (ext_unprintable_filaments_with_volume[0].count(fil_id) && ext_unprintable_filaments_with_volume[1].count(fil_id)) {
                ext_unprintable_filaments_with_volume[0].erase(fil_id);
                ext_unprintable_filaments_with_volume[1].erase(fil_id);
            }
        }
        context.model_info.unprintable_filaments = ext_unprintable_filaments_with_volume;
    }

    return context;
}


std::vector<FlushMatrix> prepare_flush_matrices(const PrintConfig& print_config)
{
    size_t extruder_nums = print_config.nozzle_diameter.values.size();
    size_t filament_nums = print_config.filament_colour.values.size();
    std::vector<FlushMatrix> nozzle_flush_mtx;
    for (size_t nozzle_id = 0; nozzle_id < extruder_nums; ++nozzle_id) {
        std::vector<float> flush_matrix(cast<float>(get_flush_volumes_matrix(print_config.flush_volumes_matrix.values, nozzle_id, extruder_nums)));
        std::vector<std::vector<float>> wipe_volumes;
        for (unsigned int i = 0; i < filament_nums; ++i)
            wipe_volumes.push_back(std::vector<float>(flush_matrix.begin() + i * filament_nums, flush_matrix.begin() + (i + 1) * filament_nums));

        nozzle_flush_mtx.emplace_back(wipe_volumes);
    }

    auto flush_multiplies = print_config.flush_multiplier.values;
    flush_multiplies.resize(extruder_nums, 1);
    for (size_t nozzle_id = 0; nozzle_id < extruder_nums; ++nozzle_id) {
        for (auto& vec : nozzle_flush_mtx[nozzle_id]) {
            for (auto& v : vec)
                v *= flush_multiplies[nozzle_id];
        }
    }

    return nozzle_flush_mtx;
}

}

MultiNozzleUtils::LayeredNozzleGroupResult refine_groups_by_Nozzle_State(const FilamentGroupContext& ctx,
                                                                         const MultiNozzleUtils::LayeredNozzleGroupResult& group,
                                                                         const std::unordered_map<int, int> &nozzles_state)
{
    std::vector<std::vector<int>> nozzle_fils(ctx.nozzle_info.nozzle_list.size());
    auto fils        = group.get_used_filaments(0);
    auto fil_noz_map = group.get_layer_filament_nozzle_map(0);

    for (auto fil : fils)
        nozzle_fils[fil_noz_map[fil]].emplace_back(fil);

    // 1、收集每个材料不可使用的喷嘴
    std::map<int, std::set<int>> fil_unplaceable_nozs;
    for (auto fil : fils) {
        std::set<NozzleVolumeType> unprintable_volumes;
        if (ctx.model_info.unprintable_volumes.count(fil))
            unprintable_volumes = ctx.model_info.unprintable_volumes.at(fil);
        auto expected_volume     = ctx.group_info.filament_volume_map[fil];

        for (int noz = 0; noz < ctx.nozzle_info.nozzle_list.size(); noz++) {
            auto noz_info             = ctx.nozzle_info.nozzle_list[noz];
            int  ext_id               = noz_info.extruder_id;
            auto ext_unprintable_fils = ctx.model_info.unprintable_filaments[ext_id];
            if (std::find(ext_unprintable_fils.begin(), ext_unprintable_fils.end(), ext_id) != ext_unprintable_fils.end() ||
                (expected_volume != nvtHybrid && expected_volume != noz_info.volume_type) || (unprintable_volumes.count(noz_info.volume_type) != 0))
                fil_unplaceable_nozs[fil].insert(noz);
        }
    }

    // 2、存储全局喷嘴匹配结果
    std::unordered_map<int, int> global_uv_match;

    // 3、按挤出机分组处理，每个挤出机单独跑费用流
    for (const auto& [ext_id, ext_nozzles] : ctx.nozzle_info.extruder_nozzle_list) {
        if (ext_nozzles.empty()) continue;

        // 3.1、构建该挤出机的 u_nodes 和 v_nodes
        std::vector<int> u_nodes = ext_nozzles;
        std::vector<int> v_nodes = ext_nozzles;

        // 3.2、构建局部索引映射 (全局喷嘴id -> 局部索引)
        std::unordered_map<int, int> global_to_local;
        for (size_t i = 0; i < ext_nozzles.size(); ++i)
            global_to_local[ext_nozzles[i]] = static_cast<int>(i);

        // 3.3、构建该挤出机的费用矩阵
        std::vector<std::vector<float>>           cost_matrix(u_nodes.size(), std::vector<float>(v_nodes.size(), std::numeric_limits<float>::max()));
        std::unordered_map<int, std::vector<int>> uv_unlink_limits;

        for (size_t local_u = 0; local_u < u_nodes.size(); ++local_u) {
            int           u_node = u_nodes[local_u];
            std::set<int> unlink_v_local;
            auto          u_fils = nozzle_fils[u_node];

            // 收集不可连接的 v_node (转换为局部索引)
            for (auto fil : u_fils) {
                for (auto unplaceable_noz : fil_unplaceable_nozs[fil]) {
                    if (global_to_local.count(unplaceable_noz))
                        unlink_v_local.insert(global_to_local[unplaceable_noz]);
                }
            }
            uv_unlink_limits[static_cast<int>(local_u)].assign(unlink_v_local.begin(), unlink_v_local.end());

            // 3.4、计算费用
            for (size_t local_v = 0; local_v < v_nodes.size(); ++local_v) {
                int   v_node = v_nodes[local_v];
                float cost   = 0;
                if (unlink_v_local.count(static_cast<int>(local_v))) continue;

                std::optional<unsigned int> v_fil_opt = std::nullopt;
                if (nozzles_state.count(v_node))
                    v_fil_opt = nozzles_state.at(v_node);

                if (!v_fil_opt.has_value() || v_fil_opt.value() >= ctx.model_info.filament_info.size()) {
                    cost = 0;
                }
                else {
                    int v_fil = v_fil_opt.value();
                    if (std::find(u_fils.begin(), u_fils.end(), v_fil) != u_fils.end())
                        cost = -1;
                    else {
                        for (auto u_fil : u_fils)
                            cost += ctx.model_info.flush_matrix[ext_id][u_fil][v_fil];
                        if (u_fils.size() > 0)
                            cost /= u_fils.size();
                    }
                }

                cost_matrix[local_u][local_v] = cost;
            }
        }

        // 3.5、使用费用流求解该挤出机的喷嘴匹配
        std::vector<int> local_u_nodes(u_nodes.size());
        std::vector<int> local_v_nodes(v_nodes.size());
        std::iota(local_u_nodes.begin(), local_u_nodes.end(), 0);
        std::iota(local_v_nodes.begin(), local_v_nodes.end(), 0);

        MinFlushFlowSolver solver(cost_matrix, local_u_nodes, local_v_nodes, {}, uv_unlink_limits);
        auto               local_match = solver.solve();

        // 3.6、将局部匹配结果转换为全局匹配
        for (size_t local_u = 0; local_u < u_nodes.size(); ++local_u) {
            int global_u = u_nodes[local_u];
            int local_v  = local_match[static_cast<int>(local_u)];
            int global_v = v_nodes[local_v];
            global_uv_match[global_u] = global_v;
        }
    }

    // 4、构建新的group_result
    std::vector<int> new_default_filament_nozzle_maps = group.get_layer_filament_nozzle_map(-1);

    for (auto fil : fils) {
        int ori_noz = new_default_filament_nozzle_maps[fil];
        if (global_uv_match.count(ori_noz))
            new_default_filament_nozzle_maps[fil] = global_uv_match[ori_noz];
    }

    auto new_group = MultiNozzleUtils::LayeredNozzleGroupResult::create(new_default_filament_nozzle_maps, ctx.nozzle_info.nozzle_list, fils);
    if (!new_group.has_value()) new_group = group;

    return *new_group;
}

// 用于 std::vector<unsigned int> 作为 unordered_map 的 key
struct VectorHash {
    size_t operator()(const std::vector<unsigned int>& v) const {
        size_t seed = v.size();
        for (auto& elem : v) {
            seed ^= std::hash<unsigned int>()(elem) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }
        return seed;
    }
};

std::vector<FilamentPlanRes> plan_filament_nozzle_mapping_and_order2(
    Print*                                             print,
    const FilamentGroupContext&                        ctx,
    const ToolOrdering::OrderingContext&               order_ctx,
    const FilamentMapMode                              mode,
    const std::vector<std::set<int>>&                  physical_unprintables,
    const std::vector<std::set<int>>&                  geometric_unprintables,
    const std::map<int, std::set<NozzleVolumeType>>&   unprintable_volumes)
{
    std::vector<FilamentPlanRes> results;

    const auto& layer_fils = ctx.model_info.layer_filaments;
    if (layer_fils.empty()) {
        return results;
    }

    results.resize(layer_fils.size());

    // key: 某一层使用的所有材料id组合 (排序后的vector)
    // value: 具有相同材料组合的连续层区间列表 [(start, end), ...]
    std::unordered_map<std::vector<unsigned int>, std::vector<std::pair<int, int>>, VectorHash> filament_combo_ranges;
    for (int layer_idx = 0; layer_idx < static_cast<int>(layer_fils.size()); ++layer_idx) {
        std::vector<unsigned int> cur_combo = layer_fils[layer_idx];
        std::sort(cur_combo.begin(), cur_combo.end());
        cur_combo.erase(std::unique(cur_combo.begin(), cur_combo.end()), cur_combo.end());
        if (cur_combo.empty())
            continue;

        auto& ranges = filament_combo_ranges[cur_combo];
        if (ranges.empty() || ranges.back().second != layer_idx - 1) {
            ranges.emplace_back(layer_idx, layer_idx);
        } else {
            ranges.back().second = layer_idx;
        }
    }

    std::map<std::pair<int,int>, std::vector<unsigned int>> range_filas_map;
    for (auto& [combo, ranges] : filament_combo_ranges) {
        for (auto & range : ranges)
            range_filas_map[range] = combo;
    }

    // 对每个材料组合的每个连续区间，构建新的 layer_filaments 并调用 get_recommended_filament_maps
    MultiNozzleUtils::NozzleStatusRecorder tool_status;
    std::vector<int> fil_noz_map(ctx.group_info.total_filament_num, 0);
    for (auto &[range, combo] : range_filas_map) {
        auto [start_layer, end_layer] = range;
        // 1、构建区间的 layer_filaments
        std::vector<std::vector<unsigned int>> range_layer_fils;
        range_layer_fils.reserve(end_layer - start_layer + 1);
        for (int layer_idx = start_layer; layer_idx <= end_layer; ++layer_idx) { range_layer_fils.push_back(layer_fils[layer_idx]); }

        // 2、生成区间分组结果
        auto nozzle_filament_map = tool_status.get_nozzle_filament_map();
        auto group_result = ToolOrdering::get_recommended_filament_maps(print, range_layer_fils, mode, physical_unprintables, geometric_unprintables, unprintable_volumes, nozzle_filament_map);

        // 3、动态调整分组id
        auto new_group_result = refine_groups_by_Nozzle_State(ctx, group_result, nozzle_filament_map);
        //auto new_group_result = group_result;
        auto range_seq_function = [&order_ctx, &start_layer, &end_layer](int layer_idx, std::vector<int> &out_seq) -> bool {
            if (layer_idx <= end_layer - start_layer) {
                int global_idx = start_layer + layer_idx;
                return order_ctx.get_custom_seq(global_idx, out_seq);
            }
            return false;
        };

        // 4、区间分组排序
        std::vector<std::vector<unsigned int>> fils_sequences;
        reorder_filaments_for_multi_nozzle_extruder(range_layer_fils.front(), new_group_result, range_layer_fils, ctx.model_info.flush_matrix, range_seq_function,
                                                    &fils_sequences, tool_status);

        // 5、存储区间结果
        for (auto fil_id : fils_sequences.back()) {
            auto noz = new_group_result.get_nozzle_for_filament(fil_id);
            if (noz.has_value()) {
                int noz_id = noz->group_id;
                int ext_id = noz->extruder_id;

                fil_noz_map[fil_id] = noz_id;

                tool_status.set_current_extruder_id(ext_id);
                tool_status.set_nozzle_status(noz_id, fil_id, ext_id);
            }
        }

        assert(fils_sequences.size() == range_layer_fils.size());
        for (size_t layer_id = 0; layer_id < fils_sequences.size(); ++layer_id) {
            int g_layer_id                       = start_layer + static_cast<int>(layer_id);
            results[g_layer_id].fil_nozzle_match = fil_noz_map;
            results[g_layer_id].fil_order        = std::vector<int>(fils_sequences[layer_id].begin(), fils_sequences[layer_id].end());
        }
    }

    return results;
}



MultiNozzleUtils::LayeredNozzleGroupResult ToolOrdering::get_recommended_filament_maps(Print                                           *print,
                                                                                     const std::vector<std::vector<unsigned int>> &layer_filaments,
                                                                                     const FilamentMapMode                         mode,
                                                                                     const std::vector<std::set<int>>             &physical_unprintables,
                                                                                     const std::vector<std::set<int>>             &geometric_unprintables,
                                                                                     const std::map<int, std::set<NozzleVolumeType>>   &unprintable_volumes,
                                                                                     const std::unordered_map<int, int>& nozzle_status)
{
    using namespace FilamentGroupUtils;
    using namespace MultiNozzleUtils;
    using namespace GroupReorder;

    if (!print || layer_filaments.empty())
        return LayeredNozzleGroupResult();

    const auto& print_config = print->config();
    size_t filament_nums = print_config.filament_colour.values.size();
    size_t extruder_nums = print_config.nozzle_diameter.values.size();
    auto used_filaments = collect_sorted_used_filaments(layer_filaments);
    bool has_multiple_nozzle = std::any_of(
        print_config.extruder_max_nozzle_count.values.begin(),
        print_config.extruder_max_nozzle_count.values.end(),
        [](int v) { return v>1;});
    bool has_multiple_extruder = extruder_nums > 1;

    auto nozzle_list = build_default_nozzle_list(print_config, extruder_nums);

    // 处理非H2C的手动模式，此时可以直接退出
    if (mode == FilamentMapMode::fmmManual && !has_multiple_nozzle){
        auto manual_filament_map = print_config.filament_map.values;
        std::transform(manual_filament_map.begin(), manual_filament_map.end(), manual_filament_map.begin(), [](int v) { return v - 1; });
        auto result = LayeredNozzleGroupResult::create(manual_filament_map, nozzle_list, used_filaments);
        return result ? *result : LayeredNozzleGroupResult();
    }

    // 处理H2C的全手动模式，从传入参数直接构造结果
    if (mode == FilamentMapMode::fmmNozzleManual) {
        // directly build group result based on filament nozzle map
        auto manual_filament_map = print_config.filament_map.values;
        std::transform(manual_filament_map.begin(), manual_filament_map.end(), manual_filament_map.begin(), [](int v) { return v - 1; });
        auto nozzle_result = LayeredNozzleGroupResult::create(used_filaments, manual_filament_map, print_config.filament_volume_map.values, print_config.filament_nozzle_map.values, get_extruder_nozzle_stats(print_config.extruder_nozzle_stats.values), print_config.nozzle_diameter.values.front());
        if (!nozzle_result) {
            BOOST_LOG_TRIVIAL(error) << "Failed to build nozzle group result from filament nozzle map!";
            return LayeredNozzleGroupResult();
        }
        return *nozzle_result;
    }

    int master_extruder_id = print_config.master_extruder_id.value - 1;
    std::vector<int> ret(filament_nums, master_extruder_id);

    if(has_multiple_extruder || has_multiple_nozzle){
        auto context = build_filament_group_context(
            print,layer_filaments,physical_unprintables,geometric_unprintables, unprintable_volumes, nozzle_status);

        if (has_multiple_nozzle) {
            if(mode == FilamentMapMode::fmmManual){
                auto manual_filament_map = print_config.filament_map.values;
                std::transform(manual_filament_map.begin(), manual_filament_map.end(), manual_filament_map.begin(), [](int v) { return v - 1; });
                ret = calc_filament_group_for_manual_multi_nozzle(manual_filament_map,context);
            }
            else if(mode == FilamentMapMode::fmmAutoForMatch){
                ret = calc_filament_group_for_match_multi_nozzle(context);
            }
            else{
                FilamentGroupMultiNozzle fg(context);
                ret = fg.calc_filament_group_by_pam();
            }

            auto result_opt = LayeredNozzleGroupResult::create(ret, context.nozzle_info.nozzle_list, used_filaments);
            if (!result_opt) return LayeredNozzleGroupResult();
            auto result = *result_opt;
            if (mode == FilamentMapMode::fmmManual) {
                auto result_map = result.get_extruder_map();
                for (auto fid : used_filaments) {
                    if (result_map[fid] != print_config.filament_map.values[fid] - 1) {
                        throw Slic3r::RuntimeError(_L("Group error in manual mode. Please check nozzle count or regroup."));
                    }
                }
            }
            return result;
        }
        else{
            FilamentGroup fg(context);
            fg.get_custom_seq = create_custom_seq_function(print_config,false);
            ret = fg.calc_filament_group();
        }
    }

    auto result_opt = LayeredNozzleGroupResult::create(ret, nozzle_list, used_filaments);
    return result_opt ? *result_opt : LayeredNozzleGroupResult();
}

FilamentChangeStats ToolOrdering::get_filament_change_stats(FilamentChangeMode mode) const
{
    switch (mode)
    {
    case Slic3r::ToolOrdering::SingleExt:
        return m_stats_by_single_extruder;
    case Slic3r::ToolOrdering::MultiExtBest:
        return m_stats_by_multi_extruder_best;
    case Slic3r::ToolOrdering::MultiExtCurr:
        return m_stats_by_multi_extruder_curr;
    default:
        break;
    }
    return m_stats_by_single_extruder;
}


void ToolOrdering::calculate_and_store_statistics(const PrintConfig                              &print_config,
                                                  const MultiNozzleUtils::LayeredNozzleGroupResult &grouping_result,
                                                  const LayerData                                &layer_data,
                                                  const OrderingContext                          &ordering_context,
                                                  const std::vector<FlushMatrix>                 &nozzle_flush_mtx,
                                                  const std::vector<std::vector<unsigned int>>   &filament_sequences)
{
    size_t extruder_num = print_config.nozzle_diameter.values.size();

    auto curr_flush_info = calc_filament_change_info_by_toolorder(print_config, grouping_result, nozzle_flush_mtx, filament_sequences);

    std::map<int,int> aa;
    for (auto b : filament_sequences) {
        int c = b.size();
        aa[c]++;
    }

    // 将统计信息输出到可执行文件所在目录的 filament_stats.txt 文件
    {
        try {
            // 获取可执行文件所在目录
            boost::filesystem::path exe_path = boost::dll::program_location();
            boost::filesystem::path stats_file = exe_path.parent_path() / "filament_stats_ori.txt";

            // 获取当前时间戳
            auto now = std::chrono::system_clock::now();
            auto time_t_now = std::chrono::system_clock::to_time_t(now);
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

            std::ostringstream timestamp;
            timestamp << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S");
            timestamp << "." << std::setfill('0') << std::setw(3) << ms.count();

            // 以追加模式打开文件
            std::ofstream ofs(stats_file.string(), std::ios::app);
            if (ofs.is_open()) {
                ofs << "[" << timestamp.str() << "] "
                    << "filament_flush_weight=" << curr_flush_info.filament_flush_weight << " "
                    << "filament_change_count=" << curr_flush_info.filament_change_count
                    << std::endl;
                ofs.close();
                BOOST_LOG_TRIVIAL(info) << "Filament stats written to: " << stats_file.string();
            } else {
                BOOST_LOG_TRIVIAL(error) << "Failed to open stats file: " << stats_file.string();
            }
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "Error writing filament stats: " << e.what();
        }
        //throw std::runtime_error("Filament stats collected, stopping slice.");
    }

    if (extruder_num <= 1) {
        m_stats_by_single_extruder = curr_flush_info;
    } else {
        m_stats_by_multi_extruder_curr = curr_flush_info;
        if (print_config.filament_map_mode == fmmAutoForFlush) m_stats_by_multi_extruder_best = curr_flush_info;
    }

    // 多喷嘴时，计算单挤出机与最优分组下（如果需要）的信息
    if (extruder_num > 1) {
        {
            std::vector<std::vector<unsigned int>> single_extruder_sequences;
            std::vector<int>                       maps_without_group(print_config.filament_colour.size(), 0);

            MultiNozzleUtils::NozzleInfo tmp;
            tmp.diameter    = print_config.nozzle_diameter.get_at(0);
            tmp.group_id    = 0;
            tmp.extruder_id = 0;
            tmp.volume_type = NozzleVolumeType::nvtStandard;

            auto result_opt = MultiNozzleUtils::LayeredNozzleGroupResult::create(maps_without_group, {tmp}, layer_data.used_filaments);
            if (!result_opt) return;
            auto result = *result_opt;

            reorder_filaments_for_multi_nozzle_extruder(ordering_context.filament_lists, result, layer_data.layer_filaments, nozzle_flush_mtx, ordering_context.get_custom_seq,
                                                        &single_extruder_sequences);
            m_stats_by_single_extruder = calc_filament_change_info_by_toolorder(print_config, result, nozzle_flush_mtx, single_extruder_sequences);
        }

        if (print_config.filament_map_mode != fmmAutoForFlush) {
            std::vector<std::vector<unsigned int>> best_seq;

            // 如果支持选料器
            if (m_print->is_dynamic_group_reorder()) {
                auto grouping_context = GroupReorder::build_filament_group_context(m_print, layer_data.layer_filaments, layer_data.physical_unprintables,
                                                                                   layer_data.geometric_unprintables, layer_data.filament_unprintable_volumes);

                auto dynamic_plan_res = plan_filament_nozzle_mapping_and_order2(m_print, grouping_context, ordering_context, FilamentMapMode::fmmAutoForFlush,
                                                                                layer_data.physical_unprintables, layer_data.geometric_unprintables, layer_data.filament_unprintable_volumes);

                std::vector<std::vector<int>> nozzle_map_per_layer;
                for (auto &res : dynamic_plan_res) {
                    best_seq.emplace_back(cast<unsigned int>(res.fil_order));
                    nozzle_map_per_layer.emplace_back(res.fil_nozzle_match);
                }

                auto result = MultiNozzleUtils::LayeredNozzleGroupResult::create(nozzle_map_per_layer, grouping_context.nozzle_info.nozzle_list, layer_data.used_filaments, filament_sequences);

                if (result)
                    m_stats_by_multi_extruder_best = calc_filament_change_info_by_toolorder(print_config, *result, nozzle_flush_mtx, best_seq);
            }
            else {
                auto group_result_auto = get_recommended_filament_maps(m_print, layer_data.layer_filaments, fmmAutoForFlush, layer_data.physical_unprintables,
                                                                       layer_data.geometric_unprintables, layer_data.filament_unprintable_volumes);

                reorder_filaments_for_multi_nozzle_extruder(ordering_context.filament_lists, group_result_auto, layer_data.layer_filaments, nozzle_flush_mtx,
                                                            ordering_context.get_custom_seq, &best_seq);

                m_stats_by_multi_extruder_best = calc_filament_change_info_by_toolorder(print_config, group_result_auto, nozzle_flush_mtx, best_seq);
            }

        }
    }
}

ToolOrdering::LayerData ToolOrdering::collect_layer_and_unprintable_data()
{
    LayerData data;

    // 收集层信息
    for(auto& lt : m_layer_tools){
        data.layer_filaments.emplace_back(lt.extruders);
    }

    data.used_filaments = collect_sorted_used_filaments(data.layer_filaments);

    data.geometric_unprintables = m_print->get_geometric_unprintable_filaments();
    data.physical_unprintables = m_print->get_physical_unprintable_filaments(data.used_filaments);
    data.filament_unprintable_volumes = m_print->get_filament_unprintable_flow(data.used_filaments);

    return data;
}



ToolOrdering::OrderingContext ToolOrdering::prepare_ordering_context(const PrintConfig& print_config, bool reorder_first_layer){
    using namespace GroupReorder;
    OrderingContext context;
    size_t filament_nums = print_config.filament_colour.size();

    context.filament_lists.resize(filament_nums);
    std::iota(context.filament_lists.begin(), context.filament_lists.end(), 0);

    std::vector<unsigned int> first_layer_filaments;
    if(!m_layer_tools.empty())
        first_layer_filaments = m_layer_tools[0].extruders;

    context.get_custom_seq = create_custom_seq_function(print_config, !reorder_first_layer, first_layer_filaments);

    context.support_multi_nozzle = std::any_of(
        print_config.extruder_max_nozzle_count.values.begin(),
        print_config.extruder_max_nozzle_count.values.end(),
        [](auto v){ return v>1;});

    context.support_dynamic_map = print_config.enable_filament_dynamic_map.value;

    return context;
}


std::vector<std::vector<unsigned int>> ToolOrdering::execute_filament_ordering(
    const PrintConfig* print_config,
    const MultiNozzleUtils::LayeredNozzleGroupResult& grouping_result,
    const LayerData& layer_data,
    const OrderingContext& ordering_context,
    const std::vector<FlushMatrix>& nozzle_flush_mtx)
{
    std::vector<std::vector<unsigned int>> filament_sequences;
    if(ordering_context.support_multi_nozzle){
        assert(m_print->get_layered_nozzle_group_result());
        reorder_filaments_for_multi_nozzle_extruder(
            ordering_context.filament_lists,
            grouping_result,
            layer_data.layer_filaments,
            nozzle_flush_mtx,
            ordering_context.get_custom_seq,
            &filament_sequences
        );
    }
    else{
        reorder_filaments_for_minimum_flush_volume(
            ordering_context.filament_lists,
            grouping_result.get_extruder_map(true),
            layer_data.layer_filaments,
            nozzle_flush_mtx,
            ordering_context.get_custom_seq,
            &filament_sequences
        );
    }
    return filament_sequences;
}



void ToolOrdering::reorder_extruders_for_minimum_flush_volume(bool reorder_first_layer)
{
    using namespace GroupReorder;
    const PrintConfig* print_config = m_print_config_ptr;
    if (!print_config && m_print_object_ptr) {
        print_config = &(m_print_object_ptr->print()->config());
    }

    if (!print_config || m_layer_tools.empty())
        return;

    std::vector<std::vector<unsigned int>> filament_sequences;

    MultiNozzleUtils::LayeredNozzleGroupResult grouping_result;

    // Stage 1: 准备冲刷矩阵
    auto nozzle_flush_mtx = prepare_flush_matrices(*print_config);
    // Stage 2: 准备层信息与不可打印限制
    auto layer_data = collect_layer_and_unprintable_data();
    // Stage 3: 生成排序所需的context
    auto ordering_context = prepare_ordering_context(*print_config, reorder_first_layer);

    // 不支持选料器或手动模式
    if(!m_print->is_dynamic_group_reorder()){
        // 逐层打印时，在生成toolodering时分组，统计相关信息并写回
        if(!m_print->is_sequential_print()){
            // Stage 4: 计算分组
            grouping_result = ToolOrdering::get_recommended_filament_maps(
                m_print,
                layer_data.layer_filaments,
                print_config->filament_map_mode.value,
                layer_data.physical_unprintables,
                layer_data.geometric_unprintables,
                layer_data.filament_unprintable_volumes
            );
        }
        else{
            // 逐件打印时，分组已经被调用过了，直接从print获取结果
            grouping_result = *(m_print->get_layered_nozzle_group_result());
        }
        // 计算排序
        filament_sequences = execute_filament_ordering(
            print_config,
            grouping_result,
            layer_data,
            ordering_context,
            nozzle_flush_mtx
        );
    }

    // 支持选料器且为自动模式
    if (m_print->is_dynamic_group_reorder()) {
        auto grouping_context =build_filament_group_context(
            m_print,
            layer_data.layer_filaments,
            layer_data.physical_unprintables,
            layer_data.geometric_unprintables,
            layer_data.filament_unprintable_volumes
        );

        // TODO(山苍)：逐件打印后面要考虑喷嘴状态
        auto dynamic_plan_res = plan_filament_nozzle_mapping_and_order2(m_print,
                                                                       grouping_context,
                                                                       ordering_context,
                                                                       FilamentMapMode::fmmAutoForFlush,
                                                                       layer_data.physical_unprintables,
                                                                       layer_data.geometric_unprintables,
                                                                       layer_data.filament_unprintable_volumes);
         //auto dynamic_plan_res = plan_filament_nozzle_mapping_and_order(grouping_context);


        std::vector<std::vector<int>> nozzle_map_per_layer;
        for (auto &res : dynamic_plan_res){
            filament_sequences.emplace_back(cast<unsigned int>(res.fil_order));
            nozzle_map_per_layer.emplace_back(res.fil_nozzle_match);
        }

        auto result = MultiNozzleUtils::LayeredNozzleGroupResult::create(nozzle_map_per_layer, grouping_context.nozzle_info.nozzle_list, layer_data.used_filaments, filament_sequences);
        grouping_result = result ? *result : MultiNozzleUtils::LayeredNozzleGroupResult();
    }

    if (!m_print->is_sequential_print()) {
        m_print->set_nozzle_group_result(std::make_shared<MultiNozzleUtils::LayeredNozzleGroupResult>(grouping_result));
        if (!m_print->is_dynamic_group_reorder()) {
            m_print->update_filament_maps_to_config(FilamentGroupUtils::update_used_filament_values(print_config->filament_map.values, grouping_result.get_extruder_map(false),
                                                                                                    layer_data.used_filaments),
                                                    FilamentGroupUtils::update_used_filament_values(print_config->filament_volume_map.values, grouping_result.get_volume_map(),
                                                                                                    layer_data.used_filaments),
                                                    grouping_result.get_nozzle_map());
        } else {
            m_print->update_to_config_by_nozzle_group_result(grouping_result);
        }
    }

    m_nozzle_group_result = grouping_result;

    // Stage 6: 统计其它模式的信息
    calculate_and_store_statistics(
        *print_config,
        grouping_result,
        layer_data,
        ordering_context,
        nozzle_flush_mtx,
        filament_sequences
    );

    // Stage 7: 应用排序结果
    for (size_t i = 0; i < filament_sequences.size(); ++i)
        m_layer_tools[i].extruders = std::move(filament_sequences[i]);
}
// Layers are marked for infinite skirt aka draft shield. Not all the layers have to be printed.
void ToolOrdering::mark_skirt_layers(const PrintConfig &config, coordf_t max_layer_height)
{
    if (m_layer_tools.empty())
        return;

    if (m_layer_tools.front().extruders.empty()) {
        // Empty first layer, no skirt will be printed.
        //FIXME throw an exception?
        return;
    }

    size_t i = 0;
    for (;;) {
        m_layer_tools[i].has_skirt = true;
        size_t j = i + 1;
        for (; j < m_layer_tools.size() && ! m_layer_tools[j].has_object; ++ j);
        // i and j are two successive layers printing an object.
        if (j == m_layer_tools.size())
            // Don't print skirt above the last object layer.
            break;
        // Mark some printing intermediate layers as having skirt.
        double last_z = m_layer_tools[i].print_z;
        for (size_t k = i + 1; k < j; ++ k) {
            if (m_layer_tools[k + 1].print_z - last_z > max_layer_height + EPSILON) {
                // Layer k is the last one not violating the maximum layer height.
                // Don't extrude skirt on empty layers.
                while (m_layer_tools[k].extruders.empty())
                    -- k;
                if (m_layer_tools[k].has_skirt) {
                    // Skirt cannot be generated due to empty layers, there would be a missing layer in the skirt.
                    //FIXME throw an exception?
                    break;
                }
                m_layer_tools[k].has_skirt = true;
                last_z = m_layer_tools[k].print_z;
            }
        }
        i = j;
    }
}

// Assign a pointer to a custom G-code to the respective ToolOrdering::LayerTools.
// Ignore color changes, which are performed on a layer and for such an extruder, that the extruder will not be printing above that layer.
// If multiple events are planned over a span of a single layer, use the last one.

// BBS: replace model custom gcode with current plate custom gcode
static CustomGCode::Info custom_gcode_per_print_z;
void ToolOrdering::assign_custom_gcodes(const Print& print)
{
    // Only valid for non-sequential print.
    assert(print.config().print_sequence == PrintSequence::ByLayer);

    custom_gcode_per_print_z = print.model().get_curr_plate_custom_gcodes();
    if (custom_gcode_per_print_z.gcodes.empty())
        return;

    // BBS
    auto num_filaments = unsigned(print.config().filament_diameter.size());
    CustomGCode::Mode mode =
        (num_filaments == 1) ? CustomGCode::SingleExtruder :
        print.object_extruders().size() == 1 ? CustomGCode::MultiAsSingle : CustomGCode::MultiExtruder;
    CustomGCode::Mode           model_mode = print.model().get_curr_plate_custom_gcodes().mode;
    auto custom_gcode_it = custom_gcode_per_print_z.gcodes.rbegin();
    // Tool changes and color changes will be ignored, if the model's tool/color changes were entered in mm mode and the print is in non mm mode
    // or vice versa.
    bool ignore_tool_and_color_changes = (mode == CustomGCode::MultiExtruder) != (model_mode == CustomGCode::MultiExtruder);
    // If printing on a single extruder machine, make the tool changes trigger color change (M600) events.
    bool tool_changes_as_color_changes = mode == CustomGCode::SingleExtruder && model_mode == CustomGCode::MultiAsSingle;

    auto apply_custom_gcode_to_layer = [mode,
        ignore_tool_and_color_changes,
        tool_changes_as_color_changes,
        num_filaments](LayerTools& lt, const std::vector<unsigned char>& extruder_printing_above, const CustomGCode::Item& item)
        {
            bool color_change = item.type == CustomGCode::ColorChange;
            bool tool_change = item.type == CustomGCode::ToolChange;
            bool pause_or_custom_gcode = !color_change && !tool_change;
            bool apply_color_change = !ignore_tool_and_color_changes &&
                // If it is color change, it will actually be useful as the exturder above will print.
                // BBS
                (color_change ?
                    mode == CustomGCode::SingleExtruder ||
                    (item.extruder <= int(num_filaments) && extruder_printing_above[unsigned(item.extruder - 1)]) :
                    tool_change && tool_changes_as_color_changes);
            if (pause_or_custom_gcode || apply_color_change)
                lt.custom_gcode = &item;
        };

    std::unordered_map<int, std::vector<unsigned char>> extruder_print_above_by_layer;
    {
        std::vector<unsigned char> extruder_printing_above(num_filaments, false);
        for (auto iter = m_layer_tools.rbegin(); iter != m_layer_tools.rend(); ++iter) {
            for (unsigned int i : iter->extruders)
                extruder_printing_above[i] = true;
            int layer_idx = m_layer_tools.rend() - iter - 1;
            extruder_print_above_by_layer.emplace(layer_idx, extruder_printing_above);
        }
    }

    for (auto custom_gcode_it = custom_gcode_per_print_z.gcodes.rbegin(); custom_gcode_it != custom_gcode_per_print_z.gcodes.rend(); ++custom_gcode_it) {
        if (custom_gcode_it->type == CustomGCode::ToolChange)
            continue;

        auto layer_it_upper = std::upper_bound(m_layer_tools.begin(), m_layer_tools.end(), custom_gcode_it->print_z, [](double z,const LayerTools& lt) {
            return z < lt.print_z;
            });

        int upper_layer_idx = layer_it_upper - m_layer_tools.begin();
        if (layer_it_upper == m_layer_tools.begin()) {
            apply_custom_gcode_to_layer(*layer_it_upper, extruder_print_above_by_layer[0], *custom_gcode_it);
        }
        else if (layer_it_upper == m_layer_tools.end()) {
            auto layer_it_lower = std::prev(layer_it_upper);
            int lower_layer_idx = layer_it_lower - m_layer_tools.begin();
            apply_custom_gcode_to_layer(*layer_it_lower, extruder_print_above_by_layer[lower_layer_idx], *custom_gcode_it);
        }
        else {
            auto layer_it_lower = std::prev(layer_it_upper);
            int lower_layer_idx = layer_it_lower - m_layer_tools.begin();
            double gap_to_lower = std::fabs(custom_gcode_it->print_z - layer_it_lower->print_z);
            double gap_to_upper = std::fabs(custom_gcode_it->print_z - layer_it_upper->print_z);
            if (gap_to_lower < gap_to_upper)
                apply_custom_gcode_to_layer(*layer_it_lower, extruder_print_above_by_layer[lower_layer_idx], *custom_gcode_it);
            else
                apply_custom_gcode_to_layer(*layer_it_upper, extruder_print_above_by_layer[upper_layer_idx], *custom_gcode_it);
        }
    }
}

const LayerTools& ToolOrdering::tools_for_layer(coordf_t print_z) const
{
    auto it_layer_tools = std::lower_bound(m_layer_tools.begin(), m_layer_tools.end(), LayerTools(print_z - EPSILON));
    assert(it_layer_tools != m_layer_tools.end());
    coordf_t dist_min = std::abs(it_layer_tools->print_z - print_z);
    for (++ it_layer_tools; it_layer_tools != m_layer_tools.end(); ++ it_layer_tools) {
        coordf_t d = std::abs(it_layer_tools->print_z - print_z);
        if (d >= dist_min)
            break;
        dist_min = d;
    }
    -- it_layer_tools;
    assert(dist_min < EPSILON);
    return *it_layer_tools;
}

// This function is called from Print::mark_wiping_extrusions and sets extruder this entity should be printed with (-1 .. as usual)
void WipingExtrusions::set_extruder_override(const ExtrusionEntity* entity, const PrintObject* object, size_t copy_id, int extruder, size_t num_of_copies)
{
    something_overridden = true;

    auto entity_map_it = (entity_map.emplace(std::make_tuple(entity, object), ExtruderPerCopy())).first; // (add and) return iterator
    ExtruderPerCopy& copies_vector = entity_map_it->second;
    copies_vector.resize(num_of_copies, -1);

    if (copies_vector[copy_id] != -1)
        std::cout << "ERROR: Entity extruder overriden multiple times!!!\n";    // A debugging message - this must never happen.

    copies_vector[copy_id] = extruder;
}

// BBS
void WipingExtrusions::set_support_extruder_override(const PrintObject* object, size_t copy_id, int extruder, size_t num_of_copies)
{
    something_overridden = true;
    support_map.emplace(object, extruder);
}

void WipingExtrusions::set_support_interface_extruder_override(const PrintObject* object, size_t copy_id, int extruder, size_t num_of_copies)
{
    something_overridden = true;
    support_intf_map.emplace(object, extruder);
}

// Finds first non-soluble extruder on the layer
int WipingExtrusions::first_nonsoluble_extruder_on_layer(const PrintConfig& print_config) const
{
    const LayerTools& lt = *m_layer_tools;
    for (auto extruders_it = lt.extruders.begin(); extruders_it != lt.extruders.end(); ++extruders_it)
        if (!print_config.filament_soluble.get_at(*extruders_it) && !print_config.filament_is_support.get_at(*extruders_it))
            return (*extruders_it);

    return (-1);
}

// Finds last non-soluble extruder on the layer
int WipingExtrusions::last_nonsoluble_extruder_on_layer(const PrintConfig& print_config) const
{
    const LayerTools& lt = *m_layer_tools;
    for (auto extruders_it = lt.extruders.rbegin(); extruders_it != lt.extruders.rend(); ++extruders_it)
        if (!print_config.filament_soluble.get_at(*extruders_it) && !print_config.filament_is_support.get_at(*extruders_it))
            return (*extruders_it);

    return (-1);
}

// Decides whether this entity could be overridden
bool WipingExtrusions::is_overriddable(const ExtrusionEntityCollection& eec, const PrintConfig& print_config, const PrintObject& object, const PrintRegion& region) const
{
    if (print_config.filament_soluble.get_at(m_layer_tools->extruder(eec, region)))
        return false;

    if (object.config().flush_into_objects)
        return true;

    if (!object.config().flush_into_infill || eec.role() != erInternalInfill)
        return false;

    return true;
}
bool WipingExtrusions::is_obj_overriddable(const ExtrusionEntityCollection &eec, const PrintObject &object) const
{
    if (object.config().flush_into_objects)
        return true;

    if (object.config().flush_into_infill && eec.role() == erInternalInfill)
        return true;

    return false;
}

// BBS
bool WipingExtrusions::is_support_overriddable(const ExtrusionRole role, const PrintObject& object) const
{
    if (!object.config().flush_into_support)
        return false;

    if (role == erMixed) {
        return object.config().support_filament == 0 || object.config().support_interface_filament == 0;
    }
    else if (role == erSupportMaterial || role == erSupportTransition) {
        return object.config().support_filament == 0;
    }
    else if (role == erSupportMaterialInterface) {
        return object.config().support_interface_filament == 0;
    }

    return false;
}

// Following function iterates through all extrusions on the layer, remembers those that could be used for wiping after toolchange
// and returns volume that is left to be wiped on the wipe tower.
float WipingExtrusions::mark_wiping_extrusions(const Print& print, unsigned int old_extruder, unsigned int new_extruder, float volume_to_wipe)
{
    const LayerTools& lt = *m_layer_tools;
    const float min_infill_volume = 0.f; // ignore infill with smaller volume than this

    if (! this->something_overridable || volume_to_wipe <= 0. || print.config().filament_soluble.get_at(old_extruder) || print.config().filament_soluble.get_at(new_extruder))
        return std::max(0.f, volume_to_wipe); // Soluble filament cannot be wiped in a random infill, neither the filament after it

    // BBS
    if (print.config().filament_is_support.get_at(old_extruder) || print.config().filament_is_support.get_at(new_extruder))
        return std::max(0.f, volume_to_wipe); // Support filament cannot be used to print support, infill, wipe_tower, etc.

    // we will sort objects so that dedicated for wiping are at the beginning:
    ConstPrintObjectPtrs object_list = print.objects().vector();
    // BBS: fix the exception caused by not fixed order between different objects
    std::sort(object_list.begin(), object_list.end(), [object_list](const PrintObject* a, const PrintObject* b) {
        if (a->config().flush_into_objects != b->config().flush_into_objects) {
            return a->config().flush_into_objects.getBool();
        }
        else {
            return a->id() < b->id();
        }
    });

    // We will now iterate through
    //  - first the dedicated objects to mark perimeters or infills (depending on infill_first)
    //  - second through the dedicated ones again to mark infills or perimeters (depending on infill_first)
    //  - then all the others to mark infills (in case that !infill_first, we must also check that the perimeter is finished already
    // this is controlled by the following variable:
    bool perimeters_done = false;

    for (int i=0 ; i<(int)object_list.size() + (perimeters_done ? 0 : 1); ++i) {
        if (!perimeters_done && (i==(int)object_list.size() || !object_list[i]->config().flush_into_objects)) { // we passed the last dedicated object in list
            perimeters_done = true;
            i=-1;   // let's go from the start again
            continue;
        }

        const PrintObject* object = object_list[i];

        // Finds this layer:
        const Layer* this_layer = object->get_layer_at_printz(lt.print_z, EPSILON);
        if (this_layer == nullptr)
        	continue;

        size_t num_of_copies = object->instances().size();

        // iterate through copies (aka PrintObject instances) first, so that we mark neighbouring infills to minimize travel moves
        for (unsigned int copy = 0; copy < num_of_copies; ++copy) {
            for (const LayerRegion *layerm : this_layer->regions()) {
                const auto &region = layerm->region();

                if (!object->config().flush_into_infill && !object->config().flush_into_objects && !object->config().flush_into_support)
                    continue;
                bool wipe_into_infill_only = !object->config().flush_into_objects && object->config().flush_into_infill;
                bool is_infill_first = print.config().is_infill_first;
                if (is_infill_first != perimeters_done || wipe_into_infill_only) {
                    for (const ExtrusionEntity* ee : layerm->fills.entities) {                      // iterate through all infill Collections
                        auto* fill = dynamic_cast<const ExtrusionEntityCollection*>(ee);

                        if (!is_overriddable(*fill, print.config(), *object, region))
                            continue;

                        if (wipe_into_infill_only && ! is_infill_first)
                            // In this case we must check that the original extruder is used on this layer before the one we are overridding
                            // (and the perimeters will be finished before the infill is printed):
                            if (!lt.is_extruder_order(lt.wall_filament(region), new_extruder))
                                continue;

                        if ((!is_entity_overridden(fill, object, copy) && fill->total_volume() > min_infill_volume))
                        {     // this infill will be used to wipe this extruder
                            set_extruder_override(fill, object, copy, new_extruder, num_of_copies);
                            if ((volume_to_wipe -= float(fill->total_volume())) <= 0.f)
                            	// More material was purged already than asked for.
	                            return 0.f;
                        }
                    }
                }

                // Now the same for perimeters - see comments above for explanation:
                if (object->config().flush_into_objects && is_infill_first == perimeters_done)
                {
                    for (const ExtrusionEntity* ee : layerm->perimeters.entities) {
                        auto* fill = dynamic_cast<const ExtrusionEntityCollection*>(ee);
                        if (is_overriddable(*fill, print.config(), *object, region) && !is_entity_overridden(fill, object, copy) && fill->total_volume() > min_infill_volume) {
                            set_extruder_override(fill, object, copy, new_extruder, num_of_copies);
                            if ((volume_to_wipe -= float(fill->total_volume())) <= 0.f)
                            	// More material was purged already than asked for.
	                            return 0.f;
                        }
                    }
                }
            }

            // BBS
            if (object->config().flush_into_support) {
                auto& object_config = object->config();
                const SupportLayer* this_support_layer = object->get_support_layer_at_printz(lt.print_z, EPSILON);

                do {
                    if (this_support_layer == nullptr)
                        break;

                    bool support_overriddable = object_config.support_filament == 0;
                    bool support_intf_overriddable = object_config.support_interface_filament == 0;
                    if (!support_overriddable && !support_intf_overriddable)
                        break;

                    auto &entities = this_support_layer->support_fills.entities;
                    if (support_overriddable && !is_support_overridden(object) && !(object_config.support_interface_not_for_body.value && !support_intf_overriddable &&(new_extruder==object_config.support_interface_filament-1||old_extruder==object_config.support_interface_filament-1))) {
                        set_support_extruder_override(object, copy, new_extruder, num_of_copies);
                        for (const ExtrusionEntity* ee : entities) {
                            if (ee->role() == erSupportMaterial || ee->role() == erSupportTransition)
                                volume_to_wipe -= ee->total_volume();

                            if (volume_to_wipe <= 0.f)
                                return 0.f;
                        }
                    }

                    if (support_intf_overriddable && !is_support_interface_overridden(object)) {
                        set_support_interface_extruder_override(object, copy, new_extruder, num_of_copies);
                        for (const ExtrusionEntity* ee : entities) {
                            if (ee->role() == erSupportMaterialInterface)
                                volume_to_wipe -= ee->total_volume();

                            if (volume_to_wipe <= 0.f)
                                return 0.f;
                        }
                    }
                } while (0);
            }
        }
    }
	// Some purge remains to be done on the Wipe Tower.
    assert(volume_to_wipe > 0.);
    return volume_to_wipe;
}



// Called after all toolchanges on a layer were mark_infill_overridden. There might still be overridable entities,
// that were not actually overridden. If they are part of a dedicated object, printing them with the extruder
// they were initially assigned to might mean violating the perimeter-infill order. We will therefore go through
// them again and make sure we override it.
void WipingExtrusions::ensure_perimeters_infills_order(const Print& print)
{
	if (! this->something_overridable)
		return;

    const LayerTools& lt = *m_layer_tools;
    unsigned int first_nonsoluble_extruder = first_nonsoluble_extruder_on_layer(print.config());
    unsigned int last_nonsoluble_extruder = last_nonsoluble_extruder_on_layer(print.config());

    for (const PrintObject* object : print.objects()) {
        // Finds this layer:
        const Layer* this_layer = object->get_layer_at_printz(lt.print_z, EPSILON);
        if (this_layer == nullptr)
        	continue;
        size_t num_of_copies = object->instances().size();

        for (size_t copy = 0; copy < num_of_copies; ++copy) {    // iterate through copies first, so that we mark neighbouring infills to minimize travel moves
            for (const LayerRegion *layerm : this_layer->regions()) {
                const auto &region = layerm->region();
                //BBS
                if (!object->config().flush_into_infill && !object->config().flush_into_objects)
                    continue;

                bool is_infill_first = print.config().is_infill_first;
                for (const ExtrusionEntity* ee : layerm->fills.entities) {                      // iterate through all infill Collections
                    auto* fill = dynamic_cast<const ExtrusionEntityCollection*>(ee);

                    if (!is_overriddable(*fill, print.config(), *object, region)
                     || is_entity_overridden(fill, object, copy) )
                        continue;

                    // This infill could have been overridden but was not - unless we do something, it could be
                    // printed before its perimeter, or not be printed at all (in case its original extruder has
                    // not been added to LayerTools
                    // Either way, we will now force-override it with something suitable:
                    //BBS
                    if (is_infill_first
                    //BBS
                    //|| object->config().flush_into_objects  // in this case the perimeter is overridden, so we can override by the last one safely
                    || lt.is_extruder_order(lt.wall_filament(region), last_nonsoluble_extruder    // !infill_first, but perimeter is already printed when last extruder prints
                    || ! lt.has_extruder(lt.sparse_infill_filament(region)))) // we have to force override - this could violate infill_first (FIXME)
                        set_extruder_override(fill, object, copy, (is_infill_first ? first_nonsoluble_extruder : last_nonsoluble_extruder), num_of_copies);
                    else {
                        // In this case we can (and should) leave it to be printed normally.
                        // Force overriding would mean it gets printed before its perimeter.
                    }
                }

                // Now the same for perimeters - see comments above for explanation:
                for (const ExtrusionEntity* ee : layerm->perimeters.entities) {                      // iterate through all perimeter Collections
                    auto* fill = dynamic_cast<const ExtrusionEntityCollection*>(ee);
                    if (is_overriddable(*fill, print.config(), *object, region) && ! is_entity_overridden(fill, object, copy))
                        set_extruder_override(fill, object, copy, (is_infill_first ? last_nonsoluble_extruder : first_nonsoluble_extruder), num_of_copies);
                }
            }
        }
    }
}

// Following function is called from GCode::process_layer and returns pointer to vector with information about which extruders should be used for given copy of this entity.
// If this extrusion does not have any override, nullptr is returned.
// Otherwise it modifies the vector in place and changes all -1 to correct_extruder_id (at the time the overrides were created, correct extruders were not known,
// so -1 was used as "print as usual").
// The resulting vector therefore keeps track of which extrusions are the ones that were overridden and which were not. If the extruder used is overridden,
// its number is saved as is (zero-based index). Regular extrusions are saved as -number-1 (unfortunately there is no negative zero).
const WipingExtrusions::ExtruderPerCopy* WipingExtrusions::get_extruder_overrides(const ExtrusionEntity* entity, const PrintObject* object, int correct_extruder_id, size_t num_of_copies)
{
	ExtruderPerCopy *overrides = nullptr;
    auto entity_map_it = entity_map.find(std::make_tuple(entity, object));
    if (entity_map_it != entity_map.end()) {
        overrides = &entity_map_it->second;
    	overrides->resize(num_of_copies, -1);
	    // Each -1 now means "print as usual" - we will replace it with actual extruder id (shifted it so we don't lose that information):
	    std::replace(overrides->begin(), overrides->end(), -1, -correct_extruder_id-1);
	}
    return overrides;
}

// BBS
int WipingExtrusions::get_support_extruder_overrides(const PrintObject* object)
{
    auto iter = support_map.find(object);
    if (iter != support_map.end())
        return iter->second;

    return -1;
}

int WipingExtrusions::get_support_interface_extruder_overrides(const PrintObject* object)
{
    auto iter = support_intf_map.find(object);
    if (iter != support_intf_map.end())
        return iter->second;

    return -1;
}


} // namespace Slic3r
