# Vulkan backend for `erhe::xr` via `XR_KHR_vulkan_enable2`

## Context

`erhe::xr` currently only supports the OpenGL graphics backend. Every XR call
path has a `#if defined(ERHE_GRAPHICS_API_OPENGL)` guard and nothing in the
`#if defined(ERHE_GRAPHICS_API_VULKAN)` branches. A previous commit
(`eafc4efd vulkan openxr`) flipped the Vulkan configure scripts to enable
`ERHE_XR_LIBRARY=openxr`, and some partial scaffolding has landed since:

- `CMakeLists.txt` moved `find_package(OpenGL)` inside the OpenGL-only branch
  so Vulkan builds do not pull in OpenGL.
- `src/erhe/xr/CMakeLists.txt` links `volk::volk` and defines
  `XR_USE_GRAPHICS_API_VULKAN=1` when `ERHE_GRAPHICS_API_VULKAN` is set.
- `xr_swapchain_image.{hpp,cpp}` already has a full `#if ERHE_GRAPHICS_API_VULKAN`
  branch: `get_vk_image()` accessor, `m_vk_images` storage, and the
  `xrEnumerateSwapchainImages` call with `XrSwapchainImageVulkanKHR`.
- `xr_instance.cpp:204` pushes `XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME` into
  `enabled_extensions`.
- `xr_session.cpp:114-123` calls `m_instance.xrGetVulkanGraphicsRequirementsKHR()`
  -- but that member function pointer is not declared in `xr_instance.hpp`
  (there is an empty `/// TODO` at line 129), so the current code would not
  compile if the Vulkan graphics backend is paired with XR.

The gaps -- what this plan completes:

1. `Xr_instance` does not load the Vulkan enable2 entry points
   (`xrGetVulkanInstanceExtensionsKHR` or more precisely the _enable2 trio:
   `xrCreateVulkanInstanceKHR`, `xrGetVulkanGraphicsDevice2KHR`,
   `xrCreateVulkanDeviceKHR`, `xrGetVulkanGraphicsRequirements2KHR`).
2. `Xr_session::create_session()` has no `XrGraphicsBindingVulkan2KHR`
   construction and no `xrCreateSession()` path for Vulkan.
3. `Xr_session::enumerate_swapchain_formats()` and `create_swapchains()` only
   have OpenGL branches. The swapchain format comparison loop needs a Vulkan
   `VkFormat -> erhe::dataformat::Format` path.
4. `Render_view` (in `xr.hpp`) carries GL texture names but has a `// TODO`
   for Vulkan handles.
5. The editor's `Headset_view_resources` (`editor/xr/headset_view_resources.cpp`)
   wraps `Render_view::color_texture` / `depth_stencil_texture` into erhe
   textures via `Texture_create_info::wrap_texture_name`. There is no Vulkan
   equivalent yet.
6. The Vulkan `Device_impl` constructor (`vulkan_device_init.cpp`) calls
   `vkCreateInstance`, `vkEnumeratePhysicalDevices`, and `vkCreateDevice`
   directly. When XR is active, those calls must instead go through
   `xrCreateVulkanInstanceKHR`, `xrGetVulkanGraphicsDevice2KHR`, and
   `xrCreateVulkanDeviceKHR` so the OpenXR runtime can inject the Vulkan
   instance / device extensions the HMD needs.
7. The editor creates `Headset` *before* `Device` today (main-thread GL
   context requirement). On Vulkan that order has to change: the graphics
   device needs to call XR-wrapped Vulkan creators, and the session needs
   the graphics device's `VkInstance` / `VkPhysicalDevice` / `VkDevice`,
   so the sequence becomes `Xr_instance -> Device -> Xr_session`.

The intended outcome is `scripts\configure_vs2026_vulkan.bat` being able to
build + run the editor with an OpenXR session active.

## Why `enable2` and not `enable`

`XR_KHR_vulkan_enable` (the original extension) asks the app to enumerate
required instance/device extensions via `xrGetVulkanInstanceExtensionsKHR`
and `xrGetVulkanDeviceExtensionsKHR`, then call `vkCreateInstance` /
`vkCreateDevice` itself, then pass the handles back to OpenXR.

