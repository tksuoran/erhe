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
        field("opengl", StructRef("Opengl_config"), added_in=1),
        field("vulkan", StructRef("Vulkan_config"), added_in=1),
    ],
)
