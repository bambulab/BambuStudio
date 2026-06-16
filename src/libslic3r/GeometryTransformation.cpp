#include "Geometry.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>

namespace Slic3r { namespace Geometry {

static Transform3d transformation_translation_transform(const Vec3d &translation)
{
    Transform3d transform = Transform3d::Identity();
    transform.translate(translation);
    return transform;
}

static Transform3d transformation_rotation_transform(const Vec3d &rotation)
{
    Transform3d transform = Transform3d::Identity();
    transform.rotate(Eigen::AngleAxisd(rotation.z(), Vec3d::UnitZ()) * Eigen::AngleAxisd(rotation.y(), Vec3d::UnitY()) *
                     Eigen::AngleAxisd(rotation.x(), Vec3d::UnitX()));
    return transform;
}

static void transformation_scale_transform(Transform3d &transform, const Vec3d &scale)
{
    transform = Transform3d::Identity();
    transform.scale(scale);
}

static Transform3d transformation_scale_transform(const Vec3d &scale)
{
    Transform3d transform;
    transformation_scale_transform(transform, scale);
    return transform;
}

static Transform3d transformation_scale_transform(double scale)
{
    return transformation_scale_transform(scale * Vec3d::Ones());
}

static Vec3d transformation_extract_euler_angles(const Eigen::Matrix<double, 3, 3, Eigen::DontAlign> &rotation_matrix)
{
    Vec3d angles = rotation_matrix.eulerAngles(2, 1, 0);
    std::swap(angles(0), angles(2));
    return angles;
}

static Vec3d transformation_extract_euler_angles(const Transform3d &transform)
{
    Eigen::Matrix<double, 3, 3, Eigen::DontAlign> matrix = transform.matrix().block(0, 0, 3, 3);
    matrix.col(0).normalize();
    matrix.col(1).normalize();
    matrix.col(2).normalize();
    return transformation_extract_euler_angles(matrix);
}

static Vec3d transformation_extract_rotation(const Transform3d &transform)
{
    return transformation_extract_euler_angles(transform);
}

static Transform3d transformation_extract_rotation_matrix(const Transform3d &trafo)
{
    Matrix3d rotation;
    Matrix3d scale;
    trafo.computeRotationScaling(&rotation, &scale);
    return Transform3d(rotation);
}

static std::pair<Transform3d, Transform3d> transformation_extract_rotation_scale(const Transform3d &trafo)
{
    Matrix3d rotation;
    Matrix3d scale;
    trafo.computeRotationScaling(&rotation, &scale);
    return { Transform3d(rotation), Transform3d(scale) };
}

static Transform3d transformation_extract_scale(const Transform3d &trafo)
{
    Matrix3d rotation;
    Matrix3d scale;
    trafo.computeRotationScaling(&rotation, &scale);
    return Transform3d(scale);
}

static bool transformation_contains_skew(const Transform3d &trafo)
{
    Matrix3d rotation;
    Matrix3d scale;
    trafo.computeRotationScaling(&rotation, &scale);

    if (scale.isDiagonal())
        return false;

    if (scale.determinant() >= 0.0)
        return true;

    const Matrix3d ratio = scale.cwiseQuotient(trafo.matrix().block<3, 3>(0, 0));

    auto check_skew = [&ratio](int i, int j, bool &skew) {
        if (!std::isnan(ratio(i, j)) && !std::isnan(ratio(j, i)))
            skew |= std::abs(ratio(i, j) * ratio(j, i) - 1.0) > EPSILON;
    };

    bool has_skew = false;
    check_skew(0, 1, has_skew);
    check_skew(0, 2, has_skew);
    check_skew(1, 2, has_skew);
    return has_skew;
}

Transform3d Transformation::get_offset_matrix() const { return transformation_translation_transform(get_offset()); }

const Vec3d &Transformation::get_rotation() const
{
    m_temp_rotation = transformation_extract_rotation(transformation_extract_rotation_matrix(m_matrix));
    return m_temp_rotation;
}

const Vec3d &Transformation::get_rotation_by_quaternion() const
{
    Matrix3d           rotation_matrix = m_matrix.matrix().block(0, 0, 3, 3);
    Eigen::Quaterniond quaternion(rotation_matrix);
    quaternion.normalize();
    m_temp_rotation = quaternion.matrix().eulerAngles(2, 1, 0);
    std::swap(m_temp_rotation(0), m_temp_rotation(2));
    return m_temp_rotation;
}

Transform3d Transformation::get_rotation_matrix() const { return transformation_extract_rotation_matrix(m_matrix); }

void Transformation::set_rotation_matrix(const Transform3d &rot_mat)
{
    const Vec3d offset = get_offset();
    m_matrix = rot_mat * transformation_extract_scale(m_matrix);
    m_matrix.translation() = offset;
}

void Transformation::set_rotation(const Vec3d &rotation)
{
    const Vec3d offset = get_offset();
    m_matrix = transformation_rotation_transform(rotation) * transformation_extract_scale(m_matrix);
    m_matrix.translation() = offset;
}

const Vec3d &Transformation::get_scaling_factor() const
{
    const Transform3d scale = transformation_extract_scale(m_matrix);
    m_temp_scaling_factor = { std::abs(scale(0, 0)), std::abs(scale(1, 1)), std::abs(scale(2, 2)) };
    return m_temp_scaling_factor;
}

Transform3d Transformation::get_scaling_factor_matrix() const
{
    Transform3d scale = transformation_extract_scale(m_matrix);
    scale(0, 0) = std::abs(scale(0, 0));
    scale(1, 1) = std::abs(scale(1, 1));
    scale(2, 2) = std::abs(scale(2, 2));
    return scale;
}

void Transformation::set_scaling_factor(const Vec3d &scaling_factor)
{
    assert(scaling_factor.x() > 0.0 && scaling_factor.y() > 0.0 && scaling_factor.z() > 0.0);

    const Vec3d offset = get_offset();
    m_matrix = transformation_extract_rotation_matrix(m_matrix) * transformation_scale_transform(scaling_factor);
    m_matrix.translation() = offset;
}

void Transformation::set_scaling_factor(Axis axis, double scaling_factor)
{
    assert(scaling_factor > 0.0);

    auto [rotation, scale] = transformation_extract_rotation_scale(m_matrix);
    scale(axis, axis) = scaling_factor;

    const Vec3d offset = get_offset();
    m_matrix = rotation * scale;
    m_matrix.translation() = offset;
}

const Vec3d &Transformation::get_mirror() const
{
    const Transform3d scale = transformation_extract_scale(m_matrix);
    m_temp_mirror = { scale(0, 0) / std::abs(scale(0, 0)), scale(1, 1) / std::abs(scale(1, 1)),
                      scale(2, 2) / std::abs(scale(2, 2)) };
    return m_temp_mirror;
}

Transform3d Transformation::get_mirror_matrix() const
{
    Transform3d scale = transformation_extract_scale(m_matrix);
    scale(0, 0) = scale(0, 0) / std::abs(scale(0, 0));
    scale(1, 1) = scale(1, 1) / std::abs(scale(1, 1));
    scale(2, 2) = scale(2, 2) / std::abs(scale(2, 2));
    return scale;
}

void Transformation::set_mirror(const Vec3d &mirror)
{
    Vec3d       copy(mirror);
    const Vec3d abs_mirror = copy.cwiseAbs();
    for (int i = 0; i < 3; ++i) {
        if (abs_mirror(i) == 0.0)
            copy(i) = 1.0;
        else if (abs_mirror(i) != 1.0)
            copy(i) /= abs_mirror(i);
    }

    auto [rotation, scale] = transformation_extract_rotation_scale(m_matrix);
    const Vec3d curr_scales = { scale(0, 0), scale(1, 1), scale(2, 2) };
    const Vec3d signs = curr_scales.cwiseProduct(copy);

    if (signs[0] < 0.0)
        scale(0, 0) = -scale(0, 0);
    if (signs[1] < 0.0)
        scale(1, 1) = -scale(1, 1);
    if (signs[2] < 0.0)
        scale(2, 2) = -scale(2, 2);

    const Vec3d offset = get_offset();
    m_matrix = rotation * scale;
    m_matrix.translation() = offset;
}

void Transformation::set_mirror(Axis axis, double mirror)
{
    double abs_mirror = std::abs(mirror);
    if (abs_mirror == 0.0)
        mirror = 1.0;
    else if (abs_mirror != 1.0)
        mirror /= abs_mirror;

    auto [rotation, scale] = transformation_extract_rotation_scale(m_matrix);
    const double curr_scale = scale(axis, axis);
    const double sign = curr_scale * mirror;

    if (sign < 0.0)
        scale(axis, axis) = -scale(axis, axis);

    const Vec3d offset = get_offset();
    m_matrix = rotation * scale;
    m_matrix.translation() = offset;
}

bool Transformation::has_skew() const { return transformation_contains_skew(m_matrix); }

void Transformation::reset() { m_matrix = Transform3d::Identity(); }

void Transformation::reset_rotation()
{
    const Geometry::TransformationSVD svd(*this);
    m_matrix = get_offset_matrix() * Transform3d(svd.v * svd.s * svd.v.transpose()) * svd.mirror_matrix();
}

void Transformation::reset_scaling_factor()
{
    const Geometry::TransformationSVD svd(*this);
    m_matrix = get_offset_matrix() * Transform3d(svd.u) * Transform3d(svd.v.transpose()) * svd.mirror_matrix();
}

void Transformation::reset_skew()
{
    auto new_scale_factor = [](const Matrix3d &s) {
        return std::pow(s(0, 0) * s(1, 1) * s(2, 2), 1. / 3.);
    };

    const Geometry::TransformationSVD svd(*this);
    m_matrix = get_offset_matrix() * Transform3d(svd.u) * transformation_scale_transform(new_scale_factor(svd.s)) *
               Transform3d(svd.v.transpose()) * svd.mirror_matrix();
}

const Transform3d &Transformation::get_matrix(bool dont_translate, bool dont_rotate, bool dont_scale, bool dont_mirror) const
{
    if (dont_translate == false && dont_rotate == false && dont_scale == false && dont_mirror == false)
        return m_matrix;

    Transformation reference_tran(m_matrix);
    if (dont_translate)
        reference_tran.reset_offset();
    if (dont_rotate)
        reference_tran.reset_rotation();
    if (dont_scale)
        reference_tran.reset_scaling_factor();
    if (dont_mirror)
        reference_tran.reset_mirror();
    m_temp_matrix = reference_tran.get_matrix();
    return m_temp_matrix;
}

Transform3d Transformation::get_matrix_no_offset() const
{
    Transformation copy(*this);
    copy.reset_offset();
    return copy.get_matrix();
}

Transform3d Transformation::get_matrix_no_scaling_factor() const
{
    Transformation copy(*this);
    copy.reset_scaling_factor();
    return copy.get_matrix();
}

Transformation Transformation::operator*(const Transformation &other) const { return Transformation(get_matrix() * other.get_matrix()); }

Geometry::TransformationSVD::TransformationSVD(const Transform3d &trafo)
{
    const auto &m0 = trafo.matrix().block<3, 3>(0, 0);
    mirror = m0.determinant() < 0.0;

    Matrix3d m;
    if (mirror)
        m = m0 * Eigen::DiagonalMatrix<double, 3, 3>(-1.0, 1.0, 1.0);
    else
        m = m0;
    const Eigen::JacobiSVD<Matrix3d> svd(m, Eigen::ComputeFullU | Eigen::ComputeFullV);
    u = svd.matrixU();
    v = svd.matrixV();
    s = svd.singularValues().asDiagonal();

    scale = !s.isApprox(Matrix3d::Identity());
    anisotropic_scale = !is_approx(s(0, 0), s(1, 1)) || !is_approx(s(1, 1), s(2, 2));
    rotation = !v.isApprox(u);

    if (anisotropic_scale) {
        rotation_90_degrees = true;
        for (int i = 0; i < 3; ++i) {
            const Vec3d  row = v.row(i).cwiseAbs();
            const size_t num_zeros = is_approx(row[0], 0.) + is_approx(row[1], 0.) + is_approx(row[2], 0.);
            const size_t num_ones = is_approx(row[0], 1.) + is_approx(row[1], 1.) + is_approx(row[2], 1.);
            if (num_zeros != 2 || num_ones != 1) {
                rotation_90_degrees = false;
                break;
            }
        }

        const Matrix3d             trafo_linear = trafo.linear();
        const std::array<Vec3d, 3> axes = { Vec3d::UnitX(), Vec3d::UnitY(), Vec3d::UnitZ() };
        std::array<Vec3d, 3>       transformed_axes;
        for (int i = 0; i < 3; ++i)
            transformed_axes[i] = trafo_linear * axes[i];
        skew = std::abs(transformed_axes[0].dot(transformed_axes[1])) > EPSILON ||
               std::abs(transformed_axes[1].dot(transformed_axes[2])) > EPSILON ||
               std::abs(transformed_axes[2].dot(transformed_axes[0])) > EPSILON;
    } else {
        skew = false;
    }
}

}} // namespace Slic3r::Geometry
