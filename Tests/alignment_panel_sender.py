import math
import socket
import time
import tkinter as tk

import msgpack

HOST = "127.0.0.1"
PORT = 7500
RATE_HZ = 30

APEX_XY = {"place": 0.002, "grasp": 0.004}
APEX_YAW = {"place": math.radians(3.0), "grasp": math.radians(6.0)}
TOL_TILT = {"place": math.radians(1.5), "grasp": math.radians(5.0)}
CONE_XY = 0.15
CONE_YAW = math.radians(0.1) / 0.001
DONE_Z = {"place": -0.013, "grasp": 0.012}
INSERT_DEPTH = 0.015


def build(arm, phase, dx, dy, dz, yaw_deg, lean_x_deg, lean_y_deg, target_yaw_deg, speed, anchor, seq, cmd_lead=(0.0, 0.0)):
    apex_ok = math.hypot(dx, dy) <= APEX_XY[phase] and abs(math.radians(yaw_deg)) <= APEX_YAW[phase]
    if phase == "place" and not apex_ok:
        dz = max(dz, 0.0)
    zp = max(dz, 0.0)
    tol_xy = APEX_XY[phase] + CONE_XY * zp
    tol_yaw = APEX_YAW[phase] + CONE_YAW * zp
    tol_tilt = TOL_TILT[phase]
    yaw = math.radians(yaw_deg)
    lean = (math.radians(lean_x_deg), math.radians(lean_y_deg))
    tilt = math.hypot(*lean)
    xy_ok = math.hypot(dx, dy) <= tol_xy
    yaw_ok = abs(yaw) <= tol_yaw
    tilt_ok = tilt <= tol_tilt
    if phase == "place":
        depth = min(max(-dz / INSERT_DEPTH, 0.0), 1.0)
        done = apex_ok and tilt_ok and dz <= DONE_Z[phase]
        wedged = (not tilt_ok) and dz < -0.004
        tilt_active = dz < 0.008
    else:
        depth = 0.0
        done = apex_ok and dz <= DONE_Z[phase]
        wedged = False
        tilt_active = False
    vmax = 0.020 + 0.8 * zp
    too_fast = (not done) and dz < 0.08 and speed > vmax
    return {
        "type": "alignment",
        "seq": seq,
        "arm": arm,
        "active": True,
        "phase": phase,
        "pos_err": [dx, dy, dz],
        "yaw_err": yaw,
        "target_yaw": math.radians(target_yaw_deg),
        "lean_err": list(lean),
        "tol_xy": tol_xy,
        "tol_yaw": tol_yaw,
        "tol_tilt": tol_tilt,
        "z_range": 0.10,
        "depth": depth,
        "xy_ok": xy_ok,
        "yaw_ok": yaw_ok,
        "tilt_ok": tilt_ok,
        "tilt_active": tilt_active,
        "done": done,
        "too_fast": too_fast,
        "wedged": wedged,
        "anchor": anchor,
        "cmd_err": [dx + cmd_lead[0], dy + cmd_lead[1], dz],
        "t_issue_ns": time.time_ns(),
    }


class App:
    def __init__(self, root):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.seq = 0
        self.root = root
        root.title("alignment panel sender")
        self.arm = tk.StringVar(value="right")
        self.phase = tk.StringVar(value="place")
        self.active = tk.BooleanVar(value=True)
        row = tk.Frame(root)
        row.pack(fill="x")
        for v in ("left", "right"):
            tk.Radiobutton(row, text=v, variable=self.arm, value=v).pack(side="left")
        for v in ("grasp", "place"):
            tk.Radiobutton(row, text=v, variable=self.phase, value=v).pack(side="left")
        tk.Checkbutton(row, text="active", variable=self.active).pack(side="left")
        self.vars = {}
        spec = [
            ("dx_mm", -40, 40, 8), ("dy_mm", -40, 40, -5), ("dz_mm", -15, 120, 60),
            ("yaw_deg", -45, 45, 10), ("lean_x_deg", -8, 8, 0), ("lean_y_deg", -8, 8, 0),
            ("target_yaw_deg", -180, 180, 0), ("speed_mm_s", 0, 150, 40),
            ("cmd_lead_x_mm", -30, 30, 0), ("cmd_lead_y_mm", -30, 30, 0),
            ("anchor_x", 0.3, 1.0, 0.65), ("anchor_y", -0.4, 0.4, 0.0), ("anchor_z", 0.6, 1.1, 0.85),
        ]
        for name, lo, hi, val in spec:
            var = tk.DoubleVar(value=val)
            res = 0.01 if name.startswith("anchor") else (0.5 if "lean" in name else 1)
            tk.Scale(root, label=name, from_=lo, to=hi, resolution=res, orient="horizontal", length=420, variable=var).pack()
            self.vars[name] = var
        tk.Button(root, text="play approach", command=self.play).pack(pady=4)
        self.playing = False
        self.tick()

    def play(self):
        self.playing = not self.playing

    def tick(self):
        g = {k: v.get() for k, v in self.vars.items()}
        if self.playing:
            nz = g["dz_mm"] - g["speed_mm_s"] / RATE_HZ
            if nz <= -15:
                nz = -15
                self.playing = False
            self.vars["dz_mm"].set(nz)
            g["dz_mm"] = nz
        msg = build(
            self.arm.get(), self.phase.get(),
            g["dx_mm"] / 1000.0, g["dy_mm"] / 1000.0, g["dz_mm"] / 1000.0,
            g["yaw_deg"], g["lean_x_deg"], g["lean_y_deg"], g["target_yaw_deg"], g["speed_mm_s"] / 1000.0,
            [g["anchor_x"], g["anchor_y"], g["anchor_z"]], self.seq,
            (g["cmd_lead_x_mm"] / 1000.0, g["cmd_lead_y_mm"] / 1000.0),
        )
        msg["active"] = bool(self.active.get())
        self.sock.sendto(msgpack.packb(msg), (HOST, PORT))
        self.seq += 1
        self.root.after(int(1000 / RATE_HZ), self.tick)


if __name__ == "__main__":
    root = tk.Tk()
    App(root)
    root.mainloop()
