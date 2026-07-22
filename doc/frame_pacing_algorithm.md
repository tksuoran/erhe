# Frame Pacer — Algorithm Design (verified)

This document is deliverable 2 of the planning phase defined in [frame_pacing.md](frame_pacing.md): the frame pacing algorithm design, verified against the control-theory model in [frame_pacing_control_model.md](frame_pacing_control_model.md). Verification status: **all model claims C1–C18 pass** in the deterministic reference simulation [`scripts/frame_pacing_sim.py`](../scripts/frame_pacing_sim.py) (60/60 checks; results in §8). C10 and C11 encode defects found by observer-mode soaking against the real engine (implementation plan step P2.1) — see deviations 7–9; C12 encodes the dynamic-grid requirement found by live grid measurement — see deviation 10; C13 encodes the slot-cursor runaway found by live inspection on a dropping presentation path — see deviation 11; C14 is the release-gating claim for that same dropping path (step P2.3): full pacing converts it to drop-free, one frame per cadence slot; C15 verifies the present-request holdback mitigation for engines that ignore target present times (measured driver behavior — see [frame_pacing_present_timing_driver_report.md](frame_pacing_present_timing_driver_report.md)); C16 verifies the gross-error re-seed for displays whose queried refresh period is grossly wrong (measured driver behavior, issue #2 of the same report) — see deviation 12. C17 verifies bounded closed-loop response to scanout-side display skips (measured live: ~5 % of vblanks slip one slot at scanout, vblank-count-periodic, uncorrelated with app timing) — with an important negative finding: the live cadence amplification does **not** reproduce with skips alone; C18 found and fixed its actual mechanism: the C15 holdback runs inline on the producer thread, gating the next decision so late that q95-budget slot reachability chronically skipped slots — the schedule slid into a stable one-slot-longer cadence (live 120 Hz: 4-slot lock at planned N=3; at N=1 the 1,2-gap sawtooth) — see deviation 13. The Python simulation is the executable reference for the later C++ implementation; the C++ unit tests shall replicate its scenarios and reproduce its verdicts.

Notation follows the model document (`T`, `N`, `j`, `d_k`, `r_k`, `L`, `s`, `m`, `q`, `W`, `H`, `T_up`, `M`, `Q*`, `F`).

## 1. Interface (backend agnostic)

The pacer is a pure, engine-independent object driven entirely through this interface. It never calls the OS, Vulkan, or a clock itself — the integration layer feeds it timestamps and executes its decisions. That is what makes it unit-testable against a virtual clock (requirements: Isolation and testability) and portable across Windows/Android backends.

```
schedule(frame_id, now) -> {
    release_time,        # earliest time the app may start per-frame work (FR2)
    target_slot,         # vsync slot index in the pacer's estimated grid
    target_time,         # requested presentation time, passed to the API (FR3)
    predicted_display,   # FR4: when this frame will be shown …
    predicted_duration,  #      … and for how long (N·T)
    N,                   # cadence used for this frame
    wait_id              # FR5: block release until this frame id has displayed
}
on_cpu_done(frame_id, cpu_duration)
on_gpu_done(frame_id, gpu_end_time, cpu_dur, gpu_dur, latency)   # latency = gpu_end − actual release
on_present_feedback(frame_id, achieved_present_time)
set_min_vsyncs(n)                    # FR1: application-set minimum
notify_swapchain_recreated()         # FR7
```

Integration mapping (details are deliverable 3): `release_time` is the pacer wait; `wait_id` maps to `VK_KHR_present_wait` / present-timing wait; `target_time` maps to `VK_EXT_present_timing` / `VK_GOOGLE_display_timing` target present time; `on_present_feedback` comes from the same extensions' feedback; `on_gpu_done` uses calibrated GPU timestamps. In off mode (FR6 baseline unmet), `schedule` returns release = now, no target time, and no waits.

Input-fidelity contract (normative, parity with the verification simulation):

- `cpu_dur` and `gpu_dur` are stage **service times** — involuntary waits (pacer wait, fence, acquire) and GPU idle bubbles excluded per [frame_pacing_inputs.md](frame_pacing_inputs.md) §3.2/§3.3. Raw spans contaminate the outer-loop statistic `W` with pacing-induced wait time and can cause spurious downshifts.
- `on_gpu_done` shall be driven by **per-frame non-blocking polling** of the frame's timestamp bracket at the schedule point, not by the lazy fence-time read; C9 quantifies the graceful degradation if the lazy path is used anyway.

## 2. State

| State | Purpose |
|---|---|
| `phase`, `period` | Estimated vsync grid: slot time `V(j) = phase + j·period`. Both tracked from presentation feedback (deviation 10): real displays deviate from the queried refresh period (11 ppm measured), so a static-period grid drifts ~0.6 ms/minute |
| `period_ref`, `period_samples` | Gross-error re-seed (deviation 12): `period_ref` is the PLL clamp center (nominal until a re-seed replaces it); `period_samples` is a small window of period estimates from raw feedback deltas backing the re-seed decision |
| `N`, `dwell` | Current cadence; accumulated upshift headroom time |
| `m` | Safety margin (AIMD-adapted) |
| `c_win`, `g_win` | Sliding windows (length = history, 120) of CPU/GPU stage durations |
| `L_win[N]` | Per-cadence sliding windows of measured release→GPU-end latency |
| `j_next`, `last_slot` | Slot cursor for the next target |
| `last_fb` | Latest presentation feedback: (frame id, achieved slot) |
| `consec_miss`, `streak_attr` | Self-miss streak length; min over the streak of per-frame max(c, g) |
| per-frame records | Ring buffer of the event timestamps required by the requirements document (also the data source for every window above) |

All estimators are **sliding-window order statistics** over the frame-record ring buffer, not constant-gain recursive trackers. This is a refinement of the model document (which described pinball-gradient trackers): the window quantile is exact, deterministic, needs no gain tuning, drops stale data completely `history` frames after a change point, and its steady-state ripple takes the role of the tracker ripple in the model's stability argument (C8 verifies ripple < `H`, measured headroom ≈ 4×). A constant-gain tracker cannot be simultaneously fast enough downward for upshift detection and low-ripple enough for hysteresis; the window statistic plus the *per-frame* upshift headroom test (§5) resolves that tension. The model's stability conclusions carry over a fortiori: the statistic is bounded, memoryless beyond the window, and has no feedback dynamics of its own.

Quantile convention: `q_p(win)` = the `⌈p·n⌉`-th smallest of the `n` window samples (conservative upper realization), `p = 0.95`.

## 3. Inner loop — release scheduling (FR2, FR3, FR4)

```
schedule(frame_id, now):
    if completed_frames < K:                     # cold start seed phase, §6
        j = max(last_slot + 1, first_slot_at_or_after(now))
        release = now
    else:
        budget = L̂ + m                           # L̂ = q_p(L_win[N]), fallback q_p(c)+q_p(g)
        j = j_next  (or first_slot_at_or_after(now) if unset)
        j = max(j, anchor(frame_id), last_slot + 1)          # §3.1
        typical = L̂_med + m                     # median latency (deviation 13)
        while V̂(j) − δ̂_pe − typical < now − guard:           # unreachable slot: skip forward
            j += 1
        release = max(now, V̂(j) − δ̂_pe − budget)
    j_next = j + N
    return decision(release, j, target_time = V̂(j),
                    predicted_display = V̂(j), predicted_duration = N·period,
                    wait_id = frame_id − 1 − Q*,
                    hold_until = V̂(j) − period + guard)    # C15 request holdback deadline
```

The deadline is `d = V̂(j) − δ̂_pe`; the release is `d − (L̂ + m)`, i.e. deadline minus predicted p-quantile latency minus adaptive margin — the pure prediction + margin servo of model §6.2. Slot **reachability** uses the *median* latency `L̂_med`, not the q95 budget (deviation 13): a spike-tail-inflated quantile must not skip slots the typical frame makes; a tail frame that does miss slips one slot and re-anchors, priced by the margin AIMD. `hold_until` is the C15 present-request holdback deadline, computed here so it uses the tracked grid; it is deliberately **not** capped to the next frame's release point (C18: the capped policy re-admits early displays whenever `budget > (N+1)·T − δ̂_pe − 2·guard`; past that bound only an asynchronous present decouples the holdback from the next decision). The present-wait clamp (`wait_id`) is enforced by the integration layer before the app starts; it is the FR5 hard invariant and is active from frame 0, including the seed phase and under corrupted estimates (C6: pending never exceeded `Q* + F`; measured max 2 of bound 3).

### 3.1 In-flight-aware anchoring

`anchor(frame_id) = achieved_slot(last_fb) + Σ N_i` over every frame scheduled after the fed-back frame, plus `N` for the frame being scheduled. Targets below the anchor are guaranteed to slip because the display cursor (FIFO) is already past them.

**The anchor is authoritative, not a lower bound (observer-mode finding, C11):** once feedback exists, `j = max(anchor, last_slot + 1)` — the internal cursor `j_next` is used only before the first feedback arrives. Taking `max(j_next, anchor)` lets the cursor ratchet ahead of reality without correction whenever the app runs faster than the schedule (observer mode, or any future scheduling error), producing unbounded prediction drift.

This formulation is a correction discovered during verification. The naive re-anchor — "on slip feedback, bump `j_next` to achieved + N" — is insufficient: it ignores frames already in flight whose targets are equally stale. The failure mode is severe and silent: after a single cold-start or spike slip, *every* subsequent frame displays exactly one slot after its target, indefinitely. Cadence looks perfect (all durations = `N·T`), so nothing recovers; meanwhile the release schedule sits one period early, the present-wait clamp binds every frame, and the effective safety slack collapses to noise (~41 % believed misses in the unfixed simulation). The in-flight-aware anchor removes the stale targets in one step: one frame shows `2·N·T` while the skipped slot drains, then the schedule is back in sync (C1: zero slips, zero misses over 16 s simulated).

### 3.2 Margin adaptation

On each `on_gpu_done`, with believed slack `s = d − gpu_end`:

```
if s < guard:  m ← min(m_max, m + Δ_inc)        # miss or near-miss: fast attack
else:          m ← max(m_min, m − δ_dec)         # ample slack: slow release
```

Equilibrium near-miss rate = `δ_dec / Δ_inc` (AIMD fixed point). Defaults `Δ_inc = 0.10·T`, `δ_dec = 0.001·T` give ≈ 1 % near-miss rate; actual slot misses sit well below that because the `guard`-wide near-miss band absorbs them (C1: 0 misses in 960 converged frames). `Δ_inc` trades recovery speed against the latency sawtooth: after a bump the margin decays over `Δ_inc/δ_dec = 100` frames, so larger `Δ_inc` means longer periods of conservatively early releases. The initial 0.25·T proposal produced a 4 ms sawtooth at 60 Hz; 0.10·T (1.7 ms) verified cleanly and is the revised default.

## 4. Miss classification and attribution

Verification forced a sharper taxonomy than the model's single "miss" event; this table is normative for the implementation:

| Event | Definition | Consequences |
|---|---|---|
| **Self-miss** | `s < 0`: this frame's own chain finished after its deadline | Margin attack; counts toward the reactive streak |
| **Near-miss** | `0 ≤ s < guard` | Margin attack only |
| **Cascade slip** | Frame ready in time (or late only because serialized behind a predecessor in the GPU/present queue), displayed after its target | No margin/streak effect; handled by anchoring |
| **Timing-only miss** | Self-miss, but the streak's stage times fit the budget (`streak_attr + guard ≤ N·T`) | Margin/estimator problem: never a cadence response |

`streak_attr` is the **minimum over the current miss streak of each frame's `max(c, g)`**. Reactive downshift requires every missed frame in the streak to individually exceed the budget. Min-attribution is the second verification-driven correction: max-attribution lets a single spike's cascade misses (whose own stage times are normal) satisfy the threshold and downshift on an impulse, violating C5. With min-attribution, C5 holds (impulse: 3 disturbed frames, cadence unchanged) and C6 holds (60 frames of forced-zero estimates: misses every frame, cadence untouched, because the load evidence is absent).

## 5. Outer loop — cadence (FR1)

Evaluated once per `on_gpu_done`:

```
W = max(q_p(c_win), q_p(g_win)) + guard          # throughput statistic, model §3.1
N_req = clamp(⌈W / period⌉, N_min_app, N_max)

if consec_miss ≥ M and streak_attr + guard > N·period:     # reactive downshift
    N ← max(N + 1, N_req);  reset streak
elif N_req > N:                                            # predictive downshift
    N ← N_req
elif N > N_min_app and max(c_k, g_k) ≤ (N−1)·period − H:   # upshift headroom
    dwell += elapsed;  if dwell ≥ T_up: N ← N − 1
else:
    dwell ← 0
```

Cadence budgets quantize by the **tracked** period, not the nominal `T` (deviation 12): with a grossly wrong nominal, load in the band between the true and nominal periods would otherwise look feasible at a cadence it cannot sustain — and the reactive path could never attribute the resulting misses (`streak_attr + guard` stays below the inflated nominal budget). Fraction-of-`T` tunables (`H`, margins, `guard`) keep the nominal scale.

- The upshift test is **per-frame** (`max(c_k, g_k)` of the just-completed frame), not the window statistic: "headroom sustained for `T_up`" means literally every frame in the dwell period had headroom. One loaded frame resets the dwell. This is what makes upshift immune to the window's ~2 s staleness after a load drop, giving C3's measured 593 ms ≈ `T_up` + detection lag.
- **Upshift backoff (observer-mode finding, C10):** an upshift that is reverted by a pressure-driven downshift within `4·T_up` doubles the required dwell for the next upshift; an upshift that survives `4·T_up` resets it to `T_up`. At persistently borderline load the pacer therefore *probes* the higher rate geometrically less often (finitely many probes) instead of flapping, while genuine load drops stay fast (first upshift is always `T_up`).
- **Predictive downshift requires pressure evidence (observer-mode finding, C10):** `W > N·T` alone does not downshift — a miss or near-miss (`s < guard`) must have occurred within the last `history` completed frames. Heavy-tailed load whose q95 exceeds the budget can still be fully sustainable (the tail is absorbed by margin and queue elasticity); misses and near-misses are the honest sustainability signal, and the margin AIMD surfaces overload as near-misses well before frames drop.
- The reactive path is the fast-attack: decision after exactly `M = 2` observed self-misses (C2). The predictive path catches ramps with zero misses once pressure appears: the window quantile crosses after ~`(1−p)·history` ≈ 6 elevated frames.
- Hysteresis `H` appears only in the upshift threshold; combined with `q_p`'s safe-side bias this parks borderline load at the higher `N` permanently (C4: 0 cadence changes over 40 s at a boundary-straddling load).
- On any cadence change: `dwell ← 0`; if `L_win[N_new]` is empty, seed it with `max(q_p(L_win[N_old]), q_p(c_win)+q_p(g_win))` (carry-over is an overestimate when downshifting — safe — and the structural sum floors it when upshifting); on upshift additionally `m ← m + Δ_inc` as a pre-emptive boost while the new cadence's latency distribution is learned (C3: zero misses through the transition).

## 6. Cold start, recovery, off mode (FR6, FR7)

- **Cold start**: first `K = 8` frames run open loop — release immediately, target the next free slot, collect records; the present-wait clamp is already active. Then engage closed loop with `N` chosen from the seeded windows. Measured time to steady state: 133 ms (bound 1 s).
- **Missed present**: never chase — a missed slot is re-anchored forward (§3.1), not compensated by scheduling negative slack. The miss feeds margin/streak/estimators per §4.
- **Swapchain recreation**: reset grid phase, `last_fb`, slot cursor, streak; **retain** `c/g/L` windows and margin (the load did not change). Measured recovery: 15 ms — effectively immediate, versus 133 ms cold (C7).
- **Off mode**: baseline extensions absent → `schedule` degenerates to release-immediately with no target time and no waits; records still collected. No control claims.
- **`set_min_vsyncs(n)`**: raises `N` immediately if below the new floor; upshift never goes below `N_min_app`. Acts as a commanded downshift; no other special handling.

## 7. Tunables (consolidated, post-verification defaults)

| Parameter | Symbol | Default | Notes |
|---|---|---|---|
| Reactive downshift threshold | `M` | 2 observed self-misses | Requirements table; decision latency verified in C2 |
| Upshift confirmation | `T_up` | 500 ms | Requirements table |
| Convergence bound | — | ≤ 1 s | Verified: worst measured 193 ms (C2) |
| Initial margin | `m0` | 0.20·T | Cold start only; adaptive thereafter |
| Margin bounds | `m_min`, `m_max` | 0.02·T, 0.50·T | |
| Margin attack / release | `Δ_inc`, `δ_dec` | 0.10·T, 0.001·T | **Revised** from 0.25·T (latency sawtooth, §3.2); equilibrium near-miss rate `δ_dec/Δ_inc` = 1 % |
| Min vsyncs per frame | `N_min` | 1 | Application-settable |
| Cadence cap | `N_max` | 8 | New; runaway backstop |
| Target queued images | `Q*` | 1 | Requirements table; invariant `q ≤ Q* + F` |
| History window | — | 120 frames | Backs all order statistics |
| Target quantile | `p` | 0.95 | Model §12 addition |
| Hysteresis width | `H` | 0.10·T | Ripple headroom ≈ 4× verified (C8) |
| Jitter guard | `guard` | max(1 ms, 0.05·T)* | Also the near-miss band (*sim uses 1 ms) |
| Cold-start seed | `K` | 8 frames | |
| Assumed deadline offset | `δ̂_pe` | 2.5 ms initial | Conservative; per presentation path; estimator refinement optional |
| Grid phase gain | — | 0.2 | Second-order PLL, phase branch |
| Grid period gain | — | 0.01 | Second-order PLL, frequency branch (per cycle); outlier-gated (innovation < `guard`), clamped to ±0.5 % of the reference period (nominal until a re-seed replaces it); period changes re-base the phase so the slot nearest the feedback keeps its time (deviation 10) |
| Grid re-seed window | — | 16 samples | Feedback-delta period estimates (`dt / round(dt/period)`, `1 ≤ dn ≤ 4`) backing the gross-error re-seed (deviation 12) |
| Grid re-seed threshold | — | 0.02 | Re-seed when the sample median deviates from the tracked period by more than this fraction of the reference period (4× the clamp band) |

## 8. Verification results

Deterministic simulation: virtual clock, two-stage pipelined plant (`F = 2` frames in flight, fence at `k−F`, serialized GPU), FIFO presentation engine quantized to a true vsync grid the pacer does not know (3.1 ms initial phase error, true `δ_pe` 1.5 ms vs. assumed 2.5 ms), presentation feedback delivered at display time, all pacer inputs delivered only when causally available (a GPU completion is not visible to a schedule decision that precedes it). 60 Hz, Gaussian stage times, fixed seeds.

| Claim | Scenario | Result |
|---|---|---|
| C1 steady state | c≈6 ms, g≈8 ms, 20 s | PASS — N constant, **0/960** misses, 100 % frames visible exactly T |
| C2 step up | g 8→22 ms | PASS — decision after 2 observed misses; 6 late/slipped total during pipeline drain (bound M+2F+1 = 7); settle 193 ms; stays at N = 2 |
| C3 step down | g 22→8 ms | PASS — upshift 593 ms after drop (`T_up` + detection); 0 misses in transition |
| C4 borderline | g ≈ 16.6 ms at T = 16.67 ms, 40 s | PASS — parks at N = 2, 0 cadence changes |
| C5 impulse | single g = 30 ms | PASS — 3 disturbed frames (spike + 2 GPU-queue cascade), cadence unchanged, margin returns to floor |
| C6 adversarial | L̂ and m forced to 0 for 60 frames | PASS — pending ≤ 2 (bound `Q*+F` = 3), no cadence response (attribution), recovery 15 ms |
| C7 cold start / recreation | empty state; recreation at 10 s | PASS — steady in 133 ms cold, 15 ms after recreation |
| C8 ripple vs hysteresis | steady-state W trace | PASS — ripple 0.42 ms < H 1.67 ms (headroom 4.0×) |
| C9 delayed delivery | GPU data via lazy fence read (frame k visible at k+F's fence wait) | PASS — steady state unaffected (0 misses, queue bound held); step: 7 late (bound M+3F+1 = 9), settle 227 ms, no oscillation |
| C10 heavy-tail borderline | 15 % of frames ~19 ms, rest ~12 ms at T = 8.33 ms (q95 above the N=2 budget, load sustainable in fact) | PASS — dwells at N=2, 2 cadence changes total (one converged probe pair), 0 in the last quarter, misses ≤ 1 % |
| C11 observer mode | app ignores release times and wait ids entirely | PASS — prediction drift bounded (max < 5·T), no growth over time |
| C12 dynamic grid | true period 200 ppm off nominal; 4 s feedback blackout mid-run | PASS — period estimate within 20 ppm of true; grid holds through the blackout (max error 0.000 ms vs ~0.8 ms untracked drift); cadence and misses unaffected |
| C13 latest-ready, fast app | app ~4× refresh rate on a dropping (fifo_latest_ready) presentation, FR5 clamp only | PASS — target lead bounded at 47 ms (bound 8·T = 133 ms) vs ~30 s runaway before the fix; no growth; display active (half the frames drop, by design of the mode) |
| C14 latest-ready, full pacing | same plant as C13 with release gating enforced (P2.3) | PASS — 0 drops after convergence (3360/3360 displayed), consecutive display times exactly one period apart (max gap error 0.000 ms), release lead 15.4 ms (bound 8·T), 0 self-misses |
| C15 inert-target engine + holdback | engine ignores target present times (measured driver behavior); GPU spike tail inflates the latency budget so typical frames carry > T of slack | PASS — without mitigation 75 % of frames display a slot early (cadence irregular, bounded); with the present-request holdback (request delayed to target − T + guard) 0 early, ≥ 98 % exactly on target, cadence stable, miss rate ≤ 2 % |
| C16 gross nominal-period error | nominal (queried) period 43.478 ms, true 41.708 ms — the measured 4.2 % driver error, ~8× the PLL clamp band; second arm with load between the true and nominal periods | PASS — period re-seeds to the measured true period (0 ppm residual), grid converges (tail max \|achieved − predicted\| = 0.000 ms), pacing healthy; ambiguity-band load correctly downshifts to N=2 with 0 tail misses (with re-seed disabled: period stuck at the full 40716 ppm error, 29 ms grid error, no downshift) |
| C17 scanout-side display skips | live 120 Hz N=1 operating point (light load, fifo_latest_ready, full pacing + holdback, on-cadence clamp); 5 % random skips and vblank-count-periodic (every 21st) arms | PASS — response bounded in both arms: ≥ 89.5 % exact 1-slot gaps (ambient skips 5 %), mean gap ≤ 1.11 slots, 0 self-misses, drops bounded, no limit cycle, release lead bounded. **Negative finding**: the live cadence amplification does not reproduce with skips alone — its actual mechanism is C18's |
| C18 inline (serialized) holdback | C15 spiky N=3 load with the holdback serialized on the producer thread (`holdback_serialized`, 1 ms tick overhead) + 5 % skips; second arm at the N=1 light-load sawtooth regime | PASS — no cadence lock: mean gap 3.10, 73 % exact 3-gaps, 0 early displays, N stable (the unfixed loop locks at mean ~4.0, < 15 % exact — reproducing the live 120 Hz 4-slot lock); spike slips ≤ 9 % ≈ the spike rate (they physically cannot make the serialized window); N=1 arm clean (mean 1.077, 92.6 % exact — the live sawtooth was ~50 %). **Verified live**: mean gap 3.01, 116/119 exact, 0 early, N=3 stable after the fix |

### Deviations from and refinements to the model document

1. **In-flight-aware anchoring (§3.1)** — the model's re-anchor was underspecified; the naive form produces a stable-looking but permanently degraded one-slot-lag regime. Model §3.3's "deadbeat correction" is achieved only by the in-flight-aware form. This is the highest-value finding of the verification.
2. **Min-over-streak attribution (§4)** — required to keep C5 (impulse) and C2 (step) simultaneously satisfiable; max-attribution conflates cascades with load.
3. **Impulse/step physical bounds** — a spike disturbs `⌈(S − budget)/(N·T − g)⌉ + 1` frames via the GPU queue, and a step disturbs up to `M + 2F` frames while the pipeline drains; the controller cannot recall in-flight frames. Model doc C2/C5 and §7.3 updated accordingly. The *decision* latencies (2 observed misses; no cadence change on impulse) hold exactly.
4. **Windowed order statistics (§2)** replace constant-gain quantile trackers, with the upshift condition moved to a per-frame test; C3 and C8 jointly confirm this resolves the fast-down/low-ripple tension the tracker could not.
5. **`Δ_inc` default revised** 0.25·T → 0.10·T (§3.2).
6. **Measurement-delivery contract (§1)** — the nominal claims assume `on_gpu_done` arrives at the first schedule point after the GPU finishes (per-frame polling of the timestamp bracket). The lazy fence-read path is a verified graceful degradation (C9), not the nominal mode, and the service-time input definitions are normative — both were found in review, not in the original model.
7. **Pressure-gated predictive downshift (§5, C10)** — found by observer soaking: the real editor's heavy-tailed load had q95 above the 2-vsync budget yet zero misses (tail absorbed by margin + queue elasticity); quantile-only downshifting flapped against the upshift path. The quantile is now an early-warning trigger only in the presence of recent (near-)miss pressure.
8. **Upshift backoff (§5, C10)** — the per-frame headroom test can complete a dwell by luck under rare-tail load; each pressure-reverted upshift doubles the next probe's dwell (geometric convergence), a surviving upshift resets it.
9. **Anchor made authoritative (§3.1, C11)** — found by observer soaking: with `max(j_next, anchor)` the slot cursor ratcheted ~49 s ahead of reality over minutes of unpaced running. Feedback now overrides the cursor outright.
10. **Dynamic grid: tracked period (§2, C12)** — found by live measurement (P2.2-era verification UI): the development display's actual refresh period is 11 ppm longer than the queried one, so a grid with the queried period drifts +10.3 µs/s (~0.6 ms/minute, a full period every ~13 minutes) — visible as misaligned refresh lines in the UI and grid error across feedback gaps. The phase PLL gains a frequency branch (second-order PLL): the innovation, normalized per elapsed cycle, corrects the period estimate; outlier innovations (≥ `guard`, i.e. slips/compositor hiccups) are excluded from the frequency branch; the estimate is clamped to ±0.5 % of nominal; and every period change re-bases the phase so the slot nearest the feedback keeps its time (slot indices are absolute — an un-rebased period change of `dP` moves slot `j` by `j·dP`, which is seconds at real reference-clock epochs). The tracked period survives swapchain recreation (it is a display property). Grid consumers (UI refresh lines, FR4 predictions) must use the tracked pair, not the nominal period.
11. **Slot floor removed from the feedback path (§3.1, C13)** — found by live inspection (frames 2836/2837 in the verification UI): with the app running faster than the refresh rate on `VK_KHR_present_mode_fifo_latest_ready`, superseded images are dropped and consume no display slot, but `j = max(anchor, last_slot + 1)` forces at least one slot per scheduled frame — the cursor ratcheted ~10 s ahead of reality (release times, targets, and believed slack all +10 s; margin pinned at the floor; miss detection dead). When feedback exists, the anchor is now the slot verbatim: its per-in-flight `+N` accounting already yields strictly increasing targets between feedbacks. The `last_slot + 1` floor remains only on the no-feedback path (cold start, feedback blackout, post-recreation). Prerequisite for P2.3: release gating on the runaway schedule would have stalled the app for the accumulated lead.
12. **Gross-error period re-seed (§2, §5, C16)** — found live while testing app-driven fullscreen mode selection: after an SDL mode set to 23.976 Hz from a 120 Hz desktop, the driver reports `refreshDuration` = 43.478 ms (23.000 Hz) while achieved-present feedback shows the true period is 41.708 ms — a 4.2 % error ([driver report](frame_pacing_present_timing_driver_report.md) issue #2). The ppm-scale PLL (deviation 10) cannot recover: every innovation is pinned above the `guard` outlier gate (the frequency branch is completely dead) and the clamp band is centered on the wrong nominal, so the phase branch absorbs ~1.7 ms per frame forever. Fix: period samples from **raw feedback deltas** (`dt / round(dt/period)` — phase-independent, and immune to slips since a skipped slot moves `dt` and `dn` together); when the median of the sample window disagrees with the tracked period by more than 4× the clamp band, period and phase are re-seeded from the measurement and the clamp re-centers on the measured period (`period_ref`). Companion change: cadence budgets (`N_req`, reactive attribution, upshift headroom, predicted duration) quantize by the tracked period, not the nominal — see §5. Delta-based estimation cannot distinguish integer-ratio aliases (e.g. a nominal at half the true rate); the re-seed handles fractional gross errors up to ~±30 % at `dn = 1`. The integration also routes the tracked period to the present-request holdback (C15): a holdback interval derived from the wrong queried duration can release the request before the previous vblank's deadline, defeating the mitigation.
13. **Typical-latency slot reachability + pacer-computed holdback deadline (§3, C18)** — found live at 120 Hz N=3: the display ran a stable **4-slot cadence at planned N=3** with achieved ≡ target (the pacer never saw a miss, margin at floor). Mechanism, reproduced in sim only after modeling the integration's thread structure: the C15 holdback runs **inline on the producer thread**, so the next frame's decision waits for the previous frame's present request (~`target − T`); with slot reachability judged by the q95 budget (spike-tail-inflated to ~31 ms against ~23 ms typical latency) the anchor slot was chronically "unreachable", the skip-forward fired every frame, and the schedule slid one slot into a self-sustaining longer cadence (at N=1: the alternating 1,2-gap sawtooth). Fix: reachability uses the **median** latency + margin; the q95 budget still sets the release. The holdback deadline `hold_until` moved into the schedule decision (tracked grid, deviation 12) and is deliberately not capped to the next release point — the sim rejected the capped policy (re-admits ~50–60 % early displays when `budget > (N+1)·T − δ̂_pe − 2·guard`). Past that budget bound the inline holdback is structurally lossy (tail frames slip; C18 accepts ≤ 9 % ≈ spike rate); recovering both exact holdback and full-budget scheduling there requires presenting from a separate thread (same queue family, no ownership transfer — future work if measurements demand it). Live after the fix: mean gap 3.01, 0 early, N=3 stable.

## 9. Implementation-phase notes

- Port the pacer as a single pure class mirroring §1's interface; the C++ unit/smoke tests shall re-implement the simulation's plant and scenarios and assert the same C1–C8 verdicts (parity with `scripts/frame_pacing_sim.py`, same constants; RNG streams need not match — assertions are on invariants and bounds, not sample paths).
- The frame-record ring buffer required by the requirements document is the same structure backing the estimator windows; no second data path.
- Integration points (Vulkan extension sourcing per capability tier, thread/timer specifics, windowed vs. fullscreen `δ̂_pe`) are deliverables 3–5 and intentionally absent here.
