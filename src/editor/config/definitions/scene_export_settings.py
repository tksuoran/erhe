from erhe_codegen import *

# Per-scene settings of Export / Prepare (PC / Quest) and of the bakes they
# carry (doc/editor/prepared_assets.md "Export settings",
# doc/editor/world_space_bake.md). Field export_settings of Scene_settings
# (present or absent) and, as the editor's defaults that adding them copies,
# of Editor_settings_config.
struct("Scene_export_settings",
    version=4,
    short_desc="Export",
    long_desc="Settings Export and Prepare (PC / Quest) write the scene with.",
    fields=[
        # v1 held an Optional (null: the editor-global lod); the Scene_settings
        # JSON migration drops a null, so it reads as the Lod_config defaults.
        field("lod", StructRef("Lod_config"), added_in=1, short_desc="LOD", long_desc="LOD request Export and Prepare generate chains with for primitives that carry none."),
        field("bake_static_to_world_space", Bool, added_in=1, default="true", short_desc="Bake Static To World Space", long_desc="Export and Prepare write the static mesh nodes under the World Space node with world-space vertices and identity transforms, so LOD selection needs no node scale; Bake World-Space Meshes makes their World-space mesh items."),
        field("instancing", EnumRef("Bake_instancing"), added_in=1, default="Bake_instancing::duplicate_within_budget", short_desc="Instancing", long_desc="What the world-space bake does with a primitive several baked nodes share."),
        field("instancing_budget_mib", Int, added_in=1, default="256", short_desc="Instancing Budget (MiB)", long_desc="Vertex and index bytes duplicate_within_budget may add by duplicating shared primitives.", ui_min="0", ui_max="4096", hard_min="0"),
        field("pvs_region_min", Vec3,  added_in=1, default="0.0f, 0.0f, 0.0f", short_desc="PVS region min", long_desc="Minimum corner (world space) of the box the potentially visible set bake covers with its grid of cells (doc/editor/pvs.md D2). Both corners zero means unset: the bake covers the union of the world bounds of the entries it bakes."),
        field("pvs_region_max", Vec3,  added_in=1, default="0.0f, 0.0f, 0.0f", short_desc="PVS region max", long_desc="Maximum corner (world space) of the potentially visible set bake region; see pvs_region_min."),
        field("pvs_cell_size",  Float, added_in=1, default="1.0f", short_desc="PVS cell size", long_desc="Edge length in meters of one cell of the potentially visible set bake grid (doc/editor/pvs.md D2)."),
        field("texture_supercompression", EnumRef("Texture_supercompression"), added_in=2, default="Texture_supercompression::zstd", short_desc="Texture Supercompression", long_desc="How the raw block images transcoded for a target are written: Zstandard supercompressed, or stored as they upload."),
        field("raytrace_bvh", Bool, added_in=2, default="true", short_desc="Raytrace BVH", long_desc="A prepared asset carries the Raytrace BVH of its meshes; off leaves it out and a prepared run builds it in memory."),
        field("pvs_mode", EnumRef("Pvs_mode"), added_in=3, default="Pvs_mode::off", short_desc="PVS Mode", long_desc="What Export does about the potentially visible set: off writes none; reuse copies the PVS item the editor asset holds as it is, unsafe: nothing checks that it matches the exported geometry; rebake bakes a new PVS on the world-space form during every Export (needs the PVS region and Bake Static To World Space)."),
        field("texture_encoding", EnumRef("Texture_encoding"), added_in=4, default="Texture_encoding::target", short_desc="Texture Encoding", long_desc="What the Texture item of a PNG / JPEG image holds: target encodes it to the target's block format by way of a Basis Universal texture item of the editor asset; source keeps the image bytes as they are and the prepared run decodes them at load. A Basis Universal image is transcoded either way."),
    ],
)
