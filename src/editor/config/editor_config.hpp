#pragma once

#include "config/generated/camera_controls_config.hpp"
#include "config/generated/developer_config.hpp"
#include "erhe_graphics/generated/graphics_config.hpp"
#include "config/generated/grid_config.hpp"
#include "config/generated/headset_config.hpp"
#include "config/generated/hotbar_config.hpp"
#include "config/generated/hud_config.hpp"
#include "config/generated/inventory_slot.hpp"
#include "config/generated/inventory_config.hpp"
#include "config/generated/id_renderer_config.hpp"
#include "config/generated/mesh_memory_config.hpp"
#include "config/generated/network_config.hpp"
#include "config/generated/physics_config.hpp"
#include "config/generated/renderdoc_config.hpp"
#include "config/generated/renderer_config.hpp"
#include "config/generated/scene_config.hpp"
#include "config/generated/shader_monitor_config.hpp"
#include "config/generated/text_renderer_config.hpp"
#include "config/generated/threading_config.hpp"
#include "config/generated/thumbnails_config.hpp"
#include "config/generated/transform_tool_config.hpp"
#include "config/generated/viewport_config_data.hpp"
#include "config/generated/window_config.hpp"

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
    Inventory_config       inventory;
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
