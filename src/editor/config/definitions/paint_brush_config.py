from erhe_codegen import *

# The Paint tool's Brush mode values (doc/editor/tools.md "Paint tool brush").
struct("Paint_brush_config",
    version=1,
    short_desc="Paint Brush",
    long_desc="The Paint tool's Brush mode: what a stroke paints, the brush sphere and the blend.",
    developer=False,
    fields=[
        field("target_kind", EnumRef("Paint_brush_target"),  added_in=1, default="Paint_brush_target::weight", short_desc="Brush Paints",  long_desc="Weight paints one channel toward Target; Color paints all four channels toward the selected palette color."),
        field("radius",      Float,                          added_in=1, default="0.5f",                       short_desc="Brush Radius",  long_desc="Meters: the radius of the brush sphere about the surface point under the pointer (0.01 - 100)."),
        field("strength",    Float,                          added_in=1, default="1.0f",                       short_desc="Strength",      long_desc="The brush alpha at its center (0 - 1)."),
        field("target",      Float,                          added_in=1, default="1.0f",                       short_desc="Target",        long_desc="The value a Weight stroke paints toward (0 - 1)."),
        field("blend",       EnumRef("Paint_brush_blend"),   added_in=1, default="Paint_brush_blend::mix",     short_desc="Blend",         long_desc="Mix, add or subtract."),
        field("channel",     EnumRef("Paint_brush_channel"), added_in=1, default="Paint_brush_channel::r",     short_desc="Channel",       long_desc="The channel a Weight stroke paints on a material with detail blend off; a material with detail blend on gives its weight channel."),
    ],
)
