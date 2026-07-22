# VK_EXT_present_timing issues on NVIDIA 596.83: inert target present time; wrong refreshDuration after an app-driven mode set

Self-contained issue descriptions for external discussion. Measured 2026-07-22
on the erhe engine's frame pacer integration (implementation plan step P2.3);
methodology details live in [frame_pacing_capability_tiers.md](frame_pacing_capability_tiers.md) §5.

Two independent issues, same environment:

1. **Target present times are accepted but never honored** — the image always
   displays at the earliest feasible vsync (§ Issue 1, the bulk of this
   document).
2. **`VkSwapchainTimingPropertiesEXT.refreshDuration` is grossly wrong (4.2 %)
   after an application-driven display mode change**, while correct when the
   same mode is set through OS display settings (§ Issue 2).

## Issue 1 summary

The driver advertises `VK_EXT_present_timing` (spec v3) with the
`presentAtRelativeTime` feature and the surface capability
`presentAtRelativeTimeSupported = TRUE`. Relative target present times chained
into `vkQueuePresentKHR` pass validation cleanly and the *feedback* half of
the extension works perfectly — but the target has **no observable effect on
when the image is displayed**. The image always becomes visible at the
earliest vsync at which it is ready, exactly as if `targetTime` were 0. This
holds across every advertised combination of flags, present stages, time
domains, presentation paths, and present modes (matrix below).

## Environment

| | |
|---|---|
| GPU / driver | NVIDIA GeForce RTX 3080, driver 596.83 (`VK_DRIVER_ID_NVIDIA_PROPRIETARY`), reported API 1.4.353.0 |
| OS | Windows 11 (10.0.26200) |
| Headers | Vulkan SDK 1.4.341.1 |
| Display | 120 Hz (`refreshDuration` reported ≈ 8,333,100–8,333,300 ns per swapchain; true period measured ~11 ppm longer via feedback) |
| Validation | Khronos validation layers enabled during all measurements; zero errors (only known unrelated BestPractices warnings) |

## What the driver reports

Device extensions (all enabled): `VK_EXT_present_timing` v3,
`VK_KHR_present_id` v1, `VK_KHR_present_wait` v1,
`VK_KHR/EXT_calibrated_timestamps`, `VK_EXT/KHR_swapchain_maintenance1`,
`VK_EXT_full_screen_exclusive` v4.

`VkPhysicalDevicePresentTimingFeaturesEXT` (queried, then enabled verbatim at
device creation):

```
presentTiming         = TRUE
presentAtAbsoluteTime = TRUE
presentAtRelativeTime = TRUE
```

`VkPresentTimingSurfaceCapabilitiesEXT` (chained to
`vkGetPhysicalDeviceSurfaceCapabilities2KHR` for the Win32 surface):

```
presentTimingSupported         = TRUE
presentAtAbsoluteTimeSupported = FALSE   <- device feature TRUE, surface says no
presentAtRelativeTimeSupported = TRUE
presentStageQueries            = 0x5     (QUEUE_OPERATIONS_END | IMAGE_FIRST_PIXEL_OUT)
```

Identical values when `VkSurfaceFullScreenExclusiveInfoEXT`
(`APPLICATION_CONTROLLED`) + `VkSurfaceFullScreenExclusiveWin32InfoEXT`
(monitor from `MonitorFromWindow`) are chained into the query — so absolute
time is unavailable in every fullscreen-exclusive mode, and could not be
legally tested (VU requires the surface bit).

`vkGetSwapchainTimeDomainPropertiesEXT`: exactly **one** time domain —
`VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT`, id `1000208000`. No QPC, no
CLOCK_MONOTONIC, no swapchain-local domain.

## How the swapchain is configured

- `VkSwapchainCreateInfoKHR.flags` includes `VK_SWAPCHAIN_CREATE_PRESENT_TIMING_BIT_EXT`.
- Present mode: `VK_PRESENT_MODE_FIFO_LATEST_READY_KHR` normally; plain
  `VK_PRESENT_MODE_FIFO_KHR` also tested (same result).
