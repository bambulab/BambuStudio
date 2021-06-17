#include "Orient.hpp"

#include <numeric>
#include <ClipperUtils.hpp>
#include <boost/geometry/index/rtree.hpp>

#if defined(_MSC_VER) && defined(__clang__)
#define BOOST_NO_CXX17_HDR_STRING_VIEW
#endif

#include <boost/multiprecision/integer.hpp>
#include <boost/rational.hpp>

#undef MAX3
#define MAX3(a,b,c) std::max(std::max(a,b),c)

#undef MEDIAN
#define MEDIAN3(a,b,c) std::max(std::min(a,b), std::min(std::max(a,b),c))


namespace Slic3r {

namespace orientation {

    struct CostItems {
        float overhang;
        float bottom;
        float bottom_hull;
        float contour;
        float area_laf;  // area_of_low_angle_faces
        float area_projected; // area of projected 2D profile
        float volume;
        float unprintability;
        CostItems() { memset(this, 0, sizeof(*this)); }
        static std::string field_names() {
            return "                                      overhang, bottom, bothull, contour, A_laf, A_prj, volume, unprintability";
        }
        std::string field_values() {
            std::stringstream ss;
            ss << std::fixed << std::setprecision(1);
            ss << overhang << ",\t" << bottom << ",\t" << bottom_hull << ",\t" << contour << ",\t" << area_laf << ",\t" << area_projected << ",\t" << volume << ",\t" << unprintability;
            return ss.str();
        }
    };



// A class encapsulating the libnest2d Nester class and extending it with other
// management and spatial index structures for acceleration.
class AutoOrienter {
public:
    TriangleMesh* mesh;
    TriangleMesh mesh_convex_hull;
    std::vector<int> face_index;
    Eigen::MatrixXf normals, normals_hull;
    Eigen::VectorXf areas, areas_hull;
    Eigen::MatrixXf z_projected;
    Eigen::VectorXf z_max, z_max_hull;  // max of projected z
    Eigen::VectorXf z_median;  // median of projected z
    Eigen::VectorXf z_mean;  // mean of projected z
    OrientParams params;


    std::vector< Vec3f> orientations;  // Vec3f == stl_normal
    std::function<void(unsigned)> progressind = { };  // default empty indicator function
    
public:
    AutoOrienter(TriangleMesh* mesh_,
                 const OrientParams           &params_,
                 std::function<void(unsigned)> progressind_,
                 std::function<bool(void)>     stopcond_)
    {
        mesh = mesh_;
        params = params_;
        progressind = progressind_;

        preprocess();
    }

    AutoOrienter(TriangleMesh* mesh_)
    {
        mesh = mesh_;
        preprocess();
    }

    struct cmp_vec3f {
        bool operator()(const Vec3f& n1, const Vec3f& n2) const {
            return n1(0) < n2(0)
                || ((n1(0) == n2(0)) && n1(1) < n2(1))
                || (((n1(0) == n2(0)) && (n1(1) == n2(1)) && (n1(2) < n2(2))));
        }
    };

    Vec3f process()
    {
        orientations = { { 0,0,-1 } }; // original orientation

        area_cumulation(normals, areas);

        area_cumulation(normals_hull, areas_hull);

        add_supplements();

        if(progressind)
            progressind(20);

        remove_duplicates();
        
        if (progressind)
            progressind(30);

        std::map<Vec3f, CostItems, cmp_vec3f> results;
        BOOST_LOG_TRIVIAL(error) << CostItems::field_names();
        for (int i = 0; i < orientations.size();i++) {
            auto orientation = -orientations[i];

            project_vertices(orientation);

            auto cost_items = get_features(orientation, params.min_volume);

            float unprintability = target_function(cost_items, params.min_volume);

            results[orientation] = cost_items;

            BOOST_LOG_TRIVIAL(error) << std::fixed << std::setprecision(4) << "orientation:" << orientation.transpose() << ", cost:" << cost_items.field_values();
        }
        if (progressind)
            progressind(60);

        typedef std::pair<Vec3f, CostItems> PAIR;
        std::vector<PAIR> results_vector(results.begin(), results.end());
        sort(results_vector.begin(), results_vector.end(), [](const PAIR& p1, const PAIR& p2) {return p1.second.unprintability < p2.second.unprintability; });
        
        if (progressind)
            progressind(80);

        auto best_orientation = results_vector[0].first;

        BOOST_LOG_TRIVIAL(error) << std::fixed << std::setprecision(4) << "best:" << best_orientation.transpose() << ", costs:" << results_vector[0].second.field_values();
        flush_logs();

        return best_orientation;
    }
     
