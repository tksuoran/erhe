from erhe_codegen import *

struct("Lightmap_config",
    version=1,
    short_desc="Lightmap",
    long_desc="Lightmap baking settings (doc/lightmap_baking_plan.md). Minimal by design: texel density is the one quality knob; everything else is fixed defaults.",
    developer=False,
    fields=[
        field(
            "texels_per_meter",
            Float,
            added_in=1,
            default="16.0f",
            short_desc="Texels per meter",
            long_desc="Lightmap texel density: lightmap texels per world-space meter. Sets each lightmapped instance's atlas region size (surface area x density). The single user-facing quality knob.",
            visible=True,
            developer=False
        ),
        field(
            "hard_angles_deg",
            Float,
            added_in=1,
            default="60.0f",
            short_desc="UV chart hard angle (deg)",
            long_desc="Automatic lightmap UV unwrap: dihedral angle above which an edge becomes a chart boundary. Fixed default; exposed for developers only.",
            visible=True,
            developer=True
        ),
    ],
)
