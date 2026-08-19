#pragma once

#include <string>

namespace editor {

// startup_commands_path selects the JSON startup script run after init (the
// scene-setup commands). Overridable from the command line (--commands); defaults
// to the standard config/editor/commands.json.
//
// startup_scene_path, when non-empty, loads a saved scene file (.glb / .gltf)
// on startup instead of running the procedural startup script.
// Overridable from the command line (--scene).
//
// no_startup_scene, when true, starts with an empty editor: neither the procedural
// default scene nor any scene is loaded. Overridable from the command line
// (--no-scene) and takes precedence over startup_scene_path.
//
// force_post_processing_off, when true, disables viewport post processing for
// the whole session regardless of editor_settings.json (--no-post-processing).
// The stored setting is NOT modified - remove the flag to get it back.
//
// fix_gltf_spot_lights, when true, fixes up spot lights of every glTF asset
// loaded in this session (--fix-spot-lights): full color value,
// intensity 1000, doubled outer cone angle, and the original outer cone
// angle as the inner cone angle. Source files are not modified.
void run_editor(
    const std::string& startup_commands_path     = "config/editor/commands.json",
    const std::string& startup_scene_path        = "",
    bool               no_startup_scene          = false,
    bool               force_post_processing_off = false,
    bool               fix_gltf_spot_lights      = false
);

}