`XR_KHR_vulkan_enable2` is the newer revision that wraps Vulkan creation:
the app builds a normal `VkInstanceCreateInfo`, and the runtime calls
`vkCreateInstance` internally after injecting its required extensions and
features. Same for device creation, and physical device selection is done
via `xrGetVulkanGraphicsDevice2KHR`. This is the approach current OpenXR
runtimes encourage and it is what the existing partial scaffolding in
`xr_instance.cpp:204` opts into. This plan sticks with `enable2`.

## Approach

### Step 1 -- Declare and load the Vulkan enable2 entry points

**`src/erhe/xr/erhe_xr/xr_instance.hpp`** gains four function pointer
members guarded by `ERHE_GRAPHICS_API_VULKAN`:

```cpp
#if defined(ERHE_GRAPHICS_API_VULKAN)
    PFN_xrGetVulkanGraphicsRequirements2KHR xrGetVulkanGraphicsRequirements2KHR{nullptr};
    PFN_xrCreateVulkanInstanceKHR           xrCreateVulkanInstanceKHR          {nullptr};
    PFN_xrGetVulkanGraphicsDevice2KHR       xrGetVulkanGraphicsDevice2KHR      {nullptr};
    PFN_xrCreateVulkanDeviceKHR             xrCreateVulkanDeviceKHR            {nullptr};
#endif
```

(Note: `xr_session.cpp:122` today calls
`m_instance.xrGetVulkanGraphicsRequirementsKHR` (no `2`). That's a leftover
from `_enable1` -- under `_enable2` the suffixed `Requirements2KHR` is the
one the runtime exposes. Fix the call site to match.)

**`src/erhe/xr/erhe_xr/xr_instance.cpp`** loads all four via the existing
`get_proc_addr<PFN_...>("xr...KHR")` helper, right next to the OpenGL
block around line 362-368:

```cpp
#if defined(ERHE_GRAPHICS_API_VULKAN)
    xrGetVulkanGraphicsRequirements2KHR = get_proc_addr<PFN_xrGetVulkanGraphicsRequirements2KHR>("xrGetVulkanGraphicsRequirements2KHR");
    xrCreateVulkanInstanceKHR           = get_proc_addr<PFN_xrCreateVulkanInstanceKHR>          ("xrCreateVulkanInstanceKHR");
    xrGetVulkanGraphicsDevice2KHR       = get_proc_addr<PFN_xrGetVulkanGraphicsDevice2KHR>      ("xrGetVulkanGraphicsDevice2KHR");
    xrCreateVulkanDeviceKHR             = get_proc_addr<PFN_xrCreateVulkanDeviceKHR>            ("xrCreateVulkanDeviceKHR");
#endif
```

### Step 2 -- Expose Vulkan-create hooks on `Xr_instance` for the graphics device

The editor must create the Vulkan `Device` with XR-aware creators. Rather
than having `erhe::graphics` depend on `erhe::xr` (bad layering -- graphics
is below xr), add a small hooks struct that lives on the public
`erhe::graphics::Device_create_info` (or is passed as a separate optional
argument to the Vulkan `Device_impl` constructor):

```cpp
struct Vulkan_external_creators
{
    // Called instead of vkCreateInstance.
    std::function<VkResult(const VkInstanceCreateInfo*, VkInstance*)>
        create_instance;

    // Called instead of picking from vkEnumeratePhysicalDevices.
    // Takes the instance, returns the physical device that drives the HMD.
    std::function<VkResult(VkInstance, VkPhysicalDevice*)>
        pick_physical_device;

    // Called instead of vkCreateDevice.
    std::function<VkResult(VkPhysicalDevice, const VkDeviceCreateInfo*, VkDevice*)>
        create_device;
};
```

`Xr_instance` gains a factory method that populates such a struct:

```cpp
[[nodiscard]] auto make_vulkan_external_creators() const -> Vulkan_external_creators;
```

