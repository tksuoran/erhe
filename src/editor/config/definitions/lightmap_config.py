from erhe_codegen import *

struct("Lightmap_config",
    version=7,
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
            "coverage_mode",
            Int,
            added_in=5,
            default="0",
            short_desc="Texel coverage",
            long_desc="G-buffer texel coverage strategy: 0 = native conservative rasterization (one pass; falls back to 9-tap when the extension is unavailable), 1 = 9-tap sub-texel jitter re-render, 2 = 25-tap sub-texel jitter re-render (denser edge coverage, slower G-buffer bake). Changing re-rasters the G-buffer and restarts accumulation.",
            visible=True,
            developer=True
        ),
        field(
            "supersample_points",
            Int,
            added_in=6,
            default="0",
            short_desc="Supersampled ray origins",
            long_desc="Frostbite Flux texel supersampling: rasterize sample positions on a regular sub-texel grid and pick a uniform-random valid point as the origin of every shadow and bounce ray, instead of one fixed origin per texel. Integrates lighting over the covered part of each texel and softens direct-shadow aliasing. 0 = off, 1 = 16 points per texel (4x4 grid), 2 = 64 points per texel (8x8 grid, the Flux default). Costs one page-sized RGBA32F target at grid-side x resolution per axis while baking (64 points = 1 KB per atlas texel). Changing re-rasters the G-buffer and restarts accumulation. (Replaces the short-lived boolean 'supersample' field; stale keys are ignored.)",
            visible=True,
            developer=False
        ),
        field(
            "active_tile_budget",
            Int,
            added_in=7,
            default="0",
            short_desc="Active tiles",
            long_desc="Camera-clamped baking for tiled atlases (pages larger than one 2048 tile cell): only the N tile cells whose regions are nearest the viewport camera keep gathering; the others stop accumulating, release their accumulation memory, and keep showing their last published result. 0 = bake all tiles.",
            visible=True,
            developer=False
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
