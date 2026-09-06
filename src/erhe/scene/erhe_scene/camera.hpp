#pragma once

#include "erhe_math/viewport.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/projection.hpp"
#include "erhe_scene/transform.hpp"
#include "erhe_property/dependency_property.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace erhe::scene {

class Camera;

class Camera_projection_transforms
{
public:
    Transform clip_from_camera;
    Transform clip_from_world;
};

// A camera prim (doc/usd-compatibility-plan.md C5, USD `Camera`): an
// `Xformable` with its own transform, name and children, and a child prim of
// its parent.
class Camera : public erhe::Item<Item_base, Xformable, Camera, erhe::Item_kind::clone_using_custom_clone_constructor>
{
public:
    Camera();
    explicit Camera(const Camera&);
    Camera& operator=(const Camera&) = delete;
    ~Camera() noexcept override;

    explicit Camera(std::string_view name);
    Camera(const Camera&, erhe::for_clone);

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Camera"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return Xformable::get_static_type() | erhe::Item_type::camera; }

    // Overrides Typed: the class fixes the token.
    [[nodiscard]] auto get_class_type_name() const -> std::string_view override { return "Camera"; }

    // Overrides Xformable: registers / unregisters the camera with the scene
    // on top of the node registration the base does.
    void handle_item_host_update(erhe::Item_host* old_item_host, erhe::Item_host* new_item_host) override;

    // Public API
    // The effective projection: a mirror of the projection properties
    // (local, style or inherited) kept current by on_property_changed, so
    // the renderers read a plain struct. Writers go through the setters.
    [[nodiscard]] auto projection           () const -> const Projection*;
    // Sets every projection property as a local value.
    void set_projection(const Projection& projection);
    [[nodiscard]] auto projection_transforms(
        const erhe::math::Viewport&               viewport,
        bool                                      reverse_depth,
        erhe::math::Depth_range                   depth_range,
        const erhe::math::Coordinate_conventions& conventions = erhe::math::Coordinate_conventions{}
    ) const -> Camera_projection_transforms;
    [[nodiscard]] auto get_exposure         () const -> float { return get_value(exposure_property); }
    [[nodiscard]] auto get_shadow_range     () const -> float { return get_value(shadow_range_property); }
    [[nodiscard]] auto get_projection_scale () const -> float;
    void set_exposure    (float value) { set_value(exposure_property, value); }
    void set_shadow_range(float value) { set_value(shadow_range_property, value); }
    void set_projection_type(Projection::Type value) { set_value(projection_type_property, value); }
    void set_fov_x         (float value) { set_value(fov_x_property,          value); }
    void set_fov_y         (float value) { set_value(fov_y_property,          value); }
    void set_fov_left      (float value) { set_value(fov_left_property,       value); }
    void set_fov_right     (float value) { set_value(fov_right_property,      value); }
    void set_fov_up        (float value) { set_value(fov_up_property,         value); }
    void set_fov_down      (float value) { set_value(fov_down_property,       value); }
    void set_ortho_left    (float value) { set_value(ortho_left_property,     value); }
    void set_ortho_width   (float value) { set_value(ortho_width_property,    value); }
    void set_ortho_bottom  (float value) { set_value(ortho_bottom_property,   value); }
    void set_ortho_height  (float value) { set_value(ortho_height_property,   value); }
    void set_frustum_left  (float value) { set_value(frustum_left_property,   value); }
    void set_frustum_right (float value) { set_value(frustum_right_property,  value); }
    void set_frustum_bottom(float value) { set_value(frustum_bottom_property, value); }
    void set_frustum_top   (float value) { set_value(frustum_top_property,    value); }
    void set_z_near        (float value) { set_value(z_near_property,         value); }
    void set_z_far         (float value) { set_value(z_far_property,          value); }
    void set_infinite_z_far(bool  value) { set_value(infinite_z_far_property, value); }

    // Registered properties (erhe::property, doc/property-system.md
    // section 4.4), all in the entry store and all inherits: a camera
    // without a local value reads its node chain (D30), and a style or an
    // empty node holds them by qualified name (Camera.fov_y).
    static const erhe::property::Property<Projection::Type> projection_type_property;
    static const erhe::property::Property<float>            fov_x_property;
    static const erhe::property::Property<float>            fov_y_property;
    static const erhe::property::Property<float>            fov_left_property;
    static const erhe::property::Property<float>            fov_right_property;
    static const erhe::property::Property<float>            fov_up_property;
    static const erhe::property::Property<float>            fov_down_property;
    static const erhe::property::Property<float>            ortho_left_property;
    static const erhe::property::Property<float>            ortho_width_property;
    static const erhe::property::Property<float>            ortho_bottom_property;
    static const erhe::property::Property<float>            ortho_height_property;
    static const erhe::property::Property<float>            frustum_left_property;
    static const erhe::property::Property<float>            frustum_right_property;
    static const erhe::property::Property<float>            frustum_bottom_property;
    static const erhe::property::Property<float>            frustum_top_property;
    static const erhe::property::Property<float>            z_near_property;
    static const erhe::property::Property<float>            z_far_property;
    static const erhe::property::Property<bool>             infinite_z_far_property;
    static const erhe::property::Property<float>            exposure_property;
    static const erhe::property::Property<float>            shadow_range_property;

private:
    // Overrides Dependency_object: mirrors the effective projection values
    // into m_projection.
    void on_property_changed(const erhe::property::Property_changed_args& args) override;
    void refresh_projection_mirror();

    Projection m_projection;
};

// The one camera of a prim: the prim itself when it is a Camera, else its
// first Camera child.
[[nodiscard]] auto get_camera(const std::shared_ptr<erhe::Item_base>& item) -> std::shared_ptr<Camera>;
[[nodiscard]] auto get_camera(const erhe::Hierarchy* item) -> std::shared_ptr<Camera>;

} // namespace erhe::scene
