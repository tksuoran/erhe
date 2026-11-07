from erhe_codegen import *

struct("Hud_config",
    reflect=True,
    version=2,
    short_desc="HUD",
    long_desc="HUD is intended for Virtual Reality - It can show ImGui UI in VR surface",
    developer=True,
    fields=[
        field(
            "enabled",
            Bool,
            added_in=1,
            default="false",
            short_desc="",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "show",
            Bool,
            added_in=1,
            default="false",
            short_desc="",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "locked",
            Bool,
            added_in=1,
            default="false",
            short_desc="",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "width",
            Int,
            added_in=1,
            default="1024",
            short_desc="",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "height",
            Int,
            added_in=1,
            default="1024",
            short_desc="",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "ppm",
            Float,
            added_in=1,
            default="5000.0f",
            short_desc="",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "x",
            Float,
            added_in=1,
            default="-0.09f",
            short_desc="Control Offset X",
            long_desc="Offset in meters of the Hud from the control pose when shown without a head pose (desktop): X along the controller frame.",
            visible=True,
            developer=False
        ),
        field(
            "y",
            Float,
            added_in=1,
            default="0.0f",
            short_desc="Control Offset Y",
            long_desc="Offset in meters of the Hud from the control pose when shown without a head pose (desktop): Y along the controller frame.",
            visible=True,
            developer=False
        ),
        field(
            "z",
            Float,
            added_in=1,
            default="-0.38f",
            short_desc="Control Offset Z",
            long_desc="Offset in meters of the Hud from the control pose when shown without a head pose (desktop): Z along the controller frame.",
            visible=True,
            developer=False
        ),
        field(
            "summon_distance",
            Float,
            added_in=1,
            default="0.7f",
            short_desc="Summon Distance",
            long_desc="Distance in meters from the head at which showing the HUD (toggle, long press) places it, along the current view direction.",
            visible=True,
            developer=False,
            ui_min="0.1f",
            ui_max="3.0f"
        ),
        field(
            "summon_offset_x",
            Float,
            added_in=2,
            default="0.0f",
            short_desc="Summon Offset X",
            long_desc="Offset in meters, in view space, of the HUD from its summon point: positive to the right of the view direction.",
            visible=True,
            developer=False,
            ui_min="-1.0f",
            ui_max="1.0f"
        ),
        field(
            "summon_offset_y",
            Float,
            added_in=2,
            default="0.0f",
            short_desc="Summon Offset Y",
            long_desc="Offset in meters, in view space, of the HUD from its summon point: positive up.",
            visible=True,
            developer=False,
            ui_min="-1.0f",
            ui_max="1.0f"
        ),
        field(
            "summon_offset_z",
            Float,
            added_in=2,
            default="0.0f",
            short_desc="Summon Offset Z",
            long_desc="Offset in meters, in view space, of the HUD from its summon point: positive toward the user (closer than the summon distance).",
            visible=True,
            developer=False,
            ui_min="-1.0f",
            ui_max="1.0f"
        ),
    ],
)
