#pragma once

#include "erhe/components/components.hpp"
#include "erhe/gl/wrapper_enums.hpp"

namespace erhe::application {

class Configuration
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_label{"Configuration"};
    static constexpr uint32_t hash{
        compiletime_xxhash::xxh32(
            c_label.data(),
            c_label.size(),
            {}
        )
    };

    Configuration(int argc, char** argv);

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }

    // Public API
    [[nodiscard]] auto depth_clear_value_pointer() const -> const float *; // reverse_depth ? 0.0f : 1.0f;
    [[nodiscard]] auto depth_function           (const gl::Depth_function depth_function) const -> gl::Depth_function;

    struct Imgui
    {
        bool enabled{true};
    };
    Imgui imgui;

    struct Headset
    {
        bool openxr{false};
    };
    Headset headset;

    struct Threading
    {
        bool parallel_initialization{true};
    };
    Threading threading;

    struct Graphics
    {
        bool low_hdr          {false};
        bool reverse_depth    {true};
        bool simpler_shaders  {true};
        bool post_processing  {true};
        bool use_time_query   {true};
        bool force_no_bindless{false};
        int  msaa_sample_count{4};
    };
    Graphics graphics;

    struct Mesh_memory
    {
        int vertex_buffer_size{32}; // in megabytes
        int index_buffer_size  {8}; // in megabytes
    };
    Mesh_memory mesh_memory;

    struct Window
    {
        bool show             {true};
        bool fullscreen       {false};
        int  width            {1920};
        int  height           {1080};
    };
    Window window;

    struct Shadow_renderer
    {
        bool enabled{true};
        int  shadow_map_resolution     {2048};
        int  shadow_map_max_light_count{8};
    };
    Shadow_renderer shadow_renderer;

    struct Text_renderer
    {
        bool enabled{true};
    };
    Text_renderer text_renderer;

    struct Renderer
    {
        int max_material_count  {256};
        int max_light_count     {256};
        int max_camera_count    {256};
        int max_primitive_count {8000}; // GLTF primitives
        int max_draw_count      {8000};
    };
    Renderer renderer;

    struct Physics
    {
        bool enabled{false};
    };
    Physics physics;

    struct Scene
    {
        int   directional_light_count{4};
        int   spot_light_count       {3};
        float floor_size             {40.0f};
        int   floor_div              {4};
        int   instance_count         {1};
        float instance_gap           {0.4f};
        int   detail                 {2};
        bool  floor                  {true};
        bool  gltf_files             {false};
        bool  obj_files              {false};
        bool  sphere                 {false};
        bool  torus                  {false};
        bool  cylinder               {false};
        bool  cone                   {false};
        bool  platonic_solids        {true};
        bool  johnson_solids         {false};
    };
    Scene scene;

    struct Windows
    {
        bool brushes            {true};
        bool debug_view         {false};
        bool fly_camera         {false};
        bool grid               {false};
        bool layers             {false};
        bool log                {false};
        bool materials          {true};
        bool material_properties{true};
        bool mesh_properties    {false};
        bool node_properties    {true};
        bool node_tree          {true};
        bool operation_stack    {true};
        bool operations         {true};
        bool performance        {false};
        bool pipelines          {false};
        bool physics            {false};
        bool physics_tool       {false};
        bool post_processing    {false};
        bool tool_properties    {true};
        bool trs                {false};
        bool view               {false};
        bool viewport           {true};
        bool viewport_config    {false};
    };
    Windows windows;

    struct Viewport
    {
        bool polygon_fill     {true};
        bool edge_lines       {false};
        bool corner_points    {false};
        bool polygon_centroids{false};
    };
    Viewport viewport;

    struct Shader_monitor
    {
        bool enabled{true};
    };
    Shader_monitor shader_monitor;

    struct Id_renderer
    {
        bool enabled{true};
    };
    Id_renderer id_renderer;
};

} // namespace erhe::application
