#!/usr/bin/env python3
"""Build the embedded Clawd frames from the vendored animated SVGs."""

import argparse
from io import BytesIO
from itertools import groupby
from pathlib import Path
import struct
import xml.etree.ElementTree as ET
import zlib

from PIL import Image
from playwright.sync_api import sync_playwright


# Keep this order in sync with clawd_pose_t. Each entry gives the frame timing
# in milliseconds and the length of the loop or one-shot sequence.
CLIPS = (
    ("clawd-idle-living", 200, 16000),
    ("clawd-crab-walking", 125, 1000),
    ("clawd-happy", 100, 1000),
    ("clawd-sleeping", 250, 6000),
    ("clawd-grooving", 100, 1600),
    ("clawd-working-juggling", 100, 1200),
    ("clawd-eureka", 150, 6000),
    ("clawd-hat-mishap", 175, 7000),
    ("clawd-coffee-head-flip", 125, 4000),
    ("clawd-dizzy", 200, 4000),
    ("clawd-snack", 100, 2000),
)
SCALE = 16  # Rasterize at the desktop's display size.
MAX_RUN_BYTES = 24 * 1024  # Match the display task's decode buffer.
ANCHOR_X, ANCHOR_Y = 7.5, 15  # Center of the torso at floor level in all SVGs.
CLIP_RECORD = struct.Struct("<HHH")  # First frame, frame count, frame time.
FRAME_RECORD = struct.Struct("<IhhHH")  # Data offset, left, top, width, height.


def rgb565_alpha(pixel):
    red, green, blue, alpha = pixel
    alpha = (alpha + 8) // 17
    rgb = ((red & 248) << 8) | ((green & 252) << 3) | (blue >> 3)
    return (rgb if alpha else 0), alpha


def encode_frame(png, left, top):
    with Image.open(BytesIO(png)) as image:
        pixels = image.convert("RGBA")
    bounds = pixels.getchannel("A").getbbox()
    if not bounds:
        raise ValueError("empty animation frame")
    cropped = pixels.crop(bounds)
    data = bytearray()
    for (color, alpha), run in groupby(map(rgb565_alpha, cropped.getdata())):
        count = sum(1 for _ in run)
        while count:
            length = min(count, 255)
            data.extend(struct.pack("<BBH", length, alpha, color))
            count -= length
    if len(data) > MAX_RUN_BYTES:
        raise ValueError(f"frame requires {len(data)} run bytes, limit is {MAX_RUN_BYTES}")
    return (left + bounds[0], top + bounds[1], *cropped.size), data


def render_clip(page, path, interval, duration):
    svg = path.read_text()
    x, y, w, h = map(float, ET.fromstring(svg).attrib["viewBox"].split())
    width, height = round(w * SCALE), round(h * SCALE)
    left, top = round((x - ANCHOR_X) * SCALE), round((y - ANCHOR_Y) * SCALE)
    page.set_viewport_size({"width": width, "height": height})
    page.set_content(f"""<!doctype html><style>
    html,body {{ margin:0; overflow:hidden; background:transparent; }}
    svg {{ display:block; width:{width}px; height:{height}px; }}
    svg, svg * {{ shape-rendering:crispEdges; }}
    </style>{svg}""")
    page.evaluate("""async () => {
        const animations = document.getAnimations();
        if (!animations.length) throw new Error('no SVG animations');
        animations.forEach(animation => animation.pause());
        await Promise.all(animations.map(animation => animation.ready));
    }""")
    for time_ms in range(0, duration, interval):
        page.evaluate("time => document.getAnimations().forEach(a => a.currentTime = time)", time_ms)
        yield encode_frame(page.screenshot(omit_background=True), left, top)


def main():
    firmware = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=firmware / "main" / "clawd_frames.bin")
    args = parser.parse_args()

    clips, frames = [], []
    with sync_playwright() as playwright:
        browser = playwright.chromium.launch()
        page = browser.new_page()
        for name, interval, duration in CLIPS:
            first = len(frames)
            frames.extend(render_clip(page, firmware / "assets" / "clawd" / f"{name}.svg", interval, duration))
            clips.append((first, len(frames) - first, interval))

    data_offset = len(clips) * CLIP_RECORD.size + len(frames) * FRAME_RECORD.size
    index = bytearray().join(CLIP_RECORD.pack(*clip) for clip in clips)
    offsets, runs = {}, bytearray()
    for bounds, data in frames:
        encoded = bytes(data)
        if encoded not in offsets:
            offsets[encoded] = data_offset + len(runs)
            compressed = zlib.compress(encoded, level=9)
            runs.extend(struct.pack("<H", len(compressed)))
            runs.extend(compressed)
        index.extend(FRAME_RECORD.pack(offsets[encoded], *bounds))
    args.out.write_bytes(index + runs)
    print(f"{len(frames)} Clawd frames, {len(index) + len(runs):,} bytes")


if __name__ == "__main__":
    main()
