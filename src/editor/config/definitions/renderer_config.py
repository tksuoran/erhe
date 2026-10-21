from erhe_codegen import *

struct("Renderer_config",
    reflect=True,
    version=2,
    short_desc="",
    long_desc="",
    developer=False,
    fields=[
        field(
            "max_material_count",
            Int,
            added_in=1,
            default="1000",
            short_desc="Max Material Count",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "max_light_count",
            Int,
            added_in=1,
            default="1024",
            short_desc="Max Light Count",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "max_camera_count",
            Int,
            added_in=1,
            default="256",
            short_desc="Max Camera Count",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "max_joint_count",
            Int,
            added_in=1,
            default="1000",
            short_desc="Max Skinning Joint Count",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "max_primitive_count",
            Int,
            added_in=1,
            default="6000",
            short_desc="Max Mesh Primitive Counht",
            long_desc="",
            visible=True,
            developer=False
        ),
        field(
            "max_draw_count",
            Int,
            added_in=1,
            default="6000",
            short_desc="Max Draw Count",
            long_desc="",
            visible=True,
            developer=False
        ),
        # The light cluster grid (doc/plans/clustered_lights.md C2-C4).
        field(
            "light_cluster_tile_size",
            Int,
            added_in=2,
            default="64",
            short_desc="Light Cluster Tile Size",
            long_desc="Screen tile edge of the light cluster grid, in pixels",
            visible=True,
            developer=False
        ),
        field(
            "light_cluster_slice_count",
            Int,
            added_in=2,
            default="24",
            short_desc="Light Cluster Depth Slices",
            long_desc="View depth slices of the light cluster grid",
            visible=True,
            developer=False
        ),
        field(
            "light_cluster_capacity",
            Int,
            added_in=2,
            default="64",
            short_desc="Light Cluster Capacity",
            long_desc="Most lights one light cluster lists; a cluster with more keeps its lowest slots and logs a warning",
            visible=True,
            developer=False
        ),
    ],
)
