#include "view_client.hpp"
#include "map_window.hpp"
#include "tile_renderer.hpp"
#include "erhe/application/view.hpp"
#include "erhe/application/imgui_windows.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"

namespace hextiles
{

View_client::View_client()
    : Component{c_label}
{
}

View_client::~View_client()
{
}

void View_client::connect()
{
    m_imgui_windows          = get<erhe::application::Imgui_windows    >();
    m_pipeline_state_tracker = get<erhe::graphics::OpenGL_state_tracker>();

    m_tile_renderer  = get<Tile_renderer                    >();
    m_map_window    = get<Map_window                      >();
    require<erhe::application::View>();
}

void View_client::initialize_component()
{
    get<erhe::application::View>()->set_client(this);
}

void View_client::initial_state()
{
    gl::clip_control(gl::Clip_control_origin::lower_left, gl::Clip_control_depth::zero_to_one);
    gl::disable     (gl::Enable_cap::primitive_restart);
    gl::enable      (gl::Enable_cap::primitive_restart_fixed_index);
    gl::enable      (gl::Enable_cap::texture_cube_map_seamless);
    gl::enable      (gl::Enable_cap::framebuffer_srgb);
}

void View_client::bind_default_framebuffer()
{
    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, 0);
    gl::viewport        (0, 0, get<erhe::application::View>()->width(), get<erhe::application::View>()->height());
}

void View_client::clear()
{
    m_pipeline_state_tracker->shader_stages.reset();
    m_pipeline_state_tracker->color_blend.execute(erhe::graphics::Color_blend_state::color_blend_disabled);
    gl::clear_color  (0.2f, 0.3f, 0.0f, 0.1f);
    gl::clear        (gl::Clear_buffer_mask::color_buffer_bit);
}

void View_client::update()
{
}

void View_client::render()
{
    m_map_window  ->render();
    m_tile_renderer->next_frame();
}

void View_client::update_imgui_window(
    erhe::application::Imgui_window* imgui_window
)
{
    static_cast<void>(imgui_window);
}

void View_client::update_keyboard(
    const bool                   pressed,
    const erhe::toolkit::Keycode code,
    const uint32_t               modifier_mask
)
{
    static_cast<void>(pressed);
    static_cast<void>(code);
    static_cast<void>(modifier_mask);
}

void View_client::update_mouse(
    const erhe::toolkit::Mouse_button button,
    const int                         count
)
{
    static_cast<void>(button);
    static_cast<void>(count);
}

void View_client::update_mouse(const double x, const double y)
{
    static_cast<void>(x);
    static_cast<void>(y);
}

} // namespace hextiles

