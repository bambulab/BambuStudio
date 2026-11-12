#pragma once

#include <cstddef>

#if defined(_WIN32)
  #define BAMBU_OMC_CALL __cdecl
#else
  #define BAMBU_OMC_CALL
#endif

extern "C" {

struct BambuOMCPoint
{
    double x;
    double y;
    double z;
};

struct BambuOMCTriangle
{
    size_t vertices[3];
};

struct BambuOMCMesh
{
    const BambuOMCPoint    *points;
    size_t                  point_count;
    const BambuOMCTriangle *triangles;
    size_t                  triangle_count;
};

// One operand: the union of its meshes, e.g. the parts of one object.
struct BambuOMCOperand
{
    const BambuOMCMesh *meshes;
    size_t              mesh_count;
};

enum BambuOMCOperation
{
    BAMBU_OMC_UNION,
    BAMBU_OMC_INTERSECTION,
    BAMBU_OMC_SUBTRACTION
};

using BambuOMCResultCallback = void(BAMBU_OMC_CALL *)(
    const BambuOMCPoint *,
    size_t,
    const BambuOMCTriangle *,
    size_t,
    void *);

// Boolean of any number of operands in one pass. Union: inside any operand.
// Intersection: inside every operand. Subtraction: inside the first operand and
// outside all others.
// Returns true on success. The result callback is invoked synchronously.
bool BAMBU_OMC_CALL bambu_omc_boolean_operands(
    const BambuOMCOperand    *operands,
    size_t                    operand_count,
    BambuOMCOperation         operation,
    BambuOMCResultCallback    result_callback,
    void                     *user_data,
    char                     *error_message,
    size_t                    error_message_capacity);

// Two single-mesh operands; same as bambu_omc_boolean_operands.
bool BAMBU_OMC_CALL bambu_omc_boolean(
    const BambuOMCMesh       *first,
    const BambuOMCMesh       *second,
    BambuOMCOperation         operation,
    BambuOMCResultCallback    result_callback,
    void                     *user_data,
    char                     *error_message,
    size_t                    error_message_capacity);

} // extern "C"

