#include "erhe/scene/trs_transform.hpp"
#include "erhe/graphics/instance.hpp"
#include "erhe/toolkit/math_util.hpp"

#include <glm/gtx/matrix_decompose.hpp>

namespace erhe::scene
{

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
    glm::vec4 perspective;
    glm::decompose(
        m_matrix,
        m_scale,
        m_rotation,
        m_translation,
        m_skew,
        perspective
    );
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
    m_matrix         = erhe::toolkit::compose        (m_scale, m_rotation, m_translation, m_skew);
    m_inverse_matrix = erhe::toolkit::compose_inverse(m_scale, m_rotation, m_translation, m_skew);
}

auto translate(
    const Trs_transform& t,
    const glm::vec3      translation
) -> Trs_transform
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

auto rotate(
    const Trs_transform& t,
    const glm::quat      rotation
) -> Trs_transform
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

auto scale(
    const Trs_transform& t,
    const glm::vec3      scale
) -> Trs_transform
{
    return Trs_transform{
        t.get_translation(),
        t.get_rotation(),
        scale * t.get_scale(),
        t.get_skew()
    };
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

void Trs_transform::set_translation_and_rotation(
    const glm::vec3 translation,
    const glm::quat rotation
)
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
