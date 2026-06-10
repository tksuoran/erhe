from erhe_codegen import *

# Settings for the modular directional light shadow frustum fit
# (erhe::scene::Shadow_frustum_fit_settings; the tight fit is implemented in
# erhe_scene/light_frustum_fit.cpp). Fields mirror the C++ struct 1:1 and the
# defaults match the C++ member defaults: with all tightening steps off the
# fit is identical to the legacy stable bounding-sphere fit.
struct("Shadow_frustum_fit_config",
    version=2,
    short_desc="Shadow Frustum Fit",
    long_desc="Directional light shadow frustum fitting settings",
    developer=False,
    fields=[
        field("fit_to_view_frustum",    Bool,  added_in=1, default="false", short_desc="Fit To View Frustum",    long_desc="Fit light space extents to the main camera view frustum"),
        field("fit_to_casters",         Bool,  added_in=1, default="false", short_desc="Fit To Casters",         long_desc="Fit to the shadow caster convex hull clipped to the extruded shadow volume"),
        field("fit_to_receivers",       Bool,  added_in=2, default="true",  short_desc="Fit To Receivers",       long_desc="Cull shadow casters against the receiver geometry volume (view frustum intersected with receiver bounds) extruded toward the light; refines Fit To Casters and has no effect without it"),
        field("fit_to_receivers_hull",  Bool,  added_in=2, default="true",  short_desc="Receivers Use Hull",      long_desc="Use the tighter convex receiver hull clipped to the view frustum instead of a bounding box for the receiver cull volume"),
        field("optimize_rotation",      Bool,  added_in=1, default="false", short_desc="Optimize Rotation",      long_desc="Rotating calipers roll around the light direction for minimum area coverage"),
        field("near_from_main_frustum", Bool,  added_in=1, default="false", short_desc="Near From View Frustum", long_desc="Near plane from the view frustum extent; enable Depth Clamp to keep casters closer to the light"),
        field("depth_clamp",            Bool,  added_in=1, default="false", short_desc="Depth Clamp",            long_desc="Depth clamp rasterization in the shadow pass"),
        field("texel_snap",             Bool,  added_in=1, default="true",  short_desc="Texel Snap",             long_desc="Snap the light space box to shadow map texels"),
        field("quantize_extents",       Bool,  added_in=1, default="false", short_desc="Quantize Extents",       long_desc="Round the light space box size up to multiples of Quantize Step"),
        field("quantize_step",          Float, added_in=1, default="0.0f",  short_desc="Quantize Step",          long_desc="Extent quantization step in world units; 0 derives a step from the shadow range"),
        field("cap_by_shadow_range",    Bool,  added_in=1, default="true",  short_desc="Cap By Shadow Range",    long_desc="Never exceed the stable shadow range box extents"),
        field("collect_debug",          Bool,  added_in=1, default="false", short_desc="Collect Debug Data",     long_desc="Collect per-step intermediates for the Shadow Fit debug visualization and its Dump to Log; disable to profile the fit without any debug-collection overhead"),
    ],
)
