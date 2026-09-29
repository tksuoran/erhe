#!/usr/bin/env python3
"""Render the results.json of an erhe_graphics_gpu_tests run into report.html.

Usage:
    py -3 scripts/gpu_test_report.py <results dir> [--output <path>]

<results dir> is the directory the test binary wrote (by default
gpu_test_results/ under the working directory of the run, or
ERHE_GPU_TEST_RESULTS_DIR). The report is one self-contained HTML file:
every image is embedded as a data URI, so it opens straight from disk with no
HTTP server. It shows the device, backend and counts, a status filter, and
for every golden assertion either an output / golden / FLIP error map
triptych with the FLIP numbers (image goldens) or a hex view of the differing
bytes (buffer goldens). See doc/erhe/graphics_test_coverage.md
"Golden assertions".
"""

from __future__ import annotations

import argparse
import base64
import html
import json
import struct
import sys
import zlib
from pathlib import Path

HEX_BYTES_PER_ROW = 16
HEX_MAX_DIFF_ROWS = 64
HEX_PREVIEW_ROWS = 8


def png_rgba8(width: int, height: int, rgba: bytes) -> bytes:
    """Encodes tightly packed RGBA8 rows (top-down) as a PNG."""

    def chunk(kind: bytes, data: bytes) -> bytes:
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    stride = width * 4
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        raw.extend(rgba[y * stride:(y + 1) * stride])
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", header)
        + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + chunk(b"IEND", b"")
    )


def read_pfm(path: Path) -> tuple[int, int, list[float]] | None:
    """Reads a three-channel PFM; returns (width, height, rgb) with rows top-down."""
    data = path.read_bytes()
    tokens: list[bytes] = []
    position = 0
    while len(tokens) < 4 and position < len(data):
        while position < len(data) and data[position:position + 1].isspace():
            position += 1
        start = position
        while position < len(data) and not data[position:position + 1].isspace():
            position += 1
        tokens.append(data[start:position])
    position += 1  # the single whitespace byte that ends the header
    if len(tokens) < 4 or tokens[0] != b"PF":
        return None
    width = int(tokens[1])
    height = int(tokens[2])
    scale = float(tokens[3])
    endian = "<" if scale < 0.0 else ">"
    count = width * height * 3
    values = struct.unpack(f"{endian}{count}f", data[position:position + (count * 4)])
    row_floats = width * 3
    rgb: list[float] = []
    for image_row in range(height):
        file_row = height - 1 - image_row
        rgb.extend(values[file_row * row_floats:(file_row + 1) * row_floats])
    return width, height, rgb


def pfm_preview_png(path: Path) -> bytes | None:
    """Converts a PFM into a PNG preview: values clamped to [0, 1], then
    sRGB-encoded for display."""
    image = read_pfm(path)
    if image is None:
        return None
    width, height, rgb = image

    def encode(value: float) -> int:
        value = min(max(value, 0.0), 1.0)
        srgb = (value * 12.92) if value <= 0.0031308 else ((1.055 * (value ** (1.0 / 2.4))) - 0.055)
        return int(round(srgb * 255.0))

    rgba = bytearray()
    for i in range(width * height):
        rgba.extend((encode(rgb[i * 3]), encode(rgb[(i * 3) + 1]), encode(rgb[(i * 3) + 2]), 255))
    return png_rgba8(width, height, bytes(rgba))


def image_data_uri(results_dir: Path, relative: str) -> str | None:
    if not relative:
        return None
    path = results_dir / relative
    if not path.is_file():
        return None
    if path.suffix.lower() == ".pfm":
        png = pfm_preview_png(path)
    else:
        png = path.read_bytes()
    if png is None:
        return None
    return "data:image/png;base64," + base64.b64encode(png).decode("ascii")


def esc(value: object) -> str:
    return html.escape(str(value), quote=True)


def format_error(value: object) -> str:
    if isinstance(value, (int, float)) and value >= 0.0:
        return f"{value:.4f}"
    return "-"


def render_image_golden(results_dir: Path, golden: dict) -> str:
    panes = []
    for label, key in (("Output", "output"), ("Golden", "golden"), ("FLIP error", "flip")):
        uri = image_data_uri(results_dir, golden.get(key, ""))
        if uri is None:
            body = '<div class="missing">not available</div>'
        else:
            body = f'<img src="{uri}" alt="{esc(label)}">'
        panes.append(f'<figure><figcaption>{esc(label)}</figcaption>{body}</figure>')
    mean = golden.get("flip_mean", -1.0)
    threshold = golden.get("threshold", 0.0)
    over = isinstance(mean, (int, float)) and mean > threshold
    numbers = (
        f'<dl class="numbers">'
        f'<dt>FLIP mean</dt><dd class="{"bad" if over else ""}">{format_error(mean)}</dd>'
        f'<dt>FLIP max</dt><dd>{format_error(golden.get("flip_max", -1.0))}</dd>'
        f'<dt>Threshold</dt><dd>{format_error(threshold)}</dd>'
        f'<dt>Size</dt><dd>{esc(golden.get("width"))}x{esc(golden.get("height"))} {esc(golden.get("format", ""))}'
        f' (golden {esc(golden.get("golden_width"))}x{esc(golden.get("golden_height"))})</dd>'
        f'</dl>'
    )
    return f'<div class="triptych">{"".join(panes)}</div>{numbers}'


