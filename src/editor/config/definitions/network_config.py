from erhe_codegen import *

struct("Network_config",
    field("upstream_address",   String, added_in=1, default='"127.0.0.1"'),
    field("upstream_port",      Int,    added_in=1, default="34567"),
    field("downstream_address", String, added_in=1, default='"0.0.0.0"'),
    field("downstream_port",    Int,    added_in=1, default="34567"),
    version=1,
)
