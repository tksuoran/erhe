from erhe_codegen import *

# Light transport of the indirect diffuse probe traces (Ddgi_config::bounces,
# Radiance_cascades_config::bounces; doc/editor/ddgi.md "Bounces"): a traced
# hit is shaded with its direct light and the flat scene ambient (single),
# or additionally with the producer's previous field sampled at the hit
# (multi), so light bounces once more every update and the field converges
# to the infinite-bounce solution.
enum("Indirect_diffuse_bounces",
    value("single", 0, short_desc="Single (direct light at hits)"),
    value("multi",  1, short_desc="Multi (previous field at hits)"),
    underlying_type=UInt,
)
