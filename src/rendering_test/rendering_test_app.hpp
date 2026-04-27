#pragma once

#include "programs.hpp"

#include "generated/rendering_test/rendering_test_settings.hpp"

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_gltf/image_transfer.hpp"
#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/compute_pipeline_state.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/fragment_outputs.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_math/viewport.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene_renderer/forward_renderer.hpp"
#include "erhe_scene_renderer/light_buffer.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_scene_renderer/program_interface.hpp"
#include "erhe_scene_renderer/content_wide_line_renderer.hpp"
#include "erhe_window/window.hpp"
#include "erhe_window/window_event_handler.hpp"

#include <atomic>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace erhe::primitive {
    class Material;
}
namespace erhe::scene {
    class Camera;
    class Light;
    class Mesh;
}

namespace rendering_test {

// Single rendering-test application. The class is intentionally split across
// several .cpp files in this directory: rendering_test.cpp drives the run loop
// and tick() dispatch, rendering_test_setup.cpp handles scene/test resource
// creation, and each cell_*.cpp file owns one or two test cells (their
// pipeline construction and per-frame draw helper).
//
// All members are public so the per-cell .cpp files can reach them without
// having to declare friends or accessor methods. The class is internal -- only
// rendering_test.cpp's run_rendering_test() instantiates it.
class Rendering_test : public erhe::window::Input_event_handler
{
public:
    explicit Rendering_test(std::string_view config_path);

    // Run loop / event handling (rendering_test.cpp)
    void run();
    auto on_window_resize_event(const erhe::window::Input_event& input_event) -> bool override;
    auto on_window_close_event (const erhe::window::Input_event&)             -> bool override;
    auto on_key_event          (const erhe::window::Input_event& input_event) -> bool override;

    // Per-frame orchestration (rendering_test.cpp)
    void tick(erhe::graphics::Command_buffer& command_buffer);

    // Setup helpers (rendering_test_setup.cpp)
    void print_conventions();
    void create_test_scene    (erhe::graphics::Command_buffer& init_command_buffer);
    void create_test_textures (erhe::graphics::Command_buffer& init_command_buffer);
    void update_swapchain_render_pass(int width, int height);
    void update_msaa_depth_render_pass(int width, int height);
    void update_texture_render_pass(int width, int height);
    [[nodiscard]] auto get_grid_tile_viewport(int col, int row) const -> erhe::math::Viewport;
    [[nodiscard]] auto is_fullscreen_mode () const -> bool;
    [[nodiscard]] auto is_replicate_mode  () const -> bool;
    [[nodiscard]] auto get_subtest_at     (int col, int row) const -> std::string_view;
    [[nodiscard]] auto has_subtest        (std::string_view name) const -> bool;
    void               dispatch_subtest(
        std::string_view                                        name,
        erhe::graphics::Command_buffer&                         command_buffer,
        erhe::graphics::Render_command_encoder&                 encoder,
        const erhe::math::Viewport&                             viewport,
        const std::vector<std::shared_ptr<erhe::scene::Light>>& lights,
        const std::vector<std::shared_ptr<erhe::scene::Mesh>>&  meshes
    );

    // 3D scene cell (cell_scene.cpp)
    void make_render_pipeline_states();
    void make_content_wide_line_renderer();
    void render_scene(
        erhe::graphics::Render_command_encoder&                 render_encoder,
        const erhe::math::Viewport&                             viewport,
        const std::vector<std::shared_ptr<erhe::scene::Light>>& lights,
        const std::vector<std::shared_ptr<erhe::scene::Mesh>>&  meshes,
        erhe::graphics::Render_pass*                            active_render_pass
    );

    // Textured-quad cell (cell_textured_quad.cpp)
    void make_quad_pipeline();
    void draw_textured_quad_cell(
        erhe::graphics::Render_command_encoder& encoder,
        const erhe::math::Viewport&             viewport
    );

