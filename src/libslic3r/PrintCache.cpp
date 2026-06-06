#include "Exception.hpp"
#include "Print.hpp"
#include "BoundingBox.hpp"

#include <boost/filesystem/path.hpp>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#include "format.hpp"
#include "nlohmann/json.hpp"

#include <iomanip>

using namespace nlohmann;

namespace Slic3r {

/*add json export/import related functions */
#define JSON_POLYGON_CONTOUR                "contour"
#define JSON_POLYGON_HOLES                  "holes"
#define JSON_POINTS                 "points"
#define JSON_EXPOLYGON              "expolygon"
#define JSON_ARC_FITTING            "arc_fitting"
#define JSON_OBJECT_NAME            "name"
#define JSON_IDENTIFY_ID          "identify_id"


#define JSON_LAYERS                  "layers"
#define JSON_SUPPORT_LAYERS                  "support_layers"
#define JSON_TREE_SUPPORT_LAYERS                  "tree_support_layers"
#define JSON_LAYER_REGIONS                  "layer_regions"
#define JSON_FIRSTLAYER_GROUPS                  "first_layer_groups"

#define JSON_FIRSTLAYER_GROUP_ID                  "group_id"
#define JSON_FIRSTLAYER_GROUP_VOLUME_IDS          "volume_ids"
#define JSON_FIRSTLAYER_GROUP_SLICES               "slices"

#define JSON_LAYER_PRINT_Z            "print_z"
#define JSON_LAYER_SLICE_Z            "slice_z"
#define JSON_LAYER_HEIGHT             "height"
#define JSON_LAYER_ID                  "layer_id"
#define JSON_LAYER_SLICED_POLYGONS    "sliced_polygons"
#define JSON_LAYER_SLLICED_BBOXES      "sliced_bboxes"
#define JSON_LAYER_OVERHANG_POLYGONS    "overhang_polygons"
#define JSON_LAYER_OVERHANG_BBOX       "overhang_bbox"

#define JSON_SUPPORT_LAYER_ISLANDS                  "support_islands"
#define JSON_SUPPORT_LAYER_FILLS                    "support_fills"
#define JSON_SUPPORT_LAYER_INTERFACE_ID             "interface_id"

#define JSON_LAYER_REGION_CONFIG_HASH             "config_hash"
#define JSON_LAYER_REGION_SLICES                  "slices"
#define JSON_LAYER_REGION_RAW_SLICES              "raw_slices"
//#define JSON_LAYER_REGION_ENTITIES                "entities"
#define JSON_LAYER_REGION_THIN_FILLS                  "thin_fills"
#define JSON_LAYER_REGION_FILL_EXPOLYGONS             "fill_expolygons"
#define JSON_LAYER_REGION_FILL_SURFACES               "fill_surfaces"
#define JSON_LAYER_REGION_FILL_NO_OVERLAP             "fill_no_overlap_expolygons"
#define JSON_LAYER_REGION_UNSUPPORTED_BRIDGE_EDGES    "unsupported_bridge_edges"
#define JSON_LAYER_REGION_PERIMETERS                  "perimeters"
#define JSON_LAYER_REGION_FILLS                  "fills"



#define JSON_SURF_TYPE              "surface_type"
#define JSON_SURF_THICKNESS         "thickness"
#define JSON_SURF_THICKNESS_LAYER   "thickness_layers"
#define JSON_SURF_BRIDGE_ANGLE       "bridge_angle"
#define JSON_SURF_EXTRA_PERIMETERS   "extra_perimeters"

#define JSON_ARC_DATA                "arc_data"
#define JSON_ARC_START_INDEX         "start_index"
#define JSON_ARC_END_INDEX           "end_index"
#define JSON_ARC_PATH_TYPE           "path_type"

#define JSON_IS_ARC                  "is_arc"
#define JSON_ARC_LENGTH              "length"
#define JSON_ARC_ANGLE_RADIUS        "angle_radians"
#define JSON_ARC_POLAY_START_THETA   "polar_start_theta"
#define JSON_ARC_POLAY_END_THETA     "polar_end_theta"
#define JSON_ARC_START_POINT          "start_point"
#define JSON_ARC_END_POINT            "end_point"
#define JSON_ARC_DIRECTION            "direction"
#define JSON_ARC_RADIUS               "radius"
#define JSON_ARC_CENTER               "center"

//extrusions
#define JSON_EXTRUSION_ENTITY_TYPE             "entity_type"
#define JSON_EXTRUSION_NO_SORT                 "no_sort"
#define JSON_EXTRUSION_PATHS                   "paths"
#define JSON_EXTRUSION_ENTITIES                "entities"
#define JSON_EXTRUSION_TYPE_PATH               "path"
#define JSON_EXTRUSION_TYPE_MULTIPATH          "multipath"
#define JSON_EXTRUSION_TYPE_LOOP               "loop"
#define JSON_EXTRUSION_TYPE_COLLECTION         "collection"
#define JSON_EXTRUSION_POLYLINE                "polyline"
#define JSON_EXTRUSION_OVERHANG_DEGREE         "overhang_degree"
#define JSON_EXTRUSION_CURVE_DEGREE            "curve_degree"
#define JSON_EXTRUSION_MM3_PER_MM              "mm3_per_mm"
#define JSON_EXTRUSION_WIDTH                   "width"
#define JSON_EXTRUSION_HEIGHT                  "height"
#define JSON_EXTRUSION_ROLE                    "role"
#define JSON_EXTRUSION_NO_EXTRUSION            "no_extrusion"
#define JSON_EXTRUSION_LOOP_ROLE               "loop_role"


static void to_json(json& j, const Points& p_s) {
    for (const Point& p : p_s)
    {
        j.push_back(p.x());
        j.push_back(p.y());
    }
}

static void to_json(json& j, const BoundingBox& bb) {
    j.push_back(bb.min.x());
    j.push_back(bb.min.y());
    j.push_back(bb.max.x());
    j.push_back(bb.max.y());
}

static void to_json(json& j, const ExPolygon& polygon) {
    json contour_json = json::array(), holes_json = json::array();

    //contour
    const Polygon& slice_contour =   polygon.contour;
    contour_json = slice_contour.points;
    j[JSON_POLYGON_CONTOUR] = std::move(contour_json);

    //holes
    const Polygons& slice_holes =   polygon.holes;
    for (const Polygon& hole_polyon : slice_holes)
    {
        json hole_json = json::array();
        hole_json =  hole_polyon.points;
        holes_json.push_back(std::move(hole_json));
    }
    j[JSON_POLYGON_HOLES] = std::move(holes_json);
}

static void to_json(json& j, const Surface& surf) {
    j[JSON_EXPOLYGON] = surf.expolygon;
    j[JSON_SURF_TYPE] = surf.surface_type;
    j[JSON_SURF_THICKNESS] = surf.thickness;
    j[JSON_SURF_THICKNESS_LAYER] = surf.thickness_layers;
    j[JSON_SURF_BRIDGE_ANGLE] = surf.bridge_angle;
    j[JSON_SURF_EXTRA_PERIMETERS] = surf.extra_perimeters;
}

static void to_json(json& j, const ArcSegment& arc_seg) {
    json start_point_json = json::array(), end_point_json = json::array(), center_point_json = json::array();
    j[JSON_IS_ARC] = arc_seg.is_arc;
    j[JSON_ARC_LENGTH] = arc_seg.length;
    j[JSON_ARC_ANGLE_RADIUS] = arc_seg.angle_radians;
    j[JSON_ARC_POLAY_START_THETA] = arc_seg.polar_start_theta;
    j[JSON_ARC_POLAY_END_THETA] = arc_seg.polar_end_theta;
    start_point_json.push_back(arc_seg.start_point.x());
    start_point_json.push_back(arc_seg.start_point.y());
    j[JSON_ARC_START_POINT] = std::move(start_point_json);
    end_point_json.push_back(arc_seg.end_point.x());
    end_point_json.push_back(arc_seg.end_point.y());
    j[JSON_ARC_END_POINT] = std::move(end_point_json);
    j[JSON_ARC_DIRECTION] = arc_seg.direction;
    j[JSON_ARC_RADIUS] = arc_seg.radius;
    center_point_json.push_back(arc_seg.center.x());
    center_point_json.push_back(arc_seg.center.y());
    j[JSON_ARC_CENTER] = std::move(center_point_json);
}


static void to_json(json& j, const Polyline& poly_line) {
    json points_json = json::array(), fittings_json = json::array();
    points_json = poly_line.points;

    j[JSON_POINTS] = std::move(points_json);
    for (const PathFittingData& path_fitting : poly_line.fitting_result)
    {
        json fitting_json;
        fitting_json[JSON_ARC_START_INDEX] = path_fitting.start_point_index;
        fitting_json[JSON_ARC_END_INDEX] = path_fitting.end_point_index;
        fitting_json[JSON_ARC_PATH_TYPE] = path_fitting.path_type;
        if (path_fitting.arc_data.is_arc)
            fitting_json[JSON_ARC_DATA] = path_fitting.arc_data;

        fittings_json.push_back(std::move(fitting_json));
    }
    j[JSON_ARC_FITTING] = fittings_json;
}

static void to_json(json& j, const ExtrusionPath& extrusion_path) {
    j[JSON_EXTRUSION_POLYLINE] = extrusion_path.polyline;
    j[JSON_EXTRUSION_OVERHANG_DEGREE] = extrusion_path.overhang_degree;
    j[JSON_EXTRUSION_CURVE_DEGREE] = extrusion_path.curve_degree;
    j[JSON_EXTRUSION_MM3_PER_MM] = extrusion_path.mm3_per_mm;
    j[JSON_EXTRUSION_WIDTH] = extrusion_path.width;
    j[JSON_EXTRUSION_HEIGHT] = extrusion_path.height;
    j[JSON_EXTRUSION_ROLE] = extrusion_path.role();
    j[JSON_EXTRUSION_NO_EXTRUSION] = extrusion_path.is_force_no_extrusion();
}

static bool convert_extrusion_to_json(json& entity_json, json& entity_paths_json, const ExtrusionEntity* extrusion_entity) {
    std::string path_type;
    const ExtrusionPath* path = NULL;
    const ExtrusionMultiPath* multipath = NULL;
    const ExtrusionLoop* loop = NULL;
    const ExtrusionEntityCollection* collection = dynamic_cast<const ExtrusionEntityCollection*>(extrusion_entity);

    if (!collection)
        path = dynamic_cast<const ExtrusionPath*>(extrusion_entity);

    if (!collection && !path)
        multipath = dynamic_cast<const ExtrusionMultiPath*>(extrusion_entity);

    if (!collection && !path && !multipath)
        loop = dynamic_cast<const ExtrusionLoop*>(extrusion_entity);

    path_type = path?JSON_EXTRUSION_TYPE_PATH:(multipath?JSON_EXTRUSION_TYPE_MULTIPATH:(loop?JSON_EXTRUSION_TYPE_LOOP:JSON_EXTRUSION_TYPE_COLLECTION));
    if (path_type.empty()) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(":invalid extrusion path type Found");
        return false;
    }

