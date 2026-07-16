from erhe_codegen import *

struct("Operation_params",
    version=1,
    short_desc="Operation parameters",
    long_desc="Frozen snapshot of the Operations-window parameter sliders, captured when an operation is dragged into an inventory slot and replayed verbatim when the slot is invoked. An all-scalar superset of every operation's parameters; an operation reads only the field(s) it needs and ignores the rest.",
    developer=False,
    omit_defaults=True,
    fields=[
        field("kis_height",                   Float, added_in=1, default="0.0f",       short_desc="Kis apex height",            long_desc="Conway Kis: apex height along the face normal (0 = flat).",                          visible=True, developer=False),
        field("gyro_ratio",                   Float, added_in=1, default="1.0f / 3.0f", short_desc="Gyro ratio",                 long_desc="Conway Gyro: edge split position (1/3 = standard gyro).",                            visible=True, developer=False),
        field("bevel_ratio",                  Float, added_in=1, default="0.5f",       short_desc="Chamfer bevel ratio",        long_desc="Conway Chamfer: bevel ratio (how much each face shrinks).",                          visible=True, developer=False),
        field("truncate_ratio",               Float, added_in=1, default="1.0f / 3.0f", short_desc="Truncate ratio",             long_desc="Conway Truncate: cut depth (1/3 = standard, 0.5 = ambo).",                           visible=True, developer=False),
        field("remesh_target_vertex_count",   Int,   added_in=1, default="2000",       short_desc="Remesh target vertex count", long_desc="Isotropic / Anisotropic Remesh: target vertex count (Geogram nb_points).",            visible=True, developer=False),
        field("decimate_bins",                Int,   added_in=1, default="50",         short_desc="Decimate bins",              long_desc="Decimate: vertex-clustering grid resolution (higher = more detail kept).",           visible=True, developer=False),
        field("anisotropy",                   Float, added_in=1, default="0.04f",      short_desc="Anisotropy",                 long_desc="Anisotropic Remesh: anisotropy strength.",                                           visible=True, developer=False),
        field("smooth_iterations",            Int,   added_in=1, default="5",          short_desc="Smooth iterations",          long_desc="Smooth: number of Taubin lambda/mu cycles.",                                         visible=True, developer=False),
        field("smooth_strength",              Float, added_in=1, default="0.5f",       short_desc="Smooth strength",            long_desc="Smooth: per-step Laplacian strength (lambda).",                                      visible=True, developer=False),
        field("remesh_regenerate_attributes", Bool,  added_in=1, default="true",       short_desc="Remesh regenerate attrs",    long_desc="When on, the remesh family recomputes smooth normals and texture coordinates.",      visible=True, developer=False),
        field("atlas_texcoord_slot",          Int,   added_in=1, default="0",          short_desc="Atlas texcoord slot",        long_desc="Make Atlas: target corner texcoord channel (0 or 1) the generated UVs overwrite.",   visible=True, developer=False),
        field("atlas_hard_angles_deg",        Float, added_in=1, default="45.0f",      short_desc="Atlas hard angles (deg)",    long_desc="Make Atlas: dihedral angle above which an edge becomes a chart boundary.",           visible=True, developer=False),
        field("atlas_parameterizer",          Int,   added_in=1, default="3",          short_desc="Atlas parameterizer",        long_desc="Make Atlas: per-chart parameterizer index (0 Projection, 1 LSCM, 2 Spectral, 3 ABF).", visible=True, developer=False),
        field("atlas_packer",                 Int,   added_in=1, default="2",          short_desc="Atlas packer",               long_desc="Make Atlas: chart packing index (0 None, 1 Tetris, 2 XAtlas).",                      visible=True, developer=False),
        field("frame_field_sharp_angle_deg",  Float, added_in=1, default="45.0f",      short_desc="Frame field sharp angle",    long_desc="Gen Frame Field Tangents: dihedral angle above which an edge is a sharp feature.",   visible=True, developer=False),
        field("add_joint_avoidance",          Int,   added_in=1, default="0",          short_desc="Add Joint avoidance",        long_desc="Add / Flip Joint: obstacle-avoidance mode (0 joint pair, 1 whole world).",           visible=True, developer=False),
    ],
)

