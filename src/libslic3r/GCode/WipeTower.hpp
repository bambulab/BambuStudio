#ifndef WipeTower_
#define WipeTower_

#include <cmath>
#include <string>
#include <sstream>
#include <utility>
#include <algorithm>

#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include <unordered_set>
#include "libslic3r/MultiNozzleUtils.hpp"
namespace Slic3r
{

class WipeTowerWriter;
class PrintConfig;
enum GCodeFlavor : unsigned char;


class WipeTower
{
public:
    friend class WipeTowerWriter;
    static const std::string never_skip_tag() { return "_GCODE_WIPE_TOWER_NEVER_SKIP_TAG"; }

	// WipeTower height to minimum depth map
	static const std::map<float, float> min_depth_per_height;
    static float get_limit_depth_by_height(float max_height);
    // Minimum tower depth for wrapping detection; the pre-slice size estimate must floor
    // its depth at this same value so it doesn't disagree with what gets sliced.
    static constexpr double wrapping_wipe_tower_depth = 10.0;
    static float get_auto_brim_by_height(float max_height);
    static TriangleMesh                 its_make_rib_tower(float width, float depth, float height, float rib_length, float rib_width, bool fillet_wall);
    static TriangleMesh                 its_make_rib_brim(const Polygon& brim, float layer_height);
    static Polygon                      rib_section(float width, float depth, float rib_length, float rib_width, bool fillet_wall);
    static Vec2f                        move_box_inside_box(const BoundingBox &box1, const BoundingBox &box2, int offset = 0);
    static Polygon                      rounding_polygon(Polygon &polygon, double rounding = 2., double angle_tol = 30. / 180. * PI);
    struct Extrusion
    {
		Extrusion(const Vec2f &pos, float width, unsigned int tool) : pos(pos), width(width), tool(tool) {}
		// End position of this extrusion.
		Vec2f				pos;
		// Width of a squished extrusion, corrected for the roundings of the squished extrusions.
		// This is left zero if it is a travel move.
		float 			width;
		// Current extruder index.
		unsigned int    tool;
	};

	struct NozzleChangeResult
    {
        std::string gcode;

        Vec2f start_pos;  // rotated
        Vec2f end_pos;

		Vec2f origin_start_pos;  // not rotated

        std::vector<Vec2f> wipe_path;
        bool is_extruder_change{false};
        bool zero_flush_idle_cooling{false};
        bool heater_off_only{false};
        float precool_target_temp{0};
        int precool_filament_id{-1};
    };

	struct ToolChangeResult
	{
		// Print heigh of this tool change.
		float					print_z;
		float 					layer_height;
		// G-code section to be directly included into the output G-code.
		std::string				gcode;
		// For path preview.
		std::vector<Extrusion> 	extrusions;
		// Initial position, at which the wipe tower starts its action.
		// At this position the extruder is loaded and there is no Z-hop applied.
		Vec2f						start_pos;
		// Last point, at which the normal G-code generator of Slic3r shall continue.
		// At this position the extruder is loaded and there is no Z-hop applied.
		Vec2f						end_pos;
		// Time elapsed over this tool change.
		// This is useful not only for the print time estimation, but also for the control of layer cooling.
		float  				    elapsed_time;

        // Is this a priming extrusion? (If so, the wipe tower rotation & translation will not be applied later)
        bool                    priming;

		bool                    is_tool_change{false};
		Vec2f                   tool_change_start_pos;

        // Pass a polyline so that normal G-code generator can do a wipe for us.
        // The wipe cannot be done by the wipe tower because it has to pass back
        // a loaded extruder, so it would have to either do a wipe with no retraction
        // (leading to https://github.com/prusa3d/PrusaSlicer/issues/2834) or do
        // an extra retraction-unretraction pair.
        std::vector<Vec2f> wipe_path;

		// BBS
        float purge_volume = 0.f;

        // Initial tool
        int initial_tool;

        // New tool
        int new_tool;

        // BBS: in bbl filament_change_gcode, toolhead will be moved to the wipe tower automatically.
        // But if finish_layer_tcr is before tool_change_tcr, we have to travel to the wipe tower before
        // executing the gcode finish_layer_tcr.
        bool is_finish_first = false;

        bool               is_contact = false;
        // True when this tool change emitted a diagonal side scrape, so GCode::append_tcr moves the
        // E deretract to after the scrape (via the [wipe_tower_deretract_gcode] placeholder). For the
        // scrape case start_pos / tool_change_start_pos are set to the scrape start (see tool_change_new).
        bool               side_scrape = false;
        // Where the scrape descends to. The wipe tower generator only knows the Z it planned the layer
        // at, while wipe_tower_no_sparse_layers prints the layer at a compacted, much lower Z. The two
        // scrape Z values are therefore emitted as [wipe_tower_scrape_z_top] / [wipe_tower_scrape_z_low]
        // placeholders and GCode::append_tcr resolves them from the fields below:
        // z_low for the uncompacted tower, depth plus min_height_above_base for the compacted one.
        float              side_scrape_z_low = 0.f;                 // lowest scrape Z in the planned layer frame
        float              side_scrape_depth = 0.f;                 // requested descent below the tower top
        float              side_scrape_min_height_above_base = 0.f; // keep this far above the tower's first layer
        NozzleChangeResult nozzle_change_result;

		// Sum the total length of the extrusion.
		float total_extrusion_length_in_plane() {
			float e_length = 0.f;
			for (size_t i = 1; i < this->extrusions.size(); ++ i) {
				const Extrusion &e = this->extrusions[i];
				if (e.width > 0) {
					Vec2f v = e.pos - (&e - 1)->pos;
					e_length += v.norm();
				}
			}
			return e_length;
		}
	};

    struct box_coordinates
    {
        box_coordinates(float left, float bottom, float width, float height) :
            ld(left        , bottom         ),
            lu(left        , bottom + height),
            rd(left + width, bottom         ),
            ru(left + width, bottom + height) {}
        box_coordinates(const Vec2f &pos, float width, float height) : box_coordinates(pos(0), pos(1), width, height) {}
        void translate(const Vec2f &shift) {
            ld += shift; lu += shift;
            rd += shift; ru += shift;
        }
        void translate(const float dx, const float dy) { translate(Vec2f(dx, dy)); }
        void expand(const float offset) {
            ld += Vec2f(- offset, - offset);
            lu += Vec2f(- offset,   offset);
            rd += Vec2f(  offset, - offset);
            ru += Vec2f(  offset,   offset);
        }
        void expand(const float offset_x, const float offset_y) {
            ld += Vec2f(- offset_x, - offset_y);
            lu += Vec2f(- offset_x,   offset_y);
            rd += Vec2f(  offset_x, - offset_y);
            ru += Vec2f(  offset_x,   offset_y);
        }
        Vec2f ld;  // left down
        Vec2f lu;	// left upper
        Vec2f rd;	// right lower
        Vec2f ru;  // right upper
    };

