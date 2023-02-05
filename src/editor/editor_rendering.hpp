#pragma once

#include "renderers/renderpass.hpp"

#include "erhe/application/commands/command.hpp"
#include "erhe/components/components.hpp"
#include "erhe/scene/viewport.hpp"

namespace erhe::graphics
{
    class Gpu_timer;
}

namespace editor
{

class Editor_rendering;
class Render_context;

class Capture_frame_command
    : public erhe::application::Command
{
public:
    Capture_frame_command();

    auto try_call() -> bool override;
};

class Editor_rendering
    : public erhe::components::Component
    , public erhe::components::IUpdate_once_per_frame
{
public:
    static constexpr std::string_view c_type_name{"Editor_rendering"};
    static constexpr uint32_t c_type_hash{
        compiletime_xxhash::xxh32(
            c_type_name.data(),
            c_type_name.size(),
            {}
        )
    };

    Editor_rendering ();
    ~Editor_rendering() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void deinitialize_component     () override;
    void post_initialize            () override;

    // Implements IUpdate_once_per_frame
    void update_once_per_frame(const erhe::components::Time_context&) override;

    // Public API
    void trigger_capture           ();
    void render                    ();
    void render_viewport_main      (const Render_context& context, bool has_pointer);
    void render_viewport_overlay   (const Render_context& context, bool has_pointer);
    void render_content            (const Render_context& context, bool polygon_fill);
    void render_selection          (const Render_context& context, bool polygon_fill);
    void render_tool_meshes        (const Render_context& context);
    void render_rendertarget_meshes(const Render_context& context);
    void render_brush              (const Render_context& context);
    void render_id                 (const Render_context& context);

    void begin_frame();
    void end_frame  ();

private:
    void setup_renderpasses();

    [[nodiscard]] auto width () const -> int;
    [[nodiscard]] auto height() const -> int;

    // Commands
    Capture_frame_command m_capture_frame_command;

    bool       m_trigger_capture{false};

    Renderpass m_rp_polygon_fill_standard;
    Renderpass m_rp_tool1_hidden_stencil;
    Renderpass m_rp_tool2_visible_stencil;
    Renderpass m_rp_tool3_depth_clear;
    Renderpass m_rp_tool4_depth;
    Renderpass m_rp_tool5_visible_color;
    Renderpass m_rp_tool6_hidden_color;
    Renderpass m_rp_line_hidden_blend;
    Renderpass m_rp_brush_back;
    Renderpass m_rp_brush_front;
    Renderpass m_rp_edge_lines;
    Renderpass m_rp_corner_points;
    Renderpass m_rp_polygon_centroids;
    Renderpass m_rp_rendertarget_meshes;

    std::unique_ptr<erhe::graphics::Gpu_timer> m_content_timer;
    std::unique_ptr<erhe::graphics::Gpu_timer> m_selection_timer;
    std::unique_ptr<erhe::graphics::Gpu_timer> m_gui_timer;
    std::unique_ptr<erhe::graphics::Gpu_timer> m_brush_timer;
    std::unique_ptr<erhe::graphics::Gpu_timer> m_tools_timer;
};

extern Editor_rendering* g_editor_rendering;

static constexpr unsigned int s_stencil_edge_lines               =  1u; // 0 inc
static constexpr unsigned int s_stencil_tool_mesh_hidden         =  2u;
static constexpr unsigned int s_stencil_tool_mesh_visible        =  3u;

static constexpr unsigned int s_stencil_line_renderer_grid_minor =  8u;
static constexpr unsigned int s_stencil_line_renderer_grid_major =  9u;
static constexpr unsigned int s_stencil_line_renderer_selection  = 10u;
static constexpr unsigned int s_stencil_line_renderer_tools      = 11u;


}