    entity_json[JSON_EXTRUSION_ENTITY_TYPE] = path_type;

    if (path) {
        json entity_path_json = *path;
        entity_paths_json.push_back(std::move(entity_path_json));
    }
    else if (multipath) {
        for (const ExtrusionPath& extrusion_path : multipath->paths)
        {
            json entity_path_json = extrusion_path;
            entity_paths_json.push_back(std::move(entity_path_json));
        }
    }
    else if (loop) {
        entity_json[JSON_EXTRUSION_LOOP_ROLE] = loop->loop_role();
        for (const ExtrusionPath& extrusion_path : loop->paths)
        {
            json entity_path_json = extrusion_path;
            entity_paths_json.push_back(std::move(entity_path_json));
        }
    }
    else {
        //recursive collections
        entity_json[JSON_EXTRUSION_NO_SORT] = collection->no_sort;
        for (const ExtrusionEntity* recursive_extrusion_entity : collection->entities) {
            json recursive_entity_json, recursive_entity_paths_json = json::array();
            bool ret = convert_extrusion_to_json(recursive_entity_json, recursive_entity_paths_json, recursive_extrusion_entity);
            if (!ret) {
                continue;
            }
            entity_paths_json.push_back(std::move(recursive_entity_json));
        }
    }

    if (collection)
        entity_json[JSON_EXTRUSION_ENTITIES] = std::move(entity_paths_json);
    else
        entity_json[JSON_EXTRUSION_PATHS] = std::move(entity_paths_json);
    return true;
}

