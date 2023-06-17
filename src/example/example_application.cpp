#include "example_application.hpp"
#include "example_log.hpp"
#include "frame_controller.hpp"
#include "gltf.hpp"
#include "image_transfer.hpp"
#include "mesh_memory.hpp"
#include "programs.hpp"

#include "erhe/renderer/forward_renderer.hpp"
#include "erhe/renderer/program_interface.hpp"
#include "erhe/renderer/shadow_renderer.hpp"

#include "erhe/application/application_log.hpp"
#include "erhe/application/configuration.hpp"
#include "erhe/application/graphics/gl_context_provider.hpp"
#include "erhe/application/graphics/shader_monitor.hpp"
#include "erhe/application/renderdoc_capture_support.hpp"
#include "erhe/application/window.hpp"
#include "erhe/gl/wrapper_functions.hpp"
#include "erhe/gl/enum_bit_mask_operators.hpp"
#include "erhe/graphics/buffer_transfer_queue.hpp"
#include "erhe/graphics/debug.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/graphics/state/vertex_input_state.hpp"
#include "erhe/renderer/pipeline_renderpass.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/scene/scene_message_bus.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/window.hpp"
#include "erhe/toolkit/verify.hpp"

#include "erhe/application/application_log.hpp"
#include "erhe/components/components_log.hpp"
#include "erhe/gl/gl_log.hpp"
#include "erhe/graphics/graphics_log.hpp"
#include "erhe/log/log.hpp"
#include "erhe/primitive/primitive_log.hpp"
#include "erhe/renderer/renderer_log.hpp"
#include "erhe/scene/scene_log.hpp"
#include "erhe/toolkit/toolkit_log.hpp"
#include "erhe/ui/ui_log.hpp"

