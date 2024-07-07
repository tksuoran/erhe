#include "example.hpp"

#include "example_log.hpp"
#include "frame_controller.hpp"
#include "mesh_memory.hpp"
#include "programs.hpp"

#include "erhe_dataformat/dataformat_log.hpp"
#include "erhe_gl/enum_bit_mask_operators.hpp"
#include "erhe_gl/gl_log.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_gltf/gltf.hpp"
#include "erhe_gltf/gltf_log.hpp"
#include "erhe_gltf/image_transfer.hpp"
#include "erhe_graphics/buffer_transfer_queue.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/instance.hpp"
#include "erhe_graphics/pipeline.hpp"
#include "erhe_item/item_log.hpp"
#include "erhe_log/log.hpp"
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
#include "erhe_window/renderdoc_capture.hpp"
#include "erhe_window/window_log.hpp"
#include "erhe_verify/verify.hpp"
#include "erhe_window/window.hpp"
#include "erhe_window/window.hpp"
#include "erhe_window/window_event_handler.hpp"
#include "erhe_ui/ui_log.hpp"

namespace example {

class Example : public erhe::window::Window_event_handler
{
public:
    virtual auto get_name() const -> const char* { return "Example"; }

    Example(
        erhe::window::Context_window&           window,
        erhe::scene::Scene&                     scene,
        erhe::graphics::Instance&               graphics_instance,
        erhe::scene_renderer::Forward_renderer& forward_renderer,
        erhe::gltf::Gltf_data&                  gltf_data,
        Mesh_memory&                            mesh_memory,
        Programs&                               programs
    )
        : m_window           {window}
        , m_scene            {scene}
        , m_graphics_instance{graphics_instance}
        , m_forward_renderer {forward_renderer}
        , m_gltf_data        {gltf_data}
        , m_mesh_memory      {mesh_memory}
        , m_programs         {programs}
    {
        m_camera = make_camera(
            "Camera",
            glm::vec3{0.0f, 0.0f, 10.0f},
            glm::vec3{0.0f, 0.0f, -1.0f}
        );
        m_light = make_point_light(
            "Light",
            glm::vec3{0.0f, 0.0f, 4.0f}, // position
            glm::vec3{1.0f, 1.0f, 1.0f}, // color
            40.0f
        );

        m_camera_controller = std::make_shared<Frame_controller>();
        m_camera_controller->set_node(m_camera->get_node());

        auto& root_event_handler = m_window.get_root_window_event_handler();
        root_event_handler.attach(this, 3);
    }

    void run()
    {
        m_current_time = std::chrono::steady_clock::now();
        while (!m_close_requested) {
            m_window.poll_events();
            update();
            m_window.swap_buffers();
        }
    }

    void update()
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
        m_scene.update_node_transforms();

        gl::enable(gl::Enable_cap::framebuffer_srgb);
        gl::clear_color(0.1f, 0.1f, 0.1f, 1.0f);
        gl::clear_stencil(0);
        gl::clear_depth_f(*m_graphics_instance.depth_clear_value_pointer());
        gl::clear(
            gl::Clear_buffer_mask::color_buffer_bit |
            gl::Clear_buffer_mask::depth_buffer_bit |
            gl::Clear_buffer_mask::stencil_buffer_bit
        );

        std::vector<erhe::renderer::Pipeline_renderpass*> passes;


        const bool reverse_depth = m_graphics_instance.configuration.reverse_depth;
        erhe::renderer::Pipeline_renderpass standard_pipeline_renderpass{ 
            erhe::graphics::Pipeline{
                erhe::graphics::Pipeline_data{
                    .name           = "Standard Renderpass",
                    .shader_stages  = &m_programs.standard,
                    .vertex_input   = &m_mesh_memory.vertex_input,
                    .input_assembly = erhe::graphics::Input_assembly_state::triangles,
                    .rasterization  = erhe::graphics::Rasterization_state::cull_mode_back_ccw,
                    .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_enabled_stencil_test_disabled(reverse_depth),
                    .color_blend    = erhe::graphics::Color_blend_state::color_blend_disabled
                }
            }
        };

