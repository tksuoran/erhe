from erhe_codegen import *

struct("Inventory_slot",
    version=3,
    short_desc="Inventory Slot",
    long_desc="A single slot in the inventory or hotbar grid",
    developer=False,
    fields=[
        field(
            "tool_name",
            String,
            added_in=1,
            default='""',
            short_desc="Tool name",
            long_desc="Matches Tool::get_description(); empty string means vacant slot",
            visible=True,
            developer=False
        ),
        field(
            "brush_name",
            String,
            added_in=2,
            default='""',
            short_desc="Brush name",
            long_desc="Brush name from content library; non-empty means brush slot",
            visible=True,
            developer=False
        ),
        field(
            "material_name",
            String,
            added_in=3,
            default='""',
            short_desc="Material name",
            long_desc="Material name from content library; non-empty means material slot",
            visible=True,
            developer=False
        ),
    ],
)

struct("Inventory_config",
    version=1,
    short_desc="Inventory",
    long_desc="Inventory and hotbar layout configuration",
    developer=False,
    fields=[
        field(
            "column_count",
            Int,
            added_in=1,
            default="9",
            short_desc="Grid columns",
            long_desc="Number of columns in the main inventory grid",
            visible=True,
            developer=False
        ),
        field(
            "row_count",
            Int,
            added_in=1,
            default="3",
            short_desc="Grid rows",
            long_desc="Number of rows in the main inventory grid",
            visible=True,
            developer=False
        ),
        field(
            "hotbar_slot_count",
            Int,
            added_in=1,
            default="9",
            short_desc="Hotbar slot count",
            long_desc="Number of slots in the hotbar row",
            visible=True,
            developer=False
        ),
        field(
            "grid_slots",
            Vector(StructRef("Inventory_slot")),
            added_in=1,
            short_desc="Grid slots",
            long_desc="Main inventory grid slot contents",
            visible=False,
            developer=False
        ),
        field(
            "hotbar_slots",
            Vector(StructRef("Inventory_slot")),
            added_in=1,
            short_desc="Hotbar slots",
            long_desc="Hotbar row slot contents",
            visible=False,
            developer=False
        ),
    ],
)
