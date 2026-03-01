#include "example.hpp"

#include "example_log.hpp"
#include "frame_controller.hpp"
#include "mesh_memory.hpp"
#include "programs.hpp"

#include "erhe_dataformat/dataformat_log.hpp"
#include "erhe_configuration/configuration.hpp"
#include "erhe_file/file.hpp"
#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
# include "erhe_gl/gl_log.hpp"
#endif
#include "erhe_gltf/gltf.hpp"
#include "erhe_gltf/gltf_log.hpp"
#include "erhe_gltf/image_transfer.hpp"
#include "erhe_graphics/buffer_transfer_queue.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/render_pipeline_state.hpp"
#include "erhe_graphics/swapchain.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_item/item_log.hpp"
#include "erhe_log/log.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_math/math_log.hpp"
#include "erhe_primitive/primitive_log.hpp"
#include "erhe_raytrace/raytrace_log.hpp"
#include "erhe_renderer/pipeline_renderpass.hpp"
#include "erhe_renderer/renderer_log.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/scene_log.hpp"
#include "erhe_scene/scene_message_bus.hpp"
#include "erhe_scene_renderer/forward_renderer.hpp"
#include "erhe_scene_renderer/program_interface.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"
#include "erhe_ui/ui_log.hpp"
#include "erhe_verify/verify.hpp"
#include "erhe_window/renderdoc_capture.hpp"
#include "erhe_window/window.hpp"
#include "erhe_window/window_event_handler.hpp"
#include "erhe_window/window_log.hpp"

#include <taskflow/taskflow.hpp>

#include <atomic>
#include <thread>

namespace example {

class Example : public erhe::window::Input_event_handler
{
public:
    Example()
        : m_window{
            erhe::window::Window_configuration{
                .use_depth = true,
                .size      = glm::ivec2{1920, 1080},
                .title     = "erhe example"
            }
        }
        , m_scene_message_bus{}
        , m_scene            {m_scene_message_bus, "example scene", nullptr}
        , m_graphics_device{
            erhe::graphics::Surface_create_info{
                .context_window            = &m_window,
                .prefer_low_bandwidth      = false,
                .prefer_high_dynamic_range = false
            }
        }
        , m_image_transfer   {m_graphics_device}
        , m_mesh_memory      {m_graphics_device}
        , m_program_interface{m_graphics_device, m_mesh_memory.vertex_format}
        , m_forward_renderer {m_graphics_device, m_program_interface}
        , m_programs         {m_graphics_device, m_program_interface}
    {
        const unsigned int thread_count = std::thread::hardware_concurrency();
        tf::Executor executor{thread_count};

        m_gltf_data = erhe::gltf::parse_gltf(
            erhe::gltf::Gltf_parse_arguments{
                .graphics_device = m_graphics_device,
                .executor        = executor,
                .image_transfer  = m_image_transfer,
                .root_node       = m_scene.get_root_node(),
                //.path          = "res/models/Box.gltf"
                .path            = "res/models/SM_Deccer_Cubes_Textured.glb"
            }
        );

        // Convert triangle soup vertex and index data to GL buffers
        erhe::primitive::Buffer_info buffer_info{
            .index_type    = erhe::dataformat::Format::format_32_scalar_uint,
            .vertex_format = m_mesh_memory.vertex_format,
            .buffer_sink   = m_mesh_memory.graphics_buffer_sink
        };

        for (const auto& node : m_gltf_data.nodes) {
            auto mesh = erhe::scene::get_mesh(node.get());
            if (mesh) {
                std::vector<erhe::scene::Mesh_primitive>& mesh_primitives = mesh->get_mutable_primitives();
                for (erhe::scene::Mesh_primitive& mesh_primitive : mesh_primitives) {
                    erhe::primitive::Primitive& primitive = *mesh_primitive.primitive.get();
                    if (!primitive.has_renderable_triangles()) {
                        ERHE_VERIFY(primitive.make_renderable_mesh(buffer_info));
                    }
                }
            }
        }

        m_mesh_memory.buffer_transfer_queue.flush();

        m_camera = make_camera("Camera", glm::vec3{0.0f, 0.0f, 10.0f}, glm::vec3{0.0f, 0.0f, -1.0f});
        m_light = make_point_light("Light",
            glm::vec3{0.0f, 0.0f, 4.0f}, // position
            glm::vec3{1.0f, 1.0f, 1.0f}, // color
            40.0f
        );

        m_camera_controller = std::make_shared<Frame_controller>();
        m_camera_controller->set_node(m_camera->get_node());

        m_last_window_width  = m_window.get_width();
        m_last_window_height = m_window.get_height();

        m_window.register_redraw_callback(
            [this](){
                if (
                    (m_last_window_width  != m_window.get_width ()) ||
                    (m_last_window_height != m_window.get_height())
                ) {
                    m_request_resize_pending.store(true);
                    m_last_window_width  = m_window.get_width();
                    m_last_window_height = m_window.get_height();
                }
                tick();
            }
        );
    }