    // Construct ToolChangeResult from current state of WipeTower and WipeTowerWriter.
    // WipeTowerWriter is moved from !
    // slab_height is the thickness this extrusion actually lays down; it exceeds the layer height when the
    // band merges several base layers for a large nozzle. Negative means "use the layer height".
    ToolChangeResult construct_tcr(WipeTowerWriter& writer,
                                   bool priming,
                                   size_t old_tool,
                                   bool is_finish,
		                           bool is_tool_change, float purge_volume, bool is_contact, float slab_height = -1.f) const;

    ToolChangeResult construct_block_tcr(WipeTowerWriter& writer,
                                   bool priming,
                                   size_t filament_id,
                                   bool is_finish, float purge_volume, float slab_height = -1.f) const;


	// x			-- x coordinates of wipe tower in mm ( left bottom corner )
	// y			-- y coordinates of wipe tower in mm ( left bottom corner )
	// width		-- width of wipe tower in mm ( default 60 mm - leave as it is )
	// wipe_area	-- space available for one toolchange in mm
	// BBS: add partplate logic
	WipeTower(const PrintConfig& config, int plate_idx, Vec3d plate_origin, size_t initial_tool, const float wipe_tower_height, const std::vector<unsigned int>& slice_used_filaments);
    void set_foundation_required(bool required) { m_foundation_required = required; }


	// Set the extruder properties.
    // config_idx addresses the filament_options_with_variant arrays, which no longer line up with idx once a
    // filament is split across nozzles of different variants.
    void set_extruder(size_t idx, const PrintConfig& config, size_t config_idx);

    void set_shared_print_bed(const Polygons &bed) { m_shared_print_bed = bed; }
	// Appends into internal structure m_plan containing info about the future wipe tower
	// to be used before building begins. The entries must be added ordered in z.
    void plan_toolchange(float z_par, float layer_height_par, unsigned int old_tool, unsigned int new_tool, float wipe_volume_ec = 0.f, float wipe_volume_nc = 0.f, float prime_volume = 0.f);


	// Iterates through prepared m_plan, generates ToolChangeResults and appends them to "result"
	void generate(std::vector<std::vector<ToolChangeResult>> &result);

	WipeTower::ToolChangeResult only_generate_out_wall(bool is_new_mode = false);
    Polygon generate_support_wall(WipeTowerWriter &writer, const box_coordinates &wt_box, double feedrate, bool first_layer);
    Polygon generate_support_wall_new(WipeTowerWriter &writer, const box_coordinates &wt_box, double feedrate, bool first_layer,bool rib_wall, bool extrude_perimeter, bool skip_points);

    Polygon generate_rib_polygon(const box_coordinates &wt_box);
    float get_depth() const { return m_wipe_tower_depth; }
    float get_brim_width() const { return m_wipe_tower_brim_width_real; }
    BoundingBoxf get_bbx() const {
        if (m_outer_wall.empty()) return BoundingBoxf({Vec2d(0,0)});
        BoundingBox  box = get_extents(m_outer_wall.begin()->second);
        BoundingBoxf res = BoundingBoxf(unscale(box.min), unscale(box.max));
        return res;
    }
    std::map<float, Polylines> get_outer_wall() const
    {
        return m_outer_wall;
    }
    float get_height() const { return m_wipe_tower_height; }
    float get_layer_height() const { return m_layer_height; }
    float get_rib_length() const { return m_rib_length; }
    float get_rib_width() const { return m_rib_width; }

	void set_last_layer_extruder_fill(bool extruder_fill) {
        if (!m_plan.empty()) {
			m_plan.back().extruder_fill = extruder_fill;
		}
	}


	// Switch to a next layer.
	void set_layer(
		// Print height of this layer.
		float print_z,
		// Layer height, used to calculate extrusion the rate.
		float layer_height,
		// Maximum number of tool changes on this layer or the layers below.
		size_t max_tool_changes,
		// Is this the first layer of the print? In that case print the brim first.
		bool is_first_layer,
		// Is this the last layer of the waste tower?
		bool is_last_layer)
	{
		m_z_pos 				= print_z;
		m_layer_height			= layer_height;
		m_depth_traversed  = 0.f;
        m_current_layer_finished = false;
		//m_current_shape = (! is_first_layer && m_current_shape == SHAPE_NORMAL) ? SHAPE_REVERSED : SHAPE_NORMAL;
		m_current_shape = SHAPE_NORMAL;
		if (is_first_layer) {
            m_num_layer_changes = 0;
            m_num_tool_changes 	= 0;
        } else
            ++ m_num_layer_changes;

		// Calculate extrusion flow from desired line width, nozzle diameter, filament diameter and layer_height:
		m_extrusion_flow = extrusion_flow(layer_height);
        // Advance m_layer_info iterator, making sure we got it right
		while (!m_plan.empty() && m_layer_info->z < print_z - WT_EPSILON && m_layer_info+1 != m_plan.end())
			++m_layer_info;
	}

	// Return the wipe tower position.
	const Vec2f& 		 position() const { return m_wipe_tower_pos; }
	// Return the wipe tower width.
	float     		 width()    const { return m_wipe_tower_width; }
	// The wipe tower is finished, there should be no more tool changes or wipe tower prints.
	bool 	  		 finished() const { return m_max_color_changes == 0; }

	// Returns gcode to prime the nozzles at the front edge of the print bed.
	std::vector<ToolChangeResult> prime(
		// print_z of the first layer.
		float 						initial_layer_print_height,
		// Extruder indices, in the order to be primed. The last extruder will later print the wipe tower brim, print brim and the object.
		const std::vector<unsigned int> &tools,
		// If true, the last priming are will be the same as the other priming areas, and the rest of the wipe will be performed inside the wipe tower.
		// If false, the last priming are will be large enough to wipe the last extruder sufficiently.
		bool 						last_wipe_inside_wipe_tower);

	// Returns gcode for a toolchange and a final print head position.
	// On the first layer, extrude a brim around the future wipe tower first.
	// BBS
    ToolChangeResult tool_change(size_t new_tool, bool extrude_perimeter = false, bool first_toolchange_to_nonsoluble = false);

	NozzleChangeResult nozzle_change(int old_filament_id, int new_filament_id);

	// Fill the unfilled space with a sparse infill.
	// Call this method only if layer_finished() is false.
    ToolChangeResult finish_layer(bool extruder_perimeter = true, bool extruder_fill = true);