        passes.push_back(&standard_pipeline_renderpass);

        erhe::math::Viewport viewport{
            .x             = 0,
            .y             = 0,
            .width         = m_window.get_width(),
            .height        = m_window.get_height(),
            .reverse_depth = m_graphics_instance.configuration.reverse_depth
        };

        std::vector<std::shared_ptr<erhe::scene::Light>> lights;
        for (const auto& light : m_gltf_data.lights) {
            lights.push_back(light);
        }
        lights.push_back(m_light);

        erhe::scene_renderer::Light_projections light_projections{
            lights,
            m_camera.get(),
            erhe::math::Viewport{},
            std::shared_ptr<erhe::graphics::Texture>{},
            0
        };

        m_forward_renderer.render(
            erhe::scene_renderer::Forward_renderer::Render_parameters{
                .index_type             = erhe::dataformat::Format::format_32_scalar_uint,
                .ambient_light          = glm::vec3{0.1f, 0.1f, 0.1f},
                .camera                 = m_camera.get(),
                .light_projections      = &light_projections,
                .lights                 = lights,
                .skins                  = m_gltf_data.skins,
                .materials              = m_gltf_data.materials,
                .mesh_spans             = { m_gltf_data.meshes },
                .passes                 = passes,
                .primitive_mode         = erhe::primitive::Primitive_mode::polygon_fill,
                .primitive_settings     = erhe::scene_renderer::Primitive_interface_settings{},
                .shadow_texture         = nullptr,
                .viewport               = viewport,
                .filter                 = erhe::Item_filter{},
                .override_shader_stages = nullptr,
                .debug_label            = "example main render"
            }
        );

        m_forward_renderer.next_frame();
    }
    void update_fixed_step()
    {
        m_camera_controller->update_fixed_step();
    }

    // Implements erhe::window::Window_event_handler
    auto on_close() -> bool override
    {
        m_close_requested = true;
        return true;
    }

    auto on_key(erhe::window::Keycode code, uint32_t, bool pressed) -> bool override
    {
        switch (code) {
            case erhe::window::Key_w: m_camera_controller->translate_z.set_less(pressed); return true;
            case erhe::window::Key_s: m_camera_controller->translate_z.set_more(pressed); return true;
            case erhe::window::Key_a: m_camera_controller->translate_x.set_less(pressed); return true;
            case erhe::window::Key_d: m_camera_controller->translate_x.set_more(pressed); return true;
            default: return false;
        }
    }

    auto on_mouse_move(float, float, float relative_x, float relative_y, uint32_t) -> bool override
    {
        if (!m_mouse_pressed) {
            return false;
        }
        if (relative_x != 0.0f) {
            m_camera_controller->rotate_y.adjust(relative_x * (-1.0f / 1024.0f));
        }

        if (relative_y != 0.0f) {
            m_camera_controller->rotate_x.adjust(relative_y * (-1.0f / 1024.0f));
        }
        return true;
    }

    auto on_mouse_button(erhe::window::Mouse_button button, bool pressed, uint32_t) -> bool override
    {
        if (button != erhe::window::Mouse_button_left) {
            return false;
        }
        m_mouse_pressed = pressed;
        return true;
    }

    auto on_mouse_wheel(float, float y, uint32_t) -> bool override
    {
        glm::vec3 position = m_camera_controller->get_position();
        const float l = glm::length(position);
        const float k = (-1.0f / 16.0f) * l * l * y;
        m_camera_controller->get_controller(Control::translate_z).adjust(k);
        return true;
    }

private:
    auto make_camera(
        const std::string_view name,
        const glm::vec3        position,
        const glm::vec3        look_at
    ) -> std::shared_ptr<erhe::scene::Camera>
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

    erhe::window::Context_window&           m_window;
    erhe::scene::Scene&                     m_scene;
    erhe::graphics::Instance&               m_graphics_instance;
    erhe::scene_renderer::Forward_renderer& m_forward_renderer;
    erhe::gltf::Gltf_data&                  m_gltf_data;
    Mesh_memory&                            m_mesh_memory;
    Programs&                               m_programs;

