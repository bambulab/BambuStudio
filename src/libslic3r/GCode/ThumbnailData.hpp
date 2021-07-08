#ifndef slic3r_ThumbnailData_hpp_
#define slic3r_ThumbnailData_hpp_

#include <vector>
#include "libslic3r/Point.hpp"

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

} // namespace Slic3r

#endif // slic3r_ThumbnailData_hpp_
