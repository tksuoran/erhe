#if 0
#include "rendertarget_imgui_windows.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/programs.hpp"
#include "renderers/render_context.hpp"
#include "scene/scene_root.hpp"
#include "scene/helpers.hpp"
#include "log.hpp"

#include "erhe/application/configuration.hpp"
#include "erhe/application/view.hpp"
//#include "erhe/application/time.hpp"
//#include "erhe/application/log.hpp"
//#include "erhe/application/window.hpp"
//
//#include "erhe/application/graphics/gl_context_provider.hpp"
#include "erhe/application/renderers/imgui_renderer.hpp"
#include "erhe/application/windows/imgui_window.hpp"
//#include "erhe/application/windows/pipelines.hpp"

#include "erhe/geometry/shapes/regular_polygon.hpp"
#include "erhe/graphics/buffer_transfer_queue.hpp"
#include "erhe/graphics/framebuffer.hpp"
#include "erhe/graphics/texture.hpp"
//#include "erhe/graphics/opengl_state_tracker.hpp"
//#include "erhe/graphics/state/color_blend_state.hpp"
#include "erhe/scene/mesh.hpp"
//#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/window.hpp"
//#include "erhe/toolkit/profile.hpp"
//
//

#include <imgui.h>

//#include <GLFW/glfw3.h>
//
//#include <gsl/gsl>

