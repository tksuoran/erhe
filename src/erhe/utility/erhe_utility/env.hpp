#pragma once

namespace erhe::utility {

// Set an environment variable in the current process. On Windows uses
// _putenv_s; on POSIX uses setenv with overwrite=1. Returns true on
// success, false on failure -- callers may want to log the failure.
//
// This must be called before any code in the process reads the variable
// it sets. In particular: layer-loader env vars (Vulkan VK_LOADER_*,
// OpenXR XR_API_LAYER_*, XR_LOADER_DEBUG, etc.) must be set before the
// loader is invoked, which means before xrCreateInstance /
// vkCreateInstance.
[[nodiscard]] bool set_env(const char* key, const char* value);

} // namespace erhe::utility