namespace example {

class Application_impl
    : public erhe::toolkit::View
{
public:
    Application_impl();
    ~Application_impl();

    auto initialize_components(Application* application, int argc, char** argv) -> bool;
    void component_initialization_complete(bool initialization_succeeded);

    void run              ();
    void update           ();
    void update_fixed_step();

    // Implements erhe::toolkit::View
    void on_close       ()                                                                  override;
    void on_key         (erhe::toolkit::Keycode code, uint32_t modifier_mask, bool pressed) override;
    void on_mouse_move  (float x, float y)                                                  override;
    void on_mouse_button(erhe::toolkit::Mouse_button button, bool pressed)                  override;
    void on_mouse_wheel (float x, float y)                                                  override;

private:
    [[nodiscard]] auto make_camera(
        const std::string_view name,
        const glm::vec3        position,
        const glm::vec3        look_at
    ) -> std::shared_ptr<erhe::scene::Camera>;
    [[nodiscard]] auto make_directional_light(
        const std::string_view name,
        const glm::vec3        position,
        const glm::vec3        color,
        const float            intensity
    ) -> std::shared_ptr<erhe::scene::Light>;
    [[nodiscard]] auto make_point_light(
        const std::string_view name,
        const glm::vec3        position,
        const glm::vec3        color,
        const float            intensity
    ) -> std::shared_ptr<erhe::scene::Light>;

    erhe::components::Components                 m_components;

    erhe::application::Configuration             configuration;
    erhe::application::Gl_context_provider       gl_context_provider;
    erhe::application::Renderdoc_capture_support renderdoc_capture_support;
    erhe::application::Shader_monitor            shader_monitor;
    erhe::application::Window                    window;

    erhe::graphics::OpenGL_state_tracker         opengl_state_tracker;
    erhe::renderer::Forward_renderer             forward_renderer;
    erhe::renderer::Program_interface            program_interface;
    erhe::renderer::Shadow_renderer              shadow_renderer;
    erhe::scene::Scene_message_bus               scene_message_bus;

    example::Mesh_memory                mesh_memory;
    example::Programs                   programs   ;

    std::unique_ptr<erhe::scene::Scene>  m_scene;
    std::unique_ptr<Parse_context>       m_gltf_parse_context;
    std::unique_ptr<Image_transfer>      m_image_transfer;
    std::shared_ptr<erhe::scene::Camera> m_camera;
    std::shared_ptr<erhe::scene::Light>  m_light;
    erhe::renderer::Pipeline_renderpass  m_standard_pipeline_renderpass;

    std::shared_ptr<Frame_controller> m_camera_controller;
    bool                              m_close_requested{false};

    std::chrono::steady_clock::time_point m_current_time;
    double                                m_time_accumulator{0.0};
    double                                m_time            {0.0};
    uint64_t                              m_frame_number{0};

    bool  m_mouse_pressed{false};
    float m_mouse_last_x{0.0f};
    float m_mouse_last_y{0.0f};
};

using erhe::graphics::OpenGL_state_tracker;
using std::shared_ptr;
using std::make_shared;

void init_logs()
{
    //erhe::log::console_init();
    erhe::log::initialize_log_sinks();
    erhe::application::initialize_logging();
    erhe::components::initialize_logging();
    gl::initialize_logging();
    erhe::graphics::initialize_logging();
    erhe::primitive::initialize_logging();
    erhe::renderer::initialize_logging();
    erhe::scene::initialize_logging();
    erhe::toolkit::initialize_logging();
    erhe::ui::initialize_logging();
    example::initialize_logging();
}

Application_impl::Application_impl() = default;

Application_impl::~Application_impl()
{
    m_components.cleanup_components();
}

auto Application_impl::initialize_components(
    Application* application,
    const int    argc,
    char**       argv
) -> bool
{
    erhe::application::log_startup->info("Parsing args");
    configuration.parse_args(argc, argv);
    {
        erhe::application::log_startup->info("Adding components");
        m_components.add(application               );
        m_components.add(&configuration            );
        m_components.add(&renderdoc_capture_support);
        m_components.add(&window                   );
        m_components.add(&gl_context_provider      );
        m_components.add(&shader_monitor           );
        m_components.add(&opengl_state_tracker     );

        m_components.add(&forward_renderer );
        m_components.add(&mesh_memory      );
        m_components.add(&program_interface);
        m_components.add(&programs         );
        m_components.add(&scene_message_bus);
        m_components.add(&shadow_renderer  );
    }

    erhe::application::log_startup->info("Initializing early components");
    configuration.initialize_component();
    renderdoc_capture_support.initialize_component();

    erhe::application::log_startup->info("Creating window");
    if (!window.create_gl_window()) {
        erhe::application::log_startup->error("GL window creation failed, aborting");
        return false;
    }

    erhe::application::log_startup->info("Launching component initialization");
    m_components.launch_component_initialization(configuration.threading.parallel_initialization);

    if (configuration.threading.parallel_initialization) {
        erhe::application::log_startup->info("Parallel init -> Providing worker GL contexts");
        gl_context_provider.provide_worker_contexts(
            window.get_context_window(),
            [this]() -> bool
            {
                return !m_components.is_component_initialization_complete();
            }
        );
    }

    erhe::application::log_startup->info("Waiting for component initialization to complete");
    m_components.wait_component_initialization_complete();

    erhe::application::log_startup->info("Component initialization complete");
    component_initialization_complete(true);

    gl::clip_control           (gl::Clip_control_origin::lower_left, gl::Clip_control_depth::zero_to_one);
    gl::disable                (gl::Enable_cap::primitive_restart);
    gl::enable                 (gl::Enable_cap::primitive_restart_fixed_index);
    gl::enable                 (gl::Enable_cap::texture_cube_map_seamless);
    gl::enable                 (gl::Enable_cap::framebuffer_srgb);
    gl::primitive_restart_index(0xffffu);

    const bool reverse_depth = erhe::application::g_configuration->graphics.reverse_depth;

    m_standard_pipeline_renderpass.pipeline.data = erhe::graphics::Pipeline_data{
        .name           = "Standard Renderpass",
        .shader_stages  = g_programs->standard.get(),
        .vertex_input   = g_mesh_memory->get_vertex_input(),
        .input_assembly = erhe::graphics::Input_assembly_state::triangles,
        .rasterization  = erhe::graphics::Rasterization_state::cull_mode_back_cw(reverse_depth),
        .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_enabled_stencil_test_disabled(reverse_depth),
        .color_blend    = erhe::graphics::Color_blend_state::color_blend_disabled
    };

    opengl_state_tracker.on_thread_enter();

    return true;
}

auto Application_impl::make_camera(
    std::string_view name,
    glm::vec3        position,
    glm::vec3        look_at
) -> std::shared_ptr<erhe::scene::Camera>
{
    using Item_flags = erhe::scene::Item_flags;

    auto node   = std::make_shared<erhe::scene::Node>(fmt::format("{} node", name));
    auto camera = std::make_shared<erhe::scene::Camera>(name);
    camera->projection()->fov_y           = glm::radians(45.0f);
    camera->projection()->projection_type = erhe::scene::Projection::Type::perspective_vertical;
    camera->projection()->z_near          = 1.0f / 128.0f;
    camera->projection()->z_far           = 512.0f;
    camera->enable_flag_bits(Item_flags::content | Item_flags::show_in_ui);
    node->attach(camera);
    node->set_parent(m_scene->get_root_node());

    const glm::mat4 m = erhe::toolkit::create_look_at(
        position, // eye
        look_at,  // center
        glm::vec3{0.0f, 1.0f, 0.0f}  // up
    );
    node->set_parent_from_node(m);
    node->enable_flag_bits(Item_flags::content | Item_flags::show_in_ui);

    return camera;
}

auto Application_impl::make_directional_light(
    const std::string_view name,
    const glm::vec3        position,
    const glm::vec3        color,
    const float            intensity
) -> std::shared_ptr<erhe::scene::Light>
{
    using Item_flags = erhe::scene::Item_flags;

    auto node  = std::make_shared<erhe::scene::Node>(fmt::format("{} node", name));
    auto light = std::make_shared<erhe::scene::Light>(name);
    light->type      = erhe::scene::Light::Type::directional;
    light->color     = color;
    light->intensity = intensity;
    light->range     = 0.0f;
    light->layer_id  = 0;
    light->enable_flag_bits(Item_flags::content | Item_flags::visible | Item_flags::show_in_ui);
    node->attach          (light);
    node->set_parent      (m_scene->get_root_node());
    node->enable_flag_bits(Item_flags::content | Item_flags::visible | Item_flags::show_in_ui);

    const glm::mat4 m = erhe::toolkit::create_look_at(
        position,                     // eye
        glm::vec3{0.0f, 0.0f, 0.0f},  // center
        glm::vec3{0.0f, 1.0f, 0.0f}   // up
    );
    node->set_parent_from_node(m);

    return light;
}
auto Application_impl::make_point_light(
    const std::string_view name,
    const glm::vec3        position,
    const glm::vec3        color,
    const float            intensity
) -> std::shared_ptr<erhe::scene::Light>
{
    using Item_flags = erhe::scene::Item_flags;

    auto node  = std::make_shared<erhe::scene::Node>(fmt::format("{} node", name));
    auto light = std::make_shared<erhe::scene::Light>(name);
    light->type      = erhe::scene::Light::Type::point;
    light->color     = color;
    light->intensity = intensity;
    light->range     = 25.0f;
    light->layer_id  = 0;
    light->enable_flag_bits(Item_flags::content | Item_flags::visible | Item_flags::show_in_ui);
    node->attach          (light);
    node->set_parent      (m_scene->get_root_node());
    node->enable_flag_bits(Item_flags::content | Item_flags::visible | Item_flags::show_in_ui);

    const glm::mat4 m = erhe::toolkit::create_translation<float>(position);
    node->set_parent_from_node(m);

    return light;
}

void Application_impl::component_initialization_complete(const bool initialization_succeeded)
{
    if (!initialization_succeeded) {
        return;
    }

    if (erhe::application::g_window == nullptr) {
        return;
    }

    auto* const context_window = window.get_context_window();
    if (context_window == nullptr) {
        return;
    }

    auto& root_view = context_window->get_root_view();

    root_view.reset_view(this);

    m_image_transfer = std::make_unique<Image_transfer>();
    m_scene = std::make_unique<erhe::scene::Scene>("scene");
    m_gltf_parse_context = std::make_unique<Parse_context>(
        g_mesh_memory->gl_vertex_format(),
        *m_image_transfer.get(),
        *m_scene.get()
    );
    m_gltf_parse_context->buffer_sink = g_mesh_memory->gl_buffer_sink.get();
    m_gltf_parse_context->path        = "res/models/Box.gltf";
    parse_gltf(*m_gltf_parse_context.get());

    m_camera = make_camera(
        "Camera",
        glm::vec3{0.0f, 0.0f, 10.0f},
        glm::vec3{0.0f, 0.0f, -1.0f}
    );
    //m_light = make_directional_light(
    //    "Light",
    //    glm::vec3{5.0f, 10.0f, 5.0f},
    //    glm::vec3{1.0f, 1.0f, 1.0f},
    //    10.0f
    //);
    m_light = make_point_light(
        "Light",
        glm::vec3{0.0f, 0.0f, 4.0f}, // position
        glm::vec3{1.0f, 1.0f, 1.0f}, // color
        40.0f
    );

    m_camera_controller = std::make_shared<Frame_controller>();
    m_camera_controller->set_node(m_camera->get_node());
}

void Application_impl::run()
{
    m_current_time = std::chrono::steady_clock::now();
    for (;;) {
        if (m_close_requested) {
            break;
        }
        erhe::application::g_window->get_context_window()->poll_events();
        if (m_close_requested) {
            break;
        }

        update();
        erhe::application::g_window->get_context_window()->swap_buffers();
    }
}

void Application_impl::on_close()
{
    m_close_requested = true;
}

void Application_impl::on_key(
    const erhe::toolkit::Keycode code,
    const uint32_t               modifier_mask,
    const bool                   pressed
)
{
    static_cast<void>(modifier_mask);
    switch (code) {
        case erhe::toolkit::Key_w: m_camera_controller->translate_z.set_less(pressed); break;
        case erhe::toolkit::Key_s: m_camera_controller->translate_z.set_more(pressed); break;
        case erhe::toolkit::Key_a: m_camera_controller->translate_x.set_less(pressed); break;
        case erhe::toolkit::Key_d: m_camera_controller->translate_x.set_more(pressed); break;
        default: return;
    }
}

void Application_impl::on_mouse_move(const float x, const float y)
{
    if (!m_mouse_pressed) {
        m_mouse_last_x = x;
        m_mouse_last_y = y;
        return;
    }
    float dx = x - m_mouse_last_x;
    float dy = y - m_mouse_last_y;
    m_mouse_last_x = x;
    m_mouse_last_y = y;
    if (dx != 0.0f) {
        m_camera_controller->rotate_y.adjust(dx * (-1.0f / 1024.0f));
    }

    if (dy != 0.0f) {
        m_camera_controller->rotate_x.adjust(dy * (-1.0f / 1024.0f));
    }
}

void Application_impl::on_mouse_button(const erhe::toolkit::Mouse_button button, const bool pressed)
{
    if (button != erhe::toolkit::Mouse_button_left) {
        return;
    }
    m_mouse_pressed = pressed;
}

void Application_impl::on_mouse_wheel(float x, float y)
{
    static_cast<void>(x);
    glm::vec3 position = m_camera_controller->get_position();
    const float l = glm::length(position);
    const float k = (-1.0f / 16.0f) * l * l * y;
    m_camera_controller->get_controller(Control::translate_z).adjust(k);
}

void Application_impl::update_fixed_step()
{
    m_camera_controller->update_fixed_step();
}

void Application_impl::update()
{
    // Update fixed steps
    {
        const auto new_time   = std::chrono::steady_clock::now();
        const auto duration   = new_time - m_current_time;
        double     frame_time = std::chrono::duration<double, std::ratio<1>>(duration).count();

        if (frame_time > 0.25) {
            frame_time = 0.25;
        }

        m_current_time = new_time;
        m_time_accumulator += frame_time;
        const double dt = 1.0 / 100.0;
        while (m_time_accumulator >= dt) {
            update_fixed_step();
            m_time_accumulator -= dt;
            m_time += dt;
        }
    }

    m_camera_controller->update();
    m_scene->update_node_transforms();

    g_mesh_memory->gl_buffer_transfer_queue->flush();
    gl::enable(gl::Enable_cap::framebuffer_srgb);
    gl::clear_color(0.1f, 0.1f, 0.1f, 1.0f);
    gl::clear_stencil(0);
    gl::clear_depth_f(*erhe::application::g_configuration->depth_clear_value_pointer());
    gl::clear(
        gl::Clear_buffer_mask::color_buffer_bit |
        gl::Clear_buffer_mask::depth_buffer_bit |
        gl::Clear_buffer_mask::stencil_buffer_bit
    );

    std::vector<erhe::renderer::Pipeline_renderpass*> passes;
    passes.push_back(&m_standard_pipeline_renderpass);

    erhe::scene::Viewport viewport{
        .x             = 0,
        .y             = 0,
        .width         = width(),
        .height        = height(),
        .reverse_depth = erhe::application::g_configuration->graphics.reverse_depth
    };

    std::vector<std::shared_ptr<erhe::scene::Light>> lights;
    for (const auto& light : m_gltf_parse_context->lights) {
        lights.push_back(light);
    }
    lights.push_back(m_light);

    erhe::renderer::Light_projections light_projections{
        lights,
        m_camera.get(),
        erhe::scene::Viewport{},
        std::shared_ptr<erhe::graphics::Texture>{},
        0
    };

    erhe::renderer::g_forward_renderer->render(
        erhe::renderer::Forward_renderer::Render_parameters{
            .vertex_input_state     = g_mesh_memory->get_vertex_input(),
            .index_type             = gl::Draw_elements_type::unsigned_int,
            .ambient_light          = glm::vec3{0.1f, 0.1f, 0.1f},
            .camera                 = m_camera.get(),
            .light_projections      = &light_projections,
            .lights                 = lights,
            .skins                  = m_gltf_parse_context->skins,
            .materials              = m_gltf_parse_context->materials,
            .mesh_spans             = { m_gltf_parse_context->meshes },
            .passes                 = passes,
            .primitive_mode         = erhe::primitive::Primitive_mode::polygon_fill,
            .primitive_settings     = erhe::renderer::Primitive_interface_settings{},
            .shadow_texture         = nullptr,
            .viewport               = viewport,
            .filter                 = erhe::scene::Item_filter{},
            .override_shader_stages = nullptr
        }
    );

    if (erhe::renderer::g_forward_renderer != nullptr) erhe::renderer::g_forward_renderer->next_frame();
    if (erhe::renderer::g_shadow_renderer  != nullptr) erhe::renderer::g_shadow_renderer ->next_frame();
}

Application* g_application{nullptr};

Application::Application()
    : erhe::components::Component{c_type_name}
{
    init_logs();
    erhe::application::log_startup->info("Logs have been initialized");
    m_impl = new Application_impl;
}

Application::~Application() noexcept
{
    ERHE_VERIFY(g_application == this);
    delete m_impl;
    g_application = this;
}

void Application::initialize_component()
{
    ERHE_VERIFY(g_application == nullptr);
    g_application = this;
}

auto Application::initialize_components(const int argc, char** argv) -> bool
{
    return m_impl->initialize_components(this, argc, argv);
}

void Application::run()
{
    m_impl->run();
}

} // namespace example

