#pragma once

#include <glm/glm.hpp>

#include <memory>
#include <optional>

namespace erhe::graphics {
    class Command_buffer;
    class Render_pass;
    class Texture;
}

namespace editor {

class App_context;

// Abstract target that a Rendertarget_imgui_host renders ImGui into and queries
// for pointer / picking geometry. This decouples the host from how the UI quad
// is presented:
//   - Quad_view              -- a tool UI quad, scene-mesh (Path A) or OpenXR
//                               quad composition layer (Path B).
//   - Mesh_rendertarget_view -- a standalone scene rendertarget mesh.
class Rendertarget_view
{
public:
    virtual ~Rendertarget_view() noexcept = default;

    [[nodiscard]] virtual auto get_width () const -> float = 0;
    [[nodiscard]] virtual auto get_height() const -> float = 0;

    // Per-frame ImGui render target. acquire_render_pass() returns the render
    // pass to draw ImGui into this frame (nullptr to skip). finish_render()
    // runs after the pass closes (e.g. generate mipmaps, or release a swapchain
    // image). get_output_texture() is the produced texture.
    [[nodiscard]] virtual auto acquire_render_pass(erhe::graphics::Command_buffer& command_buffer) -> erhe::graphics::Render_pass* = 0;
    virtual void               finish_render      (erhe::graphics::Command_buffer& command_buffer, App_context& context) = 0;
    [[nodiscard]] virtual auto get_output_texture () const -> std::shared_ptr<erhe::graphics::Texture> = 0;

    // Pointer / picking sources used to drive the ImGui mouse from the desktop
    // pointer or the VR controller ray.
    [[nodiscard]] virtual auto is_quad_visible       () const -> bool = 0;
    [[nodiscard]] virtual auto get_pointer           () const -> std::optional<glm::vec2> = 0;
    [[nodiscard]] virtual auto world_to_window       (glm::vec3 world_position) const -> std::optional<glm::vec2> = 0;
    [[nodiscard]] virtual auto get_plane_world_origin() const -> glm::vec3 = 0;
    [[nodiscard]] virtual auto get_plane_world_normal() const -> glm::vec3 = 0;

#if defined(ERHE_XR_LIBRARY_OPENXR)
    virtual void update_headset_hand_tracking() = 0;
#endif
};

} // namespace editor
