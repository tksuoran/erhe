#include "erhe/application/configuration.hpp"
#include "erhe/application/application_log.hpp"

#include "erhe/graphics/state/depth_stencil_state.hpp"

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
    data = data.substr(0, data.find(' '));
    data = data.substr(0, data.find('\t'));
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

auto split(
    const std::string& s,
    const char         delimeter
) -> std::vector<std::string>
{
    std::vector<std::string> result;
    std::stringstream        ss{s};
    std::string              item;

    while (std::getline(ss, item, delimeter))
    {
        result.push_back(item);
    }

    return result;
}

auto trim(
    const std::string& str,
    const std::string& whitespace = " \t"
) -> std::string
{
    const auto strBegin = str.find_first_not_of(whitespace);
    if (strBegin == std::string::npos)
    {
        return ""; // no content
    }

    const auto strEnd = str.find_last_not_of(whitespace);
    const auto strRange = strEnd - strBegin + 1;

    return str.substr(strBegin, strRange);
}

void ini_get(
    const mINI::INIMap<std::string>& ini,
    std::string                      key,
    glm::vec4&                       destination
)
{
    if (ini.has(key))
    {
        std::string value = ini.get(key);
        const auto values = split(value, ',');
        destination.w = 1.0f;
        if (values.size() >= 3)
        {
            destination.x = std::stof(trim(values.at(0)));
            destination.y = std::stof(trim(values.at(1)));
            destination.z = std::stof(trim(values.at(2)));
        }
        if (values.size() >= 4)
        {
            destination.w = std::stof(trim(values.at(3)));
        }
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

void Configuration::get_window(mINI::INIStructure& ini, const char* key, Window_entry& entry)
{
    if (ini.has("windows"))
    {
        const auto& section = ini["windows"];
        ini_get(section, key, entry.window);
    }
    if (ini.has("hud_windows"))
    {
        const auto& section = ini["hud_windows"];
        ini_get(section, key, entry.hud_window);
    }
}

Configuration::Configuration(int argc, char** argv)
    : erhe::components::Component{c_type_name}
{
    mINI::INIFile file("erhe.ini");
    mINI::INIStructure ini;
    if (file.read(ini))
    {
        if (ini.has("imgui"))
        {
            const auto& section = ini["imgui"];
            ini_get(section, "window_viewport", imgui.window_viewport);
            ini_get(section, "primary_font",    imgui.primary_font);
            ini_get(section, "mono_font",       imgui.mono_font);
            ini_get(section, "font_size",       imgui.font_size);
            ini_get(section, "icon_size",       imgui.icon_size);
            ini_get(section, "padding",         imgui.padding);
            ini_get(section, "rounding",        imgui.rounding);
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
            ini_get(section, "low_hdr",           graphics.low_hdr);
            ini_get(section, "reverse_depth",     graphics.reverse_depth);
            ini_get(section, "simpler_shaders",   graphics.simpler_shaders);
            ini_get(section, "post_processing",   graphics.post_processing);
            ini_get(section, "use_time_query",    graphics.use_time_query);
            ini_get(section, "force_no_bindless", graphics.force_no_bindless);
            ini_get(section, "msaa_sample_count", graphics.msaa_sample_count);
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
            ini_get(section, "show",          window.show);
            ini_get(section, "fullscreen",    window.fullscreen);
            ini_get(section, "use_finish",    window.use_finish);
            ini_get(section, "sleep_time",    window.sleep_time);
            ini_get(section, "wait_time",     window.wait_time);
            ini_get(section, "swap_interval", window.swap_interval);
            ini_get(section, "width",         window.width);
            ini_get(section, "height",        window.height);
        }

        if (ini.has("shadow_renderer"))
        {
            const auto& section = ini["shadow_renderer"];
            ini_get(section, "enabled",                    shadow_renderer.enabled);
            ini_get(section, "tight_frustum_fit",          shadow_renderer.tight_frustum_fit);
            ini_get(section, "shadow_map_resolution",      shadow_renderer.shadow_map_resolution);
            ini_get(section, "shadow_map_max_light_count", shadow_renderer.shadow_map_max_light_count);
        }

        if (ini.has("text_renderer"))
        {
            const auto& section = ini["text_renderer"];
            ini_get(section, "enabled", text_renderer.enabled);
        }

        if (ini.has("renderer"))
        {
            const auto& section = ini["renderer"];
            ini_get(section, "max_material_count",  renderer.max_material_count );
            ini_get(section, "max_light_count",     renderer.max_light_count    );
            ini_get(section, "max_camera_count",    renderer.max_camera_count   );
            ini_get(section, "max_primitive_count", renderer.max_primitive_count);
            ini_get(section, "max_draw_count",      renderer.max_draw_count     );
        }

        if (ini.has("physics"))
        {
            const auto& section = ini["physics"];
            ini_get(section, "enabled", physics.enabled);
        }

        get_window(ini, "brushes",              windows.brushes             );
        get_window(ini, "commands",             windows.commands            );
        get_window(ini, "debug_view",           windows.debug_view          );
        get_window(ini, "debug_visualizations", windows.debug_visualizations);
        get_window(ini, "fly_camera",           windows.fly_camera          );
        get_window(ini, "grid",                 windows.grid                );
        get_window(ini, "headset_view",         windows.headset_view        );
        get_window(ini, "hover_tool",           windows.hover_tool          );
        get_window(ini, "layers",               windows.layers              );
        get_window(ini, "log",                  windows.log                 );
        get_window(ini, "materials",            windows.materials           );
        get_window(ini, "material_properties",  windows.material_properties );
        get_window(ini, "mesh_properties",      windows.mesh_properties     );
        get_window(ini, "node_properties",      windows.node_properties     );
        get_window(ini, "node_tree",            windows.node_tree           );
        get_window(ini, "operation_stack",      windows.operation_stack     );
        get_window(ini, "operations",           windows.operations          );
        get_window(ini, "performance",          windows.performance         );
        get_window(ini, "pipelines",            windows.pipelines           );
        get_window(ini, "physics",              windows.physics             );
        get_window(ini, "post_processing",      windows.post_processing     );
        get_window(ini, "render_graph",         windows.render_graph        );
        get_window(ini, "tool_properties",      windows.tool_properties     );
        get_window(ini, "trs",                  windows.trs                 );
        get_window(ini, "viewport",             windows.viewport            );
        get_window(ini, "viewport_config",      windows.viewport_config     );

        if (ini.has("scene"))
        {
            const auto& section = ini["scene"];
            ini_get(section, "directional_light_intensity", scene.directional_light_intensity);
            ini_get(section, "directional_light_radius",    scene.directional_light_radius);
            ini_get(section, "directional_light_height",    scene.directional_light_height);
            ini_get(section, "directional_light_count",     scene.directional_light_count);
            ini_get(section, "spot_light_intensity",        scene.spot_light_intensity);
            ini_get(section, "spot_light_radius",           scene.spot_light_radius);
            ini_get(section, "spot_light_height",           scene.spot_light_height);
            ini_get(section, "spot_light_count",            scene.spot_light_count);
            ini_get(section, "floor_size",                  scene.floor_size);
            ini_get(section, "instance_count",              scene.instance_count);
            ini_get(section, "instance_gap",                scene.instance_gap);
            ini_get(section, "object_scale",                scene.object_scale);
            ini_get(section, "mass_scale",                  scene.mass_scale);
            ini_get(section, "detail",                      scene.detail);
            ini_get(section, "gltf_files",                  scene.gltf_files);
            ini_get(section, "obj_files",                   scene.obj_files);
            ini_get(section, "floor",                       scene.floor);
            ini_get(section, "sphere",                      scene.sphere);
            ini_get(section, "torus",                       scene.torus);
            ini_get(section, "cylinder",                    scene.cylinder);
            ini_get(section, "cone",                        scene.cone);
            ini_get(section, "platonic_solids",             scene.platonic_solids);
            ini_get(section, "johnson_solids",              scene.johnson_solids);
        }

        if (ini.has("viewport"))
        {
            const auto& section = ini["viewport"];
            ini_get(section, "polygon_fill",              viewport.polygon_fill);
            ini_get(section, "edge_lines",                viewport.edge_lines);
            ini_get(section, "selection_edge_lines",      viewport.selection_edge_lines);
            ini_get(section, "corner_points",             viewport.corner_points);
            ini_get(section, "polygon_centroids",         viewport.polygon_centroids);
            ini_get(section, "selection_bounding_box",    viewport.selection_bounding_box);
            ini_get(section, "selection_bounding_sphere", viewport.selection_bounding_sphere);
            ini_get(section, "selection_edge_color",      viewport.selection_edge_color);
            ini_get(section, "clear_color",               viewport.clear_color);
        }

        if (ini.has("shader_monitor"))
        {
            const auto& section = ini["shader_monitor"];
            ini_get(section, "enabled", shader_monitor.enabled);
        }

        if (ini.has("id_renderer"))
        {
            const auto& section = ini["id_renderer"];
            ini_get(section, "enabled", id_renderer.enabled);
        }

        if (ini.has("renderdoc"))
        {
            const auto& section = ini["renderdoc"];
            ini_get(section, "capture_support", renderdoc.capture_support);
        }

        if (ini.has("grid"))
        {
            const auto& section = ini["grid"];
            ini_get(section, "enabled",     grid.enabled);
            ini_get(section, "major_color", grid.major_color);
            ini_get(section, "minor_color", grid.minor_color);
            ini_get(section, "major_width", grid.major_width);
            ini_get(section, "minor_width", grid.minor_width);
            ini_get(section, "cell_size",   grid.cell_size);
            ini_get(section, "cell_div",    grid.cell_div);
            ini_get(section, "cell_count",  grid.cell_count);
        }

        if (ini.has("camera_controls"))
        {
            const auto& section = ini["camera_controls"];
            ini_get(section, "invert_x",           camera_controls.invert_x);
            ini_get(section, "invert_y",           camera_controls.invert_y);
            ini_get(section, "velocity_damp",      camera_controls.velocity_damp);
            ini_get(section, "velocity_max_delta", camera_controls.velocity_max_delta);
            ini_get(section, "sensitivity",        camera_controls.sensitivity);
        }

        if (ini.has("trs_tool"))
        {
            const auto& section = ini["trs_tool"];
            ini_get(section, "scale",          trs_tool.scale);
            ini_get(section, "show_translate", trs_tool.show_translate);
            ini_get(section, "show_rotate",    trs_tool.show_rotate);
        }

        if (ini.has("hud"))
        {
            const auto& section = ini["hud"];
            ini_get(section, "show", hud.show);
            ini_get(section, "x",    hud.x);
            ini_get(section, "y",    hud.y);
            ini_get(section, "z",    hud.z);
        }

        if (ini.has("hotbar"))
        {
            const auto& section = ini["hotbar"];
            ini_get(section, "show", hotbar.show);
            ini_get(section, "x",    hotbar.x);
            ini_get(section, "y",    hotbar.y);
            ini_get(section, "z",    hotbar.z);
        }
    }

    cxxopts::Options options("Editor", "Erhe Editor (C) 2022 Timo Suoranta");

    options.add_options()
        ("window-imgui-viewport",      "Enable hosting ImGui windows in window viewport",  cxxopts::value<bool>()->default_value(str( imgui.window_viewport)))
        ("no-window-imgui-viewport",   "Disable hosting ImGui windows in window viewport", cxxopts::value<bool>()->default_value(str(!imgui.window_viewport)))
        ("openxr",                     "Enable OpenXR HMD support",             cxxopts::value<bool>()->default_value(str( headset.openxr)))
        ("no-openxr",                  "Disable OpenXR HMD support",            cxxopts::value<bool>()->default_value(str(!headset.openxr)))
        ("parallel-initialization",    "Use parallel component initialization", cxxopts::value<bool>()->default_value(str( threading.parallel_initialization)))
        ("no-parallel-initialization", "Use serial component initialization",   cxxopts::value<bool>()->default_value(str(!threading.parallel_initialization)))
        ("reverse-depth",              "Enable reverse depth",                  cxxopts::value<bool>()->default_value(str( graphics.reverse_depth)))
        ("no-reverse-depth",           "Disable reverse depth",                 cxxopts::value<bool>()->default_value(str(!graphics.reverse_depth)));

    try
    {
        auto arguments = options.parse(argc, argv);

        imgui.window_viewport             = arguments["window-imgui-viewport"  ].as<bool>() && !arguments["no-window-imgui-viewport"  ].as<bool>();
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

