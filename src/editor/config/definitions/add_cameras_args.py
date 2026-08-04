from erhe_codegen import *

struct("Add_cameras_args",
    version=2,
    short_desc="Args for scene.add_cameras",
    long_desc="Per-invocation argument block consumed by scene.add_cameras. Drives default camera placement and per-camera exposure / shadow range.",
    developer=False,
    fields=[
        field(
            "camera_exposure",
            Float,
            added_in=1,
            default="1.0f",
            short_desc="Camera Exposure",
            long_desc="Per-camera exposure multiplier applied at camera construction.",
            visible=True,
            developer=False
        ),
        field(
            "shadow_range",
            Float,
            added_in=1,
            default="22.0f",
            short_desc="Shadow Range",
            long_desc="Per-camera shadow range applied at camera construction.",
            visible=True,
            developer=False
        ),
        field(
            "camera_distance",
            Float,
            added_in=1,
            default="3.0f",
            short_desc="Camera Distance (m)",
            long_desc="Distance from origin along z for the default Camera A.",
            visible=True,
            developer=False
        ),
        field(
            "camera_elevation",
            Float,
            added_in=1,
            default="1.6f",
            short_desc="Camera Elevation (m)",
            long_desc="Y position for the default Camera A.",
            visible=True,
            developer=False
        ),
        field(
            "z_near",
            Float,
            added_in=2,
            default="0.03f",
            short_desc="Near Plane (m)",
            long_desc="Perspective projection near clip plane distance applied at camera construction.",
            visible=True,
            developer=False
        ),
        field(
            "z_far",
            Float,
            added_in=2,
            default="64.0f",
            short_desc="Far Plane (m)",
            long_desc="Perspective projection far clip plane distance applied at camera construction.",
            visible=True,
            developer=False
        ),
    ],
)