	// Calculates extrusion flow needed to produce required line width for given layer height
    float extrusion_flow(float layer_height = -1.f) const // negative layer_height - return current m_extrusion_flow
    {
        if (layer_height < 0) return m_extrusion_flow;
        return layer_height * (m_perimeter_width - layer_height * (1.f - float(M_PI) / 4.f)) / filament_area();
    }
    float nozzle_change_extrusion_flow(float layer_height = -1.f) const // negative layer_height - return current m_extrusion_flow
    {
        if (layer_height < 0)
            return m_extrusion_flow;
        return layer_height * (m_nozzle_change_perimeter_width - layer_height * (1.f - float(M_PI) / 4.f)) / filament_area();
    }
    // Same, for an explicit line width. Needed once a large nozzle purges with a wider line than the
    // tower-wide m_perimeter_width, and once a merged slab is taller than the layer height.
    float extrusion_flow_for(float layer_height, float line_width) const
    {
        if (layer_height < 0) return m_extrusion_flow;
        return layer_height * extrusion_spacing_for(layer_height, line_width) / filament_area();
    }
    // Center-to-center spacing of adjacent extrusions of the given width and height.
    float extrusion_spacing_for(float layer_height, float line_width) const
    {
        return line_width - layer_height * (1.f - float(M_PI) / 4.f);
    }

	bool get_floating_area(float& start_pos_y, float& end_pos_y) const;
	bool need_thick_bridge_flow(float pos_y, float slab_height) const;
    float get_extrusion_flow() const { return m_extrusion_flow; }

	// Is the current layer finished?
	bool 			 layer_finished() const {
        return m_current_layer_finished;
	}

    std::vector<float> get_used_filament() const { return m_used_filament_length; }
    int get_number_of_toolchanges() const { return m_num_tool_changes; }

	void set_has_tpu_filament(bool has_tpu) { m_has_tpu_filament = has_tpu; }

    bool has_tpu_filament() const { return m_has_tpu_filament; }
    struct FilamentParameters {
        std::string 	    material = "PLA";
        int                 category;
        bool                is_soluble = false;
        // BBS
        bool                is_support = false;
        int  			    nozzle_temperature = 0;
        int  			    nozzle_temperature_initial_layer = 0;
        // BBS: remove useless config
        //float               loading_speed = 0.f;
        //float               loading_speed_start = 0.f;
        //float               unloading_speed = 0.f;
        //float               unloading_speed_start = 0.f;
        //float               delay = 0.f ;
        //int                 cooling_moves = 0;
        //float               cooling_initial_speed = 0.f;
        //float               cooling_final_speed = 0.f;
        float               ramming_line_width_multiplicator = 1.f;
        float               ramming_step_multiplicator = 1.f;
        float               max_e_speed = std::numeric_limits<float>::max();
        std::vector<float>  ramming_speed;
        float               nozzle_diameter;
        // Per-filament tower widths. The shared tower grid follows the smallest nozzle, while a larger
        // nozzle still purges and rams with widths appropriate for that nozzle.
        float               perimeter_width{0.f};
        float               nozzle_change_perimeter_width{0.f};
        // Feedrate scale factor for tower extrusions when hotends are handled as extruders.
        float               feedrate_factor{1.f};
        float               filament_area;
        float               retract_length;
        float               retract_speed;
        float               wipe_dist;
        std::pair<float,float>  max_e_ramming_speed;//[0]extruder change [1]nozzle change
        std::pair<float, float> ramming_travel_time; // Travel time after ramming
        std::pair<std::vector<float>,std::vector<float>>  precool_t;//Pre-cooling time, set to 0 to ensure the ramming speed is controlled solely by ramming volumetric speed.
        std::pair<std::vector<float>, std::vector<float>> precool_t_first_layer;
        std::pair<int,int>    precool_target_temp;
        float filament_cooling_before_tower = 0.f;
        bool  idle_travel_cooling_enabled = true;
        float flat_iron_area;
        float filament_tower_interface_print_temp;
        float filament_tower_interface_pre_extrusion_dist = 0;
        float filament_tower_interface_pre_extrusion_length = 0;
        std::vector<double> speed_factor_curve;
        float filament_petg_pre_extrusion_offset_dist = 0;
    };


    void set_used_filament_ids(const std::vector<int> &used_filament_ids) { m_used_filament_ids = used_filament_ids; };
    // Print::uses_multiple_nozzle_diameters(), handed in so the tower gates its multi-diameter ladder on the
    // same fact the model and the support merges use instead of deriving one of its own.
    void set_multi_diameter_print(bool multi_diameter) { m_multi_diameter_print = multi_diameter; }
    void set_tower_grid_reference_nozzle(float nozzle_diameter) { m_tower_grid_reference_nozzle = nozzle_diameter; }
    void set_tower_grid_layer_height(float layer_height) { m_tower_grid_layer_height = layer_height; }
    // Multi-diameter mode: feedrate factor and line widths come from the hardcoded per-diameter
    // defaults (multi_nozzle_prime_tower_*). Must be set before set_extruder().
    void set_per_nozzle_tower_params(bool per_nozzle) { m_per_nozzle_tower_params = per_nozzle; }
    // Direct single-diameter layering still resolves shared tower widths from the one nozzle in use.
    // Must be called after set_extruder() for every filament.
    void set_single_diameter_parameters(float nozzle_diameter);
    void set_filament_categories(const std::vector<int> & filament_categories) { m_filament_categories = filament_categories;};
    void set_nozzle_group_result(const MultiNozzleUtils::LayeredNozzleGroupResult &multi_nozzle_group_result) { m_multi_nozzle_group_result = &multi_nozzle_group_result; };
    std::vector<int> m_used_filament_ids;
    bool m_multi_diameter_print{false};
    bool m_per_nozzle_tower_params{false};
    std::vector<int> m_filament_categories;
    const MultiNozzleUtils::LayeredNozzleGroupResult *m_multi_nozzle_group_result{nullptr};

    enum class WipeTowerLayerType : unsigned char { Normal, Contact, Solid, Contact_UP};// Contact layer should be solid and reduce feed

	struct WipeTowerBlock
    {
        int              block_id{0};
        int              filament_adhesiveness_category{0};
        // Packed (adhesiveness category, nozzle diameter, lane) key used by multi-diameter tower bands.
        int              block_key{0};
        std::vector<float>      layer_depths;
        //std::vector<bool>       solid_infill;
        std::vector<float>      finish_depth{0}; // the start pos of finish frame for every layer
        std::vector<WipeTowerLayerType> layers_type;     // type of the layer, normal, Contact or Solid
        std::vector<float>      layer_heights;
        std::vector<char>       layer_reserved;
        // Filament that fills this band on that layer, or -1 to choose one while generating.
        // The support under an extra TPU lane records it so a whole run stays on one material.
        std::vector<int>        layer_fill_filament;
        float            depth{0};
        float            start_depth{0};
        float            cur_depth{0};
        int              last_filament_change_id{-1};
        int              last_nozzle_change_id{-1};
	};

