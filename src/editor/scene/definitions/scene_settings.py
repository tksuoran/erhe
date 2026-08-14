from erhe_codegen import *

# Per-scene overrides of a subset of editor-global settings (issue #239).
#
# Each field is optional. A disengaged optional (null in JSON, the default) means
# "use the editor-global default for this setting"; an engaged optional overrides
# the editor-global value for this scene only. The effective value is resolved by
# the editor helpers in scene/scene_settings_resolve.{hpp,cpp}:
#   effective = scene override if engaged, else editor_settings-><field>.
#
# The referenced config structs (Sky_config, Grid_config, ...) are defined in the
# editor config codegen unit (src/editor/config/definitions); this struct lives in
# the scene codegen unit and references them across units via the scene
# erhe_codegen_generate() EXTRA_DEFINITIONS_DIRS entry (config/generated/).
struct("Scene_settings",
    version=1,
    short_desc="Per-scene setting overrides",
    long_desc="Each field left null means: use the editor-global default.",
    fields=[
        field("scene_id", String, added_in=1, default='""', short_desc="Persistent scene identity", long_desc="Unique persistent scene id (creation timestamp + random suffix), generated lazily by Scene_root::get_scene_id for scenes that lack one and persisted with the scene. Identifies side data (e.g. the lightmap tile set manifest): side data stamped with a different scene_id is rejected. Not intended for hand editing."),
        field("sky",                Optional(StructRef("Sky_config")),                added_in=1, short_desc="Sky",                long_desc="Override the editor-global sky settings for this scene."),
        field("grid",               Optional(StructRef("Grid_config")),               added_in=1, short_desc="Grid",               long_desc="Override the editor-global grid settings for this scene."),
        field("physics",            Optional(StructRef("Physics_config")),            added_in=1, short_desc="Physics",            long_desc="Override the editor-global physics settings for this scene."),
        field("shadow_frustum_fit", Optional(StructRef("Shadow_frustum_fit_config")), added_in=1, short_desc="Shadow Frustum Fit", long_desc="Override the editor-global shadow frustum fit settings for this scene."),
        field("camera_controls",    Optional(StructRef("Camera_controls_config")),    added_in=1, short_desc="Camera Controls",    long_desc="Override the editor-global camera control settings for this scene."),
        field("clear_color",        Optional(Vec4),                                   added_in=1, short_desc="Clear Color",         long_desc="Override the editor-global viewport clear color for this scene."),
        field("post_processing",    Optional(Bool),                                   added_in=1, short_desc="Post Processing",     long_desc="Override the editor-global post-processing enable for this scene."),
        field("lightmap_tile_overrides", Vector(StructRef("Lightmap_tile_override")), added_in=1, short_desc="Lightmap tile overrides", long_desc="Lightmap quadtree grid leaf overrides, each {level, ix, iz} with level != 0: subdivided (level > 0) or merged (level < 0) tiles replacing the level-0 cells they cover. Cell size at level L is lightmap.cell_size_m * 2^-L; cells are anchored at multiples of their size from the world origin. Managed by the Lightmap window / MCP subdivide/merge tools; not intended for hand editing."),
    ],
)