static void to_json(json& j, const LayerRegion& layer_region) {
    json unsupported_bridge_edges_json = json::array(), slices_surfaces_json = json::array(), raw_slices_json = json::array(), thin_fills_json, thin_fill_entities_json = json::array();
    json fill_expolygons_json = json::array(), fill_no_overlap_expolygons_json = json::array(), fill_surfaces_json = json::array(), perimeters_json, perimeter_entities_json = json::array(), fills_json, fill_entities_json = json::array();

    j[JSON_LAYER_REGION_CONFIG_HASH] = layer_region.region().config_hash();
    //slices
    for (const Surface& slice_surface : layer_region.slices.surfaces) {
        json surface_json = slice_surface;
        slices_surfaces_json.push_back(std::move(surface_json));
    }
    j.push_back({JSON_LAYER_REGION_SLICES, std::move(slices_surfaces_json)});

    //raw_slices
    for (const ExPolygon& raw_slice_explogyon : layer_region.raw_slices) {
        json raw_polygon_json = raw_slice_explogyon;

        raw_slices_json.push_back(std::move(raw_polygon_json));
    }
    j.push_back({JSON_LAYER_REGION_RAW_SLICES, std::move(raw_slices_json)});

    //thin fills
    thin_fills_json[JSON_EXTRUSION_NO_SORT] = layer_region.thin_fills.no_sort;
    thin_fills_json[JSON_EXTRUSION_ENTITY_TYPE] = JSON_EXTRUSION_TYPE_COLLECTION;
    for (const ExtrusionEntity* extrusion_entity : layer_region.thin_fills.entities) {
        json thinfills_entity_json, thinfill_entity_paths_json = json::array();
        bool ret = convert_extrusion_to_json(thinfills_entity_json, thinfill_entity_paths_json, extrusion_entity);
        if (!ret) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(":error found at print_z %1%") % layer_region.layer()->print_z;
            continue;
        }

        thin_fill_entities_json.push_back(std::move(thinfills_entity_json));
    }
    thin_fills_json[JSON_EXTRUSION_ENTITIES] = std::move(thin_fill_entities_json);
    j.push_back({JSON_LAYER_REGION_THIN_FILLS, std::move(thin_fills_json)});

    //fill_expolygons
    for (const ExPolygon& fill_expolygon : layer_region.fill_expolygons) {
        json fill_expolygon_json = fill_expolygon;

        fill_expolygons_json.push_back(std::move(fill_expolygon_json));
    }
    j.push_back({JSON_LAYER_REGION_FILL_EXPOLYGONS, std::move(fill_expolygons_json)});

    //fill_surfaces
    for (const Surface& fill_surface : layer_region.fill_surfaces.surfaces) {
        json surface_json = fill_surface;
        fill_surfaces_json.push_back(std::move(surface_json));
    }
    j.push_back({JSON_LAYER_REGION_FILL_SURFACES, std::move(fill_surfaces_json)});

    //fill_no_overlap_expolygons
    for (const ExPolygon& fill_no_overlap_expolygon : layer_region.fill_no_overlap_expolygons) {
        json fill_no_overlap_expolygon_json = fill_no_overlap_expolygon;

        fill_no_overlap_expolygons_json.push_back(std::move(fill_no_overlap_expolygon_json));
    }
    j.push_back({JSON_LAYER_REGION_FILL_NO_OVERLAP, std::move(fill_no_overlap_expolygons_json)});

    //unsupported_bridge_edges
    for (const Polyline& poly_line : layer_region.unsupported_bridge_edges)
    {
        json polyline_json = poly_line;

        unsupported_bridge_edges_json.push_back(std::move(polyline_json));
    }
    j.push_back({JSON_LAYER_REGION_UNSUPPORTED_BRIDGE_EDGES, std::move(unsupported_bridge_edges_json)});

    //perimeters
    perimeters_json[JSON_EXTRUSION_NO_SORT] = layer_region.perimeters.no_sort;
    perimeters_json[JSON_EXTRUSION_ENTITY_TYPE] = JSON_EXTRUSION_TYPE_COLLECTION;
    for (const ExtrusionEntity* extrusion_entity : layer_region.perimeters.entities) {
        json perimeters_entity_json, perimeters_entity_paths_json = json::array();
        bool ret = convert_extrusion_to_json(perimeters_entity_json, perimeters_entity_paths_json, extrusion_entity);
        if (!ret)
            continue;

        perimeter_entities_json.push_back(std::move(perimeters_entity_json));
    }
    perimeters_json[JSON_EXTRUSION_ENTITIES] = std::move(perimeter_entities_json);
    j.push_back({JSON_LAYER_REGION_PERIMETERS, std::move(perimeters_json)});

    //fills
    fills_json[JSON_EXTRUSION_NO_SORT] = layer_region.fills.no_sort;
    fills_json[JSON_EXTRUSION_ENTITY_TYPE] = JSON_EXTRUSION_TYPE_COLLECTION;
    for (const ExtrusionEntity* extrusion_entity : layer_region.fills.entities) {
        json fill_entity_json, fill_entity_paths_json = json::array();
        bool ret = convert_extrusion_to_json(fill_entity_json, fill_entity_paths_json, extrusion_entity);
        if (!ret)
            continue;

        fill_entities_json.push_back(std::move(fill_entity_json));
    }
    fills_json[JSON_EXTRUSION_ENTITIES] = std::move(fill_entities_json);
    j.push_back({JSON_LAYER_REGION_FILLS, std::move(fills_json)});

    return;
}

static void to_json(json& j, const groupedVolumeSlices& first_layer_group) {
    json volumes_json = json::array(), slices_json = json::array();
    j[JSON_FIRSTLAYER_GROUP_ID] = first_layer_group.groupId;

    for (const ObjectID& obj_id : first_layer_group.volume_ids)
    {
        volumes_json.push_back(obj_id.id);
    }
    j[JSON_FIRSTLAYER_GROUP_VOLUME_IDS] = std::move(volumes_json);

    for (const ExPolygon& slice_expolygon : first_layer_group.slices) {
        json slice_expolygon_json = slice_expolygon;

        slices_json.push_back(std::move(slice_expolygon_json));
    }
    j[JSON_FIRSTLAYER_GROUP_SLICES] = std::move(slices_json);
}

//load apis from json
static void from_json(const json& j, Points& p_s) {
    int array_size = j.size();
    for (int index = 0; index < array_size/2; index++)
    {
        coord_t x = j[2*index], y = j[2*index+1];
        Point p(x, y);
        p_s.push_back(std::move(p));
    }
    return;
}

static void from_json(const json& j, BoundingBox& bbox) {
    bbox.min[0] = j[0];
    bbox.min[1] = j[1];
    bbox.max[0] = j[2];
    bbox.max[1] = j[3];
    bbox.defined = true;

    return;
}

static void from_json(const json& j, ExPolygon& polygon) {
    polygon.contour.points = j[JSON_POLYGON_CONTOUR];

    int holes_count = j[JSON_POLYGON_HOLES].size();
    for (int holes_index = 0; holes_index < holes_count; holes_index++)
    {
        Polygon poly;

        poly.points = j[JSON_POLYGON_HOLES][holes_index];
        polygon.holes.push_back(std::move(poly));
    }
    return;
}

static void from_json(const json& j, Surface& surf) {
    surf.expolygon = j[JSON_EXPOLYGON];
    surf.surface_type = j[JSON_SURF_TYPE];
    surf.thickness = j[JSON_SURF_THICKNESS];
    surf.thickness_layers = j[JSON_SURF_THICKNESS_LAYER];
    surf.bridge_angle = j[JSON_SURF_BRIDGE_ANGLE];
    surf.extra_perimeters = j[JSON_SURF_EXTRA_PERIMETERS];

    return;
}

static void from_json(const json& j, ArcSegment& arc_seg) {
    arc_seg.is_arc = j[JSON_IS_ARC];
    arc_seg.length = j[JSON_ARC_LENGTH];
    arc_seg.angle_radians = j[JSON_ARC_ANGLE_RADIUS];
    arc_seg.polar_start_theta = j[JSON_ARC_POLAY_START_THETA];
    arc_seg.polar_end_theta = j[JSON_ARC_POLAY_END_THETA];
    arc_seg.start_point.x() = j[JSON_ARC_START_POINT][0];
    arc_seg.start_point.y() = j[JSON_ARC_START_POINT][1];
    arc_seg.end_point.x() = j[JSON_ARC_END_POINT][0];
    arc_seg.end_point.y() = j[JSON_ARC_END_POINT][1];
    arc_seg.direction = j[JSON_ARC_DIRECTION];
    arc_seg.radius    = j[JSON_ARC_RADIUS];
    arc_seg.center.x() = j[JSON_ARC_CENTER][0];
    arc_seg.center.y() = j[JSON_ARC_CENTER][1];

    return;
}


static void from_json(const json& j, Polyline& poly_line) {
    poly_line.points = j[JSON_POINTS];

    int arc_fitting_count = j[JSON_ARC_FITTING].size();
    for (int arc_fitting_index = 0; arc_fitting_index < arc_fitting_count; arc_fitting_index++)
    {
        const json& fitting_json = j[JSON_ARC_FITTING][arc_fitting_index];
        PathFittingData path_fitting;
        path_fitting.start_point_index = fitting_json[JSON_ARC_START_INDEX];
        path_fitting.end_point_index = fitting_json[JSON_ARC_END_INDEX];
        path_fitting.path_type = fitting_json[JSON_ARC_PATH_TYPE];

        if (fitting_json.contains(JSON_ARC_DATA)) {
            path_fitting.arc_data = fitting_json[JSON_ARC_DATA];
        }

        poly_line.fitting_result.push_back(std::move(path_fitting));
    }
    return;
}

