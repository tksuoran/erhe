# Shadow robustness: remaining work

Status: proposed

The directional, spot and point shadow paths meet the correctness
requirements and gates (R1 to R8, R10; G1 to G6) of [`shadows.md`](../erhe/shadows.md) "Shadow verification" on Vulkan (the
headless editor matrices) and OpenGL (the library GPU tests); the design is in
[`shadows.md`](../erhe/shadows.md) and
[`point_light_shadows.md`](../erhe/point_light_shadows.md), the commands in
[`testing.md`](../testing.md) "Shadow verification". Two items remain, both future work. The analytic review of the landed series, with its
potential issues and improvements, is
[reference/shadow_robustness_review_2026_09_29.md](../reference/shadow_robustness_review_2026_09_29.md).

## 1. Metal

Build the shadow path on Metal and run `erhe_scene_renderer_gpu_tests
--gtest_filter=*Shadow*` there (needs a macOS machine). The shaders rely on
`precise` (`ERHE_SHADOW_DISTANCE_PRECISE`, `ERHE_VIEW_RELATIVE_PRECISE`) and
`dFdxFine` / `dFdyFine` surviving the SPIR-V to MSL translation with their
semantics; the distance technique needs its caster ray and the receiver ray to
agree bit for bit (`shadows.md` "The distance technique"). Pass: every case
passes, as on Vulkan and OpenGL, and the device's depth convention is recorded
in `shadows.md` "Verified backends".

## 2. Forward pass cost: profiling and optimization

The hardened path costs a median of +12 % forward pass GPU time against the
tree before the shadow robustness work (`shadows.md` "Shadow verification",
G7: accepted cost). Per view and light: `cornell` +9 % (directional +2 to
+9 %, spot +12 to +16 %, point +4 to +7 %), `contact_blocks` +13 % (+12 to
+15 % for every light type, point included); measured with
`shadow_verify.py --g7 9`, two alternating rounds, Debug headless Vulkan
editor, AMD iGPU, Medium preset, 1920 x 1080. The per-light spread and the
uniform `contact_blocks` increase point at two costs: the per-tap minimum
bias of the 2D paths, and a per-vertex / per-fragment cost shared by every
light type (the view-relative `precise` vertex path of `standard.vert` and
the receiver geometric normal `get_receiver_geometric_normal()` taken once
per fragment).

Profile the forward pass (RenderDoc or a shader-variant A/B on the same
editor) and attribute the cost to those terms; then move per-light
constants of the minimum bias out of the per-fragment path where the bound
allows it and share per-fragment work across lights, without weakening any
bound (the gates of `shadows.md` "Shadow verification" still pass).
Re-measure with `--g7`, lower G7's accepted cost in `shadows.md` to the new
measurement, and record the absolute numbers in the machine-local memory
bank.