	struct BlockDepthInfo
    {
        int category{-1};
        float depth{0};
        float nozzle_change_depth{0};
	};

    // Pack (category, nozzle diameter, lane) into the single int the block bookkeeping is keyed by.
    static int encode_block_key(int category, float nozzle_diameter, int lane)
    {
        const int diam_units = std::min(0x3F, int(nozzle_diameter / 0.05f + 0.5f));
        return (std::max(0, category) << 10) | (diam_units << 4) | (lane & 0xF);
    }
    // Adhesiveness category a block key belongs to. Bands that only differ in nozzle diameter or lane
    // share a category, so anything reasoning about material interfaces has to compare on this.
    static int decode_block_category(int block_key) { return block_key >> 10; }
    // Block key of the band a filament purges into on a given layer.
    int get_block_key(int filament_id, int layer_id) const;
    // Lane-independent key, used for values shared by all lanes of a (category, diameter) group.
    int get_gap_key(int filament_id) const { return get_block_key(filament_id, -1); }

	std::vector<std::vector<BlockDepthInfo>> m_all_layers_depth;
	std::vector<WipeTowerBlock> m_wipe_tower_blocks;
    int                  m_last_block_id;
    WipeTowerBlock*      m_cur_block{nullptr};
    // Lane picked for each purge event, indexed by layer then filament. Empty means "everything on lane 0",
    // which is what a single-diameter print always resolves to.
    std::vector<std::unordered_map<int, int>> m_lane_assignment;
    // Slab height of a single band on a layer, indexed by layer then block key. Only holds the bands that
    // dig their own trench; everything else runs at m_layer_slab_height.
    std::vector<std::unordered_map<int, float>> m_block_slab_height;
    // Block keys that must stay empty on a layer because a merged slab above reaches down into them.
    std::vector<std::unordered_set<int>> m_reserved_blocks;
    // Height the whole tower lays down on a planned layer, and which single-material layers are merged out.
    // The scalar model merge planner partitions every no-tool-change run; each group prints at its top Z.
    std::vector<float> m_layer_slab_height;
    std::vector<char>  m_layer_skipped;
    // The outer wall has its own Z ladder over every layer the tower prints, planned independently of where
    // the tool changes fall. Lower layers of a merged wall span omit only the wall; purge bands on those
    // layers still print normally.
    std::vector<int>   m_wall_filament;
    std::vector<float> m_wall_slab_height;
    std::vector<char>  m_wall_reserved;
    // Set by the ladder planner when the tower deviates from one slab per base layer. While false the tower
    // behaves exactly as before.
    bool m_merged_slabs_active{false};
    // Keep the raw first tower layer only when that Z also carries real object or support extrusion.
    bool m_foundation_required{true};
    bool has_multiple_nozzle_diameters() const;
    // Whether the tower prints nothing at all on that planned layer.
    bool layer_is_skipped(int layer_id) const
    {
        return layer_id >= 0 && size_t(layer_id) < m_layer_skipped.size() && m_layer_skipped[layer_id] != 0;
    }
    // Height the tower lays down on that planned layer, base layer height while no layer is dropped.
    float layer_slab_height(int layer_id, float fallback) const
    {
        return (layer_id >= 0 && size_t(layer_id) < m_layer_slab_height.size() && m_layer_slab_height[layer_id] > 0.f) ?
            m_layer_slab_height[layer_id] : fallback;
    }
    // Whether this filament's nozzle covers the height of every whole-tower slab.
    bool can_print_base_layers(int filament_id) const;

	// help function
    // Looks the block up by the key produced by encode_block_key().
    WipeTowerBlock* get_block_by_category(int block_key, bool create);
    void add_depth_to_block(int filament_id, int block_key, float depth, bool is_nozzle_change = false);
	int get_filament_category(int filament_id) const;
	void reset_block_status();
    int get_wall_filament_for_all_layer() const;
	// for generate new wipe tower
    void generate_new(std::vector<std::vector<WipeTower::ToolChangeResult>> &result);