    void preprocess()
    {
        {
            auto& facets = mesh->stl.facet_start;
            int face_count = facets.size();
            areas.resize(face_count, 1);
            normals.resize(face_count, 3);
            face_index.resize(face_count);
            for (size_t i = 0; i < face_count; i++)
            {
                float area = get_area(&facets[i]);
                if (params.NEGL_FACE_SIZE > 0 && area < params.NEGL_FACE_SIZE)
                    continue;
                normals.row(i) = facets[i].normal;
                normals.row(i).normalize();
                areas(i) = area;
                face_index[i] = i;
            }
        }

        // get convex hull statistics
        {
            mesh_convex_hull = mesh->convex_hull_3d();
            //mesh_convex_hull.write_binary("convex_hull_debug.stl");

            auto& facets = mesh_convex_hull.stl.facet_start;
            int face_count = facets.size();
            areas_hull.resize(face_count, 1);
            normals_hull.resize(face_count, 3);
            for (size_t i = 0; i < face_count; i++)
            {
                float area = get_area(&facets[i]);
                if (params.NEGL_FACE_SIZE > 0 && area < params.NEGL_FACE_SIZE)
                    continue;
                normals_hull.row(i) = facets[i].normal;
                normals_hull.row(i).normalize();
                areas_hull(i) = area;
            }
        }
    }

    void area_cumulation(const Eigen::MatrixXf& normals_, const Eigen::VectorXf& areas_, int num_directions = 10)
    {
        std::map<stl_normal, float, cmp_vec3f> alignments;
        // init to 0
        for (size_t i = 0; i < areas_.size(); i++)
            alignments.insert(std::pair(normals_.row(i), 0));
        // cumulate areas
        for (size_t i = 0; i < areas_.size(); i++)
        {
            alignments[normals_.row(i)] += areas_(i);
        }

        typedef std::pair<stl_normal, float> PAIR;
        std::vector<PAIR> align_counts(alignments.begin(), alignments.end());
        sort(align_counts.begin(), align_counts.end(), [](const PAIR& p1, const PAIR& p2) {return p1.second > p2.second; });
        
        num_directions = std::min((size_t)num_directions, align_counts.size());
        for (size_t i = 0; i < num_directions; i++)
        {
            orientations.push_back(align_counts[i].first);
        }
    }

    void add_supplements()
    {
        std::vector<Vec3f> vecs = { {0, 0, -1} ,{0.70710678, 0, -0.70710678},{0, 0.70710678, -0.70710678},
            {-0.70710678, 0, -0.70710678},{0, -0.70710678, -0.70710678},
            {1, 0, 0},{0.70710678, 0.70710678, 0},{0, 1, 0},{-0.70710678, 0.70710678, 0},
            {-1, 0, 0},{-0.70710678, -0.70710678, 0},{0, -1, 0},{0.70710678, -0.70710678, 0},
            {0.70710678, 0, 0.70710678},{0, 0.70710678, 0.70710678},
            {-0.70710678, 0, 0.70710678},{0, -0.70710678, 0.70710678},{0, 0, 1} };
        orientations.insert(orientations.end(), vecs.begin(), vecs.end());
    }

    /// <summary>
    /// remove duplicate orientations
    /// </summary>
    /// <param name="tol">tolerance. default 0.08 =sin(5\degree)</param>
    void remove_duplicates(float tol=0.08)
    {
        for (auto it = orientations.begin()+1; it < orientations.end(); )
        {
            bool duplicate = false;
            for (auto it_ok = orientations.begin(); it_ok < it; it_ok++)
            {
                if (it_ok->isApprox(*it, tol)) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate)
                it = orientations.erase(it);
            else
                it++;
        }
    }

