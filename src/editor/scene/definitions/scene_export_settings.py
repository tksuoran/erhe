from erhe_codegen import *

# Per-scene settings of Prepare (PC / Quest) and of the bakes it carries
# (doc/editor/world_space_bake.md). Field export_settings of Scene_settings.
struct("Scene_export_settings",
    version=1,
    short_desc="Export",
    long_desc="Settings Prepare (PC / Quest) writes the scene with.",
    fields=[
        field("lod", Optional(StructRef("Lod_config")), added_in=1, short_desc="LOD", long_desc="LOD request Prepare generates chains with for primitives that carry none. Null: the editor-global lod settings."),
        field("bake_static_to_world_space", Bool, added_in=1, default="true", short_desc="Bake Static To World Space", long_desc="Prepare writes the static mesh nodes with world-space vertices and identity transforms, so LOD selection needs no node scale."),
        field("instancing", EnumRef("Bake_instancing"), added_in=1, default="Bake_instancing::duplicate", short_desc="Instancing", long_desc="What the world-space bake does with a primitive several baked nodes share."),
        field("instancing_budget_mib", Int, added_in=1, default="256", short_desc="Instancing Budget (MiB)", long_desc="Vertex and index bytes duplicate_within_budget may add by duplicating shared primitives.", ui_min="0", ui_max="4096", hard_min="0"),
        field("pvs_region_min", Vec3,  added_in=1, default="0.0f, 0.0f, 0.0f", short_desc="PVS region min", long_desc="Minimum corner (world space) of the box the potentially visible set bake covers with its grid of cells (doc/editor/pvs.md D2). Both corners zero means unset: the bake covers the union of the world bounds of the entries it bakes."),
        field("pvs_region_max", Vec3,  added_in=1, default="0.0f, 0.0f, 0.0f", short_desc="PVS region max", long_desc="Maximum corner (world space) of the potentially visible set bake region; see pvs_region_min."),
        field("pvs_cell_size",  Float, added_in=1, default="1.0f", short_desc="PVS cell size", long_desc="Edge length in meters of one cell of the potentially visible set bake grid (doc/editor/pvs.md D2)."),
    ],
)
