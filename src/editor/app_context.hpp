#pragma once

#include "renderers/render_settings_access.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <thread>
#include <vector>

namespace erhe::graphics {
    class Command_buffer;
    class Device;
    class Swapchain;
}
namespace erhe::imgui {
    class Imgui_renderer;
    class Imgui_windows;
    class Performance_window;
}
namespace erhe::renderer {
    class Debug_renderer;
    class Text_renderer;
}
namespace erhe::rendergraph { class Rendergraph; }
namespace erhe::scene_renderer {
    class Draw_list_renderer;
    class Scene_pass_resources;
    class Content_wide_line_renderer;
    class Forward_renderer;
    class Mesh_memory;
    class Program_interface;
    class Cull_frame_registry;
    class Gpu_cull_classifier;
    class Light_cluster_gpu_binner;
    class Shadow_renderer;
    class Shader_variant_cache;
    class Texel_renderer;
}
namespace erhe { class Item_base; }
namespace erhe::commands { class Commands; }
namespace erhe::property { class Dependency_property; }
namespace erhe::frame_pacing { class Frame_pacing_observer; }
namespace erhe::window { class Context_window; }

namespace tf {
    class Executor;
}

struct Developer_config;
struct Editor_settings_config;
struct Graphics_config;
struct Mesh_memory_config;
struct Renderer_config;
struct Text_renderer_config;
struct Window_config;

namespace editor {

class Analytic_hover_provider;
class Animation_player;
class Animation_window;
class Asset_manager;
class Brdf_slice;
class Brush_preview;
class Brush_tool;
class Clipboard;
class Clipboard_window;
class Create;
class Debug_renderer;
class App_message_bus;
class App_rendering;
class Material_set_factory;
class App_scenes;
class App_settings;
class App_windows;
class Render_settings;
class Editor_windows;
class Fly_camera_tool;
class Frame_pacing_window;
class Geometry_graph_window;
class Grid_tool;
#if defined(ERHE_XR_LIBRARY_OPENXR)
class Hand_tracker;
#endif
class Headset_view;
class Hotbar;
class Hud;
class Icon_set;
class Id_renderer;
class Imgui_window_scene_views;
class Inventory_window;
class Input_bindings_store;
class Input_bindings_window;
class Input_state;
class Lattice_tool;
class Material_paint_tool;
class Material_preview;
class Mesh_component_selection;
class Mesh_component_selection_tool;
class Move_tool;
class Navigation_gizmo_tool;
class Node_properties_window;
class Geometry_spreadsheet_window;
class Operation_stack;
class Operations;
class Paint_tool;
class Physics_tool;
class Post_processing;
class Prefab_library;
class Programs;
class Properties;
class Lightmap_baker;
class Pvs_baker;
class Lightmap_partitioner;
class Lightmap_report;
class Lightmap_streamer;
class Lightmap_texture_window;
class Lightmap_window;
class Pvs_window;
class Ray_trace_renderer;
class Ddgi_renderer;
class Radiance_cascades_renderer;
class Rendergraph_window;
class Rotate_tool;
class Scale_tool;
class Scene_builder;
class Scene_commands;
class Scene_commit_queue;
class Scene_payload_release;
class Bone_visualization;
class Weight_display;
class Weight_paint_tool;
class Selection;
class Selection_tool;
class Settings_window;
class Sheet_window;
class Sky_renderer;
class Texture_graph_window;
class Texture_file_loader;
class Thumbnails;
class Time;
class Tools;
class Transform_tool;
class Transform_update_stats_tracker;
class Scene_views;

class App_context
{
public:
    bool  OpenXR        {false};
    bool  OpenXR_mirror {false};
    bool  developer_mode{false};
    bool  renderdoc     {false};
    // --no-post-processing: session-wide kill switch, overriding both
    // editor_settings.json and callers that default post processing to
    // enabled (Scene_views::create_viewport_scene_view clamps on it).
    bool  force_post_processing_off{false};
    // --fix-spot-lights: workaround for a broken glTF exporter. Every spot
    // light parsed from a glTF asset gets full color value, intensity
    // 1000, a doubled outer cone angle and the original outer cone angle as
    // its inner cone angle. Import-time only - the loaded scene keeps the
    // fixed-up values, and nothing rewrites the source file.
    bool  fix_gltf_spot_lights{false};
    bool  use_sleep     {false};
    float sleep_margin  {0.0f}; // TODO

    // Power saving: render at a reduced frequency when the window is idle.
    // Cached from Window_config so the per-frame loop reads plain fields.
    bool  power_save    {true}; // master enable
    int   unfocused_fps {30};   // frame cap while visible but unfocused
    int   hidden_fps    {10};   // frame cap while not visible (also pauses sim + skips render)

