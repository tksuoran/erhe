#include "rendering_test.hpp"
#include "rendering_test_log.hpp"

#include "erhe_codegen/config_io.hpp"
#include "erhe_file/file.hpp"
#include "erhe_graphics/generated/graphics_config_serialization.hpp"
#include "erhe_file/file_log.hpp"
#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
# include "erhe_gl/gl_log.hpp"
#endif
#include "erhe_verify/verify.hpp"

#include <SDL3/SDL.h>
#include "erhe_dataformat/dataformat_log.hpp"
#include "erhe_gltf/gltf_log.hpp"
#include "erhe_gltf/image_transfer.hpp"
#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/buffer_transfer_queue.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/ring_buffer.hpp"
#include "erhe_graphics/fragment_outputs.hpp"
#include "erhe_graphics/span.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/render_pipeline_state.hpp"
#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/swapchain.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_graphics/texture_heap.hpp"
#include "erhe_item/item_log.hpp"
#include "erhe_log/log.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_math/math_log.hpp"
#include "erhe_primitive/primitive_log.hpp"
#include "erhe_raytrace/raytrace_log.hpp"
#include "erhe_renderer/renderer_log.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/scene_log.hpp"
#include "erhe_scene_renderer/forward_renderer.hpp"
#include "erhe_scene_renderer/primitive_buffer.hpp"
#include "erhe_scene_renderer/program_interface.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"
#include "erhe_ui/ui_log.hpp"
#include "erhe_window/renderdoc_capture.hpp"
#include "erhe_window/window.hpp"
#include "erhe_window/window_event_handler.hpp"
#include "erhe_window/window_log.hpp"

#include "mesh_memory.hpp"
#include "programs.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/shapes/box.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene_renderer/content_wide_line_renderer.hpp"

#include <fmt/format.h>
#include <simdjson.h>

#include <atomic>
#include <thread>

namespace rendering_test {

class Rendering_test : public erhe::window::Input_event_handler
{
public:
    Rendering_test()
        : m_window{
            erhe::window::Window_configuration{
                .use_depth = true,
                .size      = glm::ivec2{1280, 720},
                .title     = "erhe rendering test"
            }
        }
        , m_graphics_device{
            erhe::graphics::Surface_create_info{
                .context_window            = &m_window,
                .prefer_low_bandwidth      = false,
                .prefer_high_dynamic_range = false
            },
            erhe::codegen::load_config<Graphics_config>("config/erhe_graphics.json")
        }
        , m_error_callback_set{(
            m_graphics_device.set_shader_error_callback(
                [](const std::string& error_log, const std::string& shader_source, const std::string& callstack) {
                    std::string clipboard_text = "=== Shader Error ===\n" + error_log + "\n=== Shader Source ===\n" + shader_source + "\n=== Callstack ===\n" + callstack;
                    SDL_SetClipboardText(clipboard_text.c_str());
                    ERHE_FATAL("Shader compilation/linking failed (error and source copied to clipboard)");
                }
            ),
            m_graphics_device.set_device_error_callback(
                [](const std::string& error_message, const std::string& callstack) {
                    std::string clipboard_text = error_message + "\n=== Callstack ===\n" + callstack;
                    SDL_SetClipboardText(clipboard_text.c_str());
                    ERHE_FATAL("Device error (copied to clipboard): %s", error_message.c_str());
                }
            ),
        true)}
        , m_image_transfer   {m_graphics_device}
        , m_mesh_memory      {m_graphics_device}
        , m_program_interface{m_graphics_device, m_mesh_memory.vertex_format, m_program_interface_config}
        , m_programs         {m_graphics_device, m_program_interface}
        , m_forward_renderer {m_graphics_device, m_program_interface, &m_programs.default_uniform_block}
        , m_scene            {"rendering test scene", nullptr}
    {
        print_conventions();
        create_test_scene();

        m_last_window_width  = m_window.get_width();
        m_last_window_height = m_window.get_height();

#if !defined(ERHE_GRAPHICS_LIBRARY_METAL)
        m_window.register_redraw_callback(
            [this](){
                if (!m_first_frame_rendered || m_in_tick.load()) {
                    return;
                }
                if (
                    (m_last_window_width  != m_window.get_width ()) ||
                    (m_last_window_height != m_window.get_height())
                ) {
                    m_request_resize_pending.store(true);
                    m_last_window_width  = m_window.get_width();
                    m_last_window_height = m_window.get_height();
                }
            }
        );
#endif

        make_render_pipeline_states();
        make_edge_lines_pipeline();
        make_content_wide_line_renderer();
        make_quad_pipeline();
        make_multi_texture_pipeline();
        make_separate_samplers_pipeline();
        create_test_textures();
    }

    int               m_last_window_width     {0};
    int               m_last_window_height    {0};
    std::atomic<bool> m_request_resize_pending{false};
    std::optional<erhe::window::Input_event> m_window_resize_event{};

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
        if (input_event.u.key_event.pressed && (input_event.u.key_event.keycode == erhe::window::Key_escape)) {
            m_close_requested = true;
            return true;
        }
        return false;
    }

    void run()
    {
        while (!m_close_requested) {
            erhe::graphics::Frame_state frame_state{};
            const bool wait_ok = m_graphics_device.wait_frame(frame_state);
            ERHE_VERIFY(wait_ok);

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

            m_in_tick.store(true);
            tick();
            m_in_tick.store(false);
            m_first_frame_rendered = true;

            const erhe::graphics::Frame_end_info frame_end_info{
                .requested_display_time = 0
            };
            const bool end_frame_ok = m_graphics_device.end_frame(frame_end_info);
            ERHE_VERIFY(end_frame_ok);
        }
    }

    void render_scene(
        erhe::graphics::Render_command_encoder&                 render_encoder,
        const erhe::math::Viewport&                             viewport,
        const std::vector<std::shared_ptr<erhe::scene::Light>>& lights,
        const std::vector<std::shared_ptr<erhe::scene::Mesh>>&  meshes
    )
    {
        render_encoder.set_viewport_rect(viewport.x, viewport.y, viewport.width, viewport.height);
        render_encoder.set_scissor_rect(viewport.x, viewport.y, viewport.width, viewport.height);

        const auto& conventions = m_graphics_device.get_info().coordinate_conventions;
        m_forward_renderer.render(
            erhe::scene_renderer::Forward_renderer::Render_parameters{
                .render_encoder         = render_encoder,
                .index_type             = erhe::dataformat::Format::format_32_scalar_uint,
                .index_buffer           = &m_mesh_memory.index_buffer,
                .vertex_buffer0         = &m_mesh_memory.position_vertex_buffer,
                .vertex_buffer1         = &m_mesh_memory.non_position_vertex_buffer,
                .ambient_light          = glm::vec3{0.3f, 0.3f, 0.3f},
                .camera                 = m_camera.get(),
                .light_projections      = &m_light_projections,
                .lights                 = lights,
                .skins                  = {},
                .materials              = m_materials,
                .mesh_spans             = { meshes },
                .render_pipeline_states = m_render_pipeline_states,
                .primitive_mode         = erhe::primitive::Primitive_mode::polygon_fill,
                .primitive_settings     = erhe::scene_renderer::Primitive_interface_settings{},
                .viewport               = viewport,
                .filter                 = erhe::Item_filter{},
                .override_shader_stages = nullptr,
                .debug_label            = "rendering test render",
                .reverse_depth          = true,
                .depth_range            = conventions.native_depth_range,
                .conventions            = conventions
            }
        );
    }

