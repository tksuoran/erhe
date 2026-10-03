from erhe_codegen import *

struct("Opengl_config",
    reflect=True,
    version=2,
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
            "force_emulate_multi_draw_indirect",
            Bool,
            added_in=1,
            default="false",
            short_desc="Force Disable OpenGL Multi Draw Indirect",
            long_desc="Prevent any use of OpenGL MDI (Multi Draw Indirect). Only meaningful for debugging.",
            visible=True,
            developer=False
        ),
    ],
)
