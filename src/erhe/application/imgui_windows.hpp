#pragma once

#include "erhe/components/components.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/graphics/vertex_attribute_mappings.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/scene/viewport.hpp"

#include <imgui.h>

#include <gsl/gsl>

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

class IImgui_windows
{
public:
    virtual void register_imgui_window(Imgui_window* window) = 0;
    virtual void imgui_windows        () = 0;
};

#if 0
class Rendertarget_imgui_windows
    : public IImgui_windows
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
    void register_imgui_window(Imgui_window* window) override;
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

private:
    void init_context              (const erhe::components::Components& components);
    void init_rendertarget         (const int width, const int height);
    void init_renderpass           (const erhe::components::Components& components);
    void begin_imgui_frame         ();
    void end_and_render_imgui_frame();
    void add_scene_node            (const erhe::components::Components& components);

    // Component dependencies
    std::shared_ptr<Imgui_renderer>                       m_renderer;
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;
    std::shared_ptr<Editor_view>                          m_editor_view;

    std::string                                  m_name;
    erhe::scene::Mesh_layer                      m_mesh_layer;
    std::mutex                                   m_mutex;
    double                                       m_dots_per_meter{0.0};
    bool                                         m_has_focus     {false};
    double                                       m_time          {0.0};
    ImGuiContext*                                m_imgui_context {nullptr};
    std::vector<gsl::not_null<Imgui_window*>>    m_imgui_windows;
    std::shared_ptr<erhe::scene::Mesh>           m_gui_mesh;
    std::shared_ptr<Node_raytrace>               m_node_raytrace;
    std::shared_ptr<erhe::graphics::Texture>     m_texture;
    std::unique_ptr<erhe::graphics::Framebuffer> m_framebuffer;
    Renderpass                                   m_renderpass;
};
#endif

class Imgui_windows
    : public erhe::components::Component
    , public IImgui_windows

{
public:
    static constexpr std::string_view c_label{"Editor_imgui_windows"};
    static constexpr std::string_view c_title{"Editor ImGui Windows"};
    static constexpr uint32_t         hash{
        compiletime_xxhash::xxh32(
            c_label.data(),
            c_label.size(),
            {}
        )
    };

    Imgui_windows ();
    ~Imgui_windows() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Implements IImgui_windows
    void register_imgui_window(Imgui_window* window) override;
    void imgui_windows        () override;

    // Public API
    void rendertarget_imgui_windows();

    [[nodiscard]] auto want_capture_mouse() const -> bool;

#if 0
    auto create_rendertarget(
        const std::string_view name,
        const int              width,
        const int              height,
        const double           dots_per_meter
    ) -> std::shared_ptr<Rendertarget_imgui_windows>;

    void destroy_rendertarget(const std::shared_ptr<Rendertarget_imgui_windows>& rendertarget);

    void render_rendertarget_gui_meshes(const Render_context& context);
#endif
    void render_imgui_frame();

    void make_imgui_context_current  ();
    void make_imgui_context_uncurrent();

    void on_key         (const signed int keycode, const uint32_t modifier_mask, const bool pressed);
    void on_char        (const unsigned int codepoint);
    void on_focus       (int focused);
    void on_cursor_enter(int entered);
    void on_mouse_click (const uint32_t button, const int count);
    void on_mouse_wheel (const double x, const double y);

private:
    void init_context      ();
    void menu              ();
    void window_menu       ();
    void begin_imgui_frame ();
    void end_imgui_frame   ();

    // Component dependencies
    //std::shared_ptr<Editor_rendering>                        m_editor_rendering;
    std::shared_ptr<View>                                    m_view;
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker>    m_pipeline_state_tracker;
    std::shared_ptr<Imgui_renderer>                          m_renderer;

    std::mutex                                               m_mutex;
    std::vector<gsl::not_null<Imgui_window*>>                m_imgui_windows;
#if 0
    std::vector<std::shared_ptr<Rendertarget_imgui_windows>> m_rendertarget_imgui_windows;
#endif
    ImGuiContext*                                            m_imgui_context       {nullptr};
    ImVector<ImWchar>                                        m_glyph_ranges;
    bool                                                     m_show_style_editor   {false};

    double                                                   m_time                {0.0};
    bool                                                     m_has_cursor          {false};
    bool                                                     m_mouse_just_pressed[ImGuiMouseButton_COUNT];

    // std::shared_ptr<erhe::scene::Mesh> m_test_rendertarget_mesh;
};

} // namespace erhe::application
