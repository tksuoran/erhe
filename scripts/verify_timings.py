"""Where the time of an MCP-driven verify script goes (doc/testing.md "Verify scripts").

install(modules) wraps, for one run:
- every top-level function named check_* / run_* of the given script modules
  (wall time of each group the run calls, nested calls folded into their
  caller);
- McpClient.call (count and time per tool);
- the waits of prepared_asset_verify.Editor (startup, stop, wait_idle,
  wait_scene, close, load, save) as categories;
- time.sleep, charged to the innermost category it was called in (outside
  them, to the calling function).
report() prints the tables. The wrappers only measure; they call through
unchanged.
"""

import collections
import sys
import time

import erhe_mcp
import prepared_asset_verify as pav

_real_sleep = time.sleep
_category_stack = []
_group_depth = [0]
_groups = []  # (name, seconds) of the top-level groups in call order
_tool_count = collections.Counter()
_tool_time = collections.Counter()
_category_time = collections.Counter()   # inclusive time per Editor wait category
_category_count = collections.Counter()
_sleep_time = collections.Counter()      # sleep per innermost category ("script" outside any)
_sleep_count = collections.Counter()
_mcp_time_by_category = collections.Counter()
_start = [None]


def _category():
    return _category_stack[-1] if _category_stack else "script"


def _sleep(seconds):
    category = _category()
    if category == "script":
        category = f"script: {sys._getframe(1).f_code.co_name}"
    _sleep_time[category] += seconds
    _sleep_count[category] += 1
    _real_sleep(seconds)


def _wrap_category(owner, name, category):
    original = getattr(owner, name)

    def wrapper(*args, **kwargs):
        _category_stack.append(category)
        begin = time.monotonic()
        try:
            return original(*args, **kwargs)
        finally:
            _category_time[category] += time.monotonic() - begin
            _category_count[category] += 1
            _category_stack.pop()
    wrapper.__name__ = original.__name__
    setattr(owner, name, wrapper)


def _wrap_call():
    original = erhe_mcp.McpClient.call

    def call(self, tool, arguments=None):
        begin = time.monotonic()
        try:
            return original(self, tool, arguments)
        finally:
            elapsed = time.monotonic() - begin
            _tool_count[tool] += 1
            _tool_time[tool] += elapsed
            _mcp_time_by_category[_category()] += elapsed
    erhe_mcp.McpClient.call = call


def _wrap_group(module, name):
    original = getattr(module, name)

    def group(*args, **kwargs):
        _group_depth[0] += 1
        begin = time.monotonic()
        try:
            return original(*args, **kwargs)
        finally:
            _group_depth[0] -= 1
            if _group_depth[0] == 0:
                _groups.append((name, time.monotonic() - begin))
    group.__name__ = original.__name__
    setattr(module, name, group)


def install(modules):
    _start[0] = time.monotonic()
    time.sleep = _sleep
    _wrap_call()
    for name, category in (("__init__", "startup"), ("stop", "stop"), ("wait_idle", "wait_idle"), ("wait_scene", "wait_scene"),
                           ("close", "close"), ("load", "load"), ("save", "save")):
        _wrap_category(pav.Editor, name, category)
    for module in modules:
        for name in list(vars(module)):
            if (name.startswith("check_") or name.startswith("run_")) and callable(getattr(module, name)) and (name != "check"):
                _wrap_group(module, name)


def report(top=25):
    total = time.monotonic() - _start[0]
    print(f"\n=== timings: {total:.1f} s total ===")
    print("groups (top-level, in run order):")
    for name, seconds in _groups:
        print(f"  {seconds:8.1f} s  {name}")
    print(f"  {sum(s for _, s in _groups):8.1f} s  sum")
    print("Editor waits (inclusive; nested waits count in both):")
    for category, seconds in _category_time.most_common():
        print(f"  {seconds:8.1f} s  {_category_count[category]:6d} x  {category}")
    print("time.sleep by innermost category:")
    for category, seconds in _sleep_time.most_common():
        print(f"  {seconds:8.1f} s  {_sleep_count[category]:6d} x  {category}")
    print("MCP round trips by innermost category:")
    for category, seconds in _mcp_time_by_category.most_common():
        print(f"  {seconds:8.1f} s  {category}")
    calls = sum(_tool_count.values())
    print(f"MCP tools by time (top {top} of {len(_tool_count)}; {calls} calls, {sum(_tool_time.values()):.1f} s):")
    for tool, seconds in _tool_time.most_common(top):
        count = _tool_count[tool]
        print(f"  {seconds:8.1f} s  {count:6d} x  {1000.0 * seconds / count:8.1f} ms  {tool}")