    void project_vertices(Vec3f orientation)
    {
        auto& facets = mesh->stl.facet_start;
        int face_count = facets.size();
        z_projected.resize(face_count, 3);
        z_max.resize(face_count, 1);
        z_median.resize(face_count, 1);
        z_mean.resize(face_count, 1);
        for (size_t i = 0; i < face_count; i++)
        {
            float z0 = this->mesh->stl.facet_start[i].vertex[0].dot(orientation);
            float z1 = this->mesh->stl.facet_start[i].vertex[1].dot(orientation);
            float z2 = this->mesh->stl.facet_start[i].vertex[2].dot(orientation);
            z_projected(i, 0) = z0;
            z_projected(i, 1) = z1;
            z_projected(i, 2) = z2;
            z_max(i) = MAX3(z0,z1,z2);
            z_median(i) = MEDIAN3(z0,z1,z2);
            z_mean(i) = (z0 + z1 + z2) / 3;
        }

        z_max_hull.resize(mesh_convex_hull.stl.facet_start.size(), 1);
        for (size_t i = 0; i < z_max_hull.rows(); i++)
        {
            float z0 = mesh_convex_hull.stl.facet_start[i].vertex[0].dot(orientation);
            float z1 = mesh_convex_hull.stl.facet_start[i].vertex[1].dot(orientation);
            float z2 = mesh_convex_hull.stl.facet_start[i].vertex[2].dot(orientation);
            z_max_hull(i) = MAX3(z0, z1, z2);
        }
    }

    static Eigen::VectorXi argsort(const Eigen::VectorXf& vec)
    {
        Eigen::VectorXi ind = Eigen::VectorXi::LinSpaced(vec.size(), 0, vec.size() - 1);//[0 1 2 3 ... N-1]
        auto rule = [vec](int i, int j)->bool {
            return vec(i) < vec(j);
        };
        std::sort(ind.data(), ind.data() + ind.size(), rule);
        return ind;

        //sorted_vec.resize(vec.size());
        //for (int i = 0; i < vec.size(); i++) {
        //    sorted_vec(i) = vec(ind(i));
        //}
    }

    // previously calc_overhang
    CostItems get_features(Vec3f orientation, bool min_volume = true)
    {
        CostItems costs;
        float total_min_z = z_projected.minCoeff();
        // filter bottom area
        auto bottom_condition = z_max.array() < total_min_z + this->params.FIRST_LAY_H;
        float bottom = bottom_condition.select(areas, 0).sum();
        costs.bottom = bottom;

        // filter overhang
        Eigen::VectorXf normal_projection(normals.rows(), 1);// = this->normals.dot(orientation);
        for (size_t i = 0; i < normals.rows(); i++)
        {
            normal_projection(i) = normals.row(i).dot(orientation);
        }
        auto overhang_areas = ((normal_projection.array() < params.ASCENT) * (!bottom_condition)).select(areas, 0);
        Eigen::MatrixXf inner = normal_projection.array() - params.ASCENT;
        inner = inner.cwiseMin(0).cwiseAbs();
        if (min_volume)
        {
            Eigen::MatrixXf heights = z_mean.array() - total_min_z;
            costs.overhang = (heights.array()* overhang_areas.array()*inner.array()).sum();
        }
        else {
            costs.overhang = 2 * (overhang_areas.array() * inner.array()).cwiseAbs2().sum();
        }

        {
            // contour perimeter
            //cost.contour = 4 * sqrt(bottom); // the simple way for contour
            float contour = 0;
            const auto& facets = mesh->stl.facet_start;
            int face_count = facets.size();
            int contour_amout = 0;
            for (size_t i = 0; i < face_count; i++)
            {
                if (bottom_condition(i)) {
                    Eigen::VectorXi index = argsort(z_projected.row(i));
                    stl_vertex line = facets[i].vertex[index(0)] - facets[i].vertex[index(1)];
                    contour += line.norm();
                    contour_amout++;
                }
            }
            costs.contour += contour + params.CONTOUR_AMOUNT * contour_amout;
        }

        // bottom of convex hull
        costs.bottom_hull = (z_max_hull.array()< total_min_z + this->params.FIRST_LAY_H).select(areas_hull, 0).sum();

        // low angle faces
        auto normal_projection_abs = normal_projection.cwiseAbs();
        Eigen::MatrixXf laf_areas = ((normal_projection_abs.array() < params.LAF_MAX) * (normal_projection_abs.array() > params.LAF_MIN) * (z_max.array() > total_min_z + params.FIRST_LAY_H)).select(areas, 0);
        costs.area_laf = laf_areas.sum();

        // volume
        if(mesh->stl.stats.volume<0)
            stl_calculate_volume(&(mesh->stl));
        costs.volume = mesh->stl.stats.volume;

        return costs;
    }