    bool                                    m_close_requested{false};
    std::shared_ptr<erhe::scene::Camera>    m_camera;
    std::shared_ptr<erhe::scene::Light>     m_light;
    std::shared_ptr<Frame_controller>       m_camera_controller;
    std::chrono::steady_clock::time_point   m_current_time;
    double                                  m_time_accumulator{0.0};
    double                                  m_time            {0.0};
    uint64_t                                m_frame_number{0};

    bool m_mouse_pressed{false};
};

void run_example()
{
    erhe::log::initialize_log_sinks();

    example::initialize_logging();
    gl::initialize_logging();
    erhe::dataformat::initialize_logging();
    erhe::item::initialize_logging();
    erhe::gltf::initialize_logging();
    erhe::graphics::initialize_logging();
    erhe::primitive::initialize_logging();
    erhe::raytrace::initialize_logging();
    erhe::renderer::initialize_logging();
    erhe::scene::initialize_logging();
    erhe::scene_renderer::initialize_logging();
    erhe::window::initialize_logging();
    erhe::ui::initialize_logging();
    erhe::window::initialize_frame_capture();

    erhe::window::Context_window window{
        erhe::window::Window_configuration{
            .gl_major          = 4,
            .gl_minor          = 6,
            .width             = 1920,
            .height            = 1080,
            .msaa_sample_count = 0,
            .title             = "erhe example"
        }
    };

    erhe::scene::Scene_message_bus          scene_message_bus{};
    erhe::scene::Scene                      scene            {scene_message_bus, "example scene", nullptr};
    erhe::graphics::Instance                graphics_instance{window};
    erhe::gltf::Image_transfer              image_transfer   {graphics_instance};
    erhe::scene_renderer::Program_interface program_interface{graphics_instance};
    erhe::scene_renderer::Forward_renderer  forward_renderer {graphics_instance, program_interface};
    Mesh_memory                             mesh_memory      {graphics_instance, program_interface};
    Programs                                programs         {graphics_instance, program_interface};

    erhe::gltf::Gltf_data gltf_data = erhe::gltf::parse_gltf(
        erhe::gltf::Gltf_parse_arguments{
            .graphics_instance = graphics_instance,
            .image_transfer    = image_transfer,
            .root_node         = scene.get_root_node(),
            //.path              = "res/models/Box.gltf"
            .path              = "res/models/SM_Deccer_Cubes_Textured.glb"
        }
    );

    // Convert triangle soup vertex and index data to GL buffers
    erhe::primitive::Buffer_info buffer_info{
        .usage         = gl::Buffer_usage::static_draw,
        .index_type    = erhe::dataformat::Format::format_32_scalar_uint,
        .vertex_format = mesh_memory.vertex_format,
        .buffer_sink   = mesh_memory.gl_buffer_sink
    };

    for (const auto& node : gltf_data.nodes) {
        auto mesh = erhe::scene::get_mesh(node.get());
        if (mesh) {
            std::vector<erhe::primitive::Primitive>& primitives = mesh->get_mutable_primitives();
            for (erhe::primitive::Primitive& primitive : primitives) {
                if (!primitive.has_renderable_triangles() && primitive.get_triangle_soup()) {
                    // TODO This looks a bit awkward
                    primitive.get_renderable_mesh() = erhe::primitive::build_renderable_mesh_from_triangle_soup(
                        *primitive.get_triangle_soup().get(),
                        buffer_info
                    );
                }
            }
        }
    }

    mesh_memory.gl_buffer_transfer_queue.flush();
    gl::clip_control(gl::Clip_control_origin::lower_left, gl::Clip_control_depth::zero_to_one);
    gl::enable      (gl::Enable_cap::framebuffer_srgb);

    Example example{window, scene, graphics_instance, forward_renderer, gltf_data, mesh_memory, programs};
    example.run();
}

} // namespace example

