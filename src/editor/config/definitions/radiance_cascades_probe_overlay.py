from erhe_codegen import *

# What the radiance cascades probe overlay draws (doc/editor/radiance_cascades.md
# "Probe overlay"): nothing, one wire sphere per probe of the chosen cascade
# coloured by its state, or the spheres plus a short line toward +Y coloured
# by the probe's merged irradiance toward +Y.
enum("Radiance_cascades_probe_overlay",
    value("none",                 0, short_desc="None"),
    value("state",                1, short_desc="State (active / inside geometry)"),
    value("state_and_irradiance", 2, short_desc="State + irradiance toward +Y"),
    underlying_type=UInt,
)
