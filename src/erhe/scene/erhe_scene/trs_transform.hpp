#pragma once

#include "erhe_scene/transform.hpp"

#include <glm/gtc/quaternion.hpp>

namespace erhe::scene {

class Trs_transform : public Transform
{
public:
    Trs_transform         () = default;
    explicit Trs_transform(glm::mat4 m);
    Trs_transform         (glm::mat4 matrix, glm::mat4 inverse_matrix);
    explicit Trs_transform(glm::vec3 translation);
    explicit Trs_transform(glm::quat rotation);
    Trs_transform         (glm::vec3 translation, glm::quat rotation);
    Trs_transform         (glm::vec3 translation, glm::quat rotation, glm::vec3 scale);
    Trs_transform         (glm::vec3 translation, glm::quat rotation, glm::vec3 scale, glm::vec3 skew);
    Trs_transform         (const Trs_transform& t);
    auto operator=        (const Trs_transform& t) -> Trs_transform&;

    [[nodiscard]] static auto inverse(const Trs_transform& transform) -> Trs_transform;

    void translate_by(glm::vec3 translation);
    void rotate_by   (glm::quat rotation);
    void scale_by    (glm::vec3 scale);

    void set_translation             (glm::vec3 translation);
    void set_rotation                (glm::quat rotation);
    void set_scale                   (glm::vec3 scale);
    void set_skew                    (glm::vec3 skew);
    void set_translation_and_rotation(glm::vec3 translation, glm::quat rotation);
    void set_trs                     (glm::vec3 translation, glm::quat rotation, glm::vec3 scale);
    void set                         (glm::vec3 translation, glm::quat rotation, glm::vec3 scale, glm::vec3 skew);

    void set(const glm::mat4& matrix);
    void set(const glm::mat4& matrix, const glm::mat4& inverse_matrix);

    [[nodiscard]] auto get_scale      () const -> glm::vec3{ return m_scale;       }
    [[nodiscard]] auto get_rotation   () const -> glm::quat{ return m_rotation;    }
    [[nodiscard]] auto get_translation() const -> glm::vec3{ return m_translation; }
    [[nodiscard]] auto get_skew       () const -> glm::vec3{ return m_skew;        }

private:
    void compose  ();
    void decompose();

    glm::vec3 m_translation{0.0f};
    glm::quat m_rotation   {1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 m_scale      {1.0f};
    glm::vec3 m_skew       {0.0f};
};

[[nodiscard]] auto translate  (const Trs_transform& t, glm::vec3 translation)             -> Trs_transform;
[[nodiscard]] auto rotate     (const Trs_transform& t, glm::quat rotation)                -> Trs_transform;
[[nodiscard]] auto scale      (const Trs_transform& t, glm::vec3 scale)                   -> Trs_transform;
[[nodiscard]] auto interpolate(const Trs_transform& t0, const Trs_transform& t1, float t) -> Trs_transform;

auto operator* (const Trs_transform& lhs, const Trs_transform& rhs) -> Trs_transform;
auto operator==(const Trs_transform& lhs, const Trs_transform& rhs) -> bool;
auto operator!=(const Trs_transform& lhs, const Trs_transform& rhs) -> bool;

} // namespace erhe::scene