The factory body wraps the four PFN calls. Each hook builds an
`XrVulkanInstanceCreateInfoKHR` / `XrVulkanGraphicsDeviceGetInfoKHR` /
`XrVulkanDeviceCreateInfoKHR`, looks up `vkGetInstanceProcAddr` from volk,
and forwards to the matching `xrCreateVulkan*KHR` / `xrGetVulkanGraphicsDevice2KHR`.

### Step 3 -- Vulkan `Device_impl` consumes the hooks

**`src/erhe/graphics/erhe_graphics/vulkan/vulkan_device.hpp`**: add an
optional field or constructor parameter for `Vulkan_external_creators`.

**`src/erhe/graphics/erhe_graphics/vulkan/vulkan_device_init.cpp`** around
lines 274, 682, and the physical device enumeration: call the hook when
set, otherwise fall back to the direct `vkCreateInstance` / `vkCreateDevice`
/ `vkEnumeratePhysicalDevices` calls that are there today. Extension
querying, feature enabling, queue family discovery, debug messenger setup
etc. stay on `Device_impl` -- the hooks only replace the three "create"
calls. OpenXR's required instance/device extensions get merged into the
existing extension list before the create call.

This is a minimally invasive change: three call sites each gain an
`if (m_external_creators.create_instance) { ... } else { ... }` branch.

### Step 4 -- Split `Xr_session` construction out of `Headset`

Today `Headset::Headset(context_window, configuration)` constructs both
`Xr_instance` *and* `Xr_session` in its body. On the Vulkan path that
sequencing breaks because the session creation needs the `Device`'s
Vulkan handles, and the `Device` does not exist yet when `Headset` is
constructed.

Change `Headset`'s constructor so it builds only `Xr_instance` eagerly,
and add a second phase:

```cpp
class Headset {
public:
    Headset(erhe::window::Context_window&, const Xr_configuration&);
    // Second phase. On the OpenGL backend this is a no-op wrapper around
    // the existing session constructor (the session can and should still
    // be built from the main-thread GL context). On Vulkan it must be
    // called after the graphics Device is ready and receives a reference
    // to it so session creation can fill in XrGraphicsBindingVulkan2KHR.
    [[nodiscard]] auto create_session(erhe::graphics::Device& device) -> bool;
    ...
};
```

`Xr_session`'s constructor gains an `erhe::graphics::Device&` parameter and
uses the Vulkan accessors (`get_vulkan_instance()`,
`get_vulkan_physical_device()`, `get_vulkan_device()`,
`get_graphics_queue_family_index()`, `get_graphics_queue()` + index) that
already exist on `Vulkan_device::Device_impl` to build the binding:

```cpp
#if defined(XR_USE_GRAPHICS_API_VULKAN)
    const XrGraphicsBindingVulkan2KHR graphics_binding{
        .type             = XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR,
        .next             = nullptr,
        .instance         = device.get_impl().get_vulkan_instance(),
        .physicalDevice   = device.get_impl().get_vulkan_physical_device(),
        .device           = device.get_impl().get_vulkan_device(),
        .queueFamilyIndex = device.get_impl().get_graphics_queue_family_index(),
        .queueIndex       = 0 // single-queue today
    };
    const XrSessionCreateInfo session_create_info{
        .type     = XR_TYPE_SESSION_CREATE_INFO,
        .next     = &graphics_binding,
        .systemId = m_instance.get_xr_system_id()
    };
    ERHE_XR_CHECK(xrCreateSession(xr_instance, &session_create_info, &m_xr_session));
#endif
```

### Step 5 -- Vulkan-aware swapchain format selection and creation

**`src/erhe/xr/erhe_xr/xr_session.cpp`** `enumerate_swapchain_formats()`
(lines 356-376) and `create_swapchains()` (lines 406-468) get
`ERHE_GRAPHICS_API_VULKAN` branches that mirror the OpenGL branches
but:

- Use `static_cast<VkFormat>(swapchain_format)` and
  `erhe::graphics::vulkan_helpers::convert_from_vk(...)` for format
  conversion. A forward-declared helper
  `auto convert_from_vk(VkFormat) -> erhe::dataformat::Format` already
  exists in `vulkan_helpers.hpp` (and its inverse `convert_to_vk`).
  `erhe::xr` may need to depend on it -- either by including
  `erhe_graphics/vulkan/vulkan_helpers.hpp` (which already has public
  visibility because volk is a PUBLIC link dependency on xr today) or
  by adding a public backend-neutral `erhe::dataformat` helper.
