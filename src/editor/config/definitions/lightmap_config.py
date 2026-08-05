from erhe_codegen import *

struct("Lightmap_config",
    version=11,
    short_desc="Lightmap",
    long_desc="Lightmap baking settings (doc/lightmap_baking_plan.md). Texel density comes from the world-space tile grid: tile_texture_size / cell size, per tile. The boolean toggles switch individual bake/sampling features off for A/B comparison and debugging. (The legacy standalone texels_per_meter unwrap density was removed in version 11; stale keys are ignored.)",
    developer=False,
    fields=[
        field(
            "cell_size_m",
            Float,
            added_in=10,
            default="8.0f",
            short_desc="Tile cell size (m)",
            long_desc="World-space side of one lightmap grid cell in meters (quadtree level 0). The world is covered by a uniform grid of this cell size anchored at the world origin; each occupied cell becomes one tile_texture_size^2 tile, so its nominal texel density is tile_texture_size / cell_size_m. Subdividing a cell (quadtree) halves the cell and doubles the density; merging does the opposite. Density flexes down per tile only when content does not fit.",
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
            long_desc="Interactive baking clamp for multi-tile layouts: at most this many spatial tiles (of the resident, slot-holding set) gather at once, ranked by camera distance to their world bounds. The others stop accumulating and release their accumulation memory. 0 = the whole resident set (resident_tile_budget).",
            visible=True,
            developer=False
        ),
        field(
            "tile_texture_size",
            Int,
            added_in=8,
            default="1024",
            short_desc="Tile texture size",
            long_desc="Texel side of one spatial lightmap tile (power of two, 256..8192). Together with cell_size_m this sets the nominal texel density of every grid tile (tile_texture_size / cell size). Also the size of every resident display slot; bake scratch memory scales with its square.",
            visible=True,
            developer=False
        ),
        field(
            "resident_tile_budget",
            Int,
            added_in=8,
            default="9",
            short_desc="Resident tiles",
            long_desc="How many spatial tiles may be resident in GPU memory at once (display atlas slots; 9 = a 3x3 grid around the camera). The nearest tiles stream in from <scene>.lightmap/ (or bake interactively); the rest render unlit until the camera approaches. Total lightmap GPU memory is bounded by this budget times tile_texture_size^2, regardless of world size. Further capped automatically by the device memory budget.",
            visible=True,
            developer=False
        ),
        field(
            "offline_sweeps",
            Int,
            added_in=8,
            default="64",
            short_desc="Bake-to-disk sweeps",
            long_desc="Accumulation sweeps gathered per tile by Bake To Disk before the tile is written to <scene>.lightmap/. Higher = less noise, linearly slower bake.",
            visible=True,
            developer=False
        ),
        field(
            "render_with_lightmaps",
            Bool,
            added_in=9,
            default="false",
            short_desc="Render with lightmaps",
            long_desc="World-space tile partition render toggle: on renders the clipped per-tile piece meshes ('Lightmap Pieces' group) for every lightmapped mesh - pieces of non-resident tiles fall back to a flat white lightmap - and hides the originals; off renders the original meshes. Takes effect when a partition is prepared (Prepare World-Space Tiles).",
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