    std::optional<erhe::window::Input_event> m_window_resize_event{};
    int               m_last_window_width     {0};
    int               m_last_window_height    {0};
    uint32_t          m_swapchain_width       {0};
    uint32_t          m_swapchain_height      {0};
    std::atomic<bool> m_request_resize_pending{false};

    auto on_window_resize_event(const erhe::window::Input_event& input_event) -> bool override
    {
        m_window_resize_event = input_event;
        return true;
    }

    auto on_window_close_event(const erhe::window::Input_event&) -> bool override
    {
        m_close_requested = true;
        return true;
    }

    auto on_key_event(const erhe::window::Input_event& input_event) -> bool override
    {
        const bool pressed = input_event.u.key_event.pressed;
        switch (input_event.u.key_event.keycode) {
            case erhe::window::Key_w: m_camera_controller->translate_z.set_less(pressed); return true;
            case erhe::window::Key_s: m_camera_controller->translate_z.set_more(pressed); return true;
            case erhe::window::Key_a: m_camera_controller->translate_x.set_less(pressed); return true;
            case erhe::window::Key_d: m_camera_controller->translate_x.set_more(pressed); return true;
            default: return false;
        }
    }

    auto on_mouse_move_event(const erhe::window::Input_event& input_event) -> bool override
    {
        const erhe::window::Mouse_move_event& mouse_move_event = input_event.u.mouse_move_event;
        if (m_mouse_pressed) {
            if (mouse_move_event.dx != 0.0f) {
                m_camera_controller->rotate_y.adjust(mouse_move_event.dx * (-1.0f / 1024.0f));
            }

            if (mouse_move_event.dy != 0.0f) {
                m_camera_controller->rotate_x.adjust(mouse_move_event.dy * (-1.0f / 1024.0f));
            }
            return true;
        }
        return false;
    }

    auto on_mouse_button_event(const erhe::window::Input_event& input_event) -> bool override
    {
        const erhe::window::Mouse_button_event& mouse_button_event = input_event.u.mouse_button_event;
        if (mouse_button_event.button == erhe::window::Mouse_button_left) {
            m_mouse_pressed = mouse_button_event.pressed;
            m_window.set_cursor_relative_hold(m_mouse_pressed);
            return true;
        }
        return false;
    }
    
    auto on_mouse_wheel_event(const erhe::window::Input_event& input_event) -> bool override
    {
        const erhe::window::Mouse_wheel_event& mouse_wheel_event = input_event.u.mouse_wheel_event;
        glm::vec3 position = m_camera_controller->get_position();
        const float l = glm::length(position);
        const float k = (-1.0f / 16.0f) * l * l * mouse_wheel_event.y;
        m_camera_controller->get_controller(Control::translate_z).adjust(k);
        return true;
    }

