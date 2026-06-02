#!/usr/bin/env python3
"""
Teleop Log Plotter
Usage: python plot_teleop.py <log_folder> [options]

Examples:
  python plot_teleop.py ../Logs/TeleOp/session_001 --plot pose
  python plot_teleop.py ../Logs/TeleOp/session_001 --plot video latency
  python plot_teleop.py ../Logs/TeleOp/session_001 --plot all
  python plot_teleop.py ../Logs/TeleOp/session_001 --plot pose --arm left
  python plot_teleop.py ../Logs/TeleOp/session_001 --plot state --list-sessions

  python plot_teleop.py ..\Logs\TeleOp --list
  python plot_teleop.py ..\Logs\TeleOp --session X --plot video latency
"""

import argparse
import os
import sys
import glob
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
import numpy as np

# ── Style ──────────────────────────────────────────────────────────────────────
plt.rcParams.update({
    "figure.facecolor": "#0f1117",
    "axes.facecolor":   "#161b22",
    "axes.edgecolor":   "#30363d",
    "axes.labelcolor":  "#c9d1d9",
    "axes.grid":        True,
    "grid.color":       "#21262d",
    "grid.linewidth":   0.8,
    "xtick.color":      "#8b949e",
    "ytick.color":      "#8b949e",
    "text.color":       "#c9d1d9",
    "legend.facecolor": "#161b22",
    "legend.edgecolor": "#30363d",
    "figure.dpi":       120,
})

ARM_COLORS = {
    "left":  {"pos": "#58a6ff", "rot": "#3fb950", "clutch": "#f78166"},
    "right": {"pos": "#d2a8ff", "rot": "#ffa657", "clutch": "#f78166"},
}
VIDEO_COLORS = {"latency": "#58a6ff", "jitter": "#ffa657", "loss": "#f78166", "fps": "#3fb950"}
LATENCY_COLORS = {"data_latency": "#58a6ff", "data_rate": "#3fb950"}


def load_csv(path: str) -> pd.DataFrame:
    df = pd.read_csv(path, sep=";")
    # Convert nanosecond timestamp to seconds from start
    df["t"] = (df["timestamp_ns"] - df["timestamp_ns"].iloc[0]) / 1e9
    return df


def find_session(root: str, session: str) -> str:
    """Resolve session path — accepts full path, folder name, or index."""
    if os.path.isfile(os.path.join(session, "stream.csv")):
        return os.path.join(session, "stream.csv")
    if os.path.isfile(os.path.join(root, session, "stream.csv")):
        return os.path.join(root, session, "stream.csv")
    # Try as integer index
    sessions = sorted(glob.glob(os.path.join(root, "*", "stream.csv")))
    try:
        idx = int(session)
        return sessions[idx]
    except (ValueError, IndexError):
        pass
    raise FileNotFoundError(f"Could not find stream.csv for session '{session}' in {root}")


def list_sessions(root: str):
    sessions = sorted(glob.glob(os.path.join(root, "*", "stream.csv")))
    if not sessions:
        print(f"No sessions found in {root}")
        return
    print(f"\nAvailable sessions in {root}:\n")
    for i, s in enumerate(sessions):
        folder = os.path.basename(os.path.dirname(s))
        df = pd.read_csv(s, sep=";", nrows=2)
        try:
            df2 = pd.read_csv(s, sep=";")
            dur = (df2["timestamp_ns"].iloc[-1] - df2["timestamp_ns"].iloc[0]) / 1e9
            rows = len(df2)
        except Exception:
            dur, rows = 0, 0
        print(f"  [{i:2d}]  {folder:<40}  {rows:>6} rows  {dur:6.1f}s")
    print()


# ── Plot functions ─────────────────────────────────────────────────────────────

def plot_pose(df: pd.DataFrame, arms: list, title: str):
    """End-effector position (xyz) and quaternion per arm."""
    n = len(arms)
    fig, axes = plt.subplots(n, 2, figsize=(14, 4 * n))
    fig.suptitle(f"{title} — End-Effector Pose", fontsize=13, color="#c9d1d9", y=1.01)
    if n == 1:
        axes = [axes]

    for row, arm in enumerate(arms):
        c = ARM_COLORS[arm]
        ax_pos, ax_rot = axes[row]

        for coord, ls in zip(["px", "py", "pz"], ["-", "--", ":"]):
            ax_pos.plot(df["t"], df[f"{arm}_{coord}"], lw=1.2, ls=ls,
                        color=c["pos"], alpha=0.9, label=coord.upper())
        ax_pos.set_title(f"{arm.capitalize()} — Position (m)", color="#c9d1d9")
        ax_pos.set_xlabel("Time (s)")
        ax_pos.legend(loc="upper right", fontsize=8)

        for q, ls in zip(["qw", "qx", "qy", "qz"], ["-", "--", ":", "-."]):
            ax_rot.plot(df["t"], df[f"{arm}_{q}"], lw=1.2, ls=ls,
                        color=c["rot"], alpha=0.9, label=q)
        ax_rot.set_title(f"{arm.capitalize()} — Quaternion", color="#c9d1d9")
        ax_rot.set_xlabel("Time (s)")
        ax_rot.legend(loc="upper right", fontsize=8)

    plt.tight_layout()


