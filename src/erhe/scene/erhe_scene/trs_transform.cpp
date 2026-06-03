#include "erhe_scene/trs_transform.hpp"
#include "erhe_math/math_util.hpp"

#include <glm/gtx/matrix_decompose.hpp>

namespace erhe::scene {

using glm::vec3;
using glm::mat4;

Trs_transform::Trs_transform(const Trs_transform& t) = default;

auto Trs_transform::operator=(const Trs_transform& t) -> Trs_transform& = default;

Trs_transform::Trs_transform(const mat4 m)
    : Transform{m}
{
    decompose();
}

void Trs_transform::decompose()
{
    const glm::quat previous_rotation = m_rotation;
    const glm::vec3 previous_skew     = m_skew;

    glm::vec4 perspective;
    glm::decompose(
        m_matrix,
        m_scale,
        m_rotation,
        m_translation,
        m_skew,
        perspective
    );

    // A rank-deficient matrix (a zero, or near-zero, scale component) cannot be decomposed
    // reliably: glm::decompose orthogonalizes the basis (Gram-Schmidt) to extract rotation,
    // and when one column is ~zero that process yields an arbitrary rotation AND corrupts the
    // OTHER axes' scale (e.g. a collapsed X axis poisons the Y/Z scale). Detect this from the
    // raw column lengths and, in that case, take the scale directly from those lengths
    // (uncoupled, robust) and keep the prior rotation/skew (which the matrix can no longer
    // provide). This lets an object collapse to (near) zero on one axis and be scaled back up
    // without its other axes' scale or its orientation being corrupted in the meantime.
    constexpr float scale_epsilon = 1e-6f;
    const glm::vec3 column_lengths{
        glm::length(glm::vec3{m_matrix[0]}),
        glm::length(glm::vec3{m_matrix[1]}),
        glm::length(glm::vec3{m_matrix[2]})
    };
    const bool degenerate =
        (column_lengths.x < scale_epsilon) ||
        (column_lengths.y < scale_epsilon) ||
        (column_lengths.z < scale_epsilon);
    if (degenerate) {
        m_scale    = column_lengths;
        m_rotation = previous_rotation;
        m_skew     = previous_skew;
    }
}

Trs_transform::Trs_transform(const mat4 matrix, const mat4 inverse_matrix)
    : Transform{matrix, inverse_matrix}
{
    decompose();
}

Trs_transform::Trs_transform(const glm::vec3 translation)
    : m_translation{translation}
{
    compose();
}

Trs_transform::Trs_transform(glm::quat rotation)
    : m_rotation{rotation}
{
    compose();
}

Trs_transform::Trs_transform(const glm::vec3 translation, glm::quat rotation)
    : m_translation{translation}
    , m_rotation   {rotation}
{
    compose();
}

Trs_transform::Trs_transform(const glm::vec3 translation, glm::quat rotation, glm::vec3 scale)
    : m_translation{translation}
    , m_rotation   {rotation}
    , m_scale      {scale}
{
    compose();
}

Trs_transform::Trs_transform(const glm::vec3 translation, glm::quat rotation, glm::vec3 scale, glm::vec3 skew)
    : m_translation{translation}
    , m_rotation   {rotation}
    , m_scale      {scale}
    , m_skew       {skew}
{
    compose();
}

auto Trs_transform::inverse(const Trs_transform& transform) -> Trs_transform
{
    return Trs_transform{
        transform.get_inverse_matrix(),
        transform.get_matrix()
    };
}

void Trs_transform::compose()
{
    m_matrix         = erhe::math::compose        (m_scale, m_rotation, m_translation, m_skew);
    m_inverse_matrix = erhe::math::compose_inverse(m_scale, m_rotation, m_translation, m_skew);
}

auto translate(const Trs_transform& t, const glm::vec3 translation) -> Trs_transform
{
    return Trs_transform{
        translation + t.get_translation(),
        t.get_rotation(),
        t.get_scale(),
        t.get_skew()
    };
}

void Trs_transform::translate_by(glm::vec3 translation)
{
    m_translation = translation + m_translation;
    compose();
}

auto rotate(const Trs_transform& t, const glm::quat rotation) -> Trs_transform
{
    return Trs_transform{
        t.get_translation(),
        rotation * t.get_rotation(),
        t.get_scale(),
        t.get_skew()
    };
}

void Trs_transform::rotate_by(glm::quat rotation)
{
    m_rotation = rotation * m_rotation;
    compose();
}

auto scale(const Trs_transform& t, const glm::vec3 scale) -> Trs_transform
{
    return Trs_transform{
        t.get_translation(),
        t.get_rotation(),
        scale * t.get_scale(),
        t.get_skew()
    };
}

auto interpolate(const Trs_transform& t0, const Trs_transform& t1, float t) -> Trs_transform
{
    const glm::vec3 translation = glm::mix  (t0.get_translation(), t1.get_translation(), t);
    const glm::quat rotation    = glm::slerp(t0.get_rotation(),    t1.get_rotation(), t);
    const glm::vec3 scale       = glm::mix  (t0.get_scale(),       t1.get_scale(), t);
    const glm::vec3 skew        = glm::mix  (t0.get_skew(),        t1.get_skew(), t);
    return Trs_transform{translation, rotation, scale, skew};
}

void Trs_transform::scale_by(glm::vec3 scale)
{
    m_scale = scale * m_scale;
    compose();
}

void Trs_transform::set_translation(const glm::vec3 translation)
{
    m_translation = translation;
    compose();
}

void Trs_transform::set_rotation(const glm::quat rotation)
{
    m_rotation = rotation;
    compose();
}

void Trs_transform::set_scale(const glm::vec3 scale)
{
    m_scale = scale;
    compose();
}

void Trs_transform::set_skew(const glm::vec3 skew)
{
    m_skew = skew;
    compose();
}

void Trs_transform::set_translation_and_rotation(const glm::vec3 translation, const glm::quat rotation)
{
    m_rotation    = rotation;
    m_translation = translation;
    compose();
}

void Trs_transform::set_trs(
    const glm::vec3 translation,
    const glm::quat rotation,
    const glm::vec3 scale
)
{
    m_scale       = scale;
    m_rotation    = rotation;
    m_translation = translation;
    compose();
}

void Trs_transform::set(
    const glm::vec3 scale,
    const glm::quat rotation,
    const glm::vec3 translation,
    const glm::vec3 skew
)
{
    m_scale       = scale;
    m_rotation    = rotation;
    m_translation = translation;
    m_skew        = skew;
    compose();
}

void Trs_transform::set(const mat4& matrix)
{
    m_matrix         = matrix;
    m_inverse_matrix = glm::inverse(matrix);
    decompose();

    glm::mat4 check_matrix = erhe::math::compose(m_scale, m_rotation, m_translation, m_skew);

}

void Trs_transform::set(const mat4& matrix, const mat4& inverse_matrix)
{
    m_matrix         = matrix;
    m_inverse_matrix = inverse_matrix;
    decompose();
}

auto operator*(const Trs_transform& lhs, const Trs_transform& rhs) -> Trs_transform
{
    return Trs_transform{
        lhs.get_matrix() * rhs.get_matrix(),
        rhs.get_inverse_matrix() * lhs.get_inverse_matrix()
    };
}

auto operator==(const Trs_transform& lhs, const Trs_transform& rhs) -> bool
{
    const bool matrix_equality  = lhs.get_matrix()         == rhs.get_matrix();
    const bool inverse_equality = lhs.get_inverse_matrix() == rhs.get_inverse_matrix();
    return matrix_equality && inverse_equality;
}

auto operator!=(const Trs_transform& lhs, const Trs_transform& rhs) -> bool
{
    return !(lhs == rhs);
}

} // namespace erhe::scene
