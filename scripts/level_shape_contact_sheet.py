#!/usr/bin/env python3
"""Put two renders of each kit shape side by side on one contact sheet.

Reads the images scripts/level_shape_render.py writes to logs/ under two tags
(logs/shape_render_<id>_<tag>.png, or the seed 1 image
logs/shape_render_<id>_1_<tag>.png of a seeded shape), crops the middle of each
and lays the pairs out two to a row with their names: a before / after review
of many shapes in one image (doc/plans/level_kit_normals.md "Verification").

Usage:
    py -3 scripts/level_shape_contact_sheet.py --tags before after --out logs/sheet.png
        [--crop 700 500] [--scale 0.5] [--logs logs] <shape id> [<shape id> ...]
"""

import argparse
import glob
import os
import sys

from PIL import Image, ImageDraw

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LABEL_HEIGHT = 18
PAIRS_PER_ROW = 2


def load_cropped(logs, shape_id, tag, crop_w, crop_h, scale):
    """The render of the shape under the tag, cropped to its middle and scaled; None when there is none."""
    paths = sorted(glob.glob(os.path.join(logs, f"shape_render_{shape_id}_{tag}.png"))) or \
        sorted(glob.glob(os.path.join(logs, f"shape_render_{shape_id}_1_{tag}.png")))
    if not paths:
        return None
    image = Image.open(paths[0]).convert("RGB")
    w, h = image.size
    image = image.crop(((w - crop_w) // 2, (h - crop_h) // 2, (w + crop_w) // 2, (h + crop_h) // 2))
    return image.resize((int(crop_w * scale), int(crop_h * scale)))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--tags", nargs=2, required=True, metavar=("LEFT", "RIGHT"))
    parser.add_argument("--out", required=True)
    parser.add_argument("--crop", nargs=2, type=int, default=[700, 500], metavar=("WIDTH", "HEIGHT"))
    parser.add_argument("--scale", type=float, default=0.5)
    parser.add_argument("--logs", default=os.path.join(REPO, "logs"))
    parser.add_argument("ids", nargs="+")
    args = parser.parse_args()

    crop_w, crop_h = args.crop
    cell_w, cell_h = int(crop_w * args.scale), int(crop_h * args.scale)
    rows = (len(args.ids) + PAIRS_PER_ROW - 1) // PAIRS_PER_ROW
    sheet = Image.new("RGB", (PAIRS_PER_ROW * 2 * cell_w, rows * (cell_h + LABEL_HEIGHT)), (30, 30, 30))
    draw = ImageDraw.Draw(sheet)
    missing = []
    for index, shape_id in enumerate(args.ids):
        row, column = divmod(index, PAIRS_PER_ROW)
        x0, y0 = column * 2 * cell_w, row * (cell_h + LABEL_HEIGHT)
        draw.text((x0 + 4, y0 + 2), f"{shape_id}  ({args.tags[0]} | {args.tags[1]})", fill=(255, 255, 0))
        for k, tag in enumerate(args.tags):
            image = load_cropped(args.logs, shape_id, tag, crop_w, crop_h, args.scale)
            if image is None:
                missing.append(f"{shape_id} {tag}")
                continue
            sheet.paste(image, (x0 + (k * cell_w), y0 + LABEL_HEIGHT))
    sheet.save(args.out)
    print(f"{args.out} {sheet.size[0]}x{sheet.size[1]}")
    if missing:
        print("no render for: " + ", ".join(missing))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
