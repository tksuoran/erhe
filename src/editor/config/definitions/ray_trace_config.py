from erhe_codegen import *

struct("Ray_trace_config",
    version=1,
    short_desc="Ray Trace",
    long_desc="GPU ray tracing (ray query) renderer settings.",
    developer=True,
    fields=[
        field(
            "downscale",
            Float,
            added_in=1,
            default="1.0f",
            short_desc="Downscale",
            long_desc="Ray traced output downscale factor (1.0 .. 8.0). 1.0 traces one ray per viewport pixel; 2.0 traces at half resolution in each dimension (a quarter of the rays) and each traced pixel covers 2x2 viewport pixels. Integer values display with nearest-neighbor magnification (crisp pixel blocks); fractional values use linear filtering.",
            visible=True,
            developer=False
        ),
        field(
            "max_rays",
            Int,
            added_in=1,
            default="24",
            short_desc="Max Rays",
            long_desc="Per-pixel traced-ray budget for the Whitted branching loop (transmissive interfaces branch into reflection + refraction). Higher values follow more branches through nested glass at higher cost.",
            visible=True,
            developer=False
        ),
        field(
            "max_bounces",
            Int,
            added_in=1,
            default="8",
            short_desc="Max Bounces",
            long_desc="Maximum transmissive interface interactions along one branch's history. Clamped to the shader's compile-time stack bound (12).",
            visible=True,
            developer=False
        ),
    ],
)
