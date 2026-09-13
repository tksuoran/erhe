#pragma once

#include "erhe_scene/boundable.hpp"

#include <cstdint>
#include <string_view>

#include <glm/glm.hpp>

namespace erhe::property {
    class Dependency_object;
    class Property_changed_args;
}

namespace erhe::scene {

// A geometric prim (doc/usd-compatibility-plan.md C5, USD `UsdGeomGprim`):
// the level of the prim class hierarchy that draws geometry, and so the
// level `doubleSided` belongs to.
//
// The level is never instantiated on its own: it exists so `Mesh` and its
// siblings sit below it.
class Gprim
    : public erhe::Item<
        Item_base,
        Boundable,
        Gprim,
        erhe::Item_kind::clone_using_custom_clone_constructor
    >
{
public:
    Gprim();
    explicit Gprim(const Gprim& src);
    Gprim& operator=(const Gprim& src);
    explicit Gprim(std::string_view name);
    Gprim(const Gprim& src, for_clone);
    ~Gprim() noexcept override;

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Gprim"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t
    {
        return Boundable::get_static_type() | erhe::Item_type::gprim;
    }

    // USD `UsdGeomGprim.doubleSided`: the prim asks to be drawn from both
    // sides whatever its material says. The renderers take a primitive as
    // double sided when either this or the material's own `double_sided`
    // (glTF `material.doubleSided`) asks for it - see is_double_sided()
    // in mesh.hpp, the one place that rule is spelled.
    static const erhe::property::Property<bool> double_sided_property;

    [[nodiscard]] auto get_double_sided() const -> bool { return get_value(double_sided_property); }
    void               set_double_sided(bool value)     { set_value(double_sided_property, value); }

    // USD `UsdGeomGprim.primvars:displayColor` authored at constant
    // interpolation: the one color of the whole surface. A `displayColor`
    // that varies is vertex color data and stays in the geometry; this is
    // the value a prim - or an `over` of one inside a variant - authors for
    // the surface as a whole, so it is a property like every other one and
    // an override of it is written and read like every other one. The
    // default is USD's own fallback grey; the value is authored exactly when
    // the item has a local one.
    static const erhe::property::Property<glm::vec3> display_color_property;

    static constexpr glm::vec3 default_display_color{0.18f, 0.18f, 0.18f};

    [[nodiscard]] auto get_display_color() const -> glm::vec3 { return get_value(display_color_property); }
    void               set_display_color(glm::vec3 value)     { set_value(display_color_property, value); }

protected:
    // A change of a Gprim render-state property reaches the geometry that
    // draws it. Gprim itself draws nothing, so the level does nothing; Mesh
    // overrides this to re-register its draw list entries.
    virtual void handle_gprim_render_state_changed() {}

    // The display color reaches the geometry differently: the renderers read
    // a mesh's own color out of its vertex data, so the value has to be built
    // into the mesh rather than re-registered. Gprim itself draws nothing;
    // Mesh overrides this to have its primitives rebuilt with the color.
    virtual void handle_gprim_display_color_changed() {}

private:
    static void on_render_state_property_changed(
        erhe::property::Dependency_object&           object,
        const erhe::property::Property_changed_args& args
    );
    static void on_display_color_property_changed(
        erhe::property::Dependency_object&           object,
        const erhe::property::Property_changed_args& args
    );
};

} // namespace erhe::scene
