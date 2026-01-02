#pragma once

#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"
#include "erhe_graphics/shader_monitor.hpp"
#include "erhe_graphics/surface.hpp"

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace erhe::window { class Context_window; }

namespace erhe::graphics {

class Format_properties
{
public:
    bool                 supported{false};

    // These are all texture_2d
    bool                 color_renderable{false};
    bool                 depth_renderable{false};
    bool                 stencil_renderable{false};
    bool                 filter{false};
    bool                 framebuffer_blend{false};

    int                  red_size        {0};
    int                  green_size      {0};
    int                  blue_size       {0};
    int                  alpha_size      {0};
    int                  depth_size      {0};
    int                  stencil_size    {0};
    int                  image_texel_size{0};

    std::vector<int>     texture_2d_sample_counts;
    int                  texture_2d_array_max_width{0};
    int                  texture_2d_array_max_height{0};
    int                  texture_2d_array_max_layers{0};

    // These are all texture_2d
    std::vector<int64_t> sparse_tile_x_sizes;
    std::vector<int64_t> sparse_tile_y_sizes;
    std::vector<int64_t> sparse_tile_z_sizes;
};

class Blit_command_encoder;
class Command_encoder;
class Compute_command_encoder;
class Device;
class Render_command_encoder;
class Render_pass;
class Ring_buffer;
class Sampler;
class Surface;
class Swapchain;
class Texture;

static constexpr unsigned int format_flag_require_depth     = 0x01u;
static constexpr unsigned int format_flag_require_stencil   = 0x02u;
static constexpr unsigned int format_flag_prefer_accuracy   = 0x04u;
static constexpr unsigned int format_flag_prefer_filterable = 0x08u;

class Device_info
{
public:
    Vendor vendor{Vendor::Unknown};

    int  glsl_version           {0};

#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
    int  gl_version             {0};
    bool core_profile           {false};
    bool compatibility_profile  {false};
    bool forward_compatible     {false};
#endif
#if defined(ERHE_GRAPHICS_LIBRARY_VULKAN)
    uint32_t vulkan_api_version {0};
#endif

    bool use_binary_shaders     {false};
    bool use_integer_polygon_ids{false};
    bool use_bindless_texture   {false};
    bool use_sparse_texture     {false};
    bool use_persistent_buffers {false};
    bool use_multi_draw_indirect{false};
    bool use_compute_shader     {false};
    bool use_clear_texture      {false};

    // limits
    int max_compute_workgroup_count[3] = { 1, 1, 1 };
    int max_compute_workgroup_size [3] = { 1, 1, 1 };
    int max_compute_work_group_invocations       {1};
    int max_compute_shared_memory_size           {1};
    int max_samples                              {0};
    int max_color_texture_samples                {0};
    int max_depth_texture_samples                {0};
    int max_framebuffer_samples                  {0};
    int max_integer_samples                      {0};
    int max_vertex_attribs                       {0};

    int max_texture_size                        {64};
    int max_3d_texture_size                      {0};
    int max_cube_map_texture_size                {0};
    int max_texture_buffer_size                  {0};
    int max_array_texture_layers                 {0};
    int max_sparse_texture_size                  {0};

    uint32_t max_per_stage_descriptor_samplers   {0};
    int max_combined_texture_image_units         {0};  // combined across all shader stages
    int max_uniform_block_size                   {0};
    int max_shader_storage_buffer_bindings       {0};
    int max_uniform_buffer_bindings              {0};
    int max_compute_shader_storage_blocks        {0};
    int max_compute_uniform_blocks               {0};
    int max_vertex_shader_storage_blocks         {0};
    int max_vertex_uniform_blocks                {0};
    int max_vertex_uniform_vectors               {0};
    int max_fragment_shader_storage_blocks       {0};
    int max_fragment_uniform_blocks              {0};
    int max_fragment_uniform_vectors             {0};
    int max_geometry_shader_storage_blocks       {0};
    int max_geometry_uniform_blocks              {0};
    int max_tess_control_shader_storage_blocks   {0};
    int max_tess_control_uniform_blocks          {0};
    int max_tess_evaluation_shader_storage_blocks{0};
    int max_tess_evaluation_uniform_blocks       {0};
    float max_texture_max_anisotropy{1.0f};

    std::vector<int> msaa_sample_counts;
    int max_depth_layers    {4};
    int max_depth_resolution{64};

    // implementation defined
    unsigned int shader_storage_buffer_offset_alignment{256};
    unsigned int uniform_buffer_offset_alignment       {256};
};

class Frame_state;
class Frame_begin_info;
class Frame_end_info;

class Device_impl;
class Device final
{
public:
    explicit Device(const Surface_create_info& surface_create_info);
    Device         (const Device&) = delete;
    void operator= (const Device&) = delete;
    Device         (Device&&)      = delete;
    void operator= (Device&&)      = delete;
    ~Device() noexcept;

    [[nodiscard]] auto wait_frame (Frame_state& out_frame_state) -> bool;
    [[nodiscard]] auto begin_frame(const Frame_begin_info& frame_begin_info) -> bool;
    [[nodiscard]] auto end_frame  (const Frame_end_info& frame_end_info) -> bool;

    void memory_barrier        (Memory_barrier_mask barriers);
    void clear_texture         (Texture& texture, std::array<double, 4> clear_value);
    void upload_to_buffer      (Buffer& buffer, size_t offset, const void* data, size_t length);
    void add_completion_handler(std::function<void()> callback);
    void on_thread_enter       ();

    [[nodiscard]] auto get_surface                 () -> Surface*;
    [[nodiscard]] auto get_handle                  (const Texture& texture, const Sampler& sampler) const -> uint64_t;
    [[nodiscard]] auto create_dummy_texture        () -> std::shared_ptr<Texture>;
    [[nodiscard]] auto get_buffer_alignment        (Buffer_target target) -> std::size_t;
    [[nodiscard]] auto get_frame_index             () const -> uint64_t;
    [[nodiscard]] auto allocate_ring_buffer_entry  (Buffer_target buffer_target, Ring_buffer_usage usage, std::size_t byte_count) -> Ring_buffer_range;
    [[nodiscard]] auto make_blit_command_encoder   () -> Blit_command_encoder;
    [[nodiscard]] auto make_compute_command_encoder() -> Compute_command_encoder;
    [[nodiscard]] auto make_render_command_encoder (Render_pass& render_pass) -> Render_command_encoder;
    [[nodiscard]] auto get_format_properties       (erhe::dataformat::Format format) const -> Format_properties;
    [[nodiscard]] auto choose_depth_stencil_format (unsigned int flags, int sample_count) const -> erhe::dataformat::Format;
    [[nodiscard]] auto get_shader_monitor          () -> Shader_monitor&;
    [[nodiscard]] auto get_info                    () const -> const Device_info&;
    [[nodiscard]] auto get_impl                    () -> Device_impl&;
    [[nodiscard]] auto get_impl                    () const -> const Device_impl&;

private:
    std::unique_ptr<Device_impl> m_impl;
};

[[nodiscard]] auto get_depth_clear_value_pointer(bool reverse_depth = true) -> const float *; // reverse_depth ? 0.0f : 1.0f;
[[nodiscard]] auto get_depth_function(Compare_operation depth_function, bool reverse_depth = true) -> Compare_operation;

class Scoped_debug_group final
{
public:
    explicit Scoped_debug_group(const std::string_view debug_label);
    ~Scoped_debug_group() noexcept;

private:
    std::string m_debug_label;
};

} // namespace erhe::graphics
