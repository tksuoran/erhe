#include "erhe/application/configuration.hpp"
#include "erhe/application/log.hpp"

#include "erhe/graphics/state/depth_stencil_state.hpp"
#include "mini/ini.h"

#include <cxxopts.hpp>

#include <algorithm>
#include <cctype>
#include <string>

namespace erhe::application {

constexpr float float_zero_value{0.0f};
constexpr float float_one_value {1.0f};

auto str(const bool value) -> const char*
{
    return value ? "true" : "false";
}

auto to_lower(std::string data) -> std::string
{
    std::transform(
        data.begin(),
        data.end(),
        data.begin(),
        [](unsigned char c)
        {
            return std::tolower(c);
        }
    );
    return data;
}

void ini_get(
    const mINI::INIMap<std::string>& ini,
    std::string                      key,
    int&                             destination
)
{
    if (ini.has(key))
    {
        destination = std::stoi(ini.get(key));
    }
}

void ini_get(
    const mINI::INIMap<std::string>& ini,
    std::string                      key,
    float&                           destination
)
{
    if (ini.has(key))
    {
        destination = std::stof(ini.get(key));
    }
}

void ini_get(
    const mINI::INIMap<std::string>& ini,
    std::string                      key,
    std::string&                     destination
)
{
    if (ini.has(key))
    {
        destination = ini.get(key);
    }
}

void ini_get(
    const mINI::INIMap<std::string>& ini,
    std::string                      key,
    bool&                            destination
)
{
    if (ini.has(key))
    {
        const std::string value = to_lower(ini.get(key));
        if (value == "1" || value == "yes" || value == "true" || value == "on")
        {
            destination = true;
        }
        else if (value == "0" || value == "no" || value == "false" || value == "off")
        {
            destination = false;
        }
    }
}

Configuration::Configuration(int argc, char** argv)
    : erhe::components::Component      {c_label}
{
    mINI::INIFile file("erhe.ini");
    mINI::INIStructure ini;
    if (file.read(ini))
    {
        if (ini.has("imgui"))
        {
            const auto& section = ini["imgui"];
            ini_get(section, "enabled", imgui.enabled);
        }
        if (ini.has("headset"))
        {
            const auto& section = ini["headset"];
            ini_get(section, "openxr", headset.openxr);
        }
        if (ini.has("threading"))
        {
            const auto& section = ini["threading"];
            ini_get(section, "parallel_init", threading.parallel_initialization);
        }
        if (ini.has("graphics"))
        {
            const auto& section = ini["graphics"];
            ini_get(section, "reverse_depth",   graphics.reverse_depth);
            ini_get(section, "simpler_shaders", graphics.simpler_shaders);
        }
        if (ini.has("mesh_memory"))
        {
            const auto& section = ini["mesh_memory"];
            ini_get(section, "vertex_buffer_size", mesh_memory.vertex_buffer_size);
            ini_get(section, "index_buffer_size",  mesh_memory.index_buffer_size);
        }
        if (ini.has("window"))
        {
            const auto& section = ini["window"];
            ini_get(section, "fullscreen",        window.fullscreen);
            ini_get(section, "width",             window.width);
            ini_get(section, "height",            window.height);
            ini_get(section, "msaa_sample_count", window.msaa_sample_count);
        }

        if (ini.has("shadow_renderer"))
        {
            const auto& section = ini["graphics`"];
            ini_get(section, "enabled",                    shadow_renderer.enabled);
            ini_get(section, "shadow_map_resolution",      shadow_renderer.shadow_map_resolution);
            ini_get(section, "shadow_map_max_light_count", shadow_renderer.shadow_map_max_light_count);
        }

        if (ini.has("forward_renderer"))
        {
            const auto& section = ini["forward_renderer"];
            ini_get(section, "max_material_count",  forward_renderer.max_material_count );
            ini_get(section, "max_light_count",     forward_renderer.max_light_count    );
            ini_get(section, "max_camera_count",    forward_renderer.max_camera_count   );
            ini_get(section, "max_primitive_count", forward_renderer.max_primitive_count);
            ini_get(section, "max_draw_count",      forward_renderer.max_draw_count     );
        }

        if (ini.has("physics"))
        {
            const auto& section = ini["physics"];
            ini_get(section, "enabled", physics.enabled);
        }

        if (ini.has("windows"))
        {
            const auto& section = ini["windows"];

            ini_get(section, "brushes",             windows.brushes            );
            ini_get(section, "debug_view",          windows.debug_view         );
            ini_get(section, "fly_camera",          windows.fly_camera         );
            ini_get(section, "grid",                windows.grid               );
            ini_get(section, "layers",              windows.layers             );
            ini_get(section, "log",                 windows.log                );
            ini_get(section, "materials",           windows.materials          );
            ini_get(section, "material_properties", windows.material_properties);
            ini_get(section, "mesh_properties",     windows.mesh_properties    );
            ini_get(section, "node_properties",     windows.node_properties    );
            ini_get(section, "node_tree",           windows.node_tree          );
            ini_get(section, "operation_stack",     windows.operation_stack    );
            ini_get(section, "operations",          windows.operations         );
            ini_get(section, "performance",         windows.performance        );
            ini_get(section, "pipelines",           windows.pipelines          );
            ini_get(section, "physics",             windows.physics            );
            ini_get(section, "physics_tool",        windows.physics_tool       );
            ini_get(section, "post_processing",     windows.post_processing    );
            ini_get(section, "tool_properties",     windows.tool_properties    );
            ini_get(section, "trs",                 windows.trs                );
            ini_get(section, "view",                windows.view               );
            ini_get(section, "viewport",            windows.viewport           );
            ini_get(section, "viewport_config",     windows.viewport_config    );
        }

        if (ini.has("scene"))
        {
            const auto& section = ini["scene"];

            ini_get(section, "directional_light_count", scene.directional_light_count);
            ini_get(section, "spot_light_count",        scene.spot_light_count);
            ini_get(section, "floor_size",              scene.floor_size);
            ini_get(section, "instance_count",          scene.instance_count);
            ini_get(section, "instance_gap",            scene.instance_gap);
            ini_get(section, "gltf_files",              scene.gltf_files);
            ini_get(section, "obj_files",               scene.obj_files);
            ini_get(section, "floor",                   scene.floor);
            ini_get(section, "sphere",                  scene.sphere);
            ini_get(section, "torus",                   scene.torus);
            ini_get(section, "cylinder",                scene.cylinder);
            ini_get(section, "cone",                    scene.cone);
            ini_get(section, "platonic_solids",         scene.platonic_solids);
            ini_get(section, "johnson_solids",          scene.johnson_solids);
        }
    }

    cxxopts::Options options("Editor", "Erhe Editor (C) 2022 Timo Suoranta");

    options.add_options()
        ("gui",                        "Enable ImGui hosted viewports",         cxxopts::value<bool>()->default_value(str( imgui.enabled)))
        ("no-gui",                     "Disable ImGui hosted viewports",        cxxopts::value<bool>()->default_value(str(!imgui.enabled)))
        ("openxr",                     "Enable OpenXR HMD support",             cxxopts::value<bool>()->default_value(str( headset.openxr)))
        ("no-openxr",                  "Disable OpenXR HMD support",            cxxopts::value<bool>()->default_value(str(!headset.openxr)))
        ("parallel-initialization",    "Use parallel component initialization", cxxopts::value<bool>()->default_value(str( threading.parallel_initialization)))
        ("no-parallel-initialization", "Use serial component initialization",   cxxopts::value<bool>()->default_value(str(!threading.parallel_initialization)))
        ("reverse-depth",              "Enable reverse depth",                  cxxopts::value<bool>()->default_value(str( graphics.reverse_depth)))
        ("no-reverse-depth",           "Disable reverse depth",                 cxxopts::value<bool>()->default_value(str(!graphics.reverse_depth)));

    try
    {
        auto arguments = options.parse(argc, argv);

        imgui.enabled                     = arguments["gui"                    ].as<bool>() && !arguments["no-gui"                    ].as<bool>();
        headset.openxr                    = arguments["openxr"                 ].as<bool>() && !arguments["no-openxr"                 ].as<bool>();
        threading.parallel_initialization = arguments["parallel-initialization"].as<bool>() && !arguments["no-parallel-initialization"].as<bool>();
        graphics.reverse_depth            = arguments["reverse-depth"          ].as<bool>() && !arguments["no-reverse-depth"          ].as<bool>();
    }
    catch (const std::exception& e)
    {
        log_startup->error(
            "Error parsing command line argumenst: {}",
            e.what()
        );
    }
}

auto Configuration::depth_clear_value_pointer() const -> const float*
{
    return graphics.reverse_depth ? &float_zero_value : &float_one_value;
}

auto Configuration::depth_function(
    const gl::Depth_function depth_function
) const -> gl::Depth_function
{
    return graphics.reverse_depth
        ? erhe::graphics::reverse(depth_function)
        : depth_function;
}

} // namespace erhe::application

