from erhe_codegen import *

struct("Metal_config",
    version=1,
    short_desc="Metal-specific Graphics Settings",
    long_desc="Debug overrides for the Metal backend.",
    developer=False,
    fields=[
        field(
            "disable_gpu_frame_capture",
            Bool,
            added_in=1,
            default="false",
            short_desc="Disable Xcode GPU Frame Capture Layer",
            long_desc="Prevents Xcode's GPU frame-capture layer (GPUToolsCapture) from loading by clearing METAL_CAPTURE_ENABLED before the Metal device is created. The layer intermittently crashes every acceleration structure command encoder in endEncoding (macOS 26 / M4, see doc/metal_gputoolscapture_as_crash_repro.mm), which otherwise forces ray query off whenever the editor runs under the Xcode debugger. While enabled, GPU frame capture from Xcode will not work for this process. No effect if the layer is already loaded before device creation (a warning is logged and the capture-layer ray query guard still applies). Only meaningful for debugging.",
            visible=True,
            developer=False
        ),
    ],
)
