#pragma once

#include "renderers/renderpass.hpp"
#include "erhe/application/imgui_viewport.hpp"
#include "erhe/components/components.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/graphics/vertex_attribute_mappings.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/scene/viewport.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

#include <glm/glm.hpp>

#include <gsl/gsl>

#include <string_view>

struct ImFontAtlas;

namespace erhe::application
{
    class Configuration;
    class Imgui_window;
    class Imgui_renderer;
    class View;
}

namespace erhe::graphics
{
    class Framebuffer;
    class OpenGL_state_tracker;
    class Sampler;
    class Texture;
}

namespace erhe::scene
{
    class Mesh;
}

namespace editor
{

class Forward_renderer;
class Mesh_memory;
class Node_raytrace;
class Pointer_context;
class Programs;
class Render_context;
class Scene_root;

class IViewport
{
public:
    virtual ~IViewport();

    virtual [[nodiscard]] auto width  () const -> float = 0;
    virtual [[nodiscard]] auto height () const -> float = 0;
    //virtual [[nodiscard]] auto try_hit(const glm::vec2 pointer) const -> std::optional<glm::vec2> = 0;
    virtual void begin(erhe::application::Imgui_viewport& imgui_viewport) = 0;
    virtual void end  () = 0;
};

class Rendertarget_viewport
    : public IViewport
{
public:
    Rendertarget_viewport(
        const erhe::components::Components& components,
        const int                           width,
        const int                           height,
        const double                        dots_per_meter
    );

    // Implements IViewport
    [[nodiscard]] auto width  () const -> float override;
    [[nodiscard]] auto height () const -> float override;
    void begin(erhe::application::Imgui_viewport& imgui_viewport) override;
    void end  () override;

    // Public API
    [[nodiscard]] auto mesh_node() -> std::shared_ptr<erhe::scene::Mesh>;
    [[nodiscard]] auto texture  () const -> std::shared_ptr<erhe::graphics::Texture>;

    void render_mesh_layer(
        Forward_renderer&     forward_renderer,
        const Render_context& context
    );

private:
    [[nodiscard]] auto get_hover_position() const -> std::optional<glm::vec2>;

    void init_rendertarget(const int width, const int height);
    void init_renderpass  (const erhe::components::Components& components);
    void add_scene_node   (const erhe::components::Components& components);

    std::shared_ptr<erhe::application::Imgui_renderer>    m_imgui_renderer;
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;
    std::shared_ptr<Pointer_context>                      m_pointer_context;
    std::shared_ptr<Scene_root>                           m_scene_root;
    erhe::scene::Mesh_layer                               m_mesh_layer;
    double                                                m_dots_per_meter{0.0};
    std::shared_ptr<erhe::scene::Mesh>                    m_gui_mesh;
    std::shared_ptr<Node_raytrace>                        m_node_raytrace;
    std::shared_ptr<erhe::graphics::Texture>              m_texture;
    std::shared_ptr<erhe::graphics::Sampler>              m_sampler;
    std::unique_ptr<erhe::graphics::Framebuffer>          m_framebuffer;
    Renderpass                                            m_renderpass;
};

class Rendertarget_imgui_viewport
    : public erhe::application::Imgui_viewport
{
public:
    Rendertarget_imgui_viewport(
        IViewport*                          viewport,
        const std::string_view              name,
        const erhe::components::Components& components
    );
    virtual ~Rendertarget_imgui_viewport() noexcept;

    // Implements Imgui_vewport
    void begin_imgui_frame() override;
    void end_imgui_frame  ()           override;

private:
    IViewport*                                         m_viewport;
    std::shared_ptr<erhe::application::Imgui_renderer> m_renderer;
    std::shared_ptr<erhe::application::View>           m_view;
    std::string                                        m_name;
    bool                                               m_has_focus    {false};
    double                                             m_time         {0.0};
    ImGuiContext*                                      m_imgui_context{nullptr};
};

} // namespace erhe::application