    // Multi-texture cell (cell_multi_texture.cpp)
    void make_multi_texture_pipeline();
    void draw_multi_texture_quad(
        erhe::graphics::Render_command_encoder&                      encoder,
        const erhe::math::Viewport&                                  viewport,
        const std::vector<std::shared_ptr<erhe::graphics::Texture>>& textures,
        const erhe::graphics::Render_pass&                           render_pass
    );

    // Separate-samplers cell (cell_separate_samplers.cpp)
    void make_separate_samplers_pipeline();
    void draw_separate_samplers_quad(
        erhe::graphics::Render_command_encoder&                      encoder,
        const erhe::math::Viewport&                                  viewport,
        const std::vector<std::shared_ptr<erhe::graphics::Texture>>& textures,
        const erhe::graphics::Render_pass&                           render_pass
    );

    // Depth visualize cell (cell_depth_visualize.cpp)
    void make_depth_visualize_pipeline();
    void draw_depth_visualize_cell(
        erhe::graphics::Render_command_encoder& encoder,
        const erhe::math::Viewport&             viewport
    );

    // Stencil cell (cell_stencil.cpp)
    void make_stencil_pipelines();
    void render_stencil_cell(
        erhe::graphics::Render_command_encoder&                 render_encoder,
        const erhe::math::Viewport&                             viewport,
        const std::vector<std::shared_ptr<erhe::scene::Light>>& lights,
        erhe::graphics::Render_pass*                            active_render_pass
    );

    // Minimal compute-triangle repro (cell_minimal_compute_triangle.cpp).
    // Smallest possible standalone VkPipeline-switch reproducer: a
    // compute shader emits 3 vertex positions into an SSBO, then the
    // graphics path reads the SSBO as a vertex buffer via a dedicated
    // Bind_group_layout. Two graphics pipelines A (stencil_test=false,
    // red) and B (stencil_test=true, not_equal 1, green) differ only
    // in depth_stencil state.
    void make_minimal_compute_triangle();
    void dispatch_minimal_compute_triangle(erhe::graphics::Compute_command_encoder& compute_encoder);
    void draw_minimal_compute_triangle(
        erhe::graphics::Render_command_encoder& encoder,
        const erhe::math::Viewport&             viewport,
        erhe::graphics::Lazy_render_pipeline&   pipeline_state
    );
    void end_frame_minimal_compute_triangle();

    // Stencil wide-line cell (cell_stencil_wide_line.cpp)
    // Replicates the editor's outline pattern: a mesh draw writes stencil,
    // then compute-generated wide-line triangles are drawn with a stencil
    // test that should mask them against the writer's footprint.
    void make_stencil_wide_line_pipeline();
    void render_stencil_wide_line_cell(
        erhe::graphics::Render_command_encoder&                 render_encoder,
        const erhe::math::Viewport&                             viewport,
        const std::vector<std::shared_ptr<erhe::scene::Light>>& lights,
        erhe::graphics::Render_pass*                            active_render_pass
    );

    // Initialized in constructor init list
    Rendering_test_settings                                     m_settings;
    Graphics_config                                             m_graphics_config;
    erhe::window::Context_window                                m_window;
    erhe::graphics::Device                                      m_graphics_device;
    bool                                                        m_error_callback_set;
    erhe::graphics::Command_buffer&                             m_init_command_buffer;
    erhe::gltf::Image_transfer                                  m_image_transfer;
    erhe::dataformat::Vertex_format                             m_vertex_format;
    erhe::scene_renderer::Mesh_memory                           m_mesh_memory;
    erhe::scene_renderer::Program_interface_config              m_program_interface_config;
    erhe::scene_renderer::Program_interface                     m_program_interface;
    rendering_test::Programs                                    m_programs;
    std::unique_ptr<erhe::scene_renderer::Forward_renderer>     m_forward_renderer;
    erhe::scene::Scene                                          m_scene;
    std::shared_ptr<erhe::scene::Camera>                        m_camera;
    std::shared_ptr<erhe::scene::Light>                         m_light;
    std::shared_ptr<erhe::scene::Mesh>                          m_test_cube;
    std::shared_ptr<erhe::scene::Mesh>                          m_stencil_cube;
    std::shared_ptr<erhe::scene::Mesh>                          m_stencil_sphere;
    std::vector<std::shared_ptr<erhe::primitive::Material>>     m_materials;
    erhe::scene_renderer::Light_projections                     m_light_projections;

