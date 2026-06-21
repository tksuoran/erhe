from erhe_codegen import *

struct("Add_lights_args",
    version=2,
    short_desc="Args for scene.add_lights",
    long_desc="Per-invocation argument block consumed by scene.add_lights. Configures the directional, spot and point lights placed by the default scene-light setup.",
    developer=False,
    fields=[
        field(
            "directional_light_intensity",
            Float,
            added_in=1,
            default="20.0f",
            short_desc="Directional Light Intensity",
            long_desc="Total intensity divided across the directional lights.",
            visible=True,
            developer=False
        ),
        field(
            "directional_light_radius",
            Float,
            added_in=1,
            default="6.0f",
            short_desc="Directional Light Layout Radius",
            long_desc="Radius of the circle on which directional lights are arranged.",
            visible=True,
            developer=False
        ),
        field(
            "directional_light_height",
            Float,
            added_in=1,
            default="10.0f",
            short_desc="Directional Light Layout Height",
            long_desc="Y position of the directional-light circle.",
            visible=True,
            developer=False
        ),
        field(
            "directional_light_shadow_count",
            Int,
            added_in=1,
            default="4",
            short_desc="Directional Light Shadow Count",
            long_desc="Number of shadow-casting directional lights to create. Clamped to the active graphics preset's shadow_light_count.",
            visible=True,
            developer=False
        ),
        field(
            "directional_light_no_shadow_count",
            Int,
            added_in=1,
            default="0",
            short_desc="Directional Light No-Shadow Count",
            long_desc="Number of additional directional lights to create that do not cast shadows.",
            visible=True,
            developer=False
        ),
        field(
            "spot_light_intensity",
            Float,
            added_in=1,
            default="150.0f",
            short_desc="Spot Light Intensity",
            long_desc="Per-spot-light intensity.",
            visible=True,
            developer=False
        ),
        field(
            "spot_light_radius",
            Float,
            added_in=1,
            default="20.0f",
            short_desc="Spot Light Layout Radius",
            long_desc="Radius of the circle on which spot lights are arranged.",
            visible=True,
            developer=False
        ),
        field(
            "spot_light_height",
            Float,
            added_in=1,
            default="10.0f",
            short_desc="Spot Light Layout Height",
            long_desc="Y position of the spot-light circle.",
            visible=True,
            developer=False
        ),
        # Replaced in v2 by spot_light_shadow_count / spot_light_no_shadow_count.
        # Kept as a removed field so the v1 schema still round-trips; the v2
        # struct does not contain it.
        field(
            "spot_light_count",
            Int,
            added_in=1,
            removed_in=2,
            default="3",
            short_desc="Spot Light Count",
            long_desc="Number of spot lights to create.",
            visible=True,
            developer=False
        ),
        field(
            "spot_light_shadow_count",
            Int,
            added_in=2,
            default="3",
            short_desc="Spot Light Shadow Count",
            long_desc="Number of shadow-casting spot lights to create. Shares the active graphics preset's shadow_light_count 2D shadow-map budget with directional lights; clamped to the remaining budget after directional shadow lights.",
            visible=True,
            developer=False
        ),
        field(
            "spot_light_no_shadow_count",
            Int,
            added_in=2,
            default="0",
            short_desc="Spot Light No-Shadow Count",
            long_desc="Number of additional spot lights to create that do not cast shadows.",
            visible=True,
            developer=False
        ),
        field(
            "point_light_intensity",
            Float,
            added_in=2,
            default="150.0f",
            short_desc="Point Light Intensity",
            long_desc="Per-point-light intensity.",
            visible=True,
            developer=False
        ),
        field(
            "point_light_radius",
            Float,
            added_in=2,
            default="15.0f",
            short_desc="Point Light Layout Radius",
            long_desc="Radius of the circle on which point lights are arranged.",
            visible=True,
            developer=False
        ),
        field(
            "point_light_height",
            Float,
            added_in=2,
            default="8.0f",
            short_desc="Point Light Layout Height",
            long_desc="Y position of the point-light circle.",
            visible=True,
            developer=False
        ),
        field(
            "point_light_shadow_count",
            Int,
            added_in=2,
            default="0",
            short_desc="Point Light Shadow Count",
            long_desc="Number of shadow-casting point lights to create. Clamped to the active graphics preset's point_shadow_light_count (the cube-map shadow array capacity).",
            visible=True,
            developer=False
        ),
        field(
            "point_light_no_shadow_count",
            Int,
            added_in=2,
            default="0",
            short_desc="Point Light No-Shadow Count",
            long_desc="Number of additional point lights to create that do not cast shadows.",
            visible=True,
            developer=False
        ),
    ],
)
