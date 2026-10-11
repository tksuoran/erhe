#pragma once

#include "erhe_graphics/texture_reference.hpp"

#include <functional>

namespace erhe::graphics { class Texture; }

namespace editor {

// A texture the render phase creates, replaces or releases (the DDGI atlases,
// the radiance cascades atlas preview, the ray trace output), shown by an
// ImGui window: the window passes this reference instead of the texture,
// and the ImGui renderer resolves it when the render thread draws the
// window's draw data, after this frame's render phase updated the texture
// (doc/editor/editor.md "Frame phases and the render thread"). The resolver
// runs on the render thread only and returns the texture the owner holds
// then, or null (the ImGui renderer then draws its dummy texture).
//
// The window constructs the reference once and keeps it (shared, as the
// ImGui renderer keeps the references of the frames it draws); the resolver
// captures the owner, which outlives the window.
class Render_texture_reference : public erhe::graphics::Texture_reference
{
public:
    explicit Render_texture_reference(std::function<const erhe::graphics::Texture*()> resolve);
    ~Render_texture_reference() noexcept override;

    auto get_referenced_texture() const -> const erhe::graphics::Texture* override;

private:
    std::function<const erhe::graphics::Texture*()> m_resolve;
};

} // namespace editor
