#include "mesh_rendertarget_view.hpp"

#include "rendertarget_mesh.hpp"

#include "erhe_scene/node.hpp"

#include <utility>

namespace editor {

Mesh_rendertarget_view::Mesh_rendertarget_view(
    std::shared_ptr<Rendertarget_mesh> mesh,
    std::shared_ptr<erhe::scene::Node> node
)
    : m_mesh{std::move(mesh)}
    , m_node{std::move(node)}
{
}

Mesh_rendertarget_view::~Mesh_rendertarget_view() noexcept = default;

auto Mesh_rendertarget_view::get_width() const -> float
{
    return (m_mesh != nullptr) ? m_mesh->get_width() : 0.0f;
}

auto Mesh_rendertarget_view::get_height() const -> float
{
    return (m_mesh != nullptr) ? m_mesh->get_height() : 0.0f;
}

auto Mesh_rendertarget_view::acquire_render_pass(erhe::graphics::Command_buffer&) -> erhe::graphics::Render_pass*
{
    return (m_mesh != nullptr) ? m_mesh->get_render_pass() : nullptr;
}

void Mesh_rendertarget_view::finish_render(erhe::graphics::Command_buffer& command_buffer, App_context& context)
{
    if (m_mesh != nullptr) {
        m_mesh->render_done(command_buffer, context);
    }
}

auto Mesh_rendertarget_view::get_output_texture() const -> std::shared_ptr<erhe::graphics::Texture>
{
    return (m_mesh != nullptr) ? m_mesh->get_texture() : nullptr;
}

auto Mesh_rendertarget_view::is_quad_visible() const -> bool
{
    return (m_node != nullptr) && m_node->is_visible();
}

auto Mesh_rendertarget_view::get_pointer() const -> std::optional<glm::vec2>
{
    return (m_mesh != nullptr) ? m_mesh->get_pointer() : std::nullopt;
}

auto Mesh_rendertarget_view::world_to_window(glm::vec3 world_position) const -> std::optional<glm::vec2>
{
    return (m_mesh != nullptr) ? m_mesh->get_world_to_window(world_position) : std::nullopt;
}

auto Mesh_rendertarget_view::get_plane_world_origin() const -> glm::vec3
{
    return (m_node != nullptr) ? glm::vec3{m_node->position_in_world()} : glm::vec3{0.0f};
}

auto Mesh_rendertarget_view::get_plane_world_normal() const -> glm::vec3
{
    return (m_node != nullptr) ? glm::vec3{m_node->direction_in_world()} : glm::vec3{0.0f, 1.0f, 0.0f};
}

#if defined(ERHE_XR_LIBRARY_OPENXR)
void Mesh_rendertarget_view::update_headset_hand_tracking()
{
    if (m_mesh != nullptr) {
        m_mesh->update_headset_hand_tracking();
    }
}
#endif

} // namespace editor