- `vkSetSwapchainPresentTimingQueueSizeEXT(device, swapchain, 32)` right after creation → `VK_SUCCESS`.
- The stage-local domain is bridged to the host clock (QPC) with
  `vkGetCalibratedTimestampsKHR` + `VkSwapchainCalibratedTimestampInfoEXT`
  (`presentStage = IMAGE_FIRST_PIXEL_OUT`), recalibrated every 120 frames.
- Windowed (DWM-composited) and application-controlled exclusive fullscreen
  (`vkAcquireFullScreenExclusiveModeEXT` returned `VK_SUCCESS`) both tested.

## What is submitted per present

`VkPresentInfoKHR.pNext` chain: `VkSwapchainPresentFenceInfoEXT` →
`VkPresentIdKHR` (`presentId` = frame index + 1) → `VkPresentTimingsInfoEXT`:

```
VkPresentTimingsInfoEXT {
    swapchainCount = 1
    pTimingInfos   = &VkPresentTimingInfoEXT {
        flags                        = PRESENT_AT_RELATIVE_TIME_BIT
                                       (also tested: | PRESENT_AT_NEAREST_REFRESH_CYCLE_BIT)
        targetTime                   = <desired display time - now> in ns,
                                       measured at the vkQueuePresentKHR call;
                                       typically 8..15 ms, always > 0 when chained
        timeDomainId                 = 1000208000  (the only advertised domain)
        presentStageQueries          = IMAGE_FIRST_PIXEL_OUT (0x4)
        targetTimeDomainPresentStage = IMAGE_FIRST_PIXEL_OUT (0x4)
                                       (also tested: QUEUE_OPERATIONS_END, 0x1)
    }
}
```

The desired display time is a vsync slot boundary predicted by a
feedback-tracked grid (phase + period PLL over achieved present times; grid
residuals ~20 µs, so the target genuinely lies on the vblank grid).

The **feedback half works**: `vkGetPastPresentationTimingEXT` returns
`VkPastPresentationTimingEXT` for every `presentId` with the
`IMAGE_FIRST_PIXEL_OUT` stage time in the stage-local domain,
`reportComplete = TRUE`, ~100 % delivery; achieved times lie on the vsync
grid. Only the *control* half (`targetTime`) is inert.

## Expected behavior (spec)

- "If `targetTime` is not zero, the implementation attempts to align the
  `VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_VISIBLE_BIT_EXT` present stage of that
  presentation request with the time specified in `targetTime`."
- Without `PRESENT_AT_NEAREST_REFRESH_CYCLE`: the application "would strictly
  prefer the image to not be visible before `targetTime` has lapsed."
- With it: the implementation may compensate small precision errors by
  picking the nearest refresh cycle.
- With `PRESENT_AT_RELATIVE_TIME`: `targetTime` is a duration relative to
  when the present call is made.

So an image submitted with a relative target ~1 refresh period in the future,
whose rendering completed early, should be **held** and first become visible
at (or, NEAREST: adjacent to) the target — not one full cycle before it.

## Actual behavior

