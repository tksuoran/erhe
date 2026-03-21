from erhe_codegen import *

struct("Graphics_config",
    field(
        "initial_clear",                     
        Bool,
        added_in=1,
        default="true",
        short_desc="Initial Clear",
        long_desc="Submit few empty frames during Startup"
    ),
    field(
        "post_processing",
        Bool,
        added_in=1,
        default="true",
        short_desc="Post Processing",
        long_desc="Enable Post Processing"
    ),
    field(
        "force_bindless_textures_off",
        Bool,
        added_in=1,
        default="false",
        short_desc="Force Disable OpenGL Bindless Textures",
        long_desc="Prevent any use of OpenGL Bindless Textures. Always used when RenderDoc capture is enabled."
    ),
    field(
        "force_no_persistent_buffers",
        Bool,
        added_in=1,
        default="false",
        short_desc="Force Disable OpenGL Persistent Buffers",
        long_desc="Prevent any use of OpenGL presistent buffer. Only meaningful for debugging."
    ),
    field(
        "force_no_direct_state_access",
        Bool,
        added_in=1,
        default="false",
        short_desc="Force Disable OpenGL Direct State Access",
        long_desc="Prevent any use of OpenGL DSA (Direct State Access). Only meaningful for debugging."
    ),
    field(
        "force_emulate_multi_draw_indirect",
        Bool,
        added_in=1,
        default="false",
        short_desc="Force Disable OpenGL Multi Draw Indirect",
        long_desc="Prevent any use of OpenGL MDI (Multi Draw Indirect). Only meaningful for debugging."
    ),
    field(
        "force_gl_version",
        Int,
        added_in=1,
        default="0",
        short_desc="Force OpenGL Version",
        long_desc="Request specific OpenGL version. Only meaningful for debugging."
    ),
    field(
        "force_glsl_version",
        Int,
        added_in=1,
        default="0",
        short_desc="Force OpenGL GLSL Version",
        long_desc="Request specific OpenGL GLSL version. Only meaningful for debugging."
    ),
    field(
        "renderdoc_capture_support",
        Bool,
        added_in=1,
        default="false",
        short_desc="Enable RenderDoc Capture Support",
        long_desc="Enables RenderDoc Capture Support. Disables use of OpenGL Bindless Textures. Only meaningful for debugging."
    ),
    field(
        "shader_monitor_enabled",
        Bool,
        added_in=1,
        default="true",
        short_desc="Enable Shader Monitor",
        long_desc="Enables Shader Monitor, allowing Shader Hot-reloading. Only meaningful for debugging."
    ),
    version=1,
    short_desc="",
    long_desc="",
)
