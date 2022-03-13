#pragma once

#include "main/programs.hpp"
#include "renderers/base_renderer.hpp"
#include "renderstack_toolkit/platform.hpp"
#include "renderstack_toolkit/service.hpp"
#include "scene/model.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace renderstack
{
namespace graphics
{
class Uniform_buffer;
class Uniform_buffer_range;
} // namespace graphics

namespace mesh
{
class Geometry_mesh;
}
namespace scene
{
class Camera;
class Light;
class Viewport;
} // namespace scene
} // namespace renderstack

class Light_mesh;

class Light_debug_renderer
    : public renderstack::toolkit::service,
      public Base_renderer
{
public:
    Light_debug_renderer();

    virtual ~Light_debug_renderer() noexcept = default;

    void connect(
        std::shared_ptr<renderstack::graphics::Renderer> renderer,
        std::shared_ptr<Programs>                        programs,
        std::shared_ptr<Light_mesh>                      light_mesh
    );

    void initialize_service() override;

    void light_pass(
        const Light_collection           &lights,
        const renderstack::scene::Camera &camera
    );

private:
    renderstack::graphics::Render_states m_debug_light_render_states;
};
