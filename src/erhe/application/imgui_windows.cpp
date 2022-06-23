#include "erhe/application/imgui_windows.hpp"

#include "erhe/application/configuration.hpp"
#include "erhe/application/view.hpp"
#include "erhe/application/time.hpp"
#include "erhe/application/log.hpp"
#include "erhe/application/window.hpp"

#include "erhe/application/graphics/gl_context_provider.hpp"
#include "erhe/application/renderers/imgui_renderer.hpp"
#include "erhe/application/windows/imgui_window.hpp"
#include "erhe/application/windows/pipelines.hpp"

#include "erhe/geometry/shapes/regular_polygon.hpp"
#include "erhe/graphics/buffer_transfer_queue.hpp"
#include "erhe/graphics/framebuffer.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/graphics/state/color_blend_state.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/profile.hpp"

#include <GLFW/glfw3.h>

#include <gsl/gsl>

namespace erhe::application
{

using erhe::graphics::Framebuffer;
using erhe::graphics::Texture;

#if defined(ERHE_GUI_LIBRARY_IMGUI)
auto from_erhe(const erhe::toolkit::Keycode keycode) -> ImGuiKey
{
    switch (keycode)
    {
        case erhe::toolkit::Key_tab          : return ImGuiKey_Tab           ;
        case erhe::toolkit::Key_left         : return ImGuiKey_LeftArrow     ;
        case erhe::toolkit::Key_right        : return ImGuiKey_RightArrow    ;
        case erhe::toolkit::Key_up           : return ImGuiKey_UpArrow       ;
        case erhe::toolkit::Key_down         : return ImGuiKey_DownArrow     ;
        case erhe::toolkit::Key_page_up      : return ImGuiKey_PageUp        ;
        case erhe::toolkit::Key_page_down    : return ImGuiKey_PageDown      ;
        case erhe::toolkit::Key_home         : return ImGuiKey_Home          ;
        case erhe::toolkit::Key_end          : return ImGuiKey_End           ;
        case erhe::toolkit::Key_insert       : return ImGuiKey_Insert        ;
        case erhe::toolkit::Key_delete       : return ImGuiKey_Delete        ;
        case erhe::toolkit::Key_backspace    : return ImGuiKey_Backspace     ;
        case erhe::toolkit::Key_space        : return ImGuiKey_Space         ;
        case erhe::toolkit::Key_enter        : return ImGuiKey_Enter         ;
        case erhe::toolkit::Key_escape       : return ImGuiKey_Escape        ;
        case erhe::toolkit::Key_left_control : return ImGuiKey_LeftCtrl      ;
        case erhe::toolkit::Key_left_shift   : return ImGuiKey_LeftShift     ;
        case erhe::toolkit::Key_left_alt     : return ImGuiKey_LeftAlt       ;
        case erhe::toolkit::Key_left_super   : return ImGuiKey_LeftSuper     ;
        case erhe::toolkit::Key_right_control: return ImGuiKey_RightCtrl     ;
        case erhe::toolkit::Key_right_shift  : return ImGuiKey_RightShift    ;
        case erhe::toolkit::Key_right_alt    : return ImGuiKey_RightAlt      ;
        case erhe::toolkit::Key_right_super  : return ImGuiKey_RightSuper    ;
        case erhe::toolkit::Key_menu         : return ImGuiKey_Menu          ;
        case erhe::toolkit::Key_0            : return ImGuiKey_0             ;
        case erhe::toolkit::Key_1            : return ImGuiKey_1             ;
        case erhe::toolkit::Key_2            : return ImGuiKey_2             ;
        case erhe::toolkit::Key_3            : return ImGuiKey_3             ;
        case erhe::toolkit::Key_4            : return ImGuiKey_4             ;
        case erhe::toolkit::Key_5            : return ImGuiKey_5             ;
        case erhe::toolkit::Key_6            : return ImGuiKey_6             ;
        case erhe::toolkit::Key_7            : return ImGuiKey_7             ;
        case erhe::toolkit::Key_8            : return ImGuiKey_8             ;
        case erhe::toolkit::Key_9            : return ImGuiKey_9             ;
        case erhe::toolkit::Key_a            : return ImGuiKey_A             ;
        case erhe::toolkit::Key_b            : return ImGuiKey_B             ;
        case erhe::toolkit::Key_c            : return ImGuiKey_C             ;
        case erhe::toolkit::Key_d            : return ImGuiKey_D             ;
        case erhe::toolkit::Key_e            : return ImGuiKey_E             ;
        case erhe::toolkit::Key_f            : return ImGuiKey_F             ;
        case erhe::toolkit::Key_g            : return ImGuiKey_G             ;
        case erhe::toolkit::Key_h            : return ImGuiKey_H             ;
        case erhe::toolkit::Key_i            : return ImGuiKey_I             ;
        case erhe::toolkit::Key_j            : return ImGuiKey_J             ;
        case erhe::toolkit::Key_k            : return ImGuiKey_K             ;
        case erhe::toolkit::Key_l            : return ImGuiKey_L             ;
        case erhe::toolkit::Key_m            : return ImGuiKey_M             ;
        case erhe::toolkit::Key_n            : return ImGuiKey_N             ;
        case erhe::toolkit::Key_o            : return ImGuiKey_O             ;
        case erhe::toolkit::Key_p            : return ImGuiKey_P             ;
        case erhe::toolkit::Key_q            : return ImGuiKey_Q             ;
        case erhe::toolkit::Key_r            : return ImGuiKey_R             ;
        case erhe::toolkit::Key_s            : return ImGuiKey_S             ;
        case erhe::toolkit::Key_t            : return ImGuiKey_T             ;
        case erhe::toolkit::Key_u            : return ImGuiKey_U             ;
        case erhe::toolkit::Key_v            : return ImGuiKey_V             ;
        case erhe::toolkit::Key_w            : return ImGuiKey_W             ;
        case erhe::toolkit::Key_x            : return ImGuiKey_X             ;
        case erhe::toolkit::Key_y            : return ImGuiKey_Y             ;
        case erhe::toolkit::Key_z            : return ImGuiKey_Z             ;
        case erhe::toolkit::Key_f1           : return ImGuiKey_F1            ;
        case erhe::toolkit::Key_f2           : return ImGuiKey_F2            ;
        case erhe::toolkit::Key_f3           : return ImGuiKey_F3            ;
        case erhe::toolkit::Key_f4           : return ImGuiKey_F4            ;
        case erhe::toolkit::Key_f5           : return ImGuiKey_F5            ;
        case erhe::toolkit::Key_f6           : return ImGuiKey_F6            ;
        case erhe::toolkit::Key_f7           : return ImGuiKey_F7            ;
        case erhe::toolkit::Key_f8           : return ImGuiKey_F8            ;
        case erhe::toolkit::Key_f9           : return ImGuiKey_F9            ;
        case erhe::toolkit::Key_f10          : return ImGuiKey_F10           ;
        case erhe::toolkit::Key_f11          : return ImGuiKey_F11           ;
        case erhe::toolkit::Key_f12          : return ImGuiKey_F12           ;
        case erhe::toolkit::Key_apostrophe   : return ImGuiKey_Apostrophe    ;
        case erhe::toolkit::Key_comma        : return ImGuiKey_Comma         ;
        case erhe::toolkit::Key_minus        : return ImGuiKey_Minus         ; // -
        case erhe::toolkit::Key_period       : return ImGuiKey_Period        ; // .
        case erhe::toolkit::Key_slash        : return ImGuiKey_Slash         ; // /
        case erhe::toolkit::Key_semicolon    : return ImGuiKey_Semicolon     ; // ;
        case erhe::toolkit::Key_equal        : return ImGuiKey_Equal         ; // =
        case erhe::toolkit::Key_left_bracket : return ImGuiKey_LeftBracket   ; // [
        case erhe::toolkit::Key_backslash    : return ImGuiKey_Backslash     ; // \ (this text inhibit multiline comment caused by backslash)
        case erhe::toolkit::Key_right_bracket: return ImGuiKey_RightBracket  ; // ]
        case erhe::toolkit::Key_grave_accent : return ImGuiKey_GraveAccent   ; // `
        case erhe::toolkit::Key_caps_lock    : return ImGuiKey_CapsLock      ;
        case erhe::toolkit::Key_scroll_lock  : return ImGuiKey_ScrollLock    ;
        case erhe::toolkit::Key_num_lock     : return ImGuiKey_NumLock       ;
        case erhe::toolkit::Key_print_screen : return ImGuiKey_PrintScreen   ;
        case erhe::toolkit::Key_pause        : return ImGuiKey_Pause         ;
        case erhe::toolkit::Key_kp_0         : return ImGuiKey_Keypad0       ;
        case erhe::toolkit::Key_kp_1         : return ImGuiKey_Keypad1       ;
        case erhe::toolkit::Key_kp_2         : return ImGuiKey_Keypad2       ;
        case erhe::toolkit::Key_kp_3         : return ImGuiKey_Keypad3       ;
        case erhe::toolkit::Key_kp_4         : return ImGuiKey_Keypad4       ;
        case erhe::toolkit::Key_kp_5         : return ImGuiKey_Keypad5       ;
        case erhe::toolkit::Key_kp_6         : return ImGuiKey_Keypad6       ;
        case erhe::toolkit::Key_kp_7         : return ImGuiKey_Keypad7       ;
        case erhe::toolkit::Key_kp_8         : return ImGuiKey_Keypad8       ;
        case erhe::toolkit::Key_kp_9         : return ImGuiKey_Keypad9       ;
        case erhe::toolkit::Key_kp_decimal   : return ImGuiKey_KeypadDecimal ;
        case erhe::toolkit::Key_kp_divide    : return ImGuiKey_KeypadDivide  ;
        case erhe::toolkit::Key_kp_multiply  : return ImGuiKey_KeypadMultiply;
        case erhe::toolkit::Key_kp_subtract  : return ImGuiKey_KeypadSubtract;
        case erhe::toolkit::Key_kp_add       : return ImGuiKey_KeypadAdd     ;
        case erhe::toolkit::Key_kp_enter     : return ImGuiKey_KeypadEnter   ;
        case erhe::toolkit::Key_kp_equal     : return ImGuiKey_KeypadEqual   ;
        default                              : return ImGuiKey_None          ;
    }
}

void update_key_modifiers(ImGuiIO& io, uint32_t modifier_mask)
{
    io.AddKeyEvent(ImGuiKey_ModCtrl,  (modifier_mask & erhe::toolkit::Key_modifier_bit_ctrl ) != 0);
    io.AddKeyEvent(ImGuiKey_ModShift, (modifier_mask & erhe::toolkit::Key_modifier_bit_shift) != 0);
    io.AddKeyEvent(ImGuiKey_ModAlt,   (modifier_mask & erhe::toolkit::Key_modifier_bit_menu ) != 0);
    io.AddKeyEvent(ImGuiKey_ModSuper, (modifier_mask & erhe::toolkit::Key_modifier_bit_super) != 0);
}

class Scoped_imgui_context
{
public:
    explicit Scoped_imgui_context(ImGuiContext* context)
    {
        m_old_context = ImGui::GetCurrentContext();
        // TODO Currently messy Expects(m_old_context == nullptr);
        ImGui::SetCurrentContext(context);
    }
    ~Scoped_imgui_context() noexcept
    {
        ImGui::SetCurrentContext(m_old_context);
    }

private:
    ImGuiContext* m_old_context{nullptr};
};
#endif

#if 0
Rendertarget_imgui_windows::Rendertarget_imgui_windows(
    const std::string_view              name,
    const erhe::components::Components& components,
    const int                           width,
    const int                           height,
    const double                        dots_per_meter
)
    : m_pipeline_state_tracker{components.get<erhe::graphics::OpenGL_state_tracker>()}
    , m_editor_view           {components.get<Editor_view>()}
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

