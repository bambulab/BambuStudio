#include "ConflictChecker.hpp"

#include <tbb/parallel_for.h>
#include <tbb/concurrent_vector.h>

#include <map>
#include <functional>
#include <atomic>

namespace Slic3r {

namespace RasterizationImpl {
using IndexPair = std::pair<int64_t, int64_t>;
using Grids     = std::vector<IndexPair>;

inline constexpr int64_t RasteXDistance = scale_(1);
inline constexpr int64_t RasteYDistance = scale_(1);

inline IndexPair point_map_grid_index(const Point &pt, int64_t xdist, int64_t ydist)
{
    auto x = pt.x() / xdist;
    auto y = pt.y() / ydist;
    return std::make_pair(x, y);
}

inline bool nearly_equal(const Point &p1, const Point &p2) { return std::abs(p1.x() - p2.x()) < SCALED_EPSILON && std::abs(p1.y() - p2.y()) < SCALED_EPSILON; }

inline Grids line_rasterization(const Line &line, int64_t xdist = RasteXDistance, int64_t ydist = RasteYDistance)
{
    Grids     res;
    Point     rayStart     = line.a;
    Point     rayEnd       = line.b;
    IndexPair currentVoxel = point_map_grid_index(rayStart, xdist, ydist);
    IndexPair firstVoxel   = currentVoxel;
    IndexPair lastVoxel    = point_map_grid_index(rayEnd, xdist, ydist);

    Point ray = rayEnd - rayStart;

    double stepX = ray.x() >= 0 ? 1 : -1;
    double stepY = ray.y() >= 0 ? 1 : -1;

    double nextVoxelBoundaryX = (currentVoxel.first + stepX) * xdist;
    double nextVoxelBoundaryY = (currentVoxel.second + stepY) * ydist;

    if (stepX < 0) { nextVoxelBoundaryX += xdist; }
    if (stepY < 0) { nextVoxelBoundaryY += ydist; }

    double tMaxX = ray.x() != 0 ? (nextVoxelBoundaryX - rayStart.x()) / ray.x() : DBL_MAX;
    double tMaxY = ray.y() != 0 ? (nextVoxelBoundaryY - rayStart.y()) / ray.y() : DBL_MAX;

    double tDeltaX = ray.x() != 0 ? static_cast<double>(xdist) / ray.x() * stepX : DBL_MAX;
    double tDeltaY = ray.y() != 0 ? static_cast<double>(ydist) / ray.y() * stepY : DBL_MAX;

    res.push_back(currentVoxel);

    double tx = tMaxX;
    double ty = tMaxY;

    while (lastVoxel != currentVoxel) {
        if (lastVoxel.first == currentVoxel.first) {
            for (int64_t i = currentVoxel.second; i != lastVoxel.second; i += (int64_t) stepY) {
                currentVoxel.second += (int64_t) stepY;
                res.push_back(currentVoxel);
            }
            break;
        }
        if (lastVoxel.second == currentVoxel.second) {
            for (int64_t i = currentVoxel.first; i != lastVoxel.first; i += (int64_t) stepX) {
                currentVoxel.first += (int64_t) stepX;
                res.push_back(currentVoxel);
            }
            break;
        }

        if (tx < ty) {
            currentVoxel.first += (int64_t) stepX;
            tx += tDeltaX;
        } else {
            currentVoxel.second += (int64_t) stepY;
            ty += tDeltaY;
        }
        res.push_back(currentVoxel);
        if (res.size() >= 100000) { // bug
            assert(0);
        }
    }

    return res;
}
} // namespace RasterizationImpl

void LinesBucketQueue::emplace_back_bucket(std::vector<ExtrusionPaths> &&paths, const void *objPtr, Point offset)
{
    auto oldSize = _buckets.capacity();
    if (_objsPtrToId.find(objPtr) == _objsPtrToId.end()) {
        _objsPtrToId.insert({objPtr, _objsPtrToId.size()});
        _idToObjsPtr.insert({_objsPtrToId.size() - 1, objPtr});
    }
    _buckets.emplace_back(std::move(paths), _objsPtrToId[objPtr], offset);
    _pq.push(&_buckets.back());
    auto newSize = _buckets.capacity();
    if (oldSize != newSize) { // pointers change
        decltype(_pq) newQueue;
        for (LinesBucket &bucket : _buckets) { newQueue.push(&bucket); }
        std::swap(_pq, newQueue);
    }
}

double LinesBucketQueue::removeLowests()
{
    auto lowest = _pq.top();
    _pq.pop();
    double                     curHeight = lowest->curHeight();
    std::vector<LinesBucket *> lowests;
    lowests.push_back(lowest);

    while (_pq.empty() == false && std::abs(_pq.top()->curHeight() - lowest->curHeight()) < EPSILON) {
        lowests.push_back(_pq.top());
        _pq.pop();
    }

    for (LinesBucket *bp : lowests) {
        bp->raise();
        if (bp->valid()) { _pq.push(bp); }
    }
    return curHeight;
}

LineWithIDs LinesBucketQueue::getCurLines() const
{
    LineWithIDs lines;
    for (const LinesBucket &bucket : _buckets) {
        if (bucket.valid()) {
            LineWithIDs tmpLines = bucket.curLines();
            lines.insert(lines.end(), tmpLines.begin(), tmpLines.end());
        }
    }
    return lines;
}

void getExtrusionPathsFromEntity(const ExtrusionEntityCollection *entity, ExtrusionPaths &paths)
{
    std::function<void(const ExtrusionEntityCollection *, ExtrusionPaths &)> getExtrusionPathImpl = [&](const ExtrusionEntityCollection *entity, ExtrusionPaths &paths) {
        for (auto entityPtr : entity->entities) {
            if (const ExtrusionEntityCollection *collection = dynamic_cast<ExtrusionEntityCollection *>(entityPtr)) {
                getExtrusionPathImpl(collection, paths);
            } else if (const ExtrusionPath *path = dynamic_cast<ExtrusionPath *>(entityPtr)) {
                paths.push_back(*path);
            } else if (const ExtrusionMultiPath *multipath = dynamic_cast<ExtrusionMultiPath *>(entityPtr)) {
                for (const ExtrusionPath &path : multipath->paths) { paths.push_back(path); }
            } else if (const ExtrusionLoop *loop = dynamic_cast<ExtrusionLoop *>(entityPtr)) {
                for (const ExtrusionPath &path : loop->paths) { paths.push_back(path); }
            }
        }
    };
    getExtrusionPathImpl(entity, paths);
}

ExtrusionPaths getExtrusionPathsFromLayer(LayerRegionPtrs layerRegionPtrs)
{
    ExtrusionPaths paths;
    for (auto regionPtr : layerRegionPtrs) {
        getExtrusionPathsFromEntity(&regionPtr->perimeters, paths);
        if (regionPtr->perimeters.empty() == false) { getExtrusionPathsFromEntity(&regionPtr->fills, paths); }
    }
    return paths;
}

ExtrusionPaths getExtrusionPathsFromSupportLayer(SupportLayer *supportLayer)
{
    ExtrusionPaths paths;
    getExtrusionPathsFromEntity(&supportLayer->support_fills, paths);
    return paths;
}

std::pair<std::vector<ExtrusionPaths>, std::vector<ExtrusionPaths>> getAllLayersExtrusionPathsFromObject(PrintObject *obj)
{
    std::vector<ExtrusionPaths> objPaths, supportPaths;

    for (auto layerPtr : obj->layers()) { objPaths.push_back(getExtrusionPathsFromLayer(layerPtr->regions())); }

    for (auto supportLayerPtr : obj->support_layers()) { supportPaths.push_back(getExtrusionPathsFromSupportLayer(supportLayerPtr)); }

    return {std::move(objPaths), std::move(supportPaths)};
}

ConflictComputeOpt ConflictChecker::find_inter_of_lines(const LineWithIDs &lines)
{
    using namespace RasterizationImpl;
    std::map<IndexPair, std::vector<int>> indexToLine;

    for (int i = 0; i < lines.size(); ++i) {
        const LineWithID &l1      = lines[i];
        auto              indexes = line_rasterization(l1._line);
        for (auto index : indexes) {
            const auto &possibleIntersectIdxs = indexToLine[index];
            for (auto possibleIntersectIdx : possibleIntersectIdxs) {
                const LineWithID &l2 = lines[possibleIntersectIdx];
                if (auto interRes = line_intersect(l1, l2); interRes.has_value()) { return interRes; }
            }
            indexToLine[index].push_back(i);
        }
    }
    return {};
}

ConflictResultOpt ConflictChecker::find_inter_of_lines_in_diff_objs(PrintObjectPtrs                      objs,
                                                                  std::optional<const FakeWipeTower *> wtdptr) // find the first intersection point of lines in different objects
{
    if (objs.size() <= 1) { return {}; }
    LinesBucketQueue conflictQueue;
    if (wtdptr.has_value()) { // wipe tower at 0 by default
        auto wtpaths = wtdptr.value()->getFakeExtrusionPathsFromWipeTower();
        conflictQueue.emplace_back_bucket(std::move(wtpaths), wtdptr.value(), {wtdptr.value()->plate_origin.x(),wtdptr.value()->plate_origin.y()});
    }
    for (PrintObject *obj : objs) {
        auto layers = getAllLayersExtrusionPathsFromObject(obj);
        conflictQueue.emplace_back_bucket(std::move(layers.first), obj, obj->instances().front().shift);
        conflictQueue.emplace_back_bucket(std::move(layers.second), obj, obj->instances().front().shift);
    }

    std::vector<LineWithIDs> layersLines;
    std::vector<double>      heights;
    while (conflictQueue.valid()) {
        LineWithIDs lines     = conflictQueue.getCurLines();
        double      curHeight = conflictQueue.removeLowests();
        heights.push_back(curHeight);
        layersLines.push_back(std::move(lines));
    }

    bool                                   find = false;
    tbb::concurrent_vector<std::pair<ConflictComputeResult,double>> conflict;

    tbb::parallel_for(tbb::blocked_range<size_t>(0, layersLines.size()), [&](tbb::blocked_range<size_t> range) {
        for (size_t i = range.begin(); i < range.end(); i++) {
            auto interRes = find_inter_of_lines(layersLines[i]);
            if (interRes.has_value()) {
                find = true;
                conflict.emplace_back(interRes.value(),heights[i]);
                break;
            }
        }
    });

    if (find) {
        const void *ptr1           = conflictQueue.idToObjsPtr(conflict[0].first._obj1);
        const void *ptr2           = conflictQueue.idToObjsPtr(conflict[0].first._obj2);
        double      conflictHeight = conflict[0].second;
        if (wtdptr.has_value()) {
            const FakeWipeTower *wtdp = wtdptr.value();
            if (ptr1 == wtdp || ptr2 == wtdp) {
                if (ptr2 == wtdp) { std::swap(ptr1, ptr2); }
                const PrintObject *obj2 = reinterpret_cast<const PrintObject *>(ptr2);
                return std::make_optional<ConflictResult>("WipeTower", obj2->model_object()->name, conflictHeight, nullptr, ptr2);
            }
        }
        const PrintObject *obj1 = reinterpret_cast<const PrintObject *>(ptr1);
        const PrintObject *obj2 = reinterpret_cast<const PrintObject *>(ptr2);
        return std::make_optional<ConflictResult>(obj1->model_object()->name, obj2->model_object()->name, conflictHeight, ptr1, ptr2);
    } else
        return {};
}

ConflictComputeOpt ConflictChecker::line_intersect(const LineWithID &l1, const LineWithID &l2)
{
    if (l1._id == l2._id) { return {}; } // return true if lines are from same object
    Point inter;
    bool  intersect = l1._line.intersection(l2._line, &inter);
    if (intersect) {
        auto dist1 = std::min(unscale(Point(l1._line.a - inter)).norm(), unscale(Point(l1._line.b - inter)).norm());
        auto dist2 = std::min(unscale(Point(l2._line.a - inter)).norm(), unscale(Point(l2._line.b - inter)).norm());
        auto dist  = std::min(dist1, dist2);
        if (dist > 0.01) { return std::make_optional<ConflictComputeResult>(l1._id, l2._id); } // the two lines intersects if dist>0.01mm
    }
    return {};
}

} // namespace Slic3r