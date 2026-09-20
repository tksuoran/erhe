from erhe_codegen import *

# Controls what the zoom input (mouse wheel) does in a view with an orthogonal
# camera: 'size_only' changes the size of the view volume around the view
# center; 'size_and_pan' also pans the camera so that the point under the
# pointer stays under the pointer.
enum("Ortho_zoom_mode",
    value("size_only",    0, short_desc="Size only"),
    value("size_and_pan", 1, short_desc="Size and pan"),
    underlying_type=UInt,
)