    const auto& configuration = *components.get<Configuration>().get();
    const auto& programs      = *components.get<Programs     >().get();
    const auto& mesh_memory   = *components.get<Mesh_memory  >().get();
    auto* vertex_input = mesh_memory.vertex_input.get();

    m_renderpass.pipeline.data =
        {
            .name           = "GUI",
            .shader_stages  = programs.textured.get(),
            .vertex_input   = vertex_input,
            .input_assembly = Input_assembly_state::triangles,
            .rasterization  = Rasterization_state::cull_mode_none,
            .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(configuration.reverse_depth),
            .color_blend    = Color_blend_state::color_blend_premultiplied
        };
}

void Rendertarget_imgui_windows::init_context(
    const erhe::components::Components& components
)
{
    IMGUI_CHECKVERSION();

    m_renderer = components.get<Imgui_renderer>();
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

void Rendertarget_imgui_windows::register_imgui_window(Imgui_window* window)
{
    const std::lock_guard<std::mutex> lock{m_mutex};

#ifndef NDEBUG
    const auto i = std::find(m_imgui_windows.begin(), m_imgui_windows.end(), window);
    if (i != m_imgui_windows.end())
    {
        log_windows.error("Window {} already registered as rendertarget ImGui Window\n", window->title());
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
    Scoped_imgui_context scoped_context{m_imgui_context};

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
                m_editor_view->set_mouse_input_sink(imgui_window);
            }
            imgui_window->end();
            ImGui::PopID();
        }
    }

    if (!any_mouse_input_sink)
    {
        m_editor_view->set_mouse_input_sink(nullptr);
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

    const auto& gui = m_editor_view->pointer_context()->get_hover(Pointer_context::gui_slot);
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
#endif

Imgui_windows::Imgui_windows()
    : erhe::components::Component{c_label}
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    std::fill(
        std::begin(m_mouse_just_pressed),
        std::end(m_mouse_just_pressed),
        false
    );
#endif
}