namespace editor
{

Rendertarget_imgui_windows::Rendertarget_imgui_windows(
    const std::string_view              name,
    const erhe::components::Components& components,
    const int                           width,
    const int                           height,
    const double                        dots_per_meter
)
    : m_pipeline_state_tracker{components.get<erhe::graphics::OpenGL_state_tracker>()}
    , m_view                  {components.get<erhe::application::View>()}
    , m_name                  {name}
    , m_mesh_layer            {"GUI Layer", erhe::scene::Node_visibility::gui}
    , m_dots_per_meter        {dots_per_meter}
{
    init_rendertarget(width, height);
    init_renderpass  (components);
    init_context     (components);
    add_scene_node   (components);
}

Rendertarget_imgui_windows::~Rendertarget_imgui_windows()
{
    ImGui::DestroyContext(m_imgui_context);
}

void Rendertarget_imgui_windows::init_rendertarget(
    const int width,
    const int height
)
{
    using Texture     = erhe::graphics::Texture;
    using Framebuffer = erhe::graphics::Framebuffer;

    m_texture = std::make_shared<Texture>(
        Texture::Create_info{
            .target          = gl::Texture_target::texture_2d,
            .internal_format = gl::Internal_format::rgba8,
            .sample_count    = 0,
            .width           = width,
            .height          = height
        }
    );
    m_texture->set_debug_label("ImGui Rendertarget");
    const float clear_value[4] = { 1.0f, 0.0f, 1.0f, 1.0f };
    gl::clear_tex_image(
        m_texture->gl_name(),
        0,
        gl::Pixel_format::rgba,
        gl::Pixel_type::float_,
        &clear_value[0]
    );

    Framebuffer::Create_info create_info;
    create_info.attach(gl::Framebuffer_attachment::color_attachment0, m_texture.get());
    m_framebuffer = std::make_unique<Framebuffer>(create_info);
    m_framebuffer->set_debug_label("ImGui Rendertarget");
}

void Rendertarget_imgui_windows::init_renderpass(
    const erhe::components::Components& components
)
{
    using erhe::graphics::Vertex_input_state;
    using erhe::graphics::Input_assembly_state;
    using erhe::graphics::Rasterization_state;
    using erhe::graphics::Depth_stencil_state;
    using erhe::graphics::Color_blend_state;

    const auto& configuration = *components.get<erhe::application::Configuration>().get();
    const auto& programs      = *components.get<Programs     >().get();
    const auto& mesh_memory   = *components.get<Mesh_memory  >().get();
    auto* vertex_input = mesh_memory.vertex_input.get();

    const bool reverse_depth = configuration.graphics.reverse_depth;

    m_renderpass.pipeline.data =
        {
            .name           = "GUI",
            .shader_stages  = programs.textured.get(),
            .vertex_input   = vertex_input,
            .input_assembly = Input_assembly_state::triangles,
            .rasterization  = Rasterization_state::cull_mode_none,
            .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(reverse_depth),
            .color_blend    = Color_blend_state::color_blend_premultiplied
        };
}

void Rendertarget_imgui_windows::init_context(
    const erhe::components::Components& components
)
{
    IMGUI_CHECKVERSION();

    m_renderer = components.get<erhe::application::Imgui_renderer>();
    auto* font_atlas = m_renderer->get_font_atlas();
    m_imgui_context = ImGui::CreateContext(font_atlas);

    ImGuiIO& io = ImGui::GetIO(m_imgui_context);
    m_renderer->use_as_backend_renderer_on_context(m_imgui_context);

    io.MouseDrawCursor = true;
    io.IniFilename = nullptr; // TODO

    IM_ASSERT(io.BackendPlatformUserData == NULL && "Already initialized a platform backend!");

    io.BackendPlatformUserData = this;
    io.BackendPlatformName     = "erhe rendertarget";
    io.DisplaySize             = ImVec2{static_cast<float>(m_texture->width()), static_cast<float>(m_texture->height())};
    io.DisplayFramebufferScale = ImVec2{1.0f, 1.0f};

    io.MousePos                = ImVec2{-FLT_MAX, -FLT_MAX};
    io.MouseHoveredViewport    = 0;
    for (int i = 0; i < IM_ARRAYSIZE(io.MouseDown); i++)
    {
        io.MouseDown[i] = false;
    }

    // io.AddFocusEvent(focus);

    //io.MouseWheelH += (float)xoffset;
    //io.MouseWheel += (float)yoffset;

    ImGui::SetCurrentContext(nullptr);
}

void Rendertarget_imgui_windows::register_imgui_window(erhe::application::Imgui_window* window)
{
    const std::lock_guard<std::mutex> lock{m_mutex};

#ifndef NDEBUG
    const auto i = std::find(m_imgui_windows.begin(), m_imgui_windows.end(), window);
    if (i != m_imgui_windows.end())
    {
        log_rendertarget_imgui_windows->error(
            "Window {} already registered as rendertarget ImGui Window\n",
            window->title()
        );
    }
    else
#endif
    m_imgui_windows.push_back(window);
}

void Rendertarget_imgui_windows::add_scene_node(
    const erhe::components::Components& components
)
{
    auto& mesh_memory = *components.get<Mesh_memory>().get();
    auto& scene_root  = *components.get<Scene_root >().get();

    auto gui_material = scene_root.make_material(
        "GUI Quad",
        glm::vec4{0.1f, 0.1f, 0.2f, 1.0f}
    );

    const auto local_width  = static_cast<double>(m_texture->width ()) / m_dots_per_meter;
    const auto local_height = static_cast<double>(m_texture->height()) / m_dots_per_meter;

    auto gui_geometry = erhe::geometry::shapes::make_rectangle(
        local_width,
        local_height
    );

    const auto shared_geometry = std::make_shared<erhe::geometry::Geometry>(
        std::move(gui_geometry)
    );

    erhe::graphics::Buffer_transfer_queue buffer_transfer_queue;
    auto gui_primitive = erhe::primitive::make_primitive(
        *shared_geometry.get(),
        mesh_memory.build_info
    );

    erhe::primitive::Primitive primitive{
        .material              = gui_material,
        .gl_primitive_geometry = gui_primitive
    };
    m_gui_mesh = std::make_shared<erhe::scene::Mesh>("GUI Quad", primitive);
    m_gui_mesh->set_visibility_mask(
        erhe::scene::Node_visibility::visible |
        erhe::scene::Node_visibility::id      |
        erhe::scene::Node_visibility::gui
    );

    m_node_raytrace = std::make_shared<Node_raytrace>(shared_geometry);

    add_to_raytrace_scene(scene_root.raytrace_scene(), m_node_raytrace);
    m_gui_mesh->attach(m_node_raytrace);

    scene_root.add( // TODO Remove scene_root.gui_layer()?
        m_gui_mesh,
        scene_root.gui_layer()
    );

    m_mesh_layer.meshes.push_back(m_gui_mesh);
}

auto Rendertarget_imgui_windows::mesh_node() -> std::shared_ptr<erhe::scene::Mesh>
{
    return m_gui_mesh;
}

auto Rendertarget_imgui_windows::texture() const -> std::shared_ptr<erhe::graphics::Texture>
{
    return m_texture;
}

template <typename T>
[[nodiscard]] inline auto as_span(const T& value) -> gsl::span<const T>
{
    return gsl::span<const T>(&value, 1);
}

void Rendertarget_imgui_windows::render_mesh_layer(
    Forward_renderer&     forward_renderer,
    const Render_context& context
)
{
    forward_renderer.render(
        Forward_renderer::Render_parameters{
            .viewport          = context.viewport,
            .camera            = context.camera,
            .mesh_spans        = { m_mesh_layer.meshes },
            .lights            = { },
            .materials         = { },
            .passes            = { &m_renderpass },
            .visibility_filter =
            {
                .require_all_bits_set = (erhe::scene::Node_visibility::visible | erhe::scene::Node_visibility::gui)
            }
        }
    );
}

void Rendertarget_imgui_windows::imgui_windows()
{
    erhe::application::Scoped_imgui_context scoped_context{m_imgui_context};

    begin_imgui_frame();

    std::size_t i = 0;
    bool any_mouse_input_sink{false};
    for (auto& imgui_window : m_imgui_windows)
    {
        if (imgui_window->is_visible())
        {
            auto imgui_id = fmt::format("##rendertarget_window-{}", ++i);
            ImGui::PushID(imgui_id.c_str());
            if (imgui_window->begin())
            {
                imgui_window->imgui();
            }
            if (
                imgui_window->consumes_mouse_input() &&
                ImGui::IsWindowHovered()
            )
            {
                any_mouse_input_sink = true;
                m_view->set_mouse_input_sink(imgui_window);
            }
            imgui_window->end();
            ImGui::PopID();
        }
    }

    if (!any_mouse_input_sink)
    {
        m_view->set_mouse_input_sink(nullptr);
    }
    end_and_render_imgui_frame();
}

void Rendertarget_imgui_windows::mouse_button(const uint32_t button, bool pressed)
{
    ImGuiIO& io = ImGui::GetIO(m_imgui_context);
    io.MouseDown[button] = pressed;
}

void Rendertarget_imgui_windows::on_key(
    const signed int keycode,
    const uint32_t   modifier_mask,
    const bool       pressed
)
{
    using erhe::toolkit::Keycode;

    ImGuiIO& io = ImGui::GetIO(m_imgui_context);
    update_key_modifiers(io, modifier_mask);
    io.AddKeyEvent(from_erhe(keycode), pressed);
}

void Rendertarget_imgui_windows::on_mouse_wheel(const double x, const double y)
{
    ImGuiIO& io = ImGui::GetIO(m_imgui_context);
    io.MouseWheelH += static_cast<float>(x);
    io.MouseWheel  += static_cast<float>(y);
}

void Rendertarget_imgui_windows::begin_imgui_frame()
{
    ImGuiIO& io = ImGui::GetIO(m_imgui_context);

    // Setup time step
    const auto current_time = glfwGetTime();
    io.DeltaTime = m_time > 0.0
        ? static_cast<float>(current_time - m_time)
        : static_cast<float>(1.0 / 60.0);
    m_time = current_time;

    const auto& gui = m_view->pointer_context()->get_hover(Pointer_context::gui_slot);
    if (
        gui.valid &&
        gui.raytrace_node == m_node_raytrace.get() &&
        gui.position.has_value()
    )
    {
        const auto width    = static_cast<float>(m_texture->width());
        const auto height   = static_cast<float>(m_texture->height());
        const auto p_world  = gui.position.value();
        const auto p_local  = glm::vec3{m_gui_mesh->node_from_world() * glm::vec4{p_world, 1.0f}};
        const auto p_window = static_cast<float>(m_dots_per_meter) * p_local;
        io.MousePos = ImVec2{
            p_window.x + 0.5f * width,
            height - (p_window.y + 0.5f * height)
        };
        if (!m_has_focus)
        {
            m_has_focus = true;
            io.AddFocusEvent(true);
        }
    }
    else
    {
        io.MousePos = ImVec2{-FLT_MAX, -FLT_MAX};
        if (m_has_focus)
        {
            io.AddFocusEvent(false);
            m_has_focus = false;
        }
    }

    ImGui::NewFrame();
    ImGui::DockSpaceOverViewport(nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
}

void Rendertarget_imgui_windows::end_and_render_imgui_frame()
{
    ImGui::EndFrame();
    ImGui::Render();

    m_pipeline_state_tracker->shader_stages.reset();
    m_pipeline_state_tracker->color_blend.execute(
        erhe::graphics::Color_blend_state::color_blend_disabled
    );

    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, m_framebuffer->gl_name());
    gl::viewport        (0, 0, m_texture->width(), m_texture->height());
    gl::clear_color     (0.0f, 0.2f, 0.0f, 1.0f);
    gl::clear           (gl::Clear_buffer_mask::color_buffer_bit);

    m_renderer->render_draw_data();
    gl::generate_texture_mipmap(m_texture->gl_name());
}

void Rendertarget_imgui_windows::render(
    const Forward_renderer& forward_renderer,
    const Render_context&   context
)
{
    for (const auto& rendertarget : m_rendertarget_imgui_windows)
    {
        rendertarget->render_mesh_layer(
            forward_renderer,
            context
        );
    }
}

}  // namespace editor

#endif