# RenderDoc frame capture on Quest

Status: **working end to end and verified on a Quest 3.** Launch, capture,
download and thumbnail extraction are driven by
[`scripts/quest_renderdoc.py`](../scripts/quest_renderdoc.py); the erhe-side
integration is in place.

The desktop Windows/Vulkan capture workflow is a separate, working thing and is
documented in [`renderdoc_fork.md`](renderdoc_fork.md). This document covers the
Android / Quest target only.

## Why this is wanted

XR rendering bugs on Quest have no pixel-level diagnostic path. `capture_screenshot`
over the in-editor MCP server **cannot work in XR**: the OpenXR swapchain is
created without `TRANSFER_SRC`, so there is no readback path and the tool returns
"Frame capture not available". The rest of the MCP tool surface does work
on-device. Without a capture, the only channel is the user describing what they
see, which has already cost real debugging time.

## The tool

**RenderDoc Meta Fork** is the supported capture tool for Quest (Meta developer
docs, *Use RenderDoc Meta Fork for GPU Profiling*). Beyond upstream RenderDoc it
provides a **Tile Timeline** (per-tile render stage trace for the Adreno tiler),
a **draw call trace** with up to 48 GPU metrics, Vulkan shader stats via
`KHR_pipeline_executable_properties`, and API validation on replay.

**68.16 or later is required** for the bundled Python API package
(`<install>/MCP`) that `scripts/quest_renderdoc.py` drives; earlier builds
predate it. Note that the fork's `renderdoccmd` command set does not always match
what its Python wrapper expects - `capture_info()`, for instance, calls a
`capture-info` subcommand that does not exist in 68.18. Check
`renderdoccmd` with no arguments for the commands a given build actually has.

## What is implemented in erhe

All of this is verified on a Quest 3.

**Passive attach to an injected capture layer.** `renderdoc_capture.cpp` has an
Android branch that does `dlopen("libVkLayer_GLES_RenderDoc.so", RTLD_NOW | RTLD_NOLOAD)`.
erhe never *loads* the layer - `RTLD_NOLOAD` attaches only if the RenderDoc host
already injected it. That keeps a normal run free of cost and makes a second copy
of the layer in-process impossible (which would trip RenderDoc's own
multi-instance `__debugbreak()`).

**The attach happens after `vkCreateInstance`, not at window creation.** The layer
is loaded *by the Vulkan loader during instance creation*, which is after
`initialize_frame_capture()` runs. `try_attach_frame_capture()` is therefore
called from `vulkan_device_init.cpp` right after `volkLoadInstance()`. The early
attempt at window creation is expected to find nothing.

**API version negotiation.** `negotiate_renderdoc_api()` asks for 1.7.0, then
1.6.0. This is required, not defensive: `RENDERDOC_GetAPI()` returns 0 rather
than negotiating down, and **RenderDoc Meta Fork tops out at API 1.6.0** (it is
forked from RenderDoc 1.41). On device the log confirms
`in-application API version 10600 negotiated`. The previous code asked only for
1.7.0 under `ERHE_VERIFY(ret == 1)`, which would have aborted the editor.

> The struct is append-only and every `RENDERDOC_API_1_*_0` name is a typedef of
> the newest one, so an older module still fills in every entry point erhe calls.
> The hazard is that below 1.7.0 the returned struct is **shorter**: its last two
> members, `SetObjectAnnotation` and `SetCommandAnnotation`, are absent and must
> never be called. erhe uses neither.

**Validation and capture are mutually exclusive, by construction.** The Khronos
validation layer and RenderDoc's capture layer must not both be enabled.
`renderdoc_capture_support` only covers capture configured up front, not a
RenderDoc that injected itself. `vulkan_device_init.cpp` therefore checks the
loader's own layer enumeration for `VK_LAYER_RENDERDOC_Capture` - available at
exactly the point the choice is made - and skips validation when it is present.
Verified: `RenderDoc capture layer is present - Vulkan validation layer will not
be enabled`.

**Capture UI keys off actual attachment.** `editor.cpp` sets
`m_app_context.renderdoc` and `developer_mode` when `get_renderdoc_api() != nullptr`,
after the graphics device exists. `renderdoc_capture_support` stays **off** on
Android on purpose: turning it on would suppress the validation layer on every
run, including runs with no RenderDoc.

**MCP trigger.** `request_renderdoc_capture` (`mcp_server_file_io.cpp`) fires
`App_rendering::request_renderdoc_capture()`, which `Headset_view` wraps around
its multiview render. This exists because an OpenXR app never calls
`vkQueuePresentKHR`, so RenderDoc cannot delimit frames on its own; the capture
has to be bracketed by the app. Driving it from MCP means the capture can be
taken at a chosen moment from the host instead of the user hunting for a menu
item while wearing the headset.

**Package visibility.** `AndroidManifest.xml` declares a `<queries>` block for
the RenderDoc packages. Android 11+ filters package visibility, and the
platform's `GraphicsEnvironment` resolves the layer app named by
`gpu_debug_layer_app` through PackageManager *from inside the app process*.
Without this the lookup is filtered and the layer silently never loads - logcat
shows `AppsFilter: ... -> com.oculus.renderdoccmd.arm64 BLOCKED`.