    void run()
    {
        m_current_time = std::chrono::steady_clock::now();
        while (!m_close_requested) {
            erhe::graphics::Frame_state frame_state{};
            const bool wait_ok = m_graphics_device.wait_frame(frame_state);
            ERHE_VERIFY(wait_ok);

            // m_graphics_device.wait_for_idle()
            // m_window.delay_before_swap(1.0f / 120.0f); // sleep half the frame

            m_window.poll_events();
            auto& input_events = m_window.get_input_events();
            for (erhe::window::Input_event& input_event : input_events) {
                static_cast<void>(this->dispatch_input_event(input_event));
            }

            if (m_window_resize_event.has_value()) {
                m_request_resize_pending.store(true);
                m_window_resize_event.reset();
                m_last_window_width  = m_window.get_width();
                m_last_window_height = m_window.get_height();
            }

            const erhe::graphics::Frame_begin_info frame_begin_info{
                .resize_width   = static_cast<uint32_t>(m_last_window_width),
                .resize_height  = static_cast<uint32_t>(m_last_window_height),
                .request_resize = m_request_resize_pending.load()
            };
            m_request_resize_pending.store(false);

            const bool begin_frame_ok = m_graphics_device.begin_frame(frame_begin_info);
            ERHE_VERIFY(begin_frame_ok);

            tick();

            const erhe::graphics::Frame_end_info frame_end_info{
                .requested_display_time = 0 // TODO
            };
            const bool end_frame_ok = m_graphics_device.end_frame(frame_end_info);
            ERHE_VERIFY(end_frame_ok);
        }
    }

    void tick()
    {
        const auto tick_end_time = std::chrono::steady_clock::now();

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
            const double dt = 1.0 / 240.0;
            while (m_time_accumulator >= dt) {
                m_camera_controller->update_fixed_step();
                m_time_accumulator -= dt;
                m_time += dt;
            }
        }

        erhe::math::Viewport viewport{
            .x      = 0,
            .y      = 0,
            .width  = m_window.get_width(),
            .height = m_window.get_height()
        };

        m_scene.update_node_transforms();

        std::vector<erhe::renderer::Pipeline_pass*> passes;
        erhe::renderer::Pipeline_pass standard_pipeline_renderpass{
            erhe::graphics::Render_pipeline_state{
                erhe::graphics::Render_pipeline_data{
                    .name           = "Example Render_pass",
                    .shader_stages  = &m_programs.standard,
                    .vertex_input   = &m_mesh_memory.vertex_input,
                    .input_assembly = erhe::graphics::Input_assembly_state::triangle,
                    .rasterization  = erhe::graphics::Rasterization_state::cull_mode_back_ccw,
                    .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_enabled_stencil_test_disabled(),
                    .color_blend    = erhe::graphics::Color_blend_state::color_blend_disabled
                }
            }
        };
        passes.push_back(&standard_pipeline_renderpass);

        std::vector<std::shared_ptr<erhe::scene::Light>> lights;
        for (const auto& light : m_gltf_data.lights) {
            lights.push_back(light);
        }
        lights.push_back(m_light);

        m_light_projections.apply(
            lights,
            m_camera.get(),
            viewport,
            erhe::math::Viewport{},
            std::shared_ptr<erhe::graphics::Texture>{}
        );

        std::vector<std::shared_ptr<erhe::scene::Mesh>> meshes;
        for (const auto& node : m_gltf_data.nodes) {
            std::shared_ptr<erhe::scene::Mesh> mesh = get_mesh(node.get());
            if (!mesh) {
                continue;
            }
            meshes.push_back(mesh);
        }

        update_render_pass(viewport.width, viewport.height);

        erhe::graphics::Render_command_encoder render_encoder = m_graphics_device.make_render_command_encoder(*m_render_pass.get());

