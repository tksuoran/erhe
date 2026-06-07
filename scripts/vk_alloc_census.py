#!/usr/bin/env python3
"""GPU/host allocation census for the erhe editor (zero-install, gdb-based).

Launches the editor under gdb in an isolated run dir (same mechanism as
idle_memory_check.py), waits for the editor to reach its main loop, then
enables counting breakpoints for a fixed window:

  - DRM_IOCTL_AMDGPU_GEM_CREATE ioctls: every GPU buffer-object allocation,
    including allocations made internally by the driver (RADV command
    buffers, descriptor pools, ...) that bypass vkAllocateMemory. Counts,
    total requested bytes, and the first N backtraces.
  - Vulkan loader entry points: vkCreate*/vkAllocate*/vkDestroy*/vkFree*
    pairs, so net object growth per frame is visible at API level.
  - glibc brk/sbrk and anonymous mmap: who grows the host heap.
  - vkQueuePresentKHR as the frame marker; all counts are reported
    per-frame as well as per-second.

The editor runs much slower under the census (every counted call is a
ptrace stop); rates are still exact per frame.

Usage:
  python3 scripts/vk_alloc_census.py [--duration 20] [--settle 8] [--build-dir ...]
  python3 scripts/vk_alloc_census.py --debuginfod   # resolve driver-internal frames (downloads symbols)

Output: census table + backtraces in <run_dir>/census_gdb_output.log,
parsed summary on stdout.
"""

import argparse
import os
import re
import signal
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import idle_memory_check as imc  # reuse run-dir isolation + sampling helpers

