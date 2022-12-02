#pragma once

#include "renderers/renderpass.hpp"
#include "renderers/material_buffer.hpp"
#include "renderers/light_buffer.hpp"
#include "renderers/camera_buffer.hpp"
#include "renderers/draw_indirect_buffer.hpp"
#include "renderers/primitive_buffer.hpp"

#include "erhe/components/components.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/primitive/primitive.hpp"
#include "erhe/scene/node.hpp"

#include <glm/glm.hpp>

#include <functional>
#include <initializer_list>
#include <memory>
#include <vector>

namespace erhe::graphics
{
    class OpenGL_state_tracker;
    class Texture;
    class Vertex_input_state;
}

namespace erhe::scene
{
    class Camera;
    class Light;
    class Light_layer;
    class Mesh;
    class Mesh_layer;
    class Node;
    class Scene_item_filter;
}

namespace erhe::application
{
    class Configuration;
}

namespace editor
{

class Programs;
class Mesh_memory;
class Shadow_renderer;

class Forward_renderer
    : public erhe::components::Component
{
public:
    using Mesh_layer_collection = std::vector<const erhe::scene::Mesh_layer*>;

    static constexpr std::string_view c_type_name{"Forward_renderer"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Forward_renderer ();
    ~Forward_renderer() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Public API
    class Render_parameters
    {
    public:
        const glm::vec3                                                    ambient_light    {0.0f};
        const erhe::scene::Camera*                                         camera           {nullptr};
        const Light_projections*                                           light_projections{nullptr};
        const gsl::span<const std::shared_ptr<erhe::scene::Light>>&        lights           {};
        const gsl::span<const std::shared_ptr<erhe::primitive::Material>>& materials        {};
        const std::initializer_list<
            const gsl::span<const std::shared_ptr<erhe::scene::Mesh>>
        >&                                                                 mesh_spans;
        const std::initializer_list<Renderpass* const>                     passes;
        const erhe::graphics::Texture*                                     shadow_texture   {nullptr};
        const erhe::scene::Viewport&                                       viewport;
        const erhe::scene::Scene_item_filter                               filter{};
    };

    void render(const Render_parameters& parameters);

    void render_fullscreen(
        const Render_parameters&  parameters,
        const erhe::scene::Light* light
    );

    void next_frame();

    auto primitive_settings() const -> Primitive_interface_settings&;

private:
    // Component dependencies
    std::shared_ptr<erhe::application::Configuration>     m_configuration;
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;
    std::shared_ptr<Mesh_memory>                          m_mesh_memory;
    std::shared_ptr<Shadow_renderer>                      m_shadow_renderer;
    std::shared_ptr<Programs>                             m_programs;

    std::unique_ptr<Material_buffer     >    m_material_buffers;
    std::unique_ptr<Light_buffer        >    m_light_buffers;
    std::unique_ptr<Camera_buffer       >    m_camera_buffers;
    std::unique_ptr<Draw_indirect_buffer>    m_draw_indirect_buffers;
    std::unique_ptr<Primitive_buffer    >    m_primitive_buffers;
    std::shared_ptr<erhe::graphics::Texture> m_dummy_texture;
};

} // namespace editor
