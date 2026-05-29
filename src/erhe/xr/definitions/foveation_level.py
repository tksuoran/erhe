from erhe_codegen import *

# Maps 1:1 to XrFoveationLevelFB (XR_FOVEATION_LEVEL_NONE_FB = 0, LOW = 1,
# MEDIUM = 2, HIGH = 3) for OpenXR fixed foveated rendering.
enum("Foveation_level",
    value("e_none",   0, short_desc="No foveation (full resolution)"),
    value("e_low",    1, short_desc="Low foveation"),
    value("e_medium", 2, short_desc="Medium foveation"),
    value("e_high",   3, short_desc="High foveation"),
)
