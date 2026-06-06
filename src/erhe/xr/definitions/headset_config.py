from erhe_codegen import *

struct("Headset_config",
    version=2,
    short_desc="Virtual Reality Headset",
    long_desc="",
    developer=False,
    fields=[
        field(
            "openxr",
            Bool,
            added_in=1,
            default="false",
            short_desc="OpenXR Enable",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "openxr_mirror",
            Bool,
            added_in=1,
            default="false",
            short_desc="OpenXR Mirror Window",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "quad_view",
            Bool,
            added_in=1,
            default="false",
            short_desc="Enable Quad View (Varjo)",
            long_desc="",
            visible=True,
            developer=True
        ),
        field(
            "debug",
            Bool,
            added_in=1,
            default="false",
            short_desc="Enable OpenXR Debug",
            long_desc="",
            visible=True,
            developer=True
        ),
        field(
            "validation",
            Bool,
            added_in=1,
            default="false",
            short_desc="Enable OpenXR Validation",
            long_desc="",
            visible=True,
            developer=True
        ),
        field(
            "api_dump",
            Bool,
            added_in=1,
            default="false",
            short_desc="Enable OpenXR API Dump",
            long_desc="",
            visible=True,
            developer=True
        ),
        field(
            "composition_depth_layer",
            Bool,
            added_in=1,
            default="false",
            short_desc="Submit OpenXR composition-layer depth",
            long_desc=("Enable XR_KHR_composition_layer_depth: the editor "
                       "submits per-eye depth to the OpenXR compositor as part "
                       "of the projection layer. Independent of "
                       "swapchain_depth_attachment (which controls whether "
                       "the editor's swapchains have a depth attachment at "
                       "all)."),
            visible=True,
            developer=True
        ),
        field(
            "swapchain_depth_attachment",
            Bool,
            added_in=1,
            default="false",
            short_desc="Allocate a depth attachment for XR swapchains",
            long_desc=("Request a depth+stencil attachment alongside the "
                       "XR color swapchain. When false the editor allocates "
                       "its own private depth target instead. Independent of "
                       "the composition_depth_layer knob above (which submits "
                       "depth to the OpenXR compositor)."),
            visible=True,
            developer=True
        ),
        field(
            "visibility_mask",
            Bool,
            added_in=1,
            default="false",
            short_desc="",
            long_desc="",
            visible=True,
            developer=True
        ),
        field(
            "hand_tracking",
            Bool,
            added_in=1,
            default="false",
            short_desc="",
            long_desc="",
            visible=True,
            developer=True
        ),
        field(
            "passthrough_fb",
            Bool,
            added_in=1,
            default="false",
            short_desc="Keep camera passthrough on for the whole session",
            long_desc=("When the OpenXR runtime supports XR_FB_passthrough, the "
                       "init status screen always shows camera passthrough "
                       "around the loading panel, regardless of this setting. "
                       "When enabled, passthrough stays running for the whole "
                       "session (the passthrough composition layer is submitted "
                       "under the projection layer every frame); when disabled, "
                       "passthrough is paused once the editor scene starts "
                       "rendering."),
            visible=True,
            developer=True
        ),
        field(
            "composition_alpha",
            Bool,
            added_in=1,
            default="false",
            short_desc="",
            long_desc="",
            visible=True,
            developer=True
        ),
        field(
            "composition_quad_layers",
            Bool,
            added_in=1,
            default="true",
            short_desc="Use OpenXR quad composition layers for UI",
            long_desc=("When enabled (and OpenXR is active), the Hud and Hotbar "
                       "are submitted as XrCompositionLayerQuad composition "
                       "layers rendered at native swapchain resolution instead "
                       "of being drawn as scene-mesh quads. Falls back to the "
                       "scene-mesh path automatically if the quad swapchain "
                       "cannot be created."),
            visible=True,
            developer=False
        ),
        field(
            "composition_quad_layers_depth",
            Bool,
            added_in=1,
            default="true",
            short_desc="Depth-test quad composition layers against the scene",
            long_desc=("When enabled, the runtime supports it "
                       "(XR_KHR_composition_layer_depth + "
                       "XR_FB_composition_layer_depth_test), and a usable depth "
                       "swapchain exists, the UI quad composition layers are "
                       "depth-tested against the rendered 3D scene so scene "
                       "geometry can occlude them. Set false to force the test "
                       "off. Has no effect unless the runtime prerequisites are "
                       "met."),
            visible=True,
            developer=False
        ),
        field(
            "cpu_performance_level",
            EnumRef("Perf_settings_level"),
            added_in=1,
            default="Perf_settings_level::e_unset",
            short_desc="CPU Performance Level",
            long_desc="Suggested CPU clock level via XR_EXT_performance_settings. e_unset keeps the runtime default.",
            visible=True,
            developer=False
        ),
        field(
            "gpu_performance_level",
            EnumRef("Perf_settings_level"),
            added_in=1,
            default="Perf_settings_level::e_unset",
            short_desc="GPU Performance Level",
            long_desc="Suggested GPU clock level via XR_EXT_performance_settings. e_unset keeps the runtime default.",
            visible=True,
            developer=False
        ),
        field(
            "boost_on_thermal_warning",
            Bool,
            added_in=1,
            default="false",
            short_desc="Step Down Level on Thermal Warning",
            long_desc="When the runtime sends a thermal warning event, automatically step the affected domain's level down by one notch.",
            visible=True,
            developer=False
        ),
        field(
            "foveation",
            Bool,
            added_in=2,
            default="false",
            short_desc="Enable Fixed Foveated Rendering",
            long_desc=("Enable OpenXR fixed foveated rendering (FFR) via "
                       "XR_FB_foveation + VK_EXT_fragment_density_map: the "
                       "periphery of each eye is rendered at lower resolution to "
                       "save GPU fill and power. Requires the runtime and Vulkan "
                       "device to support the foveation extensions; no-ops "
                       "cleanly otherwise. Vulkan backend only."),
            visible=True,
            developer=False
        ),
        field(
            "foveation_level",
            EnumRef("Foveation_level"),
            added_in=2,
            default="Foveation_level::e_high",
            short_desc="Fixed Foveation Level",
            long_desc=("Foveation level (none/low/medium/high). With dynamic "
                       "foveation enabled this is the maximum level the runtime "
                       "scales down from based on GPU load."),
            visible=True,
            developer=False
        ),
        field(
            "foveation_dynamic",
            Bool,
            added_in=2,
            default="true",
            short_desc="Dynamic Foveation",
            long_desc=("Let the runtime dynamically scale the applied foveation "
                       "level up to foveation_level based on GPU load, instead of "
                       "always applying the fixed level."),
            visible=True,
            developer=False
        ),
    ],
)
