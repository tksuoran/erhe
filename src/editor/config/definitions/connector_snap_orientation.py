from erhe_codegen import *

# Whether a connector snap of a translate drag also turns the moved nodes so the
# connector frames face each other (doc/editor/level_tools.md "Connector snap").
enum("Connector_snap_orientation",
    value("keep",  0, short_desc="Keep: the snap translates only"),
    value("align", 1, short_desc="Align: the snap also turns the frames to face each other"),
    underlying_type=UInt,
)
