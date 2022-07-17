#include "rendertarget_imgui_viewport.hpp"
#include "renderers/forward_renderer.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/programs.hpp"
#include "renderers/render_context.hpp"
#include "scene/node_raytrace.hpp"
#include "scene/scene_root.hpp"
#include "scene/helpers.hpp"
#include "tools/pointer_context.hpp"
#include "log.hpp"

#include "erhe/application/configuration.hpp"
#include "erhe/application/view.hpp"
#include "erhe/application/renderers/imgui_renderer.hpp"
#include "erhe/application/windows/imgui_window.hpp"

#include "erhe/geometry/shapes/regular_polygon.hpp"
#include "erhe/graphics/buffer_transfer_queue.hpp"
#include "erhe/graphics/framebuffer.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/toolkit/window.hpp"

#include <imgui.h>

#include <GLFW/glfw3.h> // TODO Fix dependency ?

namespace editor
{

IViewport::~IViewport()
{
}

Rendertarget_viewport::Rendertarget_viewport(
    const erhe::components::Components& components,
    const int                           width,
    const int                           height,
    const double                        dots_per_meter
)
    : m_imgui_renderer        {components.get<erhe::application::Imgui_renderer   >()}
    , m_pipeline_state_tracker{components.get<erhe::graphics::OpenGL_state_tracker>()}
    , m_pointer_context       {components.get<Pointer_context                     >()}
    , m_mesh_layer            {"GUI Layer", erhe::scene::Node_visibility::gui}
    , m_dots_per_meter        {dots_per_meter}
{
    init_rendertarget(width, height);
    init_renderpass  (components);
    add_scene_node   (components);
}

void Rendertarget_viewport::init_rendertarget(
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

void Rendertarget_viewport::init_renderpass(
    const erhe::components::Components& components
)
{
    using erhe::graphics::Vertex_input_state;
    using erhe::graphics::Input_assembly_state;
    using erhe::graphics::Rasterization_state;
    using erhe::graphics::Depth_stencil_state;
    using erhe::graphics::Color_blend_state;

    const auto& configuration = *components.get<erhe::application::Configuration>().get();
    const auto& programs      = *components.get<Programs   >().get();
    const auto& mesh_memory   = *components.get<Mesh_memory>().get();
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

void Rendertarget_viewport::add_scene_node(
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

auto Rendertarget_viewport::mesh_node() -> std::shared_ptr<erhe::scene::Mesh>
{
    return m_gui_mesh;
}

auto Rendertarget_viewport::texture() const -> std::shared_ptr<erhe::graphics::Texture>
{
    return m_texture;
}

void Rendertarget_viewport::render_mesh_layer(
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
            .light_projections = { },
            .materials         = { },
            .passes            = { &m_renderpass },
            .visibility_filter =
            {
                .require_all_bits_set = (erhe::scene::Node_visibility::visible | erhe::scene::Node_visibility::gui)
            }
        }
    );
}

[[nodiscard]] auto Rendertarget_viewport::width() const -> float
{
    return static_cast<float>(m_texture->width());
}

[[nodiscard]] auto Rendertarget_viewport::height() const -> float
{
    return static_cast<float>(m_texture->height());
}

[[nodiscard]] auto Rendertarget_viewport::get_hover_position() const -> std::optional<glm::vec2>
{
    const auto& gui = m_pointer_context->get_hover(Pointer_context::gui_slot);
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
        return glm::vec2{
            p_window.x + 0.5f * width,
            height - (p_window.y + 0.5f * height)
        };
    }
    return {};
}

void Rendertarget_viewport::begin(erhe::application::Imgui_viewport& imgui_viewport)
{
    const auto& gui = m_pointer_context->get_hover(Pointer_context::gui_slot);
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
        if (!imgui_viewport.has_cursor())
        {
            imgui_viewport.on_cursor_enter(1);
        }
        imgui_viewport.on_mouse_move(
            p_window.x + 0.5f * width,
            height - (p_window.y + 0.5f * height)
        );
    }
    else
    {
        if (imgui_viewport.has_cursor())
        {
            imgui_viewport.on_cursor_enter(0);
        }
        imgui_viewport.on_mouse_move(-FLT_MAX, -FLT_MAX);
    }
}

void Rendertarget_viewport::end()
{
    m_pipeline_state_tracker->shader_stages.reset();
    m_pipeline_state_tracker->color_blend.execute(
        erhe::graphics::Color_blend_state::color_blend_disabled
    );

    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, m_framebuffer->gl_name());
    gl::viewport        (0, 0, m_texture->width(), m_texture->height());
    gl::clear_color     (0.0f, 0.2f, 0.0f, 1.0f);
    gl::clear           (gl::Clear_buffer_mask::color_buffer_bit);

    m_imgui_renderer->render_draw_data();
    gl::generate_texture_mipmap(m_texture->gl_name());
}

Rendertarget_imgui_viewport::Rendertarget_imgui_viewport(
    IViewport*                          viewport,
    const std::string_view              name,
    const erhe::components::Components& components
)
    : m_viewport{viewport}
    , m_view    {components.get<erhe::application::View>()}
    , m_name    {name}
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
    io.DisplaySize             = ImVec2{static_cast<float>(m_viewport->width()), static_cast<float>(m_viewport->height())};
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

Rendertarget_imgui_viewport::~Rendertarget_imgui_viewport()
{
    ImGui::DestroyContext(m_imgui_context);
}

template <typename T>
[[nodiscard]] inline auto as_span(const T& value) -> gsl::span<const T>
{
    return gsl::span<const T>(&value, 1);
}

void Rendertarget_imgui_viewport::begin_imgui_frame()
{
    m_viewport->begin(*this);

    ImGui::NewFrame();
    ImGui::DockSpaceOverViewport(nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
}

void Rendertarget_imgui_viewport::end_imgui_frame()
{
    ImGui::EndFrame();
    ImGui::Render();

    m_viewport->end();
}

}  // namespace editor
