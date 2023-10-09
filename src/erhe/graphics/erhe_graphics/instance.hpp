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

namespace erhe::graphics
{

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

class Instance
{
public:
    explicit Instance(erhe::window::Context_window& context_window);
    ~Instance();
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
        int max_fragment_shader_storage_blocks       {0};
        int max_fragment_uniform_blocks              {0};
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
        //bool force_no_bindless          {false}; // TODO move to erhe::graphics
        //bool force_no_persistent_buffers{false}; // TODO move to erhe::graphics
        //bool initial_clear              {true};
        //int  msaa_sample_count          {4};     // TODO move to editor
    };

    // Public API
    [[nodiscard]] auto depth_clear_value_pointer() const -> const float *; // reverse_depth ? 0.0f : 1.0f;
    [[nodiscard]] auto depth_function           (const gl::Depth_function depth_function) const -> gl::Depth_function;

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
    erhe::window::Context_window& m_context_window;

    // Texture unit cache for bindless emulation
    unsigned int          m_base_texture_unit;
    std::vector<uint64_t> m_texture_units;
};

} // namespace erhe::graphics
