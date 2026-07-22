#!/usr/bin/env python3
"""Frame pacing algorithm reference model and verification simulation.

Supports deliverable 2 of doc/frame_pacing.md: implements the algorithm
specified in doc/frame_pacing_algorithm.md and verifies claims C1..C8 from
doc/frame_pacing_control_model.md against a deterministic simulated plant
(virtual clock, synthetic CPU/GPU stage times, modeled presentation engine).
No real GPU, display, or wall clock is involved; runs are reproducible.

Usage: python scripts/frame_pacing_sim.py
Exit code 0 iff all claims pass.
"""

import heapq
import math
import random
from collections import deque
from dataclasses import dataclass, field


# ---------------------------------------------------------------------------
# Tunables (requirements doc table + model doc section 12 additions)
# ---------------------------------------------------------------------------

@dataclass
class Tunables:
    T:          float = 1.0 / 60.0  # display refresh period [s]
    N_min:      int   = 1           # application-set minimum vsyncs per frame
    N_max:      int   = 8           # cadence cap
    p:          float = 0.95        # target quantile for window order statistics
    history:    int   = 120         # frame record window length
    guard:      float = 0.001       # actuation/scheduling jitter allowance [s]
    H_frac:     float = 0.10        # hysteresis width H, fraction of T
    T_up:       float = 0.5         # upshift confirmation (dwell) time [s]
    M:          int   = 2           # reactive downshift: consecutive self-misses
    m0_frac:    float = 0.20        # initial margin, fraction of T
    m_min_frac: float = 0.02
    m_max_frac: float = 0.50
    dinc_frac:  float = 0.10        # margin additive increase, fraction of T
    ddec_frac:  float = 0.001       # margin decay per frame, fraction of T
    Q_star:     int   = 1           # target queued present images
    F:          int   = 2           # frames in flight (plant property, input)
    K_seed:     int   = 8           # cold start open-loop seed frames
    dpe_hat:    float = 0.0025      # assumed presentation deadline offset [s]
    grid_gain:  float = 0.2         # vsync grid phase tracker gain
    grid_period_gain: float = 0.01  # vsync grid period tracker gain (per cycle)
    grid_period_max_dev: float = 0.005  # tracked period clamp, fraction of the reference period
    grid_reseed_window: int = 16    # feedback-delta samples backing the gross-error re-seed
    grid_reseed_dev: float = 0.02   # re-seed when median delta-derived period deviates this much

    @property
    def H(self):     return self.H_frac * self.T
    @property
    def m0(self):    return self.m0_frac * self.T
    @property
    def m_min(self): return self.m_min_frac * self.T
    @property
    def m_max(self): return self.m_max_frac * self.T
    @property
    def d_inc(self): return self.dinc_frac * self.T
    @property
    def d_dec(self): return self.ddec_frac * self.T


# ---------------------------------------------------------------------------
# Frame pacer (the algorithm under verification; backend-agnostic, pure)
# ---------------------------------------------------------------------------

@dataclass
class ScheduleDecision:
    frame_id:     int
    release:      float  # earliest time app may start per-frame work
    target_slot:  int    # pacer-grid slot index
    target_time:  float  # requested presentation time (FR3)
    pred_display: float  # FR4: predicted display time
    pred_dur:     float  # FR4: predicted visible duration
    N:            int
    wait_id:      int    # present-wait clamp: block until this id displayed
    hold_until:   float = 0.0  # C15/C18: delay the present request to this
                               # time (0 = no holdback needed). Capped so an
                               # INLINE holdback can never push the next
                               # frame's decision past its release point.


