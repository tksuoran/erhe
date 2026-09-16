from erhe_codegen import *

# Per-user session state, saved to config/editor/user_state.json (and to
# openxr_user_state.json under OpenXR) by Editor_settings_store, next to the
# editor settings it used to be part of.
#
# What belongs here is state the user builds up by USING the editor - the
# inventory / hotbar slot contents and the per scene view scene / camera /
# visual style selections - as opposed to the tuning knobs of
# Editor_settings_config, which the Settings window edits.
struct("User_state_config",
    version=1,
    short_desc="User state",
    long_desc="Per-user editor state saved to user_state.json.",
    developer=False,
    fields=[
        field("inventory",   StructRef("Inventory_config"),           added_in=1),
        field("scene_views", Vector(StructRef("Scene_view_settings")), added_in=1),
    ],
)
