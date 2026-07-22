# Frame Pacer — Verification UI and Simulated Workload (requirements)

Requirements for an editor ImGui window that lets a human visually verify
frame pacer behavior on real hardware, and for an adjustable simulated
workload used to drive the pacer through its scenarios. This document
specifies requirements only; implementation and plan sequencing are out of
scope here. (Plan note: this work is to be inserted into
[frame_pacing_implementation_plan.md](frame_pacing_implementation_plan.md)
at the current point; it subsumes the optional P0.6 records overlay and
provides the CPU half of the P3.1 induced-load knobs.)

Related documents: [requirements](frame_pacing.md) (Data requirements section
defines the frame records this UI consumes), [algorithm](frame_pacing_algorithm.md)
(N, T, Q*, F, release/deadline terminology), [behavior](frame_pacing_behavior.md)
(the scenarios a user should be able to reproduce and recognize).

## Scope

In scope:

- One ImGui window in the desktop editor, fed by the existing frame time
  records (`erhe::frame_pacing::Frame_time_recorder`) and pacer decisions.
- A simulated CPU workload knob (extra per-frame wait).

Future work (design for it, do not implement now):

- Simulated GPU workload knob (P3.1 needs it; CPU-only is acceptable first).
- Exporting / persisting captured data for offline analysis.

Out of scope:

- General-purpose profiling (Tracy covers that). This window answers one
  question: "is the pacer doing what it claims?"
- Headless / Android presentation paths.

## Definitions

- **Record** — one frame's `Frame_time_record` (timestamps in reference-clock
  seconds; see requirements doc, Data requirements).
- **T** — display refresh period; **N** — vsyncs per frame (cadence);
  **slot** — frame-in-flight slot (F = 2 in the current engine).
- **Planned cadence** — N from the pacer's schedule decision for the frame.
- **Actual cadence** — achieved visible duration of the frame in refresh
  cycles, derived from presentation feedback: the delta between consecutive
  achieved present times, divided by T and rounded to the nearest integer.
- **Visibility midpoint** — achieved present time + (actual cadence − 1)·T/2,
  i.e. the average of the first and last refresh-cycle start at which the
  frame was on screen.

## Functional requirements

**U1 — Window.** There shall be a frame pacer ImGui window in the editor,
openable and closeable like other tool windows. Everything below lives in it.

**U2 — Simulated workload (extra per-frame wait).**

- The window shall provide controls for an extra CPU wait inserted into each
  frame: minimum and maximum wait time in milliseconds, each in [0, 60],
  constrained to min ≤ max. Default 0/0 (off).
- Per frame the inserted wait duration shall be drawn uniformly at random
  from [min, max]. (min = max gives a constant load; moving the sliders
  produces the step scenarios of the behavior document; jitter comes from
  min < max.)
- The wait shall be inserted inside the application's per-frame work span —
  after the pacer release (pacer waits, fence, acquire are not part of it) —
  so that the records count it as CPU service time and the pacer sees it as
  real load. A wait that lands in a span the service-time computation
  subtracts (see `Frame_time_record::cpu_service_time`) defeats the purpose.
- The wait shall be precise: actual duration within the jitter guard (~1 ms)
  of the requested duration. `std::this_thread::sleep_for` is unusable for
  this (P0.5 measured p99 wake error 15.3 ms); a high-resolution waitable
  timer or busy-wait is required. The dialed value must be the delivered
  value, otherwise the user cannot correlate knob position with pacer
  response.
- Desirable: a one-shot spike button (inject the configured maximum for a
  single frame) to reproduce the impulse scenario without touching sliders.

**U3 — Graphs.** The window shall show time-series graphs (time on x,
value on y; continuous step plots — each sample spans its frame's start and
end time):

- **Graph 1 — cadence:** refresh cycles per rendered frame, one sample per
  frame. Plot 1: planned cadence. Plot 2: actual cadence (see Definitions).
  Steady state shows the two plots overlapping at a constant integer;
  divergence is a slip, alternation is the judder the pacer must prevent.
- **Graph 2 — frame delta time:** milliseconds between consecutive achieved
  present times, one sample per frame. This is the user-visible pacing
  quality signal (flat line = smooth). Desirable second plot: delta between
  consecutive CPU slot begins, to expose scheduling jitter that presentation
  absorbs.
- **Graph 3 — input latency:** per frame, visibility midpoint minus the time
  the CPU was released to the application (pacer wait end when a wait ran,
  else CPU slot begin). This is the metric the present-wait clamp (FR5) and
  release gating (FR2b) exist to reduce; the clamp's effect must be visible
  here when toggling `frame_pacing_enforce`.

**U4 — CPU timeline.** A track showing which frame the CPU is working on at
every point in time, and at what high-level activity:

- Filled rectangle bars along a horizontal line while the CPU is active on a
  frame.
