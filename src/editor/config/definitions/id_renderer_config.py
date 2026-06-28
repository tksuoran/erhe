from erhe_codegen import *

struct("Id_renderer_config",
    version=2,
    short_desc="ID Renderer",
    long_desc="",
    developer=True,
    fields=[
        field(
            "enabled",
            Bool,
            added_in=1,
            default="false",
            short_desc="Enable ID Renderer",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "box_select_use_compute",
            Bool,
            added_in=2,
            default="true",
            short_desc="Box/Paint Select Uses GPU Compute",
            long_desc="When on, box / paint face selection scans the id buffer with two GPU compute passes (gather into a bitmask, then compact to a dense id vector). When off, it falls back to the per-pixel CPU readback + dedup path. Only applies on devices that support compute shaders; the CPU path is always used otherwise. Use this to A/B test the two paths.",
            visible=True,
            developer=False
        ),
    ],
)
