from erhe_codegen import *

# Settings for the sky composition pass fragment shader (sky.frag).
# Defaults match the previously hardcoded shader values.
struct("Sky_config",
    version=1,
    short_desc="Sky",
    long_desc="Sky shader settings",
    developer=False,
    fields=[
        field("sky_horizon_color",    Vec4,  added_in=1, default="0.3f, 0.3f, 0.33f, 1.0f", short_desc="Sky Horizon Color"),
        field("sky_zenith_color",     Vec4,  added_in=1, default="0.2f, 0.2f, 0.22f, 1.0f", short_desc="Sky Zenith Color"),
        field("sky_power",            Float, added_in=1, default="10.0f", short_desc="Sky Power", long_desc="Falloff exponent from horizon to zenith"),
        field("ground_horizon_color", Vec4,  added_in=1, default="0.2f, 0.2f, 0.2f, 1.0f",  short_desc="Ground Horizon Color"),
        field("ground_nadir_color",   Vec4,  added_in=1, default="0.1f, 0.1f, 0.1f, 1.0f",  short_desc="Ground Nadir Color"),
        field("ground_power",         Float, added_in=1, default="8.0f",  short_desc="Ground Power", long_desc="Falloff exponent from horizon to nadir"),
        field("checker_frequency",    Vec2,  added_in=1, default="18.0f, 18.0f", short_desc="Checker Frequency", long_desc="Checkerboard frequency in heading and elevation"),
        field("checker_intensity_a",  Float, added_in=1, default="0.92f", short_desc="Checker Intensity A"),
        field("checker_intensity_b",  Float, added_in=1, default="1.0f",  short_desc="Checker Intensity B"),
    ],
)