        m_forward_renderer.render(
            erhe::scene_renderer::Forward_renderer::Render_parameters{
                .render_encoder         = render_encoder,
                .index_type             = erhe::dataformat::Format::format_32_scalar_uint,
                .index_buffer           = &m_mesh_memory.index_buffer,
                .vertex_buffer0         = &m_mesh_memory.position_vertex_buffer,
                .vertex_buffer1         = &m_mesh_memory.non_position_vertex_buffer,
                .ambient_light          = glm::vec3{0.1f, 0.1f, 0.1f},
                .camera                 = m_camera.get(),
                .light_projections      = &m_light_projections,
                .lights                 = lights,
                .skins                  = m_gltf_data.skins,
                .materials              = m_gltf_data.materials,
                .mesh_spans             = { meshes },
                .passes                 = passes,
                .primitive_mode         = erhe::primitive::Primitive_mode::polygon_fill,
                .primitive_settings     = erhe::scene_renderer::Primitive_interface_settings{},
                .viewport               = viewport,
                .filter                 = erhe::Item_filter{},
                .override_shader_stages = nullptr,
                .debug_label            = "example main render"
            }
        );
    }

private:
    void update_render_pass(int width, int height)
    {
        if (
            m_render_pass &&
            (m_render_pass->get_render_target_width () == width) &&
            (m_render_pass->get_render_target_height() == height)
        ) {
            return;
        }

        m_render_pass.reset();
        erhe::graphics::Render_pass_descriptor render_pass_descriptor;
        render_pass_descriptor.swapchain = m_graphics_device.get_surface()->get_swapchain();
        render_pass_descriptor.color_attachments[0].load_action    = erhe::graphics::Load_action::Clear;
        render_pass_descriptor.color_attachments[0].clear_value[0] = 0.02;
        render_pass_descriptor.color_attachments[0].clear_value[1] = 0.02;
        render_pass_descriptor.color_attachments[0].clear_value[2] = 0.02;
        render_pass_descriptor.color_attachments[0].clear_value[3] = 1.0;
        render_pass_descriptor.depth_attachment    .load_action    = erhe::graphics::Load_action::Clear;
        render_pass_descriptor.stencil_attachment  .load_action    = erhe::graphics::Load_action::Clear;
        render_pass_descriptor.render_target_width  = width;
        render_pass_descriptor.render_target_height = height;
        render_pass_descriptor.debug_label          = "Example Render_pass";
        m_render_pass = std::make_unique<erhe::graphics::Render_pass>(m_graphics_device, render_pass_descriptor);
    }

    auto make_camera(const std::string_view name, const glm::vec3 position, const glm::vec3 look_at) -> std::shared_ptr<erhe::scene::Camera>
    {
        using Item_flags = erhe::Item_flags;

        auto node   = std::make_shared<erhe::scene::Node>(name);
        auto camera = std::make_shared<erhe::scene::Camera>(name);
        camera->projection()->fov_y           = glm::radians(45.0f);
        camera->projection()->projection_type = erhe::scene::Projection::Type::perspective_vertical;
        camera->projection()->z_near          = 1.0f / 128.0f;
        camera->projection()->z_far           = 512.0f;
        camera->enable_flag_bits(Item_flags::content | Item_flags::show_in_ui);
        node->attach(camera);
        node->set_parent(m_scene.get_root_node());

        const glm::mat4 m = erhe::math::create_look_at(
            position, // eye
            look_at,  // center
            glm::vec3{0.0f, 1.0f, 0.0f}  // up
        );
        node->set_parent_from_node(m);
        node->enable_flag_bits(Item_flags::content | Item_flags::show_in_ui);

        return camera;
    }

