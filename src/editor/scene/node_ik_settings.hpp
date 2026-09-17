#pragma once

#include "erhe_scene/node_attachment.hpp"
#include "erhe_property/dependency_property.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <string_view>

namespace editor {

// Effective values of an Ik_settings attachment, as the IK solver reads
// them (doc/property-system.md section 4.19: a mirror of the registered
// properties, refreshed by Ik_settings::on_property_changed).
//
// Parameters are per local axis x = 0, y = 1, z = 2. lock wins over limit
// on the same axis. Limits are radians with min in [-pi, 0] and max in
// [0, pi], so the rest angle 0 is always legal. stiffness (0..0.99) is
// serialized but inert in this slice (no solver enforcement yet).
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

    // Registered properties (doc/property-system.md section 4.19), entry
    // stored, UI group "IK". Every one inherits from the node chain (D30)
    // except rest_rotation, a per-bone pose. limit_min / limit_max are
    // radians shown in degrees, coerced per component to [-pi, 0] and
    // [0, pi]; stiffness is coerced to [0, 0.99] and developer-only (inert).
    static const erhe::property::Property<bool>      lock_x_property;
    static const erhe::property::Property<bool>      lock_y_property;
    static const erhe::property::Property<bool>      lock_z_property;
    static const erhe::property::Property<bool>      limit_x_property;
    static const erhe::property::Property<bool>      limit_y_property;
    static const erhe::property::Property<bool>      limit_z_property;
    static const erhe::property::Property<glm::vec3> limit_min_property;
    static const erhe::property::Property<glm::vec3> limit_max_property;
    static const erhe::property::Property<glm::vec3> stiffness_property;
    static const erhe::property::Property<glm::quat> rest_rotation_property;

    [[nodiscard]] static auto lock_property (int axis) -> const erhe::property::Property<bool>&;
    [[nodiscard]] static auto limit_property(int axis) -> const erhe::property::Property<bool>&;

    void on_property_changed(const erhe::property::Property_changed_args& args) override;

    // The effective values (mirror). Writers go through the setters below,
    // which write local values to the property store.
    [[nodiscard]] auto get_data() const -> const Ik_settings_data& { return m_data; }
    void set_lock         (int axis, bool value);
    void set_limit        (int axis, bool value);
    void set_limit_min    (const glm::vec3& value);
    void set_limit_max    (const glm::vec3& value);
    void set_stiffness    (const glm::vec3& value);
    void set_rest_rotation(const glm::quat& value);

private:
    void refresh_mirror();

    Ik_settings_data m_data;
};

} // namespace editor