    void tick()
    {
        const int full_width  = m_window.get_width();
        const int full_height = m_window.get_height();

        m_scene.update_node_transforms();

        const auto& conventions = m_graphics_device.get_info().coordinate_conventions;
        std::vector<std::shared_ptr<erhe::scene::Light>> lights;
        lights.push_back(m_light);

        const erhe::math::Viewport cell_00 = get_cell_viewport(0, 0, 2, 3);

        m_light_projections.apply(
            lights,
            m_camera.get(),
            cell_00,
            erhe::math::Viewport{},
            std::shared_ptr<erhe::graphics::Texture>{},
            true,
            conventions.native_depth_range,
            conventions
        );

        std::vector<std::shared_ptr<erhe::scene::Mesh>> meshes;
        meshes.push_back(m_test_cube);

        // --- Pass 1: render 3D scene to texture (for top-right cell) ---
        update_texture_render_pass(cell_00.width, cell_00.height);
        {
            erhe::graphics::Render_command_encoder encoder = m_graphics_device.make_render_command_encoder();
            erhe::graphics::Scoped_render_pass scoped_render_pass{*m_texture_render_pass.get()};
            render_scene(encoder, erhe::math::Viewport{0, 0, cell_00.width, cell_00.height}, lights, meshes);
        }

        // --- Compute pass: expand edge lines to triangles (Metal) ---
        if (m_content_wide_line_renderer && m_content_wide_line_renderer->is_enabled()) {
            m_content_wide_line_renderer->begin_frame();
            m_content_wide_line_renderer->add_mesh(*m_test_cube, glm::vec4{0.0f, 0.0f, 0.4f, 1.0f}, -6.0f);
            erhe::graphics::Compute_command_encoder compute_encoder = m_graphics_device.make_compute_command_encoder();
            const bool reverse_depth = (conventions.native_depth_range == erhe::math::Depth_range::zero_to_one);
            m_content_wide_line_renderer->compute(
                compute_encoder, cell_00, *m_camera.get(),
                reverse_depth,
                conventions.native_depth_range,
                conventions
            );
        }

        // --- Pass 2: swapchain pass (all cells) ---
        update_swapchain_render_pass(full_width, full_height);
        {
            erhe::graphics::Render_command_encoder encoder = m_graphics_device.make_render_command_encoder();
            erhe::graphics::Scoped_render_pass scoped_render_pass{*m_swapchain_render_pass.get()};

            // Top-left (0,0): 3D scene with wide edge lines rendered directly to swapchain
            {
                erhe::graphics::Scoped_debug_group cell_scope{"Cell (0,0) Scene + Edge Lines"};
                render_scene(encoder, cell_00, lights, meshes);
                render_edge_lines(encoder, cell_00, lights, meshes);

                // Compute-based edge lines
                if (m_content_wide_line_renderer && m_content_wide_line_renderer->is_enabled() && m_compute_edge_lines_pipeline_state) {
                    m_content_wide_line_renderer->render(encoder, *m_compute_edge_lines_pipeline_state);
                }
            }

            // Top-right (1,0): render-to-texture displayed via textured quad
            {
                erhe::graphics::Scoped_debug_group cell_scope{"Cell (1,0) RTT Quad"};
                const erhe::math::Viewport cell_10 = get_cell_viewport(1, 0, 2, 3);
                const bool top_left = (conventions.texture_origin == erhe::math::Texture_origin::top_left);
                const glm::vec2 uv_min = top_left ? glm::vec2{0.0f, 1.0f} : glm::vec2{0.0f, 0.0f};
                const glm::vec2 uv_max = top_left ? glm::vec2{1.0f, 0.0f} : glm::vec2{1.0f, 1.0f};

                erhe::graphics::Ring_buffer_range quad_buffer_range = m_quad_buffer.acquire(
                    erhe::graphics::Ring_buffer_usage::CPU_write,
                    m_quad_block->get_size_bytes()
                );
                const std::span<std::byte> gpu_data   = quad_buffer_range.get_span();
                const std::span<float>     float_data{reinterpret_cast<float*>(gpu_data.data()), gpu_data.size_bytes() / sizeof(float)};
                const std::span<uint32_t>  uint_data {reinterpret_cast<uint32_t*>(gpu_data.data()), gpu_data.size_bytes() / sizeof(uint32_t)};

                using erhe::graphics::as_span;
                using erhe::graphics::write;

                const uint64_t handle = m_graphics_device.get_handle(*m_color_texture.get(), m_nearest_sampler);
                const uint32_t texture_handle[2] = {
                    static_cast<uint32_t>(handle & 0xffffffffu),
                    static_cast<uint32_t>(handle >> 32u)
                };
                write(uint_data,  m_quad_texture_handle->get_offset_in_parent(), std::span<const uint32_t>{&texture_handle[0], 2});
                write(float_data, m_quad_uv_min->get_offset_in_parent(),         as_span(uv_min));
                write(float_data, m_quad_uv_max->get_offset_in_parent(),         as_span(uv_max));
                quad_buffer_range.bytes_written(m_quad_block->get_size_bytes());
                quad_buffer_range.close();

                encoder.set_render_pipeline_state(m_quad_pipeline);
                encoder.set_viewport_rect(cell_10.x, cell_10.y, cell_10.width, cell_10.height);
                encoder.set_scissor_rect(cell_10.x, cell_10.y, cell_10.width, cell_10.height);
                m_quad_buffer.bind(encoder, quad_buffer_range);

                erhe::graphics::Texture_heap texture_heap{m_graphics_device, *m_color_texture.get(), m_nearest_sampler, 1, m_test_bind_group_layout.get(), m_quad_default_uniform_block.get()};
                texture_heap.assign(0, m_color_texture.get(), &m_nearest_sampler);
                texture_heap.bind();

                encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 3);

                texture_heap.unbind();
                quad_buffer_range.release();
            }

            // Bottom-left (0,1): texture heap with 2 textures (red + green = yellow)
            {
                erhe::graphics::Scoped_debug_group cell_scope{"Cell (0,1) Multi-tex 2"};
                const erhe::math::Viewport cell_01 = get_cell_viewport(0, 1, 2, 3);
                std::vector<std::shared_ptr<erhe::graphics::Texture>> tex2{m_red_texture, m_green_texture};
                draw_multi_texture_quad(encoder, cell_01, tex2);
            }

            // Bottom-right (1,1): texture heap with 3 textures (red + green + blue = white)
            {
                erhe::graphics::Scoped_debug_group cell_scope{"Cell (1,1) Multi-tex 3"};
                const erhe::math::Viewport cell_11 = get_cell_viewport(1, 1, 2, 3);
                std::vector<std::shared_ptr<erhe::graphics::Texture>> tex3{m_red_texture, m_green_texture, m_blue_texture};
                draw_multi_texture_quad(encoder, cell_11, tex3);
            }

            // Row 2: Separate sampler binding tests
            // (2,0): 2 separate samplers (red + green = yellow)
            {
                erhe::graphics::Scoped_debug_group cell_scope{"Cell (0,2) Sep-samp 2"};
                const erhe::math::Viewport cell_02 = get_cell_viewport(0, 2, 2, 3);
                std::vector<std::shared_ptr<erhe::graphics::Texture>> tex2{m_red_texture, m_green_texture};
                draw_separate_samplers_quad(encoder, cell_02, tex2);
            }

            // (2,1): 3 separate samplers (red + green + blue = white)
            {
                erhe::graphics::Scoped_debug_group cell_scope{"Cell (1,2) Sep-samp 3"};
                const erhe::math::Viewport cell_12 = get_cell_viewport(1, 2, 2, 3);
                std::vector<std::shared_ptr<erhe::graphics::Texture>> tex3{m_red_texture, m_green_texture, m_blue_texture};
                draw_separate_samplers_quad(encoder, cell_12, tex3);
            }
        }