The image becomes visible at the earliest vsync at which it is ready,
regardless of the target. Steady state (app paced to one frame per 3 cycles
at 120 Hz, target = the pacer's intended slot `V(j)`): whenever the frame's
rendering happens to complete more than one cycle + compositor deadline
before `V(j)`, it displays at `V(j−1)` — one full refresh cycle (8.33 ms)
**before** the requested target. Depending on the run's slack distribution
this is 18–63 % of all frames; if the target were honored it would be 0 % by
definition. Distributions are statistically identical to a control with
`targetTime = 0`.

## Ruled out

| Hypothesis | Test | Result |
|---|---|---|
| NEAREST flag permits early display; strict mode would hold | Strict `RELATIVE` only | Unchanged (~60 % early) |
| Driver rounds to nearest cycle and our on-boundary targets tie ambiguously | Target biased to mid-cycle (`slot + T/2`) under NEAREST — the early slot is then 1.5 cycles from the target, never a legal "nearest" pick | Unchanged (~63 % early) → target value is inert, not rounded |
| Wrong target stage | `targetTimeDomainPresentStage` = `QUEUE_OPERATIONS_END` instead of `IMAGE_FIRST_PIXEL_OUT` (the only two advertised) | Unchanged (~59 % early) |
| Wrong time domain | Only one domain is advertised (`PRESENT_STAGE_LOCAL`) | Nothing else to test |
| DWM composition swallows it; flip path would honor | Application-controlled exclusive fullscreen (acquire succeeded) | Unchanged (~48–61 % early) |
| `FIFO_LATEST_READY` policy conflicts with holdback | Forced plain `VK_PRESENT_MODE_FIFO_KHR` | Unchanged (~54 % early) |
| Malformed request | Khronos validation layers on for every run | Zero errors |
| Absolute mode would work | Surface reports `presentAtAbsoluteTimeSupported = FALSE` in default **and** application-controlled exclusive modes (mode-aware caps query) | Not legally testable |

## Application-side mitigation

erhe now works around the issue with a software holdback (frame pacing claim
C15): after GPU submission, `vkQueuePresentKHR` is delayed until one refresh
period before the pacer's target present time, so the earliest feasible
vsync *is* the target. Config: `frame_pacing_present_holdback` (default
true). Verified in the reference simulation (75 % early displays → 0); on a
conforming driver the holdback degenerates to a no-op.

## Issue 2: refreshDuration grossly wrong after an app-driven mode change

`vkGetSwapchainTimingPropertiesEXT` reports a `refreshDuration` that is 4.2 %
away from the display's actual refresh period when the display mode was
changed **by the application** (SDL3 exclusive-fullscreen mode set), while
the same query reports the correct value when the same mode was set through
**Windows display settings** beforehand.

Reproduction (same environment as issue 1; secondary display, exclusive
fullscreen via `VK_EXT_full_screen_exclusive` application-controlled mode):

| How the 23.976 Hz mode was entered | `refreshDuration` reported | True period (measured) | Error |
|---|---|---|---|
| App-driven mode set (desktop at 120 Hz, SDL `SDL_SetWindowFullscreenMode` to the 3840×2160 @ 23.976 Hz mode) | 43,478,260 ns ≈ 43.478 ms (**exactly 23.000 Hz**) | 41.72 ms (23.976 Hz) | **4.2 %** |
| Same mode pre-set in Windows display settings, app fullscreen at desktop mode | 41,708,333 ns ≈ 41.708 ms | 41.72 ms | ~0.03 % |

The true period is measured from the extension's own past-presentation
feedback (`IMAGE_FIRST_PIXEL_OUT` achieved times, which lie cleanly on a
41.72 ms grid in both cases) — so the feedback path and the
`refreshDuration` query disagree with each other within the same extension.
The reported 43.478 ms is suspicious on its face: it is exactly 1/23 s, as
if derived from a truncated integer 23 Hz rather than the 24000/1001 NTSC
rate of the actual mode.

Consequence for applications: any pacing logic seeded from `refreshDuration`
runs with a 4.2 % period error — a full refresh period of accumulated drift
every ~24 frames. erhe now treats `refreshDuration` as a hint only and
re-seeds its tracked grid period from feedback deltas when they disagree
grossly (frame pacing claim C16).

## Questions for discussion

1. Is target-time holdback implemented at all in this driver generation, or
   is `presentAtRelativeTimeSupported = TRUE` advertised for a path that only
   implements the timing *queries*?
2. Is `presentAtAbsoluteTimeSupported = FALSE` (with the device feature TRUE)
   intentional for Win32 surfaces, and is absolute support planned?
3. Is there any driver-side state (control panel setting, flip-model
   requirement, HDR/MPO condition) that gates the holdback path?
4. Should `vkGetPastPresentationTimingEXT`'s `targetTime` echo the requested
   value? (Untested here; would confirm whether the request survives into the
   presentation queue at all.)
5. (Issue 2) Where does the post-mode-set 43.478 ms come from — a stale or
   integer-truncated mode entry? Is the query expected to reflect the actual
   fractional (24000/1001) rate, as it does when the mode is set via the OS?