static void from_json(const json& j, ExtrusionPath& extrusion_path) {
    extrusion_path.polyline               =    j[JSON_EXTRUSION_POLYLINE];
    extrusion_path.overhang_degree        =    j[JSON_EXTRUSION_OVERHANG_DEGREE];
    extrusion_path.curve_degree           =    j[JSON_EXTRUSION_CURVE_DEGREE];
    extrusion_path.mm3_per_mm             =    j[JSON_EXTRUSION_MM3_PER_MM];
    extrusion_path.width                  =    j[JSON_EXTRUSION_WIDTH];
    extrusion_path.height                 =    j[JSON_EXTRUSION_HEIGHT];
    extrusion_path.set_extrusion_role(j[JSON_EXTRUSION_ROLE]);
    extrusion_path.set_force_no_extrusion(j[JSON_EXTRUSION_NO_EXTRUSION]);
}

static bool convert_extrusion_from_json(const json& entity_json, ExtrusionEntityCollection& entity_collection) {
    std::string path_type = entity_json[JSON_EXTRUSION_ENTITY_TYPE];
    bool ret = false;

    if (path_type == JSON_EXTRUSION_TYPE_PATH) {
        ExtrusionPath* path = new ExtrusionPath();
        if (!path) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": oom when new ExtrusionPath");
            return false;
        }
        *path = entity_json[JSON_EXTRUSION_PATHS][0];
        entity_collection.entities.push_back(path);
    }
    else if (path_type == JSON_EXTRUSION_TYPE_MULTIPATH) {
        ExtrusionMultiPath* multipath = new ExtrusionMultiPath();
        if (!multipath) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": oom when new ExtrusionMultiPath");
            return false;
        }
        int paths_count = entity_json[JSON_EXTRUSION_PATHS].size();
        for (int path_index = 0; path_index < paths_count; path_index++)
        {
            ExtrusionPath path;
            path = entity_json[JSON_EXTRUSION_PATHS][path_index];
            multipath->paths.push_back(std::move(path));
        }
        entity_collection.entities.push_back(multipath);
    }
    else if (path_type == JSON_EXTRUSION_TYPE_LOOP) {
        ExtrusionLoop* loop = new ExtrusionLoop();
        if (!loop) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": oom when new ExtrusionLoop");
            return false;
        }
        loop->set_loop_role(entity_json[JSON_EXTRUSION_LOOP_ROLE]);
        int paths_count = entity_json[JSON_EXTRUSION_PATHS].size();
        for (int path_index = 0; path_index < paths_count; path_index++)
        {
            ExtrusionPath path;
            path = entity_json[JSON_EXTRUSION_PATHS][path_index];
            loop->paths.push_back(std::move(path));
        }
        entity_collection.entities.push_back(loop);
    }
    else if (path_type == JSON_EXTRUSION_TYPE_COLLECTION) {
        ExtrusionEntityCollection* collection = new ExtrusionEntityCollection();
        if (!collection) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": oom when new ExtrusionEntityCollection");
            return false;
        }
        collection->no_sort = entity_json[JSON_EXTRUSION_NO_SORT];
        int entities_count = entity_json[JSON_EXTRUSION_ENTITIES].size();
        for (int entity_index = 0; entity_index < entities_count; entity_index++)
        {
            const json& entity_item_json = entity_json[JSON_EXTRUSION_ENTITIES][entity_index];
            ret = convert_extrusion_from_json(entity_item_json, *collection);
            if (!ret) {
                BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": convert_extrusion_from_json failed");
                return false;
            }
        }
        entity_collection.entities.push_back(collection);
    }
    else {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": unknown path type %1%")%path_type;
        return false;
    }

    return true;
}

static void convert_layer_region_from_json(const json& j, LayerRegion& layer_region) {
    //slices
    int slices_count = j[JSON_LAYER_REGION_SLICES].size();
    for (int slices_index = 0; slices_index < slices_count; slices_index++)
    {
        Surface surface;

        surface = j[JSON_LAYER_REGION_SLICES][slices_index];
        layer_region.slices.surfaces.push_back(std::move(surface));
    }

    //raw_slices
    int raw_slices_count = j[JSON_LAYER_REGION_RAW_SLICES].size();
    for (int raw_slices_index = 0; raw_slices_index < raw_slices_count; raw_slices_index++)
    {
        ExPolygon polygon;

        polygon = j[JSON_LAYER_REGION_RAW_SLICES][raw_slices_index];
        layer_region.raw_slices.push_back(std::move(polygon));
    }

    //thin fills
    layer_region.thin_fills.no_sort = j[JSON_LAYER_REGION_THIN_FILLS][JSON_EXTRUSION_NO_SORT];
    int thinfills_entities_count = j[JSON_LAYER_REGION_THIN_FILLS][JSON_EXTRUSION_ENTITIES].size();
    for (int thinfills_entities_index = 0; thinfills_entities_index < thinfills_entities_count; thinfills_entities_index++)
    {
        const json& extrusion_entity_json =  j[JSON_LAYER_REGION_THIN_FILLS][JSON_EXTRUSION_ENTITIES][thinfills_entities_index];
        bool ret = convert_extrusion_from_json(extrusion_entity_json, layer_region.thin_fills);
        if (!ret) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(":error parsing thin_fills found at layer %1%, print_z %2%") %layer_region.layer()->id() %layer_region.layer()->print_z;
            char error_buf[1024];
            ::sprintf(error_buf, "Error while parsing thin_fills at layer %zu, print_z %f", layer_region.layer()->id(), layer_region.layer()->print_z);
            throw Slic3r::FileIOError(error_buf);
        }
    }

    //fill_expolygons
    int fill_expolygons_count = j[JSON_LAYER_REGION_FILL_EXPOLYGONS].size();
    for (int fill_expolygons_index = 0; fill_expolygons_index < fill_expolygons_count; fill_expolygons_index++)
    {
        ExPolygon polygon;

        polygon = j[JSON_LAYER_REGION_FILL_EXPOLYGONS][fill_expolygons_index];
        layer_region.fill_expolygons.push_back(std::move(polygon));
    }

    //fill_surfaces
    int fill_surfaces_count = j[JSON_LAYER_REGION_FILL_SURFACES].size();
    for (int fill_surfaces_index = 0; fill_surfaces_index < fill_surfaces_count; fill_surfaces_index++)
    {
        Surface surface;

        surface = j[JSON_LAYER_REGION_FILL_SURFACES][fill_surfaces_index];
        layer_region.fill_surfaces.surfaces.push_back(std::move(surface));
    }

    //fill_no_overlap_expolygons
    int fill_no_overlap_expolygons_count = j[JSON_LAYER_REGION_FILL_NO_OVERLAP].size();
    for (int fill_no_overlap_expolygons_index = 0; fill_no_overlap_expolygons_index < fill_no_overlap_expolygons_count; fill_no_overlap_expolygons_index++)
    {
        ExPolygon polygon;

        polygon = j[JSON_LAYER_REGION_FILL_NO_OVERLAP][fill_no_overlap_expolygons_index];
        layer_region.fill_no_overlap_expolygons.push_back(std::move(polygon));
    }

    //unsupported_bridge_edges
    int unsupported_bridge_edges_count = j[JSON_LAYER_REGION_UNSUPPORTED_BRIDGE_EDGES].size();
    for (int unsupported_bridge_edges_index = 0; unsupported_bridge_edges_index < unsupported_bridge_edges_count; unsupported_bridge_edges_index++)
    {
        Polyline polyline;

        polyline = j[JSON_LAYER_REGION_UNSUPPORTED_BRIDGE_EDGES][unsupported_bridge_edges_index];
        layer_region.unsupported_bridge_edges.push_back(std::move(polyline));
    }

    //perimeters
    layer_region.perimeters.no_sort = j[JSON_LAYER_REGION_PERIMETERS][JSON_EXTRUSION_NO_SORT];
    int perimeters_entities_count = j[JSON_LAYER_REGION_PERIMETERS][JSON_EXTRUSION_ENTITIES].size();
    for (int perimeters_entities_index = 0; perimeters_entities_index < perimeters_entities_count; perimeters_entities_index++)
    {
        const json& extrusion_entity_json =  j[JSON_LAYER_REGION_PERIMETERS][JSON_EXTRUSION_ENTITIES][perimeters_entities_index];
        bool ret = convert_extrusion_from_json(extrusion_entity_json, layer_region.perimeters);
        if (!ret) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": error parsing perimeters found at layer %1%, print_z %2%") %layer_region.layer()->id() %layer_region.layer()->print_z;
            char error_buf[1024];
            ::sprintf(error_buf, "Error while parsing perimeters at layer %zu, print_z %f", layer_region.layer()->id(), layer_region.layer()->print_z);
            throw Slic3r::FileIOError(error_buf);
        }
    }

    //fills
    layer_region.fills.no_sort = j[JSON_LAYER_REGION_FILLS][JSON_EXTRUSION_NO_SORT];
    int fills_entities_count = j[JSON_LAYER_REGION_FILLS][JSON_EXTRUSION_ENTITIES].size();
    for (int fills_entities_index = 0; fills_entities_index < fills_entities_count; fills_entities_index++)
    {
        const json& extrusion_entity_json =  j[JSON_LAYER_REGION_FILLS][JSON_EXTRUSION_ENTITIES][fills_entities_index];
        bool ret = convert_extrusion_from_json(extrusion_entity_json, layer_region.fills);
        if (!ret) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": error parsing fills found at layer %1%, print_z %2%") %layer_region.layer()->id() %layer_region.layer()->print_z;
            char error_buf[1024];
            ::sprintf(error_buf, "Error while parsing fills at layer %zu, print_z %f", layer_region.layer()->id(), layer_region.layer()->print_z);
            throw Slic3r::FileIOError(error_buf);
        }
    }

    return;
}


