#ifndef configuration_hpp_erhe_graphics
#define configuration_hpp_erhe_graphics


namespace erhe::graphics
{

class Configuration
{
public:
    // TODO Reverse depth

    static inline constexpr const bool reverse_depth = true;

    static inline constexpr const float depth_clear_value = reverse_depth ? 0.0f : 1.0f;

    struct info_t
    {
        int  gl_version             {0};
        int  glsl_version           {0};
        int  shader_model_version   {0};

        bool core_profile           {false};
        bool compatibility_profile  {false};
        bool forward_compatible     {false};

        bool use_binary_shaders     {false};
        bool use_integer_polygon_ids{false};
    };

    struct limits_t
    {
        int max_vertex_attribs                {0};
        int max_texture_size                 {64};
        int max_3d_texture_size               {0};
        int max_cube_map_texture_size         {0};
        int max_texture_buffer_size           {0};
        int max_texture_image_units           {0};  // in fragment shaders
        int max_combined_texture_image_units  {0};  // combined across all shader stages
        int max_uniform_block_size            {0};
        int max_uniform_buffer_bindings       {0};
        int max_vertex_uniform_blocks         {0};
        int max_fragment_uniform_blocks       {0};
        int max_geometry_uniform_blocks       {0};
        int max_tess_control_uniform_blocks   {0};
        int max_tess_evaluation_uniform_blocks{0};
    };

    struct implementation_defined_t
    {
        unsigned int uniform_buffer_offset_alignment{1024};
    };

    static info_t                   info;
    static limits_t                 limits;
    static implementation_defined_t implementation_defined;

    using PFN_generic          = void (*) ();
    using PFN_get_proc_address = PFN_generic (*) (const char*);

    static void initialize(PFN_get_proc_address get_proc_address);

    static void (* s_glNamedFramebufferTextureEXT) (unsigned int framebuffer, unsigned int attachment, unsigned int texture, int level);
    static void (* s_glNamedFramebufferTexture) (unsigned int framebuffer, unsigned int attachment, unsigned int texture, int level);

};

} // namespace erhe::graphics

#endif