	void plan_tower_new();
    // Decide which planned layers the tower prints at all and how thick each printed layer is.
    void plan_tower_ladder();
    // First planned layer the tower actually visits after ladder planning.
    int first_printed_layer_idx() const;
    // Give the layers [run_begin, run_end] to the tool-change layer above them. False when that layer cannot
    // take them, which leaves the run at its base layer heights.
    bool absorb_run_into_next_layer(int run_begin, int run_end);
    // Decide which purge events dig a trench below themselves and which lane each one uses.
    void plan_merged_slabs();
    // Merge the fill of every band over the layers on which it does not purge.
    void plan_merged_block_fill();
    // Merge the outer wall over the layers the tower prints, independently of where the tool changes fall.
    void plan_merged_outer_wall();
    // Report bands and walls whose planned slabs claim Z a lower layer already filled. Diagnostics only.
    void audit_merged_slabs() const;
    // One step of a merge ladder: the printed steps [begin_step, top_step] print together at top_step.
    struct MergeLadderGroup
    {
        int   begin_step  { 0 };
        int   top_step    { 0 };
        int   filament_id { -1 };
        float height      { 0.f };
    };
    // Planned layers the tower prints, in Z order, starting at its foundation. Everything merged across Z
    // is planned on these steps: the layers in between are not visited at all.
    void collect_printed_steps(std::vector<int> &printed) const;
    // Bottom of the tower foundation.
    double tower_bed_z() const;
    // Filaments that visit the tower on each printed step and can therefore lay something down there: the
    // ones taking part in its tool changes, or the loaded one on a layer that only carries the tower.
    // Sorted by nozzle diameter so that the finest able nozzle is preferred.
    void collect_tower_visitors(const std::vector<int> &printed, std::vector<std::vector<int>> &visitors) const;
    // Partition the printed steps [first_step, last_step] into merge groups. Each group prints at its top
    // step and covers the Z span down to the group below it, starting from bottom_z. reservable[step] == 0
    // forbids any group from swallowing that step, can_merge_top[step] == 0 keeps that step on its own.
    // False when no partition exists.
    bool plan_merge_ladder(const std::vector<int> &printed, int first_step, int last_step, double bottom_z,
                           const std::vector<std::vector<int>> &visitors, const std::vector<char> &reservable,
                           const std::vector<char> &can_merge_top, std::vector<MergeLadderGroup> &groups) const;
    // Smallest and largest layer height the nozzle of this filament can lay down.
    float min_layer_height_for_filament(int filament_id) const;
    float max_layer_height_for_filament(int filament_id) const;
    bool  filament_can_print_height(int filament_id, float height) const;
    // Merge ladder scored against a fixed nozzle band instead of the base cadence. preferred_height
    // overrides the visiting filament's grid when the geometry belongs to one fixed nozzle band; zero keeps
    // the visitor-specific behavior used by the shared outer wall. prefer_max_height scores each slab
    // against the extruding nozzle's maximum layer height and charges one unit per slab, so the span is
    // covered in as few layers as that nozzle can lay down. height_ceiling, when positive, is the block
    // nozzle's printable maximum: a merged slab may not grow past it, even when the filament actually
    // extruding could. False when no partition exists.
    bool plan_band_merge_ladder(const std::vector<int> &printed, int first_step, int last_step, double bottom_z,
                                const std::vector<std::vector<int>> &visitors, const std::vector<char> &reservable,
                                const std::vector<char> &can_merge_top, std::vector<MergeLadderGroup> &groups,
                                float preferred_height = 0.f, bool prefer_max_height = false,
                                float height_ceiling = 0.f) const;
    // Fold groups thinner than the extruding nozzle's band into a neighbor, but only while the result stays
    // inside that nozzle's printable window. height_ceiling, when set, is a tighter cap from the block nozzle.
    void fold_thin_ladder_groups(const std::vector<std::vector<int>> &visitors, const std::vector<char> &reservable,
                                 const std::vector<char> &can_merge_top, std::vector<MergeLadderGroup> &groups,
                                 float height_ceiling = 0.f) const;
    // Number of base grid layers this filament's nozzle should merge into one band.
    size_t preferred_band_size_for_filament(int filament_id) const;
    // Band height this nozzle should extrude on the tower grid.
    float preferred_height_for_filament(int filament_id) const;
    // Whether the tower is planned in merged slabs: it has several nozzle diameters and nothing needs it on
    // every layer as the ladder left it.
    bool merged_fill_active() const;
    // The filament still loaded when the tower reaches this layer, or -1.
    int loaded_filament(int layer_id) const;
    // Height of the slab this filament lays down on that layer: its band's trench if it digs one, otherwise
    // whatever the whole tower lays down there.
    float slab_height_for(int filament_id, int layer_id, float fallback) const
    {
        if (layer_id >= 0 && size_t(layer_id) < m_block_slab_height.size()) {
            const auto iter = m_block_slab_height[layer_id].find(get_block_key(filament_id, layer_id));
            if (iter != m_block_slab_height[layer_id].end() && iter->second > 0.f)
                return iter->second;
        }
        return layer_slab_height(layer_id, fallback);
    }
    // Tail slowdown asks this instead of the nominal layer height, so a merged slab that
    // reaches the bed is treated as a first layer for that nozzle only.
    float filament_extrusion_height(int filament_id) const
    {
        return slab_height_for(filament_id, int(m_cur_layer_id), m_layer_height);
    }
	void generate_wipe_tower_blocks(bool add_solid_flag);
    void update_all_layer_depth(float wipe_tower_depth);
    void set_nozzle_last_layer_id();
    void set_first_layer_flow_ratio(const float flow_ratio);
    void calc_block_infill_gap();
    ToolChangeResult   tool_change_new(size_t new_tool, bool solid_change = false, bool solid_nozzlechange=false);
    NozzleChangeResult ramming(int old_filament_id, int new_filament_id, bool solid_change = false, bool extruder_change = true); // extruder_chang means nozzle_change
    NozzleChangeResult heater_off_nozzle_change(int old_filament_id, int new_filament_id);
    NozzleChangeResult idle_travel_cooling(int old_filament_id, int new_filament_id);
    // Number of brim/chamfer loops printed around a layer of the given height, where dist_to_first counts
    // layers above the tower's first layer. line_width is the extrusion that actually draws those loops
    // (the wall filament), not the tower-wide m_perimeter_width, which follows the smallest nozzle and
    // would pack a 0.4 mm wall at 0.2 mm pitch. Shared by finish_layer_new(), which prints the loops, and
    // side_scrape_z_floor(), which has to know how far up they reach; deriving both from one place keeps
    // the scrape from descending into a flare the printer actually laid down.
    int                brim_chamfer_loops_num(float layer_height, bool first_layer, int dist_to_first, float line_width) const;
    ToolChangeResult   finish_layer_new(bool extrude_perimeter = true, bool extrude_fill = true, bool extrude_fill_wall = true,
                                        float wall_height = -1.f, bool wall_reserved = false);
    ToolChangeResult   finish_block(const WipeTowerBlock &block, int filament_id, bool extrude_fill = true);
    ToolChangeResult   finish_block_solid(const WipeTowerBlock &block, int filament_id, bool extrude_fill = true, WipeTowerLayerType layer_type = WipeTowerLayerType::Normal);
    void toolchange_wipe_new(WipeTowerWriter &writer, const box_coordinates &cleaning_box, float wipe_length,bool solid_toolchange=false);
    // Vertical extent of one scrape, see ToolChangeResult::side_scrape_z_low for how it is consumed.
    struct SideScrapeZ
    {
        float z_low                 = 0.f;
        float depth                 = 0.f;
        float min_height_above_base = 0.f;
    };
    // After a tool change and before the wipe line, move to the prime tower side wall and do a
    // diagonal lift scrape (travel only) so the oozed filament stub sticks to the side wall.
    // Returns false when this layer leaves no room for the scrape and nothing was emitted, in which case
    // out_scrape_start / out_z are left untouched and the tool change has to fall back to its normal
    // start point.
    bool toolchange_side_scrape(WipeTowerWriter &writer, const box_coordinates &cleaning_box, const Vec2f &initial_position,
                                Vec2f *out_scrape_start = nullptr, SideScrapeZ *out_z = nullptr);
    // The scrape works on one pair of opposite tower walls, so the geometry helpers below are written
    // against two abstract axes instead of X and Y: "across" is the axis the nozzle approaches the wall
    // on and the diagonal moves in, "along" is the wall tangent. across_is_x selects the pair:
    // true = left/right walls (across = X, along = Y), false = front/back walls (across = Y, along = X).
    // positive_side picks the wall with the larger coordinate (right resp. back).
    // All of them work in the tower footprint frame the m_outer_wall polylines are stored in, which is
    // the writer frame with m_y_shift already folded in.
    //
    // Outermost wall coordinate at one `along` value, for the layer whose stored Z is nearest to z.
    bool outer_wall_extreme_at(float z, bool across_is_x, bool positive_side, float along, float &out_across) const;
    // Span of the wall (at the current Z, taking previous and current layer walls into account) that runs
    // straight, grown outwards from the middle of the wall so that the rounded corners and the rib bulges
    // stay out of it. Collapses to the middle of the wall when it runs straight nowhere; only a wall with
    // no stored geometry at all returns false.
    bool side_wall_straight_along_range(bool across_is_x, bool positive_side, float &along_begin, float &along_end) const;
    // Lowest Z the nozzle may descend to. Because keeps_outer_walls_unshrunk() locks the wall of every
    // layer to the same footprint, the only part of the tower reaching further out is the brim chamfer
    // at the base, so the floor is simply the top of the highest chamfered layer (the tower's own first
    // layer when there is no brim). This also keeps the nozzle off the bed. The rib arms do grow towards
    // the base, but the scrape only ever runs on the middle of a wall, which they do not reach into.
    float side_scrape_z_floor() const;
    Vec2f              get_rib_offset() const { return m_rib_offset; }
    bool               is_need_ramming(int filament_id_1, int filament_id_2, int layer_id) const;
    bool               is_same_extruder(int filament_id_1, int filament_id_2, int layer_id) const;
    bool               is_same_nozzle(int filament_id_1, int filament_id_2, int layer_id) const;
    int                get_nozzle_id(int filament_id, int layer_id) const;
    int                get_extruder_id(int filament_id, int layer_id) const;

private:
	enum wipe_shape // A fill-in direction
	{
		SHAPE_NORMAL = 1,
		SHAPE_REVERSED = -1
	};