        if (m_content_wide_line_renderer && m_content_wide_line_renderer->is_enabled()) {
            m_content_wide_line_renderer->end_frame();
        }
    }

private:
    void print_conventions()
    {
        const auto& c = m_graphics_device.get_info().coordinate_conventions;
        const char* depth_str = (c.native_depth_range == erhe::math::Depth_range::zero_to_one)        ? "[0, 1]"      : "[-1, 1]";
        const char* fb_str    = (c.framebuffer_origin == erhe::math::Framebuffer_origin::bottom_left) ? "bottom-left" : "top-left";
        const char* flip_str  = (c.clip_space_y_flip  == erhe::math::Clip_space_y_flip::enabled)      ? "enabled"     : "disabled";
        const char* tex_str   = (c.texture_origin     == erhe::math::Texture_origin::bottom_left)     ? "bottom-left" : "top-left";
        const bool  y_flip    = (c.clip_space_y_flip  == erhe::math::Clip_space_y_flip::enabled);

        log_test->info("=== Coordinate Conventions ===");
        log_test->info("  Depth range:       {}", depth_str);
        log_test->info("  Framebuffer origin: {}", fb_str);
        log_test->info("  Clip space Y-flip: {}", flip_str);
        log_test->info("  Texture origin:    {}", tex_str);
        log_test->info("  Projection Y-flip: {}", y_flip ? "yes" : "no");
        log_test->info("==============================");
    }

    void create_test_scene()
    {
        // Camera at (1.5, 1.2, 2.4) looking at origin - close enough for cube to fill cell
        {
            auto node   = std::make_shared<erhe::scene::Node>("Camera");
            auto camera = std::make_shared<erhe::scene::Camera>("Camera");
            camera->projection()->fov_y           = glm::radians(50.0f);
            camera->projection()->projection_type = erhe::scene::Projection::Type::perspective_vertical;
            camera->projection()->z_near          = 0.1f;
            camera->projection()->z_far           = 100.0f;
            camera->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
            node->attach(camera);
            node->set_parent(m_scene.get_root_node());
            node->set_parent_from_node(erhe::math::create_look_at(
                glm::vec3{1.5f, 1.2f, 2.4f},
                glm::vec3{0.0f, 0.0f, 0.0f},
                glm::vec3{0.0f, 1.0f, 0.0f}
            ));
            node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
            m_camera = camera;
        }

        // Point light
        {
            auto node  = std::make_shared<erhe::scene::Node>("Light");
            auto light = std::make_shared<erhe::scene::Light>("Light");
            light->type      = erhe::scene::Light::Type::point;
            light->color     = glm::vec3{1.0f, 1.0f, 1.0f};
            light->intensity = 30.0f;
            light->range     = 30.0f;
            light->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
            node->attach(light);
            node->set_parent(m_scene.get_root_node());
            node->set_parent_from_node(erhe::math::create_look_at(
                glm::vec3{3.0f, 4.0f, 5.0f},
                glm::vec3{0.0f, 0.0f, 0.0f},
                glm::vec3{0.0f, 1.0f, 0.0f}
            ));
            node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
            m_light = light;
        }

        // Colored cube
        {
            auto material = std::make_shared<erhe::primitive::Material>(
                erhe::primitive::Material_create_info{.name = "Cube Material"}
            );
            m_materials.push_back(material);

            auto geometry = std::make_shared<erhe::geometry::Geometry>("Orientation Cube");
            erhe::geometry::shapes::make_box(geometry->get_mesh(), 1.0f, 1.0f, 1.0f);
            geometry->process(
                erhe::geometry::Geometry::process_flag_connect |
                erhe::geometry::Geometry::process_flag_build_edges |
                erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals
            );

            erhe::primitive::Build_info build_info{
                .primitive_types = {.fill_triangles = true, .edge_lines = true},
                .buffer_info = {
                    .index_type    = erhe::dataformat::Format::format_32_scalar_uint,
                    .vertex_format = m_mesh_memory.vertex_format,
                    .buffer_sink   = m_mesh_memory.graphics_buffer_sink
                }
            };

            auto primitive = std::make_shared<erhe::primitive::Primitive>(
                geometry, build_info, erhe::primitive::Normal_style::corner_normals
            );
            m_mesh_memory.buffer_transfer_queue.flush();

            const erhe::primitive::Buffer_mesh* buffer_mesh = primitive->get_renderable_mesh();
            if (buffer_mesh != nullptr) {
                const erhe::primitive::Index_range edge_range = buffer_mesh->index_range(erhe::primitive::Primitive_mode::edge_lines);
                log_test->info("Cube edge line index count: {}", edge_range.index_count);
                log_test->info(
                    "Cube edge line vertex buffer range: byte_offset={}, count={}, element_size={}",
                    buffer_mesh->edge_line_vertex_buffer_range.byte_offset,
                    buffer_mesh->edge_line_vertex_buffer_range.count,
                    buffer_mesh->edge_line_vertex_buffer_range.element_size
                );
            }

            auto node = std::make_shared<erhe::scene::Node>("Cube");
            auto mesh = std::make_shared<erhe::scene::Mesh>("Cube");
            mesh->add_primitive(primitive, material);
            mesh->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::visible | erhe::Item_flags::show_in_ui);
            node->attach(mesh);
            node->set_parent(m_scene.get_root_node());
            node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::visible | erhe::Item_flags::show_in_ui);
            m_test_cube = mesh;
        }
    }

    void update_swapchain_render_pass(const int width, const int height)
    {
        if (
            m_swapchain_render_pass &&
            (m_swapchain_render_pass->get_render_target_width () == width) &&
            (m_swapchain_render_pass->get_render_target_height() == height)
        ) {
            return;
        }
        m_swapchain_render_pass.reset();
        m_swapchain_depth_texture.reset();

        const erhe::dataformat::Format depth_format = m_graphics_device.choose_depth_stencil_format(
            erhe::graphics::format_flag_require_depth, 0
        );
        m_swapchain_depth_texture = std::make_unique<erhe::graphics::Texture>(
            m_graphics_device,
            erhe::graphics::Texture_create_info{
                .device      = m_graphics_device,
                .usage_mask  = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment,
                .type        = erhe::graphics::Texture_type::texture_2d,
                .pixelformat = depth_format,
                .width       = width,
                .height      = height,
                .debug_label = erhe::utility::Debug_label{"Swapchain depth"}
            }
        );

        erhe::graphics::Render_pass_descriptor desc;
        desc.swapchain = m_graphics_device.get_surface()->get_swapchain();
        desc.color_attachments[0].load_action    = erhe::graphics::Load_action::Clear;
        desc.color_attachments[0].clear_value[0] = 0.1;
        desc.color_attachments[0].clear_value[1] = 0.1;
        desc.color_attachments[0].clear_value[2] = 0.15;
        desc.color_attachments[0].clear_value[3] = 1.0;
        desc.depth_attachment.texture             = m_swapchain_depth_texture.get();
        desc.depth_attachment.load_action         = erhe::graphics::Load_action::Clear;
        desc.depth_attachment.clear_value[0]      = 0.0; // reverse depth: near=1, far=0, clear to 0 (far)
        desc.stencil_attachment.load_action       = erhe::graphics::Load_action::Clear;
        desc.render_target_width                  = width;
        desc.render_target_height                 = height;
        desc.debug_label                          = erhe::utility::Debug_label{"Swapchain Render_pass"};
        m_swapchain_render_pass = std::make_unique<erhe::graphics::Render_pass>(m_graphics_device, desc);
    }

    void update_texture_render_pass(const int width, const int height)
    {
        if (
            m_color_texture &&
            (m_color_texture->get_width () == width) &&
            (m_color_texture->get_height() == height)
        ) {
            return;
        }

        m_texture_render_pass.reset();
        m_color_texture.reset();
        m_depth_texture.reset();

        m_color_texture = std::make_shared<erhe::graphics::Texture>(
            m_graphics_device,
            erhe::graphics::Texture_create_info{
                .device      = m_graphics_device,
                .usage_mask  =
                    erhe::graphics::Image_usage_flag_bit_mask::color_attachment |
                    erhe::graphics::Image_usage_flag_bit_mask::sampled,
                .type        = erhe::graphics::Texture_type::texture_2d,
                .pixelformat = erhe::dataformat::Format::format_8_vec4_srgb,
                .width       = width,
                .height      = height,
                .debug_label = erhe::utility::Debug_label{"RTT color"}
            }
        );

        const erhe::dataformat::Format depth_format = m_graphics_device.choose_depth_stencil_format(
            erhe::graphics::format_flag_require_depth, 0
        );
        m_depth_texture = std::make_unique<erhe::graphics::Texture>(
            m_graphics_device,
            erhe::graphics::Texture_create_info{
                .device      = m_graphics_device,
                .usage_mask  = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment,
                .type        = erhe::graphics::Texture_type::texture_2d,
                .pixelformat = depth_format,
                .width       = width,
                .height      = height,
                .debug_label = erhe::utility::Debug_label{"RTT depth"}
            }
        );

        erhe::graphics::Render_pass_descriptor desc;
        desc.color_attachments[0].texture      = m_color_texture.get();
        desc.color_attachments[0].load_action  = erhe::graphics::Load_action::Clear;
        desc.color_attachments[0].clear_value[0] = 0.15;
        desc.color_attachments[0].clear_value[1] = 0.1;
        desc.color_attachments[0].clear_value[2] = 0.1;
        desc.color_attachments[0].clear_value[3] = 1.0;
        desc.color_attachments[0].store_action   = erhe::graphics::Store_action::Store;
        desc.depth_attachment.texture        = m_depth_texture.get();
        desc.depth_attachment.load_action    = erhe::graphics::Load_action::Clear;
        desc.depth_attachment.store_action   = erhe::graphics::Store_action::Dont_care;
        desc.depth_attachment.clear_value[0] = 0.0; // reverse depth
        desc.render_target_width             = width;
        desc.render_target_height            = height;
        desc.debug_label                     = erhe::utility::Debug_label{"RTT Render_pass"};
        m_texture_render_pass = std::make_unique<erhe::graphics::Render_pass>(m_graphics_device, desc);
    }

    void make_render_pipeline_states()
    {
        const auto& c = m_graphics_device.get_info().coordinate_conventions;
        const bool  y_flip = (c.clip_space_y_flip == erhe::math::Clip_space_y_flip::enabled);

        m_standard_render_pipeline_state = std::make_unique<erhe::graphics::Render_pipeline_state>(
            erhe::graphics::Render_pipeline_data{
                .debug_label    = erhe::utility::Debug_label{"Orientation Test Pipeline"},
                .shader_stages  = &m_programs.standard,
                .vertex_input   = &m_mesh_memory.vertex_input,
                .input_assembly = erhe::graphics::Input_assembly_state::triangle,
                .rasterization  = erhe::graphics::Rasterization_state::cull_mode_back_ccw.with_winding_flip_if(y_flip),
                .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_enabled_stencil_test_disabled(),
                .color_blend    = erhe::graphics::Color_blend_state::color_blend_disabled
            }
        );
        m_render_pipeline_states.push_back(m_standard_render_pipeline_state.get());
    }

    void make_edge_lines_pipeline()
    {
        if (!m_programs.wide_lines.is_valid()) {
            log_test->warn("wide_lines shader not valid, edge lines test disabled");
            return;
        }
        log_test->info("wide_lines shader valid, edge lines test enabled");
        m_edge_lines_pipeline_state = std::make_unique<erhe::graphics::Render_pipeline_state>(
            erhe::graphics::Render_pipeline_data{
                .debug_label    = erhe::utility::Debug_label{"Edge Lines Pipeline"},
                .shader_stages  = &m_programs.wide_lines,
                .vertex_input   = &m_mesh_memory.vertex_input,
                .input_assembly = erhe::graphics::Input_assembly_state::line,
                .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
                .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_enabled_stencil_test_disabled(),
                .color_blend    = erhe::graphics::Color_blend_state::color_blend_premultiplied
            }
        );
        m_edge_lines_pipeline_states.push_back(m_edge_lines_pipeline_state.get());
    }

    void make_content_wide_line_renderer()
    {
        if (!m_graphics_device.get_info().use_compute_shader) {
            return;
        }

        // First create the renderer without shaders to get the shader resource
        // definitions (SSBO structs, UBO layout). Then compile shaders using
        // those definitions. The renderer stays alive so the resources remain valid.
        m_content_wide_line_renderer = std::make_unique<erhe::scene_renderer::Content_wide_line_renderer>(
            m_graphics_device,
            m_mesh_memory.edge_line_vertex_buffer,
            nullptr,
            nullptr
        );

        // Build compute shader using renderer's resource definitions
        {
            using namespace erhe::graphics;
            const std::filesystem::path shader_path{"res/shaders"};
            Shader_stages_create_info create_info{
                .name             = "compute_before_content_line",
                .struct_types     = {
                    m_content_wide_line_renderer->get_edge_line_vertex_struct(),
                    m_content_wide_line_renderer->get_triangle_vertex_struct()
                },
                .interface_blocks = {
                    m_content_wide_line_renderer->get_edge_line_vertex_buffer_block(),
                    m_content_wide_line_renderer->get_triangle_vertex_buffer_block(),
                    m_content_wide_line_renderer->get_view_block()
                },
                .shaders = { { Shader_type::compute_shader, shader_path / "compute_before_content_line.comp" } },
                .bind_group_layout = m_content_wide_line_renderer->get_bind_group_layout(),
            };
            Shader_stages_prototype prototype{m_graphics_device, create_info};
            if (prototype.is_valid()) {
                m_compute_shader_stages = std::make_unique<Shader_stages>(m_graphics_device, std::move(prototype));
            }
        }

        // Build graphics shader (renders triangle output from compute)
        {
            using namespace erhe::graphics;
            const std::filesystem::path shader_path{"res/shaders"};
            Shader_stages_create_info create_info{
                .name             = "content_line_after_compute",
                .interface_blocks = { m_content_wide_line_renderer->get_view_block() },
                .fragment_outputs = &m_content_wide_line_renderer->get_fragment_outputs(),
                .vertex_format    = &m_content_wide_line_renderer->get_triangle_vertex_format(),
                .shaders = {
                    { Shader_type::vertex_shader,   shader_path / "line_after_compute.vert" },
                    { Shader_type::fragment_shader, shader_path / "line_after_compute.frag" }
                },
                .bind_group_layout = m_content_wide_line_renderer->get_bind_group_layout(),
            };
            Shader_stages_prototype prototype{m_graphics_device, create_info};
            if (prototype.is_valid()) {
                m_graphics_shader_stages = std::make_unique<Shader_stages>(m_graphics_device, std::move(prototype));
            }
        }

        if (m_compute_shader_stages && m_graphics_shader_stages) {
            // Now set the shader stages on the existing renderer
            m_content_wide_line_renderer->set_shader_stages(
                m_compute_shader_stages.get(),
                m_graphics_shader_stages.get()
            );

            // Create pipeline state for rendering compute output
            m_compute_edge_lines_pipeline_state = std::make_unique<erhe::graphics::Render_pipeline_state>(
                erhe::graphics::Render_pipeline_data{
                    .debug_label    = erhe::utility::Debug_label{"Compute Edge Lines Pipeline"},
                    .shader_stages  = m_content_wide_line_renderer->get_graphics_shader_stages(),
                    .vertex_input   = m_content_wide_line_renderer->get_vertex_input(),
                    .input_assembly = erhe::graphics::Input_assembly_state::triangle,
                    .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
                    .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_enabled_stencil_test_disabled(),
                    .color_blend    = erhe::graphics::Color_blend_state::color_blend_premultiplied
                }
            );
            log_test->info("Content wide line renderer enabled (compute path)");
        } else {
            m_content_wide_line_renderer.reset();
            log_test->warn("Failed to create compute/graphics shaders for content wide line renderer");
        }
    }

    void render_edge_lines(
        erhe::graphics::Render_command_encoder&                 render_encoder,
        const erhe::math::Viewport&                             viewport,
        const std::vector<std::shared_ptr<erhe::scene::Light>>& lights,
        const std::vector<std::shared_ptr<erhe::scene::Mesh>>&  meshes
    )
    {
        if (m_edge_lines_pipeline_states.empty()) {
            return;
        }

        render_encoder.set_viewport_rect(viewport.x, viewport.y, viewport.width, viewport.height);
        render_encoder.set_scissor_rect(viewport.x, viewport.y, viewport.width, viewport.height);

        const auto& conventions = m_graphics_device.get_info().coordinate_conventions;
        m_forward_renderer.render(
            erhe::scene_renderer::Forward_renderer::Render_parameters{
                .render_encoder         = render_encoder,
                .index_type             = erhe::dataformat::Format::format_32_scalar_uint,
                .index_buffer           = &m_mesh_memory.index_buffer,
                .vertex_buffer0         = &m_mesh_memory.position_vertex_buffer,
                .vertex_buffer1         = &m_mesh_memory.non_position_vertex_buffer,
                .ambient_light          = glm::vec3{0.3f, 0.3f, 0.3f},
                .camera                 = m_camera.get(),
                .light_projections      = &m_light_projections,
                .lights                 = lights,
                .skins                  = {},
                .materials              = m_materials,
                .mesh_spans             = { meshes },
                .render_pipeline_states = m_edge_lines_pipeline_states,
                .primitive_mode         = erhe::primitive::Primitive_mode::edge_lines,
                .primitive_settings     = erhe::scene_renderer::Primitive_interface_settings{
                    .color_source    = erhe::scene_renderer::Primitive_color_source::constant_color,
                    .constant_color0 = glm::vec4{0.0f, 0.0f, 0.4f, 1.0f},
                    .size_source     = erhe::scene_renderer::Primitive_size_source::constant_size,
                    .constant_size   = -6.0f
                },
                .viewport               = viewport,
                .filter                 = erhe::Item_filter{},
                .override_shader_stages = nullptr,
                .debug_label            = "rendering test edge lines",
                .reverse_depth          = true,
                .depth_range            = conventions.native_depth_range,
                .conventions            = conventions
            }
        );
    }

    void make_quad_pipeline()
    {
        const std::filesystem::path shader_path{"res/shaders"};
        const bool bindless = m_graphics_device.get_info().uses_bindless_texture();

        m_test_bind_group_layout = std::make_unique<erhe::graphics::Bind_group_layout>(
            m_graphics_device,
            erhe::graphics::Bind_group_layout_create_info{
                .bindings = {{0, erhe::graphics::Binding_type::uniform_buffer}},
                .debug_label = "Rendering test"
            }
        );
        m_quad_block = std::make_unique<erhe::graphics::Shader_resource>(
            m_graphics_device, "quad", 0, erhe::graphics::Shader_resource::Type::uniform_block
        );
        m_quad_texture_handle = m_quad_block->add_uvec2("texture_handle"); // offset  0, size 8
        m_quad_uv_min         = m_quad_block->add_vec2("uv_min");       // offset  8, size 8
        m_quad_uv_max         = m_quad_block->add_vec2("uv_max");       // offset 16, size 8
        m_quad_padding        = m_quad_block->add_vec2("padding");      // offset 24, size 8 -> total 32 = 2 x vec4

        if (!bindless) {
            m_quad_default_uniform_block = std::make_unique<erhe::graphics::Shader_resource>(m_graphics_device);
            m_quad_default_uniform_block->add_sampler(
                "s_textures",
                erhe::graphics::Glsl_type::sampler_2d,
                0, 1
            );
        }

        erhe::graphics::Shader_stages_create_info create_info{
            .name                  = "textured_quad",
            .interface_blocks      = { m_quad_block.get() },
            .fragment_outputs      = &m_quad_fragment_outputs,
            .default_uniform_block = bindless ? nullptr : m_quad_default_uniform_block.get(),
            .shaders = {
                { erhe::graphics::Shader_type::vertex_shader,   shader_path / "textured_quad.vert" },
                { erhe::graphics::Shader_type::fragment_shader, shader_path / "textured_quad.frag" }
            },
            .bind_group_layout = m_test_bind_group_layout.get(),
            .build = true
        };
        m_quad_shader_stages = std::make_unique<erhe::graphics::Shader_stages>(
            m_graphics_device,
            erhe::graphics::Shader_stages_prototype{m_graphics_device, create_info}
        );

        m_quad_pipeline = erhe::graphics::Render_pipeline_state{
            erhe::graphics::Render_pipeline_data{
                .debug_label    = erhe::utility::Debug_label{"Textured Quad Pipeline"},
                .shader_stages  = m_quad_shader_stages.get(),
                .vertex_input   = &m_empty_vertex_input,
                .input_assembly = erhe::graphics::Input_assembly_state::triangle,
                .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
                .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
                .color_blend    = erhe::graphics::Color_blend_state::color_blend_disabled
            }
        };
    }

    auto get_cell_viewport(int col, int row, int cols = 2, int rows = 2) const -> erhe::math::Viewport
    {
        const int full_width  = m_window.get_width();
        const int full_height = m_window.get_height();
        const int cell_width  = full_width  / cols;
        const int cell_height = full_height / rows;
        // On OpenGL, viewport Y=0 is at the bottom of the screen.
        // Flip row so that row 0 appears at the top.
        const auto& conventions = m_graphics_device.get_info().coordinate_conventions;
        const int y_row = (conventions.framebuffer_origin == erhe::math::Framebuffer_origin::bottom_left)
            ? (rows - 1 - row)
            : row;
        return erhe::math::Viewport{
            .x      = col * cell_width,
            .y      = y_row * cell_height,
            .width  = cell_width,
            .height = cell_height
        };
    }

    void create_test_textures()
    {
        // Create small solid-color textures for texture heap testing
        constexpr int tex_size = 4;

        auto make_solid_texture = [&](const char* name, uint8_t r, uint8_t g, uint8_t b) {
            auto texture = std::make_shared<erhe::graphics::Texture>(
                m_graphics_device,
                erhe::graphics::Texture_create_info{
                    .device      = m_graphics_device,
                    .usage_mask  =
                        erhe::graphics::Image_usage_flag_bit_mask::sampled |
                        erhe::graphics::Image_usage_flag_bit_mask::transfer_dst,
                    .type        = erhe::graphics::Texture_type::texture_2d,
                    .pixelformat = erhe::dataformat::Format::format_8_vec4_srgb,
                    .width       = tex_size,
                    .height      = tex_size,
                    .debug_label = erhe::utility::Debug_label{std::string_view{name}}
                }
            );
            uint8_t pixels[tex_size * tex_size * 4];
            for (int i = 0; i < tex_size * tex_size; ++i) {
                pixels[i * 4 + 0] = r;
                pixels[i * 4 + 1] = g;
                pixels[i * 4 + 2] = b;
                pixels[i * 4 + 3] = 255;
            }
            const int row_stride = tex_size * 4;
            const int byte_count = tex_size * row_stride;
            erhe::graphics::Ring_buffer_client upload_buf{
                m_graphics_device, erhe::graphics::Buffer_target::transfer_src, "test texture upload"
            };
            erhe::graphics::Ring_buffer_range range = upload_buf.acquire(erhe::graphics::Ring_buffer_usage::CPU_write, byte_count);
            memcpy(range.get_span().data(), pixels, byte_count);
            range.bytes_written(byte_count);
            range.close();
            erhe::graphics::Blit_command_encoder blit{m_graphics_device};
            blit.copy_from_buffer(
                range.get_buffer()->get_buffer(), range.get_byte_start_offset_in_buffer(),
                row_stride, byte_count,
                glm::ivec3{tex_size, tex_size, 1},
                texture.get(), 0, 0,
                glm::ivec3{0, 0, 0}
            );
            range.release();
            return texture;
        };
        m_red_texture   = make_solid_texture("test red",   255, 0, 0);
        m_green_texture = make_solid_texture("test green", 0, 255, 0);
        m_blue_texture  = make_solid_texture("test blue",  0, 0, 255);
    }

    void make_multi_texture_pipeline()
    {
        const std::filesystem::path shader_path{"res/shaders"};
        const bool bindless = m_graphics_device.get_info().uses_bindless_texture();

        m_multi_tex_block = std::make_unique<erhe::graphics::Shader_resource>(
            m_graphics_device, "multi_tex", 0, erhe::graphics::Shader_resource::Type::uniform_block
        );
        m_multi_tex_handle_0 = m_multi_tex_block->add_uvec2("texture_handle_0"); // offset  0, size  8
        m_multi_tex_handle_1 = m_multi_tex_block->add_uvec2("texture_handle_1"); // offset  8, size  8
        m_multi_tex_handle_2 = m_multi_tex_block->add_uvec2("texture_handle_2"); // offset 16, size  8
        m_multi_tex_count    = m_multi_tex_block->add_uint ("texture_count");    // offset 24, size  4
        m_multi_tex_padding0 = m_multi_tex_block->add_uint ("padding0");         // offset 28, size  4
                                                                                  // total  32 = 2 x vec4

        if (!bindless) {
            m_multi_tex_default_uniform_block = std::make_unique<erhe::graphics::Shader_resource>(m_graphics_device);
            m_multi_tex_default_uniform_block->add_sampler(
                "s_textures",
                erhe::graphics::Glsl_type::sampler_2d,
                0, 3
            );
        }

        erhe::graphics::Shader_stages_create_info create_info{
            .name                  = "multi_texture_test",
            .interface_blocks      = { m_multi_tex_block.get() },
            .fragment_outputs      = &m_quad_fragment_outputs,
            .default_uniform_block = bindless ? nullptr : m_multi_tex_default_uniform_block.get(),
            .shaders = {
                { erhe::graphics::Shader_type::vertex_shader,   shader_path / "multi_texture_test.vert" },
                { erhe::graphics::Shader_type::fragment_shader, shader_path / "multi_texture_test.frag" }
            },
            .bind_group_layout = m_test_bind_group_layout.get(),
            .build = true
        };
        m_multi_tex_shader_stages = std::make_unique<erhe::graphics::Shader_stages>(
            m_graphics_device,
            erhe::graphics::Shader_stages_prototype{m_graphics_device, create_info}
        );

        m_multi_tex_pipeline = erhe::graphics::Render_pipeline_state{
            erhe::graphics::Render_pipeline_data{
                .debug_label    = erhe::utility::Debug_label{"Multi Texture Test Pipeline"},
                .shader_stages  = m_multi_tex_shader_stages.get(),
                .vertex_input   = &m_empty_vertex_input,
                .input_assembly = erhe::graphics::Input_assembly_state::triangle,
                .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
                .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
                .color_blend    = erhe::graphics::Color_blend_state::color_blend_disabled
            }
        };
    }

    void draw_multi_texture_quad(
        erhe::graphics::Render_command_encoder&                      encoder,
        const erhe::math::Viewport&                                  viewport,
        const std::vector<std::shared_ptr<erhe::graphics::Texture>>& textures
    )
    {
        using erhe::graphics::as_span;
        using erhe::graphics::write;

        const uint32_t tex_count = static_cast<uint32_t>(textures.size());

        erhe::graphics::Ring_buffer_range buffer_range = m_quad_buffer.acquire(
            erhe::graphics::Ring_buffer_usage::CPU_write,
            m_multi_tex_block->get_size_bytes()
        );
        const std::span<std::byte> gpu_data   = buffer_range.get_span();
        const std::span<uint32_t>  uint_data  {reinterpret_cast<uint32_t*>(gpu_data.data()), gpu_data.size_bytes() / sizeof(uint32_t)};

        auto write_handle = [&](erhe::graphics::Shader_resource* field, const erhe::graphics::Texture& tex) {
            const uint64_t handle = m_graphics_device.get_handle(tex, m_nearest_sampler);
            const uint32_t h[2] = {
                static_cast<uint32_t>(handle & 0xffffffffu),
                static_cast<uint32_t>(handle >> 32u)
            };
            write(uint_data, field->get_offset_in_parent(), std::span<const uint32_t>{&h[0], 2});
        };

        if (tex_count > 0) write_handle(m_multi_tex_handle_0, *textures[0]);
        if (tex_count > 1) write_handle(m_multi_tex_handle_1, *textures[1]);
        if (tex_count > 2) write_handle(m_multi_tex_handle_2, *textures[2]);
        write(uint_data, m_multi_tex_count->get_offset_in_parent(), std::span<const uint32_t>{&tex_count, 1});

        buffer_range.bytes_written(m_multi_tex_block->get_size_bytes());
        buffer_range.close();

        encoder.set_render_pipeline_state(m_multi_tex_pipeline);
        encoder.set_viewport_rect(viewport.x, viewport.y, viewport.width, viewport.height);
        encoder.set_scissor_rect(viewport.x, viewport.y, viewport.width, viewport.height);
        m_quad_buffer.bind(encoder, buffer_range);

        // Create texture heap with reserved slots matching texture count
        erhe::graphics::Texture_heap texture_heap{m_graphics_device, *m_red_texture.get(), m_nearest_sampler, tex_count, m_test_bind_group_layout.get(), m_multi_tex_default_uniform_block.get()};
        for (uint32_t i = 0; i < tex_count; ++i) {
            texture_heap.assign(i, textures[i].get(), &m_nearest_sampler);
        }
        texture_heap.bind();

        encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 3);

        texture_heap.unbind();
        buffer_range.release();
    }

    void make_separate_samplers_pipeline()
    {
        const std::filesystem::path shader_path{"res/shaders"};
        const bool bindless = m_graphics_device.get_info().uses_bindless_texture();

        // Uniform block with texture handles (for bindless) and count
        m_sep_tex_block = std::make_unique<erhe::graphics::Shader_resource>(
            m_graphics_device, "separate_tex", 0, erhe::graphics::Shader_resource::Type::uniform_block
        );
        m_sep_tex_handle_0 = m_sep_tex_block->add_uvec2("texture_handle_0"); // offset  0, size  8
        m_sep_tex_handle_1 = m_sep_tex_block->add_uvec2("texture_handle_1"); // offset  8, size  8
        m_sep_tex_handle_2 = m_sep_tex_block->add_uvec2("texture_handle_2"); // offset 16, size  8
        m_sep_tex_count    = m_sep_tex_block->add_uint ("texture_count");    // offset 24, size  4
        m_sep_tex_padding0 = m_sep_tex_block->add_uint ("padding0");         // offset 28, size  4
                                                                              // total  32 = 2 x vec4

        // Non-bindless: 3 SEPARATE samplers at different bindings (like post-processing)
        if (!bindless) {
            m_sep_tex_default_uniform_block = std::make_unique<erhe::graphics::Shader_resource>(m_graphics_device);
            m_sep_tex_default_uniform_block->add_sampler("s_tex0", erhe::graphics::Glsl_type::sampler_2d, 0);
            m_sep_tex_default_uniform_block->add_sampler("s_tex1", erhe::graphics::Glsl_type::sampler_2d, 1);
            m_sep_tex_default_uniform_block->add_sampler("s_tex2", erhe::graphics::Glsl_type::sampler_2d, 2);
        }

        erhe::graphics::Shader_stages_create_info create_info{
            .name                  = "separate_samplers_test",
            .interface_blocks      = { m_sep_tex_block.get() },
            .fragment_outputs      = &m_quad_fragment_outputs,
            .default_uniform_block = bindless ? nullptr : m_sep_tex_default_uniform_block.get(),
            .shaders = {
                { erhe::graphics::Shader_type::vertex_shader,   shader_path / "multi_texture_test.vert" },
                { erhe::graphics::Shader_type::fragment_shader, shader_path / "separate_samplers_test.frag" }
            },
            .bind_group_layout = m_test_bind_group_layout.get(),
            .build = true
        };
        m_sep_tex_shader_stages = std::make_unique<erhe::graphics::Shader_stages>(
            m_graphics_device,
            erhe::graphics::Shader_stages_prototype{m_graphics_device, create_info}
        );

        m_sep_tex_pipeline = erhe::graphics::Render_pipeline_state{
            erhe::graphics::Render_pipeline_data{
                .debug_label    = erhe::utility::Debug_label{"Separate Samplers Test Pipeline"},
                .shader_stages  = m_sep_tex_shader_stages.get(),
                .vertex_input   = &m_empty_vertex_input,
                .input_assembly = erhe::graphics::Input_assembly_state::triangle,
                .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
                .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
                .color_blend    = erhe::graphics::Color_blend_state::color_blend_disabled
            }
        };
    }

    void draw_separate_samplers_quad(
        erhe::graphics::Render_command_encoder&                      encoder,
        const erhe::math::Viewport&                                  viewport,
        const std::vector<std::shared_ptr<erhe::graphics::Texture>>& textures
    )
    {
        using erhe::graphics::as_span;
        using erhe::graphics::write;

        const uint32_t tex_count = static_cast<uint32_t>(textures.size());

        erhe::graphics::Ring_buffer_range buffer_range = m_quad_buffer.acquire(
            erhe::graphics::Ring_buffer_usage::CPU_write,
            m_sep_tex_block->get_size_bytes()
        );
        const std::span<std::byte> gpu_data  = buffer_range.get_span();
        const std::span<uint32_t>  uint_data{reinterpret_cast<uint32_t*>(gpu_data.data()), gpu_data.size_bytes() / sizeof(uint32_t)};

        auto write_handle = [&](erhe::graphics::Shader_resource* field, const erhe::graphics::Texture& tex) {
            const uint64_t handle = m_graphics_device.get_handle(tex, m_nearest_sampler);
            const uint32_t h[2] = {
                static_cast<uint32_t>(handle & 0xffffffffu),
                static_cast<uint32_t>(handle >> 32u)
            };
            write(uint_data, field->get_offset_in_parent(), std::span<const uint32_t>{&h[0], 2});
        };

        if (tex_count > 0) write_handle(m_sep_tex_handle_0, *textures[0]);
        if (tex_count > 1) write_handle(m_sep_tex_handle_1, *textures[1]);
        if (tex_count > 2) write_handle(m_sep_tex_handle_2, *textures[2]);
        write(uint_data, m_sep_tex_count->get_offset_in_parent(), std::span<const uint32_t>{&tex_count, 1});

        buffer_range.bytes_written(m_sep_tex_block->get_size_bytes());
        buffer_range.close();

        encoder.set_render_pipeline_state(m_sep_tex_pipeline);
        encoder.set_viewport_rect(viewport.x, viewport.y, viewport.width, viewport.height);
        encoder.set_scissor_rect(viewport.x, viewport.y, viewport.width, viewport.height);
        m_quad_buffer.bind(encoder, buffer_range);

        // Texture heap: 3 reserved slots at individual GLSL bindings 0, 1, 2
        erhe::graphics::Texture_heap texture_heap{m_graphics_device, *m_red_texture.get(), m_nearest_sampler, tex_count, m_test_bind_group_layout.get(), m_sep_tex_default_uniform_block.get()};
        for (uint32_t i = 0; i < tex_count; ++i) {
            texture_heap.assign(i, textures[i].get(), &m_nearest_sampler);
        }
        texture_heap.bind();

        encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 3);

        texture_heap.unbind();
        buffer_range.release();
    }

    // Initialized in constructor init list
    erhe::window::Context_window                                m_window;
    erhe::graphics::Device                                      m_graphics_device;
    bool                                                        m_error_callback_set;
    erhe::gltf::Image_transfer                                  m_image_transfer;
    rendering_test::Mesh_memory                                 m_mesh_memory;
    erhe::scene_renderer::Program_interface_config              m_program_interface_config;
    erhe::scene_renderer::Program_interface                     m_program_interface;
    rendering_test::Programs                                    m_programs;
    erhe::scene_renderer::Forward_renderer                      m_forward_renderer;
    erhe::scene::Scene                                          m_scene;
    std::shared_ptr<erhe::scene::Camera>                        m_camera;
    std::shared_ptr<erhe::scene::Light>                         m_light;
    std::shared_ptr<erhe::scene::Mesh>                          m_test_cube;
    std::vector<std::shared_ptr<erhe::primitive::Material>>     m_materials;
    erhe::scene_renderer::Light_projections                     m_light_projections;

    // 3D scene pipeline
    std::unique_ptr<erhe::graphics::Render_pipeline_state>      m_standard_render_pipeline_state;
    std::vector<erhe::graphics::Render_pipeline_state*>         m_render_pipeline_states;

    // Edge lines pipeline (geometry shader path - OpenGL)
    std::unique_ptr<erhe::graphics::Render_pipeline_state>      m_edge_lines_pipeline_state;
    std::vector<erhe::graphics::Render_pipeline_state*>         m_edge_lines_pipeline_states;

    // Content wide line renderer (compute path - Metal)
    std::unique_ptr<erhe::scene_renderer::Content_wide_line_renderer> m_content_wide_line_renderer;
    std::unique_ptr<erhe::graphics::Shader_stages>              m_compute_shader_stages;
    std::unique_ptr<erhe::graphics::Shader_stages>              m_graphics_shader_stages;
    std::unique_ptr<erhe::graphics::Render_pipeline_state>      m_compute_edge_lines_pipeline_state;

    // Swapchain render pass
    std::unique_ptr<erhe::graphics::Texture>                    m_swapchain_depth_texture;
    std::unique_ptr<erhe::graphics::Render_pass>                m_swapchain_render_pass;

    // Render-to-texture resources
    std::shared_ptr<erhe::graphics::Texture>                    m_color_texture;
    std::unique_ptr<erhe::graphics::Texture>                    m_depth_texture;
    std::unique_ptr<erhe::graphics::Render_pass>                m_texture_render_pass;

    // Textured quad resources
    erhe::graphics::Vertex_input_state                          m_empty_vertex_input{m_graphics_device, erhe::graphics::Vertex_input_state_data{}};
    erhe::graphics::Sampler                                     m_nearest_sampler{
        m_graphics_device,
        erhe::graphics::Sampler_create_info{
            .min_filter  = erhe::graphics::Filter::nearest,
            .mag_filter  = erhe::graphics::Filter::nearest,
            .mipmap_mode = erhe::graphics::Sampler_mipmap_mode::nearest,
            .debug_label = "Orientation nearest"
        }
    };
    erhe::graphics::Ring_buffer_client                           m_quad_buffer{
        m_graphics_device,
        erhe::graphics::Buffer_target::uniform,
        "Quad uniform buffer",
        0 // binding point, matches quad block binding = 0
    };
    std::unique_ptr<erhe::graphics::Bind_group_layout>           m_test_bind_group_layout;
    std::unique_ptr<erhe::graphics::Shader_resource>            m_quad_block;
    erhe::graphics::Shader_resource*                            m_quad_texture_handle{nullptr};
    erhe::graphics::Shader_resource*                            m_quad_uv_min        {nullptr};
    erhe::graphics::Shader_resource*                            m_quad_uv_max        {nullptr};
    erhe::graphics::Shader_resource*                            m_quad_padding       {nullptr};
    std::unique_ptr<erhe::graphics::Shader_resource>            m_quad_default_uniform_block;
    erhe::graphics::Fragment_outputs                            m_quad_fragment_outputs{
        erhe::graphics::Fragment_output{
            .name     = "out_color",
            .type     = erhe::graphics::Glsl_type::float_vec4,
            .location = 0
        }
    };
    std::unique_ptr<erhe::graphics::Shader_stages>              m_quad_shader_stages;
    erhe::graphics::Render_pipeline_state                       m_quad_pipeline;

    // Separate samplers test pipeline (post-processing pattern)
    std::unique_ptr<erhe::graphics::Shader_resource>            m_sep_tex_block;
    erhe::graphics::Shader_resource*                            m_sep_tex_handle_0{nullptr};
    erhe::graphics::Shader_resource*                            m_sep_tex_handle_1{nullptr};
    erhe::graphics::Shader_resource*                            m_sep_tex_handle_2{nullptr};
    erhe::graphics::Shader_resource*                            m_sep_tex_count   {nullptr};
    erhe::graphics::Shader_resource*                            m_sep_tex_padding0{nullptr};
    std::unique_ptr<erhe::graphics::Shader_resource>            m_sep_tex_default_uniform_block;
    std::unique_ptr<erhe::graphics::Shader_stages>              m_sep_tex_shader_stages;
    erhe::graphics::Render_pipeline_state                       m_sep_tex_pipeline;

    // Test textures (solid colors for texture heap testing)
    std::shared_ptr<erhe::graphics::Texture>                    m_red_texture;
    std::shared_ptr<erhe::graphics::Texture>                    m_green_texture;
    std::shared_ptr<erhe::graphics::Texture>                    m_blue_texture;

    // Multi-texture test pipeline
    std::unique_ptr<erhe::graphics::Shader_resource>            m_multi_tex_block;
    erhe::graphics::Shader_resource*                            m_multi_tex_handle_0{nullptr};
    erhe::graphics::Shader_resource*                            m_multi_tex_handle_1{nullptr};
    erhe::graphics::Shader_resource*                            m_multi_tex_handle_2{nullptr};
    erhe::graphics::Shader_resource*                            m_multi_tex_count   {nullptr};
    erhe::graphics::Shader_resource*                            m_multi_tex_padding0{nullptr};
    std::unique_ptr<erhe::graphics::Shader_resource>            m_multi_tex_default_uniform_block;
    std::unique_ptr<erhe::graphics::Shader_stages>              m_multi_tex_shader_stages;
    erhe::graphics::Render_pipeline_state                       m_multi_tex_pipeline;

    bool                                                        m_close_requested{false};
    bool                                                        m_first_frame_rendered{false};
    std::atomic<bool>                                           m_in_tick{false};
};