void extract_layer(const json& layer_json, Layer& layer) {
    //slice_polygons
    int slice_polygons_count = layer_json[JSON_LAYER_SLICED_POLYGONS].size();
    for (int polygon_index = 0; polygon_index < slice_polygons_count; polygon_index++)
    {
        ExPolygon polygon;

        polygon = layer_json[JSON_LAYER_SLICED_POLYGONS][polygon_index];
        layer.lslices.push_back(std::move(polygon));
    }

    //slice_bboxes
    int sliced_bboxes_count = layer_json[JSON_LAYER_SLLICED_BBOXES].size();
    for (int bbox_index = 0; bbox_index < sliced_bboxes_count; bbox_index++)
    {
        BoundingBox bbox;

        bbox = layer_json[JSON_LAYER_SLLICED_BBOXES][bbox_index];
        layer.lslices_bboxes.push_back(std::move(bbox));
    }

    //overhang_polygons
    int overhang_polygons_count = layer_json[JSON_LAYER_OVERHANG_POLYGONS].size();
    for (int polygon_index = 0; polygon_index < overhang_polygons_count; polygon_index++)
    {
        ExPolygon polygon;

        polygon = layer_json[JSON_LAYER_OVERHANG_POLYGONS][polygon_index];
        layer.loverhangs.push_back(std::move(polygon));
    }

    //overhang_box
    layer.loverhangs_bbox = layer_json[JSON_LAYER_OVERHANG_BBOX];

    //layer_regions
    int layer_region_count = layer.region_count();
    for (int layer_region_index = 0; layer_region_index < layer_region_count; layer_region_index++)
    {
        LayerRegion* layer_region = layer.get_region(layer_region_index);
        const json& layer_region_json = layer_json[JSON_LAYER_REGIONS][layer_region_index];
        convert_layer_region_from_json(layer_region_json, *layer_region);

        //LayerRegion layer_region = layer_json[JSON_LAYER_REGIONS][layer_region_index];
    }

    return;
}

void extract_support_layer(const json& support_layer_json, SupportLayer& support_layer) {
    extract_layer(support_layer_json, support_layer);

    //support_islands
    int islands_count = support_layer_json[JSON_SUPPORT_LAYER_ISLANDS].size();
    for (int islands_index = 0; islands_index < islands_count; islands_index++)
    {
        ExPolygon polygon;

        polygon = support_layer_json[JSON_SUPPORT_LAYER_ISLANDS][islands_index];
        support_layer.support_islands.push_back(std::move(polygon));
    }

    //support_fills
    support_layer.support_fills.no_sort = support_layer_json[JSON_SUPPORT_LAYER_FILLS][JSON_EXTRUSION_NO_SORT];
    int support_fills_entities_count = support_layer_json[JSON_SUPPORT_LAYER_FILLS][JSON_EXTRUSION_ENTITIES].size();
    for (int support_fills_entities_index = 0; support_fills_entities_index < support_fills_entities_count; support_fills_entities_index++)
    {
        const json& extrusion_entity_json =  support_layer_json[JSON_SUPPORT_LAYER_FILLS][JSON_EXTRUSION_ENTITIES][support_fills_entities_index];
        bool ret = convert_extrusion_from_json(extrusion_entity_json, support_layer.support_fills);
        if (!ret) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": error parsing fills found at support_layer %1%, print_z %2%")%support_layer.id() %support_layer.print_z;
            char error_buf[1024];
            ::sprintf(error_buf, "Error while parsing fills at support_layer %zu, print_z %f", support_layer.id(), support_layer.print_z);
            throw Slic3r::FileIOError(error_buf);
        }
    }

    return;
}

static void from_json(const json& j, groupedVolumeSlices& firstlayer_group)
{
    firstlayer_group.groupId               =   j[JSON_FIRSTLAYER_GROUP_ID];

    int volume_count = j[JSON_FIRSTLAYER_GROUP_VOLUME_IDS].size();
    for (int volume_index = 0; volume_index < volume_count; volume_index++)
    {
        ObjectID obj_id;

        obj_id.id = j[JSON_FIRSTLAYER_GROUP_VOLUME_IDS][volume_index];
        firstlayer_group.volume_ids.push_back(std::move(obj_id));
    }

    int slices_count = j[JSON_FIRSTLAYER_GROUP_SLICES].size();
    for (int slice_index = 0; slice_index < slices_count; slice_index++)
    {
        ExPolygon polygon;

        polygon = j[JSON_FIRSTLAYER_GROUP_SLICES][slice_index];
        firstlayer_group.slices.push_back(std::move(polygon));
    }
}