- Use `VkFormat` swapchain usage flags when building
  `XrSwapchainCreateInfo`. Usage flags map from
  `XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT`
  (unchanged -- those are XR flags, not graphics-API flags).
- `format` field of `XrSwapchainCreateInfo` is `int64_t(VkFormat)` for
  Vulkan, as opposed to `int64_t(GLenum)` for GL.

### Step 6 -- `Render_view` carries Vulkan image handles

**`src/erhe/xr/erhe_xr/xr.hpp`** around line 35-39, fill in the existing
Vulkan TODO:

```cpp
#if defined(ERHE_GRAPHICS_API_VULKAN)
    uint64_t vk_color_image        {0}; // VkImage reinterpreted as uint64_t
    uint64_t vk_depth_stencil_image{0};
#endif
```

(`VkImage` is a 64-bit non-dispatchable handle, so `uint64_t` is the
correct storage type and keeps the public header free of `<vulkan.h>`.)

`Xr_session::render()` / `populate_render_views()` (the code around
`xr_session.cpp:993-1025` that currently calls `swapchain_image.get_gl_texture()`)
gains a Vulkan branch that calls `.get_vk_image()` instead and stores the
result as `uint64_t`.

### Step 7 -- `Headset_view_resources` wraps Vulkan images

**`src/editor/xr/headset_view_resources.cpp`** lines 51-53 and 68-70 set
`wrap_texture_name` only on the OpenGL path today. On the Vulkan path,
pass the `vk_color_image` / `vk_depth_stencil_image` uint64 from the
`Render_view` into the same `wrap_texture_name` slot:

```cpp
#if defined(ERHE_GRAPHICS_API_OPENGL)
    .wrap_texture_name = render_view.color_texture,
#elif defined(ERHE_GRAPHICS_API_VULKAN)
    .wrap_texture_name = render_view.vk_color_image,
#endif
```

### Step 8 -- Vulkan `Texture_impl` wraps an external VkImage

**`src/erhe/graphics/erhe_graphics/vulkan/vulkan_texture.cpp`**: add a
code path for `Texture_create_info::wrap_texture_name != 0` that
interprets the value as a `VkImage` handle and creates a `VkImageView`
from it without allocating the image storage. The GL path already does
this (`gl_texture.cpp:631` wraps the handle into `Gl_texture{handle, false}`,
where the `false` means "do not destroy on dtor"). The Vulkan impl
mirrors this: set an `m_is_wrapped` flag, skip the `vmaCreateImage`
call, create the view from the provided image, and skip the image
destruction in the destructor (while still destroying the view). The
image lifetime is owned by the OpenXR swapchain and is released when
`xrDestroySwapchain` runs.

### Step 9 -- Editor construction order

**`src/editor/editor.cpp`** around lines 586-606 currently does
`m_headset = std::make_unique<Headset>(...)` before `m_graphics_device`
is created. Restructure to:

1. Build `Xr_configuration` (unchanged).
2. Create `m_headset = std::make_unique<Headset>(*m_window.get(), configuration)`.
   With the new split from Step 4 this constructs only the `Xr_instance`.
3. If `m_headset && m_headset->is_valid()` (Xr_instance ready) and the
   graphics backend is Vulkan, fetch
   `m_headset->get_xr_instance()->make_vulkan_external_creators()` and
   pass it into the `Graphics_device` constructor (via
   `Device_create_info::vulkan_external_creators`).
4. Create `m_graphics_device` as today.
5. Call `m_headset->create_session(*m_graphics_device)` -- this builds
   the `Xr_session`. On the OpenGL path it just runs the existing
   session constructor; on Vulkan it uses the graphics device handles.

On OpenGL the behavior is identical to today -- Step 4's split just
moves a few lines of code. On Vulkan the hooks take effect.