    // 3D scene pipeline
    std::unique_ptr<erhe::graphics::Lazy_render_pipeline>       m_standard_render_pipeline_state;
    std::vector<erhe::graphics::Lazy_render_pipeline*>          m_render_pipeline_states;

    // Stencil diagnostic pipelines (legacy 0x80 patterns kept around for
    // future re-use; not currently bound to any cell).
    std::unique_ptr<erhe::graphics::Lazy_render_pipeline>       m_stencil_disabled_pipeline;
    std::vector<erhe::graphics::Lazy_render_pipeline*>          m_stencil_disabled_pipeline_states;
    std::unique_ptr<erhe::graphics::Lazy_render_pipeline>       m_stencil_write_0x80_pipeline;
    std::vector<erhe::graphics::Lazy_render_pipeline*>          m_stencil_write_0x80_pipeline_states;
    std::unique_ptr<erhe::graphics::Lazy_render_pipeline>       m_stencil_test_ne_0x80_pipeline;
    std::vector<erhe::graphics::Lazy_render_pipeline*>          m_stencil_test_ne_0x80_pipeline_states;

    // Cell (1,3) stencil test pipelines: red cube writes stencil = 1, green
    // sphere only renders where stencil != 1. The shader stages are dedicated
    // "stencil_color" variants compiled with different STENCIL_COLOR defines
    // because the rendering_test "standard" shader visualizes encoded normals
    // and would not produce flat red/green output.
    std::unique_ptr<erhe::graphics::Shader_stages>              m_stencil_red_shader_stages;
    std::unique_ptr<erhe::graphics::Shader_stages>              m_stencil_green_shader_stages;
    // Cell (1,4) reuses the cell (1,3) stencil pipelines but with a
    // cyan cube color so it is visually distinct from cell (1,3)'s
    // red/green pair (the two cells sit directly on top of each other
    // in the grid layout).
    std::unique_ptr<erhe::graphics::Shader_stages>              m_stencil_cyan_shader_stages;
    std::unique_ptr<erhe::graphics::Lazy_render_pipeline>       m_stencil_write_1_pipeline;
    std::vector<erhe::graphics::Lazy_render_pipeline*>          m_stencil_write_1_pipeline_states;
    std::unique_ptr<erhe::graphics::Lazy_render_pipeline>       m_stencil_test_ne_1_pipeline;
    std::vector<erhe::graphics::Lazy_render_pipeline*>          m_stencil_test_ne_1_pipeline_states;

    // Minimal VkPipeline-switch repro pipelines. Identical except for
    // depth_stencil.stencil_test_enable (off vs on with function=not_equal,
    // reference=0). Both draw the stencil cube via forward_renderer.
    // See the `cube_no_stencil` / `cube_stencil_test_ne_0` subtests.
    std::unique_ptr<erhe::graphics::Lazy_render_pipeline>       m_no_stencil_red_pipeline;
    std::vector<erhe::graphics::Lazy_render_pipeline*>          m_no_stencil_red_pipeline_states;
    std::unique_ptr<erhe::graphics::Lazy_render_pipeline>       m_stencil_test_ne_0_green_pipeline;
    std::vector<erhe::graphics::Lazy_render_pipeline*>          m_stencil_test_ne_0_green_pipeline_states;
    // Pipeline C: same stencil setup as Pipeline B, but with
    // depth_test=false, depth_write=false, compare=always. This matches
    // the depth_stencil state of m_compute_edge_lines_stencil_pipeline_state,
    // so pairing (A, C) via forward_renderer exercises exactly the same
    // depth_stencil delta as the wide-line regression.
    std::unique_ptr<erhe::graphics::Lazy_render_pipeline>       m_stencil_test_ne_0_no_depth_green_pipeline;
    std::vector<erhe::graphics::Lazy_render_pipeline*>          m_stencil_test_ne_0_no_depth_green_pipeline_states;

