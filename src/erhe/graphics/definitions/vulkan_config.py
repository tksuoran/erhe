from erhe_codegen import *

struct("Vulkan_config",
    reflect=True,
    version=1,
    short_desc="Vulkan-specific Graphics Settings",
    long_desc="Debug overrides for the Vulkan backend.",
    developer=False,
    fields=[
        field(
            "disable_ray_tracing",
            Bool,
            added_in=1,
            default="false",
            short_desc="Disable Vulkan Ray Tracing",
            long_desc="Skips enabling VK_KHR_acceleration_structure / VK_KHR_ray_query / VK_KHR_deferred_host_operations / VK_KHR_ray_tracing_position_fetch on the Vulkan device, so Device_info::use_ray_query stays false and every GPU ray tracing consumer (ray trace renderer, lightmap baker) reports unsupported. Workaround for drivers whose ray tracing implementation is broken (e.g. crashes observed on AMD integrated graphics). Change requires restart.",
            visible=True,
            developer=False
        ),
        field(
            "vulkan_validation_layers",
            Bool,
            added_in=1,
            default="true",
            short_desc="Enable Vulkan Validation Layers",
            long_desc="Enables Vulkan validation layers (VK_LAYER_KHRONOS_validation). Only meaningful for Vulkan backend.",
            visible=True,
            developer=False
        ),
        field(
            "use_kosmickrisp",
            Bool,
            added_in=1,
            default="false",
            short_desc="Use KosmicKrisp Vulkan driver (macOS)",
            long_desc="On macOS, point VK_DRIVER_FILES at $VULKAN_SDK/share/vulkan/icd.d/libkosmickrisp_icd.json so the Vulkan loader uses the KosmicKrisp ICD (Vulkan-on-Metal, Apple Silicon + macOS 26+) instead of MoltenVK. No effect on other platforms.",
            visible=True,
            developer=False
        ),
        field(
            "use_moltenvk",
            Bool,
            added_in=1,
            default="false",
            short_desc="Use MoltenVK Vulkan driver (macOS)",
            long_desc="On macOS, point VK_DRIVER_FILES at MoltenVK_icd.json (preferring the Vulkan SDK, falling back to /usr/local/share/vulkan/icd.d) so the Vulkan loader uses the MoltenVK ICD. Useful when KosmicKrisp is also installed, since the loader would otherwise enumerate both and may pick KosmicKrisp. Takes precedence over use_kosmickrisp. No effect on other platforms.",
            visible=True,
            developer=False
        ),
    ],
)