int Print::export_cached_data(const std::string& directory, int& obj_cnt_exported, bool with_space)
{
    int ret = 0;
    boost::filesystem::path directory_path(directory);
    obj_cnt_exported = 0;

    auto convert_layer_to_json = [](json& layer_json, const Layer* layer) {
        json slice_polygons_json = json::array(), slice_bboxs_json = json::array(), overhang_polygons_json = json::array(), layer_regions_json = json::array();
        layer_json[JSON_LAYER_PRINT_Z] = layer->print_z;
        layer_json[JSON_LAYER_HEIGHT] = layer->height;
        layer_json[JSON_LAYER_SLICE_Z] = layer->slice_z;
        layer_json[JSON_LAYER_ID] = layer->id();
        //layer_json["slicing_errors"] = layer->slicing_errors;

        //sliced_polygons
        for (const ExPolygon& slice_polygon : layer->lslices) {
            json slice_polygon_json = slice_polygon;
            slice_polygons_json.push_back(std::move(slice_polygon_json));
        }
        layer_json[JSON_LAYER_SLICED_POLYGONS] = std::move(slice_polygons_json);

        //sliced_bbox
        for (const BoundingBox& slice_bbox : layer->lslices_bboxes) {
            json bbox_json = json::array();

            bbox_json = slice_bbox;
            slice_bboxs_json.push_back(std::move(bbox_json));
        }
        layer_json[JSON_LAYER_SLLICED_BBOXES] = std::move(slice_bboxs_json);

        //overhang_polygons
        for (const ExPolygon& overhang_polygon : layer->loverhangs) {
            json overhang_polygon_json = overhang_polygon;
            overhang_polygons_json.push_back(std::move(overhang_polygon_json));
        }
        layer_json[JSON_LAYER_OVERHANG_POLYGONS] = std::move(overhang_polygons_json);

        //overhang_box
        layer_json[JSON_LAYER_OVERHANG_BBOX] = layer->loverhangs_bbox;

        for (const LayerRegion *layer_region : layer->regions()) {
            json region_json = *layer_region;

            layer_regions_json.push_back(std::move(region_json));
        }
        layer_json[JSON_LAYER_REGIONS] = std::move(layer_regions_json);

        return;
    };

    //firstly clear this directory
    /*if (fs::exists(directory_path)) {
        fs::remove_all(directory_path);
    }*/
    try {
        if (!fs::exists(directory_path)) {
            if (!fs::create_directory(directory_path)) {
                BOOST_LOG_TRIVIAL(error) << boost::format("create directory %1% failed")%directory;
                return CLI_EXPORT_CACHE_DIRECTORY_CREATE_FAILED;
            }
        }
    }
    catch (...)
    {
        BOOST_LOG_TRIVIAL(error) << boost::format("create directory %1% failed")%directory;
        return CLI_EXPORT_CACHE_DIRECTORY_CREATE_FAILED;
    }

    int count = 0;
    std::vector<std::string> filename_vector;
    std::vector<json> json_vector;
    size_t region_cnt = this->num_print_regions();
    size_t hash_values = 0;
    for (size_t region_idx = 0; region_idx < region_cnt; region_idx++)
    {
        boost::hash_combine(hash_values, this->get_print_region(region_idx).config_hash());
    }
    for (PrintObject *obj : m_objects) {
        const ModelObject* model_obj = obj->model_object();
        /*if (obj->get_shared_object()) {
            BOOST_LOG_TRIVIAL(info) << boost::format("shared object %1%, skip directly")%model_obj->name;
            continue;
        }*/
        if (m_reslicing_objects.count(obj) == 0) {
            BOOST_LOG_TRIVIAL(info) << boost::format("shared object or already cached before: %1%, skip directly")%model_obj->name;
            continue;
        }
        obj_cnt_exported++;

        const PrintInstance &print_instance = obj->instances()[0];
        const ModelInstance *model_instance = print_instance.model_instance;
        size_t identify_id = (model_instance->loaded_id > 0)?model_instance->loaded_id: model_instance->id().id;
        std::string file_name = directory + "/obj_" + std::to_string(identify_id) + "_" + std::to_string(region_cnt) + "_" + std::to_string(hash_values) + ".json";

        BOOST_LOG_TRIVIAL(warning) << boost::format("begin to dump object %1%, identify_id %2%, hash %3% to %4%, region count %5%")%model_obj->name %identify_id %hash_values %file_name %region_cnt;

        try {
            json root_json, layers_json = json::array(), support_layers_json = json::array(), first_layer_groups = json::array();

            root_json[JSON_OBJECT_NAME] = model_obj->name;
            root_json[JSON_IDENTIFY_ID] = identify_id;

            //export the layers
            std::vector<json> layers_json_vector(obj->layer_count());
            tbb::parallel_for(
                tbb::blocked_range<size_t>(0, obj->layer_count()),
                [&layers_json_vector, obj, convert_layer_to_json](const tbb::blocked_range<size_t>& layer_range) {
                    for (size_t layer_index = layer_range.begin(); layer_index < layer_range.end(); ++ layer_index) {
                        const Layer *layer = obj->get_layer(layer_index);
                        json layer_json;
                        convert_layer_to_json(layer_json, layer);
                        layers_json_vector[layer_index] = std::move(layer_json);
                    }
                }
            );
            for (int l_index = 0; l_index < layers_json_vector.size(); l_index++) {
                layers_json.push_back(std::move(layers_json_vector[l_index]));
            }
            layers_json_vector.clear();
            /*for (const Layer *layer : obj->layers()) {
                // for each layer
                json layer_json;

                convert_layer_to_json(layer_json, layer);

                layers_json.push_back(std::move(layer_json));
            }*/

            root_json[JSON_LAYERS] = std::move(layers_json);

            //export the support layers
            std::vector<json> support_layers_json_vector(obj->support_layer_count());
            tbb::parallel_for(
                tbb::blocked_range<size_t>(0, obj->support_layer_count()),
                [&support_layers_json_vector, obj, convert_layer_to_json](const tbb::blocked_range<size_t>& support_layer_range) {
                    for (size_t s_layer_index = support_layer_range.begin(); s_layer_index < support_layer_range.end(); ++ s_layer_index) {
                        const SupportLayer *support_layer = obj->get_support_layer(s_layer_index);
                        json support_layer_json, support_islands_json = json::array(), support_fills_json, supportfills_entities_json = json::array();

                        convert_layer_to_json(support_layer_json, support_layer);

                        support_layer_json[JSON_SUPPORT_LAYER_INTERFACE_ID] = support_layer->interface_id();

                        //support_islands
                        for (const ExPolygon& support_island : support_layer->support_islands) {
                            json support_island_json = support_island;
                            support_islands_json.push_back(std::move(support_island_json));
                        }
                        support_layer_json[JSON_SUPPORT_LAYER_ISLANDS] = std::move(support_islands_json);

                        //support_fills
                        support_fills_json[JSON_EXTRUSION_NO_SORT] = support_layer->support_fills.no_sort;
                        support_fills_json[JSON_EXTRUSION_ENTITY_TYPE] = JSON_EXTRUSION_TYPE_COLLECTION;
                        for (const ExtrusionEntity* extrusion_entity : support_layer->support_fills.entities) {
                            json supportfill_entity_json, supportfill_entity_paths_json = json::array();
                            bool ret = convert_extrusion_to_json(supportfill_entity_json, supportfill_entity_paths_json, extrusion_entity);
                            if (!ret)
                                continue;

                            supportfills_entities_json.push_back(std::move(supportfill_entity_json));
                        }
                        support_fills_json[JSON_EXTRUSION_ENTITIES] = std::move(supportfills_entities_json);
                        support_layer_json[JSON_SUPPORT_LAYER_FILLS] = std::move(support_fills_json);

                        support_layers_json_vector[s_layer_index] = std::move(support_layer_json);
                    }
                }
            );
            for (int s_index = 0; s_index < support_layers_json_vector.size(); s_index++) {
                support_layers_json.push_back(std::move(support_layers_json_vector[s_index]));
            }
            support_layers_json_vector.clear();

            /*for (const SupportLayer *support_layer : obj->support_layers()) {
                json support_layer_json, support_islands_json = json::array(), support_fills_json, supportfills_entities_json = json::array();

                convert_layer_to_json(support_layer_json, support_layer);

                support_layer_json[JSON_SUPPORT_LAYER_INTERFACE_ID] = support_layer->interface_id();

                //support_islands
                for (const ExPolygon& support_island : support_layer->support_islands.expolygons) {
                    json support_island_json = support_island;
                    support_islands_json.push_back(std::move(support_island_json));
                }
                support_layer_json[JSON_SUPPORT_LAYER_ISLANDS] = std::move(support_islands_json);

                //support_fills
                support_fills_json[JSON_EXTRUSION_NO_SORT] = support_layer->support_fills.no_sort;
                support_fills_json[JSON_EXTRUSION_ENTITY_TYPE] = JSON_EXTRUSION_TYPE_COLLECTION;
                for (const ExtrusionEntity* extrusion_entity : support_layer->support_fills.entities) {
                    json supportfill_entity_json, supportfill_entity_paths_json = json::array();
                    bool ret = convert_extrusion_to_json(supportfill_entity_json, supportfill_entity_paths_json, extrusion_entity);
                    if (!ret)
                        continue;

                    supportfills_entities_json.push_back(std::move(supportfill_entity_json));
                }
                support_fills_json[JSON_EXTRUSION_ENTITIES] = std::move(supportfills_entities_json);
                support_layer_json[JSON_SUPPORT_LAYER_FILLS] = std::move(support_fills_json);

                support_layers_json.push_back(std::move(support_layer_json));
            } // for each layer*/
            root_json[JSON_SUPPORT_LAYERS] = std::move(support_layers_json);

            const std::vector<groupedVolumeSlices> &first_layer_obj_groups =  obj->firstLayerObjGroups();
            for (size_t s_group_index = 0; s_group_index < first_layer_obj_groups.size(); ++ s_group_index) {
                groupedVolumeSlices group = first_layer_obj_groups[s_group_index];

                //convert the id
                for (ObjectID& obj_id : group.volume_ids)
                {
                    const ModelVolume* currentModelVolumePtr = nullptr;
                    //BBS: support shared object logic
                    const PrintObject* shared_object = obj->get_shared_object();
                    if (!shared_object)
                        shared_object = obj;
                    const ModelVolumePtrs& volumes_ptr = shared_object->model_object()->volumes;
                    size_t volume_count = volumes_ptr.size();
                    for (size_t index = 0; index < volume_count; index ++) {
                        currentModelVolumePtr = volumes_ptr[index];
                        if (currentModelVolumePtr->id() == obj_id) {
                            obj_id.id = index;
                            break;
                        }
                    }
                }

                json first_layer_group_json;

                first_layer_group_json = group;
                first_layer_groups.push_back(std::move(first_layer_group_json));
            }
            root_json[JSON_FIRSTLAYER_GROUPS] = std::move(first_layer_groups);

            filename_vector.push_back(file_name);
            json_vector.push_back(std::move(root_json));
            /*boost::nowide::ofstream c;
            c.open(file_name, std::ios::out | std::ios::trunc);
            if (with_space)
                c << std::setw(4) << root_json << std::endl;
            else
                c << root_json.dump(0) << std::endl;
            c.close();*/
            count ++;
            BOOST_LOG_TRIVIAL(info) << boost::format("will dump object %1%'s json to %2%.")%model_obj->name%file_name;
        }
        catch(std::exception &err) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__<< ": save to "<<file_name<<" got a generic exception, reason = " << err.what();
            ret = CLI_EXPORT_CACHE_WRITE_FAILED;
        }
    }

    boost::mutex mutex;
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, filename_vector.size()),
        [filename_vector, &json_vector, with_space, &ret, &mutex](const tbb::blocked_range<size_t>& output_range) {
            for (size_t object_index = output_range.begin(); object_index < output_range.end(); ++ object_index) {
                try {
                    boost::nowide::ofstream c;
                    c.open(filename_vector[object_index], std::ios::out | std::ios::trunc);
                    if (with_space)
                        c << std::setw(4) << json_vector[object_index] << std::endl;
                    else
                        c << json_vector[object_index].dump(0) << std::endl;
                    c.close();
                }
                catch(std::exception &err) {
                    BOOST_LOG_TRIVIAL(error) << __FUNCTION__<< ": save to "<<filename_vector[object_index]<<" got a generic exception, reason = " << err.what();
                    boost::unique_lock l(mutex);
                    ret = CLI_EXPORT_CACHE_WRITE_FAILED;
                }
            }
        }
    );

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(": total printobject count %1%, saved %2%, ret=%3%")%m_objects.size() %count %ret;
    return ret;
}


