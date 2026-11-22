# Detail blend weight: implementation steps

Status: in progress

The build order for `doc/plans/detail_blend_weight_design.md` (BD-numbers).
It meets `doc/plans/detail_blend_weight.md` (B-numbers). One step is one
commit. A step is done when its verification passes. A done step is removed
from this list, and what it built is described in the current documents
its step names.

Each step is one coder brief (`doc/agents/orchestration_harness.md`): the
coder builds, runs the step's own checks and leaves the changes
uncommitted; the orchestrator reviews the diff and commits. A step runs
its own checks only (`AGENTS.md` "When to run the slow test suites"). MCP
tool changes go into `config/editor/mcp_tools.json` in the step that makes
them. Step 5 runs the gates:

- `erhe_math_tests`, `erhe_primitive_tests`, the editor test executable
  that holds the channel brush tests;
- `scripts/detail_blend_verify.py` (full);
- `scripts/scene_roundtrip_verify.py`, `scripts/shader_fp16_verify.py`;
- `ctest -C Debug -L editor -j1`.

## Built

Step 1, material values: the Detail Blend properties, the sixth texture
slot, the record fields (`doc/erhe/primitive.md`,
`doc/erhe/scene_renderer.md`), persistence through `ERHE_material`, the MCP
material tools and `scripts/detail_blend_verify.py --only-material`.

Step 2, channel painting without the tool: the channel brush core and its
unit tests (`editor_ik_solver_tests`, `Channel_brush.*`), the stroke, fill
and invert, the MCP tools `paint_vertex_channel`, `fill_vertex_channel` and
`invert_vertex_channel` (`doc/editor/tools.md` "Channel painting") and
`scripts/detail_blend_verify.py --only-paint`.

Step 3, shader: the variant axes `USE_DETAIL_BLEND`,
`USE_DETAIL_BLEND_TEXTURE`, `USE_DETAIL_BLEND_NOISE`,
`DETAIL_BLEND_TEXGEN_MODE` and `DETAIL_BLEND_NOISE_SPACE` (the bool axes now
a 64-bit `Shader_bool_mask`), `res/shaders/erhe_detail_blend.glsl`, the
vertex color factor dropped for a detail blend material, the debug mode
`Shader_debug::detail_blend_weight` (38) (`doc/erhe/shader_variants.md`
"Detail blend"), `erhe::math::detail_blend()` with `Detail_blend.*` in
`erhe_math_tests` (`doc/erhe/math.md` "Detail blend") and
`scripts/detail_blend_verify.py --only-shader`, which also passes with the
16-bit material record (`material_record_half` on, desktop) and with Vulkan
validation on.

The cost (B6.4), measured on the desktop (Windows, Vulkan, the headless
Debug editor): `malioc` and the PowerVR compiler are not installed there, so
the shader analysis of `doc/editor/shader_analytics.md` gave no Mali or
PowerVR numbers and no headset variant was analyzed. Measured instead: the
instruction count of the standard fragment stage's SPIR-V (from the SPIR-V
cache, the render_scene_image variants of a plane): all instructions / all
but debug and non-semantic ones / after `spirv-opt --strip-debug
--strip-nonsemantic` then `spirv-opt -O`.

| Variant | Instructions |
|---|---|
| detail blend off, lit, before this step | 5797 / 3285 / 842 |
| detail blend off, lit, after this step | 5797 / 3285 / 842 (the optimized modules equal up to id numbering) |
| detail blend on (texture), lit | 5789 / 3277 / 839 (the vertex color factor dropped) |
| detail blend off, debug mode 38 | 5951 / 3381 / 846 |
| detail blend on, debug mode 38, texture only | 6376 / 3631 / 920 |
| detail blend on, debug mode 38, texture and noise | 6722 / 3847 / 1122 |

Step 4, the tool: `Paint_tool`'s mode Brush with the brush sphere, Shift +
wheel, Escape, Fill and Invert and the Show Detail Weight switch, the saved
`Paint_brush_config`, the MCP tools `get_paint_brush_settings` and
`set_paint_brush_settings` (`doc/editor/tools.md` "Paint tool brush") and
`scripts/detail_blend_verify.py --only-tool`.

Step 5, the final pass: the gates on the Windows headless Debug tree
(`build_vs2026_vulkan_headless`, committed config), with `editor`,
`mcp_server_tests`, `example`, `hello_swap` and `hextiles` building.
`erhe_math_tests` 117 passed and 3 skipped (the `Shadow_fit_bench` /
`Shadow_fit_replay` benchmarks), `erhe_primitive_tests` 102,
`erhe_scene_renderer_tests` 117, `editor_ik_solver_tests` 61;
`scripts/detail_blend_verify.py` 108/108; `scripts/scene_roundtrip_verify.py`
490/490 checks; `scripts/shader_fp16_verify.py` 520 rows, 0 FAIL on the
stations `roughness_metallic`, `hdr_ramp`, `dark_room` and `attenuation`;
`ctest -C Debug -L editor -j1` 108/108 (on the second run; the first,
with the SPIR-V cache cold after the shader change, failed
`Mcp_test.get_async_status_reports_background_work` on its 60 s wait).
Failures older than this series: the `grazing` station of
`shader_fp16_verify.py` (its `create_shape` outlasts the client's 10 s
socket timeout in Debug) and the artifact cleanup of
`scene_roundtrip_verify.py` (the running editor holds the `.erhe_editor`
stores open).

## Open

- The Mali and PowerVR cost of the detail blend variants (B6.4, desktop
  and headset variant) waits for a machine with `malioc` and the PowerVR
  compiler; the 16-bit record has run on the desktop only.
- USD carries the detail blend values as `erhe:Material:*` attributes and
  leaves the slot's texture out.
- A Graph Texture in a material slot loses the slot's UV transform on save
  and load (every slot; the binding table has no transform field).

- The rulings of `doc/plans/detail_blend_weight.md` "For the user to rule
  on": the build follows the proposed choice of each (user, 2026-11-22:
  the design and the build go ahead on the requirements as written).
- For the user's review, the design's choices: the default weight channel
  `r` (BD1.1); a stroke stays on the primitive it started on (BD7.2); the
  "Color" brush mask (BD7.3). The build's choices: the tool losing priority
  mid-drag cancels a Brush stroke; Fill and Invert take each mesh's channel
  from its first primitive's material; the tool's deactivation switches
  Show Detail Weight off; the saved values leave out the tool's mode (Point,
  Corner, Polygon, Brush).

## User test (windowed)

Build the windowed editor, reset `config/editor/desktop_windows.json` and
`editor_settings.json`, launch it for the user
(`doc/agents/editor_runs.md` "Handing the editor to the user") and ask for
these checks:

1. A material's Properties show the "Detail Blend" group; a texture
   dropped into its slot shows there.
2. Paint tool, Brush mode, Weight: a left drag over a mesh with that
   material paints; the debug mode switch shows the grayscale, with the
   texture's pattern along the painted boundary.
3. Transition width 0 gives a hard patterned edge, 1 a soft one; height
   influence 0 gives the plain painted weight.
4. Noise amount above 0 breaks up the pattern; the noise space setting
   decides whether it follows the moved node.
5. Shift + wheel changes the brush sphere; Escape during a drag restores;
   one undo per stroke; fill and invert.
6. Save, close and open the scene: values, texture and weights are there.