    // Minimal compute-triangle repro resources (cell_minimal_compute_triangle.cpp).
    bool                                                        m_minimal_compute_enabled{false};
    std::unique_ptr<erhe::graphics::Shader_resource>            m_minimal_triangle_vertex_struct;
    std::unique_ptr<erhe::graphics::Shader_resource>            m_minimal_triangle_ssbo_block;
    std::unique_ptr<erhe::graphics::Bind_group_layout>          m_minimal_compute_bind_group_layout;
    std::unique_ptr<erhe::graphics::Bind_group_layout>          m_minimal_graphics_bind_group_layout;
    erhe::dataformat::Vertex_format                             m_minimal_vertex_format;
    std::unique_ptr<erhe::graphics::Vertex_input_state>         m_minimal_vertex_input;
    erhe::graphics::Fragment_outputs                            m_minimal_fragment_outputs{
        erhe::graphics::Fragment_output{
            .name     = "out_color",
            .type     = erhe::graphics::Glsl_type::float_vec4,
            .location = 0
        }
    };
    std::unique_ptr<erhe::graphics::Shader_stages>              m_minimal_compute_shader_stages;
    std::unique_ptr<erhe::graphics::Shader_stages>              m_minimal_graphics_shader_stages_red;
    std::unique_ptr<erhe::graphics::Shader_stages>              m_minimal_graphics_shader_stages_green;
    std::unique_ptr<erhe::graphics::Shader_stages>              m_minimal_graphics_shader_stages_blue;
    std::unique_ptr<erhe::graphics::Shader_stages>              m_minimal_graphics_shader_stages_yellow;
    std::optional<erhe::graphics::Compute_pipeline>             m_minimal_compute_pipeline;
    std::unique_ptr<erhe::graphics::Lazy_render_pipeline>       m_minimal_pipeline_A_red;
    std::unique_ptr<erhe::graphics::Lazy_render_pipeline>       m_minimal_pipeline_B_green;
    // Pipeline C: stencil_test=true, function=always, keep ops, blue.
    // Pipeline D: stencil_test=true, function=equal 1, keep ops, yellow.
    // Both have stencil_test_enable=true; they differ only in stencil
    // function. C always draws; D should NEVER draw when stencil has
    // not been written (swapchain clears stencil to 0, 0==1 false).
    // Pairing (C, D) in a 2-cell grid is the targeted repro for a
    // KosmicKrisp state-transition failure that keeps stencil_test_enable
    // but drops the stencil function/reference update on pipeline switch.
    std::unique_ptr<erhe::graphics::Lazy_render_pipeline>       m_minimal_pipeline_C_stencil_always_blue;
    std::unique_ptr<erhe::graphics::Lazy_render_pipeline>       m_minimal_pipeline_D_stencil_eq_1_yellow;
    erhe::graphics::Ring_buffer_client                          m_minimal_triangle_buffer_client{
        m_graphics_device,
        erhe::graphics::Buffer_target::storage,
        "Minimal triangle vertex buffer",
        0 // binding point = 0 matches the SSBO block
    };
    erhe::graphics::Ring_buffer_range                           m_minimal_triangle_range{};
    bool                                                        m_minimal_triangle_range_valid{false};

    // Content wide line renderer (compute path - Metal)
    std::unique_ptr<erhe::scene_renderer::Content_wide_line_renderer> m_content_wide_line_renderer;
    std::unique_ptr<erhe::graphics::Shader_stages>              m_compute_shader_stages;
    std::unique_ptr<erhe::graphics::Shader_stages>              m_graphics_shader_stages;
    std::unique_ptr<erhe::graphics::Lazy_render_pipeline>       m_compute_edge_lines_pipeline_state;
    // Wide-line pipeline variant with stencil test != 0x01 (used by the
    // cell that replicates the editor outline pattern).
    std::unique_ptr<erhe::graphics::Lazy_render_pipeline>       m_compute_edge_lines_stencil_pipeline_state;

