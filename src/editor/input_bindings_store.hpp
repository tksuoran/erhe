#pragma once

namespace erhe::commands { class Commands; }

namespace editor {

static const char* const c_input_bindings_file_path = "config/editor/input_bindings.json";

// Owns the persistence of the user's input binding overrides
// (erhe::commands::Commands::apply_binding_overrides / get_binding_overrides)
// in config/editor/input_bindings.json. The file lists only the commands the
// user has edited and does not exist until the first edit.
//
// An AI-driven run (is_ai_driver()) neither reads nor writes the file, so it
// runs on the default bindings the code declares.
//
// Change sites (the Input Bindings window, the MCP binding tools) call save()
// right after editing Commands; nothing polls for changes.
class Input_bindings_store
{
public:
    explicit Input_bindings_store(erhe::commands::Commands& commands);

    // Reads the file and applies it to Commands. Called once after every
    // part has registered its commands and declared its default bindings.
    void load();

    // Writes the current overrides of Commands to the file.
    void save();

private:
    erhe::commands::Commands& m_commands;
    bool                      m_persist{true};
};

} // namespace editor