Imgui_windows::~Imgui_windows() noexcept
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ImGui::DestroyContext(m_imgui_context);
#endif
}

void Imgui_windows::declare_required_components()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    m_renderer = require<Imgui_renderer>();
#endif

    require<Configuration      >();
    require<Gl_context_provider>();
    require<Window             >();
}

void Imgui_windows::initialize_component()
{
    const Scoped_gl_context gl_context{Component::get<Gl_context_provider>()};

    init_context();
}

void Imgui_windows::post_initialize()
{
    m_view                   = get<View>();
    m_pipeline_state_tracker = get<erhe::graphics::OpenGL_state_tracker>();
}

void Imgui_windows::init_context()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    IMGUI_CHECKVERSION();

    auto* font_atlas = m_renderer->get_font_atlas();
    m_imgui_context = ImGui::CreateContext(font_atlas);

    m_renderer->use_as_backend_renderer_on_context(m_imgui_context);

    ImGuiIO& io     = ImGui::GetIO(m_imgui_context);
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    io.BackendPlatformUserData = this;
    io.BackendPlatformName = "imgui_impl_erhe";
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;         // We can honor GetMouseCursor() values (optional)
    //io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;          // We can honor io.WantSetMousePos requests (optional, rarely used)
    //io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports;    // We can create multi-viewports on the Platform side (optional)

    // TODO Clipboard
    // TODO Update monitors

    // Our mouse update function expect PlatformHandle to be filled for the main viewport
    ImGuiViewport* main_viewport = ImGui::GetMainViewport();
    main_viewport->PlatformHandle = this;

    ImGui::SetCurrentContext(nullptr);
