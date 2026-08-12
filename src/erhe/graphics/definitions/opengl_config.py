from erhe_codegen import *

struct("Opengl_config",
    reflect=True,
    version=1,
    short_desc="OpenGL-specific Graphics Settings",
    long_desc="Debug overrides for the OpenGL backend.",
    developer=False,
    fields=[
        field(
            "force_bindless_textures_off",
            Bool,
            added_in=1,
            default="false",
            short_desc="Force Disable OpenGL Bindless Textures",
            long_desc="Prevent any use of OpenGL Bindless Textures. Always used when RenderDoc capture is enabled.",
            visible=True,
            developer=False
        ),
        field(
            "force_no_persistent_buffers",
            Bool,
            added_in=1,
            default="false",
            short_desc="Force Disable OpenGL Persistent Buffers",
            long_desc="Prevent any use of OpenGL presistent buffer. Only meaningful for debugging.",
            visible=True,
            developer=False
        ),
        field(
            "force_no_direct_state_access",
            Bool,
            added_in=1,
            default="false",
            short_desc="Force Disable OpenGL Direct State Access",
            long_desc="Prevent any use of OpenGL DSA (Direct State Access). Only meaningful for debugging.",
            visible=True,
            developer=False
        ),
        field(
            "force_no_clip_control",
            Bool,
            added_in=1,
            default="false",
            short_desc="Force Disable OpenGL Clip Control",
            long_desc="Prevent use of glClipControl. Disables zero-to-one depth range and reverse-Z. Only meaningful for debugging.",
            visible=True,
            developer=False
        ),
        field(
            "force_emulate_multi_draw_indirect",
            Bool,
            added_in=1,
            default="false",
            short_desc="Force Disable OpenGL Multi Draw Indirect",
            long_desc="Prevent any use of OpenGL MDI (Multi Draw Indirect). Only meaningful for debugging.",
            visible=True,
            developer=False
        ),
        field(
            "force_no_compute_shader",
            Bool,
            added_in=1,
            default="false",
            short_desc="Force Disable Compute Shaders",
            long_desc="Prevent any use of compute shaders. Only meaningful for debugging.",
            visible=True,
            developer=False
        ),
        field(
            "force_gl_version",
            Int,
            added_in=1,
            default="0",
            short_desc="Force OpenGL Version",
            long_desc="Request specific OpenGL version. Only meaningful for debugging.",
            visible=True,
            developer=False
        ),
        field(
            "force_glsl_version",
            Int,
            added_in=1,
            default="0",
            short_desc="Force OpenGL GLSL Version",
            long_desc="Request specific OpenGL GLSL version. Only meaningful for debugging.",
            visible=True,
            developer=False
        ),
    ],
)
