#pragma once

#include "tools/pointer_context.hpp"
#include "tools/selection_tool.hpp"
#include "erhe/toolkit/window.hpp"

#include "imgui.h"
#include "renderdoc_app.h"

#include <chrono>
#include <map>
#include <optional>
#include <thread>

namespace erhe::graphics
{
    class Framebuffer;
    class OpenGL_state_tracker;
    class Renderbuffer;
    class Texture;
}

namespace erhe::scene
{
    class ICamera;
}


namespace editor {

class Application;
class Brushes;
class Camera_properties;
class Fly_camera_tool;
class Forward_renderer;
class Grid_tool;
class Hover_tool;
class Id_renderer;
class Light_properties;
class Line_renderer;
class Material_properties;
class Mesh_properties;
class Node_properties;
class Operations;
class Operation_stack;
class Physics_tool;
class Physics_window;
class Scene_manager;
class Scene_root;
class Selection_tool;
class Shader_monitor;
class Shadow_renderer;
class Text_renderer;
class Tool;
class Trs_tool;
class Viewport_config;
class Viewport_window;
class Window;

class Editor_rendering;
class Editor_tools;
class Editor_view;
class Editor_time;

}