## The workflow

`scripts/quest_renderdoc.py` drives the whole loop. It uses the fork's bundled
Python API (`<install>/MCP/renderdoc_mcp`), which only needs to be importable -
it shells out to `renderdoccmd` rather than binding the native module, so its
fastmcp/pydantic dependencies are not required and nothing needs `pip install`.

```sh
py -3 scripts/quest_renderdoc.py status          # check everything, change nothing
py -3 scripts/quest_renderdoc.py launch          # force-stop, then launch injected
# ... put the headset on, get the scene into the state you want ...
py -3 scripts/quest_renderdoc.py capture --frames 1
py -3 scripts/quest_renderdoc.py stop
```

`launch` records the capture ident (`logs/.renderdoc_ident.json`) so `capture`
can run as a separate invocation, as many times as wanted. Captures and their
extracted thumbnails land in `logs/rdc/`.

One-time device setup: install the fork's on-device component from
`<install>/plugins/android/com.oculus.renderdoccmd.arm64.apk`. `status` says so
if it is missing.

### Traps the script handles

Each of these fails silently or misleadingly, and each costs a headset cycle to
rediscover, so `preflight()` checks them up front.

- **Two adb versions fight.** RenderDoc ships adb 1.0.40 (2018); the Android SDK
  has 1.0.41. adb refuses to talk to a server of a different version and *kills*
  it, so interleaving the two clients restarts the daemon underneath whatever is
  running - silently dropping RenderDoc's target-control forwards mid-capture.
  The script pins the SDK's adb for everything, and checks that RenderDoc's
  `Android.SDKDirPath` points at that same SDK (it is empty by default, which is
  what makes RenderDoc fall back to its own copy).
- **Stale capture paths.** On a copy failure `renderdoccmd` reports
  `COPY_FAILED:<path>` naming a capture from an *earlier* run, not the one just
  taken. Following that path downloads a stale frame that looks like a
  successful capture until its contents are examined. The script instead
  snapshots the device's capture directory before and after, and pulls whatever
  is genuinely new.
- **renderdoccmd's own copy fails but `adb pull` works.** The script pulls
  directly; a `.rdc` is never abandoned on the device because of a transfer
  failure.
- **Manual layer injection must be clear** - see below.
- **Store builds cannot be captured**, and a missing on-device component gives an
  unhelpful error; both are checked.

A thumbnail is extracted from every capture by default. It is the only part of a
capture that can be looked at without starting a replay session, so it is the
cheapest proof that the capture holds the frame that was intended.

## Do not inject the layer manually

Android's GPU debug layer settings *can* inject the capture layer without
qrenderdoc, and with the `<queries>` manifest entry the layer does load. **But
the editor then crashes during startup**, inside RenderDoc's layer:

```
signal 11 (SIGSEGV), code 1 (SEGV_MAPERR), fault addr 0x10   (null pointer dereference)
  #00 libVkLayer_GLES_RenderDoc.so  Atomic::Inc32(int*)+0
  #01 libVkLayer_GLES_RenderDoc.so  ResourceRecord::AddParent(ResourceRecord*)+108
  #02 libVkLayer_GLES_RenderDoc.so  WrappedVulkan::vkBindImageMemory(...)+1028
  #03 libvrapiimpl.so               (Meta's XR runtime)
  #07 libopenxr_loader.so           xrCreateSwapchain+124
  #08 libmain.so                    erhe::xr::Xr_session::create_swapchains()+928
```

Meta's XR runtime creates the swapchain images itself and calls
`vkBindImageMemory` on the app's device; the layer intercepts that for a resource
it never saw created, finds a null `ResourceRecord`, and dereferences it.

RenderDoc's own launcher injects differently and does **not** hit this, which is
why `launch` goes through `renderdoccmd adb-launch`. If the `gpu_debug_*`
settings are ever left set, every subsequent launch crashes this way;
`preflight()` refuses to run until they are cleared, and
`stop --clear-layer-settings` clears them.

## Gotchas

- **Memory.** Meta warns that a crash on capture usually means the device ran out
  of memory, and that it must hold the app, the capture layer *and* the `.rdc`
  at once. The Quest APK is ~4 GB against 8 GB RAM (~3.8 GB available); the
  RenderDoc layer alone is a 391 MB `.so`. Storage is not the issue (53 GB free).
- **Development builds only.** Store builds cannot be captured. The debug APK is
  already `DEBUGGABLE` (`dumpsys package` reports it), so nothing is needed here.
- **`is_ray_tracing_blocked_by_capture_layer()` is Windows/AMD-only**, so
  enabling capture does not cost ray tracing on Quest.
- **Two RenderDoc copies in one process** trip RenderDoc's multi-instance
  `__debugbreak()`. The `RTLD_NOLOAD` attach cannot cause this; a bundled *and*
  injected layer would.

## Related

- [`renderdoc_fork.md`](renderdoc_fork.md) - the desktop Windows/Vulkan MCP
  capture workflow.
- `.agents/skills/erhe-quest-launch` - build / install / launch protocol.
