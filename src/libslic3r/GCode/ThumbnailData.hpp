#ifndef slic3r_ThumbnailData_hpp_
#define slic3r_ThumbnailData_hpp_

#include <vector>
#include "libslic3r/Point.hpp"
#include "nlohmann/json.hpp"

namespace Slic3r {

struct ThumbnailData
{
    unsigned int width;
    unsigned int height;
    std::vector<unsigned char> pixels;

    ThumbnailData() { reset(); }
    void set(unsigned int w, unsigned int h);
    void reset();

    bool is_valid() const;
};

//BBS: add plate id into thumbnail render logic
using ThumbnailsList = std::vector<ThumbnailData>;

struct ThumbnailsParams
{
	const Vec2ds 	sizes;
	bool 			printable_only;
	bool 			parts_only;
	bool 			show_bed;
	bool 			transparent_background;
    int             plate_id;
};

typedef std::function<ThumbnailsList(const ThumbnailsParams&)> ThumbnailsGeneratorCallback;

struct BBoxData
{
    int id;  // object id
    std::vector<coordf_t> bbox; // first layer bounding box: min.{x,y}, max.{x,y}
    float area;  // first layer area
    void to_json(nlohmann::json& j) const{
        j = nlohmann::json{ {"id",id},{"bbox",bbox},{"area",area} };
    }
    void from_json(const nlohmann::json& j) {
        j.at("id").get_to(id);
        j.at("bbox").get_to(bbox);
        j.at("area").get_to(area);
    }
};

struct PlateBBoxData
{
    std::vector<coordf_t> bbox_all;  // total bounding box of all objects including brim
    std::vector<BBoxData> bbox_objs; // BBoxData of seperate object
    void to_json(nlohmann::json& j) const{
        j = nlohmann::json{ {"bbox_all",bbox_all} };
        for (const auto& bbox : bbox_objs) {
            nlohmann::json j_bbox;
            bbox.to_json(j_bbox);
            j["bbox_objects"].push_back(j_bbox);
        }
    }
    void from_json(const nlohmann::json& j) {
        j.at("bbox_all").get_to(bbox_all);
        for (auto& bbox_j : j.at("bbox_objects")) {
            BBoxData bbox_data;
            bbox_data.from_json(bbox_j);
            bbox_objs.push_back(bbox_data);
        }
    }
    bool is_valid() const {
        return !bbox_objs.empty();
    }
};

} // namespace Slic3r

#endif // slic3r_ThumbnailData_hpp_
