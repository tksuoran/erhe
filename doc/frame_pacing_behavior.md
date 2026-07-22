# Frame Pacer — Behavior Specification per Scenario

This document is deliverable 4 of the planning phase defined in [frame_pacing.md](frame_pacing.md): the normative, observable behavior of the frame pacer in each required scenario. Mechanisms are specified in [frame_pacing_algorithm.md](frame_pacing_algorithm.md); the control-theoretic grounding is [frame_pacing_control_model.md](frame_pacing_control_model.md). Numeric bounds use the default tunables (60 Hz examples, `T` = 16.67 ms); "measured" values are from the verification simulation (`scripts/frame_pacing_sim.py`, claims C1–C9).

Each scenario gives: the trigger, the required behavior (*shall*-statements — these are the acceptance contract for the implementation), quantitative bounds, and the **record signature**: what the scenario looks like in the frame-record data, so profiling tools and humans can recognize it.

## 1. Steady state

**Definition:** stationary load; per-stage p-quantiles at least `H` clear of a cadence boundary.

- Every displayed frame shall be visible exactly `N·T`; the cadence `N` shall not change.
- Believed self-miss rate shall not exceed the AIMD equilibrium `δ_dec/Δ_inc` (default 1 %); actual slot misses shall be rarer (the `guard`-wide near-miss band absorbs the tail). Measured: 0 misses in 960 frames (C1).
- Release times shall sit at `d_k − (L̂_p + m)`; average scheduling slack ≈ `m`, which saw-tooths in `[m_min, m_min + Δ_inc]` with decay period `Δ_inc/δ_dec` = 100 frames after each rare near-miss.
- FR4 predictions (`predicted_display`, `predicted_duration`) shall equal the realized display time and duration for every non-disturbed frame.

**Record signature:** constant `N`; pacer-wait durations ≈ `N·T` minus chain time; slack histogram with p-quantile ≈ `m`; achieved present slots consecutive at stride `N`.

## 2. Sustained load increase (CPU or GPU step)

**Trigger:** the p-quantile of either stage rises above `N·T − guard` (ramp) or frames start missing outright (abrupt step). CPU and GPU steps are handled identically — the decision statistic is `max(ĉ_p, ĝ_p)` and the streak attribution uses per-frame `max(c, g)`.

- Abrupt step: the cadence decision shall occur at the `M`-th (= 2nd) *observed* self-miss whose streak passes throughput attribution. Frames already in flight cannot be recalled: total late/slipped frames shall not exceed `M + 2F + 1` (= 7; measured 6 ≈ 100 ms of disruption) while the pipeline drains.
- Ramp: the predictive path (`W > N·T`) shall downshift with zero misses once ~`(1−p)·history` ≈ 6 elevated frames enter the window.
- New steady state (constant new `N`, no misses) shall be reached within 1 s (measured 193 ms, C2).
- The pacer shall not oscillate back: upshift requires per-frame headroom against `(N−1)·T − H` sustained for `T_up`, which a sustained load cannot satisfy.
- During the disturbed frames FR4 predictions may be stale by the slip amount; from the first re-anchored frame onward they shall be exact again.

**Record signature:** `M` self-misses with elevated own-stage times, then an `N` step-up, ≤ `2F` further late frames with *normal* stage times (queue drain), one frame visible `2·N·T` at re-anchor, then clean cadence at the new `N`.

## 3. Transient spike (single-frame impulse)

**Trigger:** one frame's stage time far above the distribution (e.g. shader compile, OS scheduling glitch); the following frames are normal.

- The cadence shall not change (min-over-streak attribution: the cascade frames' own stage times are normal and break the streak's evidence). Measured: cadence unchanged through a 30 ms spike at 60 Hz (C5).
- Disturbed frames = the spike itself plus the GPU-queue cascade, ≈ `⌈(S − budget)/(N·T − g)⌉ + 1` frames for spike size `S`, plus at most one slipped frame during re-anchoring (measured 3 total).
- The margin shall take one attack step per disturbed self-missing frame and decay back to its pre-spike level within `Δ_inc/δ_dec` frames per step; no permanent latency cost (C5).
- The window quantile shall move only negligibly (single outlier ≪ `(1−p)·history` samples): no predictive downshift.

**Record signature:** one frame with an outlier stage time, 1–2 successors late with normal stage times, margin bump decaying linearly, `N` flat.

## 4. Load drop (upshift)

**Trigger:** per-frame `max(c_k, g_k) ≤ (N−1)·T − H` for every frame during `T_up`.

- The pacer shall upshift exactly one cadence step per satisfied dwell period; the dwell restarts after each step. Time per step ≈ `T_up` + detection lag (measured 593 ms, C3).
- A single loaded frame during the dwell shall reset it (conservative by design; borderline load therefore never upshifts — see §5).
- The transition shall be miss-free: on upshift the new cadence's latency estimate is seeded with `max(carry-over, ĉ_p + ĝ_p)` and the margin gets a pre-emptive `Δ_inc` boost (measured 0 transition misses, C3).
- Underutilization bound (requirements FR1): after a load drop the pacer shall reach the minimum feasible `N` within `(N_start − N_final) · (T_up + detection lag)`.

