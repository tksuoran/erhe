#pragma once

#include "renderers/pipeline_renderpass.hpp"
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
#include <optional>
#include <vector>

namespace erhe::graphics
{
    class Texture;
}

namespace erhe::scene
{
    class Camera;
    class Light;
    class Mesh;
    class Mesh_layer;
    class Item_filter;
}

namespace editor
{

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
    void deinitialize_component     () override;

    // Public API
    class Render_parameters
    {
    public:
        const glm::vec3                                                    ambient_light    {0.0f};
        const erhe::scene::Camera*                                         camera           {nullptr};
        const Light_projections*                                           light_projections{nullptr};
        const gsl::span<const std::shared_ptr<erhe::scene::Light>>&        lights           {};
        const gsl::span<const std::shared_ptr<erhe::primitive::Material>>& materials        {};
        const std::vector<
            gsl::span<const std::shared_ptr<erhe::scene::Mesh>>
        >&                                                                 mesh_spans;
        const std::vector<Pipeline_renderpass*>                            passes;
        erhe::primitive::Primitive_mode                                    primitive_mode{erhe::primitive::Primitive_mode::polygon_fill};
        Primitive_interface_settings                                       primitive_settings;
        const erhe::graphics::Texture*                                     shadow_texture{nullptr};
        const erhe::scene::Viewport&                                       viewport;
        const erhe::scene::Item_filter                                     filter{};
        const erhe::graphics::Shader_stages*                               override_shader_stages{nullptr};
    };

    void render(const Render_parameters& parameters);

    void render_fullscreen(
        const Render_parameters&  parameters,
        const erhe::scene::Light* light
    );

    void next_frame();

private:
    std::optional<Material_buffer     >      m_material_buffers;
    std::optional<Light_buffer        >      m_light_buffers;
    std::optional<Camera_buffer       >      m_camera_buffers;
    std::optional<Draw_indirect_buffer>      m_draw_indirect_buffers;
    std::optional<Primitive_buffer    >      m_primitive_buffers;
    std::shared_ptr<erhe::graphics::Texture> m_dummy_texture;
};

extern Forward_renderer* g_forward_renderer;

} // namespace editor