### Step 10 -- Build-system guard tweaks

- `src/erhe/xr/erhe_xr/xr_instance.cpp:197-204` OpenGL extension-push
  block already skips on Vulkan. Keep as-is.
- `src/erhe/xr/erhe_xr/xr_session.cpp:143-188` selects a
  platform-specific graphics binding. Add a
  `#if defined(XR_USE_GRAPHICS_API_VULKAN)` branch that works on both
  Win32 and Linux (the Vulkan binding struct does not have per-platform
  variants -- `XrGraphicsBindingVulkan2KHR` is platform-independent).
- `erhe_xr` CMakeLists (`src/erhe/xr/CMakeLists.txt:58-67`) already
  links volk when Vulkan is active. If Step 5 reaches into
  `erhe_graphics/vulkan/vulkan_helpers.hpp` for the format conversion,
  add a `target_link_libraries(erhe_xr PRIVATE erhe::graphics)` line
  under the Vulkan branch -- but that creates a cycle because
  `erhe::graphics` depends on nothing from `erhe::xr`. Preferred
  alternative: move the `VkFormat <-> erhe::dataformat::Format` helper
  into `erhe::dataformat` (which already owns all other format
  conversions). Needs a quick check that nothing graphics-backend-specific
  is in the helper today.

## Critical files to modify

| File | Change |
|---|---|
| `src/erhe/xr/erhe_xr/xr_instance.hpp` | Declare `xrGetVulkanGraphicsRequirements2KHR`, `xrCreateVulkanInstanceKHR`, `xrGetVulkanGraphicsDevice2KHR`, `xrCreateVulkanDeviceKHR` function pointer members |
| `src/erhe/xr/erhe_xr/xr_instance.cpp` | Load the four PFNs; add `make_vulkan_external_creators()` factory |
| `src/erhe/xr/erhe_xr/xr_session.hpp` | Constructor takes `erhe::graphics::Device&` (for the Vulkan graphics binding); fix `xrGetVulkanGraphicsRequirementsKHR` -> `Requirements2KHR` |
| `src/erhe/xr/erhe_xr/xr_session.cpp` | Build `XrGraphicsBindingVulkan2KHR`; add Vulkan branches to `enumerate_swapchain_formats()` / `create_swapchains()` / `populate_render_views()`-equivalent |
| `src/erhe/xr/erhe_xr/xr.hpp` | Fill in `Render_view` Vulkan handle fields |
| `src/erhe/xr/erhe_xr/headset.{hpp,cpp}` | Split session creation into a separate `create_session(device)` method |
| `src/erhe/graphics/erhe_graphics/device.hpp` (or new `vulkan_external_creators.hpp`) | `Vulkan_external_creators` struct on `Device_create_info` |
| `src/erhe/graphics/erhe_graphics/vulkan/vulkan_device.hpp` | Store external creators; new accessor if not already public |
| `src/erhe/graphics/erhe_graphics/vulkan/vulkan_device_init.cpp` | Route `vkCreateInstance` / `vkEnumeratePhysicalDevices` / `vkCreateDevice` calls through the hooks when set |
| `src/erhe/graphics/erhe_graphics/vulkan/vulkan_texture.cpp` | Wrap an externally-owned `VkImage` when `wrap_texture_name != 0`, do not destroy it on dtor |
| `src/editor/editor.cpp` | Reorder `Headset` / `Graphics_device` construction; call `headset->create_session(device)` after device is ready |
| `src/editor/xr/headset_view_resources.cpp` | Pass `render_view.vk_color_image` / `vk_depth_stencil_image` into `Texture_create_info::wrap_texture_name` on Vulkan |
| `src/erhe/dataformat/erhe_dataformat/vk_format.{hpp,cpp}` (new) or an existing file | Pure `VkFormat <-> Format` helper that `erhe::xr` and `erhe::graphics::vulkan` can both include without circular dependency. If this is infeasible, `erhe::xr` can depend on a narrow public subset of `erhe_graphics/vulkan/vulkan_helpers.hpp` directly. |

## Verification

Smoke test on a machine with an OpenXR runtime (Meta Quest via Link,
SteamVR, Monado, etc.):

