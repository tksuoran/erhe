// Mcp_server tool list.
//
// The static half -- name, description and input schema for every built-in
// tool -- is data, not code: it lives in config/editor/mcp_tools.json and is
// loaded here. It used to be ~1600 lines of nlohmann brace-init in this file,
// which made this the single most expensive translation unit in the build
// (43s exclusive, 12% of total build wall-time responsibility): every nested
// {...} instantiates initializer_list<json> machinery and constructs a
// temporary json per node. Nothing ever inspected the result -- handle_tools_list
// copies all three fields verbatim into the response, and the handlers parse
// their own arguments -- so none of that instantiation bought anything.
//
// The JSON file was generated from the previous hand-written code (dumped from
// a running editor) rather than transcribed, so it matches it exactly.
//
// The dynamic half stays here: one tool per registered editor command, which
// is only knowable at runtime. That is also why refresh_tool_list() still runs
// per tools/list request.

#include "mcp/mcp_server.hpp"
#include "mcp/mcp_server_shared.hpp"

#include "editor_log.hpp"

#include "erhe_commands/command.hpp"
#include "erhe_commands/commands.hpp"
#include "erhe_file/file.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <iterator>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace editor {

using namespace mcp_server_detail;

namespace {

constexpr const char* c_tool_list_path = "config/editor/mcp_tools.json";

#if !defined(ERHE_VOXEL_LIBRARY_OPENVDB)
// The SDF geometry-graph node types exist only in an OpenVDB build. The JSON
// carries the full list because it was dumped from an OpenVDB build, so a
// build without OpenVDB takes them back out -- the same conditional the
// hand-written list expressed with #if defined(ERHE_VOXEL_LIBRARY_OPENVDB)
// around the geometry_node_types push_backs.
void remove_sdf_geometry_node_types(std::vector<Mcp_tool_info>& tools)
{
    static constexpr std::string_view c_sdf_node_types[] = {
        "sdf_sphere", "sdf_capsule", "voxelize", "sdf_mesh",
        "sdf_boolean", "sdf_offset", "sdf_smooth"
    };
    for (Mcp_tool_info& tool : tools) {
        if (tool.name != "geometry_graph_add_node") {
            continue;
        }
        const auto properties = tool.input_schema.find("properties");
        if (properties == tool.input_schema.end()) {
            break;
        }
        const auto type_property = properties->find("type");
        if (type_property == properties->end()) {
            break;
        }
        const auto enum_values = type_property->find("enum");
        if ((enum_values == type_property->end()) || !enum_values->is_array()) {
            break;
        }
        nlohmann::json kept = nlohmann::json::array();
        for (const nlohmann::json& value : *enum_values) {
            const std::string name = value.get<std::string>();
            if (std::find(std::begin(c_sdf_node_types), std::end(c_sdf_node_types), name) == std::end(c_sdf_node_types)) {
                kept.push_back(value);
            }
        }
        *enum_values = kept;
        break;
    }
}
#endif

// Parsed once per process: the file is a static asset and refresh_tool_list()
// runs on every tools/list request.
[[nodiscard]] auto load_static_tool_list() -> std::vector<Mcp_tool_info>
{
    std::vector<Mcp_tool_info> tools;

    const std::optional<std::string> contents = erhe::file::read("MCP tool list", c_tool_list_path);
    if (!contents.has_value()) {
        log_mcp->error("MCP server: cannot read {} - no built-in tools will be advertised", c_tool_list_path);
        return tools;
    }

    const nlohmann::json parsed = nlohmann::json::parse(contents.value(), nullptr, false);
    if (parsed.is_discarded() || !parsed.is_array()) {
        log_mcp->error("MCP server: {} is not a JSON array of tools - no built-in tools will be advertised", c_tool_list_path);
        return tools;
    }

    tools.reserve(parsed.size());
    for (const nlohmann::json& entry : parsed) {
        const auto name        = entry.find("name");
        const auto description = entry.find("description");
        const auto input_schema= entry.find("inputSchema");
        if ((name == entry.end()) || !name->is_string() ||
            (description == entry.end()) || !description->is_string() ||
            (input_schema == entry.end())) {
            log_mcp->error("MCP server: {} has an entry without name/description/inputSchema - skipped", c_tool_list_path);
            continue;
        }
        tools.push_back({name->get<std::string>(), description->get<std::string>(), *input_schema});
    }

#if !defined(ERHE_VOXEL_LIBRARY_OPENVDB)
    remove_sdf_geometry_node_types(tools);
#endif

    log_mcp->info("MCP server: loaded {} built-in tools from {}", tools.size(), c_tool_list_path);
    return tools;
}

} // anonymous namespace

void Mcp_server::validate_tool_list_against_dispatch()
{
    std::lock_guard<std::mutex> lock{m_tools_mutex};
    const std::span<const Tool_dispatch_entry> dispatch_table = get_dispatch_table();

    for (const Tool_dispatch_entry& entry : dispatch_table) {
        const auto i = std::find_if(
            m_tool_infos.begin(), m_tool_infos.end(),
            [&entry](const Mcp_tool_info& tool) { return tool.name == entry.name; }
        );
        if (i == m_tool_infos.end()) {
            log_mcp->error(
                "MCP server: tool '{}' has a handler but is missing from {} - it cannot be called",
                entry.name, c_tool_list_path
            );
        }
    }

    for (const Mcp_tool_info& tool : m_tool_infos) {
        const auto i = std::find_if(
            dispatch_table.begin(), dispatch_table.end(),
            [&tool](const Tool_dispatch_entry& entry) { return tool.name == entry.name; }
        );
        if (i == dispatch_table.end()) {
            // Editor commands are dispatched by execute_command(), not by the
            // table, so only complain about names that are no command either.
            const auto& registered_commands = m_commands.get_commands();
            const auto command = std::find_if(
                registered_commands.begin(), registered_commands.end(),
                [&tool](const erhe::commands::Command* c) {
                    const char* name = (c != nullptr) ? c->get_name() : nullptr;
                    return (name != nullptr) && (tool.name == name);
                }
            );
            if (command == registered_commands.end()) {
                log_mcp->error(
                    "MCP server: {} advertises '{}' but nothing handles it",
                    c_tool_list_path, tool.name
                );
            }
        }
    }
}

void Mcp_server::refresh_tool_list()
{
    std::lock_guard<std::mutex> lock{m_tools_mutex};
    m_tool_infos.clear();

    static const std::vector<Mcp_tool_info> s_static_tools = load_static_tool_list();
    m_tool_infos = s_static_tools;

    // Editor commands: one tool per registered command, resolved at runtime.
    const auto& registered_commands = m_commands.get_commands();
    for (const auto* command : registered_commands) {
        const char* name = command->get_name();
        if (name == nullptr || name[0] == '\0') {
            continue;
        }
        m_tool_infos.push_back({name, std::string{"Editor command: "} + name, schema_no_args()});
    }
}

} // namespace editor
