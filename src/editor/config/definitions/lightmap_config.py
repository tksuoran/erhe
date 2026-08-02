from erhe_codegen import *

struct("Lightmap_config",
    version=4,
    short_desc="Lightmap",
    long_desc="Lightmap baking settings (doc/lightmap_baking_plan.md). Texel density is the one quality knob; the boolean toggles switch individual bake/sampling features off for A/B comparison and debugging.",
    developer=False,
    fields=[
        field(
            "texels_per_meter",
            Float,
            added_in=1,
            default="16.0f",
            short_desc="Texels per meter",
            long_desc="Lightmap texel density: lightmap texels per world-space meter. Sets each lightmapped instance's atlas region size (surface area x density). The single user-facing quality knob.",
            visible=True,
            developer=False
        ),
        field(
            "hard_angles_deg",
            Float,
            added_in=1,
            default="60.0f",
            short_desc="UV chart hard angle (deg)",
            long_desc="Automatic lightmap UV unwrap: dihedral angle above which an edge becomes a chart boundary. Fixed default; exposed for developers only.",
            visible=True,
            developer=True
        ),
        field(
            "uv_parameterizer",
            Int,
            added_in=3,
            default="3",
            short_desc="UV parameterizer",
            long_desc="Chart parameterizer for lightmap UV unwrap; matches erhe::geometry::operation::Atlas_parameterizer: 0 = projection, 1 = LSCM, 2 = spectral LSCM, 3 = ABF++ (Geogram default), 4 = per-facet (every facet its own isometric chart; no Geogram, zero overlaps, no shared texels - doc/lightmap_seam_driven_unwrap_plan.md). Exposed to iterate on unwrap defects (overlapping / folded UV triangles).",
            visible=True,
            developer=True
        ),
        field(
            "uv_packer",
            Int,
            added_in=3,
            default="2",
            short_desc="UV packer",
            long_desc="Chart packer for lightmap UV unwrap; matches erhe::geometry::operation::Atlas_packer: 0 = none, 1 = tetris, 2 = xatlas (Geogram default). With texel density > 0 erhe repacks charts itself; the packer still affects chart normalization.",
            visible=True,
            developer=True
        ),
        field(
            "uv_gutter_texels",
            Float,
            added_in=3,
            default="3.0f",
            short_desc="UV chart gutter (texels)",
            long_desc="Minimum empty space between any two charts, in texels at the expected rasterization density, when erhe packs the charts itself (density-aware packing).",
            visible=True,
            developer=True
        ),
        field(
            "uv_min_chart_texels",
            Float,
            added_in=4,
            default="2.0f",
            short_desc="UV min chart size (texels)",
            long_desc="Minimum chart side in texels at the expected rasterization density: charts smaller than this are scaled up (capped at 16x) so every chart contains at least one texel center and bakes valid data. 0 disables the clamp. Matters most for per-facet unwraps of dense meshes.",
            visible=True,
            developer=True
        ),
        field(
            "indirect_bounce",
            Bool,
            added_in=2,
            default="true",
            short_desc="Indirect bounce",
            long_desc="One cosine-weighted hemisphere bounce ray per gather sample; off = pure direct lighting. Toggling restarts accumulation.",
            visible=True,
            developer=False
        ),
        field(
            "terminator_fix",
            Bool,
            added_in=2,
            default="true",
            short_desc="Terminator fix",
            long_desc="Phong-tessellated smooth sample positions (article shadow-terminator fix). Toggling re-rasters the G-buffer and restarts accumulation.",
            visible=True,
            developer=False
        ),
        field(
            "denoise",
            Bool,
            added_in=2,
            default="true",
            short_desc="Denoise (JNLM)",
            long_desc="Joint non-local means denoise of the published atlas, applied at each per-sweep publish.",
            visible=True,
            developer=False
        ),
        field(
            "dilation",
            Bool,
            added_in=2,
            default="true",
            short_desc="Dilation",
            long_desc="Flood valid texels into invalid chart-padding neighbors at publish so bilinear/bicubic filtering never reads unbaked (black) texels.",
            visible=True,
            developer=False
        ),
        field(
            "seam_blend",
            Bool,
            added_in=2,
            default="true",
            short_desc="Seam blend",
            long_desc="Blend both sides of every UV seam edge toward each other at publish (Godot lm_blendseams approach).",
            visible=True,
            developer=False
        ),
        field(
            "bicubic_sampling",
            Bool,
            added_in=2,
            default="true",
            short_desc="Bicubic sampling",
            long_desc="Viewport lightmap filtering: cubic B-spline reconstruction (4 bilinear taps) instead of plain bilinear. Applies immediately; no rebake needed.",
            visible=True,
            developer=False
        ),
    ],
)