#endif

    m_time = 0.0;
}

void Imgui_windows::begin_imgui_frame()
{
    SPDLOG_LOGGER_TRACE(log_frame, "Imgui_windows::begin_imgui_frame()");

#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION

    const auto& editor_view    = *get<View  >().get();
    const auto& window         = *get<Window>().get();
    const auto& context_window = window.get_context_window();
    auto*       glfw_window    = context_window->get_glfw_window();

    const auto w = editor_view.width();
    const auto h = editor_view.height();

    ImGuiIO& io = ImGui::GetIO(m_imgui_context);
    io.DisplaySize = ImVec2{
        static_cast<float>(w),
        static_cast<float>(h)
    };

    // Setup time step
    const auto current_time = glfwGetTime();
    io.DeltaTime = m_time > 0.0
        ? static_cast<float>(current_time - m_time)
        : static_cast<float>(1.0 / 60.0);
    m_time = current_time;

    // ImGui_ImplGlfw_UpdateMousePosAndButtons();
    io.MousePos = ImVec2{-FLT_MAX, -FLT_MAX};
    for (int i = 0; i < IM_ARRAYSIZE(io.MouseDown); i++)
    {
        io.MouseDown[i] = m_mouse_just_pressed[i] || glfwGetMouseButton(glfw_window, i) != 0;
        m_mouse_just_pressed[i] = false;
        ///// for (const auto& rendertarget : m_rendertarget_imgui_windows)
        ///// {
        /////     rendertarget->mouse_button(i, io.MouseDown[i]);
        ///// }
    }
    if (m_has_cursor)
    {
        double mouse_x;
        double mouse_y;
        glfwGetCursorPos(glfw_window, &mouse_x, &mouse_y);
        io.MousePos = ImVec2{
            static_cast<float>(mouse_x),
            static_cast<float>(mouse_y)
        };
    }

    // ImGui_ImplGlfw_UpdateMouseCursor
    const auto cursor = static_cast<erhe::toolkit::Mouse_cursor>(ImGui::GetMouseCursor());
    context_window->set_cursor(cursor);

    SPDLOG_LOGGER_TRACE(log_frame, "ImGui::NewFrame()");
    ImGui::NewFrame();
    ImGui::DockSpaceOverViewport(nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
#endif
}

void Imgui_windows::end_imgui_frame()
{
}

void Imgui_windows::render_imgui_frame()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION

    SPDLOG_LOGGER_TRACE(log_frame, "ImGui::Render()");
    ImGui::Render();

    m_renderer->render_draw_data();
#endif
}

