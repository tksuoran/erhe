#pragma once

#include <memory>

namespace editor {

class Content_library;
class Style;

// The style item the default metals share (doc/style_library.md D5):
// roughness, metallic, BxDF model and the brushed metal flags.
[[nodiscard]] auto make_brushed_metal_style() -> std::shared_ptr<Style>;

void add_default_materials        (Content_library& library);
void add_default_physics_materials(Content_library& library);

}
