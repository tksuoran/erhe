#pragma once

namespace erhe::commands {
    class Commands;
}
namespace erhe::graphics {
    class Device;
}
namespace erhe::imgui {
    class Imgui_renderer;
    class Imgui_windows;
}
namespace erhe::renderer {
    class Debug_renderer;
    class Text_renderer;
#if defined(ERHE_PHYSICS_LIBRARY_JOLT)
    class Jolt_debug_renderer;
#endif
}
namespace erhe::rendergraph {
    class Rendergraph;
}
namespace erhe::scene {
    class Scene_message_bus;
}
namespace erhe::scene_renderer {
    class Forward_renderer;
    class Shadow_renderer;
}
namespace erhe::window {
    class Context_window;
}

namespace editor {

class Brdf_slice;
class Brush_preview;
class Brush_tool;
class Clipboard;
class Clipboard_window;
class Create;
class Debug_renderer;
class App_message_bus;
class App_rendering;
class App_scenes;
class App_settings;
class App_windows;
class Fly_camera_tool;
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
class Input_state;
class Jolt_debug_renderer;
class Material_paint_tool;
class Material_preview;
class Mesh_memory;
class Move_tool;
class Operation_stack;
class Paint_tool;
class Physics_tool;
class Post_processing;
class Programs;
class Rotate_tool;
class Scale_tool;
class Scene_builder;
class Scene_commands;
class Scene_message_bus;
class Selection;
class Selection_tool;
class Settings_window;
class Sheet_window;
class Thumbnails;
class Time;
class Timeline_window;
class Tools;
class Transform_tool;
class Viewport_config_window;
class Scene_views;

class App_context
{
public:
    bool  OpenXR        {false};
    bool  OpenXR_mirror {false};
    bool  developer_mode{false};
    bool  renderdoc     {false};
    bool  use_sleep     {false};
    float sleep_margin  {false};

    erhe::commands::Commands*               commands              {nullptr};
    erhe::graphics::Device*                 graphics_device       {nullptr};
    erhe::imgui::Imgui_renderer*            imgui_renderer        {nullptr};
    erhe::imgui::Imgui_windows*             imgui_windows         {nullptr};
#if defined(ERHE_PHYSICS_LIBRARY_JOLT)
    erhe::renderer::Jolt_debug_renderer*    jolt_debug_renderer   {nullptr};
#endif
    erhe::renderer::Debug_renderer*         debug_renderer        {nullptr};
    erhe::renderer::Text_renderer*          text_renderer         {nullptr};
    erhe::rendergraph::Rendergraph*         rendergraph           {nullptr};
    erhe::scene::Scene_message_bus*         scene_message_bus     {nullptr};
    erhe::scene_renderer::Forward_renderer* forward_renderer      {nullptr};
    erhe::scene_renderer::Shadow_renderer*  shadow_renderer       {nullptr};
    erhe::window::Context_window*           context_window        {nullptr};
    Brdf_slice*                             brdf_slice            {nullptr};
    Brush_tool*                             brush_tool            {nullptr};
    Clipboard*                              clipboard             {nullptr};
    Clipboard_window*                       clipboard_window      {nullptr};
    Create*                                 create                {nullptr};
    App_message_bus*                        app_message_bus       {nullptr};
    App_rendering*                          app_rendering         {nullptr};
    App_scenes*                             app_scenes            {nullptr};
    App_settings*                           app_settings          {nullptr};
    App_windows*                            app_windows           {nullptr};
    Fly_camera_tool*                        fly_camera_tool       {nullptr};
    Grid_tool*                              grid_tool             {nullptr};
#if defined(ERHE_XR_LIBRARY_OPENXR)
    Hand_tracker*                           hand_tracker          {nullptr};
#endif
    Headset_view*                           headset_view          {nullptr};
    Hotbar*                                 hotbar                {nullptr};
    Hud*                                    hud                   {nullptr};
    Icon_set*                               icon_set              {nullptr};
    Id_renderer*                            id_renderer           {nullptr};
    Input_state*                            input_state           {nullptr};
    Material_paint_tool*                    material_paint_tool   {nullptr};
    Material_preview*                       material_preview      {nullptr};
    Brush_preview*                          brush_preview         {nullptr};
    Mesh_memory*                            mesh_memory           {nullptr};
    Move_tool*                              move_tool             {nullptr};
    Operation_stack*                        operation_stack       {nullptr};
    Paint_tool*                             paint_tool            {nullptr};
    Physics_tool*                           physics_tool          {nullptr};
    Post_processing*                        post_processing       {nullptr};
    Programs*                               programs              {nullptr};
    Rotate_tool*                            rotate_tool           {nullptr};
    Scale_tool*                             scale_tool            {nullptr};
    Scene_builder*                          scene_builder         {nullptr};
    Scene_commands*                         scene_commands        {nullptr};
    Selection*                              selection             {nullptr};
    Selection_tool*                         selection_tool        {nullptr};
    Settings_window*                        settings_window       {nullptr};
    Sheet_window*                           sheet_window          {nullptr};
    Thumbnails*                             thumbnails            {nullptr};
    Time*                                   time                  {nullptr};
    Timeline_window*                        timeline_window       {nullptr};
    Tools*                                  tools                 {nullptr};
    Transform_tool*                         transform_tool        {nullptr};
    Viewport_config_window*                 viewport_config_window{nullptr};
    Scene_views*                            scene_views           {nullptr};
};

}