def hex_rows(data: bytes, rows: list[int], diff: set[int]) -> str:
    lines = []
    for row in rows:
        base = row * HEX_BYTES_PER_ROW
        cells = []
        for offset in range(base, base + HEX_BYTES_PER_ROW):
            if offset < len(data):
                text = f"{data[offset]:02x}"
            else:
                text = "--" if offset in diff else "  "
            css = ' class="d"' if offset in diff else ""
            cells.append(f"<span{css}>{text}</span>")
        lines.append(f'<span class="off">{base:08x}</span> {" ".join(cells)}')
    return "\n".join(lines)


def render_buffer_golden(results_dir: Path, golden: dict) -> str:
    output_path = results_dir / golden.get("output", "") if golden.get("output") else None
    golden_path = results_dir / golden.get("golden", "") if golden.get("golden") else None
    output = output_path.read_bytes() if output_path and output_path.is_file() else b""
    reference = golden_path.read_bytes() if golden_path and golden_path.is_file() else b""
    offsets = golden.get("diff_offsets", [])
    diff = set(offsets)
    diff_count = golden.get("diff_count", 0)
    numbers = (
        f'<dl class="numbers">'
        f'<dt>Output</dt><dd>{esc(golden.get("output_size"))} bytes</dd>'
        f'<dt>Golden</dt><dd>{esc(golden.get("golden_size"))} bytes</dd>'
        f'<dt>Differing</dt><dd class="{"bad" if diff_count else ""}">{esc(diff_count)} bytes'
        f'{" (first " + str(len(offsets)) + " listed)" if diff_count > len(offsets) else ""}</dd>'
        f'</dl>'
    )
    if diff:
        rows = sorted({offset // HEX_BYTES_PER_ROW for offset in diff})
        truncated = len(rows) > HEX_MAX_DIFF_ROWS
        rows = rows[:HEX_MAX_DIFF_ROWS]
        note = f'<p class="note">Showing the first {HEX_MAX_DIFF_ROWS} rows with differences.</p>' if truncated else ""
    else:
        total = max(len(output), len(reference))
        rows = list(range(min(HEX_PREVIEW_ROWS, (total + HEX_BYTES_PER_ROW - 1) // HEX_BYTES_PER_ROW)))
        note = '<p class="note">Identical; first rows shown.</p>' if rows else ""
    panes = (
        f'<div class="hex"><figure><figcaption>Output</figcaption><pre>{hex_rows(output, rows, diff)}</pre></figure>'
        f'<figure><figcaption>Golden</figcaption><pre>{hex_rows(reference, rows, diff)}</pre></figure></div>'
    )
    return numbers + note + panes


def render_test(results_dir: Path, test: dict) -> str:
    status = test.get("status", "unknown")
    parts = [
        f'<section class="test" data-status="{esc(status)}">',
        '<header>',
        f'<span class="badge {esc(status)}">{esc(status)}</span>',
        f'<span class="name">{esc(test.get("name", ""))}</span>',
        f'<span class="time">{esc(test.get("duration_ms", 0))} ms</span>',
        '</header>',
    ]
    message = test.get("message", "")
    if message:
        parts.append(f'<pre class="message">{esc(message)}</pre>')
    for golden in test.get("goldens", []):
        parts.append('<div class="golden">')
        parts.append(
            f'<h3>{esc(golden.get("kind", ""))} golden <code>{esc(golden.get("name", ""))}</code>'
            f' <span class="outcome">{esc(golden.get("outcome", ""))}</span></h3>'
        )
        if golden.get("kind") == "image":
            parts.append(render_image_golden(results_dir, golden))
        else:
            parts.append(render_buffer_golden(results_dir, golden))
        parts.append("</div>")
    parts.append("</section>")
    return "\n".join(parts)


STYLE = """
:root {
  --bg: #f7f7f5; --panel: #ffffff; --text: #1d1d1b; --muted: #6b6b66; --border: #dcdcd6;
  --pass: #1f7a3f; --fail: #b3261e; --skip: #8a6d00; --diff: #ffd7d3; --diff-text: #8f1d15;
  --code: #f0f0ec;
}
@media (prefers-color-scheme: dark) {
  :root {
    --bg: #161615; --panel: #1f1f1d; --text: #ececea; --muted: #a0a09a; --border: #3a3a36;
    --pass: #5cc98a; --fail: #ff8a80; --skip: #e3c55a; --diff: #5a1f1a; --diff-text: #ffb4ab;
    --code: #2a2a27;
  }
}
* { box-sizing: border-box; }
body { margin: 0; padding: 16px; background: var(--bg); color: var(--text);
  font: 14px/1.45 system-ui, -apple-system, "Segoe UI", sans-serif; }
h1 { font-size: 20px; margin: 0 0 4px; }
.meta { color: var(--muted); margin-bottom: 12px; overflow-wrap: anywhere; }
.counts { display: flex; flex-wrap: wrap; gap: 8px; margin-bottom: 12px; }
.counts button { border: 1px solid var(--border); background: var(--panel); color: var(--text);
  border-radius: 6px; padding: 6px 12px; cursor: pointer; font: inherit; }
.counts button.active { outline: 2px solid var(--text); }
.test { background: var(--panel); border: 1px solid var(--border); border-radius: 8px;
  padding: 10px 12px; margin-bottom: 10px; }
.test header { display: flex; flex-wrap: wrap; align-items: baseline; gap: 10px; }
.test .name { font-family: ui-monospace, Consolas, monospace; overflow-wrap: anywhere; flex: 1 1 auto; }
.test .time { color: var(--muted); }
.badge { font-weight: 600; text-transform: uppercase; font-size: 12px; }
.badge.passed { color: var(--pass); } .badge.failed { color: var(--fail); } .badge.skipped { color: var(--skip); }
.message { background: var(--code); padding: 8px; border-radius: 6px; white-space: pre-wrap;
  overflow-wrap: anywhere; margin: 8px 0 0; }
.golden h3 { font-size: 14px; margin: 12px 0 6px; }
.golden .outcome { color: var(--muted); font-weight: 400; }
.triptych { display: flex; flex-wrap: wrap; gap: 12px; }
figure { margin: 0; }
figcaption { color: var(--muted); font-size: 12px; margin-bottom: 4px; }
.triptych img { width: 192px; max-width: 100%; image-rendering: pixelated; border: 1px solid var(--border);
  background: repeating-conic-gradient(#8884 0% 25%, transparent 0% 50%) 50% / 16px 16px; }
.missing { width: 192px; height: 96px; display: grid; place-items: center; color: var(--muted);
  border: 1px dashed var(--border); }
.numbers { display: grid; grid-template-columns: max-content 1fr; gap: 2px 12px; margin: 8px 0 0; }
.numbers dt { color: var(--muted); } .numbers dd { margin: 0; font-family: ui-monospace, Consolas, monospace; }
.numbers dd.bad { color: var(--fail); font-weight: 600; }
.hex { display: flex; flex-wrap: wrap; gap: 12px; }
.hex figure { min-width: 0; max-width: 100%; }
.hex pre { background: var(--code); padding: 8px; border-radius: 6px; margin: 0; overflow-x: auto;
  font: 12px/1.4 ui-monospace, Consolas, monospace; }
.hex .off { color: var(--muted); }
.hex .d { background: var(--diff); color: var(--diff-text); }
.note { color: var(--muted); margin: 6px 0; }
"""

SCRIPT = """
const buttons = document.querySelectorAll('.counts button');
function apply(filter) {
  buttons.forEach(b => b.classList.toggle('active', b.dataset.filter === filter));
  document.querySelectorAll('.test').forEach(t => {
    t.hidden = (filter !== 'all') && (t.dataset.status !== filter);
  });
}
buttons.forEach(b => b.addEventListener('click', () => apply(b.dataset.filter)));
apply(document.querySelector('.test[data-status="failed"]') ? 'failed' : 'all');
"""


def render_report(results_dir: Path, results: dict) -> str:
    summary = results.get("summary", {})
    tests = results.get("tests", [])
    ordering = {"failed": 0, "passed": 1, "skipped": 2}
    tests = sorted(tests, key=lambda t: (ordering.get(t.get("status"), 3), t.get("name", "")))
    buttons = [
        ("all", f'All {summary.get("total", len(tests))}'),
        ("failed", f'Failed {summary.get("failed", 0)}'),
        ("passed", f'Passed {summary.get("passed", 0)}'),
        ("skipped", f'Skipped {summary.get("skipped", 0)}'),
    ]
    button_html = "".join(f'<button data-filter="{key}">{esc(label)}</button>' for key, label in buttons)
    body = "\n".join(render_test(results_dir, test) for test in tests)
    return f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>GPU test report</title>
<style>{STYLE}</style>
</head>
<body>
<h1>erhe GPU test report</h1>
<div class="meta">{esc(results.get("device", ""))} - {esc(results.get("backend", ""))} - {esc(results.get("timestamp", ""))}
 - {esc(results.get("duration_ms", 0))} ms</div>
<div class="counts">{button_html}</div>
{body}
<script>{SCRIPT}</script>
</body>
</html>
"""


def main() -> int:
    parser = argparse.ArgumentParser(description="Render erhe GPU test results.json into a self-contained report.html")
    parser.add_argument("results_dir", type=Path, help="directory holding results.json and artifacts/")
    parser.add_argument("--output", type=Path, default=None, help="report path (default: <results dir>/report.html)")
    arguments = parser.parse_args()

    results_dir: Path = arguments.results_dir
    results_path = results_dir / "results.json"
    if not results_path.is_file():
        print(f"error: {results_path} does not exist", file=sys.stderr)
        return 1
    results = json.loads(results_path.read_text(encoding="utf-8"))
    output_path: Path = arguments.output if arguments.output is not None else (results_dir / "report.html")
    output_path.write_text(render_report(results_dir, results), encoding="utf-8")
    print(f"wrote {output_path} ({output_path.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