- Base color from the frame-in-flight slot (two slots: red and green; shall
  follow the engine's frames-in-flight count rather than hardcoding two).
- Color attenuated by activity: pacer wait dimmed, active work full
  brightness. Involuntary-wait sections inside the slot (fence wait, acquire,
  present request→return) marked with distinct outline colors around the
  bar segment.
- All section boundaries come from the record's timestamps; the timeline
  shall not invent events the records do not contain.

**U5 — GPU timeline.** As U4 but simpler: which frame (if any) the GPU is
executing at each point in time, base color by slot only (the records track
GPU busy per frame via the frame bracket, not GPU sub-activities). Span:
`gpu_frame_begin` to `gpu_frame_end` (calibrated).

**U6 — Display timeline.** Which frame (if any) the display is presenting
during each refresh cycle, derived from achieved present times and T:

- Base color from the frame-in-flight slot, like U4/U5.
- Attenuated by frame age: full brightness during the frame's first refresh
  cycle, half brightness for the remaining cycles of its visibility. A
  steady N = 3 cadence is then visually one bright cycle followed by two dim
  ones, repeating.

**U7 — Frame inspection (hover).** Hovering an active section in any of the
U4–U6 timelines shall:

- Highlight the same frame in all three timelines (white surrounding
  rectangles).
- Show a tooltip with all known information about the frame — the full
  record (every timestamp and derived value: service times, slack, planned
  and actual cadence, latency) — plus, when hovering the CPU timeline, the
  identity and duration of the hovered section.

**U8 — Shared time axis.** All graphs and timelines shall share one visible
time range, with mouse pan and zoom. Each shall draw a single horizontal
base line, and vertical lines marking display refresh cycle boundaries
(from the achieved-present grid / estimated phase; they are estimates and
may drift from true scanout). The view shall follow live data by default.
There shall be an explicit Pause/Unpause button governing all graphs and
timelines together: Pause freezes the displayed data (capture may continue
into history, U9), Unpause resumes following live data. In addition,
panning/zooming or inspecting (U7) shall implicitly pause the view — pan
and hover are useless on a moving target — and the button then shows the
paused state and offers the way back to following.

The refresh-cycle boundary lines shall come from the frame pacer's
DYNAMIC vsync grid (tracked phase + tracked period, algorithm doc
deviation 10), captured per frame and applied sample-locally, never from
a static anchor with the queried refresh period: the real period deviates
from the queried one (11 ppm measured), which drifts a static grid ~0.6 ms
per minute of distance from its anchor — visibly misaligned across a
paused or panned view.

**U9 — Data source and persistent capture.** The window consumes the
existing frame record ring and pacer decision output only — no second
instrumentation path (algorithm doc §9) — and copies them into a
persistent capture owned by the window. The capture is append-only and
unbounded (for now): data visible in a paused or panned view must never
disappear while the user is inspecting it, regardless of how long the
session runs. An explicit Clear control is the only way capture data is
dropped; the UI shall show the capture size (frames and memory). Where
data is missing — presentation feedback not yet delivered (~1 % of
frames), tier OFF, hidden window — the affected plot or timeline shall
show a gap, not fabricated values.

Future work (design the storage for it, do not implement yet):
serialization and deserialization of the capture. The format shall be
memory-efficient both at runtime and as a file — the capture is a dense
frame-id-indexed sequence of fixed-size samples, so a columnar,
delta-encoded layout (frame begin times as inter-frame deltas, other
timestamps as float milliseconds relative to their frame's begin) is the
intended direction rather than dumping structs.

## Non-functional requirements

- **Self-perturbation.** The window is itself CPU load and shall be treated
  as such: its cost must appear in the records as ordinary application work,
  and closing the window must remove it. It shall be cheap enough not to
  change the pacer's operating point on the development machine at steady
  state (guideline: well under one margin, i.e. < ~1 ms per frame).
- **Correctness over prettiness.** Every drawn value must be traceable to a
  record field or pacer decision; interpolation or smoothing that hides
  slips/misses is not acceptable.

## Acceptance criteria

A user, without reading logs, shall be able to:

1. Confirm steady state: graph 1's two plots constant and equal; graph 2
   flat at N·T; display timeline showing the regular bright/dim pattern.
2. Reproduce a load step with U2 and watch the downshift within the plan's
   bounds, then the upshift after the confirmation time — and recognize
   cadence flapping if it ever occurs.
3. See the present-wait clamp working: pending frames on the display/CPU
   timelines bounded by Q* + F, pacer-wait sections visible on the CPU
   timeline, and graph 3 shifting when `frame_pacing_enforce` is toggled.
4. Inspect any anomalous frame via U7 and read the full record.

## Tunable parameters

| Parameter | Proposed default | Notes |
|---|---|---|
| Extra wait range | 0–60 ms, min/max sliders, default 0/0 | U2; uniform per frame in [min, max] |
| Capture growth | unbounded until Clear | U9; persistent UI-owned copy, independent of the 120-frame decision ring |
| Slot base colors | red, green | U4–U6; per frame-in-flight slot |
| Age/wait attenuation | 50 % brightness | U4 pacer-wait dim and U6 non-first refresh cycles |
