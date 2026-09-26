#include "input_bindings_store.hpp"
#include "ai_driver.hpp"
#include "editor_log.hpp"

#include "config/generated/input_bindings_config.hpp"
#include "config/generated/input_bindings_config_serialization.hpp"
#include "erhe_codegen/config_io.hpp"
#include "erhe_commands/binding_desc.hpp"
#include "erhe_commands/commands.hpp"

#include <filesystem>
#include <optional>
#include <vector>

namespace editor {

Input_bindings_store::Input_bindings_store(erhe::commands::Commands& commands)
    : m_commands{commands}
    , m_persist {!is_ai_driver()}
{
}

void Input_bindings_store::load()
{
    if (!m_persist) {
        log_startup->info("AI-driven run: {} is neither read nor written, default input bindings apply", c_input_bindings_file_path);
        return;
    }
    std::error_code ec{};
    if (!std::filesystem::exists(std::filesystem::path{c_input_bindings_file_path}, ec)) {
        return;
    }

    const Input_bindings_config config = erhe::codegen::load_config<Input_bindings_config>(c_input_bindings_file_path);

    std::vector<erhe::commands::Binding_override> overrides;
    overrides.reserve(config.overrides.size());
    for (const Input_binding_override& entry : config.overrides) {
        erhe::commands::Binding_override& binding_override = overrides.emplace_back();
        binding_override.command_name = entry.command;
        for (const std::string& text : entry.bindings) {
            const std::optional<erhe::commands::Binding_desc> desc = erhe::commands::Binding_desc::parse(text);
            if (!desc.has_value()) {
                log_startup->warn("{}: command '{}': ignoring unparseable binding '{}'", c_input_bindings_file_path, entry.command, text);
                continue;
            }
            binding_override.bindings.push_back(desc.value());
        }
    }
    m_commands.apply_binding_overrides(overrides);
    log_startup->info("Input binding overrides loaded from {} ({} commands)", c_input_bindings_file_path, overrides.size());
}

void Input_bindings_store::save()
{
    if (!m_persist) {
        return;
    }

    std::vector<erhe::commands::Binding_override> overrides;
    m_commands.get_binding_overrides(overrides);

    Input_bindings_config config{};
    config.overrides.reserve(overrides.size());
    for (const erhe::commands::Binding_override& binding_override : overrides) {
        Input_binding_override& entry = config.overrides.emplace_back();
        entry.command = binding_override.command_name;
        for (const erhe::commands::Binding_desc& desc : binding_override.bindings) {
            entry.bindings.push_back(desc.to_string());
        }
    }
    if (!erhe::codegen::save_config(config, c_input_bindings_file_path)) {
        log_startup->warn("Could not write {}", c_input_bindings_file_path);
    }
}

} // namespace editor
