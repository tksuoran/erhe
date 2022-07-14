#pragma once

#include "erhe/application/rendertarget_imgui_viewport.hpp"
#include "erhe/components/components.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/graphics/vertex_attribute_mappings.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/scene/viewport.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

#include <gsl/gsl>

#include <string_view>

struct ImFontAtlas;

namespace erhe::graphics
{
    class Framebuffer;
    class OpenGL_state_tracker;
    class Texture;
}

namespace erhe::scene
{
    class Mesh;
}

namespace erhe::application
{

class Configuration;
class View;
class Imgui_window;
class Imgui_renderer;
class Mesh_memory;
class Node_raytrace;
class Pointer_context;
class Programs;
class Render_context;
class Scene_root;
class View;

}

namespace editor
{

class Node_raytrace;
class Render_context;

class Rendertarget_imgui_windows
    : public erhe::application::Base_imgui_windows
{
public:
    Rendertarget_imgui_windows(
        const std::string_view              name,
        const erhe::components::Components& components,
        const int                           width,
        const int                           height,
        const double                        dots_per_meter
    );
    virtual ~Rendertarget_imgui_windows() noexcept;

    // Implements IImgui_windows
    void register_imgui_window(erhe::application::Imgui_window* window) override;
    void imgui_windows        () override;

    // Public API
    [[nodiscard]] auto mesh_node() -> std::shared_ptr<erhe::scene::Mesh>;
    [[nodiscard]] auto texture  () const -> std::shared_ptr<erhe::graphics::Texture>;

    void render_mesh_layer(
        Forward_renderer&     forward_renderer,
        const Render_context& context
    );

    void mouse_button  (const uint32_t button, bool pressed);
    void on_key        (const signed int keycode, const uint32_t modifier_mask, const bool pressed);
    //void on_char       (const unsigned int codepoint);
    void on_mouse_wheel(const double x, const double y);

    void render(const Render_context& context);

private:
    void init_context              (const erhe::components::Components& components);
    void init_rendertarget         (const int width, const int height);
    void init_renderpass           (const erhe::components::Components& components);
    void begin_imgui_frame         ();
    void end_and_render_imgui_frame();
    void add_scene_node            (const erhe::components::Components& components);

    // Component dependencies
    std::shared_ptr<erhe::application::Imgui_renderer>    m_renderer;
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;
    std::shared_ptr<erhe::application::View>              m_view;

    std::string                                                  m_name;
    erhe::scene::Mesh_layer                                      m_mesh_layer;
    std::mutex                                                   m_mutex;
    double                                                       m_dots_per_meter{0.0};
    bool                                                         m_has_focus     {false};
    double                                                       m_time          {0.0};
    ImGuiContext*                                                m_imgui_context {nullptr};
    std::vector<gsl::not_null<erhe::application::Imgui_window*>> m_imgui_windows;
    std::shared_ptr<erhe::scene::Mesh>                           m_gui_mesh;
    std::shared_ptr<Node_raytrace>                               m_node_raytrace;
    std::shared_ptr<erhe::graphics::Texture>                     m_texture;
    std::unique_ptr<erhe::graphics::Framebuffer>                 m_framebuffer;
    Renderpass                                                   m_renderpass;
};

} // namespace erhe::application
