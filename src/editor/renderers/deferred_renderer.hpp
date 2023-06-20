#pragma once

#include "main/programs.hpp"
#include "renderers/base_renderer.hpp"
#include "renderers/quad_renderer.hpp"
#include "renderstack_toolkit/platform.hpp"
#include "scene/model.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

#define NUM_UNIFORM_BUFFERS 3

namespace renderstack
{
namespace graphics
{
class Uniform_buffer;
class Uniform_buffer_range;
class Viewport;
} // namespace graphics

namespace mesh
{
class Geometry_mesh;
}

namespace scene
{
class Camera;
class Light;
} // namespace scene

} // namespace renderstack

class Light_mesh;
class Material;

class Deferred_renderer
    : public renderstack::toolkit::service,
      public Base_renderer
{
public:
    Deferred_renderer();

    virtual ~Deferred_renderer() noexcept = default;

    void connect(
        std::shared_ptr<renderstack::graphics::Renderer> renderer,
        std::shared_ptr<Programs>                        programs,
        std::shared_ptr<Quad_renderer>                   quad_renderer,
        std::shared_ptr<Light_mesh>                      light_mesh
    );

    void initialize_service() override;

    void geometry_pass(
        const Material_collection        &materials,
        const Model_collection           &models,
        const renderstack::scene::Camera &camera
    );

    void light_pass(
        const Light_collection           &lights,
        const renderstack::scene::Camera &camera
    );

    void show_rt();

    void resize(int width, int height);

private:
    void fbo_clear();

    void bind_gbuffer_fbo();

    void bind_linear_fbo();

    std::shared_ptr<Quad_renderer> m_quad_renderer;

    renderstack::graphics::Render_states m_gbuffer_render_states;
    renderstack::graphics::Render_states m_light_stencil_render_states;
    renderstack::graphics::Render_states m_light_with_stencil_test_render_states;
    renderstack::graphics::Render_states m_light_render_states;
    renderstack::graphics::Render_states m_show_rt_render_states;
    renderstack::graphics::Render_states m_camera_render_states;

    // framebuffer
    unsigned int                                    m_gbuffer_fbo;
    std::shared_ptr<renderstack::graphics::Texture> m_gbuffer_rt[4];
    std::shared_ptr<renderstack::graphics::Texture> m_depth;

    unsigned int                                    m_linear_fbo;
    std::shared_ptr<renderstack::graphics::Texture> m_linear_rt[4];
    unsigned int                                    m_stencil_rbo;
};