    // Swapchain render pass
    std::unique_ptr<erhe::graphics::Texture>                    m_swapchain_depth_texture;
    std::unique_ptr<erhe::graphics::Render_pass>                m_swapchain_render_pass;

    // Render-to-texture resources
    std::shared_ptr<erhe::graphics::Texture>                    m_color_texture;
    std::unique_ptr<erhe::graphics::Texture>                    m_depth_texture;
    std::unique_ptr<erhe::graphics::Render_pass>                m_texture_render_pass;

    // MSAA depth resolve cell resources
    std::shared_ptr<erhe::graphics::Texture>                    m_msaa_color_texture;
    std::unique_ptr<erhe::graphics::Texture>                    m_msaa_depth_texture;
    std::shared_ptr<erhe::graphics::Texture>                    m_msaa_depth_resolve_texture;
    std::unique_ptr<erhe::graphics::Render_pass>                m_msaa_depth_render_pass;
    std::unique_ptr<erhe::graphics::Shader_stages>              m_depth_visualize_shader_stages;
    erhe::graphics::Lazy_render_pipeline                        m_depth_visualize_pipeline;

    // Dedicated bind group layout for the depth visualize cell.
    std::unique_ptr<erhe::graphics::Bind_group_layout>          m_depth_visualize_bind_group_layout;

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
    erhe::graphics::Ring_buffer_client                          m_quad_buffer{
        m_graphics_device,
        erhe::graphics::Buffer_target::uniform,
        "Quad uniform buffer",
        0 // binding point, matches quad block binding = 0
    };
    std::unique_ptr<erhe::graphics::Bind_group_layout>          m_test_bind_group_layout;
    std::unique_ptr<erhe::graphics::Shader_resource>            m_quad_block;
    erhe::graphics::Shader_resource*                            m_quad_texture_handle{nullptr};
    erhe::graphics::Shader_resource*                            m_quad_uv_min        {nullptr};
    erhe::graphics::Shader_resource*                            m_quad_uv_max        {nullptr};
    erhe::graphics::Shader_resource*                            m_quad_padding       {nullptr};
    erhe::graphics::Fragment_outputs                            m_quad_fragment_outputs{
        erhe::graphics::Fragment_output{
            .name     = "out_color",
            .type     = erhe::graphics::Glsl_type::float_vec4,
            .location = 0
        }
    };
    std::unique_ptr<erhe::graphics::Shader_stages>              m_quad_shader_stages;
    erhe::graphics::Lazy_render_pipeline                        m_quad_pipeline;

    // Separate samplers test pipeline (post-processing pattern)
    std::unique_ptr<erhe::graphics::Bind_group_layout>          m_sep_tex_bind_group_layout;
    std::unique_ptr<erhe::graphics::Shader_resource>            m_sep_tex_block;
    erhe::graphics::Shader_resource*                            m_sep_tex_handle_0{nullptr};
    erhe::graphics::Shader_resource*                            m_sep_tex_handle_1{nullptr};
    erhe::graphics::Shader_resource*                            m_sep_tex_handle_2{nullptr};
    erhe::graphics::Shader_resource*                            m_sep_tex_count   {nullptr};
    erhe::graphics::Shader_resource*                            m_sep_tex_padding0{nullptr};
    std::unique_ptr<erhe::graphics::Shader_stages>              m_sep_tex_shader_stages;
    erhe::graphics::Lazy_render_pipeline                        m_sep_tex_pipeline;

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
    std::unique_ptr<erhe::graphics::Shader_stages>              m_multi_tex_shader_stages;
    erhe::graphics::Lazy_render_pipeline                        m_multi_tex_pipeline;

    // Window resize / run-loop state
    int                                      m_last_window_width     {0};
    int                                      m_last_window_height    {0};
    std::atomic<bool>                        m_request_resize_pending{false};
    std::optional<erhe::window::Input_event> m_window_resize_event{};

    bool                                     m_close_requested      {false};
    bool                                     m_first_frame_rendered {false};
    std::atomic<bool>                        m_in_tick              {false};
};

} // namespace rendering_test
