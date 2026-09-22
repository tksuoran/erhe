from erhe_codegen import *

# One property group of the Properties windows (the UI group a registered
# property names in Property_ui::group): whether the user has it open, in
# the user's order. User_state_config keeps these in a vector whose order is
# the order the groups are drawn in; a group the vector does not name is
# closed and appended when it is first drawn.
struct("Property_group_state",
    version=1,
    short_desc="Property group state",
    long_desc="Fold state of one property group of the Properties windows, in the user's group order.",
    developer=False,
    fields=[
        field("name", String, added_in=1, default='""',    short_desc="Group",  long_desc="The Property_ui::group label."),
        field("open", Bool,   added_in=1, default="false", short_desc="Open",   long_desc="Whether the group is expanded."),
    ],
)
