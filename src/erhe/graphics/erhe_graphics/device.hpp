#pragma once

#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/shader_monitor.hpp"

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace erhe::window { class Context_window; }

namespace erhe::graphics {

class Blit_command_encoder;
class Compute_command_encoder;
class Render_pass;
class Render_command_encoder;
class Command_encoder;

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

class Texture;
class Sampler;

enum class Vendor : unsigned int {
    Unknown = 0,
    Nvidia  = 1,
    Amd     = 2,
    Intel   = 3
};

class GPU_ring_buffer;

class Device;

enum class Ring_buffer_usage : unsigned int
{
    None       = 0,
    CPU_write  = 1,
    CPU_read   = 2,
    GPU_access = 3
};

class Buffer_range
{
public:
    Buffer_range();
    Buffer_range(
        GPU_ring_buffer&     ring_buffer,
        Ring_buffer_usage    usage,
        std::span<std::byte> span,
        std::size_t          wrap_count,
        size_t               start_byte_offset_in_buffer
    );
    Buffer_range(Buffer_range&& old) noexcept;
    Buffer_range& operator=(Buffer_range&) = delete;
    Buffer_range& operator=(Buffer_range&& old) noexcept;
    ~Buffer_range();

    void bytes_written                  (std::size_t byte_count);
    void flush                          (std::size_t byte_write_position_in_span);
    void close                          ();
    void release                        ();
    void cancel                         ();
    auto get_span                       () const -> std::span<std::byte>;
    auto get_byte_start_offset_in_buffer() const -> std::size_t;
    auto get_writable_byte_count        () const -> std::size_t;
    auto get_written_byte_count         () const -> std::size_t;
    auto is_closed                      () const -> bool;

    [[nodiscard]] auto get_buffer() const -> GPU_ring_buffer*;

private:
    GPU_ring_buffer*       m_ring_buffer{nullptr};
    std::vector<std::byte> m_cpu_buffer;
    std::span<std::byte>   m_span;
    std::size_t            m_wrap_count{0};
    size_t                 m_byte_span_start_offset_in_buffer{0};
    size_t                 m_byte_write_position_in_span{0};
    size_t                 m_byte_flush_position_in_span{0};
    Ring_buffer_usage      m_usage{Ring_buffer_usage::None};
    bool                   m_is_closed{false};
    bool                   m_is_released{false};
    bool                   m_is_cancelled{false};
};

class GPU_ring_buffer_create_info
{
public:
    std::size_t      size       {0};
    Buffer_direction direction  {Buffer_direction::cpu_to_gpu};
    Buffer_usage     usage      {0};
    const char*      debug_label{nullptr};
};

class GPU_ring_buffer_impl;

class GPU_ring_buffer
{
public:
    GPU_ring_buffer(Device& device, const GPU_ring_buffer_create_info& create_info);
    ~GPU_ring_buffer();

    void get_size_available_for_write(
        std::size_t  required_alignment,
        std::size_t& out_alignment_byte_count_without_wrap,
        std::size_t& out_available_byte_count_without_wrap,
        std::size_t& out_available_byte_count_with_wrap
    ) const;
    [[nodiscard]] auto acquire(std::size_t required_alignment, Ring_buffer_usage usage, std::size_t byte_count) -> Buffer_range;
    [[nodiscard]] auto get_direction() const -> Buffer_direction;

    // For Buffer_range
    void flush(std::size_t byte_offset, std::size_t byte_count);
    void close(std::size_t byte_offset, std::size_t byte_write_count);
    void make_sync_entry(std::size_t wrap_count, std::size_t byte_offset, std::size_t byte_count);

    [[nodiscard]] auto get_buffer() -> Buffer*;

    void frame_completed(uint64_t frame);

private:
    std::unique_ptr<GPU_ring_buffer_impl> m_impl;
};

static constexpr unsigned int format_flag_require_depth     = 0x01u;
static constexpr unsigned int format_flag_require_stencil   = 0x02u;
static constexpr unsigned int format_flag_prefer_accuracy   = 0x04u;
static constexpr unsigned int format_flag_prefer_filterable = 0x08u;

class Device_info
{
public:
    Vendor vendor{Vendor::Unknown};

    int  gl_version             {0};
    int  glsl_version           {0};
    int  shader_model_version   {0};

    bool core_profile           {false};
    bool compatibility_profile  {false};
    bool forward_compatible     {false};

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

    int max_texture_image_units                  {0};  // in fragment shaders
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

class Device_impl;
class Device final
{
public:
    explicit Device(erhe::window::Context_window& context_window);
    Device         (const Device&) = delete;
    void operator= (const Device&) = delete;
    Device         (Device&&)      = delete;
    void operator= (Device&&)      = delete;
    ~Device();

    void start_of_frame        ();
    void end_of_frame          ();
    void memory_barrier        (Memory_barrier_mask barriers);
    void clear_texture         (Texture& texture, std::array<double, 4> clear_value);
    void upload_to_buffer      (Buffer& buffer, size_t offset, const void* data, size_t length);
    void add_completion_handler(std::function<void()> callback);
    void on_thread_enter       ();

    [[nodiscard]] auto get_handle                  (const Texture& texture, const Sampler& sampler) const -> uint64_t;
    [[nodiscard]] auto create_dummy_texture        () -> std::shared_ptr<Texture>;
    [[nodiscard]] auto get_buffer_alignment        (Buffer_target target) -> std::size_t;
    [[nodiscard]] auto get_frame_number            () const -> uint64_t;
    [[nodiscard]] auto allocate_ring_buffer_entry  (Buffer_target buffer_target, Ring_buffer_usage usage, std::size_t byte_count) -> Buffer_range;
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

// Unified API for bindless textures and texture unit cache emulating bindless textures
// using sampler arrays. Also candidate for future metal argument buffer / vulkan
// descriptor indexing based implementations
class Texture_heap_impl;
class Texture_heap final
{
public:
    Texture_heap(
        Device&        device,
        const Texture& fallback_texture,
        const Sampler& fallback_sampler,
        std::size_t    reserved_slot_count
    );
    ~Texture_heap();

    auto assign           (std::size_t slot, const Texture* texture, const Sampler* sample) -> uint64_t;
    void reset            ();
    auto allocate         (const Texture* texture, const Sampler* sample) -> uint64_t;
    auto get_shader_handle(const Texture* texture, const Sampler* sample) -> uint64_t; // bindless ? handle : slot
    auto bind             () -> std::size_t;
    void unbind           ();

private:
    std::unique_ptr<Texture_heap_impl> m_impl;
};

class GPU_ring_buffer_client
{
public:
    GPU_ring_buffer_client(
        Device&                     graphics_device,
        Buffer_target               buffer_target,
        std::string_view            debug_label, 
        std::optional<unsigned int> binding_point = {}
    );

    auto acquire(Ring_buffer_usage usage, std::size_t byte_count) -> Buffer_range;
    auto bind   (Command_encoder& command_encoder, const Buffer_range& range) -> bool;

protected:
    Device&                     m_graphics_device;

private:
    Buffer_target               m_buffer_target;
    std::string                 m_debug_label;
    std::optional<unsigned int> m_binding_point;
};

class Scoped_debug_group final
{
public:
    explicit Scoped_debug_group(const std::string_view debug_label);
    ~Scoped_debug_group() noexcept;

private:
    std::string m_debug_label;
};

} // namespace erhe::graphics
