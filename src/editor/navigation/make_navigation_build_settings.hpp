#pragma once

#include "navigation/navigation_build_settings.hpp"

struct Navigation_settings; // generated into the global namespace by erhe_codegen

namespace editor {

[[nodiscard]] auto make_navigation_build_settings(const Navigation_settings& settings) -> Navigation_build_settings;

} // namespace editor
