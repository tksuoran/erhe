from erhe_codegen import *

struct("Physics_config",
    reflect=True,
    version=1,
    short_desc="Physics",
    long_desc="",
    developer=False,
    fields=[
        field(
            "static_enable",
            Bool,
            added_in=1,
            default="true",
            short_desc="Static Enable",
            long_desc="Changing value required editor restart. If not enabled, dynamic enable is not possible",
            visible=True,
            developer=False
        ),
        field(
            "dynamic_enable",
            Bool,
            added_in=1,
            default="true",
            short_desc="Dynamic Enable",
            long_desc="Value can be changed at runtime",
            visible=True,
            developer=False
        ),
        field(
            "debug_draw",
            Bool,
            added_in=1,
            default="false",
            short_desc="Debug Draw",
            long_desc="Enable physics debug visualization",
            visible=True,
            developer=False
        ),
        field(
            "wind_enable",
            Bool,
            added_in=1,
            default="false",
            short_desc="Wind Enable",
            long_desc="Apply wind forces to wind-receptive rigid bodies (Node_physics wind_receptivity > 0) each fixed step",
            visible=True,
            developer=False
        ),
        field(
            "wind_direction",
            Vec3,
            added_in=1,
            default="glm::vec3{1.0f, 0.0f, 0.0f}",
            short_desc="Wind Direction",
            long_desc="World space wind direction (normalized before use; zero vector disables wind)",
            visible=True,
            developer=False
        ),
        field(
            "wind_speed",
            Float,
            added_in=1,
            default="3.0f",
            short_desc="Wind Speed",
            long_desc="Base wind speed in m/s",
            visible=True,
            developer=False
        ),
        field(
            "wind_gust_amplitude",
            Float,
            added_in=1,
            default="1.5f",
            short_desc="Gust Amplitude",
            long_desc="Gust wind speed variation amplitude in m/s, added on top of the base speed",
            visible=True,
            developer=False
        ),
        field(
            "wind_gust_frequency",
            Float,
            added_in=1,
            default="0.4f",
            short_desc="Gust Frequency",
            long_desc="Gust variation frequency in Hz",
            visible=True,
            developer=False
        ),
        field(
            "wind_turbulence",
            Float,
            added_in=1,
            default="0.3f",
            short_desc="Turbulence",
            long_desc="Fraction [0, 1] of the local wind speed applied as an incoherent lateral / vertical component",
            visible=True,
            developer=False
        ),
        field(
            "wind_wavelength",
            Float,
            added_in=1,
            default="8.0f",
            short_desc="Gust Wavelength",
            long_desc="Spatial wavelength in meters of the traveling gust wave; nearby plants move together, distant ones out of phase",
            visible=True,
            developer=False
        ),
    ],
)