    const float Width_To_Nozzle_Ratio = 1.25f; // desired line width (oval) in multiples of nozzle diameter - may not be actually neccessary to adjust
    static constexpr float WT_EPSILON = 1e-3f;
    // How far the brim chamfer may flare out beyond the tower wall.
    static constexpr float WT_MAX_CHAMFER_WIDTH = 3.f;
    float filament_area() const {
        return m_filpar[m_current_tool].filament_area;
    }
    // Line widths of a specific filament. Fall back to the tower-wide values when the filament index is
    // out of range (priming / not-yet-registered filaments).
    float filament_perimeter_width(int filament_id) const {
        return (filament_id >= 0 && size_t(filament_id) < m_filpar.size() && m_filpar[filament_id].perimeter_width > 0.f) ?
            m_filpar[filament_id].perimeter_width : m_perimeter_width;
    }
    float filament_nozzle_change_width(int filament_id) const {
        return (filament_id >= 0 && size_t(filament_id) < m_filpar.size() && m_filpar[filament_id].nozzle_change_perimeter_width > 0.f) ?
            m_filpar[filament_id].nozzle_change_perimeter_width : m_nozzle_change_perimeter_width;
    }
    // 1.25x the nozzle of the filament; the configured width when the nozzle is unknown.
    float nozzle_based_width(int filament_id) const {
        if (filament_id >= 0 && size_t(filament_id) < m_filpar.size() && m_filpar[filament_id].nozzle_diameter > 0.f)
            return m_filpar[filament_id].nozzle_diameter * Width_To_Nozzle_Ratio;
        return filament_perimeter_width(filament_id);
    }
    // min(1.25x the nozzle, configured tower line width): the outer wall and the sparse grid are capped at
    // this width to save material. A configured width of 0 already resolves to 1.25x the nozzle.
    float capped_wall_grid_width(int filament_id) const {
        return std::min(nozzle_based_width(filament_id), filament_perimeter_width(filament_id));
    }

    int    m_slice_used_filaments      = 0;
    int    m_wrapping_detection_layers = 0;
    bool   m_enable_wrapping_detection = false;
	bool   m_enable_timelapse_print = false;
	bool   m_semm               = true; // Are we using a single extruder multimaterial printer?
    Vec2f  m_wipe_tower_pos; 			// Left front corner of the wipe tower in mm.
	float  m_wipe_tower_width; 			// Width of the wipe tower.
	float  m_wipe_tower_depth 	= 0.f; 	// Depth of the wipe tower
    // Layer and line width of the outer wall only_generate_out_wall() drew (smooth timelapse / wrapping
    // detection). finish_layer_new() then puts the brim and chamfer loops around that wall.
    float  m_out_wall_only_z     = -1.f;
    float  m_out_wall_only_width = 0.f;
	// BBS
	float  m_wipe_tower_height = 0.f;
    float  m_wipe_tower_brim_width      = 0.f; 	// Width of brim (mm) from config
    float  m_wipe_tower_brim_width_real = 0.f; 	// Width of brim (mm) after generation
	float  m_wipe_tower_rotation_angle = 0.f; // Wipe tower rotation angle in degrees (with respect to x axis)
    float  m_internal_rotation  = 0.f;
	float  m_y_shift			= 0.f;  // y shift passed to writer
	float  m_z_pos 				= 0.f;  // Current Z position.
	float  m_layer_height 		= 0.f; 	// Current layer height.
	size_t m_max_color_changes 	= 0; 	// Maximum number of color changes per layer.
    int    m_old_temperature    = -1;   // To keep track of what was the last temp that we set (so we don't issue the command when not neccessary)
    float  m_travel_speed       = 0.f;
    float  m_first_layer_speed  = 0.f;
    size_t m_first_layer_idx    = size_t(-1);
    Vec2f            m_origin;
    std::vector<int>    m_last_layer_id;
    std::pair<std::vector<double>,std::vector<double>> m_filaments_change_length;//[0]extruder change [1]nozzle change
    size_t       m_cur_layer_id;
    NozzleChangeResult m_nozzle_change_result;
    bool               m_has_tpu_filament{false};
    bool               m_is_multi_extruder{false};
    bool               m_use_gap_wall{false};
    bool               m_use_rib_wall{false};
    float              m_rib_length=0.f;
    float              m_rib_width=0.f;
    float              m_extra_rib_length=0.f;
    bool               m_used_fillet{false};
    Vec2f              m_rib_offset{Vec2f(0.f, 0.f)};
    bool               m_tower_framework{false};
    bool               m_need_reverse_travel{false};
    bool               m_enable_tower_interface_features{false};
	// G-code generator parameters.
    // BBS: remove useless config
    //float           m_cooling_tube_retraction   = 0.f;
    //float           m_cooling_tube_length       = 0.f;
    //float           m_parking_pos_retraction    = 0.f;
    //float           m_extra_loading_move        = 0.f;
    float           m_bridging                  = 0.f;
    // Not the raw wipe_tower_no_sparse_layers option: it also accounts for the settings that put a
    // tower on every layer regardless. Keeping m_first_layer_idx at the bottom-most planned layer and
    // counting the filament of a layer that is printed anyway both depend on that distinction.
    bool            m_sparse_layers_skipped       = false;
    // BBS: remove useless config
    //bool            m_set_extruder_trimpot      = false;
    bool            m_adhesion                  = true;
    GCodeFlavor     m_gcode_flavor;
    bool                      m_is_multiple_nozzle = false;
    std::vector<unsigned int> m_normal_accels;
    std::vector<unsigned int> m_first_layer_normal_accels;
    std::vector<unsigned int> m_travel_accels;
    std::vector<unsigned int> m_first_layer_travel_accels;
    unsigned int              m_max_accels;
    bool                      m_accel_to_decel_enable;
    float                     m_accel_to_decel_factor;
    bool                      m_enable_arc_fitting = true;
    std::vector<double>       m_hotend_heating_rate;
    std::vector<double>       m_hotend_cooling_rate;
    Polygons                  m_shared_print_bed;

