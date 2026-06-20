from erhe_codegen import *

# Per-Scene_view persisted state of the editor's Debug Visualizations window
# (tools/debug_visualizations.cpp): the enable toggles and per-view modes.
# The appearance of these visualizations (colors, line widths, label geometry)
# is editor-global and lives in Debug_visualizations_style instead. Defaults
# match the C++ member defaults. Not to be confused with
# Debug_visualizations_config, the per-viewport light/camera visualization
# modes in Viewport_config_data.
struct("Debug_visualizations_settings",
    version=8,
    short_desc="Debug Visualizations",
    long_desc="Settings for the Debug Visualizations window",
    developer=False,
    fields=[
        # Per scene view:
        field("lights",                      EnumRef("Visualization_mode"), added_in=1, default="Visualization_mode::off", short_desc="Lights"),
        field("cameras",                     EnumRef("Visualization_mode"), added_in=1, default="Visualization_mode::off", short_desc="Cameras"),
        field("layouts",                     EnumRef("Visualization_mode"), added_in=1, default="Visualization_mode::off", short_desc="Layouts"),
        field("skins",                       EnumRef("Visualization_mode"), added_in=1, default="Visualization_mode::off", short_desc="Skins"),
        field("node_axises",                 EnumRef("Visualization_mode"), added_in=1, default="Visualization_mode::off", short_desc="Node Axises"),
        field("physics",                     EnumRef("Visualization_mode"), added_in=1, default="Visualization_mode::off", short_desc="Physics"),
        field("raytrace",                    EnumRef("Visualization_mode"), added_in=1, default="Visualization_mode::off", short_desc="Raytrace"),
        field("vertex_labels",               EnumRef("Visualization_mode"), added_in=1, default="Visualization_mode::off", short_desc="Show Vertices"),
        field("facet_labels",                EnumRef("Visualization_mode"), added_in=1, default="Visualization_mode::off", short_desc="Show Facets"),
        field("edge_labels",                 EnumRef("Visualization_mode"), added_in=1, default="Visualization_mode::off", short_desc="Show Edges"),
        field("corner_labels",               EnumRef("Visualization_mode"), added_in=1, default="Visualization_mode::off", short_desc="Show Corners"),
        field("world_axes",                        Bool,  added_in=1, default="false", short_desc="World Axes"),
        field("shadow_debug",                      Bool,  added_in=1, default="false", short_desc="Shadow Debug"),
        field("shadow_fit_debug",                  Bool,  added_in=2, default="false", short_desc="Shadow Fit",           long_desc="Master toggle for the shadow frustum fit visualizations"),
        field("shadow_fit_casters",                Bool,  added_in=2, default="false", short_desc="Fit Casters",          long_desc="Per-caster world AABBs, colored by whether the caster affects the selected light's shadow (intersects F_shadow) or is culled"),
        field("shadow_fit_caster_hull",            Bool,  added_in=2, default="false", short_desc="Fit Caster Hull",      long_desc="Convex hull around the caster bounds, before clipping to the shadow caster volume"),
        field("shadow_fit_view_frustum",           Bool,  added_in=5, default="false", short_desc="Fit View Frustum",     long_desc="Main camera view frustum, truncated to the shadow range - the region shadow receivers are filtered against"),
        field("shadow_fit_receivers",              Bool,  added_in=2, default="false", short_desc="Fit Receivers",        long_desc="Receiver world AABBs, colored by whether they pass the view frustum filter (and so contribute to the receiver-aware caster cull) or fail it"),
        field("shadow_fit_receiver_hull",           Bool, added_in=6, default="false", short_desc="Fit Receiver Hull Clipped",   long_desc="Convex hull of the receivers that intersect the view frustum, clipped to the view frustum - the body extruded toward the light into the caster cull volume (receiver analogue of the caster hull)"),
        field("shadow_fit_receiver_hull_unclipped", Bool, added_in=8, default="false", short_desc="Fit Receiver Hull Unclipped", long_desc="Convex hull of the receivers that intersect the view frustum, before the clip to the view frustum"),
        field("shadow_fit_volume_planes",          Bool,  added_in=2, default="false", short_desc="Fit Volume Planes",    long_desc="Shadow caster volume (F_shadow) planes the caster AABBs are tested against, drawn as bounded frustum faces"),
        field("shadow_fit_far_plane_hull",         Bool,  added_in=7, default="false", short_desc="Fit Far Plane Hull",   long_desc="Receiver silhouette (the 2D hull of the clipped receiver hull projected along the light onto the flat far cap) drawn as a closed loop in world space - the polygon the receiver caster-cull volume side planes are swept from; drawn directly, independent of the volume-planes reconstruction"),
        field("shadow_fit_points",                 Bool,  added_in=2, default="false", short_desc="Fit Points",           long_desc="Point set the light space box was fitted to (clipped caster hull or view frustum corners)"),
        field("shadow_fit_light_plane_hull",       Bool,  added_in=2, default="false", short_desc="Fit Light Plane Hull", long_desc="2D hull on the light plane with the rotating calipers OBB and its supporting edge"),
        field("shadow_fit_box_fit",                Bool,  added_in=2, default="false", short_desc="Fit Box: Points",      long_desc="Light space box after fitting to the point set"),
        field("shadow_fit_box_frustum",            Bool,  added_in=2, default="false", short_desc="Fit Box: Frustum",     long_desc="Light space box after the view frustum / near constraints"),
        field("shadow_fit_box_cap",                Bool,  added_in=2, default="false", short_desc="Fit Box: Range Cap",   long_desc="Light space box after the shadow range cap"),
        field("shadow_fit_box_final",              Bool,  added_in=2, default="false", short_desc="Fit Box: Final",       long_desc="Final light space box after quantize and texel snap; matches the light projection"),

        field("selection",                         Bool,  added_in=1, default="true",   short_desc="Selection"),
        field("selection_bounding_points",         Bool,  added_in=1, default="false",  short_desc="Bounding Points"),
        field("selection_box",                     Bool,  added_in=1, default="false",  short_desc="Selection Box"),
        field("selection_sphere",                  Bool,  added_in=1, default="false",  short_desc="Selection Sphere"),
        field("selection_parts",                   Bool,  added_in=1, default="false",  short_desc="Selection Parts"),
        field("selection_convex_hull",             Bool,  added_in=1, default="false",  short_desc="Selection Convex Hull"),
        field("selection_convex_hull_projected",   Bool,  added_in=1, default="false",  short_desc="Selection Projected Hull"),
        field("debug_convex_hull",                 Bool,  added_in=1, default="false",  short_desc="Debug Convex Hull"),
        field("convex_hull_edge",                  Int,   added_in=1, default="0",      short_desc="Convex Hull Edge"),
        field("gap",                               Float, added_in=1, default="0.003f", short_desc="Gap"),
        field("tool_hide",                         Bool,  added_in=1, default="false",  short_desc="Tool Hide"),
        field("frustum_box",                       Bool,  added_in=1, default="false",  short_desc="Frustum Box"),
        field("frustum_planes",                    Bool,  added_in=1, default="false",  short_desc="Frustum Planes"),
        field("camera_cull_test",                  Bool,  added_in=1, default="false",  short_desc="Camera Cull Test"),

        field("max_labels",                        Int,   added_in=1, default="400",    short_desc="Max Labels"),
        field("vertex_positions",                  Bool,  added_in=1, default="false",  short_desc="Vertex Positions"),
    ],
)