    Developer_config*       developer_config    {nullptr};
    Graphics_config*        graphics_config     {nullptr};
    Mesh_memory_config*     mesh_memory_config  {nullptr};
    Renderer_config*        renderer_config     {nullptr};
    Text_renderer_config*   text_renderer_config{nullptr};
    Window_config*          window_config       {nullptr};
    // Owned by App_settings (settings_store().get_settings()); exposed
    // directly because the config data is read pervasively.
    Editor_settings_config* editor_settings      {nullptr};
    // The copies of the settings the render phase reads (Editor::m_render_settings,
    // one per frame in flight; renderers/render_settings.hpp), filled at the
    // end of M2; render_settings->x reads the calling role's copy
    // (renderers/render_settings_access.hpp).
    Render_settings_access  render_settings      {};

    tf::Executor*                           executor              {nullptr};
    std::atomic_int                         pending_async_ops     {};
    std::atomic_int                         running_async_ops     {};
    // Worker-produced scene mutations waiting for the main thread; applied
    // once per tick by Editor::tick() (see scene/scene_commit_queue.hpp).
    Scene_commit_queue*                     scene_commit_queue    {nullptr};
    // Destroys the mesh payload of closed scenes on a worker
    // (see scene/scene_payload_release.hpp).
    Scene_payload_release*                  scene_payload_release {nullptr};

    // Async work that has not yet landed in a scene: worker tasks pending
    // or running, operations queued for the main thread, scene commits
    // waiting for the next flush and asset loads in flight. Stale-data guards
    // (lightmap prepare, MCP get_async_status) must treat any of these as
    // "scene still changing".
    [[nodiscard]] auto get_async_in_flight_count() const -> std::size_t;

    // True while any of the above is outstanding, i.e. exactly the condition
    // under which the MCP get_async_status tool reports the editor as not
    // idle. Every term can still change the content of a scene, and every
    // such change attaches, detaches or rebuilds raytrace instances, so this
    // is also the "the acceleration structure cannot settle" predicate: the
    // hover path (Scene_view::update_hover_with_raytrace) consults it and
    // skips the commit + trace entirely while it is true. Asset load tasks
    // are part of it for the same reason - a load publishes its meshes into
    // the scene while it runs, not only when it finishes.
    [[nodiscard]] auto is_scene_load_in_flight() const -> bool;

    // The one place that maps a property's consequence flags
    // (erhe::property::Property_flags, doc/erhe/property_system.md D11) to
    // editor actions. Called after every property write made through
    // Property_set_operation / Property_set_apply_operation.
    void on_item_property_changed(erhe::Item_base& item, const erhe::property::Dependency_property& property);

    // Set at the top of Editor construction, before parts construction.
    // Main-thread-only parts (e.g. Operation_stack) verify their callers
    // against this instead of pretending to be thread-safe with a lock.
    std::thread::id                         main_thread_id        {};
    // The thread that runs the render phase (doc/editor/render_thread.md
    // section 2.1). Equal to main_thread_id while the render phase runs inline
    // on the main thread.
    std::thread::id                         render_thread_id      {};

