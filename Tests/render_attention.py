#!/usr/bin/env python3
"""
Render the gaze attention video offline from a logged interface session.

Usage:
  python render_attention.py <session_dir> [--out FILE] [--radius-frac 0.10] [--dark 0.20] [--dot-radius 6] [--no-dot]

Reads session.mp4, frames.jsonl and metadata.json from <session_dir> and writes
session_attention.mp4 next to them (H.264 via ffmpeg if available, else mp4v via OpenCV).
Requires: numpy, opencv-python.
"""

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

import cv2
import numpy as np


def load_gaze(path):
    rows = []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            o = json.loads(line)
            rows.append((float(o.get("gaze_u", 0.5)), float(o.get("gaze_v", 0.5)),
                         float(o.get("gaze_confidence", 0.0)), bool(o.get("gaze_valid", False))))
    return rows


class Writer:
    def __init__(self, out, w, h, fps):
        self.proc = None
        self.cv = None
        ffmpeg = shutil.which("ffmpeg")
        if ffmpeg:
            self.proc = subprocess.Popen(
                [ffmpeg, "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "bgr24",
                 "-s", f"{w}x{h}", "-r", f"{fps}", "-i", "-",
                 "-c:v", "libx264", "-preset", "medium", "-crf", "20", "-pix_fmt", "yuv420p", str(out)],
                stdin=subprocess.PIPE)
            self.name = "ffmpeg/libx264"
        else:
            self.cv = cv2.VideoWriter(str(out), cv2.VideoWriter_fourcc(*"mp4v"), fps, (w, h))
            self.name = "opencv/mp4v"

    def write(self, frame):
        if self.proc:
            self.proc.stdin.write(frame.tobytes())
        else:
            self.cv.write(frame)

    def close(self):
        if self.proc:
            self.proc.stdin.close()
            self.proc.wait()
        else:
            self.cv.release()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("session_dir")
    ap.add_argument("--out", default=None)
    ap.add_argument("--radius-frac", type=float, default=None)
    ap.add_argument("--dark", type=float, default=None)
    ap.add_argument("--dot-radius", type=int, default=6)
    ap.add_argument("--no-dot", action="store_true")
    args = ap.parse_args()

    d = Path(args.session_dir)
    video = d / "session.mp4"
    frames = d / "frames.jsonl"
    if not video.exists() or not frames.exists():
        sys.exit(f"missing session.mp4 or frames.jsonl in {d}")

    meta = {}
    if (d / "metadata.json").exists():
        meta = json.loads((d / "metadata.json").read_text(encoding="utf-8"))
    radius_frac = args.radius_frac if args.radius_frac is not None else float(meta.get("gaze_radius_frac", 0.10))
    dark = args.dark if args.dark is not None else float(meta.get("peripheral_brightness", 0.20))
    dark = min(max(dark, 0.0), 1.0)

    gaze = load_gaze(frames)
    cap = cv2.VideoCapture(str(video))
    if not cap.isOpened():
        sys.exit(f"cannot open {video}")
    w = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    h = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    fps = cap.get(cv2.CAP_PROP_FPS) or float(meta.get("target_fps", 30.0))
    n_video = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))

    out = Path(args.out) if args.out else d / "session_attention.mp4"
    writer = Writer(out, w, h, fps)

    sig_sq = (radius_frac * w) ** 2
    xs = np.arange(w, dtype=np.float32)
    ys = np.arange(h, dtype=np.float32)
    dark_frame = np.float32(dark)
    dot_r2 = args.dot_radius * args.dot_radius
    yy, xx = np.mgrid[-args.dot_radius:args.dot_radius + 1, -args.dot_radius:args.dot_radius + 1]
    dot_mask = (xx * xx + yy * yy) <= dot_r2

    print(f"{video.name}: {w}x{h} @ {fps:.1f} fps, {n_video} frames, {len(gaze)} gaze rows, "
          f"radius_frac={radius_frac} dark={dark} -> {out.name} ({writer.name})")
    if n_video and abs(n_video - len(gaze)) > 2:
        print(f"warning: frame count {n_video} != gaze rows {len(gaze)}; extra frames reuse the last gaze row")

    i = 0
    while True:
        ok, frame = cap.read()
        if not ok:
            break
        g = gaze[min(i, len(gaze) - 1)] if gaze else (0.5, 0.5, 0.0, False)
        u, v, conf, valid = g
        gx, gy = u * w, v * h
        if valid and conf > 0.0:
            ex = np.exp(-0.45 * (xs - gx) ** 2 / (2.0 * sig_sq))
            ey = np.exp(-0.45 * (ys - gy) ** 2 / (2.0 * sig_sq))
            weight = dark_frame + (1.0 - dark_frame) * np.outer(ey, ex)
        else:
            weight = np.full((h, w), dark_frame, dtype=np.float32)
        frame = np.clip(frame.astype(np.float32) * weight[:, :, None], 0, 255).astype(np.uint8)

        if valid and not args.no_dot:
            cx, cy = int(gx), int(gy)
            r = args.dot_radius
            x0, x1 = max(cx - r, 0), min(cx + r + 1, w)
            y0, y1 = max(cy - r, 0), min(cy + r + 1, h)
            if x0 < x1 and y0 < y1:
                m = dot_mask[(y0 - (cy - r)):(y1 - (cy - r)), (x0 - (cx - r)):(x1 - (cx - r))]
                frame[y0:y1, x0:x1][m] = (50, 50, 255)

        writer.write(frame)
        i += 1
        if i % 300 == 0:
            print(f"  {i} frames")

    cap.release()
    writer.close()
    print(f"done: {i} frames -> {out}")


if __name__ == "__main__":
    main()
