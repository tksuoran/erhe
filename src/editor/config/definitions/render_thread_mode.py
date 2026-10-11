from erhe_codegen import *

# Where the editor's render phase (R) runs (doc/editor/render_thread.md
# section 2.5, doc/editor/editor.md "Frame phases and the render thread").
# inline_main_thread runs the three frame phases back to back on the main
# thread; dedicated_thread runs R on its own thread, handed off through
# Render_thread::kick / join. The mode is fixed for the session; the
# environment variable ERHE_RENDER_THREAD=0|1 overrides the setting.
enum("Render_thread_mode",
    value("inline_main_thread", 0, short_desc="Inline (render phase on the main thread)"),
    value("dedicated_thread",   1, short_desc="Dedicated render thread"),
    underlying_type=UInt,
)
