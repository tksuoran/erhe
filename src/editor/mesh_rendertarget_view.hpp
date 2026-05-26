#pragma once

#include "rendertarget_view.hpp"

#include <memory>

namespace erhe::scene { class Node; }

namespace editor {

class Rendertarget_mesh;

// Rendertarget_view backed by a Rendertarget_mesh placed in the scene graph,
// used by the standalone scene rendertarget that Scene_commands creates. This
// is always the scene-mesh path (there is no OpenXR composition layer for an
// arbitrary scene rendertarget). Holds shared ownership of the mesh and node so
// the view stays valid for the lifetime of the owning Rendertarget_imgui_host.
class Mesh_rendertarget_view : public Rendertarget_view
{
public:
    Mesh_rendertarget_view(
        std::shared_ptr<Rendertarget_mesh> mesh,
        std::shared_ptr<erhe::scene::Node> node
    );
    ~Mesh_rendertarget_view() noexcept override;

    // Implements Rendertarget_view
    [[nodiscard]] auto get_width () const -> float override;
    [[nodiscard]] auto get_height() const -> float override;
    [[nodiscard]] auto acquire_render_pass(erhe::graphics::Command_buffer& command_buffer) -> erhe::graphics::Render_pass* override;
    void               finish_render      (erhe::graphics::Command_buffer& command_buffer, App_context& context) override;
    [[nodiscard]] auto get_output_texture () const -> std::shared_ptr<erhe::graphics::Texture> override;
    [[nodiscard]] auto is_quad_visible       () const -> bool override;
    [[nodiscard]] auto get_pointer           () const -> std::optional<glm::vec2> override;
    [[nodiscard]] auto world_to_window       (glm::vec3 world_position) const -> std::optional<glm::vec2> override;
    [[nodiscard]] auto get_plane_world_origin() const -> glm::vec3 override;
    [[nodiscard]] auto get_plane_world_normal() const -> glm::vec3 override;
#if defined(ERHE_XR_LIBRARY_OPENXR)
    void update_headset_hand_tracking() override;
#endif

private:
    std::shared_ptr<Rendertarget_mesh> m_mesh;
    std::shared_ptr<erhe::scene::Node> m_node;
};

} // namespace editor