struct("Inventory_slot",
    version=4,
    short_desc="Inventory Slot",
    long_desc="A single slot in the inventory or hotbar grid",
    developer=False,
    omit_defaults=True,
    fields=[
        field(
            "tool_name",
            String,
            added_in=1,
            default='""',
            short_desc="Tool name",
            long_desc="Matches Tool::get_description(); empty string means vacant slot",
            visible=True,
            developer=False
        ),
        field(
            "brush_name",
            String,
            added_in=1,
            default='""',
            short_desc="Brush name (legacy)",
            long_desc="Legacy (pre-v4) brush name from content library; still read and migrated to a scene_local brush_asset key, no longer written",
            visible=True,
            developer=False
        ),
        field(
            "material_name",
            String,
            added_in=1,
            default='""',
            short_desc="Material name (legacy)",
            long_desc="Legacy (pre-v4) material name from content library; still read and migrated to a scene_local material_asset key, no longer written",
            visible=True,
            developer=False
        ),
        field(
            "brush_asset",
            StructRef("Asset_reference_data"),
            added_in=4,
            short_desc="Brush asset",
            long_desc="Asset reference for a brush slot; non-empty scope means brush slot. Supersedes brush_name.",
            visible=False,
            developer=False
        ),
        field(
            "material_asset",
            StructRef("Asset_reference_data"),
            added_in=4,
            short_desc="Material asset",
            long_desc="Asset reference for a material slot; non-empty scope means material slot. Supersedes material_name.",
            visible=False,
            developer=False
        ),
        field(
            "command_name",
            String,
            added_in=2,
            default='""',
            short_desc="Command name",
            long_desc="Operation command name (erhe::commands::Command::get_name()); non-empty means operation slot, resolved via Commands::find_command",
            visible=True,
            developer=False
        ),
        field(
            "operation_params",
            StructRef("Operation_params"),
            added_in=2,
            short_desc="Operation params",
            long_desc="Frozen parameter snapshot for an operation slot; ignored unless command_name is non-empty",
            visible=False,
            developer=False
        ),
        field(
            "graph_node_kind",
            String,
            added_in=3,
            default='""',
            short_desc="Graph node kind",
            long_desc="Graph editor kind tag for a graph-node slot (\"geometry_graph\" or \"texture_graph\"); paired with graph_node_type",
            visible=True,
            developer=False
        ),
        field(
            "graph_node_type",
            String,
            added_in=3,
            default='""',
            short_desc="Graph node type",
            long_desc="Node factory type name for a graph-node slot; non-empty means the slot spawns this node in its graph editor",
            visible=True,
            developer=False
        ),
        field(
            "graph_node_label",
            String,
            added_in=3,
            default='""',
            short_desc="Graph node label",
            long_desc="Palette display label shown on a graph-node slot button",
            visible=True,
            developer=False
        ),
    ],
)

struct("Inventory_config",
    version=1,
    short_desc="Inventory",
    long_desc="Inventory and hotbar layout configuration",
    developer=False,
    omit_defaults=True,
    fields=[
        field(
            "column_count",
            Int,
            added_in=1,
            default="9",
            short_desc="Grid columns",
            long_desc="Number of columns in the main inventory grid",
            visible=True,
            developer=False
        ),
        field(
            "row_count",
            Int,
            added_in=1,
            default="3",
            short_desc="Grid rows",
            long_desc="Number of rows in the main inventory grid",
            visible=True,
            developer=False
        ),
        field(
            "hotbar_slot_count",
            Int,
            added_in=1,
            default="10",
            short_desc="Hotbar slot count",
            long_desc="Number of slots in the hotbar row (Minecraft-style; keys 1..9,0 activate slots 1..10)",
            visible=True,
            developer=False
        ),
        field(
            "grid_slots",
            Vector(StructRef("Inventory_slot")),
            added_in=1,
            short_desc="Grid slots",
            long_desc="Main inventory grid slot contents",
            visible=False,
            developer=False
        ),
        field(
            "hotbar_slots",
            Vector(StructRef("Inventory_slot")),
            added_in=1,
            short_desc="Hotbar slots",
            long_desc="Hotbar row slot contents",
            visible=False,
            developer=False
        ),
    ],
)
