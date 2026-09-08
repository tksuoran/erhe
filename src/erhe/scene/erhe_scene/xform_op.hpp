#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>
#include <variant>
#include <vector>

namespace erhe::scene {

// USD's xformOp vocabulary (UsdGeomXformOp::Type). The name says which kind of
// operation the op is; `rotate_xyz` .. `rotate_zyx` name the order the three
// Euler angles are applied in, first axis first.
enum class Xform_op_type {
    translate,
    scale,
    rotate_x,
    rotate_y,
    rotate_z,
    rotate_xyz,
    rotate_xzy,
    rotate_yxz,
    rotate_yzx,
    rotate_zxy,
    rotate_zyx,
    orient,
    transform
};

// The authored value type of an op: `float3` vs `double3`, `quatf` vs `quatd`,
// `matrix4d`. The value itself is always kept in double precision, so an
// authored `double3` survives a load / edit / save round trip; the precision
// says which type name the op is written back as.
enum class Xform_op_precision {
    half_,
    float_,
    double_
};

// The value of an op, in the form its type takes:
// - `double`      : rotate_x / rotate_y / rotate_z, in degrees
// - `glm::dvec3`  : translate, scale, and rotate_xyz .. rotate_zyx (degrees)
// - `glm::dquat`  : orient
// - `glm::dmat4`  : transform, as an erhe / glm column-vector matrix (USD
//                   authors the row-vector transpose of it)
using Xform_op_value = std::variant<double, glm::dvec3, glm::dquat, glm::dmat4>;

// One time sample of an op. `time_code` is the time in the units the authoring
// file uses - USD time codes, which become seconds by dividing by the stage's
// `timeCodesPerSecond` - and `value` is the op's value there, in the same form
// Xform_op::value takes.
class Xform_op_sample
{
public:
    double         time_code{0.0};
    Xform_op_value value    {glm::dmat4{1.0}};

    [[nodiscard]] auto operator==(const Xform_op_sample& other) const -> bool;
    [[nodiscard]] auto operator!=(const Xform_op_sample& other) const -> bool;
};

// One authored `xformOp:<type>[:<suffix>]` of a prim's xformOpOrder.
class Xform_op
{
public:
    Xform_op_type      type     {Xform_op_type::transform};
    Xform_op_precision precision{Xform_op_precision::float_};
    // The `pivot` of `xformOp:translate:pivot`; empty when the op has none.
    std::string        suffix   {};
    // `!invert!` in xformOpOrder: the op contributes the inverse of its value.
    bool               inverted {false};
    Xform_op_value     value    {glm::dmat4{1.0}};
    // The op's authored time samples, in the file's own time codes and in
    // increasing time order; empty when the op is not time-sampled. `value`
    // stays the op's single value - what it composes to - so compose() and
    // the write-back need no notion of time; the samples are the authored
    // record a save writes back, and the playable projection of them is an
    // erhe::scene::Animation channel the importer builds
    // (src/erhe/usd/notes.md, "Time samples").
    std::vector<Xform_op_sample> samples{};

    // The op's contribution as a glm column-vector matrix, inversion applied.
    [[nodiscard]] auto to_matrix() const -> glm::dmat4;

    [[nodiscard]] auto operator==(const Xform_op& other) const -> bool;
    [[nodiscard]] auto operator!=(const Xform_op& other) const -> bool;
};

// A prim's authored transform: its xformOpOrder, in order.
class Xform_op_stack
{
public:
    std::vector<Xform_op> ops;
    // `!resetXformStack!` at the head of xformOpOrder: the prim ignores its
    // ancestors' transforms. Stored and round-tripped, nothing else: erhe's
    // transform propagation always composes with the parent, so compose()
    // ignores this flag.
    bool                  reset_xform_stack{false};

    // The local transform the stack composes to, as a glm column-vector
    // matrix: with xformOpOrder = [op0, op1, ..., opN] this is
    // M(op0) * M(op1) * ... * M(opN), so a point is transformed by opN first.
    // A [translate, rotate, scale] stack composes to T * R * S.
    [[nodiscard]] auto compose() const -> glm::dmat4;

    // Whether any op of the stack carries time samples.
    [[nodiscard]] auto has_time_samples() const -> bool;

    [[nodiscard]] auto operator==(const Xform_op_stack& other) const -> bool;
    [[nodiscard]] auto operator!=(const Xform_op_stack& other) const -> bool;
};

enum class Xform_op_write_back_result {
    written,           // the stack now composes to the requested transform
    not_representable  // no op of the stack can carry a component that changed
};

// Write a local transform back into an authored stack, so that an edit made
// through erhe's TRS lands in the op the stack designates:
//
// - translation goes to the LAST non-inverted `translate` op with no suffix
// - rotation to the LAST non-inverted `orient` or `rotate_*` op with no
//   suffix, converted to that op's form
// - scale to the LAST non-inverted `scale` op with no suffix
// - a stack that is a single non-inverted, unsuffixed `transform` op receives
//   the whole matrix
//
// Every other op keeps its value, and the stack must compose to the requested
// transform after the write. A component that changed beyond tolerance and has
// no designated op (no scale op and the scale changed, a `rotate_x` op and the
// new rotation is not about x), or a stack whose composition after the write
// is not the requested transform (the remaining ops - a pivot pair, an
// inverted op - move the result), makes the stack unable to carry the edit:
// the ops keep their authored values and `not_representable` is returned.
auto write_trs_into_xform_op_stack(
    Xform_op_stack& stack,
    glm::vec3       translation,
    glm::quat       rotation,
    glm::vec3       scale
) -> Xform_op_write_back_result;

// The Euler angles, in degrees and in the op type's own axis order, of a
// rotation. `type` must be one of rotate_xyz .. rotate_zyx.
[[nodiscard]] auto euler_degrees_from_rotation(Xform_op_type type, glm::dquat rotation) -> glm::dvec3;

// The matrix of `angles_degrees` in the op type's own axis order (first axis
// first), as a glm column-vector matrix. `type` must be one of
// rotate_xyz .. rotate_zyx.
[[nodiscard]] auto euler_matrix(Xform_op_type type, glm::dvec3 angles_degrees) -> glm::dmat4;

} // namespace erhe::scene
