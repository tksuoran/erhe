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
//
// no_startup_scene, when true, starts with an empty editor: neither the procedural
// default scene nor any scene is loaded. Overridable from the command line
// (--no-scene) and takes precedence over startup_scene_path.
void run_editor(
    const std::string& startup_commands_path = "config/editor/commands.json",
    const std::string& startup_scene_path    = "",
    bool               no_startup_scene      = false
);

}
