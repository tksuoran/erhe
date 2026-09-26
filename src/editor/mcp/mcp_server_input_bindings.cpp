// Mcp_server input binding tools: list_input_bindings, set_command_bindings,
// reset_command_bindings (doc/editor/input_bindings.md).

#include "mcp/mcp_server.hpp"
#include "mcp/mcp_server_shared.hpp"

#include "app_context.hpp"
#include "input_bindings_store.hpp"

#include "erhe_commands/binding_desc.hpp"
#include "erhe_commands/command.hpp"
#include "erhe_commands/commands.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace editor {

using namespace mcp_server_detail;

namespace {

[[nodiscard]] auto bindings_to_json(const std::vector<erhe::commands::Binding_desc>& bindings) -> nlohmann::json
{
    nlohmann::json result = nlohmann::json::array();
    for (const erhe::commands::Binding_desc& desc : bindings) {
        result.push_back(desc.to_string());
    }
    return result;
}

} // anonymous namespace

auto Mcp_server::query_input_bindings(const nlohmann::json& args) -> std::string
{
    erhe::commands::Commands& commands = *m_context.commands;
    const std::string filter = args.value("filter", std::string{});

    std::vector<erhe::commands::Binding_desc> defaults;
    std::vector<erhe::commands::Binding_desc> effective;
    nlohmann::json list = nlohmann::json::array();
    for (erhe::commands::Command* command : commands.get_commands()) {
        const erhe::commands::Input_kind input_kind = commands.get_input_kind(*command);
        if (input_kind == erhe::commands::Input_kind::internal) {
            continue;
        }
        const std::string name{command->get_name()};
        if (!filter.empty() && (name.find(filter) == std::string::npos)) {
            continue;
        }
        commands.get_default_bindings  (*command, defaults);
        commands.get_effective_bindings(*command, effective);
        nlohmann::json conflicts = nlohmann::json::array();
        for (const erhe::commands::Binding_conflict& conflict : commands.get_binding_conflicts()) {
            if (conflict.command == command) {
                conflicts.push_back({
                    {"binding",       conflict.binding.to_string()},
                    {"other_command", conflict.other_command->get_name()}
                });
            }
        }
        list.push_back({
            {"command",    name},
            {"input_kind", erhe::commands::c_str(input_kind)},
            {"defaults",   bindings_to_json(defaults)},
            {"bindings",   bindings_to_json(effective)},
            {"modified",   commands.has_binding_override(*command)},
            {"conflicts",  conflicts}
        });
    }
    return make_json_content({{"commands", list}}).dump();
}

auto Mcp_server::action_set_command_bindings(const nlohmann::json& args) -> std::string
{
    erhe::commands::Commands& commands = *m_context.commands;
    if (!args.contains("command") || !args.at("command").is_string()) {
        return make_error_content("command is required (a command name from list_input_bindings)");
    }
    if (!args.contains("bindings") || !args.at("bindings").is_array()) {
        return make_error_content("bindings is required (an array of binding strings such as \"key:ctrl+x\"; empty unbinds)");
    }
    const std::string name = args.at("command").get<std::string>();
    erhe::commands::Command* const command = commands.find_command(name);
    if (command == nullptr) {
        return make_error_content("unknown command '" + name + "'");
    }

    std::vector<erhe::commands::Binding_desc> bindings;
    for (const nlohmann::json& entry : args.at("bindings")) {
        if (!entry.is_string()) {
            return make_error_content("every binding must be a string");
        }
        const std::string text = entry.get<std::string>();
        const std::optional<erhe::commands::Binding_desc> desc = erhe::commands::Binding_desc::parse(text);
        if (!desc.has_value()) {
            return make_error_content("'" + text + "' is not a valid binding");
        }
        bindings.push_back(desc.value());
    }

    std::string error;
    if (!commands.set_binding_override(*command, bindings, &error)) {
        return make_error_content(error);
    }
    m_context.input_bindings_store->save();

    std::vector<erhe::commands::Binding_desc> effective;
    commands.get_effective_bindings(*command, effective);
    return make_json_content({
        {"command",  name},
        {"bindings", bindings_to_json(effective)}
    }).dump();
}

auto Mcp_server::action_reset_command_bindings(const nlohmann::json& args) -> std::string
{
    erhe::commands::Commands& commands = *m_context.commands;
    if (args.value("all", false)) {
        commands.clear_all_binding_overrides();
        m_context.input_bindings_store->save();
        return make_json_content({{"reset", "all"}}).dump();
    }
    if (!args.contains("command") || !args.at("command").is_string()) {
        return make_error_content("command (a command name) or all: true is required");
    }
    const std::string name = args.at("command").get<std::string>();
    erhe::commands::Command* const command = commands.find_command(name);
    if (command == nullptr) {
        return make_error_content("unknown command '" + name + "'");
    }
    commands.clear_binding_override(*command);
    m_context.input_bindings_store->save();

    std::vector<erhe::commands::Binding_desc> effective;
    commands.get_effective_bindings(*command, effective);
    return make_json_content({
        {"command",  name},
        {"bindings", bindings_to_json(effective)}
    }).dump();
}

} // namespace editor
