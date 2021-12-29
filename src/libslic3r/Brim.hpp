#ifndef slic3r_Brim_hpp_
#define slic3r_Brim_hpp_

#include<map>
#include<vector>

namespace Slic3r {

class Print;
class ExtrusionEntityCollection;
class PrintTryCancel;
class Polygons;
class ObjectID;

// Produce brim lines around those objects, that have the brim enabled.
// Collect islands_area to be merged into the final 1st layer convex hull.
ExtrusionEntityCollection make_brim(const Print& print, PrintTryCancel try_cancel, Polygons& islands_area);
void make_brim(const Print& print, PrintTryCancel try_cancel,
    Polygons& islands_area, std::map<ObjectID, ExtrusionEntityCollection>& brimMap,
    std::map<ObjectID, ExtrusionEntityCollection>& supportBrimMap,
    std::vector<std::pair<ObjectID, unsigned int>>& objPrintVec);

// BBS: automatically make brim
ExtrusionEntityCollection make_brim_auto(const Print &print, PrintTryCancel try_cancel, Polygons &islands_area);

} // Slic3r

#endif // slic3r_Brim_hpp_