GDB_SCRIPT = r'''
import gdb

COUNTS = {}
ALL_BPS = []

class CensusBP(gdb.Breakpoint):
    def __init__(self, location, bt_budget=0):
        super().__init__(location)
        self.census_name = location
        self.bt_budget = bt_budget
        self.enabled = False
        COUNTS[self.census_name] = 0
        ALL_BPS.append(self)

    def stop(self):
        COUNTS[self.census_name] += 1
        if self.bt_budget > 0:
            self.bt_budget -= 1
            print("\n--- backtrace [%s] hit %d ---" % (self.census_name, COUNTS[self.census_name]))
            try:
                gdb.execute("bt 28")
            except gdb.error as error:
                print("bt failed: %s" % error)
        return False

class IoctlBP(gdb.Breakpoint):
    GEM_CREATE = 0xC0206440  # DRM_IOCTL_AMDGPU_GEM_CREATE
    GEM_CLOSE  = 0x40086409  # DRM_IOCTL_GEM_CLOSE

    def __init__(self, bt_budget):
        super().__init__("ioctl")
        self.bt_budget = bt_budget
        self.enabled = False
        COUNTS["amdgpu_gem_create"] = 0
        COUNTS["amdgpu_gem_create_bytes"] = 0
        COUNTS["gem_close"] = 0
        ALL_BPS.append(self)

    def stop(self):
        req = int(gdb.parse_and_eval("(unsigned int)$rsi"))
        if req == self.GEM_CREATE:
            COUNTS["amdgpu_gem_create"] += 1
            size = -1
            try:
                size = int(gdb.parse_and_eval("*(unsigned long long*)$rdx"))
                COUNTS["amdgpu_gem_create_bytes"] += size
            except gdb.error:
                pass
            if self.bt_budget > 0:
                self.bt_budget -= 1
                print("\n--- backtrace [amdgpu_gem_create size=%d] hit %d ---" % (size, COUNTS["amdgpu_gem_create"]))
                try:
                    gdb.execute("bt 28")
                except gdb.error as error:
                    print("bt failed: %s" % error)
        elif req == self.GEM_CLOSE:
            COUNTS["gem_close"] += 1
        return False

class BrkBP(gdb.Breakpoint):
    def __init__(self, location, bt_budget):
        super().__init__(location)
        self.census_name = "heap_" + location
        self.bt_budget = bt_budget
        self.enabled = False
        COUNTS[self.census_name] = 0
        ALL_BPS.append(self)

    def stop(self):
        COUNTS[self.census_name] += 1
        if self.bt_budget > 0:
            self.bt_budget -= 1
            print("\n--- backtrace [%s] hit %d ---" % (self.census_name, COUNTS[self.census_name]))
            try:
                gdb.execute("bt 28")
            except gdb.error as error:
                print("bt failed: %s" % error)
        return False

class MmapBP(gdb.Breakpoint):
    MAP_ANONYMOUS = 0x20

    def __init__(self, bt_budget):
        super().__init__("mmap")
        self.bt_budget = bt_budget
        self.enabled = False
        COUNTS["mmap_anon"] = 0
        COUNTS["mmap_anon_bytes"] = 0
        ALL_BPS.append(self)

    def stop(self):
        flags = int(gdb.parse_and_eval("(int)$rcx"))
        if (flags & self.MAP_ANONYMOUS) != 0:
            COUNTS["mmap_anon"] += 1
            length = int(gdb.parse_and_eval("(unsigned long)$rsi"))
            COUNTS["mmap_anon_bytes"] += length
            if self.bt_budget > 0:
                self.bt_budget -= 1
                print("\n--- backtrace [mmap_anon size=%d] hit %d ---" % (length, COUNTS["mmap_anon"]))
                try:
                    gdb.execute("bt 28")
                except gdb.error as error:
                    print("bt failed: %s" % error)
        return False

VK_COUNT_ONLY = [
    "vkQueuePresentKHR", "vkQueueSubmit2", "vkAcquireNextImageKHR",
    "vkAllocateCommandBuffers", "vkBeginCommandBuffer",
    "vkDestroySemaphore", "vkDestroyFence", "vkResetFences",
    "vkDestroyDescriptorPool", "vkResetDescriptorPool", "vkFreeDescriptorSets", "vkUpdateDescriptorSets",
    "vkDestroyQueryPool", "vkResetQueryPool",
    "vkDestroyImage", "vkDestroyImageView", "vkDestroyBuffer", "vkFreeMemory",
    "vkDestroyFramebuffer", "vkDestroyRenderPass", "vkDestroyPipeline", "vkDestroySampler",
    "vkCreateShaderModule", "vkCreateSwapchainKHR", "vkMapMemory",
]
VK_WITH_BT = [
    ("vkCreateSemaphore", 3), ("vkCreateFence", 3), ("vkCreateEvent", 3),
    ("vkCreateDescriptorPool", 3), ("vkAllocateDescriptorSets", 3),
    ("vkCreateQueryPool", 3),
    ("vkCreateImage", 3), ("vkCreateImageView", 3),
    ("vkCreateBuffer", 3), ("vkAllocateMemory", 3),
    ("vkCreateFramebuffer", 3), ("vkCreateRenderPass", 3), ("vkCreateRenderPass2", 3),
    ("vkCreateGraphicsPipelines", 3), ("vkCreateComputePipelines", 3),
    ("vkCreateSampler", 3), ("vkCreatePipelineLayout", 3), ("vkCreateDescriptorSetLayout", 3),
]

def census_make(gem_bt, brk_bt, mmap_bt):
    for name in VK_COUNT_ONLY:
        CensusBP(name, 0)
    for name, budget in VK_WITH_BT:
        CensusBP(name, budget)
    IoctlBP(gem_bt)
    BrkBP("brk", brk_bt)
    BrkBP("sbrk", brk_bt)
    MmapBP(mmap_bt)

def census_enable():
    for bp in ALL_BPS:
        bp.enabled = True
    print("CENSUS_ENABLED")

def census_report():
    print("=== CENSUS COUNTS BEGIN ===")
    for key in sorted(COUNTS):
        print("COUNT %s %d" % (key, COUNTS[key]))
    print("=== CENSUS COUNTS END ===")

census_make(%GEM_BT%, %BRK_BT%, %MMAP_BT%)
print("CENSUS_READY")
'''


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build-dir", default="build_ninja_linux_vulkan")
    parser.add_argument("--duration", type=float, default=20.0, help="census window seconds")
    parser.add_argument("--settle", type=float, default=8.0, help="seconds after readiness before census starts")
    parser.add_argument("--readiness-timeout", type=float, default=120.0)
    parser.add_argument("--label", default="census")
    parser.add_argument("--set", dest="overrides", action="append", default=[], metavar="FILE.json:dotted.path=VALUE")
    parser.add_argument("--quit-after-frames", type=int, default=0)
    parser.add_argument("--runs-root", default="")
    parser.add_argument("--gem-bt", type=int, default=14, help="backtraces to capture for amdgpu GEM_CREATE")
    parser.add_argument("--brk-bt", type=int, default=10, help="backtraces for brk/sbrk heap growth")
    parser.add_argument("--mmap-bt", type=int, default=6, help="backtraces for anonymous mmap")
    parser.add_argument("--debuginfod", action="store_true", help="enable debuginfod symbol download (slow first run)")
    args = parser.parse_args()

    editor = imc.REPO_ROOT / args.build_dir / "src" / "editor" / "editor"
    if not editor.is_file():
        print(f"error: editor binary not found: {editor}", file=sys.stderr)
        return 2

    run_dir = imc.make_run_dir(args)
    print(f"run dir: {run_dir}")

    gdb_script = GDB_SCRIPT.replace("%GEM_BT%", str(args.gem_bt)).replace("%BRK_BT%", str(args.brk_bt)).replace("%MMAP_BT%", str(args.mmap_bt))
    script_path = run_dir / "census_defs.py"
    script_path.write_text(gdb_script)

    out_path = run_dir / "census_gdb_output.log"
    out_file = out_path.open("wb")
    env = dict(os.environ)
    if args.debuginfod:
        env.setdefault("DEBUGINFOD_URLS", "https://debuginfod.ubuntu.com")

    gdb = subprocess.Popen(
        ["gdb", "-q", "--nx", str(editor)],
        cwd=run_dir, env=env,
        stdin=subprocess.PIPE, stdout=out_file, stderr=subprocess.STDOUT,
    )

    def send(*commands: str) -> None:
        for command in commands:
            gdb.stdin.write((command + "\n").encode())
        gdb.stdin.flush()

    send(
        "set pagination off",
        "set confirm off",
        "set width 0",
        "set breakpoint pending on",
        "set debuginfod enabled " + ("on" if args.debuginfod else "off"),
        f"source {script_path}",
        "run",
    )

    # Find the editor pid (child of gdb) once it spawns.
    def find_inferior_pid() -> int:
        for _ in range(200):
            result = subprocess.run(["pgrep", "-P", str(gdb.pid)], capture_output=True, text=True)
            pids = [int(p) for p in result.stdout.split()]
            if pids:
                return pids[0]
            time.sleep(0.1)
        return 0

    inferior_pid = find_inferior_pid()
    if inferior_pid == 0:
        print("error: editor did not spawn under gdb", file=sys.stderr)
        gdb.kill()
        return 2
    print(f"gdb pid {gdb.pid}, editor pid {inferior_pid}")

    # Wait for readiness marker in the run log.
    log_file = run_dir / "logs" / "log.txt"
    t0 = time.monotonic()
    while time.monotonic() - t0 < args.readiness_timeout:
        if gdb.poll() is not None:
            print(f"error: gdb exited during startup; see {out_path}", file=sys.stderr)
            return 2
        if log_file.is_file() and imc.READINESS_MARKER in log_file.read_text(errors="replace"):
            break
        time.sleep(0.5)
    else:
        print("error: readiness marker not seen; killing", file=sys.stderr)
        gdb.kill()
        return 2
    print(f"editor ready after {time.monotonic() - t0:.1f}s; settling {args.settle:.0f}s")
    time.sleep(args.settle)

    sample_begin = imc.take_sample(inferior_pid, 0.0)
    os.kill(inferior_pid, signal.SIGINT)  # gdb intercepts: editor never sees it
    send("python census_enable()", "continue")
    census_t0 = time.monotonic()
    print(f"census running for {args.duration:.0f}s ...")
    time.sleep(args.duration)
    census_seconds = time.monotonic() - census_t0
    sample_end = imc.take_sample(inferior_pid, census_seconds)

    os.kill(inferior_pid, signal.SIGINT)
    send("python census_report()", "kill", "quit")
    try:
        gdb.wait(timeout=60)
    except subprocess.TimeoutExpired:
        gdb.kill()
    out_file.close()

    # Parse counts.
    text = out_path.read_text(errors="replace")
    counts = {}
    for match in re.finditer(r"^COUNT (\S+) (\d+)$", text, re.M):
        counts[match.group(1)] = int(match.group(2))
    if not counts:
        print(f"error: no census counts found; see {out_path}", file=sys.stderr)
        return 2

    frames = max(1, counts.get("vkQueuePresentKHR", 0))
    print()
    print(f"=== census: {frames} frames in {census_seconds:.1f}s ({frames / census_seconds:.1f} fps under census overhead) ===")
    print(f"{'call':36s} {'count':>10s} {'per frame':>10s} {'per sec':>10s}")
    for key in sorted(counts, key=lambda k: -counts[k]):
        if counts[key] == 0:
            continue
        print(f"{key:36s} {counts[key]:10d} {counts[key] / frames:10.2f} {counts[key] / census_seconds:10.1f}")

    gem_bytes = counts.get("amdgpu_gem_create_bytes", 0)
    if gem_bytes:
        print(f"\nGEM_CREATE requested bytes: {gem_bytes / (1024 * 1024):.1f} MiB total, "
              f"{gem_bytes / frames / 1024.0:.1f} KiB/frame, {gem_bytes / census_seconds / (1024 * 1024):.1f} MiB/s")
    mmap_bytes = counts.get("mmap_anon_bytes", 0)
    if mmap_bytes:
        print(f"mmap_anon requested bytes:  {mmap_bytes / (1024 * 1024):.1f} MiB total, "
              f"{mmap_bytes / frames / 1024.0:.1f} KiB/frame")

    if sample_begin and sample_end:
        print("\nmemory during census window:")
        for metric in ("rss_kib", "anonymous_kib", "drm_gtt_kib", "drm_vram_kib"):
            if metric in sample_begin and metric in sample_end:
                delta = sample_end[metric] - sample_begin[metric]
                print(f"  {metric:16s} {sample_begin[metric] / 1024.0:9.1f} -> {sample_end[metric] / 1024.0:9.1f} MiB "
                      f"({delta / 1024.0:+9.1f} MiB, {delta / frames:+9.1f} KiB/frame)")

    print(f"\nbacktraces + full table: {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
