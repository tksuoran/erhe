from erhe_codegen import *

struct("Vulkan_config",
    version=1,
    short_desc="Vulkan-specific Graphics Settings",
    long_desc="Debug overrides for the Vulkan backend.",
    developer=False,
    fields=[
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
    ],
)
