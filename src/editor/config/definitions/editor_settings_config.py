from erhe_codegen import *

struct("Editor_settings_config",
    version=10,
    short_desc="Editor settings",
    long_desc="Runtime-editable settings saved to editor_settings.json.",
    developer=False,
    fields=[
        field("camera_controls",      StructRef("Camera_controls_config"), added_in=1),
        # Editor-global appearance of the debug visualizations (colors, line
        # widths, label geometry), shared by all scene views. The per-view
        # enable toggles / visualization modes (Debug_visualizations_settings)
        # live ONLY per scene view, in Scene_view_settings below - there is no
        # editor-global slot for them. New views fall back to the
        # Debug_visualizations_settings struct defaults.
        field("debug_visualizations_style", StructRef("Debug_visualizations_style"), added_in=1),
        # Editor-global render-style appearance (colors, line / point widths,
        # point size, per-primitive color sources), shared by all scene views.
        # Two sets: render_style_appearance for the Default style (non-selected
        # meshes) and selected_render_style_appearance for the Selection style.
        # The per-view render styles (Viewport_config.render_style_*) keep only
        # the visibility toggles (Render_style_data).
        field("render_style_appearance",          StructRef("Render_style_appearance"), added_in=1),
        field("selected_render_style_appearance", StructRef("Render_style_appearance"), added_in=1),
        # Editor-global selection outline appearance, shared by all scene views.
        field("selection_outline",                StructRef("Selection_outline_style"), added_in=1),
        # Editor-global mesh-component (vertex / edge / face) selection style,
        # shared by all scene views.
        field("mesh_component_style",             StructRef("Mesh_component_style"),    added_in=1),
        # Editor-global viewport clear color, shared by all scene views (moved
        # out of the per-view Visual Style popup). gizmo_scale moved on to
        # Transform_tool_config in v17.
        field("clear_color",                      Vec4,  added_in=1, default="0.0f, 0.0f, 0.0f, 0.4f", short_desc="Clear Color", long_desc="Viewport background clear color."),
        # Editor-global content edge-line (wide-line) method and bias tuning.
        field("content_edge_lines",               StructRef("Content_edge_lines_config"), added_in=1),
        # Edge-line overlay for the preview thumbnails: geometry graph node
        # previews (on by default) and brush previews (off by default; the
        # designated-initializer default overrides only 'enabled', the other
        # members keep the struct's own defaults).
        field("graph_node_preview_edge_lines",    StructRef("Preview_edge_lines_config"), added_in=1, short_desc="Graph Node Preview Edge Lines"),
        field("brush_preview_edge_lines",         StructRef("Preview_edge_lines_config"), added_in=1, short_desc="Brush Preview Edge Lines", default=".enabled = false"),
        # Editor-global geometry graph node preview thumbnails: visibility
        # (on by default) and hover auto-rotation.
        field("graph_node_previews",              StructRef("Graph_node_previews_config"), added_in=1),
        field("developer",            StructRef("Developer_config"),       added_in=1),
        field("grid",                 StructRef("Grid_config"),            added_in=1),
        field("headset",              StructRef("Headset_config"),         added_in=1),
        field("hotbar",               StructRef("Hotbar_config"),          added_in=1),
        field("hud",                  StructRef("Hud_config"),             added_in=1),
        field("id_renderer",          StructRef("Id_renderer_config"),     added_in=1),
        field("ray_trace",            StructRef("Ray_trace_config"),       added_in=1),
        field("ddgi",                 StructRef("Ddgi_config"),            added_in=1),
        field("radiance_cascades",    StructRef("Radiance_cascades_config"), added_in=5),
        field("lightmap",             StructRef("Lightmap_config"),        added_in=1),
        # glTF import/open performance options (doc/editor/async_asset_loading.md).
        field("load",                 StructRef("Load_config"),            added_in=1),
        field("network",              StructRef("Network_config"),         added_in=1),
        field("physics",              StructRef("Physics_config"),         added_in=1),
        field("scene",                StructRef("Scene_config"),           added_in=1),
        field("shadow_frustum_fit",   StructRef("Shadow_frustum_fit_config"), added_in=1),
        field("sky",                  StructRef("Sky_config"),             added_in=1),
        field("thumbnails",           StructRef("Thumbnails_config"),      added_in=1),
        field("transform_tool",       StructRef("Transform_tool_config"), added_in=1),
        field("viewport",             StructRef("Viewport_config_data"),  added_in=1),
        field("graphics_preset_name", String, added_in=1, default='"Medium"'),
        field("imgui",                StructRef("Imgui_settings_config"), added_in=1),
        field("icons",                StructRef("Icon_settings_config"),  added_in=1),
        field("threading",            StructRef("Threading_config"),     added_in=1),
        field(
            "post_processing",
            Bool,
            added_in=1,
            default="true",
            short_desc="Post Processing",
            long_desc="Enable Post Processing",
            visible=True,
            developer=False
        ),
        # doc/erhe/draw_list_renderer.md: render eligible composition
        # passes and shadow maps from the scene's persistent draw lists
        # (Draw_list_scene) instead of re-bucketing mesh spans every pass.
        # Runtime toggle; ineligible passes always use the classic path.
        field(
            "use_draw_lists",
            Bool,
            added_in=2,
            default="false",
            short_desc="Draw Lists",
            long_desc="Render content fill and shadow maps through persistent per-scene draw lists (faster with many primitives). Off = classic per-pass bucketing.",
            visible=True,
            developer=False
        ),
        # Unlit (KHR_materials_unlit) primitives are typically sky domes,
        # backdrops and emissive decals: geometry that should not occlude the
        # lit scene and should not drag the camera framing out to the horizon.
        # They still count toward the camera far plane, so the backdrop stays
        # visible.
        field(
            "exclude_unlit_primitives",
            Bool,
            added_in=3,
            default="true",
            short_desc="Exclude Unlit Primitives",
            long_desc="Unlit (KHR_materials_unlit) primitives do not cast shadows and are ignored when framing the camera on scene open. They still count toward the camera far plane.",
            visible=True,
            developer=False
        ),
        field(
            "geometry_edit_mode",
            EnumRef("Geometry_edit_mode"),
            added_in=1,
            default="Geometry_edit_mode::shared",
            short_desc="Geometry Edit Mode",
            long_desc="When editing components of a mesh whose geometry is shared by other meshes (e.g. a duplicate), whether to edit the shared geometry (all instances change) or fork a copy on edit (only this instance changes).",
            visible=True,
            developer=False
        ),
        field(
            "transform_mode",
            EnumRef("Mesh_transform_mode"),
            added_in=1,
            default="Mesh_transform_mode::move",
            short_desc="Mesh Transform Mode",
            long_desc="When transforming a mesh component selection with the gizmo, whether to move the selected components or extrude them (duplicate the selection boundary and bridge it with new faces) before moving.",
            visible=True,
            developer=False
        ),
        # The producer of the indirect diffuse probe field
        # (doc/editor/radiance_cascades.md "Source selection").
        field(
            "indirect_diffuse_source",
            EnumRef("Indirect_diffuse_source"),
            added_in=5,
            default="Indirect_diffuse_source::ambient",
            short_desc="Indirect Diffuse",
            long_desc="Producer of the indirect diffuse light for non-lightmapped draws: the flat scene ambient term, DDGI, or radiance cascades. DDGI and radiance cascades need GPU ray query support.",
            visible=True,
            developer=False
        ),
        # Where the draw lists' culling classification runs
        # (doc/plans/culling.md phases 5 and 6). gpu also runs it as a compute
        # pass whose masks a diagnostic compares with the CPU masks and from
        # which a second pass generates the shadow and draw-list color passes'
        # draw commands.
        field(
            "culling_classification",
            EnumRef("Culling_classification"),
            added_in=6,
            default="Culling_classification::cpu",
            short_desc="Culling Classification",
            long_desc="Where the draw lists' culling classification runs: the CPU, or also a compute pass on the GPU that generates the shadow and draw-list color passes' draw commands too (devices with native multi-draw indirect with a GPU-read draw count and first-instance record indexing; others use the CPU).",
            visible=True,
            developer=True
        ),
        # The raster occlusion test of the viewports' color views
        # (doc/plans/occlusion_culling.md): with the gpu culling
        # classification, each viewport's color render pass rasterizes the
        # draw lists' entry boxes against its opaque depth and marks the
        # visible entries; the next frame's classification culls the entries
        # found hidden (one frame of lag).
        field(
            "occlusion_culling",
            Bool,
            added_in=8,
            default="false",
            short_desc="Occlusion Culling",
            long_desc="Run the raster occlusion test in the viewports (needs the gpu culling classification, a GPU-read draw count and fragment shader storage atomics): the draw lists' entry boxes are drawn against each view's opaque depth and the visible entries are marked. The next frame skips the entries found hidden (one frame of lag).",
            visible=True,
            developer=True
        ),
        # The potentially visible set rule (doc/plans/pvs.md "Runtime
        # consumption"): with a ready database, the culling classification
        # (both paths) drops the entries no cell around a viewport camera's
        # query box can see. An absent or stale database culls nothing.
        field(
            "pvs_culling",
            Bool,
            added_in=9,
            default="true",
            short_desc="PVS Culling",
            long_desc="Cull the draw list entries outside the potentially visible set of the cells around each viewport camera, when the scene has a ready PVS database (a bake; a scene edit that moves a baked entry makes it stale and turns this off until the next bake).",
            visible=True,
            developer=True
        ),
        # The positional margin of a viewport camera's PVS query box: the
        # box is the camera position +- this many meters per axis.
        field(
            "pvs_query_margin",
            Float,
            added_in=9,
            default="0.05f",
            short_desc="PVS Query Margin",
            long_desc="Half the size, in meters, of the box around a viewport camera whose cells' potentially visible sets the PVS culling reads.",
            visible=True,
            developer=True
        ),
        # The PVS page streaming (doc/plans/pvs.md "Residency and
        # streaming"): the page slots the GPU holds and the radius, in pages,
        # around a view's page that its query box registration requests.
        field(
            "pvs_resident_pages",
            Int,
            added_in=10,
            default="64",
            short_desc="PVS Resident Pages",
            long_desc="Number of PVS database pages (page_size^3 cells each) resident on the GPU at once. Pages without a slot read as everything visible; the pages around each viewport camera stream in from the scene file, one page per frame.",
            visible=True,
            developer=True
        ),
        field(
            "pvs_stream_radius_pages",
            Int,
            added_in=10,
            default="1",
            short_desc="PVS Stream Radius",
            long_desc="The radius, in pages, around a viewport camera's PVS page that is requested when the camera enters another page (1: the 27 pages around it).",
            visible=True,
            developer=True
        ),
        # Where the light cluster lists of clustered views are binned
        # (doc/plans/clustered_lights.md C4). gpu runs the binning as a
        # compute pass; cpu bins on the render thread (the reference
        # get_light_cluster_check compares the GPU lists with).
        field(
            "light_cluster_binning",
            EnumRef("Light_cluster_binning"),
            added_in=7,
            default="Light_cluster_binning::gpu",
            short_desc="Light Cluster Binning",
            long_desc="Where the light cluster lists of clustered light shading are binned: a compute pass on the GPU, or the CPU on the render thread. Both produce the same lists.",
            visible=True,
            developer=True
        ),
        field(
            "quit_after_frames",
            Int,
            added_in=1,
            default="0",
            short_desc="Quit After Frames",
            long_desc="Exit editor after this many frames. 0 to disable.",
            visible=True,
            developer=True
        ),
    ],
)
