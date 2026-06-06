#include "ExPolygonSecondMoment.hpp"

namespace Slic3r {

namespace {

bool compSecondMoment(Polygon poly, Vec2d &sm)
{
    if (poly.is_clockwise())
        poly.make_counter_clockwise();

    sm = Vec2d(0., 0.);
    if (poly.points.size() >= 3) {
        Vec2d p1 = poly.points.back().cast<double>();
        for (const Point &p : poly.points) {
            Vec2d  p2 = p.cast<double>();
            double a  = cross2(p1, p2);
            sm += Vec2d(
                      (p1.y() * p1.y() + p1.y() * p2.y() + p2.y() * p2.y()),
                      (p1.x() * p1.x() + p1.x() * p2.x() + p2.x() * p2.x()))
                  * a / 12;
            p1 = p2;
        }
        return true;
    }
    return false;
}

struct ExPolyProp
{
    double aera = 0;
    Vec2d  centroid;
    Vec2d  secondMomentOfAreaRespectToCentroid;
};

bool compSecondMoment(const ExPolygon &expoly, ExPolyProp &expolyProp)
{
    double aera = expoly.contour.area();
    Vec2d  cent = expoly.contour.centroid().cast<double>() * aera;
    Vec2d  sm;
    if (! compSecondMoment(expoly.contour, sm))
        return false;

    for (auto &hole : expoly.holes) {
        double a = hole.area();
        aera += hole.area();
        cent += hole.centroid().cast<double>() * a;
        Vec2d smh;
        if (compSecondMoment(hole, smh))
            sm += -smh;
    }

    cent = cent / aera;
    sm   = sm - Vec2d(cent.y() * cent.y(), cent.x() * cent.x()) * aera;
    expolyProp.aera = aera;
    expolyProp.centroid = cent;
    expolyProp.secondMomentOfAreaRespectToCentroid = sm;
    return true;
}

} // namespace

bool compSecondMoment(const ExPolygons &expolys, double &smExpolysX, double &smExpolysY)
{
    if (expolys.empty())
        return false;

    std::vector<ExPolyProp> props;
    for (const ExPolygon &expoly : expolys) {
        ExPolyProp prop;
        if (compSecondMoment(expoly, prop))
            props.push_back(prop);
    }
    if (props.empty())
        return false;

    double totalArea = 0.;
    Vec2d  staticMoment(0., 0.);
    for (const ExPolyProp &prop : props) {
        totalArea += prop.aera;
        staticMoment += prop.centroid * prop.aera;
    }
    double totalCentroidX = staticMoment.x() / totalArea;
    double totalCentroidY = staticMoment.y() / totalArea;

    smExpolysX = 0;
    smExpolysY = 0;
    for (const ExPolyProp &prop : props) {
        double deltaX = prop.centroid.x() - totalCentroidX;
        double deltaY = prop.centroid.y() - totalCentroidY;
        smExpolysX += prop.secondMomentOfAreaRespectToCentroid.x() + prop.aera * deltaY * deltaY;
        smExpolysY += prop.secondMomentOfAreaRespectToCentroid.y() + prop.aera * deltaX * deltaX;
    }

    return true;
}

} // namespace Slic3r
