# Frame Pacer — Control-Theory Model

This document is deliverable 1 of the planning phase defined in [frame_pacing.md](frame_pacing.md): a model of the frame pacing problem in the framework of control theory. It defines the plant, signals, disturbances, and controller structure, states the stability and convergence properties the algorithm design must have, and lists the claims that must be verified in simulation before the plan can be approved. The algorithm design itself (deliverable 2) builds on this model but is a separate document.

## 1. System overview

Frame pacing is a sampled-data control problem: decisions are made once per frame, actuation is quantized to the display refresh grid, and the plant (the application's CPU→GPU frame production pipeline) has stochastic, time-varying execution times.

The controller decomposes into a cascade of three loops on separated timescales, plus a shared estimation stage:

```
                +------------------------------------------------------------+
                |                        Frame pacer                         |
                |                                                            |
                |  +---------------+  N_k    +----------------+  r_k, j_k    |
                |  | Outer loop    |-------->| Inner loop     |--------------+--> release app
                |  | cadence (FR1) |         | release        |              |    for frame k
                |  +---------------+         | schedule (FR2) |              |
                |        ^                   +----------------+              |
                |        |                        ^      |                   |
                |        | estimates              |      | target present    |
                |  +-----+------------+           |      | time (FR3)        |
                |  | Estimators:      |-----------+      v                   |
                |  | vsync grid V̂,T̂  |         +------------------+         |
                |  | stage times ĉ,ĝ  |         | Queue clamp      |         |
                |  | latency L̂, δ̂_pe |         | (FR5, hard)      |         |
                |  +------------------+         +------------------+         |
                |        ^                                                   |
                +--------+---------------------------------------------------+
                         |
              frame records + presentation feedback (sensors)
```

- **Inner loop (FR2)** — decides the release time `r_k` and target vsync slot `j_k` for each frame. Runs every frame (period `N_k·T`). Fundamentally a *prediction + margin* servo: the plant from release time to completion time has unit gain, so control quality is determined by the quality of the latency prediction, not by dynamics compensation.
- **Outer loop (FR1)** — decides the cadence `N_k` (vsyncs per frame). A hysteretic switching controller with dwell time. Switches on the timescale of hundreds of milliseconds.
- **Queue clamp (FR5)** — a hard saturation on the number of queued present images, enforced independently of the other loops' correctness (e.g. via `VK_KHR_present_wait`-style blocking). Functions as anti-windup for the whole system.
- **Estimators** — track the vsync grid, stage duration quantiles, and pipeline latency from the measured frame records and presentation feedback. All feedback flows through this stage; estimator dynamics are part of the loop dynamics and are analyzed as such.

A structural property worth stating up front: releases are **time-triggered, anchored to the vsync grid**, not completion-triggered (frame k+1 does not start "when frame k is done" but "at the scheduled time for slot j_{k+1}"). This breaks the positive-feedback path where one late frame delays the next release, which delays the next, producing a death spiral. Self-timed pipelines are marginally stable at best; grid-anchored pipelines reject one-frame disturbances structurally (§7.4).

## 2. Notation and signals

| Symbol | Meaning |
|---|---|
| `T` | Display refresh period (fixed; slow drift tracked, see §5.1) |
| `V_j = V_0 + j·T` | Vsync (scanout) times, `j` integer; `V̂` denotes the estimated grid |
| `k` | Frame index |
| `N_k ∈ {N_min, N_min+1, …}` | Vsyncs per frame (outer loop output); `N_min` is the application-set minimum |
| `j_k` | Target vsync slot for frame k; in steady state `j_{k+1} = j_k + N_k` |
| `δ_pe` | Presentation-engine deadline offset: how long before `V_j` the image must be complete to be presented at `V_j` (platform/path dependent, estimated) |
| `d_k = V̂_{j_k} − δ̂_pe` | Completion deadline for frame k |
| `r_k` | Release time (inner loop output): when the app may start per-frame work |
| `c_k`, `g_k` | CPU stage and GPU stage durations for frame k (random processes) |
| `L_k` | Measured release-to-GPU-end latency, including all waits (fence, acquire, scheduling) |
| `s_k = d_k − (r_k + L_k)` | Slack; frame k misses its slot iff `s_k < 0` |
| `m_k` | Safety margin (adaptive) |
| `q_k` | Present-queue occupancy: images submitted for presentation but not yet displayed |
| `Q*` | Target queued present images (tunable, default 1) |
| `x̂_p` | Estimated p-quantile of random quantity `x` (e.g. `ĉ_p`, `ĝ_p`, `L̂_p`) |
| `H` | Hysteresis width of the outer loop |
| `T_up` | Upshift confirmation (dwell) time |
| `M` | Reactive downshift threshold: consecutive missed-budget frames (default 2) |

All times are in the single monotonic clock domain required by the requirements document.

## 3. Plant model

### 3.1 The production pipeline

One frame passes through two pipelined stages: CPU (simulation + command recording, duration `c_k`) then GPU (execution, duration `g_k`). With `F` frames in flight (typically 2), CPU work of frame k overlaps GPU work of frame k−1. Two distinct constraints follow, and conflating them is the classic frame-pacing modeling error:

- **Throughput (sustainability):** a cadence of period `N·T` is sustainable iff each stage individually fits the period: `c ≤ N·T` and `g ≤ N·T` (up to the chosen quantile). The *sum* `c + g` does not have to fit `N·T`, because the stages are pipelined. The outer loop decides `N` against per-stage quantiles, i.e. against `max(ĉ_p, ĝ_p)`, not against `L̂_p`.
- **Latency (scheduling):** the release must precede the deadline by the whole-chain latency: `r_k + L_k ≤ d_k`. `L_k` includes pipeline interference (waiting for the frame-in-flight fence, GPU busy with the previous frame), so in steady state `L ≈ c + g + waits`, and it is `L`, not `c + g`, that the inner loop must predict. The inner loop uses `L̂_p`.

`c_k`, `g_k`, and hence `L_k` are modeled as stochastic processes: piecewise-stationary (stationary between load change points) with unknown, possibly heavy-tailed distributions. No distributional form is assumed; all decisions use empirical quantiles, which is what makes the approach distribution-free.

### 3.2 The presentation stage

The presentation engine is modeled as a **quantizer with a deadline**: an image completed at time `t` and requested for slot `j` is displayed at `V_j` iff `t ≤ V_j − δ_pe`; otherwise it slips to a later slot. The vsync grid quantizes actuation: any completion within `(previous deadline, d_k]` produces an identical result. This deadband is favorable — timing errors smaller than the distance to the deadline are absorbed completely, and the only externally observable error is the binary miss/hit of a slot. It also means the measured output is censored: hits reveal only `s_k ≥ 0` plus the measured slack; the *display* outcome carries one bit. Consequently the loops are driven primarily by the continuous internal measurement `s_k`, with slot misses as discrete events.

`δ_pe` differs between presentation paths (fullscreen flip vs. DWM-composited windowed; see §9) and is not documented; it is estimated from presentation feedback by correlating GPU-end times with achieved present times.

### 3.3 The present queue

Queue occupancy evolves as an integrator: `q` increments on each present request and decrements once per displayed frame (one per `N·T` in steady state). Because the inner loop targets exactly one frame per chosen slot and slots are consumed one per `N` vsyncs, arrival and departure rates are equal *by construction* — the queue is neutrally stable at its initial value rather than self-regulating. Excursions happen when a frame misses its slot (its display slips, the next frame queues behind it). Two mechanisms control `q`:

- **Slot re-anchoring (planned):** when feedback shows the achieved present slot lags the targeted slot, subsequent targets are re-anchored to the achieved grid position (skipping slots forward), draining the excess. This is a deadbeat correction: one re-anchor removes the accumulated error.
- **Present-wait clamp (hard, FR5):** the release of frame k additionally blocks until at most `Q*` images are pending display. This bounds `q` unconditionally — even if estimates, feedback, or the re-anchoring logic are wrong — and is the system's anti-windup: the integrator can never accumulate more than `Q* + F` frames of state.

The swapchain image count is a fixed input; it bounds how many images *exist*, not how many are *queued*, and plays no role in the control law beyond feasibility (`Q* + F` must not exceed it).

### 3.4 Actuators

| Actuator | Loop | Resolution / caveat |
|---|---|---|
| Release gating (wake app at `r_k`) | Inner | OS timer granularity + scheduler jitter, ~0.5–1 ms worst case on Windows with high-resolution timers; folded into the margin (§6.2) |
| Target present time request (`VK_EXT_present_timing` / `VK_GOOGLE_display_timing`) | Inner | Quantized to the vsync grid by the presentation engine |
| Present-wait blocking | Queue clamp | Exact (event-driven) |

### 3.5 Sensors

Every model signal is sourced from the frame records defined in the requirements document:

| Model signal | Source events |
|---|---|
| `r_k` | Frame pacer wait end |
| `c_k` | CPU slot end − CPU slot begin |
| `g_k` | GPU frame end − GPU frame begin (calibrated timestamps) |
| `L_k` | GPU frame end − frame pacer wait end |
| Fence/acquire wait components of `L_k` | Fence wait begin/end, acquire begin/end |
| Achieved present time → `V̂`, `T̂`, `δ̂_pe`, `q_k`, realized `s_k` | Presentation feedback (`VK_EXT_present_timing` / `VK_GOOGLE_display_timing`) |

Presentation feedback arrives with a delay of a few frames (the presentation engine reports after display). The vsync-grid and `δ_pe` estimators must tolerate this delay; the inner loop's per-frame slack signal does not depend on it (GPU-end vs. deadline is available as soon as the GPU finishes — *provided the implementation polls the timestamp query non-blockingly every frame*; reading results lazily at the frame-in-flight fence delays observation by ≈ `F` frames, a tolerated degradation verified as claim C9).

## 4. Control objectives, stated formally

Mapping the functional requirements onto control-theoretic objectives:

| Requirement | Control objective |
|---|---|
| FR2a (make the deadline) | Chance constraint: `P(s_k < 0) ≤ 1 − p` for target quantile `p`. Constraint satisfaction has priority over all performance objectives. |
| FR2b (minimize latency) | Performance: minimize `E[d_k − r_k]` (equivalently keep `E[s_k]` small and positive) subject to the chance constraint. |
| FR1 (cadence) | Supervisory: choose the minimum `N ≥ N_min` for which the chance constraint is feasible at sustainable throughput; `N_k` constant in steady state. |
| FR1 (no judder/oscillation) | No limit cycles in the switching loop: the closed loop under stationary load must converge to a constant `N` in finite time and stay there. 1‑2‑1‑2 cadence alternation *is* a period-2 limit cycle of the outer loop and is excluded by design (§7.2). |
| FR5 (queue depth) | Invariant: `q_k ≤ Q*` + bounded transient, guaranteed unconditionally (hard clamp), not merely in expectation. |
| FR4 (predicted display time) | Observer accuracy: predicted `(V̂_{j_k}, N_k·T̂)` equals the realized display time and duration whenever the chance constraint holds — i.e. FR4 is a byproduct of FR2a plus grid estimation, not a separate loop. |
| FR7 (recovery) | Bounded settling: after any modeled disturbance, return to steady state within the convergence tunable (default ≤ 1 s). |

The asymmetry demanded by the requirements — downshift fast, upshift deliberately — is the classic *fast-attack, slow-release* policy and appears in three places: the reactive downshift path (§6.3), the margin adaptation (§6.2), and the upshift dwell time.

## 5. Disturbance model

The design must be analyzed against, and the simulator must generate, the following disturbance classes:

| Class | Model | Primary responder |
|---|---|---|
| Per-frame jitter | Stationary noise on `c_k`, `g_k` (includes OS scheduling noise); possibly heavy-tailed | Absorbed by margin + quantile prediction; no cadence response |
| Impulse | Single-frame spike in `c` or `g` | At most one late frame; must *not* change cadence (§7.3) |
| Step | Sustained change in the distribution of `c` and/or `g` (load change, either direction) | Outer loop shifts `N`; inner loop re-converges estimates |
| Ramp / drift | Slow change in load, or vsync-grid drift | Tracked by estimators; no discrete response needed |
| Periodic | Background task beating against frame cadence | Absorbed if within quantile; else appears as borderline load (§7.2) |
| Structural | Missed present, cold start, swapchain recreation | FR7 recovery paths (§8) |

Measurement disturbances: presentation feedback delay (§3.5), GPU-timestamp calibration error (small, absorbed into margin), and censoring of display outcomes (§3.2).

## 6. Controller structure

### 6.1 Estimators (shared)

- **Vsync grid tracker**: estimates `V̂_0, T̂` from achieved present times — structurally a phase-locked loop with a low-gain integrator; `T` is nominally known from the display mode, so the tracker mainly maintains phase. Its bandwidth is far below all other loops and it can be treated as a static reference in their analysis.
- **Stage-quantile estimators** `ĉ_p`, `ĝ_p`, `L̂_p`: online quantile trackers of the form `x̂ ← x̂ + η·(p − 1{x ≤ x̂})` (stochastic gradient on the pinball loss). By stochastic-approximation theory this converges to the true p-quantile for stationary input; with a constant gain `η` (required, to track change points) the estimate has bounded steady-state ripple proportional to `η`. **Design constraint:** the ripple of the decision statistic must be smaller than the outer loop's hysteresis width `H` (§7.2). The frame-record history (default 120 frames) bounds the data window; the estimator gain, not the window, sets the effective time constant.
- **`δ̂_pe` estimator**: per presentation path (fullscreen / windowed), from correlating GPU-end times with hit/miss outcomes and achieved present times. Slowly varying; conservative (over-)estimate preferred, error is absorbed by the margin.

### 6.2 Inner loop — release scheduling (FR2)

Control law (per frame):

```
j_k  = j_{k−1} + N_{k−1}                      (next slot per current cadence, re-anchored on feedback, §3.3)
d_k  = V̂(j_k) − δ̂_pe
r_k  = clamp( d_k − L̂_p − m_k ,  lower: now,  upper: d_k − L_floor )
        …and additionally gated by the present-wait clamp (§3.3)
```

Because `s_k = d_k − r_k − L_k = (L̂_p + m_k) − L_k`, the plant from `r` to `s` is a **unit gain with additive noise**: there are no dynamics to compensate, and closed-loop behavior is entirely determined by the estimator `L̂_p` and the margin `m`. This reframes FR2 as a *quantile prediction problem with a safety allowance*, which is the central modeling insight of the inner loop.

Role separation, to prevent the two adaptive elements from fighting:

- `L̂_p` tracks the p-quantile of *measured* latency — fast timescale (per frame).
- `m_k` covers what the estimator cannot: estimation ripple, actuation jitter (§3.4), `δ̂_pe` error, non-stationarity between change points — slow timescale, adapted on *realized outcomes*: additive increase on a miss or near-miss (fast attack), slow decay toward `m_min` when slack is consistently ample (slow release). This is AIMD-shaped, prioritizing the constraint (FR2a) over the performance objective (FR2b) exactly as required. `m` is clamped to `[m_min, m_max]`; `m_0` is the cold-start tunable (20 % of `T`).

Integral windup in `m` is impossible by the clamp; windup in the schedule itself (releasing ever earlier while the queue backs up) is impossible by the present-wait clamp.

### 6.3 Outer loop — cadence (FR1)

Decision statistic: `W_k = max(ĉ_p, ĝ_p) + guard`, the throughput requirement per §3.1 (`guard` covers per-frame actuation/scheduling jitter). Feasibility of cadence `N`: `fit(N) ≡ W_k ≤ N·T`.

Switching law — a hysteretic relay with dwell time and an event-triggered override:

- **Predictive downshift**: if `¬fit(N_k)`, set `N ← min{N ≥ N_min : fit(N)}` immediately. (Estimator-driven; catches sustained load growth before misses accumulate.)
- **Reactive downshift**: if `M` consecutive frames miss their budget (default `M = 2`), set `N ← N_k + 1` immediately, regardless of estimates. (Event-driven; catches abrupt steps faster than any filtered estimate can, and satisfies the "downshift within ≤ 2 missed-budget frames" tunable by construction.)
- **Upshift**: only if `W_k ≤ (N_k − 1)·T − H` continuously for the dwell time `T_up`, then `N ← N_k − 1` (one step at a time, restarting the dwell). `H` is the hysteresis width; `T_up` is the upshift confirmation tunable (default 500 ms).
- `N` is floored at the application-set `N_min` at all times; a raised `N_min` (paused screen) acts as a commanded downshift and needs no special analysis.

### 6.4 Timescale separation

The cascade is analyzable loop-by-loop because the timescales are separated by roughly an order of magnitude each:

vsync grid tracker (seconds+) ≪ margin `m` decay (many frames) < quantile estimators (per frame, gain-limited) < inner release law (per frame, memoryless) — and the outer loop switches no faster than `M` frames down / `T_up` up. Each loop sees the slower ones as constants and the faster ones as converged. Violating this separation (e.g. margin adapting as fast as the quantile estimator) is the main way the design could introduce interaction instability; the algorithm design must preserve the ordering, and the simulator must include a test that perturbs the gains toward each other to find the separation limit.

## 7. Stability and convergence analysis

### 7.1 Inner loop

With the plant being unit-gain (§6.2), inner-loop stability reduces to estimator convergence:

- The quantile tracker is a Robbins–Monro recursion; for stationary `L` it converges to the p-quantile with steady-state ripple `O(η)`. No gain choice can make it unstable in the control sense (the recursion is a contraction in expectation for any `0 < η`, since the pinball subgradient is bounded); the trade-off is purely ripple vs. tracking speed.
- The margin AIMD recursion is bounded by its clamps and converges to a neighborhood of the smallest margin that satisfies the miss-rate constraint — the standard AIMD fixed-point argument. Because increase is event-driven (misses) and decrease is slow, the closed-loop miss rate is regulated to approximately the accepted rate `1 − p` without oscillation.

**Conclusion:** the inner loop cannot oscillate or diverge; its failure mode is only *transient* constraint violation while estimates catch up to a change point, which is the outer loop's and FR7's territory.

### 7.2 Outer loop — no limit cycles

The outer loop is a relay with hysteresis acting on the filtered statistic `W_k`. Chatter (including 1‑2‑1‑2 judder) requires the decision statistic to re-cross the switching boundary; this is excluded by three stacked mechanisms:

1. **Hysteresis:** upshift and downshift thresholds are separated by `H`. A limit cycle requires `W` to traverse the full band; steady-state estimator ripple is designed `< H` (§6.1), so stationary load cannot cause traversal. This yields the design rule tying `η` (estimator gain) to `H`.
2. **Dwell time:** upshift requires the condition to hold continuously for `T_up`. Any single excursion of `W` into the upshift region shorter than `T_up` causes no switch. Minimum time between an upshift and the next possible upshift is `T_up`; between upshift and downshift the reactive path can act fast — which is correct: safety may switch fast, performance may not.
3. **Safe-side bias for borderline load:** if the true load sits exactly at a cadence boundary, hysteresis parks the system at the *higher* `N` (lower rate): upshift needs `W ≤ (N−1)T − H` which borderline load does not satisfy, while staying at `N` is feasible. The system sacrifices rate, never stability — consistent with FR2a > FR2b priority.

A formal statement the algorithm design must preserve: *under load with stationary per-stage quantiles, the switching signal `N_k` changes value at most finitely many times, and the final value is the minimum feasible `N` if the load is at least `H` clear of the boundary, else the next higher `N`.*

### 7.3 Impulse rejection

A single-frame spike produces one self-caused miss, plus a short cascade: with `F` frames in flight, successors already released are serialized behind the spiked frame's GPU work, so a spike of size `S` disturbs roughly `⌈(S − budget)/(N·T − g)⌉ + 1` frames while the excess drains, plus at most one slipped frame during slot re-anchoring. This drain is pipeline physics, not a controller property. What the controller must guarantee: the quantile estimator moves by only `O(η)` per sample (bounded subgradient — a key property of quantile tracking vs. mean tracking, where one huge outlier drags the estimate), so `W` stays within hysteresis: no predictive downshift. The reactive path must attribute the *cascade* misses correctly — the successors' own stage times are normal, so they must not count toward the consecutive-miss threshold (see attribution in the algorithm design). The margin takes additive increases and decays back. **Net response: spike + bounded drain, cadence unchanged, margin briefly raised.** This is claim C5 below.

### 7.4 No death spiral

The positive-feedback failure (late frame → longer fence wait → later start → later frame …) is excluded structurally: `r_k` is computed from the *grid* (`d_k − L̂_p − m`), not from frame k−1's completion. A late frame k−1 can lengthen `L_k` (fence wait) once; frame k+1's release is unaffected unless estimates moved. Combined with slot re-anchoring and the present-wait clamp, one-frame lateness is a decaying, not amplifying, perturbation.

### 7.5 Queue boundedness

`q_k ≤ Q* + F` holds unconditionally by the present-wait clamp — a hard invariant independent of every estimate and both loops (§3.3). Convergence of `q` back to `Q*` after a miss is deadbeat via slot re-anchoring. This satisfies FR5's "enforced even when estimates are wrong" demand and is claim C6.

## 8. Disturbance and recovery scenarios (FR7)

| Scenario | Model response | Bound |
|---|---|---|
| Steady load | Inner loop converges; `N` constant; miss rate ≈ `1 − p`; slack quantile ≈ `m` | — (claim C1) |
| Step load increase (CPU or GPU) | Reactive downshift after ≤ `M` misses (abrupt step) or predictive downshift with zero misses (ramped step); estimators re-converge at new level | ≤ 2 missed-budget frames to respond; ≤ 1 s to steady state (C2) |
| Step load decrease | `W` falls; upshift after `T_up` per step of `N`; no misses during transition | `T_up` per step + estimator lag (C3) |
| Borderline load | Parks at higher `N`; no alternation | No limit cycle (C4) |
| One-frame spike | One late frame; no cadence change | (C5, §7.3) |
| Missed present | Slot re-anchor to achieved grid position; no catch-up attempt (never schedule negative slack to "make up" a miss); counts toward reactive-`M` | Queue restored in one re-anchor (C6) |
| Cold start | `N = N_min`; margin `m_0`; first `K` frames released immediately (open loop) to seed estimators, then close the loop. `Q*` clamp active from frame 0 | ≤ 1 s to steady state (C7) |
| Swapchain recreation | Grid/`δ̂_pe`/queue state reset; **stage-time estimates retained** (load did not change); re-converges like a cold start with warm load estimates | ≪ cold start (C7) |

## 9. Presentation-path variants

The model is identical for fullscreen (independent flip) and DWM-composited windowed presentation; the *parameters* differ:

- **Fullscreen / independent flip:** `δ_pe` is small; feedback reflects actual scanout; the model applies directly.
- **Windowed (DWM):** the compositor is an additional sampled stage running on its own vsync — modeled as a larger, more variable `δ_pe` plus one added period of display latency; presentation feedback may reflect composition rather than scanout. All of this is absorbed by the `δ̂_pe` estimator and the margin; no structural change to the loops. The achievable pacing quality is lower (larger effective deadline offset and feedback uncertainty); quantifying it per capability tier is deliverable 5.

The pacer detects the active path (where the platform reports it) or simply inherits it through the estimated `δ̂_pe`; the control laws do not branch on it.

## 10. Assumptions and limitations

1. Fixed refresh rate; grid drift is slow and tracked (VRR is out of scope by requirements).
2. `c_k`, `g_k` piecewise-stationary; the quantile approach is distribution-free but assumes *some* short-term statistical regularity. Adversarial per-frame alternation of load (period-2 load exactly matching the control period) would look like borderline load and resolves to the safe side.
3. Frame-to-frame correlation of load is not exploited (persistence prediction could start CPU later on average); the quantile baseline is deliberately conservative. Possible refinement, not required for correctness.
4. The CPU stage is modeled as one block; internal job-system parallelism appears only through `c_k`.
5. Cross-frame coupling through shared resources (fence waits, GPU contention) is captured *empirically* inside measured `L_k`, not structurally. This is adequate because the inner loop predicts `L` as observed, but it means `L̂_p` must be learned per cadence `N` or re-converged after a cadence switch (waits change when the period changes) — the algorithm design must address this (e.g. reset-with-prior or per-`N` estimates).
6. Actuation jitter (timer wake-up) is folded into `guard`/margin rather than modeled; on Windows this requires high-resolution timers and is bounded ~1 ms.
7. Presentation feedback delay of a few frames is tolerated by the estimators; the model assumes feedback *eventually* arrives for every presented frame (true for `VK_EXT_present_timing` / `VK_GOOGLE_display_timing`; off mode otherwise, in which no loop runs).
8. Off mode (FR6 baseline unmet) is not modeled: no control authority, no claims.

## 11. Claims to verify in simulation

The algorithm design (deliverable 2) is approved only when a deterministic simulation (virtual clock, synthetic `c/g` processes, modeled presentation engine per §3.2) demonstrates:

- **C1 — Steady state:** stationary load → constant `N`, miss rate ≤ `1 − p`, every displayed frame visible exactly `N` periods, slack p-quantile within tolerance of `m`.
- **C2 — Step up:** load step exceeding the budget → cadence decision after ≤ `M` = 2 *observed* misses (frames already in flight cannot be recalled: total late/slipped frames during the pipeline drain ≤ `M + 2F + 1`), new steady state within ≤ 1 s.
- **C3 — Step down:** load drop → upshift after `T_up` (± one estimator time constant) per cadence step; zero misses during the transition.
- **C4 — Borderline load:** load at a cadence boundary ± estimator ripple → settles at the higher `N`, zero cadence alternation over an arbitrarily long run.
- **C5 — Impulse:** single-frame spike → spike + GPU-queue cascade drain (§7.3) + ≤ 1 re-anchoring slip only, no cadence change, margin returns to pre-spike level.
- **C6 — Queue invariant:** under every scenario above plus adversarial estimate corruption (force `L̂` to zero), `q ≤ Q* + F` always; `q` returns to `Q*` within one re-anchor after a miss.
- **C7 — Cold start / recreation:** steady state within ≤ 1 s from empty state; faster after swapchain recreation with retained load estimates.
- **C8 — Gain/hysteresis margin:** measured steady-state ripple of `W` < `H` with the chosen `η`; report the ratio (design headroom). Additionally sweep gains toward each other to locate the timescale-separation limit (§6.4) and confirm the defaults sit well inside it.
- **C9 — Delayed measurement delivery:** with GPU measurements delivered via the lazy fence-read path (frame k's data first visible at frame k+F's fence wait) instead of per-frame polling, steady state is unaffected and a step response degrades gracefully: total late/slipped ≤ `M + 3F + 1`, settle ≤ 1 s, no oscillation. The nominal claims C1–C8 assume per-frame polling.
- **C10 — Heavy-tailed borderline load:** load whose per-stage q95 exceeds the `N·T` budget but which is sustainable in fact (tail absorbed by margin and queue elasticity, verified by the miss rate) must be held at the sustainable cadence: predictive downshift requires recent (near-)miss pressure, and upshift probes converge geometrically (each pressure-reverted probe doubles the next dwell; a surviving probe resets it).
- **C11 — Observer mode:** with the app ignoring every pacer decision (no release gating, no clamp), predictions must not drift: `|achieved − predicted|` stays bounded (a few refresh periods) indefinitely. Requires presentation feedback to be authoritative for the slot cursor, not a lower bound.

## 12. Parameter map

Model symbols vs. the tunables in the requirements document:

| Requirements tunable | Model symbol / role |
|---|---|
| Downshift reaction time (≤ 2 frames) | `M`, reactive downshift threshold (§6.3) |
| Upshift confirmation time (500 ms) | `T_up`, dwell time (§6.3) |
| Convergence after disturbance (≤ 1 s) | Settling-time bound in C2/C7; constrains estimator gain `η` from below |
| Latency safety margin initial (20 % of `T`) | `m_0`; `m ∈ [m_min, m_max]` adaptive (§6.2) |
| Minimum vsyncs per frame | `N_min` (§6.3) |
| Target queued present images (1) | `Q*` (§3.3) |
| Frame record history (120) | Data window bounding all estimators (§6.1) |
| — (new, introduced by the model) | `p` target quantile; `H` hysteresis width; `η` estimator gain; `guard` jitter allowance; `K` cold-start seed frames; `m_min`, `m_max` |

The parameters introduced by the model (bottom row) must be added to the tunable set by the algorithm design, with defaults justified against C1–C8.
