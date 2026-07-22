from erhe_codegen import *

struct("Graphics_config",
    version=1,
    short_desc="Graphics Settings",
    long_desc="",
    developer=False,
    fields=[
        field(
            "initial_clear",
            Bool,
            added_in=1,
            default="true",
            short_desc="Initial Clear",
            long_desc="Submit few empty frames during Startup",
            visible=True,
            developer=False
        ),
        field(
            "renderdoc_capture_support",
            Bool,
            added_in=1,
            default="false",
            short_desc="Enable RenderDoc Capture Support",
            long_desc="Enables RenderDoc Capture Support. Disables use of OpenGL Bindless Textures. Only meaningful for debugging.",
            visible=True,
            developer=False
        ),
        field(
            "renderdoc_library_path_override_enable",
            Bool,
            added_in=1,
            default="false",
            short_desc="Enable RenderDoc Library Path Override",
            long_desc="When enabled, loads the RenderDoc capture library from renderdoc_library_path_override instead of the system-installed one. Kept separate from the path so the path can stay configured while the override is toggled on and off. Has no effect unless renderdoc_capture_support is also enabled. Only meaningful for debugging.",
            visible=True,
            developer=False
        ),
        field(
            "renderdoc_library_path_override",
            String,
            added_in=1,
            default='""',
            short_desc="RenderDoc Library Path Override",
            long_desc="Path to the RenderDoc capture library to load when renderdoc_library_path_override_enable is true (e.g. an experimental RenderDoc fork) instead of the system-installed one. On Vulkan, the parent directory is added to the loader's implicit layer search path (VK_ADD_IMPLICIT_LAYER_PATH) and the system RenderDoc capture layers are disabled so the override is the one that gets used. Only meaningful for debugging.",
            visible=True,
            developer=False
        ),
        field(
            "shader_monitor_enabled",
            Bool,
            added_in=1,
            default="true",
            short_desc="Enable Shader Monitor",
            long_desc="Enables Shader Monitor, allowing Shader Hot-reloading. Only meaningful for debugging.",
            visible=True,
            developer=False
        ),
        field(
            "force_disable_vsync",
            Bool,
            added_in=1,
            default="false",
            short_desc="Force Disable VSync",
            long_desc="Forces immediate presentation (no vsync) when supported. On Vulkan this maps to VK_PRESENT_MODE_IMMEDIATE_KHR. Intended for accurate performance measurements.",
            visible=True,
            developer=False
        ),
        field(
            "force_disable_reverse_depth",
            Bool,
            added_in=1,
            default="false",
            short_desc="Force Disable Reverse Z",
            long_desc="When false, reverse-Z (near=1.0, far=0.0) is used whenever the graphics API supports it (native [0,1] clip depth -- Vulkan/Metal always, OpenGL only with glClipControl), for better depth precision. When true, reverse-Z is never used regardless of API support. The effective choice is derived once at device init and queried via erhe::graphics::Device::get_reverse_depth(); it is baked into pipeline depth state, projection matrices and shadow comparison samplers, so all callers must use that single value.",
            visible=True,
            developer=False
        ),
        field(
            "use_draw_list_renderer",
            Bool,
            added_in=1,
            default="false",
            short_desc="Use Draw List Renderer",
            long_desc="Default renderer for new viewports: true selects Draw_list_renderer, false selects Forward_renderer. Each viewport can override this at runtime via Scene View Config.",
            visible=True,
            developer=False
        ),
        field(
            "frame_pacing_enforce",
            Bool,
            added_in=1,
            default="true",
            short_desc="Enforce Frame Pacing Decisions",
            long_desc="When true, the frame pacer's decisions are enforced where the integration supports it; currently the present-wait clamp (FR5): before per-frame work the CPU blocks until the frame from Q*+1 frames ago has been displayed, bounding presented-image queueing and input latency. When false the pacer runs in observer mode only (computes and logs, enforces nothing) - the kill switch required by the frame pacing implementation plan. Effective only on capability tier W (Vulkan present id/wait/timing all available); otherwise pacing is off regardless.",
            visible=True,
            developer=False
        ),
        field(
            "frame_pacing_present_holdback",
            Bool,
            added_in=1,
            default="true",
            short_desc="Hold Present Requests to the Pacer Target",
            long_desc="Mitigation for presentation engines that accept but do not honor target present times (measured on NVIDIA 596.83, see doc/frame_pacing_present_timing_driver_report.md): delay each frame's vkQueuePresentKHR until one refresh period before the pacer's target present time, so the earliest feasible vsync IS the target (claim C15). No effect when frame_pacing_enforce is false or no target is scheduled. Disable for A/B measurements of the mitigation itself.",
            visible=True,
            developer=False
        ),
        field(
            "frame_pacing_tier",
            String,
            added_in=1,
            default='"auto"',
            short_desc="Frame Pacing Capability Tier",
            long_desc="Which frame pacing method to use (doc/frame_pacing_capability_tiers.md). 'auto' resolves the best available tier: W (full pacer; needs present id + wait + timing + calibrated timestamps, Vulkan) else S (slop-servo fallback; needs only plain-FIFO backpressure). 'w' forces tier W (downgrades with a warning if the capabilities are missing), 's' forces the slop-servo fallback, 'off' disables pacing. The resolved tier owns the swapchain configuration: tier W selects fifo_latest_ready, tier S selects plain FIFO with minimum image count (the servo needs backpressure; on latest_ready superseded images drop instead of blocking and the servo winds to nothing, claim C21a). Change requires restart (the swapchain is created from the resolved tier).",
            visible=True,
            developer=False
        ),
        field("opengl", StructRef("Opengl_config"), added_in=1),
        field("vulkan", StructRef("Vulkan_config"), added_in=1),
    ],
)