    erhe::commands::Commands*               commands              {nullptr};
    // Hover sources without scene meshes (the transform gizmo), consulted by
    // Scene_view::update_hover_with_analytic_tools(). Filled once, after all
    // parts are constructed; see scene/analytic_hover_provider.hpp.
    std::vector<Analytic_hover_provider*>   analytic_hover_providers{};
    // The main thread's cb (doc/editor/render_thread.md section 2.3): the
    // upload cb during the prepare phase (M2), null during the overlap phase
    // (M1) and between frames, and the init cb during startup. The render
    // phase (R) runs while M1 of the next frame does, so it neither writes
    // nor reads this: it passes its frame cb to its recorders explicitly
    // (Thumbnails::update, the preview renders, the DDGI / radiance cascades
    // ticks, the rendergraph). Runtime-triggered main-thread code paths (e.g.
    // Scene_commands::create_new_rendertarget, Hotbar / Hud rendertarget
    // construction) read this to record GPU work into the same cb.
    erhe::graphics::Command_buffer*         current_command_buffer{nullptr};
    // Render phase (R) state, written by the thread running R at its start
    // from its Frame_packet copy and read only by R: Time's host system
    // time of the frame being rendered (the selection outline pulse). Time
    // itself advances in M1 of the next frame while R runs.
    int64_t                                 render_phase_host_system_time_ns{0};
    // Main thread, prepare phase (or outside the frame loop): joins the
    // render phase still in flight, as the next kick would, so a standalone
    // submit (record + submit + Device::wait_idle on the main thread) that
    // writes or reads resources the render phase records with is ordered
    // after that render phase's recording and submits. A no-op without frame
    // pipelining (the end of M1 joined already) and when no frame is
    // outstanding; set by Editor (null in programs without one). The
    // lightmap baker and streamer call it before their standalone submits.
    std::function<void()>                   join_render_phase;
    // Main thread: the device frame of the newest render phase the main
    // thread joined (Frame_result::device_frame_id, set by
    // Editor::on_frame_joined); empty before the first join. What the
    // render phase writes for the main thread (Render_frame_results: the
    // shadow light projections, the composition pass statistics, the DDGI
    // and radiance cascades probe readbacks) the main thread reads as the
    // result of this frame, one frame late
    // (doc/editor/editor.md "Frame phases and the render thread").
    std::optional<uint64_t>                 last_rendered_frame_index{};
    // The frame whose render phase results the calling phase reads: the
    // render phase (R) its own frame (Device::get_frame_index() on the render
    // role; the results its producers wrote earlier in the frame, e.g. the
    // headset recording its tools in R), every main-thread phase
    // last_rendered_frame_index. For the code that runs in both (the
    // renderables and the debug visualizations).
    [[nodiscard]] auto get_results_frame_index() const -> std::optional<uint64_t>;
    // Set on the main thread to end the run loop after the current frame -
    // the programmatic counterpart of the window close event. The MCP
    // request_exit tool sets it (ctest stops the editor it started as the
    // mcp_server_tests fixture this way); Editor::run() reads it.
    bool                                    close_requested       {false};
    erhe::graphics::Device*                 graphics_device       {nullptr};
    erhe::imgui::Imgui_renderer*            imgui_renderer        {nullptr};
    erhe::imgui::Imgui_windows*             imgui_windows         {nullptr};
    erhe::imgui::Performance_window*        performance_window    {nullptr};
    // Frame pacing introspection for the MCP server (get_frame_pacing_*):
    // the observer wraps the pacer (decisions, estimates); the window owns
    // the persistent capture of per-frame samples. Both live for the whole
    // editor lifetime once set.
    Frame_pacing_window*                    frame_pacing_window   {nullptr};
    erhe::frame_pacing::Frame_pacing_observer* frame_pacing_observer{nullptr};
    // Per-frame Scene::update_node_transforms() cost aggregation; feeds the
    // Performance window plots and MCP get_transform_update_stats.
    Transform_update_stats_tracker*         transform_update_stats_tracker{nullptr};
    erhe::renderer::Debug_renderer*                   debug_renderer            {nullptr};
    erhe::rendergraph::Rendergraph*                   rendergraph               {nullptr};
    erhe::renderer::Text_renderer*                    text_renderer             {nullptr};
    erhe::scene_renderer::Content_wide_line_renderer* content_wide_line_renderer{nullptr};
    erhe::scene_renderer::Forward_renderer*           forward_renderer          {nullptr};
    // The draw-list colour entry point. Shares forward_renderer's
    // Scene_pass_resources; Composition_pass picks between the two by the
    // path it has already decided on.
    erhe::scene_renderer::Draw_list_renderer*         draw_list_renderer        {nullptr};
    erhe::scene_renderer::Shadow_renderer*            shadow_renderer           {nullptr};
    erhe::scene_renderer::Gpu_cull_classifier*        gpu_cull_classifier       {nullptr};
    erhe::scene_renderer::Light_cluster_gpu_binner*   light_cluster_gpu_binner  {nullptr};
    erhe::scene_renderer::Cull_frame_registry*        cull_frame_registry       {nullptr};
    erhe::scene_renderer::Texel_renderer*             texel_renderer            {nullptr};
    erhe::scene_renderer::Shader_variant_cache*       shader_variant_cache      {nullptr};
    erhe::scene_renderer::Program_interface*          program_interface         {nullptr};

