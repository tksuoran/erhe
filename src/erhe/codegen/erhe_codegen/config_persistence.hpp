#pragma once

namespace erhe::codegen {

// Process-wide persistence policy for configuration files.
//
// - read_write: save_config() writes (the default).
// - read_only:  save_config() writes nothing and returns false; load_config()
//   is unaffected. Other config writers that are not save_config() (such as the
//   ImGui layout .ini files of erhe::imgui::Imgui_host) honor it as well.
//
// Set once at application startup, before any thread that loads or saves
// configuration is started; it is not meant to change afterwards.
enum class Config_persistence : unsigned int
{
    read_write = 0,
    read_only
};

void set_config_persistence(Config_persistence persistence);
[[nodiscard]] auto get_config_persistence() -> Config_persistence;

} // namespace erhe::codegen
