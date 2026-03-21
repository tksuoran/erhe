#pragma once

#include "camera_controls_config.hpp"
#include "developer_config.hpp"
#include "graphics_config.hpp"
#include "grid_config.hpp"
#include "headset_config.hpp"
#include "hotbar_config.hpp"
#include "hud_config.hpp"
#include "id_renderer_config.hpp"
#include "mesh_memory_config.hpp"
#include "network_config.hpp"
#include "physics_config.hpp"
#include "renderdoc_config.hpp"
#include "renderer_config.hpp"
#include "scene_config.hpp"
#include "shader_monitor_config.hpp"
#include "text_renderer_config.hpp"
#include "threading_config.hpp"
#include "thumbnails_config.hpp"
#include "transform_tool_config.hpp"
#include "viewport_config_data.hpp"
#include "window_config.hpp"

#include <string_view>

struct Editor_config
{
    Camera_controls_config camera_controls;
    Developer_config       developer;
    Graphics_config        graphics;
    Grid_config            grid;
    Headset_config         headset;
    Hotbar_config          hotbar;
    Hud_config             hud;
    Id_renderer_config     id_renderer;
    Mesh_memory_config     mesh_memory;
    Network_config         network;
    Physics_config         physics;
    Renderdoc_config       renderdoc;
    Renderer_config        renderer;
    Scene_config           scene;
    Shader_monitor_config  shader_monitor;
    Text_renderer_config   text_renderer;
    Threading_config       threading;
    Thumbnails_config      thumbnails;
    Transform_tool_config  transform_tool;
    Viewport_config_data   viewport;
    Window_config          window;
};

auto load_editor_config(std::string_view path) -> Editor_config;
void save_editor_config(const Editor_config& config, std::string_view path);
