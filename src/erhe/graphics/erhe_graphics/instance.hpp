#pragma once

#include "erhe_gl/wrapper_enums.hpp"
#include "erhe_graphics/shader_monitor.hpp"
#include "erhe_graphics/gl_context_provider.hpp"
#include "erhe_graphics/opengl_state_tracker.hpp"

#include <memory>
#include <optional>
#include <unordered_map>

namespace erhe::window {
    class Context_window;
}

namespace erhe::graphics {

class Tile_size
{
public:
    int x{0};
    int y{0};
    int z{0};
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

class Frame_sync
{
public:
    uint64_t        frame_number;
    void*           fence_sync {nullptr};
    gl::Sync_status result     {gl::Sync_status::timeout_expired};
};

struct Ring_buffer_sync_entry
{
    uint64_t    waiting_for_frame{0};
    std::size_t wrap_count {0};
    size_t      byte_offset{0};
    size_t      byte_count {0};
};

class Instance;

enum class Ring_buffer_usage : unsigned int
{
    None       = 0,
    CPU_write  = 1,
    GPU_access = 2
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
    void submit                         ();
    void cancel                         ();
    auto get_span                       () const -> std::span<std::byte>;
    auto get_byte_start_offset_in_buffer() const -> std::size_t;
    auto get_writable_byte_count        () const -> std::size_t;
    auto get_written_byte_count         () const -> std::size_t;
    auto is_closed                      () const -> bool;

    [[nodiscard]] auto get_buffer() const -> GPU_ring_buffer*;

private:
    GPU_ring_buffer*     m_ring_buffer{nullptr};
    std::span<std::byte> m_span;
    std::size_t          m_wrap_count{0};
    size_t               m_byte_span_start_offset_in_buffer{0};
    size_t               m_byte_write_position_in_span{0};
    size_t               m_byte_flush_position_in_span{0};
    Ring_buffer_usage    m_usage{Ring_buffer_usage::None};
    bool                 m_is_closed{false};
    bool                 m_is_submitted{false};
    bool                 m_is_cancelled{false};
};

class GPU_ring_buffer_create_info
{
public:
    //gl::Buffer_target target       {0};
    //unsigned int      binding_point{std::numeric_limits<unsigned int>::max()};
    std::size_t size         {0};
    const char* debug_label  {nullptr};
};

class GPU_ring_buffer
{
public:
    GPU_ring_buffer(Instance& graphics_instance, const GPU_ring_buffer_create_info& create_info);
    ~GPU_ring_buffer();

    void sanity_check();
    void get_size_available_for_write(
        std::size_t  required_alignment,
        std::size_t& out_alignment_byte_count_without_wrap,
        std::size_t& out_available_byte_count_without_wrap,
        std::size_t& out_available_byte_count_with_wrap
    ) const;
    auto allocate_range(std::size_t required_alignment, Ring_buffer_usage usage, std::size_t byte_count) -> Buffer_range;
    //// auto bind(m_binding_point, const Buffer_range& range) -> bool;

    // For Buffer_range
    void flush(std::size_t byte_offset, std::size_t byte_count);
    void close(std::size_t byte_offset, std::size_t byte_write_count);
    void make_sync_entry(std::size_t wrap_count, std::size_t byte_offset, std::size_t byte_count);

    [[nodiscard]] auto get_buffer() -> erhe::graphics::Buffer*;
    [[nodiscard]] auto get_name  () const -> const std::string&;

    // For Instance
    void frame_completed(uint64_t frame);

private:
    void wrap_write();
    //void update_entries();

    Instance&               m_instance;
    std::unique_ptr<Buffer> m_buffer;

    std::size_t             m_map_offset           {0};
    std::size_t             m_write_position       {0};
    std::size_t             m_write_wrap_count     {1};
    std::size_t             m_last_write_wrap_count{1}; // for handling write wraps wraps
    std::size_t             m_read_wrap_count      {0};
    std::size_t             m_read_offset          {0}; // This is the first offset where we cannot write

    std::string             m_name;

    std::vector<Ring_buffer_sync_entry> m_sync_entries;
};

class Instance
{
public:
    explicit Instance(erhe::window::Context_window& context_window);
    Instance      (const Instance&) = delete;
    void operator=(const Instance&) = delete;
    Instance      (Instance&&)      = delete;
    void operator=(Instance&&)      = delete;

    [[nodiscard]] auto get_handle(const Texture& texture, const Sampler& sampler) const -> uint64_t;
    [[nodiscard]] auto create_dummy_texture() -> std::shared_ptr<Texture>;

    // Texture unit cache for bindless emulation
    void texture_unit_cache_reset   (unsigned int base_texture_unit);
    auto texture_unit_cache_allocate(uint64_t handle) -> std::optional<std::size_t>;
    auto texture_unit_cache_bind    (uint64_t fallback_handle) -> std::size_t;

    auto get_buffer_alignment(gl::Buffer_target target) -> std::size_t;

    [[nodiscard]] auto get_frame_number() const -> uint64_t;
    [[nodiscard]] auto allocate_ring_buffer_entry(gl::Buffer_target buffer_target, Ring_buffer_usage usage, std::size_t byte_count) -> Buffer_range;
    void end_of_frame();

    class Info
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
    };

    class Limits
    {
    public:
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
    };

    class Implementation_defined
    {
    public:
        unsigned int shader_storage_buffer_offset_alignment{256};
        unsigned int uniform_buffer_offset_alignment       {256};
    };

    class Configuration
    {
    public:
        bool reverse_depth  {true};  // TODO move to editor
        bool post_processing{true};  // TODO move to editor
        bool use_time_query {true};
    };

    // Public API
    [[nodiscard]] auto depth_clear_value_pointer() const -> const float *; // reverse_depth ? 0.0f : 1.0f;
    [[nodiscard]] auto depth_function           (gl::Depth_function depth_function) const -> gl::Depth_function;

    Shader_monitor         shader_monitor;
    OpenGL_state_tracker   opengl_state_tracker;
    Gl_context_provider    context_provider;
    Info                   info;
    Limits                 limits;
    Implementation_defined implementation_defined;
    Configuration          configuration;

    std::unordered_map<gl::Internal_format, Tile_size> sparse_tile_sizes;

    using PFN_generic          = void (*) ();
    using PFN_get_proc_address = PFN_generic (*) (const char*);

private:
    void frame_completed(uint64_t frame);
    erhe::window::Context_window& m_context_window;

    // Texture unit cache for bindless emulation
    unsigned int          m_base_texture_unit;
    std::vector<uint64_t> m_texture_units;

    std::vector<std::unique_ptr<GPU_ring_buffer>> m_ring_buffers;
    std::size_t                                   m_min_buffer_size = 2 * 1024 * 1024; // TODO

    std::array<Frame_sync, 8> m_frame_syncs;
    uint64_t                  m_frame_number{0};

    std::vector<uint64_t>     m_pending_frames;
    std::vector<uint64_t>     m_completed_frames;
};

class GPU_ring_buffer_client
{
public:
    GPU_ring_buffer_client(Instance& graphics_instance, std::string_view debug_label, gl::Buffer_target buffer_target, std::optional<unsigned int> binding_point = {});

    auto allocate_range(Ring_buffer_usage usage, std::size_t byte_count) -> Buffer_range;
    auto bind(const Buffer_range& range) -> bool;

private:
    Instance&                   m_graphics_instance;
    gl::Buffer_target           m_buffer_target;
    std::string                 m_debug_label;
    std::optional<unsigned int> m_binding_point;
};

} // namespace erhe::graphics
