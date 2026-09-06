#pragma once

#include "erhe_item/typed.hpp"

#include <cstdint>
#include <string_view>

namespace erhe::scene {

// An imageable prim (doc/usd-compatibility-plan.md C5, USD
// `UsdGeomImageable`): the level of the prim class hierarchy that renders,
// and so the level `visible` and `purpose` belong to. Both are registered on
// `Item_base` today and stay there until a step moves them here.
//
// The level is never instantiated on its own: it exists so `Xformable` and,
// through it, every transformable prim sits below it, and so a consumer can
// ask `is<Imageable>(item)`.
class Imageable : public erhe::Item<erhe::Item_base, erhe::Typed, Imageable>
{
public:
    Imageable();
    explicit Imageable(const Imageable& src);
    Imageable& operator=(const Imageable& src);
    explicit Imageable(std::string_view name);
    Imageable(const Imageable& src, erhe::for_clone);
    ~Imageable() noexcept override;

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Imageable"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t
    {
        return erhe::Item_type::typed | erhe::Item_type::imageable;
    }
};

} // namespace erhe::scene
