#pragma once

#include "erhe_scene/node_attachment.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <string_view>

namespace editor {

// Value part of Ik_settings, separated so Properties editing can snapshot
// before/after copies for one-undo-per-edit (Ik_settings_change_operation,
// the Material_change_operation pattern).
//
// Parameters are per local axis x = 0, y = 1, z = 2. lock wins over limit
// on the same axis. Limits are radians with min in [-pi, 0] and max in
// [0, pi], so the rest angle 0 is always legal. stiffness (0..0.99) is
// serialized but inert in this slice (no UI, no solver enforcement yet).
class Ik_settings_data
{
public:
    std::array<bool, 3> lock     {false, false, false};
    std::array<bool, 3> limit    {false, false, false};
    glm::vec3           limit_min{-glm::pi<float>(), -glm::pi<float>(), -glm::pi<float>()};
    glm::vec3           limit_max{ glm::pi<float>(),  glm::pi<float>(),  glm::pi<float>()};
    glm::vec3           stiffness{0.0f, 0.0f, 0.0f};

    // Reference orientation defining the zero of the limits: the limited
    // quantity is inverse(rest_rotation) * parent_from_node_rotation.
    // Captured at attachment creation (bind pose when available, else the
    // current local rotation); re-capturable from Properties.
    glm::quat           rest_rotation{1.0f, 0.0f, 0.0f, 0.0f};

    auto operator==(const Ik_settings_data&) const -> bool = default;
};

// Per-bone IK settings: per-axis DOF locks and joint rotation limits,
// enforced by the constrained IK solver via swing/twist decomposition
// relative to rest_rotation. Pure data - no runtime behavior; a bone
// without the attachment is unconstrained. Serialized per node through
// the ERHE_rig glTF extension. See doc/ik-settings-requirements.md.
class Ik_settings
    : public erhe::Item<
        erhe::Item_base,
        erhe::scene::Node_attachment,
        Ik_settings,
        erhe::Item_kind::clone_using_custom_clone_constructor
    >
{
public:
    Ik_settings(const Ik_settings&);
    Ik_settings& operator=(const Ik_settings&) = delete;
    ~Ik_settings() noexcept override;

    explicit Ik_settings(std::string_view name);
    Ik_settings(const Ik_settings& src, erhe::for_clone);

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Ik_settings"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t
    {
        return erhe::Item_type::node_attachment | erhe::Item_type::ik_settings;
    }

    Ik_settings_data data;
};

} // namespace editor