def plot_video(df: pd.DataFrame, title: str):
    """Video stream quality metrics."""
    fig = plt.figure(figsize=(14, 8))
    fig.suptitle(f"{title} — Video Stream", fontsize=13, color="#c9d1d9")
    gs = gridspec.GridSpec(2, 2, figure=fig, hspace=0.4, wspace=0.3)

    metrics = [
        ("video_latency_ms", "Latency (ms)",    VIDEO_COLORS["latency"]),
        ("video_jitter_ms",  "Jitter (ms)",      VIDEO_COLORS["jitter"]),
        ("video_loss_pct",   "Packet Loss (%)",  VIDEO_COLORS["loss"]),
        ("video_fps",        "FPS",              VIDEO_COLORS["fps"]),
    ]

    for i, (col, label, color) in enumerate(metrics):
        ax = fig.add_subplot(gs[i // 2, i % 2])
        ax.plot(df["t"], df[col], lw=1.2, color=color, alpha=0.9)
        ax.fill_between(df["t"], df[col], alpha=0.12, color=color)
        ax.set_title(label, color="#c9d1d9")
        ax.set_xlabel("Time (s)")

        # Draw mean line
        mean_val = df[col].mean()
        ax.axhline(mean_val, color=color, lw=0.8, ls="--", alpha=0.6,
                   label=f"mean: {mean_val:.2f}")
        ax.legend(fontsize=8)


def plot_latency(df: pd.DataFrame, title: str):
    """Data channel latency and message rate."""
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(14, 6), sharex=True)
    fig.suptitle(f"{title} — Data Channel", fontsize=13, color="#c9d1d9")

    ax1.plot(df["t"], df["data_latency_ms"], lw=1.2,
             color=LATENCY_COLORS["data_latency"], label="Latency (ms)")
    ax1.fill_between(df["t"], df["data_latency_ms"], alpha=0.12,
                     color=LATENCY_COLORS["data_latency"])
    mean_lat = df["data_latency_ms"].mean()
    ax1.axhline(mean_lat, lw=0.8, ls="--", color=LATENCY_COLORS["data_latency"],
                alpha=0.7, label=f"mean: {mean_lat:.2f} ms")
    ax1.set_ylabel("Latency (ms)")
    ax1.legend(fontsize=8)

    ax2.plot(df["t"], df["data_msg_rate_hz"], lw=1.2,
             color=LATENCY_COLORS["data_rate"], label="Msg Rate (Hz)")
    ax2.fill_between(df["t"], df["data_msg_rate_hz"], alpha=0.12,
                     color=LATENCY_COLORS["data_rate"])
    ax2.set_ylabel("Rate (Hz)")
    ax2.set_xlabel("Time (s)")
    ax2.legend(fontsize=8)


def plot_state(df: pd.DataFrame, title: str):
    """Operator state and clutch/gear/grasp per arm."""
    fig, axes = plt.subplots(3, 1, figsize=(14, 8), sharex=True)
    fig.suptitle(f"{title} — Operator State & Control", fontsize=13, color="#c9d1d9")

    # Operator state
    axes[0].step(df["t"], df["operator_state"], lw=1.5, color="#58a6ff", where="post")
    axes[0].set_ylabel("Op State")
    axes[0].set_yticks([0, 1, 2])
    axes[0].set_yticklabels(["Idle", "Active", "Emergency"])

    # Left arm clutch / gear / grasp
    ax = axes[1]
    ax.step(df["t"], df["left_clutch"], lw=1.2, color=ARM_COLORS["left"]["clutch"],
            where="post", label="Clutch")
    ax.step(df["t"], df["left_gear"] / df["left_gear"].max(), lw=1.2,
            color=ARM_COLORS["left"]["pos"], where="post", ls="--", label="Gear (norm)")
    ax.step(df["t"], df["left_grasp"], lw=1.2, color=ARM_COLORS["left"]["rot"],
            where="post", ls=":", label="Grasp")
    ax.set_ylabel("Left Arm")
    ax.legend(fontsize=8, loc="upper right")

    # Right arm
    ax = axes[2]
    ax.step(df["t"], df["right_clutch"], lw=1.2, color=ARM_COLORS["right"]["clutch"],
            where="post", label="Clutch")
    ax.step(df["t"], df["right_gear"] / df["right_gear"].max(), lw=1.2,
            color=ARM_COLORS["right"]["pos"], where="post", ls="--", label="Gear (norm)")
    ax.step(df["t"], df["right_grasp"], lw=1.2, color=ARM_COLORS["right"]["rot"],
            where="post", ls=":", label="Grasp")
    ax.set_ylabel("Right Arm")
    ax.set_xlabel("Time (s)")
    ax.legend(fontsize=8, loc="upper right")

    plt.tight_layout()


def plot_head(df: pd.DataFrame, title: str):
    """Head pan/tilt over time."""
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(14, 5), sharex=True)
    fig.suptitle(f"{title} — Head Gaze", fontsize=13, color="#c9d1d9")

    ax1.plot(df["t"], np.degrees(df["head_pan"]), lw=1.2, color="#58a6ff")
    ax1.set_ylabel("Pan (deg)")

    ax2.plot(df["t"], np.degrees(df["head_tilt"]), lw=1.2, color="#d2a8ff")
    ax2.set_ylabel("Tilt (deg)")
    ax2.set_xlabel("Time (s)")

    plt.tight_layout()


