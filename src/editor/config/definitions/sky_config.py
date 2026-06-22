from erhe_codegen import *

# Settings for the sky composition pass fragment shader (sky.frag) and the
# physically-based atmosphere (sky_atmosphere.frag).
# Defaults match the previously hardcoded shader values.
struct("Sky_config",
    version=3,
    short_desc="Sky",
    long_desc="Sky shader settings",
    developer=False,
    fields=[
        field(
            "enabled",
            Bool,
            added_in=2,
            default="true",
            short_desc="Enable Sky",
            long_desc=("Render the sky background. When disabled in VR with "
                       "camera passthrough available, the scene background "
                       "shows the room around the scene content (mixed "
                       "reality)."),
        ),
        # 0 = gradient/checker sky, 1 = physically-based atmosphere (Hillaire).
        # Atmosphere mode requires storage-image compute (Vulkan, or OpenGL
        # 4.3+); without it the gradient sky is used regardless of this value.
        field("mode",                 Int,   added_in=3, default="0",     short_desc="Sky Mode", long_desc="0 = gradient / checker, 1 = physically-based atmosphere (requires Vulkan, or OpenGL 4.3+)"),
        field("sky_horizon_color",    Vec4,  added_in=1, default="0.3f, 0.3f, 0.33f, 1.0f", short_desc="Sky Horizon Color"),
        field("sky_zenith_color",     Vec4,  added_in=1, default="0.2f, 0.2f, 0.22f, 1.0f", short_desc="Sky Zenith Color"),
        field("sky_power",            Float, added_in=1, default="10.0f", short_desc="Sky Power", long_desc="Falloff exponent from horizon to zenith"),
        field("ground_horizon_color", Vec4,  added_in=1, default="0.2f, 0.2f, 0.2f, 1.0f",  short_desc="Ground Horizon Color"),
        field("ground_nadir_color",   Vec4,  added_in=1, default="0.1f, 0.1f, 0.1f, 1.0f",  short_desc="Ground Nadir Color"),
        field("ground_power",         Float, added_in=1, default="8.0f",  short_desc="Ground Power", long_desc="Falloff exponent from horizon to nadir"),
        field("checker_frequency",    Vec2,  added_in=1, default="18.0f, 18.0f", short_desc="Checker Frequency", long_desc="Checkerboard frequency in heading and elevation"),
        field("checker_intensity_a",  Float, added_in=1, default="0.92f", short_desc="Checker Intensity A"),
        field("checker_intensity_b",  Float, added_in=1, default="1.0f",  short_desc="Checker Intensity B"),
        # Atmosphere (mode == 1) parameters.
        field("sun_intensity",          Float, added_in=3, default="20.0f", short_desc="Sun Intensity", long_desc="Atmosphere sun illuminance"),
        field("march_steps",            Int,   added_in=3, default="32",    short_desc="Atmosphere Steps", long_desc="Ray-march step count for the atmosphere"),
        field("observer_altitude_km",   Float, added_in=3, default="0.5f",  short_desc="Observer Altitude (km)", long_desc="Virtual observer altitude above sea level"),
        field("sun_angular_radius_deg", Float, added_in=3, default="0.5f",  short_desc="Sun Angular Radius (deg)"),
        field("sun_disc_intensity",     Float, added_in=3, default="30.0f", short_desc="Sun Disc Intensity"),
        field("sun_elevation_deg",      Float, added_in=3, default="45.0f", short_desc="Sun Elevation (deg)", long_desc="Used when the scene has no directional light"),
        field("sun_azimuth_deg",        Float, added_in=3, default="0.0f",  short_desc="Sun Azimuth (deg)", long_desc="Used when the scene has no directional light"),
    ],
)