    // Bed properties
    enum {
        RectangularBed,
        CircularBed,
        CustomBed
    } m_bed_shape;
    float m_bed_width; // width of the bed bounding box
    Vec2f m_bed_bottom_left; // bottom-left corner coordinates (for rectangular beds)

    float m_first_layer_flow_ratio;
	float m_perimeter_width = 0.4f * Width_To_Nozzle_Ratio; // Width of an extrusion line, also a perimeter spacing for 100% infill.
    float m_nozzle_change_perimeter_width = 0.4f * Width_To_Nozzle_Ratio;
    float m_tower_grid_nozzle_diameter = 0.f;
    float m_tower_grid_reference_nozzle = 0.f;
    float m_tower_grid_layer_height = 0.f;
	float m_extrusion_flow = 0.038f; //0.029f;// Extrusion flow is derived from m_perimeter_width, layer height and filament diameter.
    std::unordered_map<int, std::pair<float,float>> m_block_infill_gap_width; // categories to infill_gap: toolchange gap, nozzlechange gap
	// Extruder specific parameters.
    std::vector<FilamentParameters> m_filpar;


	// State of the wipe tower generator.
	unsigned int m_num_layer_changes = 0; // Layer change counter for the output statistics.
	unsigned int m_num_tool_changes  = 0; // Tool change change counter for the output statistics.
	///unsigned int 	m_idx_tool_change_in_layer = 0; // Layer change counter in this layer. Counting up to m_max_color_changes.
	bool m_print_brim = true;
	// A fill-in direction (positive Y, negative Y) alternates with each layer.
	wipe_shape   	m_current_shape = SHAPE_NORMAL;
    size_t 	m_current_tool  = 0;
	// BBS
    //const std::vector<std::vector<float>> wipe_volumes;

	float           m_depth_traversed = 0.f; // Current y position at the wipe tower.
    bool            m_current_layer_finished = false;
	bool 			m_left_to_right   = true;
	float			m_extra_spacing   = 1.f;
	float           m_tpu_fixed_spacing = 2;
    float           m_max_speed = 5400.f;  // the maximum printing speed on the prime tower.
    float           m_tail_speed_factor = 1.f;  // speed scale factor applied to the tail of each wipe-tower visit.
    float           m_tail_slowdown_volume = 10.f;  // only the last N mm3 of each wipe-tower visit is slowed down; 0 = whole visit.
    std::vector<std::vector<Vec2f>> m_wall_skip_points;
    std::map<float,Polylines> m_outer_wall;
    std::vector<double>        m_printable_height;
    bool is_first_layer() const { return is_first_layer_for_slab(m_layer_height); }
    // Whether an extrusion of slab_height topping out at the current Z sits on the bed. A merged slab
    // printed above the tower's first layer still lands on the bed when it spans down to it.
    bool is_first_layer_for_slab(float slab_height) const
    {
        if (m_plan.empty() || m_layer_info == m_plan.end())
            return false;
        const size_t idx = size_t(m_layer_info - m_plan.begin());
        if (idx == m_first_layer_idx)
            return true;
        const size_t first = m_first_layer_idx == size_t(-1) ? 0 : m_first_layer_idx;
        if (first >= m_plan.size())
            return false;
        const float bed = m_plan[first].z - m_plan[first].height;
        return m_z_pos - slab_height <= bed + WT_EPSILON;
    }
    float calc_tower_feedrate(int filament_id, float extrusion_flow) const {
        return calc_tower_feedrate(filament_id, extrusion_flow, is_first_layer());
    }
    float calc_tower_feedrate(int filament_id, float extrusion_flow, bool first_layer) const {
        if (first_layer)
            return std::min(m_first_layer_speed * 60.f, m_max_speed);
        if (m_handle_hotend_as_extruder) {
            float feedrate = 60.0f * m_filpar[filament_id].max_e_speed / extrusion_flow;
            feedrate *= m_filpar[filament_id].feedrate_factor;
            return std::min(feedrate, m_machine_max_speed);
        }
        return std::min(60.0f * m_filpar[filament_id].max_e_speed / extrusion_flow, m_max_speed);
    }
    bool                       is_valid_last_layer(int tool, int layer_id, double layer_z) const;
    bool                       m_flat_ironing=false;
    bool                       m_line_ironing = true;
    float                      m_ironing_speed = 10.f * 60.f;
    bool                       m_enable_side_scrape = false;
    // Which pair of tower walls the scrape runs on: the left/right pair, or else the front/back pair.
    // Resolved from prime_tower_side_scrape_face in the constructor so this header does not need the
    // enum, which lives behind the forward declared PrintConfig.
    bool                       m_side_scrape_on_left_right = false;
    // The scrape descends outside the outer wall, so the wall must not step inward with height: any
    // wider layer below would be in the way. Keeping every layer at the full tower depth makes all
    // walls sit at the same place, which leaves the brim chamfer at the tower base as the only part
    // that reaches further out. Same idea as smooth timelapse forcing a full-depth tower on every layer.
    bool keeps_outer_walls_unshrunk() const { return m_enable_side_scrape; }
    float                      m_side_scrape_length = 2.f;   // mm, horizontal distance moved inward
    float                      m_side_scrape_height = 1.5f;  // mm, Z lift height
    float                      m_side_scrape_speed  = 1400.f * 60.f; // mm/min
    float                      m_side_scrape_wall_gap = 0.2f; // mm, gap kept outside the outer wall
    float                      m_side_scrape_point_spacing = 1.f; // mm, target gap between scrape start points
    float                      m_side_scrape_along_length  = 2.f; // mm, empty travel along the wall after the scrape
    // Increments on every side scrape attempt; used to cycle start points across both outer walls.
    unsigned int               m_side_scrape_seq = 0;
    float                      m_machine_max_speed = 500.f * 60.f;
    bool                       m_contact_ironing = false;
    bool                       m_has_filament_switcher = false;
    float                      m_contact_speed   = 20 * 60.f;
    bool                       m_handle_hotend_as_extruder = false;
    std::vector<int>           m_physical_extruder_map;
	// Calculates length of extrusion line to extrude given volume
	float volume_to_length(float volume, float line_width, float layer_height) const {
		return std::max(0.f, volume / (layer_height * (line_width - layer_height * (1.f - float(M_PI) / 4.f))));
	}
    // Calculates volume of extrusion line
    float length_to_volume(float length,float line_width, float layer_height) const
    {
        return std::max(0.f, length * (layer_height * (line_width - layer_height * (1.f - float(M_PI) / 4.f))));
    }
	// Calculates depth for all layers and propagates them downwards
	void plan_tower();

