from erhe_codegen import *

struct("Threading_config",
    version=4,
    short_desc="",
    long_desc="",
    developer=False,
    fields=[
        field(
            "thread_count",
            Int,
            added_in=1,
            default="8",
            short_desc="",
            long_desc="",
            visible=True,
            developer=False
        ),
        # doc/editor/editor.md "Frame phases and the render thread"
        field(
            "render_thread",
            EnumRef("Render_thread_mode"),
            added_in=2,
            default="Render_thread_mode::dedicated_thread",
            short_desc="Render Thread",
            long_desc="Where the render phase of each frame runs: inline on the main thread, or on a dedicated render thread. Read at startup; ERHE_RENDER_THREAD=0|1 overrides it. The OpenGL and null graphics backends always run inline.",
            visible=True,
            developer=False
        ),
        # doc/editor/render_thread.md section 2.5
        field(
            "imgui_overlap",
            Bool,
            added_in=3,
            default="true",
            short_desc="ImGui Overlap",
            long_desc="With a dedicated render thread, draw the editor windows while the render thread renders the same frame's scene (the ImGui phase runs after the kick). Off keeps the serial order for bisecting. Read at startup; ERHE_IMGUI_OVERLAP=0|1 overrides it.",
            visible=True,
            developer=False
        ),
        # doc/editor/editor.md "Frame phases and the render thread"
        field(
            "frame_pipelining",
            Bool,
            added_in=4,
            default="true",
            short_desc="Frame Pipelining",
            long_desc="With a dedicated render thread, prepare the next frame on the main thread while the render thread renders the previous one: the main thread waits for the render thread only when it hands over the next frame. Off joins the render phase before each prepare phase (the serial order) for bisecting. Read at startup; ERHE_FRAME_PIPELINING=0|1 overrides it. Off with an OpenXR headset.",
            visible=True,
            developer=False
        ),
    ],
)
