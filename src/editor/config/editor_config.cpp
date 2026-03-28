#include "config/editor_config.hpp"

#include "config/generated/camera_controls_config_serialization.hpp"
#include "config/generated/developer_config_serialization.hpp"
#include "config/generated/grid_config_serialization.hpp"
#include "config/generated/headset_config_serialization.hpp"
#include "config/generated/hotbar_config_serialization.hpp"
#include "config/generated/hud_config_serialization.hpp"
#include "config/generated/inventory_slot_serialization.hpp"
#include "config/generated/inventory_config_serialization.hpp"
#include "config/generated/id_renderer_config_serialization.hpp"
#include "config/generated/mesh_memory_config_serialization.hpp"
#include "config/generated/network_config_serialization.hpp"
#include "config/generated/physics_config_serialization.hpp"
#include "config/generated/renderer_config_serialization.hpp"
#include "config/generated/scene_config_serialization.hpp"
#include "config/generated/shader_monitor_config_serialization.hpp"
#include "config/generated/text_renderer_config_serialization.hpp"
#include "config/generated/threading_config_serialization.hpp"
#include "config/generated/thumbnails_config_serialization.hpp"
#include "config/generated/transform_tool_config_serialization.hpp"
#include "config/generated/viewport_config_data_serialization.hpp"
#include "config/generated/window_config_serialization.hpp"
#include "erhe_graphics/generated/graphics_config_serialization.hpp"

#include "erhe_file/file.hpp"

#include <simdjson.h>

#include <cstdio>

namespace {

template <typename T>
void deserialize_section(simdjson::ondemand::object& root, std::string_view key, T& out)
{
    simdjson::ondemand::value val;
    if (!root[key].get(val)) {
        simdjson::ondemand::object obj;
        if (!val.get_object().get(obj)) {
            deserialize(obj, out);
        }
    }
}

} // anonymous namespace

auto load_editor_config(std::string_view path) -> Editor_config
{
    Editor_config config{};

    std::optional<std::string> contents = erhe::file::read("load_editor_config()", path);
    if (!contents.has_value()) {
        // No config file - use defaults and write one
        save_editor_config(config, path);
        return config;
    }

    simdjson::ondemand::parser parser;
    simdjson::padded_string padded{contents.value()};
    simdjson::ondemand::document doc;
    auto error = parser.iterate(padded).get(doc);
    if (error) {
        std::fprintf(stderr, "erhe config: JSON parse error: %s\n", simdjson::error_message(error));
        return config;
    }

    simdjson::ondemand::object root;
    error = doc.get_object().get(root);
    if (error) {
        std::fprintf(stderr, "erhe config: expected JSON object at root\n");
        return config;
    }

    deserialize_section(root, "camera_controls", config.camera_controls);
    deserialize_section(root, "developer",       config.developer);
    deserialize_section(root, "graphics",        config.graphics);
    deserialize_section(root, "grid",            config.grid);
    deserialize_section(root, "headset",         config.headset);
    deserialize_section(root, "hotbar",          config.hotbar);
    deserialize_section(root, "hud",             config.hud);
    deserialize_section(root, "inventory",       config.inventory);
    deserialize_section(root, "id_renderer",     config.id_renderer);
    deserialize_section(root, "mesh_memory",     config.mesh_memory);
    deserialize_section(root, "network",         config.network);
    deserialize_section(root, "physics",         config.physics);
    deserialize_section(root, "renderer",        config.renderer);
    deserialize_section(root, "scene",           config.scene);
    deserialize_section(root, "shader_monitor",  config.shader_monitor);
    deserialize_section(root, "text_renderer",   config.text_renderer);
    deserialize_section(root, "threading",       config.threading);
    deserialize_section(root, "thumbnails",      config.thumbnails);
    deserialize_section(root, "transform_tool",  config.transform_tool);
    deserialize_section(root, "viewport",        config.viewport);
    deserialize_section(root, "window",          config.window);

    return config;
}

void save_editor_config(const Editor_config& config, std::string_view path)
{
    std::string out;
    out += "{\n";
    out += "    \"camera_controls\": "; out += serialize(config.camera_controls, 1); out += ",\n";
    out += "    \"developer\": ";       out += serialize(config.developer,       1); out += ",\n";
    out += "    \"graphics\": ";        out += serialize(config.graphics,        1); out += ",\n";
    out += "    \"grid\": ";            out += serialize(config.grid,            1); out += ",\n";
    out += "    \"headset\": ";         out += serialize(config.headset,         1); out += ",\n";
    out += "    \"hotbar\": ";          out += serialize(config.hotbar,          1); out += ",\n";
    out += "    \"hud\": ";             out += serialize(config.hud,             1); out += ",\n";
    out += "    \"inventory\": ";        out += serialize(config.inventory,       1); out += ",\n";
    out += "    \"id_renderer\": ";     out += serialize(config.id_renderer,     1); out += ",\n";
    out += "    \"mesh_memory\": ";     out += serialize(config.mesh_memory,     1); out += ",\n";
    out += "    \"network\": ";         out += serialize(config.network,         1); out += ",\n";
    out += "    \"physics\": ";         out += serialize(config.physics,         1); out += ",\n";
    out += "    \"renderer\": ";        out += serialize(config.renderer,        1); out += ",\n";
    out += "    \"scene\": ";           out += serialize(config.scene,           1); out += ",\n";
    out += "    \"shader_monitor\": ";  out += serialize(config.shader_monitor,  1); out += ",\n";
    out += "    \"text_renderer\": ";   out += serialize(config.text_renderer,   1); out += ",\n";
    out += "    \"threading\": ";       out += serialize(config.threading,       1); out += ",\n";
    out += "    \"thumbnails\": ";      out += serialize(config.thumbnails,      1); out += ",\n";
    out += "    \"transform_tool\": ";  out += serialize(config.transform_tool,  1); out += ",\n";
    out += "    \"viewport\": ";        out += serialize(config.viewport,        1); out += ",\n";
    out += "    \"window\": ";          out += serialize(config.window,          1); out += "\n";
    out += "}\n";

    erhe::file::write_file(path, out);
}
