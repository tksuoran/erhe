#!/usr/bin/env bash
#
# Quest logcat capture: streams `adb logcat` to a timestamped file under
# logs/ and tracks the streamer's PID in logs/.logcat.pid so the next
# invocation kills the previous capture before starting a new one. Run
# this before `adb shell am start ...` whenever you launch the editor on
# the headset, so the full startup trace is preserved past the in-memory
# logcat buffer's roll-over.
#
# Invariants the design holds to:
#   * At most one streamer process at a time (the prior PID is killed
#     before the new capture starts, with a process-name check so PID
#     recycling cannot kill an unrelated process).
#   * No orphaned processes across Claude Code sessions: the PID file
#     persists in the repo, so a fresh shell still finds and stops the
#     previous capture.
#   * No PID-recycle hazard: we verify the recorded PID actually points
#     at an `adb logcat` process before killing it.
#
# Usage:
#   scripts/quest_logcat.sh           # start a new capture (kills prior)
#   scripts/quest_logcat.sh stop      # stop the current capture, no new one
#   scripts/quest_logcat.sh filter <logfile>
#                                     # write logs/<name>_erhe.log containing
#                                     # only lines tagged 'erhe' (the editor's
#                                     # spdlog tag). Useful for cutting a
#                                     # ~30k-line capture down to a few
#                                     # thousand lines you can actually scan.
#
# The file path is printed to stdout so callers can echo it back to the
# user; on stop, the final file is named so the user can grep / open it.

set -u

# Resolve adb path -- mirrors install_android.bat's probe order so this
# script works without ANDROID_HOME exported.
if [ -z "${ANDROID_HOME:-}" ] && [ -d "${LOCALAPPDATA:-}/Android/Sdk" ]; then
    ANDROID_HOME="${LOCALAPPDATA}/Android/Sdk"
fi
if [ -z "${ANDROID_HOME:-}" ]; then
    echo "ERROR: ANDROID_HOME is not set and \$LOCALAPPDATA/Android/Sdk does not exist." >&2
    exit 1
fi
ADB="$ANDROID_HOME/platform-tools/adb.exe"
if [ ! -x "$ADB" ] && [ ! -f "$ADB" ]; then
    # POSIX adb on Linux/macOS hosts has no .exe suffix.
    ADB="$ANDROID_HOME/platform-tools/adb"
fi

# Repo root: the script lives in scripts/, so the parent dir is the root.
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
LOGDIR="$REPO_ROOT/logs"
PIDFILE="$LOGDIR/.logcat.pid"

mkdir -p "$LOGDIR"

stop_previous() {
    if [ ! -f "$PIDFILE" ]; then
        return 0
    fi
    local prev_pid
    prev_pid="$(cat "$PIDFILE" 2>/dev/null || true)"
    if [ -z "$prev_pid" ]; then
        rm -f "$PIDFILE"
        return 0
    fi
    if kill -0 "$prev_pid" 2>/dev/null; then
        # Defensive: only kill if the live process really is adb. PIDs are
        # recycled freely on Windows / Linux; without this guard we risk
        # killing an unrelated tool that inherited the recorded number.
        # Git Bash's `ps -p PID` shows the executable path but not the
        # argv, so we cannot grep for "logcat" itself -- "adb" is enough
        # because every other adb call in this workflow is short-lived
        # and would have exited before the PID file became stale.
        local ps_row
        ps_row="$(ps -p "$prev_pid" 2>/dev/null | tail -n +2 || true)"
        case "$ps_row" in
            *adb*) kill "$prev_pid" 2>/dev/null || true ;;
            "")    : ;;
            *)     echo "Stale PID $prev_pid does not look like adb; leaving it alone." >&2 ;;
        esac
    fi
    rm -f "$PIDFILE"
}

if [ "${1:-}" = "stop" ]; then
    stop_previous
    echo "Logcat capture stopped."
    exit 0
fi

if [ "${1:-}" = "filter" ]; then
    if [ -z "${2:-}" ]; then
        echo "Usage: scripts/quest_logcat.sh filter <logfile>" >&2
        exit 1
    fi
    src="$2"
    if [ ! -f "$src" ]; then
        echo "ERROR: $src not found" >&2
        exit 1
    fi
    # Build the output path next to the input file: strip a trailing
    # .log if present, append _erhe.log. Works for capture files under
    # logs/ and for any ad-hoc file the caller points us at.
    base="${src%.log}"
    out="${base}_erhe.log"
    # Threadtime tag column is space-padded; "erhe" appears between
    # spaces and a colon. Anchor on that to avoid matching the substring
    # "erhe" inside other tags or message bodies.
    grep -E " erhe +:" "$src" >"$out"
    line_count="$(wc -l <"$out")"
    echo "$out  ($(printf '%s' "$line_count" | tr -d ' ') lines)"
    exit 0
fi

stop_previous

# Clear the in-memory ring so the new file starts from this moment
# rather than replaying whatever the previous run left behind.
"$ADB" logcat -c

stamp="$(date +%Y%m%d_%H%M%S)"
logfile="$LOGDIR/quest_$stamp.log"

# -v threadtime: timestamp + tid, useful for multi-thread analysis.
# Redirect stderr into the same file so adb-side errors (e.g. device
# disconnect) survive past this shell exiting. The trailing & makes
# this the one extra process the design budgets for.
"$ADB" logcat -v threadtime >"$logfile" 2>&1 &
echo $! >"$PIDFILE"

echo "$logfile"