1. **GL bindless + XR** (`scripts\configure_vs2026_opengl.bat` then
   editor): baseline -- already works. Confirms the refactored
   `Headset::create_session(device)` two-phase construction does not
   regress the OpenGL path.
2. **GL sampler-array + XR**: same as above but on the sampler-array
   device path. Still should just work.
3. **Vulkan + XR** (`scripts\configure_vs2026_vulkan.bat` then editor):
   primary verification target. Expected behavior: editor launches,
   Vulkan instance is created via `xrCreateVulkanInstanceKHR`, physical
   device selected via `xrGetVulkanGraphicsDevice2KHR`, device created
   via `xrCreateVulkanDeviceKHR`, session created with
   `XrGraphicsBindingVulkan2KHR`, swapchain enumerated with
   `XrSwapchainImageVulkanKHR`, `Headset_view` renders into the wrapped
   `VkImage` swapchain images, and both eyes present in the HMD. VMA
   reports no leaks at shutdown (`xrDestroySwapchain` releases the
   images before `Device_impl` destruction).
4. **Vulkan without XR**: `scripts\configure_vs2026_vulkan.bat` with
   `-DERHE_XR_LIBRARY=none`. Must still build and run the editor in
   windowed mode.
5. **GL without XR**, **Metal**: unaffected; smoke-test anyway.

Validation layer spot-checks to run under `Vulkan + XR`:
- No `VUID-VkInstanceCreateInfo-*` errors from XR-injected extensions.
- No `VUID-VkDeviceCreateInfo-*` errors.
- Swapchain images should not trip `VUID-VkImageView*` errors when
  wrapped into erhe Textures (the view creation must use the matching
  `VkFormat` and subresource range).
- VMA leak report at shutdown should be clean.

## Risks

**Risk 1 -- Extension merging during instance/device creation.** OpenXR's
`xrCreateVulkanInstanceKHR` expects the app to pass its own
`VkInstanceCreateInfo` *including* any extensions the app wants
*plus* whatever extensions OpenXR decides are required. Today erhe's
Vulkan Device_impl builds its extension list internally, then calls
`vkCreateInstance`. The hook must be called with the same finalized
`VkInstanceCreateInfo` the app would have passed to `vkCreateInstance`,
not a pre-merge snapshot. Verify the hook insertion point is *after*
erhe has finished populating its own extension list, so OpenXR only
has to add XR-specific extensions on top (which the runtime does
internally).

**Risk 2 -- Physical device selection.** erhe's existing physical device
selection enumerates and scores. OpenXR dictates which physical device
the HMD is on, and `xrGetVulkanGraphicsDevice2KHR` returns it. When the
hook is set, the erhe scoring is bypassed entirely -- just trust the
XR pick. Log it so regressions are obvious.

**Risk 3 -- Single graphics queue assumption.** The
`XrGraphicsBindingVulkan2KHR` struct requires a specific queue family
index and queue index. erhe currently uses a single graphics queue --
fine. If a future multi-queue setup lands, XR will need to reserve one
specific queue.

**Risk 4 -- `erhe::xr` accessing graphics-backend-internal headers.**
If Step 5 ends up including `vulkan_helpers.hpp` from `erhe::xr`, we
couple the XR library to a concrete graphics backend. Prefer moving
`VkFormat <-> Format` into `erhe::dataformat` (check that file for
existing per-backend helpers first).

**Risk 5 -- Headset construction reorder.** The current comment at
`editor.cpp:584-585` says "Apparently it is necessary to create OpenXR
session in the main thread / original OpenGL context". Keep that
constraint on the OpenGL path -- the `create_session()` split must
still be called on the main thread with the GL context current.

## Out of scope

- `XR_KHR_vulkan_enable1` fallback for runtimes that do not support
  `_enable2`. All three common runtimes (SteamVR, Monado, Meta Quest)
  support `_enable2` today.
- Multi-queue Vulkan setups.
- XR on Metal -- not supported by OpenXR yet.
- Turning any of this into a generic "external graphics device"
  creation path beyond what XR needs.
