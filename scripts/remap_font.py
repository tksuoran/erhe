#!/usr/bin/env python3
"""
remap_font.py — Remap TTF font codepoints from one Unicode range to another.

Usage:
    python remap_font.py \
        --input  <input.ttf> \
        --output <output.ttf> \
        --in-start  <hex or decimal codepoint> \
        --in-end    <hex or decimal codepoint> \
        --out-start <hex or decimal codepoint>

Examples:
    # Remap U+0041–U+005A (A–Z) to start at U+FF21 (fullwidth A–Z)
    python remap_font.py --input MyFont.ttf --output MyFont_remapped.ttf \
        --in-start 0x0041 --in-end 0x005A --out-start 0xFF21

    # Same with decimal values
    python remap_font.py --input MyFont.ttf --output MyFont_remapped.ttf \
        --in-start 65 --in-end 90 --out-start 65313

Codepoints may be supplied as:
    • Hex strings   e.g. 0x0041  or  U+0041
    • Decimal ints  e.g. 65
"""

import argparse
import sys
import copy

try:
    from fontTools import ttLib
except ImportError:
    sys.exit(
        "fontTools is required. Install it with:  pip install fonttools"
    )


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def parse_codepoint(value: str) -> int:
    """Parse a codepoint given as hex (0x…, U+…) or decimal."""
    v = value.strip()
    if v.upper().startswith("U+"):
        return int(v[2:], 16)
    if v.lower().startswith("0x"):
        return int(v, 16)
    return int(v)


def codepoint_repr(cp: int) -> str:
    return f"U+{cp:04X} (dec {cp})"


# ---------------------------------------------------------------------------
# Core remapping
# ---------------------------------------------------------------------------

def remap_codepoints(
    input_path: str,
    output_path: str,
    in_start: int,
    in_end: int,
    out_start: int,
) -> None:
    if in_end < in_start:
        sys.exit(
            f"Error: in-end ({codepoint_repr(in_end)}) must be >= "
            f"in-start ({codepoint_repr(in_start)})."
        )

    range_size = in_end - in_start + 1
    out_end    = out_start + range_size - 1
    offset     = out_start - in_start

    print(f"Input font  : {input_path}")
    print(f"Output font : {output_path}")
    print(
        f"Input range : {codepoint_repr(in_start)} – {codepoint_repr(in_end)} "
        f"({range_size} codepoints)"
    )
    print(
        f"Output range: {codepoint_repr(out_start)} – {codepoint_repr(out_end)}"
    )
    print()

    # --- Load font -----------------------------------------------------------
    try:
        font = ttLib.TTFont(input_path)
    except Exception as exc:
        sys.exit(f"Error loading font: {exc}")

    # --- Locate every cmap subtable ------------------------------------------
    if "cmap" not in font:
        sys.exit("Font has no 'cmap' table — cannot remap codepoints.")

    cmap_table = font["cmap"]
    total_moved = 0

    for subtable in cmap_table.tables:
        cmap = subtable.cmap          # dict: codepoint (int) → glyph name (str)

        # Collect entries that fall inside the input range
        to_remap = {
            cp: glyph
            for cp, glyph in cmap.items()
            if in_start <= cp <= in_end
        }

        if not to_remap:
            continue

        print(
            f"  cmap subtable (platform={subtable.platformID} "
            f"encoding={subtable.platEncID} format={subtable.format}): "
            f"{len(to_remap)} glyph(s) to move"
        )

        # Remove old entries, add new ones
        for cp, glyph in to_remap.items():
            del cmap[cp]
            new_cp = cp + offset
            if new_cp in cmap:
                print(
                    f"    Warning: {codepoint_repr(new_cp)} already mapped to "
                    f"'{cmap[new_cp]}' — overwriting with '{glyph}'"
                )
            cmap[new_cp] = glyph
            total_moved += 1

    if total_moved == 0:
        print(
            "No glyphs found in the specified input range. "
            "The output font will be identical to the input."
        )
    else:
        print(f"\nTotal glyph mappings remapped: {total_moved}")

    # --- Save ----------------------------------------------------------------
    try:
        font.save(output_path)
    except Exception as exc:
        sys.exit(f"Error saving font: {exc}")

    print(f"Saved: {output_path}")


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument(
        "--input", "-i", required=True, metavar="INPUT.TTF",
        help="Path to the source .ttf font file.",
    )
    p.add_argument(
        "--output", "-o", required=True, metavar="OUTPUT.TTF",
        help="Path where the remapped .ttf font will be written.",
    )
    p.add_argument(
        "--in-start", required=True, metavar="CODEPOINT",
        help="First codepoint of the input range (hex or decimal).",
    )
    p.add_argument(
        "--in-end", required=True, metavar="CODEPOINT",
        help="Last codepoint of the input range (hex or decimal, inclusive).",
    )
    p.add_argument(
        "--out-start", required=True, metavar="CODEPOINT",
        help="First codepoint of the output range (hex or decimal).",
    )
    return p


def main() -> None:
    args = build_parser().parse_args()

    try:
        in_start  = parse_codepoint(args.in_start)
        in_end    = parse_codepoint(args.in_end)
        out_start = parse_codepoint(args.out_start)
    except ValueError as exc:
        sys.exit(f"Error parsing codepoint: {exc}")

    remap_codepoints(
        input_path  = args.input,
        output_path = args.output,
        in_start    = in_start,
        in_end      = in_end,
        out_start   = out_start,
    )


if __name__ == "__main__":
    main()
