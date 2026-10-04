#pragma once

// Texture_reference and Texture_reference_user without Texture itself:
// a class that resolves to a texture (a render graph node, a texture graph
// output) or watches one (a material) derives from these and needs only a
// forward-declared Texture, not texture.hpp with the erhe_item headers
// behind it.

namespace erhe::graphics {

class Texture;

// A holder of a Texture_reference that must learn when the texture the
// reference resolves to becomes a different object without the holder
// writing anything - a texture graph bake landing a new output texture
// behind the reference a material slot holds.
class Texture_reference_user
{
public:
    virtual ~Texture_reference_user() noexcept;
    virtual void on_referenced_texture_changed() = 0;
};

class Texture_reference
{
public:
    virtual ~Texture_reference() noexcept;
    [[nodiscard]] virtual auto get_referenced_texture() const -> const Texture* = 0;

    // A reference that always resolves to the same texture (a Texture is
    // itself) keeps no users, so the defaults do nothing. A reference whose
    // resolved texture changes over its lifetime overrides these, keeps the
    // registered users, and calls on_referenced_texture_changed() on each
    // when the texture it resolves to changes.
    virtual void add_user   (Texture_reference_user& user) { static_cast<void>(user); }
    virtual void remove_user(Texture_reference_user& user) { static_cast<void>(user); }
};

} // namespace erhe::graphics
