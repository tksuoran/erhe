#pragma once

#include "renderers/base_renderer.hpp"
#include "renderers/renderpass.hpp"

#include "erhe/components/components.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/primitive/primitive.hpp"

#include <glm/glm.hpp>

#include <functional>
#include <initializer_list>
#include <memory>
#include <vector>

namespace erhe::graphics
{
    class OpenGL_state_tracker;
    class Vertex_input_state;
}

namespace erhe::scene
{
    class ICamera;
    class Camera;
    class Light;
    class Light_layer;
    class Mesh;
    class Mesh_layer;
    class Node;
    class Visibility_filter;
}

namespace editor
{

class Configuration;
class Programs;
class Mesh_memory;
class Shadow_renderer;

class Forward_renderer
    : public erhe::components::Component
    , public Base_renderer
{
public:
    using Mesh_layer_collection = std::vector<const erhe::scene::Mesh_layer*>;

    static constexpr std::string_view c_name{"Forward_renderer"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Forward_renderer ();
    ~Forward_renderer() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Public API
    class Render_parameters
    {
    public:
        const erhe::scene::Viewport&                                       viewport;
        const erhe::scene::ICamera*                                        camera           {nullptr};
        const std::initializer_list<
            const gsl::span<const std::shared_ptr<erhe::scene::Mesh>>
        >&                                                                 mesh_spans;
        const gsl::span<const std::shared_ptr<erhe::scene::Light>>&        lights;
        const gsl::span<const std::shared_ptr<erhe::primitive::Material>>& materials        {};
        const std::initializer_list<Renderpass* const>                     passes;
        const erhe::scene::Visibility_filter                               visibility_filter{};
        const glm::vec3                                                    ambient_light    {0.0f};
    };

    void render(const Render_parameters& parameters);

    void render_fullscreen(const Render_parameters& parameters);

private:
    // Component dependencies
    std::shared_ptr<Configuration>                        m_configuration;
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;
    std::shared_ptr<Mesh_memory>                          m_mesh_memory;
    std::shared_ptr<Shadow_renderer>                      m_shadow_renderer;
    std::shared_ptr<Programs>                             m_programs;
};

} // namespace editor