	// Goes through m_plan and recalculates depths and width of the WT to make it exactly square - experimental
	void make_wipe_tower_square();

	Vec2f get_next_pos(const WipeTower::box_coordinates &cleaning_box, float wipe_length, bool solid_toolchange);

    // Goes through m_plan, calculates border and finish_layer extrusions and subtracts them from last wipe
    void save_on_last_wipe();

	bool is_tpu_filament(int filament_id) const;
	bool is_petg_filament(int filament_id) const;
    bool is_need_reverse_travel(int filament, bool extruder_change) const;
	// BBS
	box_coordinates align_perimeter(const box_coordinates& perimeter_box);

    void set_for_wipe_tower_writer(WipeTowerWriter &writer);

    // to store information about tool changes for a given layer
	struct WipeTowerInfo{
		struct ToolChange {
            size_t old_tool;
            size_t new_tool;
			float required_depth;
            float ramming_depth;
            float first_wipe_line;
            float wipe_volume;
			float wipe_length;
            float nozzle_change_depth{0};
            float nozzle_change_length{0};
			// BBS
			float purge_volume;
            ToolChange(size_t old, size_t newtool, float depth=0.f, float ramming_depth=0.f, float fwl=0.f, float wv=0.f, float wl = 0, float pv = 0)
				: old_tool{ old }, new_tool{ newtool }, required_depth{ depth }, ramming_depth{ ramming_depth }, first_wipe_line{ fwl }, wipe_volume{ wv }, wipe_length{ wl }, purge_volume{ pv } {}
		};
		float z;		// z position of the layer
		float height;	// layer height
		float depth;	// depth of the layer based on all layers above
		float extra_spacing;
        bool  extruder_fill{true};
		float toolchanges_depth() const { float sum = 0.f; for (const auto &a : tool_changes) sum += a.required_depth; return sum; }

		std::vector<ToolChange> tool_changes;

		WipeTowerInfo(float z_par, float layer_height_par)
			: z{z_par}, height{layer_height_par}, depth{0}, extra_spacing{1.f} {}
	};

	std::vector<WipeTowerInfo> m_plan; 	// Stores information about all layers and toolchanges for the future wipe tower (filled by plan_toolchange(...))
	std::vector<WipeTowerInfo>::iterator m_layer_info = m_plan.end();

    // Stores information about used filament length per extruder:
    std::vector<float> m_used_filament_length;

    // BBS: consider both soluable and support properties
    // Return index of first toolchange that switches to non-soluble extruder
    // ot -1 if there is no such toolchange.
    int first_toolchange_to_nonsoluble_nonsupport(
            const std::vector<WipeTowerInfo::ToolChange>& tool_changes) const;

    int get_wall_filament_for_layer(const WipeTowerInfo &layer, int layer_id, int wall_filament) const;
    // A filament this layer has on hand that may top the band up, or -1: the band's own group first, then any
    // filament of its category. A band must only rest on material it bonds to.
    int same_category_filler(const WipeTowerInfo &layer, int block_key) const;
    // Filament that tops a band up on a layer, chosen without regard to the slab height. The Z ladder is
    // planned against it, so that the height and the nozzle that has to extrude it are decided together.
    // Y depth this band must fill on a layer. Several bands keep fixed slots sized by their deepest
    // layer and the outer wall stays at that full depth, so a shallower layer still fills the rest of
    // the slot. A single band whose wall shrinks with the layer only fills its own footprint.
    float band_fill_depth(const WipeTowerBlock &block, int layer_id) const;
    int block_fill_filament_base(const WipeTowerInfo &layer, int layer_id, const WipeTowerBlock &block, int wall_filament) const;
    int get_block_fill_filament(const WipeTowerInfo &layer, int layer_id, const WipeTowerBlock &block, int wall_filament) const;
    WipeTowerInfo::ToolChange set_toolchange(int old_tool, int new_tool, float layer_height, float wipe_volume, float purge_volume,int layer_id);
	void toolchange_Unload(
		WipeTowerWriter &writer,
		const box_coordinates  &cleaning_box,
		const std::string&	 	current_material,
		const int 				new_temperature);

	void toolchange_Change(
		WipeTowerWriter &writer,
        const size_t		new_tool,
		const std::string& 		new_material);

	void toolchange_Load(
		WipeTowerWriter &writer,
		const box_coordinates  &cleaning_box);

	void toolchange_Wipe(
		WipeTowerWriter &writer,
		const box_coordinates  &cleaning_box,
		float wipe_volume);
    // Shared Y pitch of a purge zigzag. Its start position and emitted rows must use the same value.
    float wipe_line_spacing(bool solid_toolchange);
    void get_wall_skip_points(const WipeTowerInfo &layer, int layer_id, int wall_filament);
    void get_all_wall_skip_points(int wall_filament);
    ToolChangeResult merge_tcr(ToolChangeResult &first, ToolChangeResult &second);
    float            get_block_gap_width(int tool, bool is_nozzlechangle = false);
};


// Compaction rule for wipe_tower_no_sparse_layers. Shared by the G-code emitter and by the
// clearance validator so that both agree on where the compacted tower actually sits; a drift
// between the two would either let a real nozzle collision through or reject a safe plate.

// Whether sparse layers are really skipped, i.e. whether the tower is compacted at all. Smooth
// timelapse and wrapping detection put a tower on every layer, so no layer is ever dropped and the
// tower keeps following the object even though the option is on. Tower planning, G-code emission and
// the clearance validator all ask this single question, so none of them can compact on its own.
bool wipe_tower_sparse_layers_skipped(const PrintConfig &config);

// A planned layer prints no tower at all when its only toolchange keeps the same filament.
bool wipe_tower_layer_is_sparse(const std::vector<WipeTower::ToolChangeResult> &layer_tool_changes);

// How far the diagonal side scrape reaches outside the prime tower's outer wall, per side. The scrape
// is a travel move so it deposits nothing, but the nozzle does enter that ring, which means every
// clearance check has to reserve it on top of the printed footprint. Zero when the scrape is off.
// This is the unclamped upper bound: toolchange_side_scrape() shortens the diagonal on a narrow tower
// and skips it where the wall leaves no room, both of which only shrink the real excursion.
double prime_tower_side_scrape_margin(const PrintConfig &config);

// Print z the compacted tower reaches on every planned layer. Sparse layers carry over the
// previous value, so the tower falls one layer height behind the object for each of them.
std::vector<float> compute_compacted_wipe_tower_z(const std::vector<std::vector<WipeTower::ToolChangeResult>> &tool_changes);


} // namespace Slic3r

#endif // WipeTowerPrusaMM_hpp_
