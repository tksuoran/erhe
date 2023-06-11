#pragma once

#include "erhe/renderer/camera_buffer.hpp"
#include "erhe/renderer/draw_indirect_buffer.hpp"
#include "erhe/renderer/joint_buffer.hpp"
#include "erhe/renderer/light_buffer.hpp"
#include "erhe/renderer/material_buffer.hpp"
#include "erhe/renderer/pipeline_renderpass.hpp"
#include "erhe/renderer/primitive_buffer.hpp"

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
    class Sampler;
    class Texture;
}

namespace erhe::scene
{
    class Camera;
    class Item_filter;
    class Light;
    class Mesh;
    class Mesh_layer;
}

namespace erhe::renderer
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
        const erhe::graphics::Vertex_input_state*                          vertex_input_state{nullptr};
        gl::Draw_elements_type                                             index_type        {gl::Draw_elements_type::unsigned_int};

        const glm::vec3                                                    ambient_light    {0.0f};
        const erhe::scene::Camera*                                         camera           {nullptr};
        const erhe::renderer::Light_projections*                           light_projections{nullptr};
        const gsl::span<const std::shared_ptr<erhe::scene::Light>>&        lights           {};
        const gsl::span<const std::shared_ptr<erhe::scene::Skin>>&         skins            {};
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
    std::optional<Camera_buffer       >      m_camera_buffers;
    std::optional<Draw_indirect_buffer>      m_draw_indirect_buffers;
    std::optional<Joint_buffer        >      m_joint_buffers;
    std::optional<Light_buffer        >      m_light_buffers;
    std::optional<Material_buffer     >      m_material_buffers;
    std::optional<Primitive_buffer    >      m_primitive_buffers;


    int m_base_texture_unit{0};

    std::shared_ptr<erhe::graphics::Texture> m_dummy_texture;
    std::unique_ptr<erhe::graphics::Sampler> m_nearest_sampler;
    std::unique_ptr<erhe::graphics::Sampler> m_linear_sampler;
};

extern Forward_renderer* g_forward_renderer;

} // namespace erhe::renderer