    float target_function(CostItems& costs, bool min_volume)
    {
        float cost=0;
        if (min_volume)
        {
            float overhang = costs.overhang / 25;
            cost = params.TAR_A * (overhang + params.TAR_B) + params.RELATIVE_F * (/*costs.volume/100*/overhang + params.TAR_C) / (params.TAR_D + params.CONTOUR_F * costs.contour + params.BOTTOM_F * costs.bottom + params.BOTTOM_HULL_F*costs.bottom_hull + params.TAR_E * overhang + params.TAR_PROJ_AREA*costs.area_projected);
        }
        else {
            float overhang = costs.overhang;
            cost = params.TAR_A * (overhang + params.TAR_B) + params.RELATIVE_F * (costs.overhang + params.TAR_C) / (params.TAR_D + params.CONTOUR_F * costs.contour + params.BOTTOM_F * costs.bottom + params.BOTTOM_HULL_F * costs.bottom_hull + params.TAR_PROJ_AREA * costs.area_projected);
        }

        if (params.use_low_angle_face) {
            cost += params.TAR_LAF * costs.area_laf;
        }

        costs.unprintability = cost;

        return cost;
    }
};

void get_axis_angle_from_two_vectors(Vec3f bestside, Vec3f& rotation_axis, float& phi)
{
    Vec3f z_axis(0, 0, 1);
    if ((bestside + z_axis).isMuchSmallerThan(0.001))
    {
        rotation_axis << 1, 0, 0;
        phi = M_PI;
    }
    else if ((bestside - z_axis).isMuchSmallerThan(0.001)) {
        rotation_axis << 1, 0, 0;
        phi = 0;
    }
    else {
        rotation_axis = z_axis.cross(bestside);
        rotation_axis.normalize();
        phi = acos(std::min(z_axis.dot(bestside), 1.f));
    }
}


void _orient(OrientMeshs& meshs_,
        const OrientParams           &params,
        std::function<void(unsigned, std::string)> progressfn,
        std::function<bool()>         stopfn)
{
    for (auto& mesh_ : meshs_) {
        auto progressfn_i = [&](unsigned cnt) {progressfn(cnt, "Orienting " + mesh_.name); };
        AutoOrienter orienter(&mesh_.mesh, params, progressfn_i, stopfn);

        mesh_.orientation = orienter.process();

        get_axis_angle_from_two_vectors(mesh_.orientation, mesh_.axis, mesh_.angle);

        BOOST_LOG_TRIVIAL(error) << std::fixed << std::setprecision(3) << "v,phi: " << mesh_.axis.transpose() <<", "<<mesh_.angle;
        flush_logs();
    }
}

void orient(OrientMeshs &      arrangables,
             const OrientMeshs &excludes,
             const OrientParams &  params)
{
    
    auto &cfn = params.stopcondition;
    auto &pri = params.progressind;
    
    _orient(arrangables, params, pri, cfn);
    
}

void orient(ModelObject* obj)
{
    auto m = obj->mesh();
    AutoOrienter orienter(&m);
    Vec3f orientation = orienter.process();
    Vec3f axis;
    float angle;
    get_axis_angle_from_two_vectors(orientation, axis, angle);

    auto axisd = -axis.cast<double>();
    double angled = angle;
    obj->rotate(angled, axisd);
    obj->ensure_on_bed(false);
}


} // namespace arr
} // namespace Slic3r