#if 0
auto Imgui_windows::create_rendertarget(
    const std::string_view name,
    const int              width,
    const int              height,
    const double           dots_per_meter
) -> std::shared_ptr<Rendertarget_imgui_windows>
{
    auto new_rendertarget = std::make_shared<Rendertarget_imgui_windows>(
        name,
        *m_components,
        width,
        height,
        dots_per_meter
    );
    m_rendertarget_imgui_windows.push_back(new_rendertarget);
    return new_rendertarget;
}

void Editor_imgui_windows::destroy_rendertarget(
    const std::shared_ptr<Rendertarget_imgui_windows>& rendertarget
)
{
    m_rendertarget_imgui_windows.erase(
        std::remove_if(
            m_rendertarget_imgui_windows.begin(),
            m_rendertarget_imgui_windows.end(),
            [&rendertarget](const auto& entry)
            {
                return entry == rendertarget;
            }
        ),
        m_rendertarget_imgui_windows.end()
    );
}
#endif

void Imgui_windows::menu()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    if (ImGui::BeginMainMenuBar())
    {
        window_menu();
        ImGui::EndMainMenuBar();
    }

    //ImGui::End();
#endif
}

void Imgui_windows::window_menu()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{10.0f, 10.0f});

    if (ImGui::BeginMenu("Window"))
    {
        for (const auto& window : m_imgui_windows)
        {
            bool enabled = window->is_visible();
            if (ImGui::MenuItem(window->title().data(), "", &enabled))
            {
                if (enabled)
                {
                    window->show();
                }
                else
                {
                    window->hide();
                }
            }
        }
        //ImGui::MenuItem("Tool Properties", "", &m_show_tool_properties);
        ImGui::MenuItem("ImGui Style Editor", "", &m_show_style_editor);

        ImGui::Separator();
        if (ImGui::MenuItem("Close All"))
        {
            for (const auto& window : m_imgui_windows)
            {
                window->hide();
            }
            //m_show_tool_properties = false;
        }
        if (ImGui::MenuItem("Open All"))
        {
            for (const auto& window : m_imgui_windows)
            {
                window->show();
            }
            //m_show_tool_properties = true;
        }
        ImGui::EndMenu();
    }

    ImGui::PopStyleVar();
#endif
}

auto Imgui_windows::want_capture_mouse() const -> bool
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    return ImGui::GetIO(m_imgui_context).WantCaptureMouse;
#else
    return false;
#endif
}

void Imgui_windows::register_imgui_window(Imgui_window* window)
{
    const std::lock_guard<std::mutex> lock{m_mutex};

    window->initialize(*m_components);

#ifndef NDEBUG
    const auto i = std::find(m_imgui_windows.begin(), m_imgui_windows.end(), window);
    if (i != m_imgui_windows.end())
    {
        log_windows->error("Window {} already registered as ImGui Window", window->title());
    }
    else
#endif
    {
        m_imgui_windows.emplace_back(window);
        std::sort(
            m_imgui_windows.begin(),
            m_imgui_windows.end(),
            [](const gsl::not_null<Imgui_window*>& lhs, const gsl::not_null<Imgui_window*>& rhs)
            {
                return lhs->title() < rhs->title();
            }
        );
    }
}

