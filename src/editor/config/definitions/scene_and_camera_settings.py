from erhe_codegen import *

# Per-Scene_view "Scene and Camera" selection, persisted as part of
# Scene_view_settings. Scene / camera / shadow-camera are stored by name
# (resolved against App_scenes and the scene's cameras at runtime; object
# ids are not stable across sessions). Empty name = no selection.
#
# shader_debug and renderer_choice are stored as the raw integer value of
# their (non-codegen) enums erhe::scene_renderer::Shader_debug and
# editor::Renderer_choice. These enums are append-stable; if reorder-safe
# string tokens are wanted later, they can be migrated to codegen enums
# (the config codegen already wires the erhe_scene_renderer definitions).
struct("Scene_and_camera_settings",
    version=1,
    short_desc="Scene and Camera Settings",
    long_desc="Per scene view persisted scene / camera selection",
    developer=False,
    fields=[
        field("scene_name",                      String, added_in=1, default='""', short_desc="Scene"),
        field("camera_name",                     String, added_in=1, default='""', short_desc="Camera"),
        field("shadow_fit_override_camera_name", String, added_in=1, default='""', short_desc="Shadow Camera Override"),
        field("shader_debug",                    Int,    added_in=1, default="0",  short_desc="Shader Debug"),
        field("renderer_choice",                 Int,    added_in=1, default="0",  short_desc="Renderer"),
    ],
)
