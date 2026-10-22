from erhe_codegen import *

# Specular anti-aliasing of the anisotropic BRDF (doc/erhe/scene_renderer.md
# "Specular anti-aliasing"); the value is erhe::scene_renderer::
# Specular_anti_aliasing, the SPECULAR_ANTI_ALIASING shader variant axis.
#   off               = no kernel roughness.
#   normal_kernel     = the kernel from the shading normal's screen space
#                       derivatives, once per fragment (Tokuyoshi-Kaplanyan,
#                       as Filament). The default.
#   finite_difference = the kernel from the half vector's change toward the
#                       x and y neighbours, per light, from derivatives taken
#                       once per fragment.
enum("Specular_anti_aliasing_mode",
    value("off",               0, short_desc="Off"),
    value("normal_kernel",     1, short_desc="Normal kernel (per fragment)"),
    value("finite_difference", 2, short_desc="Finite difference (per light)"),
    underlying_type=UInt,
)
