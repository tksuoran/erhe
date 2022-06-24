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

    class Imgui
    {
    public:
        bool enabled{true};
    };
    Imgui imgui;

    class Headset
    {
    public:
        bool openxr{false};
    };
    Headset headset;

    class Threading
    {
    public:
        bool parallel_initialization{true};
    };
    Threading threading;

    class Graphics
    {
    public:
        bool low_hdr          {false};
        bool reverse_depth    {true};
        bool simpler_shaders  {true};
        bool post_processing  {true};
        bool use_time_query   {true};
        bool force_no_bindless{false};
        int  msaa_sample_count{4};
    };
    Graphics graphics;

    class Mesh_memory
    {
    public:
        int vertex_buffer_size{32}; // in megabytes
        int index_buffer_size  {8}; // in megabytes
    };
    Mesh_memory mesh_memory;

    class Window
    {
    public:
        bool show             {true};
        bool fullscreen       {false};
        int  width            {1920};
        int  height           {1080};
    };
    Window window;

    class Shadow_renderer
    {
    public:
        bool enabled{true};
        int  shadow_map_resolution     {2048};
        int  shadow_map_max_light_count{8};
    };
    Shadow_renderer shadow_renderer;

    class Text_renderer
    {
    public:
        bool enabled{true};
    };
    Text_renderer text_renderer;

    class Renderer
    {
    public:
        int max_material_count  {256};
        int max_light_count     {256};
        int max_camera_count    {256};
        int max_primitive_count {8000}; // GLTF primitives
        int max_draw_count      {8000};
    };
    Renderer renderer;

    class Physics
    {
    public:
        bool enabled{false};
    };
    Physics physics;

    class Scene
    {
    public:
        float directional_light_intensity{20.0f};
        float directional_light_radius   {6.0f};
        float directional_light_height   {10.0f};
        int   directional_light_count    {4};
        float spot_light_intensity       {150.0f};
        float spot_light_radius          {20.0f};
        float spot_light_height          {10.0f};
        int   spot_light_count           {3};
        float floor_size                 {40.0f};
        int   floor_div                  {4};
        int   instance_count             {1};
        float instance_gap               {0.4f};
        int   detail                     {2};
        bool  floor                      {true};
        bool  gltf_files                 {false};
        bool  obj_files                  {false};
        bool  sphere                     {false};
        bool  torus                      {false};
        bool  cylinder                   {false};
        bool  cone                       {false};
        bool  platonic_solids            {true};
        bool  johnson_solids             {false};
    };
    Scene scene;

    class Windows
    {
    public:
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

    class Viewport
    {
    public:
        bool polygon_fill             {true};
        bool edge_lines               {false};
        bool corner_points            {false};
        bool polygon_centroids        {false};
        bool selection_bounding_sphere{true};
        bool selection_bounding_box   {true};
    };
    Viewport viewport;

    class Shader_monitor
    {
    public:
        bool enabled{true};
    };
    Shader_monitor shader_monitor;

    class Id_renderer
    {
    public:
        bool enabled{true};
    };
    Id_renderer id_renderer;

    class Renderdoc
    {
    public:
        bool capture_support{true};
    };
    Renderdoc renderdoc;

    class Grid
    {
    public:
        bool  enabled   {false};
        float cell_size {1.0f};
        int   cell_div  {10};
        int   cell_count{2};
    };
    Grid grid;

    class Camera_controls
    {
    public:
        bool  invert_x          {false};
        bool  invert_y          {false};
        float velocity_damp     {0.92f};
        float velocity_max_delta{0.004f};
        float sensitivity       {1.0f};
    };
    Camera_controls camera_controls;

    //class Input
    //{
    //};
    //Input input;
};

} // namespace erhe::application