**Record signature:** sustained headroom in per-frame stage times, dwell accumulating (visible as no upshift for `T_up`), then `N` stepping down one at a time with a small margin bump at each step.

## 5. Borderline load

**Trigger:** a stage quantile within `±H` of a cadence boundary `(N−1)·T`.

- The pacer shall park at the *higher* `N` (safe side) and stay there: zero cadence alternation, indefinitely (measured 0 changes over 40 s, C4). Rate is sacrificed, never stability — FR2a over FR2b.
- 1-2-1-2 visible-duration alternation shall not occur in any steady load, borderline included.

**Record signature:** `N` flat at the value above the boundary; per-frame stage times straddling `(N−1)·T − H`; dwell repeatedly resetting before reaching `T_up`.

## 6. Missed present

Two distinct cases, distinguished by the miss taxonomy (design §4):

**6a. Load-attributable miss** — chain genuinely late. Behavior: margin attack; streak counting toward reactive downshift (§2 applies if sustained; §3 if isolated). The pacer shall never *chase* a miss: no negative-slack scheduling to "catch up"; the missed frame's successors are re-anchored forward (one visibly longer frame, then clean).

**6b. Timing-only miss** — deadline missed although stage times fit the budget (estimator/margin/grid error, OS interference, corrupted state). Behavior: the cadence shall not change (attribution fails); the margin AIMD and estimator windows shall absorb the error. Under even adversarial corruption (latency estimate and margin forced to zero for 60 frames) the present-queue invariant `q ≤ Q* + F` shall hold unconditionally via the present-wait clamp, and recovery to steady state shall follow within 1 s of the corruption ending (measured: pending ≤ 2 of bound 3; recovery 15 ms; cadence untouched — C6).

**Record signature (6b):** self-misses with ample stage headroom — this pattern in the records is the diagnostic for "pacer timing wrong, load fine" and shall be surfaced as such to analysis tools.

## 7. Cold start

**Trigger:** pacer creation with empty state.

- Frames 0..`K−1` (default 8) shall run open loop: release immediately, target the next free slot, collect records. The present-wait clamp shall be active from frame 0 (queue safety needs no estimates).
- At frame `K` the pacer shall engage closed loop with `N` chosen from the seeded windows and margin `m0`.
- Steady state shall be reached within 1 s (measured 133 ms, C7). Early frames may slip while the grid phase converges (initial phase error is expected); these shall not produce cadence churn beyond the attribution rules.

**Record signature:** `K` back-to-back releases (burst), grid-phase residuals decaying geometrically (tracker gain 0.2 per feedback), then scheduling snapping to the grid.

## 8. Swapchain recreation

**Trigger:** resize, present-mode change, surface loss → `notify_swapchain_recreated()`.

- The pacer shall reset presentation-side state only: grid phase, feedback anchor, slot cursor, miss streak. It shall retain load state: `c/g/L` windows and margin (the workload did not change).
- Refresh period `T` shall be re-queried (it may differ on the new swapchain); if it changed, cadence-boundary state is re-evaluated on the next completions.
- Recovery shall be much faster than cold start (measured 15 ms vs. 133 ms, C7); no open-loop seed phase is needed.
- If recreation occurs with frames in flight, their late feedback (old swapchain ids) shall be ignored, not fed to the estimators.

**Record signature:** grid-phase discontinuity with immediate reconvergence; load windows continuous across the event.

## 9. Application-commanded minimum (`set_min_vsyncs`)

**Trigger:** app raises or lowers `N_min` (e.g. paused screen).

- Raising above the current `N` shall act as an immediate commanded downshift (next scheduled frame uses the new cadence). Lowering shall not change `N` directly — the normal upshift path (dwell + headroom) brings the cadence back down, so resuming from pause re-accelerates within `(N_pause − N_load) · T_up` plus detection.
- All scenario behaviors above apply unchanged with `N` floored at `N_min`.

## 10. Off mode

**Trigger:** capability baseline unmet (see [frame_pacing_capability_tiers.md](frame_pacing_capability_tiers.md)).

- `schedule()` shall return release = now, no target present time, no wait ids; no pacing decisions of any kind are made and no claims from §1–§9 apply.
- Frame records shall still be collected from whatever sources exist (CPU events always; GPU timestamps if available; no presentation feedback), so profiling remains useful and the FR4 fields shall be populated with best-effort wall-clock estimates, clearly flagged as unpaced.
- The interface shall be indistinguishable to the caller apart from a queryable `is_active()` / tier report; no code path in the app may depend on pacing being live.

## Cross-scenario invariants

These hold in *every* scenario, including off mode where marked:

| Invariant | Scope |
|---|---|
| `q ≤ Q* + F` (present-queue bound) | All paced scenarios, including corrupted estimates (C6). Not enforced in off mode (no wait mechanism). |
| No negative-slack chase after any miss | All paced scenarios |
| Cadence changes only via the three paths (reactive w/ attribution, predictive, dwell upshift) | All paced scenarios |
| Frame records complete and monotonic in the reference clock | Always, including off mode |
| `N ≥ N_min` | Always |
| Graceful degradation under delayed GPU-measurement delivery: steady state unaffected; step disruption bound relaxes to `M + 3F + 1`, settle bound unchanged, no oscillation | All paced scenarios (C9; nominal behavior assumes per-frame timestamp polling) |
