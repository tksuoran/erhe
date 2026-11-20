from erhe_codegen import *

struct("Threading_config",
    version=6,
    short_desc="",
    long_desc="",
    developer=False,
    fields=[
        # doc/editor/editor.md "Worker executors"
        field(
            "thread_count",
            Int,
            added_in=1,
            default="8",
            short_desc="Foreground Thread Count",
            long_desc="Worker threads of the foreground executor, which runs the work the user waits for and the work of editor startup. Read at startup; 0 or less is one worker per hardware thread.",
            visible=True,
            developer=False
        ),
        # doc/editor/editor.md "Worker executors"
        field(
            "background_thread_count",
            Int,
            added_in=5,
            default="4",
            short_desc="Background Thread Count",
            long_desc="Worker threads of the background executor, which runs the work nobody waits for; its workers run at lowered priority. Read at startup; 0 or less is one worker per hardware thread.",
            visible=True,
            developer=False
        ),
        # doc/editor/editor.md "Background apply meter"
        field(
            "background_apply_soft_limit_ms",
            Float,
            added_in=6,
            default="2.0f",
            short_desc="Background Apply Soft Limit",
            long_desc="Main-thread milliseconds per frame for applying the results of background work (texture file previews, lightmap tiles, kit thumbnails). A frame over it logs a warning naming each owner and its time, at most once per second; no result is held back. Takes effect at once.",
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
