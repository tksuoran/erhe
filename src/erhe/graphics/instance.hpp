#pragma once

#include "erhe/gl/wrapper_enums.hpp"

#include <unordered_map>

namespace erhe::graphics
{

class Tile_size
{
public:
    int x{0};
    int y{0};
    int z{0};
};

class Instance
{
public:
    // static inline constexpr bool reverse_depth = true;
    // static inline constexpr float depth_clear_value = reverse_depth ? 0.0f : 1.0f;

    class Info
    {
    public:
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

    static Info                   info;
    static Limits                 limits;
    static Implementation_defined implementation_defined;

    static std::unordered_map<gl::Internal_format, Tile_size> sparse_tile_sizes;

    using PFN_generic          = void (*) ();
    using PFN_get_proc_address = PFN_generic (*) (const char*);

    static void initialize();
};

} // namespace erhe::graphics