void run_rendering_test()
{
    erhe::file::ensure_working_directory_contains("rendering_test", "config");

    erhe::log::initialize_log_sinks();

    // Load logging configuration from logging.json
    {
        std::optional<std::string> contents = erhe::file::read("logging config", "logging.json");
        if (contents.has_value()) {
            simdjson::ondemand::parser parser;
            simdjson::padded_string padded{contents.value()};
            simdjson::ondemand::document doc;
            simdjson::error_code error = parser.iterate(padded).get(doc);
            if (!error) {
                simdjson::ondemand::object obj;
                error = doc.get_object().get(obj);
                if (!error) {
                    simdjson::ondemand::array loggers;
                    error = obj["loggers"].get_array().get(loggers);
                    if (!error) {
                        std::vector<std::pair<std::string, std::string>> levels;
                        for (simdjson::ondemand::value logger_val : loggers) {
                            simdjson::ondemand::object logger_obj;
                            if (logger_val.get_object().get(logger_obj) == simdjson::SUCCESS) {
                                std::string_view name;
                                std::string_view level;
                                if (logger_obj["name"].get_string().get(name) == simdjson::SUCCESS &&
                                    logger_obj["level"].get_string().get(level) == simdjson::SUCCESS) {
                                    levels.emplace_back(std::string{name}, std::string{level});
                                }
                            }
                        }
                        erhe::log::configure_log_levels(levels);
                    }
                }
            }
        }
    }

    rendering_test::initialize_logging();

    erhe::dataformat::initialize_logging();
    erhe::file::initialize_logging();
    erhe::graphics::initialize_logging();
    erhe::gltf::initialize_logging();
    erhe::item::initialize_logging();
    erhe::math::initialize_logging();
    erhe::primitive::initialize_logging();
    erhe::raytrace::initialize_logging();
    erhe::renderer::initialize_logging();
    erhe::scene::initialize_logging();
    erhe::scene_renderer::initialize_logging();
    erhe::ui::initialize_logging();
    erhe::window::initialize_logging();

    Rendering_test test;
    test.run();
}

} // namespace rendering_test
