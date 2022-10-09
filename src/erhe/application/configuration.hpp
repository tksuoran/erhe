#pragma once

#include "erhe/components/components.hpp"
#include "erhe/gl/wrapper_enums.hpp"

#include "mini/ini.h"

namespace erhe::application {

class Configuration
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_type_name{"Configuration"};
    static constexpr uint32_t c_type_hash{
        compiletime_xxhash::xxh32(
            c_type_name.data(),
            c_type_name.size(),
            {}
        )
    };

    Configuration(int argc, char** argv);

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }

    // Public API
    [[nodiscard]] auto depth_clear_value_pointer() const -> const float *; // reverse_depth ? 0.0f : 1.0f;
    [[nodiscard]] auto depth_function           (const gl::Depth_function depth_function) const -> gl::Depth_function;

    class Imgui
    {
    public:
        bool        window_viewport{true};
        std::string primary_font   {"res/fonts/SourceSansPro-Regular.otf"};
        std::string mono_font      {"res/fonts/SourceCodePro-Semibold.otf"};
        float       font_size      {17.0f};
        int         icon_size      {16};
        int         vr_icon_size   {128};
        float       padding        {2.0f};
        float       rounding       {3.0f};
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
        bool  show         {true};
        bool  fullscreen   {false};
        bool  use_finish   {false};
        float sleep_time   {0.004f};
        float wait_time    {0.000f};
        int   swap_interval{1};
        int   width        {1920};
        int   height       {1080};
    };
    Window window;

    class Shadow_renderer
    {
    public:
        bool enabled                   {true};
        bool tight_frustum_fit         {true};
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
        float object_scale               {1.0f};
        float mass_scale                 {1.0f};
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

    class Window_entry
    {
    public:
        bool window{false};
        bool hud_window{false};
    };

    void get_window(mINI::INIStructure& ini, const char* key, Window_entry& entry);

    class Windows
    {
    public:
        Window_entry brushes             {false};
        Window_entry commands            {false};
        Window_entry debug_view          {false};
        Window_entry debug_visualizations{false};
        Window_entry fly_camera          {false};
        Window_entry grid                {false};
        Window_entry hover_tool          {false};
        Window_entry layers              {false};
        Window_entry log                 {false};
        Window_entry materials           {false};
        Window_entry material_properties {false};
        Window_entry mesh_properties     {false};
        Window_entry node_properties     {false};
        Window_entry node_tree           {false};
        Window_entry operation_stack     {false};
        Window_entry operations          {false};
        Window_entry performance         {false};
        Window_entry pipelines           {false};
        Window_entry physics             {false};
        Window_entry post_processing     {false};
        Window_entry render_graph        {false};
        Window_entry tool_properties     {false};
        Window_entry trs                 {false};
        Window_entry viewport            {true};
        Window_entry viewport_config     {false};
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

    class Trs_tool
    {
    public:
        float scale         {4.0f};
        bool  show_translate{true};
        bool  show_rotate   {false};
    };
    Trs_tool trs_tool;

    class Hud
    {
    public:
        bool  show{true};
        float x   {0.0f};
        float y   {0.0f};
        float z   {0.0f};
    };
    Hud hud;

    class Hotbar
    {
    public:
        bool  show{true};
        float x   {0.0f};
        float y   {0.0f};
        float z   {0.0f};
    };
    Hotbar hotbar;
};

} // namespace erhe::application
