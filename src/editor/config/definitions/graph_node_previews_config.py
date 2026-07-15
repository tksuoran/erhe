from erhe_codegen import *

# Geometry graph node preview thumbnails: editor-global visibility and
# auto-rotation, persisted in Editor_settings_config (graph_node_previews).
# Replaces the old session-only per-Graph_mesh "Show node previews" toggle;
# turning previews on forces a full background re-evaluation of every graph
# so clean nodes get preview primitives too. Auto-rotation spins a hovered
# node's preview; it pauses while the preview is being drag-rotated
# (arcball) and resumes after.
struct("Graph_node_previews_config",
    version=1,
    short_desc="Graph Node Previews",
    long_desc="Geometry graph per-node mesh preview thumbnails",
    developer=False,
    fields=[
        field("enabled",     Bool, added_in=1, default="true", short_desc="Show Node Previews", long_desc="Show per-node mesh preview thumbnails on the geometry graph canvas."),
        field("auto_rotate", Bool, added_in=1, default="true", short_desc="Auto-Rotate",        long_desc="Spin the preview while the pointer hovers the node. Paused while drag-rotating a preview."),
    ],
)