    auto make_directional_light(
        const std::string_view name,
        const glm::vec3        position,
        const glm::vec3        color,
        const float            intensity
    ) -> std::shared_ptr<erhe::scene::Light>
    {
        using Item_flags = erhe::Item_flags;

        auto node  = std::make_shared<erhe::scene::Node>(name);
        auto light = std::make_shared<erhe::scene::Light>(name);
        light->type      = erhe::scene::Light::Type::directional;
        light->color     = color;
        light->intensity = intensity;
        light->range     = 0.0f;
        light->layer_id  = 0;
        light->enable_flag_bits(Item_flags::content | Item_flags::visible | Item_flags::show_in_ui);
        node->attach          (light);
        node->set_parent      (m_scene.get_root_node());
        node->enable_flag_bits(Item_flags::content | Item_flags::visible | Item_flags::show_in_ui);

        const glm::mat4 m = erhe::math::create_look_at(
            position,                     // eye
            glm::vec3{0.0f, 0.0f, 0.0f},  // center
            glm::vec3{0.0f, 1.0f, 0.0f}   // up
        );
        node->set_parent_from_node(m);

        return light;
    }

    auto make_point_light(
        const std::string_view name,
        const glm::vec3        position,
        const glm::vec3        color,
        const float            intensity
    ) -> std::shared_ptr<erhe::scene::Light>
    {
        using Item_flags = erhe::Item_flags;

        auto node  = std::make_shared<erhe::scene::Node>(name);
        auto light = std::make_shared<erhe::scene::Light>(name);
        light->type      = erhe::scene::Light::Type::point;
        light->color     = color;
        light->intensity = intensity;
        light->range     = 25.0f;
        light->layer_id  = 0;
        light->enable_flag_bits(Item_flags::content | Item_flags::visible | Item_flags::show_in_ui);
        node->attach          (light);
        node->set_parent      (m_scene.get_root_node());
        node->enable_flag_bits(Item_flags::content | Item_flags::visible | Item_flags::show_in_ui);

        const glm::mat4 m = erhe::math::create_translation<float>(position);
        node->set_parent_from_node(m);

        return light;
    }

    erhe::window::Context_window                 m_window;
    erhe::scene::Scene_message_bus               m_scene_message_bus;
    erhe::scene::Scene                           m_scene;
    erhe::graphics::Device                       m_graphics_device;
    erhe::gltf::Image_transfer                   m_image_transfer;
    Mesh_memory                                  m_mesh_memory;
    erhe::scene_renderer::Program_interface      m_program_interface;
    erhe::scene_renderer::Forward_renderer       m_forward_renderer;
    std::unique_ptr<erhe::graphics::Render_pass> m_render_pass;
    Programs                                     m_programs;

    erhe::scene_renderer::Light_projections m_light_projections;

    erhe::gltf::Gltf_data                   m_gltf_data;
    bool                                    m_close_requested{false};
    std::shared_ptr<erhe::scene::Camera>    m_camera;
    std::shared_ptr<erhe::scene::Light>     m_light;
    std::shared_ptr<Frame_controller>       m_camera_controller;
    std::chrono::steady_clock::time_point   m_current_time;
    double                                  m_time_accumulator{0.0};
    double                                  m_time            {0.0};
    uint64_t                                m_frame_number    {0};
    bool                                    m_mouse_pressed   {false};
};

void run_example()
{
    // Workaround for
    // https://intellij-support.jetbrains.com/hc/en-us/community/posts/27792220824466-CMake-C-git-project-How-to-share-working-directory-in-git
    erhe::file::ensure_working_directory_contains("example", erhe::c_erhe_config_file_path);

    erhe::log::initialize_log_sinks();

    example::initialize_logging();
#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
    gl::initialize_logging();
#endif
    erhe::dataformat::initialize_logging();
    erhe::gltf::initialize_logging();
    erhe::graphics::initialize_logging();
    erhe::item::initialize_logging();
    erhe::math::initialize_logging();
    erhe::primitive::initialize_logging();
    erhe::raytrace::initialize_logging();
    erhe::renderer::initialize_logging();
    erhe::scene::initialize_logging();
    erhe::scene_renderer::initialize_logging();
    erhe::window::initialize_logging();
    erhe::ui::initialize_logging();

    //erhe::window::initialize_frame_capture();

    Example example{};
    example.run();
}

} // namespace example