    erhe::window::Context_window*      context_window       {nullptr};
    Animation_player*                  animation_player     {nullptr};
    Animation_window*                  animation_window     {nullptr};
    Asset_manager*                     asset_manager        {nullptr};
    Brdf_slice*                        brdf_slice           {nullptr};
    Brush_tool*                        brush_tool           {nullptr};
    Clipboard*                         clipboard            {nullptr};
    Clipboard_window*                  clipboard_window     {nullptr};
    Create*                            create               {nullptr};
    App_message_bus*                   app_message_bus      {nullptr};
    App_rendering*                     app_rendering        {nullptr};
    // The shared fallback texture / sampler pair and the empty Material_set
    // every GPU-backed Material_set is built from. Published before the
    // construction taskflow, unlike everything else here, because the objects
    // that own sets are built inside it.
    Material_set_factory*              material_set_factory {nullptr};
    App_scenes*                        app_scenes           {nullptr};
    App_settings*                      app_settings         {nullptr};
    App_windows*                       app_windows          {nullptr};
    Editor_windows*                    editor_windows       {nullptr};
    Fly_camera_tool*                   fly_camera_tool      {nullptr};
    Geometry_graph_window*             geometry_graph_window{nullptr};
    Grid_tool*                         grid_tool            {nullptr};
#if defined(ERHE_XR_LIBRARY_OPENXR)
    Hand_tracker*                      hand_tracker         {nullptr};
#endif
    Headset_view*                      headset_view         {nullptr};
    Hotbar*                            hotbar               {nullptr};
    Hud*                               hud                  {nullptr};
    Icon_set*                          icon_set             {nullptr};
    Id_renderer*                       id_renderer          {nullptr};
    Input_bindings_store*              input_bindings_store {nullptr};
    Input_bindings_window*             input_bindings_window{nullptr};
    Input_state*                       input_state          {nullptr};
    Inventory_window*                  inventory_window     {nullptr};
    Lattice_tool*                      lattice_tool         {nullptr};
    Material_paint_tool*               material_paint_tool  {nullptr};
    Material_preview*                  material_preview     {nullptr};
    Brush_preview*                     brush_preview        {nullptr};
    erhe::scene_renderer::Mesh_memory* mesh_memory          {nullptr};
    Mesh_component_selection*          mesh_component_selection     {nullptr};
    Mesh_component_selection_tool*     mesh_component_selection_tool{nullptr};
    Move_tool*                         move_tool            {nullptr};
    Navigation_gizmo_tool*             navigation_gizmo_tool{nullptr};
    Node_properties_window*            node_properties_window{nullptr};
    Geometry_spreadsheet_window*       geometry_spreadsheet_window{nullptr};
    Operation_stack*                   operation_stack      {nullptr};
    Operations*                        operations           {nullptr};
    Properties*                        properties           {nullptr}; // the primary Properties window
    Paint_tool*                        paint_tool           {nullptr};
    Physics_tool*                      physics_tool         {nullptr};
    Post_processing*                   post_processing      {nullptr};
    Prefab_library*                    prefab_library       {nullptr};
    Programs*                          programs             {nullptr};
    Ray_trace_renderer*                ray_trace_renderer   {nullptr};
    Ddgi_renderer*                     ddgi_renderer        {nullptr};
    Radiance_cascades_renderer*        radiance_cascades_renderer{nullptr};
    Lightmap_baker*                    lightmap_baker       {nullptr};
    // The potentially visible set bake (doc/plans/pvs.md; MCP pvs_bake).
    Pvs_baker*                         pvs_baker            {nullptr};
    // World-space mesh partitioner: uniquifies + tile-clips lightmapped
    // meshes into per-tile piece meshes ("Lightmap Pieces" group).
    Lightmap_partitioner*              lightmap_partitioner {nullptr};
    // Failure/warning sink for the lightmap pipeline; written from worker
    // threads (UV unwrap) and the baker, displayed by the Lightmap window.
    Lightmap_report*                   lightmap_report      {nullptr};
    // Baked-tile disk streamer; invalidated when the partition changes so
    // manifest regions re-resolve against the new piece set.
    Lightmap_streamer*                 lightmap_streamer    {nullptr};
    Lightmap_texture_window*           lightmap_texture_window{nullptr};
    Lightmap_window*                   lightmap_window      {nullptr};
    Pvs_window*                        pvs_window           {nullptr};
    Rendergraph_window*                rendergraph_window   {nullptr};
    Rotate_tool*                       rotate_tool          {nullptr};
    Bone_visualization*                bone_visualization   {nullptr};
    Weight_display*                    weight_display       {nullptr};
    Weight_paint_tool*                 weight_paint_tool    {nullptr};
    Scale_tool*                        scale_tool           {nullptr};
    Scene_builder*                     scene_builder        {nullptr};
    Scene_commands*                    scene_commands       {nullptr};
    Selection*                         selection            {nullptr};
    Selection_tool*                    selection_tool       {nullptr};
    Settings_window*                   settings_window      {nullptr};
    Sky_renderer*                      sky_renderer         {nullptr};
    Sheet_window*                      sheet_window         {nullptr};
    Texture_graph_window*              texture_graph_window {nullptr};
    Texture_file_loader*               texture_file_loader  {nullptr};
    Thumbnails*                        thumbnails           {nullptr};
    Time*                              time                 {nullptr};
    Tools*                             tools                {nullptr};
    Transform_tool*                    transform_tool       {nullptr};
    Scene_views*                       scene_views          {nullptr};
};

}
