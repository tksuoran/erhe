#pragma once

#include <renderdoc_app.h>

namespace editor {

void initialize_renderdoc_capture_support();
auto get_renderdoc_api() -> RENDERDOC_API_1_1_2*;

}
