#pragma once

#include "erhe_scene/boundable.hpp"

#include <cstdint>
#include <string_view>

namespace erhe::scene {

// A `PointInstancer` prim (doc/usd-compatibility-plan.md S1, USD
// `UsdGeomPointInstancer`): a boundable prim whose children are the
// prototypes it instances and one prim per instance.
//
// erhe expands an instancer into prims: every instance is a child `Xform`
// carrying an internal reference to one of the prototypes, so an instance is
// a prefab instance like any other reference and its transform is the prim's
// own. The children are therefore the whole of the instancer: `positions`,
// `orientations` and `scales` are the instance prims' transforms and
// `protoIndices` is the prototype each one references, all four recomputed
// from the tree when the prim is written. The class holds no array of its
// own, so nothing can drift out of step with the children.
//
// The prototypes are the instancer's children that are not instances, in
// tree order, which is the order `prototypes` is written in.
class Point_instancer
    : public erhe::Item<
        Item_base,
        Boundable,
        Point_instancer,
        erhe::Item_kind::clone_using_custom_clone_constructor
    >
{
public:
    Point_instancer();
    explicit Point_instancer(const Point_instancer& src);
    Point_instancer& operator=(const Point_instancer& src);
    explicit Point_instancer(std::string_view name);
    Point_instancer(const Point_instancer& src, for_clone);
    ~Point_instancer() noexcept override;

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Point_instancer"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t
    {
        return Boundable::get_static_type() | erhe::Item_type::point_instancer;
    }

    // Overrides Typed: the class fixes the token. USD spells the schema
    // `PointInstancer`; the erhe class name is the erhe spelling of it.
    [[nodiscard]] auto get_class_type_name() const -> std::string_view override { return "PointInstancer"; }
};

} // namespace erhe::scene
