#pragma once

#include "config/generated/viewport_config.hpp"

struct Viewport_config_data;

namespace editor {

[[nodiscard]] auto make_viewport_config(const Viewport_config_data& viewport_config_data) -> Viewport_config;

}
