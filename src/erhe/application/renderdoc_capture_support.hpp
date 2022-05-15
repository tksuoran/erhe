#pragma once

#include "erhe/application/renderdoc_app.h"

namespace erhe::application {

void initialize_renderdoc_capture_support();
auto get_renderdoc_api() -> RENDERDOC_API_1_4_0*;

}
