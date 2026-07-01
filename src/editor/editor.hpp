#pragma once

#include <string>

namespace editor {

// startup_commands_path selects the JSON startup script run after init (the
// scene-setup commands). Overridable from the command line (--commands); defaults
// to the standard config/editor/commands.json.
//
// startup_scene_path, when non-empty, loads a saved scene directory bundle
// (.erhescene) on startup instead of running the procedural startup script.
// Overridable from the command line (--scene).
void run_editor(
    const std::string& startup_commands_path = "config/editor/commands.json",
    const std::string& startup_scene_path    = ""
);

}