def plot_overview(df: pd.DataFrame, title: str):
    """Quick summary dashboard: one plot per major group."""
    plot_state(df, title)
    plot_pose(df, ["left", "right"], title)
    plot_video(df, title)
    plot_latency(df, title)
    plot_head(df, title)


# ── CLI ────────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="Plot teleop session logs from stream.csv files.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Plot groups:
  pose      End-effector position + quaternion (per arm)
  video     Video latency, jitter, loss, fps
  latency   Data channel latency + message rate
  state     Operator state, clutch/gear/grasp
  head      Head pan and tilt
  all       All of the above

Examples:
  python plot_teleop.py ../Logs/TeleOp --session 0 --plot pose
  python plot_teleop.py ../Logs/TeleOp --session my_session --plot video latency
  python plot_teleop.py ../Logs/TeleOp --session 2 --plot all --arm left
  python plot_teleop.py ../Logs/TeleOp --list
        """
    )

    parser.add_argument("log_root", help="Root log directory (e.g. ../Logs/TeleOp)")
    parser.add_argument("--session", "-s", default="0",
                        help="Session folder name or index (default: 0 = most recent)")
    parser.add_argument("--plot", "-p", nargs="+",
                        choices=["pose", "video", "latency", "state", "head", "all"],
                        default=["all"],
                        help="Which plots to show")
    parser.add_argument("--arm", "-a", nargs="+", choices=["left", "right"],
                        default=["left", "right"],
                        help="Which arms to include in pose plots (default: both)")
    parser.add_argument("--list", "-l", action="store_true",
                        help="List available sessions and exit")
    parser.add_argument("--save", action="store_true",
                        help="Save figures to PNG instead of displaying")
    parser.add_argument("--time", nargs=2, type=float, metavar=("T_START", "T_END"),
                        help="Trim to time window in seconds, e.g. --time 10 60")

    args = parser.parse_args()

    if args.list:
        list_sessions(args.log_root)
        return

    # Load data
    csv_path = find_session(args.log_root, args.session)
    session_name = os.path.basename(os.path.dirname(csv_path))
    print(f"Loading: {csv_path}")
    df = load_csv(csv_path)
    print(f"  {len(df)} rows  |  duration: {df['t'].iloc[-1]:.1f}s")

    # Optional time crop
    if args.time:
        t0, t1 = args.time
        df = df[(df["t"] >= t0) & (df["t"] <= t1)].reset_index(drop=True)
        print(f"  Trimmed to [{t0}s, {t1}s] → {len(df)} rows")

    plots = args.plot
    if "all" in plots:
        plots = ["state", "pose", "video", "latency", "head"]

    figures_before = plt.get_fignums()

    for p in plots:
        if p == "pose":
            plot_pose(df, args.arm, session_name)
        elif p == "video":
            plot_video(df, session_name)
        elif p == "latency":
            plot_latency(df, session_name)
        elif p == "state":
            plot_state(df, session_name)
        elif p == "head":
            plot_head(df, session_name)

    if args.save:
        out_dir = os.path.join(os.path.dirname(csv_path), "plots")
        os.makedirs(out_dir, exist_ok=True)
        new_figs = [f for f in plt.get_fignums() if f not in figures_before]
        for i, fnum in enumerate(new_figs):
            out_path = os.path.join(out_dir, f"{plots[i] if i < len(plots) else i}.png")
            plt.figure(fnum).savefig(out_path, bbox_inches="tight", facecolor="#0f1117")
            print(f"  Saved: {out_path}")
    else:
        plt.show()


if __name__ == "__main__":
    main()
