#pragma once

#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>

namespace erhe::imgui {
    class Value_edit_state;
}
namespace erhe::physics {
    enum class Motion_mode : unsigned int;
}
namespace erhe::scene {
    class Mesh;
    class Node;
}

namespace editor {

class Property_editor;

class Rotation_inspector
{
public:
    enum class Representation : unsigned int {
        e_quaternion   = 0,
        e_matrix       = 1,
        e_axis_angle   = 2,
        e_euler_angles = 3,
        e_count        = 4
    };

    static constexpr const char* c_representation_strings[] = {
        "Quaternion",
        "Matrix",
        "Axis-angle",
        "Euler Angles"
    };

    enum class Euler_angle_order : unsigned int {
        e_xyz   =  0,
        e_yxz   =  1,
        e_xzx   =  2,
        e_xyx   =  3,
        e_yxy   =  4,
        e_yzy   =  5,
        e_zyz   =  6,
        e_zxz   =  7,
        e_xzy   =  8,
        e_yzx   =  9,
        e_zyx   = 10,
        e_zxy   = 11,
        e_count = 12
    };

    static constexpr const char* c_euler_strings[] = {
        // Proper euler angles
        "XYX",
        "XZX",
        "YXY",
        "YZY",
        "ZXZ",
        "ZYZ",
        // Tait-Bryan angles
        "XYZ",
        "XZY",
        "YXZ",
        "YZX",
        "ZYX",
        "ZXY"
    };

    [[nodiscard]] static auto is_proper    (Euler_angle_order euler_angle_order) -> bool;
    [[nodiscard]] static auto is_tait_bryan(Euler_angle_order euler_angle_order) -> bool;

    Rotation_inspector();

    void set_matrix        (const glm::mat3& m);
    void set_quaternion    (const glm::quat& q);
    void set_representation(Representation representation);
    void set_euler_order   (Euler_angle_order euler_angle_order);
    void set_axis_angle    (glm::vec3 axis, float angle);

    void set_active(bool active);

    void update_euler_angles_from_matrix               ();
    void update_axis_angle_from_quaternion             ();
    void update_matrix_and_quaternion_from_euler_angles();
    void update_from_axis_angle                        ();
    void update_from_quaternion                        ();

    void imgui(
        erhe::imgui::Value_edit_state& quaternion_state,
        erhe::imgui::Value_edit_state& euler_state,
        erhe::imgui::Value_edit_state& axis_angle_state,
        glm::quat                      rotation,
        bool                           matches_gizmo,
        Property_editor&               property_editor
    );

    [[nodiscard]] auto get_matrix     () -> glm::mat4;
    [[nodiscard]] auto get_quaternion () -> glm::quat;
    [[nodiscard]] auto get_euler_value(std::size_t i) const -> float;
    [[nodiscard]] auto get_axis       () const -> glm::vec3;
    [[nodiscard]] auto get_angle      () const -> float;

private:
    [[nodiscard]] auto get_label_color    (std::size_t i, bool text, bool matches_gizmo) const -> uint32_t;
    [[nodiscard]] auto get_euler_axis     (std::size_t i) const -> std::size_t;
    [[nodiscard]] auto gimbal_lock_warning() const -> float;

    float             m_euler_angles     [3];
    Representation    m_representation   {Representation::e_euler_angles};
    Euler_angle_order m_euler_angle_order{Euler_angle_order::e_zyx};
    glm::mat4         m_matrix           {1.0f};
    glm::quat         m_quaternion       {1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3         m_axis             {1.0f, 0.0f, 0.0f};
    float             m_angle            {0.0f};
    bool              m_matrix_dirty     {false};
    bool              m_active           {false};
};

} // namespace editor