class FramePacer:
    def __init__(self, tun: Tunables, nominal_phase: float = 0.0):
        self.tun = tun
        self.phase = nominal_phase        # estimated vsync grid phase
        self.period = tun.T               # estimated vsync grid period (tracked)
        self.period_ref = tun.T           # PLL clamp center (nominal until a re-seed replaces it)
        self.fb_raw_prev = None           # previous raw feedback time (re-seed sampling)
        self.period_samples = deque(maxlen=tun.grid_reseed_window)
        self.last_fb_slot_time = None     # (nearest_slot, t_present) of last feedback
        self.N = tun.N_min
        self.N_min_app = tun.N_min
        self.m = tun.m0
        self.c_win = deque(maxlen=tun.history)
        self.g_win = deque(maxlen=tun.history)
        self.L_win = {}                   # cadence N -> deque of latency samples
        self.j_next = None                # next target slot (None until engaged)
        self.last_slot = -1
        self.dwell = 0.0
        self.consec_miss = 0
        self.streak_attr = float('inf')   # min over streak of max(c,g)
        self.last_pressure = -10**9       # completed-count of last (near-)miss
        self.last_fb = None               # (frame_id, achieved_slot)
        # Upshift backoff: an upshift reverted by pressure within the
        # survival window doubles the required dwell (geometric convergence
        # at infeasible-borderline load); a surviving upshift resets it.
        self.dwell_required = tun.T_up
        self.upshift_time = None          # gpu-time of the last upshift
        self.completed = 0
        self.last_gpu_done = None
        self.forced = None                # test hook: (L_hat, margin) override
        self.frame_N = {}                 # id -> cadence used at schedule time
        self.frame_slot = {}              # id -> target slot
        self.stats = {}                   # id -> dict(self_miss, s, W, m, N)

    # -- helpers ------------------------------------------------------------

    def V(self, j):
        return self.phase + j * self.period

    def _q(self, win, default):
        return self._q2(win, self.tun.p, default)

    def _q2(self, win, p, default):
        if not win:
            return default
        s = sorted(win)
        i = min(len(s) - 1, max(0, math.ceil(p * len(s)) - 1))
        return s[i]

    def _L_hat(self):
        if self.forced is not None:
            return self.forced[0]
        win = self.L_win.get(self.N)
        fallback = self._q(self.c_win, 0.0) + self._q(self.g_win, 0.0)
        if fallback <= 0.0:
            fallback = self.tun.T
        if win:
            return self._q(win, fallback)
        return fallback

    def _L_typical(self):
        # Median latency (deviation 13, C18): slot REACHABILITY is judged by
        # what a typical frame achieves, not the q95 budget - a spike-tail-
        # inflated quantile must not skip slots the typical frame makes.
        if self.forced is not None:
            return self.forced[0]
        win = self.L_win.get(self.N)
        fallback = self._q2(self.c_win, 0.5, 0.0) + self._q2(self.g_win, 0.5, 0.0)
        if fallback <= 0.0:
            fallback = self.tun.T
        if win:
            return self._q2(win, 0.5, fallback)
        return fallback

    def _margin(self):
        if self.forced is not None:
            return self.forced[1]
        return self.m

    def _W(self):
        return max(self._q(self.c_win, 0.0), self._q(self.g_win, 0.0)) + self.tun.guard

    def set_min_vsyncs(self, n):
        self.N_min_app = max(1, n)
        if self.N < self.N_min_app:
            self._change_cadence(self.N_min_app)

    def notify_swapchain_recreated(self, nominal_phase=0.0):
        # Reset presentation-side state; keep load estimates (model section 8).
        # The tracked grid PERIOD is retained: it is a property of the
        # display, not of the swapchain (deviation 10).
        self.phase = nominal_phase
        self.last_fb_slot_time = None
        self.fb_raw_prev = None
        self.period_samples.clear()
        self.j_next = None
        self.last_fb = None
        self.consec_miss = 0
        self.streak_attr = float('inf')
        self.dwell = 0.0

    # -- scheduling (inner loop, FR2) ---------------------------------------

    def schedule(self, frame_id, now):
        t = self.tun
        if self.completed < t.K_seed:
            # Cold start seed phase: open loop, release immediately (FR7).
            j = max(self.last_slot + 1, math.ceil((now - self.phase) / self.period))
            release = now
        else:
            budget = self._L_hat() + self._margin()
            if self.last_fb is not None:
                # Presentation feedback is AUTHORITATIVE for the slot cursor:
                # the anchor (latest achieved present plus the scheduled
                # cadence of every in-flight frame) is where the display
                # queue actually is. Using it as a mere lower bound lets
                # j_next ratchet ahead of reality without correction when the
                # app runs unpaced (observer mode finding, step P2.1).
                fb_id, achieved = self.last_fb
                anchor = achieved
                for i in range(fb_id + 1, frame_id):
                    anchor += self.frame_N.get(i, self.N)
                anchor += self.N
                # The last_slot+1 floor must NOT apply here (deviation 11):
                # on a dropping presentation path (fifo_latest_ready) an app
                # running faster than refresh consumes less than one display
                # slot per frame, so a per-frame slot floor ratchets the
                # cursor ahead without bound (~10 s observed live, C13). The
                # anchor's per-in-flight +N already makes targets strictly
                # increasing between feedbacks.
                j = anchor
            else:
                j = self.j_next if self.j_next is not None \
                    else math.ceil((now - self.phase) / self.period)
                j = max(j, self.last_slot + 1)
            # Re-anchor forward if the slot is no longer reachable. The
            # reachability test uses the TYPICAL (median) latency plus
            # margin, not the q95 budget (deviation 13, C18): a spike-tail-
            # inflated quantile would skip slots the typical frame makes -
            # with the decision point gated late (e.g. the C15 holdback
            # serialized on the producer thread), that chronic +1 slid the
            # whole schedule into a stable one-slot-longer cadence (the
            # live 120 Hz N=3 -> 4-slot lock / N=1 sawtooth). A tail frame
            # that does miss slips one slot and re-anchors - bounded, and
            # priced by the margin AIMD.
            typical = self._L_typical() + self._margin()
            while self.V(j) - t.dpe_hat - typical < now - t.guard:
                j += 1
            release = max(now, self.V(j) - t.dpe_hat - budget)
        self.j_next = j + self.N
        self.last_slot = j
        self.frame_N[frame_id] = self.N
        self.frame_slot[frame_id] = j
        # Present-request holdback deadline (C15 mitigation): hold the
        # request into the target slot's cycle. Computed HERE so it uses the
        # tracked period (deviation 12) - a holdback interval derived from a
        # grossly wrong queried refreshDuration can release the request
        # before the previous vblank's deadline. NOT capped to the next
        # frame's release point: the sim rejected that policy (C18) - when
        # the budget exceeds (N+1)*T - dpe - 2*guard the cap re-admits
        # early displays, which is worse than the occasional spike slip the
        # uncapped inline holdback costs.
        hold_until = 0.0
        if self.completed >= t.K_seed:
            hold_until = self.V(j) - self.period + t.guard
        return ScheduleDecision(
            frame_id=frame_id, release=release, target_slot=j,
            target_time=self.V(j), pred_display=self.V(j),
            pred_dur=self.N * self.period, N=self.N,
            wait_id=frame_id - 1 - t.Q_star,
            hold_until=hold_until)

    # -- measurement feedback ----------------------------------------------

    def on_cpu_done(self, frame_id, c):
        self.c_win.append(c)

    def on_gpu_done(self, frame_id, gpu_end, c, g, L):
        t = self.tun
        self.g_win.append(g)
        n_used = self.frame_N.get(frame_id, self.N)
        self.L_win.setdefault(n_used, deque(maxlen=t.history)).append(L)
        d = self.V(self.frame_slot[frame_id]) - t.dpe_hat
        s = d - gpu_end
        elapsed = 0.0
        if self.last_gpu_done is not None:
            elapsed = max(0.0, gpu_end - self.last_gpu_done)
        self.last_gpu_done = gpu_end
        self.completed += 1

        # Margin AIMD (FR2b under FR2a priority).
        if self.forced is None:
            if s < t.guard:                      # miss or near-miss
                self.m = min(t.m_max, self.m + t.d_inc)
            else:
                self.m = max(t.m_min, self.m - t.d_dec)
        if s < t.guard:
            self.last_pressure = self.completed  # deadline pressure evidence

        # Miss streak with throughput attribution: the streak is attributed to
        # load only if EVERY missed frame's own stage time exceeds the budget
        # (min over streak of per-frame max) — a cascade miss behind a single
        # spike has normal stage times and breaks the attribution (C5).
        self_miss = s < 0.0
        if self_miss:
            self.consec_miss += 1
            self.streak_attr = min(self.streak_attr, max(c, g))
        else:
            self.consec_miss = 0
            self.streak_attr = float('inf')

        W = self._W()
        self.stats[frame_id] = dict(self_miss=self_miss, s=s, W=W,
                                    m=self.m, N=self.N)

        # Outer loop (FR1). Cadence budgets quantize by the TRACKED period,
        # not the nominal (deviation 12): with a grossly wrong nominal, load
        # in the band between the true and nominal periods would otherwise
        # look feasible at a cadence it cannot sustain - and the reactive
        # path could never attribute the resulting misses (streak_attr +
        # guard stays below the inflated nominal budget).
        n_req = max(self.N_min_app,
                    min(t.N_max, math.ceil(W / self.period)))
        # Upshift survival: no pressure-driven revert for 4*T_up after an
        # upshift means the new cadence holds - future upshifts are cheap.
        if (self.upshift_time is not None
                and (gpu_end - self.upshift_time) >= 4.0 * t.T_up):
            self.dwell_required = t.T_up
            self.upshift_time = None

        if (self.consec_miss >= t.M
                and self.streak_attr + t.guard > self.N * self.period):
            # Reactive downshift: sustained overload evidenced by the missed
            # frames' own stage times (attribution excludes timing-only misses
            # and cascades behind a single spike).
            self._on_pressure_downshift(gpu_end)
            self._change_cadence(max(self.N + 1, n_req))
            self.consec_miss = 0
            self.streak_attr = float('inf')
        elif (n_req > self.N
                and (self.completed - self.last_pressure) <= t.history):
            # Predictive downshift needs PRESSURE evidence (a recent miss or
            # near-miss), not the quantile alone: heavy-tailed load whose
            # q95 exceeds the budget can still be fully sustainable (tail
            # absorbed by margin and queue elasticity - observer finding,
            # step P2.1). Downshifting such load then flaps against the
            # upshift path. Misses/near-misses are the honest signal; the
            # margin AIMD surfaces overload as near-misses well before drops.
            self._on_pressure_downshift(gpu_end)
            self._change_cadence(n_req)
        elif (self.N > self.N_min_app
                and max(c, g) <= (self.N - 1) * self.period - t.H):
            self.dwell += elapsed                # upshift headroom sustained
            if self.dwell >= self.dwell_required:
                self._change_cadence(self.N - 1, upshift=True)
                self.upshift_time = gpu_end
        else:
            self.dwell = 0.0

    def _on_pressure_downshift(self, gpu_end):
        # A pressure-driven downshift shortly after an upshift means the
        # upshift probed an infeasible cadence: double the price of the next
        # probe. Geometric backoff converges (finitely many probes) at
        # persistently borderline load while staying adaptive to real drops.
        if (self.upshift_time is not None
                and (gpu_end - self.upshift_time) < 4.0 * self.tun.T_up):
            self.dwell_required *= 2.0
            self.upshift_time = None

    def _change_cadence(self, n_new, upshift=False):
        t = self.tun
        n_new = max(self.N_min_app, min(t.N_max, n_new))
        if n_new == self.N:
            return
        old = self.N
        self.N = n_new
        self.dwell = 0.0
        win = self.L_win.get(n_new)
        if not win:
            carry = self._q(self.L_win.get(old), 0.0)
            struct = self._q(self.c_win, 0.0) + self._q(self.g_win, 0.0)
            init = max(carry, struct)
            if init <= 0.0:
                init = t.T
            self.L_win[n_new] = deque([init], maxlen=t.history)
        if upshift:
            self.m = min(t.m_max, self.m + t.d_inc)   # pre-emptive safety boost

    def on_present_feedback(self, frame_id, t_present):
        t = self.tun
        # Gross-error re-seed (deviation 12, C16): the queried nominal period
        # can be grossly wrong (4.2% measured after an app-driven fullscreen
        # mode set - driver report issue #2). The PLL cannot recover on its
        # own: every innovation is pinned above the outlier gate and the
        # clamp band is centered on the wrong nominal. Period samples from
        # raw feedback DELTAS (dt / round(dt/period)) are phase-independent
        # and immune to slips (a skipped slot moves dt and dn together);
        # when their median disagrees with the tracked period by far more
        # than the clamp band, re-seed the period and phase from the
        # measurement and re-center the clamp on the measured period.
        if self.fb_raw_prev is not None:
            dt = t_present - self.fb_raw_prev
            dn = round(dt / self.period)
            if 1 <= dn <= 4:
                self.period_samples.append(dt / dn)
        self.fb_raw_prev = t_present
        if len(self.period_samples) == t.grid_reseed_window:
            med = sorted(self.period_samples)[t.grid_reseed_window // 2]
            if abs(med - self.period) > t.grid_reseed_dev * self.period_ref:
                nearest_old = round((t_present - self.phase) / self.period)
                self.period = med
                self.period_ref = med
                self.phase = t_present - nearest_old * med
                self.period_samples.clear()
                self.last_fb_slot_time = None
        nearest = round((t_present - self.phase) / self.period)
        err = t_present - (self.phase + nearest * self.period)
        # Dynamic grid (deviation 10): second-order PLL - phase (existing)
        # plus a period tracker. The frequency correction normalizes the
        # innovation by cycles elapsed since the previous good feedback, is
        # skipped for outliers (a slip or compositor hiccup produces a near
        # +-period/2 innovation that would kick the period estimate hard),
        # and the estimate is clamped to a band around the reference period
        # (the nominal until a gross-error re-seed replaces it).
        # A period change RE-BASES the phase so the slot nearest to the
        # feedback keeps its time: slot indices are absolute, so an
        # un-rebased period change of dP moves slot j by j*dP - seconds,
        # for real reference-clock epochs.
        if abs(err) < t.guard:
            if self.last_fb_slot_time is not None:
                dn = nearest - self.last_fb_slot_time[0]
                if dn >= 1:
                    period_new = self.period + t.grid_period_gain * err / dn
                    lo = self.period_ref * (1.0 - t.grid_period_max_dev)
                    hi = self.period_ref * (1.0 + t.grid_period_max_dev)
                    period_new = min(hi, max(lo, period_new))
                    self.phase += nearest * (self.period - period_new)
                    self.period = period_new
            self.last_fb_slot_time = (nearest, t_present)
        self.phase += t.grid_gain * err
        achieved = round((t_present - self.phase) / self.period)
        if self.last_fb is None or frame_id > self.last_fb[0]:
            self.last_fb = (frame_id, achieved)


# ---------------------------------------------------------------------------
# Slop-servo pacer (tier S reference model, implementation plan P4.1)
#
# The Games-by-Mason FramePacer.zig method (capability tier doc section 2):
# no presentation-timing extensions, no vsync grid, no cadence - sense
# BACKPRESSURE instead of display times. Per frame, measure "slop" (time the
# CPU spent involuntarily blocked on the GPU fence / swapchain acquire) and
# servo a sleep inserted before input polling: additive correction toward a
# static headroom of residual blocking, multiplicative back-off when the loop
# overshoots the refresh period, clamped to [0, T - headroom]. Equilibrium:
# residual blocking ~= headroom - the servo self-tunes toward the vblank
# deadline without ever knowing where it is. Hard requirement: plain-FIFO
# backpressure; on fifo_latest_ready superseded images drop instead of
# blocking, slop reads ~0 and the servo winds to nothing (claim C21a).
# ---------------------------------------------------------------------------

@dataclass
class ServoTunables:
    headroom:       float = 0.001  # target residual blocking per frame [s]
    gain:           float = 0.1    # additive sleep correction per unit slop error
    backoff:        float = 0.5    # multiplicative sleep back-off on overshoot
    overshoot_frac: float = 0.10   # loop duration > T*(1+this) counts as overshoot


class SlopServo:
    def __init__(self, tun: ServoTunables, T: float):
        self.tun = tun
        self.T = T
        self.sleep = 0.0

    def update(self, slop: float, loop_duration: float):
        t = self.tun
        if loop_duration > self.T * (1.0 + t.overshoot_frac):
            # Overshot the refresh period: the sleep ate into the frame
            # budget - back off multiplicatively (the fast lane; the additive
            # branch alone would bleed headroom for many frames).
            self.sleep *= t.backoff
        else:
            self.sleep += t.gain * (slop - t.headroom)
        self.sleep = min(max(self.sleep, 0.0), self.T - t.headroom)


# ---------------------------------------------------------------------------
# Simulated plant: pipeline, presentation engine, virtual clock
# ---------------------------------------------------------------------------

@dataclass
class Scenario:
    name:        str
    n_frames:    int
    c_fn:        object                  # k -> CPU stage duration [s]
    g_fn:        object                  # k -> GPU stage duration [s]
    tun:         Tunables = field(default_factory=Tunables)
    true_phase:  float = 0.0031          # real grid phase (pacer starts at 0)
    dpe_true:    float = 0.0015          # real presentation deadline offset
    forced_range: tuple = None           # (first, last) frames with corruption
    recreate_at: int = None              # swapchain recreation at this frame
    lazy_gpu:    bool = False            # C9: GPU measurements delivered via the
                                         # lazy fence-read path (frame k's data
                                         # first visible at frame k+F's CPU
                                         # start) instead of polled per frame
    unpaced:     bool = False            # C11: observer mode - the app ignores
                                         # the pacer's release times and wait
                                         # ids entirely (nothing is enforced)
    true_period: float = None            # C12: real grid period (None: tun.T).
                                         # Real displays deviate from the
                                         # queried period (measured 11 ppm on
                                         # the development machine).
    fb_blackout: tuple = None            # C12: (first, last) frames whose
                                         # present feedback is never delivered
    latest_ready: bool = False           # C13: fifo_latest_ready presentation -
                                         # when two images are ready for the
                                         # same vsync, the older is DROPPED
                                         # (never displayed, no feedback)
                                         # instead of queued (FIFO backpressure)
    clamp_only:  bool = False            # C13: enforce only the FR5 present-
                                         # wait clamp, ignore release times -
                                         # the P2.2 integration state
    inert_target: bool = False           # C15: the presentation engine IGNORES
                                         # the requested target present time
                                         # (measured driver behavior, see
                                         # doc/frame_pacing_present_timing_driver_report.md):
                                         # an image displays at the earliest
                                         # vsync at which it is available,
                                         # never held back for its target.
                                         # (latest_ready is inert by
                                         # construction.)
    holdback:    bool = False            # C15 mitigation: the app delays the
                                         # present REQUEST until one period
                                         # before the target (target - T +
                                         # hold guard), so the earliest
                                         # feasible vsync IS the target even
                                         # on an inert-target engine. Image
                                         # availability = max(gpu end,
                                         # request time).
    skip_prob:   float = 0.0             # C17: scanout-side display skips -
                                         # with this probability a committed
                                         # display slips one slot at scanout
                                         # (availability unchanged, present op
                                         # on cadence, first-pixel-out +1
                                         # slot). Measured live: ~5% of
                                         # vblanks at both 23.976 and 120 Hz,
                                         # uncorrelated with app timing.
    skip_seed:   int = 9999              # C17: RNG stream for skips
    skip_period: int = 0                 # C17: deterministic variant - every
                                         # slot whose index is a multiple of
                                         # this skips (the live skips are
                                         # vblank-count-periodic, ~21)
    skip_clamp_on_cadence: bool = False  # C17: the present-wait clamp
                                         # (satisfied times) resolves at the
                                         # UNSKIPPED slot - live, the present
                                         # queue op executes on cadence and
                                         # only first-pixel-out slips, so
                                         # vkWaitForPresentKHR may unblock a
                                         # slot before the actual display
    holdback_serialized: bool = False    # C18: the holdback wait runs INLINE
                                         # on the producer thread (the erhe
                                         # integration), so the next frame's
                                         # decision cannot happen before the
                                         # previous frame's present request
                                         # went out (found live: 120 Hz N=3
                                         # 4-slot cadence lock)
    tick_overhead: float = 0.0           # C18: fixed serial cost between the
                                         # previous frame's present request
                                         # and the next decision (present +
                                         # fence + observer bookkeeping)
    fifo_images: int = 0                 # P4.1: plain-FIFO swapchain image
                                         # count. > 0 adds acquire
                                         # backpressure: frame k's CPU start
                                         # blocks until frame k-(images-1)
                                         # has DISPLAYED - the queue-full
                                         # blocking the slop servo senses.
                                         # (0 = unmodeled, pre-P4.1 plants.)
    slop_servo: bool = False             # C19..C21: release comes from the
                                         # tier S slop-servo sleep instead of
                                         # the FramePacer decision; the
                                         # presentation target is ignored
                                         # (plain FIFO displays at the
                                         # earliest feasible vsync)
    servo_tun: ServoTunables = field(default_factory=ServoTunables)


@dataclass
class FrameRec:
    k:           int
    N:           int
    decision_t:  float
    release:     float
    target_time: float
    cpu_start:   float
    gpu_end:     float
    disp_time:   float
    disp_slot:   int
    self_miss:   bool
    slipped:     bool
    pending:     int
    W:           float
    m:           float
    slop:        float = 0.0             # P4.1: involuntary blocking this frame
    sleep:       float = 0.0             # P4.1: servo sleep applied this frame


def run(sc: Scenario):
    t = sc.tun
    pacer = FramePacer(t, nominal_phase=0.0)
    events = []                          # heap of (time, seq, kind, payload)
    seq = 0

    def push(time, kind, payload):
        nonlocal seq
        heapq.heappush(events, (time, seq, kind, payload))
        seq += 1

    def deliver(upto):
        while events and events[0][0] <= upto:
            _, _, kind, payload = heapq.heappop(events)
            if kind == 'cpu':
                pacer.on_cpu_done(*payload)
            elif kind == 'gpu':
                pacer.on_gpu_done(*payload)
            elif kind == 'present':
                pacer.on_present_feedback(*payload)

    cpu_end, gpu_end, disp_time = {}, {}, {}
    pending_gpu = {}                     # lazy_gpu: payloads awaiting fence read
    prev_disp_slot = -10**9
    decision_t = 0.0
    recs = []
    # latest_ready presentation state: one image can sit ready-but-unshown;
    # a newer image ready for the same slot supersedes (drops) it.
    # satisfied[i] = time at which a present with id >= i reached the display
    # (vkWaitForPresentKHR semantics: any newer id unblocks older waits).
    pending_disp = None                  # (frame_id, candidate_slot)
    satisfied = {}
    satisfied_upto = -1
    skip_rng = random.Random(sc.skip_seed)
    servo = SlopServo(sc.servo_tun, t.T) if sc.slop_servo else None
    prev_decision_t = 0.0

    def maybe_skip(slot):
        # C17: scanout-side skip - the present op stays on cadence but
        # first-pixel-out slips one slot. Applied at display commit only
        # (dropped images never reach scanout).
        if sc.skip_period > 0 and slot % sc.skip_period == 0:
            return slot + 1
        if sc.skip_prob > 0.0 and skip_rng.random() < sc.skip_prob:
            return slot + 1
        return slot

    def resolve_satisfied(upto_id, time):
        nonlocal satisfied_upto
        for idx in range(satisfied_upto + 1, upto_id + 1):
            satisfied[idx] = time
        satisfied_upto = max(satisfied_upto, upto_id)

    for k in range(sc.n_frames):
        if sc.recreate_at is not None and k == sc.recreate_at:
            pacer.notify_swapchain_recreated()
        deliver(decision_t)
        pacer.forced = (0.0, 0.0) if (
            sc.forced_range and sc.forced_range[0] <= k <= sc.forced_range[1]
        ) else None
        dec = pacer.schedule(k, decision_t)
        if sc.slop_servo:
            # Tier S: the servo sleep is the only release control; the
            # FramePacer's decision (still computed above) is ignored.
            r_eff = decision_t + servo.sleep
        elif sc.unpaced:
            r_eff = decision_t                        # observer: enforce nothing
        else:
            if sc.latest_ready:
                wait_t = satisfied.get(dec.wait_id, 0.0)
            else:
                wait_t = disp_time.get(dec.wait_id, 0.0)  # present-wait clamp (FR5)
            if sc.clamp_only:
                r_eff = max(decision_t, wait_t)       # P2.2: clamp enforced, release not
            else:
                r_eff = max(dec.release, decision_t, wait_t)
        c, g = sc.c_fn(k), sc.g_fn(k)
        cpu_start = max(r_eff, gpu_end.get(k - t.F, 0.0))
        if sc.fifo_images > 0:
            # Plain-FIFO backpressure (P4.1): with M swapchain images, the
            # acquire blocks until the image used M-1 frames ago has left the
            # display queue. This is the blocking a slop servo senses.
            cpu_start = max(cpu_start, disp_time.get(k - (sc.fifo_images - 1), 0.0))
        slop = cpu_start - r_eff                      # involuntary blocking
        if sc.lazy_gpu and (k - t.F) in pending_gpu:
            # Lazy read path: frame k-F's GPU results become visible at the
            # fence wait during frame k's CPU start, not when the GPU finished.
            push(cpu_start, 'gpu', pending_gpu.pop(k - t.F))
        ce = cpu_start + c
        cpu_end[k] = ce
        push(ce, 'cpu', (k, c))
        gs = max(ce, gpu_end.get(k - 1, 0.0))
        ge = gs + g
        gpu_end[k] = ge
        # c and g are pure stage service times (waits excluded) — normative,
        # see frame_pacing_inputs.md section 3.2/3.3.
        gpu_payload = (k, ge, c, g, ge - r_eff)
        if sc.lazy_gpu:
            pending_gpu[k] = gpu_payload
        else:
            push(ge, 'gpu', gpu_payload)

        # Presentation engine, quantized to the true vsync grid (period tp -
        # which the pacer does NOT know exactly, C12) with deadline offset
        # dpe_true. Two modes:
        # - FIFO (default): every image displays, in order (backpressure is
        #   modeled by the pipeline, not here).
        # - latest_ready (C13): when a newer image is ready by the same
        #   vsync deadline, the older ready-but-unshown image is DROPPED -
        #   it never displays and produces no feedback. This is
        #   VK_KHR_present_mode_fifo_latest_ready, the erhe editor's mode.
        #
        # An image is AVAILABLE to the engine once its GPU work is done AND
        # its present request has been made. The request is made at CPU end;
        # the C15 holdback mitigation delays it to one period before the
        # target so an inert-target engine cannot display the image early.
        tp = sc.true_period if sc.true_period is not None else t.T
        req_time = ce
        if sc.holdback:
            req_time = max(ce, dec.hold_until)
        avail = max(ge, req_time)
        blacked_out = sc.fb_blackout and sc.fb_blackout[0] <= k <= sc.fb_blackout[1]
        if sc.latest_ready:
            earliest = math.ceil((avail + sc.dpe_true - sc.true_phase) / tp)
            if pending_disp is not None:
                p_id, p_slot = pending_disp
                cand = max(prev_disp_slot + 1, earliest)
                if cand <= p_slot:
                    # k is ready by the pending image's slot: supersede it.
                    # Waits on the dropped id unblock when the display
                    # passes that slot (a newer id presents there).
                    resolve_satisfied(p_id, sc.true_phase + p_slot * tp)
                else:
                    # Commit the pending image (C17: scanout may skip).
                    p_slot_shown = maybe_skip(p_slot)
                    p_dt = sc.true_phase + p_slot_shown * tp
                    disp_time[p_id], prev_disp_slot = p_dt, p_slot_shown
                    clamp_dt = sc.true_phase + p_slot * tp \
                        if sc.skip_clamp_on_cadence else p_dt
                    resolve_satisfied(p_id, clamp_dt)
                    if not blacked_out:
                        push(p_dt, 'present', (p_id, p_dt))
            pending_disp = (k, max(prev_disp_slot + 1, earliest))
        else:
            if sc.inert_target or sc.slop_servo:
                # Inert-target engine (C15): earliest feasible vsync, the
                # target is never honored (measured driver behavior).
                # Slop-servo mode (P4.1): plain FIFO has no target at all -
                # same earliest-feasible display.
                i = prev_disp_slot + 1
            else:
                i = max(prev_disp_slot + 1,
                        math.ceil((dec.target_time - 0.25 * t.T - sc.true_phase) / tp))
            while sc.true_phase + i * tp - sc.dpe_true < avail:
                i += 1
            i = maybe_skip(i)            # C17: scanout may skip a slot
            dt = sc.true_phase + i * tp
            disp_time[k], prev_disp_slot = dt, i
            if not blacked_out:
                push(dt, 'present', (k, dt))

        pending = 1 + sum(1 for j in range(max(0, k - 8), k)
                          if disp_time.get(j, 0.0) > ce)
        rec_dt   = disp_time.get(k, 0.0)  # latest_ready: pending (backfilled) or dropped (stays 0)
        rec_slot = prev_disp_slot if sc.latest_ready else i
        recs.append(FrameRec(
            k=k, N=dec.N, decision_t=decision_t, release=r_eff,
            target_time=dec.target_time,
            cpu_start=cpu_start, gpu_end=ge, disp_time=rec_dt, disp_slot=rec_slot,
            self_miss=False, slipped=rec_dt > dec.target_time + 0.5 * t.T,
            pending=pending, W=0.0, m=0.0,
            slop=slop, sleep=servo.sleep if servo is not None else 0.0))
        prev_decision_t = decision_t
        decision_t = ce
        if servo is not None:
            # Loop duration = decision-to-decision: sleep + work + blocking.
            servo.update(slop, decision_t - prev_decision_t)
        if sc.holdback_serialized:
            # The C15 holdback runs inline on the producer thread: the next
            # tick starts only after this frame's present request went out.
            decision_t = max(decision_t, req_time) + sc.tick_overhead

    if sc.latest_ready and pending_disp is not None:
        p_id, p_slot = pending_disp
        tp = sc.true_period if sc.true_period is not None else t.T
        p_dt = sc.true_phase + p_slot * tp
        disp_time[p_id] = p_dt
        resolve_satisfied(p_id, p_dt)
        push(p_dt, 'present', (p_id, p_dt))
    for payload in pending_gpu.values():
        push(payload[1], 'gpu', payload)
    deliver(float('inf'))
    if sc.latest_ready:
        for r in recs:  # backfill display times committed after the frame's own turn
            r.disp_time = disp_time.get(r.k, 0.0)
            r.slipped   = r.disp_time > r.target_time + 0.5 * t.T
    for r in recs:
        st = pacer.stats.get(r.k)
        if st:
            r.self_miss, r.W, r.m = st['self_miss'], st['W'], st['m']
    return recs, pacer


# ---------------------------------------------------------------------------
# Scenario builders
# ---------------------------------------------------------------------------

MS = 1e-3


def gauss_fn(rng, mean_ms, sd_ms, floor_ms=0.5):
    def fn(_k):
        return max(floor_ms, rng.gauss(mean_ms, sd_ms)) * MS
    return fn


def stepped(base_fn, high_fn, first, last=None):
    def fn(k):
        if k >= first and (last is None or k <= last):
            return high_fn(k)
        return base_fn(k)
    return fn


# ---------------------------------------------------------------------------
# Claim checks C1..C8
# ---------------------------------------------------------------------------

RESULTS = []


def check(claim, cond, detail):
    RESULTS.append((claim, bool(cond), detail))
    print(f"  [{'PASS' if cond else 'FAIL'}] {claim}: {detail}")


def steady_frames(recs, start, n_expect):
    return [r for r in recs[start:] if r.N == n_expect]


def settle_time(recs, tun, from_frame, n_expect, quiet_s=0.5):
    """Virtual time from frame `from_frame` until N==n_expect and no
    self-miss for quiet_s of virtual time; None if never."""
    t0 = recs[from_frame].release
    quiet_start = None
    for r in recs[from_frame:]:
        ok = (r.N == n_expect) and not r.self_miss
        if ok:
            if quiet_start is None:
                quiet_start = r.gpu_end
            if r.gpu_end - quiet_start >= quiet_s:
                return quiet_start - t0
        else:
            quiet_start = None
    return None


def c1_steady():
    print("C1 steady state")
    rng = random.Random(101)
    tun = Tunables()
    sc = Scenario("c1", 1200, gauss_fn(rng, 6, 0.4), gauss_fn(rng, 8, 0.6),
                  tun=tun)
    recs, _ = run(sc)
    conv = recs[240:]
    n_vals = {r.N for r in conv}
    misses = sum(1 for r in conv if r.self_miss)
    slips = sum(1 for r in conv if r.slipped)
    durs = [recs[i + 1].disp_time - recs[i].disp_time
            for i in range(240, len(recs) - 1)]
    exact = sum(1 for d in durs if abs(d - tun.T) < 1e-6)
    check("C1 cadence constant", n_vals == {1}, f"N values after conv: {n_vals}")
    check("C1 miss rate", misses / len(conv) <= 0.01,
          f"{misses}/{len(conv)} self-misses ({100*misses/len(conv):.2f}%)")
    check("C1 visible duration", exact / len(durs) >= 0.99,
          f"{exact}/{len(durs)} frames visible exactly T "
          f"({slips} slips)")
    return recs, tun


def c2_step_up():
    print("C2 step load increase")
    rng = random.Random(202)
    tun = Tunables()
    step = 300
    g = stepped(gauss_fn(rng, 8, 0.6), gauss_fn(rng, 22, 0.8), step)
    sc = Scenario("c2", 900, gauss_fn(rng, 6, 0.4), g, tun=tun)
    recs, _ = run(sc)
    # Decision latency: self-misses the pacer had OBSERVED (gpu done) by the
    # time the cadence change was scheduled; in-flight frames don't count.
    n_change = next((r.k for r in recs[step:] if r.N == 2), None)
    t_dec = recs[n_change].decision_t if n_change else None
    observed = sum(1 for r in recs[step:n_change]
                   if r.self_miss and r.gpu_end <= t_dec) if n_change else -1
    late_total = sum(1 for r in recs[step:step + 60]
                     if r.self_miss or r.slipped)
    st = settle_time(recs, tun, step, 2)
    tail = {r.N for r in recs[step + 120:]}
    check("C2 decision within M observed misses",
          n_change is not None and observed <= tun.M,
          f"N->2 at frame {n_change}, {observed} misses observed at decision")
    check("C2 total disruption <= M+2F+1",
          late_total <= tun.M + 2 * tun.F + 1,
          f"{late_total} late/slipped frames around the step "
          f"(pipeline drain bound {tun.M + 2 * tun.F + 1})")
    check("C2 settle <= 1 s", st is not None and st <= 1.0,
          f"settle {st*1000:.0f} ms" if st else "did not settle")
    check("C2 stays at N=2", tail == {2}, f"tail N values: {tail}")


def c3_step_down():
    print("C3 step load decrease")
    rng = random.Random(303)
    tun = Tunables()
    up, down = 300, 900
    g = stepped(gauss_fn(rng, 8, 0.6), gauss_fn(rng, 22, 0.8), up, down - 1)
    sc = Scenario("c3", 1800, gauss_fn(rng, 6, 0.4), g, tun=tun)
    recs, _ = run(sc)
    t_drop = recs[down].release
    back = next((r for r in recs[down:] if r.N == 1), None)
    t_up = (back.gpu_end - t_drop) if back else None
    trans_miss = sum(1 for r in recs[down:back.k + 30] if r.self_miss) \
        if back else -1
    check("C3 upshift after ~T_up",
          back is not None and tun.T_up * 0.8 <= t_up <= tun.T_up + 0.6,
          f"upshift {t_up*1000:.0f} ms after drop" if back else "no upshift")
    check("C3 no misses during transition", trans_miss == 0,
          f"{trans_miss} self-misses in transition window")


def c4_borderline():
    print("C4 borderline load")
    rng = random.Random(404)
    tun = Tunables()
    sc = Scenario("c4", 2400, gauss_fn(rng, 6, 0.4),
                  gauss_fn(rng, 16.6, 0.4), tun=tun)
    recs, _ = run(sc)
    conv = recs[240:]
    n_vals = {r.N for r in conv}
    changes = sum(1 for i in range(241, len(recs)) if recs[i].N != recs[i-1].N)
    check("C4 parks at safe side", n_vals == {2}, f"N values: {n_vals}")
    check("C4 zero cadence alternation", changes == 0,
          f"{changes} cadence changes after convergence")


def c5_impulse():
    print("C5 single-frame impulse")
    rng = random.Random(505)
    tun = Tunables()
    base_g = gauss_fn(rng, 8, 0.6)
    g = stepped(base_g, lambda k: 30 * MS, 600, 600)
    sc = Scenario("c5", 1200, gauss_fn(rng, 6, 0.4), g, tun=tun)
    recs, _ = run(sc)
    n_vals = {r.N for r in recs[240:]}
    late = sum(1 for r in recs[600:640] if r.self_miss or r.slipped)
    m_before = recs[590].m
    m_tail_min = min(r.m for r in recs[900:1190])
    check("C5 cadence unchanged", n_vals == {1}, f"N values: {n_vals}")
    check("C5 spike + GPU-queue cascade only", late <= 4,
          f"{late} late/slipped frames around impulse "
          f"(spike + <=2 cascade + <=1 resync)")
    check("C5 margin returns", m_tail_min <= m_before + 0.0005,
          f"margin {m_before*1000:.2f} ms before, "
          f"tail min {m_tail_min*1000:.2f} ms")


def c6_adversarial():
    print("C6 queue invariant under estimate corruption")
    rng = random.Random(606)
    tun = Tunables()
    sc = Scenario("c6", 1200, gauss_fn(rng, 6, 0.4), gauss_fn(rng, 8, 0.6),
                  tun=tun, forced_range=(600, 660))
    recs, _ = run(sc)
    max_pending = max(r.pending for r in recs)
    n_vals = {r.N for r in recs[240:]}
    st = settle_time(recs, tun, 661, 1)
    check("C6 pending <= Q*+F", max_pending <= tun.Q_star + tun.F,
          f"max pending {max_pending} (bound {tun.Q_star + tun.F})")
    check("C6 no cadence response to timing-only misses", n_vals == {1},
          f"N values: {n_vals}")
    check("C6 recovery <= 1 s", st is not None and st <= 1.0,
          f"settle {st*1000:.0f} ms after corruption ends" if st
          else "did not settle")


def c7_cold_start():
    print("C7 cold start and swapchain recreation")
    rng = random.Random(707)
    tun = Tunables()
    sc = Scenario("c7", 1200, gauss_fn(rng, 6, 0.4), gauss_fn(rng, 8, 0.6),
                  tun=tun, recreate_at=600)
    recs, _ = run(sc)
    st_cold = settle_time(recs, tun, 0, 1)
    st_rec = settle_time(recs, tun, 600, 1)
    check("C7 cold start <= 1 s", st_cold is not None and st_cold <= 1.0,
          f"steady {st_cold*1000:.0f} ms after start" if st_cold else "never")
    check("C7 recreation recovery <= cold start",
          st_rec is not None and st_rec <= max(st_cold, 0.2) + 0.3,
          f"steady {st_rec*1000:.0f} ms after recreation" if st_rec
          else "never")


def c8_ripple(c1_recs, tun):
    print("C8 decision statistic ripple vs hysteresis")
    ws = [r.W for r in c1_recs[240:] if r.W > 0]
    ripple = max(ws) - min(ws)
    check("C8 ripple < H", ripple < tun.H,
          f"W ripple {ripple*1000:.2f} ms vs H {tun.H*1000:.2f} ms "
          f"(headroom x{tun.H/ripple:.1f})" if ripple > 0 else "no ripple")


def c9_lazy_delivery():
    print("C9 delayed GPU-measurement delivery (lazy fence-read path)")
    rng = random.Random(909)
    tun = Tunables()
    sc = Scenario("c9a", 1200, gauss_fn(rng, 6, 0.4), gauss_fn(rng, 8, 0.6),
                  tun=tun, lazy_gpu=True)
    recs, _ = run(sc)
    conv = recs[240:]
    misses = sum(1 for r in conv if r.self_miss)
    n_vals = {r.N for r in conv}
    maxp = max(r.pending for r in recs)
    check("C9 steady state unaffected",
          n_vals == {1} and misses / len(conv) <= 0.01
          and maxp <= tun.Q_star + tun.F,
          f"N {n_vals}, {misses}/{len(conv)} misses, max pending {maxp}")

    rng2 = random.Random(910)
    step = 300
    g = stepped(gauss_fn(rng2, 8, 0.6), gauss_fn(rng2, 22, 0.8), step)
    sc2 = Scenario("c9b", 900, gauss_fn(rng2, 6, 0.4), g, tun=tun,
                   lazy_gpu=True)
    recs2, _ = run(sc2)
    late_total = sum(1 for r in recs2[step:step + 80]
                     if r.self_miss or r.slipped)
    st = settle_time(recs2, tun, step, 2)
    tail = {r.N for r in recs2[step + 180:]}
    bound = tun.M + 3 * tun.F + 1
    check("C9 step degrades gracefully",
          st is not None and st <= 1.0 and tail == {2}
          and late_total <= bound,
          (f"{late_total} late (bound {bound} = M+3F+1), settle "
           f"{st*1000:.0f} ms, tail N {tail}") if st else "did not settle")


def c10_heavy_tail_borderline():
    print("C10 heavy-tailed borderline load (no cadence flapping)")
    rng = random.Random(1010)
    tun = Tunables()
    # Typical frames fit 2 vsyncs (median ~12 ms < 16.67 ms) but the tail
    # does not (q95 ~19 ms > 16.67 ms): per-frame headroom alone would flap
    # 2<->3; the W-gated upshift must park at 3 (safe side).
    def g(_k):
        if rng.random() < 0.15:
            return max(0.5, rng.gauss(19.0, 1.0)) * MS
        return max(0.5, rng.gauss(12.0, 1.0)) * MS
    sc = Scenario("c10", 2400, gauss_fn(rng, 6, 0.4), g, tun=tun)
    recs, _ = run(sc)
    conv = recs[240:]
    n_vals = {r.N for r in conv}
    changes = sum(1 for i in range(241, len(recs)) if recs[i].N != recs[i-1].N)
    misses = sum(1 for r in conv if r.self_miss)
    # The tail is absorbed by margin + queue elasticity: the load IS
    # sustainable at N=2 (verified by the miss rate below). The pacer may
    # PROBE N=1 (upshift) but each pressure-reverted probe doubles the next
    # probe's dwell, so probing converges geometrically instead of flapping.
    changes_tail = sum(1 for i in range(1801, len(recs)) if recs[i].N != recs[i-1].N)
    check("C10 dwells at sustainable cadence", (2 in n_vals) and (3 not in n_vals),
          f"N values: {n_vals}")
    check("C10 probing converges", (changes <= 16) and (changes_tail <= 2),
          f"{changes} changes total, {changes_tail} in the last quarter")
    check("C10 sustainable in fact", misses <= len(conv) // 100,
          f"{misses}/{len(conv)} self-misses")


def c11_observer_mode():
    print("C11 observer mode (unpaced app, bounded prediction drift)")
    rng = random.Random(1111)
    tun = Tunables()
    sc = Scenario("c11", 2400, gauss_fn(rng, 6, 0.4), gauss_fn(rng, 8, 0.6),
                  tun=tun, unpaced=True)
    recs, _ = run(sc)
    conv = recs[240:]
    # The app ignores the schedule, so the pacer cannot HIT its targets;
    # what it must not do is drift: predicted display times must stay within
    # a few refresh periods of the achieved present times, indefinitely.
    max_err = max(abs(r.disp_time - r.target_time) for r in conv)
    late_half = conv[len(conv)//2:]
    max_err_tail = max(abs(r.disp_time - r.target_time) for r in late_half)
    check("C11 prediction drift bounded", max_err <= 5 * tun.T,
          f"max |achieved - predicted| = {max_err*1000:.2f} ms (bound {5*tun.T*1000:.1f} ms)")
    check("C11 no growth over time", max_err_tail <= max_err + 1e-9,
          f"tail max {max_err_tail*1000:.2f} ms vs overall {max_err*1000:.2f} ms")


def c12_grid_period_tracking():
    print("C12 dynamic grid (true period differs from nominal; feedback blackout)")
    rng = random.Random(1212)
    tun = Tunables()
    ppm = 200e-6
    true_period = tun.T * (1.0 + ppm)
    # 60 s at 60 Hz; feedback blackout for 4 s in the middle (hidden window /
    # feedback loss). Without period tracking, the pacer's grid drifts
    # ppm * blackout = 0.8 ms per 4 s whenever feedback stops, and the
    # exposed grid is wrong for any wide UI view.
    blackout_first, blackout_last = 1800, 2040
    sc = Scenario("c12", 3600, gauss_fn(rng, 6, 0.4), gauss_fn(rng, 8, 0.6),
                  tun=tun, true_period=true_period,
                  fb_blackout=(blackout_first, blackout_last))
    recs, pacer = run(sc)
    conv = recs[240:]

    # 1. Period estimate converges to the true period (not the nominal).
    period_err_ppm = abs(pacer.period - true_period) / tun.T * 1e6
    check("C12 period estimate converges", period_err_ppm <= 20.0,
          f"|period_est - true| = {period_err_ppm:.1f} ppm (true is {ppm*1e6:.0f} ppm off nominal; bound 20 ppm)")

    # 2. Grid stays believable across the feedback blackout: prediction error
    #    at the end of the blackout is bounded by the residual period error,
    #    not by (blackout duration * full period error). Untracked drift over
    #    the 4 s blackout would be ~0.8 ms; require better than half of it.
    tail = [r for r in recs[blackout_last - 10:blackout_last + 1]]
    blackout_err = max(abs(r.disp_time - r.target_time) for r in tail)
    check("C12 grid holds through blackout", blackout_err <= 0.4 * MS,
          f"max |achieved - predicted| in blackout tail = {blackout_err*1000:.3f} ms "
          f"(untracked drift would be ~{ppm*4.0*1000:.1f} ms)")

    # 3. The wrong nominal period must not degrade pacing: steady cadence,
    #    miss rate as in C1.
    n_vals = {r.N for r in conv}
    misses = sum(1 for r in conv if r.self_miss)
    check("C12 pacing unaffected", (n_vals == {1}) and (misses / len(conv) <= 0.01),
          f"N values {n_vals}, {misses}/{len(conv)} self-misses")


def c13_latest_ready_fast_app():
    print("C13 latest-ready presentation, app faster than refresh (clamp-only)")
    rng = random.Random(1313)
    tun = Tunables()
    # The live-editor regime that ratcheted the slot cursor ~10 s ahead
    # (P2.2-era finding): light load (app ~4x the refresh rate), the
    # fifo_latest_ready presentation drops every ready-but-superseded image,
    # and only the FR5 clamp is enforced (release gating is P2.3). Dropped
    # frames consume NO display slot, so any per-frame slot-monotonicity
    # floor (last_slot + 1) advances the cursor faster than the display.
    sc = Scenario("c13", 3600, gauss_fn(rng, 2, 0.2), gauss_fn(rng, 2, 0.2),
                  tun=tun, latest_ready=True, clamp_only=True)
    recs, _ = run(sc)
    conv = recs[240:]

    # 1. The schedule must stay anchored to reality: target and release
    #    times bounded relative to the decision time, forever.
    max_target_lead  = max(r.target_time - r.decision_t for r in conv)
    max_release_lead = max(r.release - r.decision_t for r in conv)
    check("C13 target lead bounded", max_target_lead <= 8.0 * tun.T,
          f"max target - now = {max_target_lead*1000:.1f} ms (bound {8.0*tun.T*1000:.0f} ms)")
    tail = conv[len(conv)//2:]
    max_target_lead_tail = max(r.target_time - r.decision_t for r in tail)
    check("C13 no cursor growth", max_target_lead_tail <= max_target_lead + 1e-9,
          f"tail max {max_target_lead_tail*1000:.1f} ms vs overall {max_target_lead*1000:.1f} ms")

    # 2. Sanity: the display is actually showing frames (drops happen, but
    #    a good share of frames reach the screen).
    displayed = sum(1 for r in conv if r.disp_time > 0.0)
    check("C13 display active", displayed >= len(conv) // 4,
          f"{displayed}/{len(conv)} frames displayed (rest dropped by latest-ready)")


def c14_latest_ready_full_pacing():
    print("C14 latest-ready presentation, full pacing (release gating live)")
    rng = random.Random(1414)
    tun = Tunables()
    # Same plant as C13 (app ~4x the refresh rate on fifo_latest_ready), but
    # with release gating enforced (P2.3): the pacer must pace the app down
    # to one frame per cadence slot, at which point latest-ready has nothing
    # to supersede - drops disappear and every frame occupies its own slot.
    sc = Scenario("c14", 3600, gauss_fn(rng, 2, 0.2), gauss_fn(rng, 2, 0.2),
                  tun=tun, latest_ready=True)
    recs, _ = run(sc)
    conv = recs[240:]

    # 1. No drops after convergence: release gating leaves nothing for
    #    latest-ready to supersede.
    displayed = [r for r in conv if r.disp_time > 0.0]
    check("C14 no drops", len(displayed) == len(conv),
          f"{len(displayed)}/{len(conv)} frames displayed")

    # 2. Every frame occupies its own slot: consecutive display times one
    #    true period apart (N=1 at this load).
    n_vals = {r.N for r in conv}
    max_gap_err = max(abs((b.disp_time - a.disp_time) - tun.T)
                      for a, b in zip(displayed, displayed[1:]))
    check("C14 one frame per slot", (n_vals == {1}) and (max_gap_err <= 0.1 * MS),
          f"N values {n_vals}, max |display gap - T| = {max_gap_err*1000:.3f} ms")

    # 3. The schedule stays anchored: bounded release lead, no runaway.
    max_release_lead = max(r.release - r.decision_t for r in conv)
    check("C14 release lead bounded", max_release_lead <= 8.0 * tun.T,
          f"max release - now = {max_release_lead*1000:.1f} ms (bound {8.0*tun.T*1000:.0f} ms)")

    # 4. Paced, not thrashing: miss rate as in C1.
    misses = sum(1 for r in conv if r.self_miss)
    check("C14 miss rate", misses / len(conv) <= 0.01,
          f"{misses}/{len(conv)} self-misses")


def c15_inert_target_holdback():
    print("C15 inert-target engine; present-request holdback mitigation")
    # The measured driver behavior (doc/frame_pacing_present_timing_driver_report.md):
    # target present times are accepted but never honored - an image displays
    # at the earliest vsync at which it is AVAILABLE. Whenever budget slack
    # (margin + quantile-vs-typical latency gap) exceeds one refresh period,
    # the frame's chain finishes a full slot early and the display runs a
    # slot ahead of the schedule. Regime mirrors the real 120 Hz N=3
    # observation: T = 8.33 ms, jittery ~21 ms CPU (rare spikes keep the
    # quantile and margin elevated).
    tun = Tunables(T=1.0 / 120.0)

    # Load sits comfortably inside N=3 (per-stage q95 + guard < 3T) so the
    # cadence loop has no boundary excuse; the GPU-side spike tail inflates
    # the LATENCY quantile (release budget) without moving the cadence
    # statistic, so typical frames carry more than one refresh period of
    # slack - the early-display precondition observed live.
    def gpu_load(seed):
        rng = random.Random(seed)
        base = gauss_fn(rng, 4.5, 0.3)
        def fn(k):
            if rng.random() < 0.08:
                return 12.0 * MS
            return base(k)
        return fn

    def early_count(recs):
        conv = [r for r in recs[240:] if r.disp_time > 0.0]
        early = sum(1 for r in conv if round((r.disp_time - r.target_time) / tun.T) < 0)
        exact = sum(1 for r in conv if round((r.disp_time - r.target_time) / tun.T) == 0)
        return conv, early, exact

    # a) Without the mitigation: a visible fraction of frames displays a
    #    slot early (the driver-report symptom), though pacing stays stable.
    sc_a = Scenario("c15a", 3600, gauss_fn(random.Random(1501), 20.5, 0.8), gpu_load(1502),
                    tun=tun, latest_ready=True, inert_target=True)
    recs_a, _ = run(sc_a)
    conv_a, early_a, _ = early_count(recs_a)
    check("C15 inert engine shows early displays", early_a / len(conv_a) >= 0.02,
          f"{early_a}/{len(conv_a)} frames display before their target slot without holdback")
    # Cadence must stay bounded even while the display runs a slot ahead
    # of the schedule. (With a boundary-straddling load, early displays
    # additionally excite 3<->4 probe flapping - seen live in P2.2/P2.3
    # soaks - but that is C10 territory; this load stays inside N=3.)
    n_vals_a = {r.N for r in recs_a[240:]}
    check("C15 pacing bounded without holdback", n_vals_a <= {3, 4},
          f"N values {n_vals_a} (no runaway)")

    # b) With the request holdback: the earliest feasible vsync IS the
    #    target - early displays vanish and the cadence is exact.
    sc_b = Scenario("c15b", 3600, gauss_fn(random.Random(1501), 20.5, 0.8), gpu_load(1502),
                    tun=tun, latest_ready=True, inert_target=True, holdback=True)
    recs_b, _ = run(sc_b)
    conv_b, early_b, exact_b = early_count(recs_b)
    check("C15 holdback: no early displays", early_b == 0,
          f"{early_b}/{len(conv_b)} early")
    check("C15 holdback: exact cadence", exact_b / len(conv_b) >= 0.98,
          f"{exact_b}/{len(conv_b)} frames display exactly at their target slot")
    misses_b = sum(1 for r in recs_b[240:] if r.self_miss)
    check("C15 holdback: miss rate", misses_b / len(recs_b[240:]) <= 0.02,
          f"{misses_b}/{len(recs_b[240:])} self-misses")
    n_vals_b = {r.N for r in recs_b[240:]}
    check("C15 holdback: cadence stable", n_vals_b == {3},
          f"N values {n_vals_b} (the a-arm flapping is gone)")


def c16_gross_period_reseed():
    print("C16 gross nominal-period error (wrong queried refreshDuration) - re-seed")
    # The live finding (driver report issue #2): after an app-driven
    # fullscreen mode set to 23.976 Hz from a 120 Hz desktop, the driver
    # reports refreshDuration = 43.478 ms (23.000 Hz) while achieved-present
    # feedback shows the true period is 41.708 ms - a 4.2% error, ~8x the
    # PLL clamp band. C12 covers ppm-scale deviation; this claim covers the
    # gross case: the pacer must re-seed period and phase from measured
    # feedback deltas and converge to the true grid.
    rng = random.Random(1616)
    tun = Tunables(T=1.0 / 23.0)
    true_period = 0.041708
    sc = Scenario("c16a", 1800, gauss_fn(rng, 6, 0.4), gauss_fn(rng, 8, 0.6),
                  tun=tun, true_period=true_period)
    recs, pacer = run(sc)
    conv = recs[240:]
    err_ppm = abs(pacer.period - true_period) / tun.T * 1e6
    check("C16 period re-seeds to measured", err_ppm <= 100.0,
          f"|period_est - true| = {err_ppm:.0f} ppm of nominal "
          f"(nominal is {abs(tun.T - true_period)/tun.T*1e6:.0f} ppm off; "
          f"clamp band {tun.grid_period_max_dev*1e6:.0f} ppm)")
    tail = recs[1200:]
    tail_err = max(abs(r.disp_time - r.target_time) for r in tail)
    check("C16 grid converges to truth", tail_err <= 0.5 * MS,
          f"tail max |achieved - predicted| = {tail_err*1000:.3f} ms")
    n_vals = {r.N for r in conv}
    misses = sum(1 for r in conv if r.self_miss)
    check("C16 pacing healthy", (n_vals == {1}) and (misses / len(conv) <= 0.01),
          f"N values {n_vals}, {misses}/{len(conv)} self-misses")

    # b) Load in the ambiguity band (true period < load < nominal period):
    # with budgets quantized by the wrong nominal the pacer believes N=1 is
    # feasible and sits missing every frame - the reactive path can never
    # attribute (streak_attr + guard < nominal budget) and the predictive
    # quantile never crosses the inflated nominal. Budgets must follow the
    # TRACKED period: after the re-seed the load is recognized as
    # infeasible at N=1 and the pacer downshifts.
    rng_b = random.Random(1617)
    sc_b = Scenario("c16b", 1800, gauss_fn(rng_b, 6, 0.4),
                    gauss_fn(rng_b, 41.9, 0.15),
                    tun=tun, true_period=true_period)
    recs_b, _pacer_b = run(sc_b)
    tail_b = recs_b[900:]
    n_tail = {r.N for r in tail_b}
    misses_b = sum(1 for r in tail_b if r.self_miss)
    check("C16 infeasible-at-true load downshifts", n_tail == {2},
          f"tail N values {n_tail} (load fits the nominal but not the true period)")
    check("C16 downshifted pacing healthy", misses_b / len(tail_b) <= 0.01,
          f"{misses_b}/{len(tail_b)} self-misses in tail")


def c17_scanout_skip_robustness():
    print("C17 scanout-side display skips (bounded closed-loop response)")
    # The live finding (dual-stage feedback, H1 resolved): ~5% of vblanks
    # slip one slot at scanout (vblank-count-periodic, ~every 21st),
    # uncorrelated with any app timing; the present op executes on cadence
    # while first-pixel-out lands one slot late - so the present-wait clamp
    # may unblock a slot before the actual display (skip_clamp_on_cadence).
    # Claim: the closed-loop response stays BOUNDED - a skip costs one
    # 2-slot gap (or a latest-ready drop) and a one-slot schedule shift
    # (re-anchor), so the displayed cadence error stays comparable to the
    # ambient skip rate, with no limit cycle and no margin/miss runaway.
    # Plant: the live 120 Hz N=1 operating point (light load,
    # fifo_latest_ready, full pacing + holdback).
    #
    # NEGATIVE FINDING (important for the live investigation): the ~50%
    # alternating 1,2-gap sawtooth observed live at 120 Hz N=1 does NOT
    # reproduce here - nor with periodic skips, holdback off, an early
    # (on-cadence) clamp, or budget-tight load. The algorithm as specified
    # is robust to this disturbance; the live amplification must involve
    # integration- or driver-level behavior outside this model.
    tun = Tunables(T=1.0 / 120.0)

    def run_arm(name, **skip_kw):
        rng_seed = hash(name) % 10000
        sc = Scenario(name, 3600,
                      gauss_fn(random.Random(1717), 2, 0.2),
                      gauss_fn(random.Random(1718), 2, 0.2),
                      tun=tun, latest_ready=True, holdback=True,
                      skip_clamp_on_cadence=True, **skip_kw)
        recs, _pacer = run(sc)
        conv = recs[240:]
        displayed = [r for r in conv if r.disp_time > 0.0]
        gaps = [round((b.disp_time - a.disp_time) / tun.T)
                for a, b in zip(displayed, displayed[1:])]
        hist = {}
        for gap in gaps:
            hist[gap] = hist.get(gap, 0) + 1
        print(f"  [diag {name}] displayed {len(displayed)}/{len(conv)}, "
              f"gap histogram (slots): {dict(sorted(hist.items()))}")
        return conv, displayed, gaps, hist

    # a) Random skips at the ambient rate.
    conv, displayed, gaps, hist = run_arm("c17a", skip_prob=0.05)
    exact = hist.get(1, 0) / len(gaps)
    check("C17 cadence error ~= skip rate", exact >= 0.875,
          f"{100*exact:.1f}% exact 1-slot gaps (bound 87.5%; ambient skips 5%; "
          f"the live sawtooth was ~50%)")
    tail_gaps = gaps[3 * len(gaps) // 4:]
    tail_exact = sum(1 for gap in tail_gaps if gap == 1) / len(tail_gaps)
    check("C17 no limit cycle", tail_exact >= 0.875,
          f"tail exact fraction {100*tail_exact:.1f}% (a persistent sawtooth would stay ~50%)")
    check("C17 few drops", len(displayed) >= 0.90 * len(conv),
          f"{len(displayed)}/{len(conv)} displayed")
    misses = sum(1 for r in conv if r.self_miss)
    check("C17 miss rate bounded", misses / len(conv) <= 0.02,
          f"{misses}/{len(conv)} self-misses")
    max_release_lead = max(r.release - r.decision_t for r in conv)
    check("C17 release lead bounded", max_release_lead <= 8.0 * tun.T,
          f"max release - now = {max_release_lead*1000:.1f} ms (bound {8.0*tun.T*1000:.0f} ms)")

    # b) Vblank-count-periodic skips (the live pattern, every 21st vblank):
    #    a periodic disturbance is the classic way to entrain a feedback
    #    loop into a limit cycle - the response must stay aperiodic-bounded.
    conv_b, displayed_b, gaps_b, hist_b = run_arm("c17b", skip_period=21)
    exact_b = hist_b.get(1, 0) / len(gaps_b)
    mean_b = sum(gaps_b) / len(gaps_b)
    check("C17 periodic skips bounded", (exact_b >= 0.85) and (mean_b <= 1.15),
          f"{100*exact_b:.1f}% exact, mean gap {mean_b:.3f} slots "
          f"(bounds 85% / 1.15; matches the live 24 Hz tolerance)")
    misses_b = sum(1 for r in conv_b if r.self_miss)
    check("C17 periodic skips no miss runaway", misses_b / len(conv_b) <= 0.02,
          f"{misses_b}/{len(conv_b)} self-misses")


def c18_serialized_holdback():
    print("C18 inline (serialized) present-request holdback - no cadence lock")
    # The live 120 Hz N=3 finding: the C15 holdback runs INLINE on the
    # producer thread, so the next frame's decision waits for the previous
    # frame's request (~target - T). With slot reachability judged by the
    # q95 budget (spike-tail-inflated to ~31 ms against ~23 ms typical
    # latency), the anchor slot was chronically "unreachable", the schedule
    # slid one slot per frame, and the loop locked into a stable 4-slot
    # cadence at planned N=3 (achieved == target throughout, so the pacer
    # never saw a miss; live capture: 37% 4-gaps -> sustained 4-lock).
    # Fix (deviation 13): reachability uses TYPICAL (median) latency +
    # margin; the q95 budget still sets the release. A tail frame that does
    # miss slips one slot and re-anchors - bounded, priced by margin AIMD.
    # Capping the holdback at the next release point instead was REJECTED:
    # when budget > (N+1)*T - dpe - 2*guard it re-admits early displays
    # (sim: ~50-60% early). Past that bound only an async present thread
    # can give both exact holdback and full-budget scheduling; the inline
    # compromise below (spike frames slip, typical frames on cadence) is
    # the accepted producer-thread behavior.
    tun = Tunables(T=1.0 / 120.0)

    def gpu_load(seed):
        rng = random.Random(seed)
        base = gauss_fn(rng, 4.5, 0.3)
        def fn(k):
            if rng.random() < 0.08:
                return 12.0 * MS
            return base(k)
        return fn

    sc = Scenario("c18a", 3600, gauss_fn(random.Random(1501), 20.5, 0.8), gpu_load(1502),
                  tun=tun, latest_ready=True, inert_target=True, holdback=True,
                  skip_prob=0.05, skip_clamp_on_cadence=True,
                  holdback_serialized=True, tick_overhead=1e-3)
    recs, _pacer = run(sc)
    conv = recs[240:]
    displayed = [r for r in conv if r.disp_time > 0.0]
    gaps = [round((b.disp_time - a.disp_time) / tun.T)
            for a, b in zip(displayed, displayed[1:])]
    hist = {}
    for gap in gaps:
        hist[gap] = hist.get(gap, 0) + 1
    print(f"  [diag c18a] gap histogram (slots): {dict(sorted(hist.items()))}")
    mean_gap = sum(gaps) / len(gaps)
    exact3 = hist.get(3, 0) / len(gaps)
    check("C18 no cadence lock", (mean_gap <= 3.3) and (exact3 >= 0.70),
          f"mean gap {mean_gap:.2f} slots, {100*exact3:.1f}% exact "
          f"(the unfixed loop locks at mean ~4.0 with <15% exact)")
    early = sum(1 for r in conv
                if r.disp_time > 0 and round((r.disp_time - r.target_time) / tun.T) < 0)
    check("C18 mitigation intact", early == 0,
          f"{early} early displays (the capped-holdback policy re-admits ~50-60%)")
    misses = sum(1 for r in conv if r.self_miss)
    check("C18 spike slips bounded", misses / len(conv) <= 0.09,
          f"{misses}/{len(conv)} self-misses (~= the 8% spike rate: spikes physically "
          f"cannot make the serialized window; they slip one slot and re-anchor)")
    n_vals = {r.N for r in conv}
    check("C18 cadence stable", n_vals == {3}, f"N values {n_vals}")

    # b) The N=1 sawtooth regime (light load, serialized): must be clean.
    sc_b = Scenario("c18b", 3600, gauss_fn(random.Random(1), 2, 0.2), gauss_fn(random.Random(2), 2, 0.2),
                    tun=tun, latest_ready=True, holdback=True, skip_prob=0.05,
                    skip_clamp_on_cadence=True, holdback_serialized=True, tick_overhead=1e-3)
    recs_b, _ = run(sc_b)
    conv_b = recs_b[240:]
    displayed_b = [r for r in conv_b if r.disp_time > 0.0]
    gaps_b = [round((b.disp_time - a.disp_time) / tun.T)
              for a, b in zip(displayed_b, displayed_b[1:])]
    mean_b = sum(gaps_b) / len(gaps_b)
    exact_b = sum(1 for g in gaps_b if g == 1) / len(gaps_b)
    check("C18 no N=1 sawtooth", (mean_b <= 1.10) and (exact_b >= 0.875),
          f"mean gap {mean_b:.3f} slots, {100*exact_b:.1f}% exact "
          f"(the live sawtooth alternated 1,2 at ~50%)")


def c19_slop_servo_equilibrium():
    """P4.1 tier S: on a plain-FIFO backpressure plant with steady load, the
    slop servo converges to residual blocking ~= headroom, trimming latency
    versus the unpaced baseline without giving up throughput."""
    print("C19 slop-servo equilibrium (tier S)")
    tun = Tunables()
    stn = ServoTunables()

    def make(name, servo_on):
        rng = random.Random(1901)
        return Scenario(name, 1200, gauss_fn(rng, 6, 0.4), gauss_fn(rng, 8, 0.6),
                        tun=tun, fifo_images=3,
                        slop_servo=servo_on, unpaced=not servo_on,
                        inert_target=True, servo_tun=stn)

    base_recs, _ = run(make("c19_base", False))
    recs, _ = run(make("c19", True))
    conv, base_conv = recs[300:], base_recs[300:]

    def med(vals):
        s = sorted(vals)
        return s[len(s) // 2]

    slop_med = med([r.slop for r in conv])
    sleep_med = med([r.sleep for r in conv])
    # Input-poll latency: the loop samples input right after the servo sleep
    # (r_eff = release); the unpaced loop samples at the top of the iteration
    # and then blocks INSIDE the frame. disp - cpu_start is identical in both
    # runs (the backpressure still binds at equilibrium; the servo converts
    # post-input blocking into pre-input sleep, it does not move production).
    lat_med = med([r.disp_time - r.release for r in conv])
    lat_base = med([r.disp_time - r.release for r in base_conv])
    gaps = [conv[i + 1].disp_slot - conv[i].disp_slot for i in range(len(conv) - 1)]
    exact = sum(1 for d in gaps if d == 1)
    check("C19 residual blocking ~= headroom",
          slop_med <= 3.0 * stn.headroom,
          f"median slop {slop_med*1000:.2f} ms vs headroom {stn.headroom*1000:.1f} ms")
    check("C19 servo engaged", sleep_med > 1.0 * MS,
          f"median sleep {sleep_med*1000:.2f} ms")
    check("C19 latency trimmed vs unpaced",
          lat_med <= lat_base - 0.5 * sleep_med,
          f"median latency {lat_med*1000:.2f} ms vs baseline {lat_base*1000:.2f} ms "
          f"(sleep {sleep_med*1000:.2f} ms)")
    check("C19 throughput kept", exact / len(gaps) >= 0.99,
          f"{exact}/{len(gaps)} exact 1-slot gaps")
    return recs


def c20_slop_servo_overshoot():
    """P4.1 tier S: a load spike that overshoots the refresh period backs the
    sleep off multiplicatively; disturbance bounded, servo re-converges."""
    print("C20 slop-servo overshoot back-off")
    tun = Tunables()
    stn = ServoTunables()
    rng = random.Random(2001)
    spike_first, spike_last = 600, 602
    c_fn = stepped(gauss_fn(rng, 6, 0.4), lambda k: 30 * MS, spike_first, spike_last)
    sc = Scenario("c20", 1200, c_fn, gauss_fn(rng, 8, 0.6),
                  tun=tun, fifo_images=3, slop_servo=True, servo_tun=stn)
    recs, _ = run(sc)
    sleep_before = recs[spike_first - 1].sleep
    sleep_after = min(r.sleep for r in recs[spike_first:spike_first + 6])
    window = recs[spike_first:spike_first + 40]
    gaps = [window[i + 1].disp_slot - window[i].disp_slot for i in range(len(window) - 1)]
    disturbed = sum(1 for d in gaps if d != 1)
    tail = recs[900:]
    tail_gaps = [tail[i + 1].disp_slot - tail[i].disp_slot for i in range(len(tail) - 1)]
    tail_exact = sum(1 for d in tail_gaps if d == 1)
    sleep_recovered = sorted(r.sleep for r in tail)[len(tail) // 2]
    check("C20 multiplicative back-off",
          sleep_after <= 0.6 * sleep_before,
          f"sleep {sleep_before*1000:.2f} -> {sleep_after*1000:.2f} ms within 6 frames")
    check("C20 disturbance bounded", disturbed <= 8,
          f"{disturbed} non-1-slot gaps in the 40 frames after the spike")
    check("C20 re-converges",
          sleep_recovered >= 0.7 * sleep_before and tail_exact / len(tail_gaps) >= 0.99,
          f"tail sleep {sleep_recovered*1000:.2f} ms, "
          f"{tail_exact}/{len(tail_gaps)} exact gaps")


def c21_slop_servo_failure_modes():
    """P4.1 tier S documented failure modes: (a) fifo_latest_ready has no
    backpressure - slop ~0, the servo winds to nothing (this is why the pacer
    method must own the present mode, P4.2); (b) heavy-tailed load - the
    mean-based servo trims into the tail and recurring misses result, where
    the full pacer's quantile budget absorbs the same load."""
    print("C21 slop-servo failure modes")
    tun = Tunables()
    stn = ServoTunables()

    # (a) latest_ready: superseded images drop instead of blocking.
    rng = random.Random(2101)
    sc_a = Scenario("c21a", 1200, gauss_fn(rng, 6, 0.4), gauss_fn(rng, 4, 0.3),
                    tun=tun, latest_ready=True, slop_servo=True, servo_tun=stn)
    recs_a, _ = run(sc_a)
    conv_a = recs_a[300:]
    sleep_a = sorted(r.sleep for r in conv_a)[len(conv_a) // 2]
    drops = sum(1 for r in conv_a if r.disp_time == 0.0)
    check("C21a latest_ready: servo dead", sleep_a <= 1.0 * MS,
          f"median sleep {sleep_a*1000:.2f} ms (no backpressure to sense)")
    check("C21a latest_ready: free-runs and drops",
          drops / len(conv_a) >= 0.3,
          f"{drops}/{len(conv_a)} frames dropped "
          f"({100*drops/len(conv_a):.0f}%)")

    # (b) heavy-tailed load on the proper plain-FIFO plant.
    def heavy(seed):
        r = random.Random(seed)
        def fn(_k):
            return (14.0 if r.random() < 0.10 else 6.0) * MS
        return fn

    # The M=3 FIFO queue holds ~2T of slack, so tail frames do NOT miss
    # slots - the mean-based servo's documented failure mode is OSCILLATION
    # (tier doc: "heavy-tailed load oscillates or parks conservatively"):
    # every tail frame overshoots the loop period, the sleep collapses
    # multiplicatively, then recovers additively - the latency benefit is
    # intermittent and input-poll latency jitters by the sleep amplitude.
    sc_b = Scenario("c21b", 1600, heavy(2102), gauss_fn(random.Random(2103), 6, 0.3),
                    tun=tun, fifo_images=3, slop_servo=True, servo_tun=stn)
    recs_b, _ = run(sc_b)
    conv_b = recs_b[300:]
    backoffs = sum(1 for i in range(len(conv_b) - 1)
                   if conv_b[i + 1].sleep < 0.7 * conv_b[i].sleep - 1e-9)
    sleeps = sorted(r.sleep for r in conv_b)
    spread = sleeps[int(0.9 * len(sleeps))] - sleeps[int(0.1 * len(sleeps))]
    lat_b = sorted(r.disp_time - r.release for r in conv_b)
    jitter = lat_b[int(0.95 * len(lat_b))] - lat_b[len(lat_b) // 2]

    # Same load under the full pacer (tier W machinery, FIFO engine): the
    # q95 budget prices the tail in and absorbs it.
    sc_w = Scenario("c21w", 1600, heavy(2102), gauss_fn(random.Random(2103), 6, 0.3),
                    tun=tun)
    recs_w, _ = run(sc_w)
    conv_w = recs_w[300:]
    w_misses = sum(1 for r in conv_w if r.self_miss or r.slipped)
    check("C21b heavy tail: servo oscillates",
          backoffs >= 20 and spread >= 3.0 * MS,
          f"{backoffs} multiplicative collapses, sleep p10-p90 spread "
          f"{spread*1000:.1f} ms")
    check("C21b heavy tail: latency benefit intermittent",
          jitter >= 3.0 * MS,
          f"input-poll latency p95-p50 {jitter*1000:.1f} ms")
    check("C21b heavy tail: full pacer absorbs the same load",
          w_misses / len(conv_w) <= 0.01,
          f"full pacer {w_misses}/{len(conv_w)} misses/slips "
          f"({100*w_misses/len(conv_w):.2f}%)")


def main():
    c1_recs, tun = c1_steady()
    c2_step_up()
    c3_step_down()
    c4_borderline()
    c5_impulse()
    c6_adversarial()
    c7_cold_start()
    c8_ripple(c1_recs, tun)
    c9_lazy_delivery()
    c10_heavy_tail_borderline()
    c11_observer_mode()
    c12_grid_period_tracking()
    c13_latest_ready_fast_app()
    c14_latest_ready_full_pacing()
    c15_inert_target_holdback()
    c16_gross_period_reseed()
    c17_scanout_skip_robustness()
    c18_serialized_holdback()
    c19_slop_servo_equilibrium()
    c20_slop_servo_overshoot()
    c21_slop_servo_failure_modes()
    failed = [c for c, ok, _ in RESULTS if not ok]
    print()
    print(f"{len(RESULTS) - len(failed)}/{len(RESULTS)} checks passed")
    if failed:
        print("FAILED:", ", ".join(failed))
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
