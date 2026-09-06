#pragma once

#include "erhe_scene/node.hpp"

#include <cstdint>
#include <string_view>

namespace erhe::scene {

// An `Xform` prim (doc/usd-compatibility-plan.md C5, USD `Xform`): a
// transform with children and nothing else. It is the prim every
// node-creation path makes - the Create menu, the MCP `create_node` tool and
// the import of a transform-only node.
class Xform
    : public erhe::Item<
        Item_base,
        Xformable,
        Xform,
        erhe::Item_kind::clone_using_custom_clone_constructor
    >
{
public:
    Xform();
    explicit Xform(const Xform& src);
    Xform& operator=(const Xform& src);
    explicit Xform(std::string_view name);
    Xform(const Xform& src, for_clone);
    ~Xform() noexcept override;

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Xform"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t
    {
        return Xformable::get_static_type() | erhe::Item_type::xform;
    }

    // Overrides Typed: the class fixes the token.
    [[nodiscard]] auto get_class_type_name() const -> std::string_view override { return "Xform"; }
};

} // namespace erhe::scene