#if 0
void Imgui_windows::rendertarget_imgui_windows()
{
    for (const auto& rendertarget : m_rendertarget_imgui_windows)
    {
        rendertarget->imgui_windows();
    }
}
#endif

void Imgui_windows::imgui_windows()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION

    Scoped_imgui_context scoped_context{m_imgui_context};

    begin_imgui_frame();

    menu();

    std::size_t i = 0;
    bool any_mouse_input_sink{false};
    for (auto& imgui_window : m_imgui_windows)
    {
        if (imgui_window->is_visible())
        {
            auto imgui_id = fmt::format("##window-{}", ++i);
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

    if (m_show_style_editor)
    {
        if (ImGui::Begin("Dear ImGui Style Editor", &m_show_style_editor))
        {
            ImGui::ShowStyleEditor();
        }
        ImGui::End();
    }

    end_imgui_frame();
#endif
}

#if 0
void Editor_imgui_windows::render_rendertarget_gui_meshes(
    const Render_context& context
)
{
    auto* forward_renderer = get<Forward_renderer>().get();
    if (forward_renderer == nullptr)
    {
        return;
    }
    for (const auto& rendertarget : m_rendertarget_imgui_windows)
    {
        rendertarget->render_mesh_layer(
            *forward_renderer,
            context
        );
    }
}
#endif

void Imgui_windows::on_focus(int focused)
{
#if !defined(ERHE_GUI_LIBRARY_IMGUI)
    static_cast<void>(focused);
#else
    // TODO must make sure context is current
    ImGuiIO& io = ImGui::GetIO(m_imgui_context);
    io.AddFocusEvent(focused != 0);
#endif
}

void Imgui_windows::on_cursor_enter(int entered)
{
    m_has_cursor = (entered != 0);
}

void Imgui_windows::on_mouse_click(
    const uint32_t button,
    const int      count
)
{
#if !defined(ERHE_GUI_LIBRARY_IMGUI)
    static_cast<void>(button);
    static_cast<void>(count);
#else
    if (
        (button < ImGuiMouseButton_COUNT) &&
        (count > 0)
    )
    {
        m_mouse_just_pressed[button] = true;
    }
#endif
}

void Imgui_windows::on_mouse_wheel(
    const double x,
    const double y
)
{
#if !defined(ERHE_GUI_LIBRARY_IMGUI)
    static_cast<void>(x);
    static_cast<void>(y);
#else
    ImGuiIO& io = ImGui::GetIO(m_imgui_context);
    io.MouseWheelH += static_cast<float>(x);
    io.MouseWheel  += static_cast<float>(y);

    ///// for (const auto& rendertarget : m_rendertarget_imgui_windows)
    ///// {
    /////     rendertarget->on_mouse_wheel(x, y);
    ///// }
#endif
}

void Imgui_windows::make_imgui_context_current()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION

    ImGui::SetCurrentContext(m_imgui_context);
#endif
}

void Imgui_windows::make_imgui_context_uncurrent()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION

    ImGui::SetCurrentContext(nullptr);
#endif
}

void Imgui_windows::on_key(
    const signed int keycode,
    const uint32_t   modifier_mask,
    const bool       pressed
)
{
#if !defined(ERHE_GUI_LIBRARY_IMGUI)
    static_cast<void>(keycode);
    static_cast<void>(modifier_mask);
    static_cast<void>(pressed);
#else
    ///// for (const auto& rendertarget : m_rendertarget_imgui_windows)
    ///// {
    /////     rendertarget->on_key(keycode, modifier_mask, pressed);
    ///// }

    using erhe::toolkit::Keycode;

    ImGuiIO& io = ImGui::GetIO(m_imgui_context);
    update_key_modifiers(io, modifier_mask);
    io.AddKeyEvent(from_erhe(keycode), pressed);
#endif
}

void Imgui_windows::on_char(
    const unsigned int codepoint
)
{
#if !defined(ERHE_GUI_LIBRARY_IMGUI)
    static_cast<void>(codepoint);
#else
    ImGuiIO& io = ImGui::GetIO(m_imgui_context);
    io.AddInputCharacter(codepoint);
#endif
}

}  // namespace editor