int Print::load_cached_data(const std::string& directory)
{
    int ret = 0;
    boost::filesystem::path directory_path(directory);

    if (!fs::exists(directory_path)) {
        BOOST_LOG_TRIVIAL(info) << boost::format("directory %1% not exist.")%directory;
        return CLI_IMPORT_CACHE_NOT_FOUND;
    }

    auto find_region = [this](PrintObject* object, size_t config_hash) -> const PrintRegion* {
        int regions_count = object->num_printing_regions();
        for (int index = 0; index < regions_count; index++ )
        {
            const PrintRegion&  print_region = object->printing_region(index);
            if (print_region.config_hash() == config_hash ) {
                return &print_region;
            }
        }
        return NULL;
    };

    int count = 0;
    std::vector<std::pair<std::string, PrintObject*>> object_filenames;
    size_t region_cnt = this->num_print_regions();
    size_t hash_values = 0;
    for (size_t region_idx = 0; region_idx < region_cnt; region_idx++)
    {
        boost::hash_combine(hash_values, this->get_print_region(region_idx).config_hash());
    }
    for (PrintObject *obj : m_objects) {
        const ModelObject* model_obj = obj->model_object();
        const PrintInstance &print_instance = obj->instances()[0];
        const ModelInstance *model_instance = print_instance.model_instance;

        obj->clear_layers();
        obj->clear_support_layers();

        int identify_id = model_instance->loaded_id;
        if (identify_id <= 0) {
            //for old 3mf
            identify_id = model_instance->id().id;
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(": object %1%'s loaded_id is 0, need to use the instance_id %2%")%model_obj->name %identify_id;
            //continue;
        }
        std::string file_name = directory + "/obj_" + std::to_string(identify_id) + "_" + std::to_string(region_cnt) + "_" + std::to_string(hash_values) + ".json";

        if (!fs::exists(file_name)) {
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__<<boost::format(": file %1% not exist, maybe a shared object or not generated before, skip it")%file_name;
            continue;
        }
        object_filenames.push_back({file_name, obj});
    }

    boost::mutex mutex;
    std::vector<json> object_jsons(object_filenames.size());
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, object_filenames.size()),
        [object_filenames, &ret, &object_jsons, &mutex](const tbb::blocked_range<size_t>& filename_range) {
            for (size_t filename_index = filename_range.begin(); filename_index < filename_range.end(); ++ filename_index) {
                try {
                    json root_json;
                    boost::nowide::ifstream ifs(object_filenames[filename_index].first);
                    ifs >> root_json;
                    object_jsons[filename_index] = std::move(root_json);
                }
                catch(std::exception &err) {
                    BOOST_LOG_TRIVIAL(error) << __FUNCTION__<< ": load from "<<object_filenames[filename_index].first<<" got a generic exception, reason = " << err.what();
                    boost::unique_lock l(mutex);
                    ret = CLI_IMPORT_CACHE_LOAD_FAILED;
                }
            }
        }
    );

    if (ret) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__<< boost::format(": load json failed.");
        return ret;
    }

    for (int obj_index = 0; obj_index < object_jsons.size(); obj_index++) {
        json& root_json = object_jsons[obj_index];
        PrintObject *obj = object_filenames[obj_index].second;

        try {
            //boost::nowide::ifstream ifs(file_name);
            //ifs >> root_json;

            std::string name = root_json.at(JSON_OBJECT_NAME);
            int identify_id = root_json.at(JSON_IDENTIFY_ID);
            int layer_count = 0, support_layer_count = 0, firstlayer_group_count = 0;

            layer_count = root_json[JSON_LAYERS].size();
            support_layer_count = root_json[JSON_SUPPORT_LAYERS].size();
            firstlayer_group_count = root_json[JSON_FIRSTLAYER_GROUPS].size();

            BOOST_LOG_TRIVIAL(info) << __FUNCTION__<<boost::format(":will load %1%, identify_id %2%, layer_count %3%, support_layer_count %4%, firstlayer_group_count %5%")
                %name %identify_id %layer_count %support_layer_count %firstlayer_group_count;

            Layer* previous_layer = NULL;
            //create layer and layer regions
            for (int index = 0; index < layer_count; index++)
            {
                json& layer_json = root_json[JSON_LAYERS][index];
                Layer* new_layer = obj->add_layer(layer_json[JSON_LAYER_ID], layer_json[JSON_LAYER_HEIGHT], layer_json[JSON_LAYER_PRINT_Z], layer_json[JSON_LAYER_SLICE_Z]);
                if (!new_layer) {
                    BOOST_LOG_TRIVIAL(error) <<__FUNCTION__<< boost::format(":create_layer failed, out of memory");
                    return CLI_OUT_OF_MEMORY;
                }
                if (previous_layer) {
                    previous_layer->upper_layer = new_layer;
                    new_layer->lower_layer = previous_layer;
                }
                previous_layer = new_layer;

                //layer regions
                int layer_regions_count = layer_json[JSON_LAYER_REGIONS].size();
                for (int region_index = 0; region_index < layer_regions_count; region_index++)
                {
                    json& region_json = layer_json[JSON_LAYER_REGIONS][region_index];
                    size_t config_hash = region_json[JSON_LAYER_REGION_CONFIG_HASH];
                    const PrintRegion *print_region = find_region(obj, config_hash);

                    if (!print_region){
                        BOOST_LOG_TRIVIAL(error) <<__FUNCTION__<< boost::format(":can not find print region of object %1%, layer %2%, print_z %3%, layer_region %4%")
                            %name % index %new_layer->print_z %region_index;
                        //delete new_layer;
                        return CLI_IMPORT_CACHE_DATA_CAN_NOT_USE;
                    }

                    new_layer->add_region(print_region);
                }

            }

            //load the layer data parallel
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__<<boost::format(": load the layers in parallel");
            tbb::parallel_for(
                tbb::blocked_range<size_t>(0, obj->layer_count()),
                [&root_json, &obj](const tbb::blocked_range<size_t>& layer_range) {
                    for (size_t layer_index = layer_range.begin(); layer_index < layer_range.end(); ++ layer_index) {
                        const json& layer_json = root_json[JSON_LAYERS][layer_index];
                        Layer* layer = obj->get_layer(layer_index);
                        extract_layer(layer_json, *layer);
                    }
                }
            );

            //support layers
            Layer* previous_support_layer = NULL;
            //create support_layers
            for (int index = 0; index < support_layer_count; index++)
            {
                json& layer_json = root_json[JSON_SUPPORT_LAYERS][index];
                SupportLayer* new_support_layer = obj->add_support_layer(layer_json[JSON_LAYER_ID], layer_json[JSON_SUPPORT_LAYER_INTERFACE_ID], layer_json[JSON_LAYER_HEIGHT], layer_json[JSON_LAYER_PRINT_Z]);
                if (!new_support_layer) {
                    BOOST_LOG_TRIVIAL(error) <<__FUNCTION__<< boost::format(":add_support_layer failed, out of memory");
                    return CLI_OUT_OF_MEMORY;
                }
                if (previous_support_layer) {
                    previous_support_layer->upper_layer = new_support_layer;
                    new_support_layer->lower_layer = previous_support_layer;
                }
                previous_support_layer = new_support_layer;
            }

            BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(": finished load layers, start to load support_layers.");
            tbb::parallel_for(
                tbb::blocked_range<size_t>(0, obj->support_layer_count()),
                [&root_json, &obj](const tbb::blocked_range<size_t>& support_layer_range) {
                    for (size_t layer_index = support_layer_range.begin(); layer_index < support_layer_range.end(); ++ layer_index) {
                        const json& layer_json = root_json[JSON_SUPPORT_LAYERS][layer_index];
                        SupportLayer* support_layer = obj->get_support_layer(layer_index);
                        extract_support_layer(layer_json, *support_layer);
                    }
                }
            );

            //load first group volumes
            std::vector<groupedVolumeSlices>& firstlayer_objgroups = obj->firstLayerObjGroupsMod();
            for (int index = 0; index < firstlayer_group_count; index++)
            {
                json& firstlayer_group_json = root_json[JSON_FIRSTLAYER_GROUPS][index];
                groupedVolumeSlices firstlayer_group = firstlayer_group_json;
                //convert the id
                for (ObjectID& obj_id : firstlayer_group.volume_ids)
                {
                    ModelVolume* currentModelVolumePtr = nullptr;
                    ModelVolumePtrs& volumes_ptr = obj->model_object()->volumes;
                    size_t volume_count = volumes_ptr.size();
                    if (obj_id.id < volume_count) {
                        currentModelVolumePtr = volumes_ptr[obj_id.id];
                        obj_id = currentModelVolumePtr->id();
                    }
                    else {
                        BOOST_LOG_TRIVIAL(error) << __FUNCTION__<< boost::format(": can not find volume_id %1% from object file %2% in firstlayer groups, volume_count %3%!")
                            %obj_id.id %object_filenames[obj_index].first %volume_count;
                        return CLI_IMPORT_CACHE_LOAD_FAILED;
                    }
                }
                firstlayer_objgroups.push_back(std::move(firstlayer_group));
            }

            count ++;
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(": load object %1% from %2% successfully.")%count%object_filenames[obj_index].first;
        }
        catch(nlohmann::detail::parse_error &err) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__<< ": parse "<<object_filenames[obj_index].first<<" got a nlohmann::detail::parse_error, reason = " << err.what();
            return CLI_IMPORT_CACHE_LOAD_FAILED;
        }
        catch(std::exception &err) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__<< ": load from "<<object_filenames[obj_index].first<<" got a generic exception, reason = " << err.what();
            ret = CLI_IMPORT_CACHE_LOAD_FAILED;
        }
    }

    object_jsons.clear();
    object_filenames.clear();
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(": total printobject count %1%, loaded %2%, ret=%3%")%m_objects.size() %count %ret;
    return ret;
}


} // namespace Slic3r
