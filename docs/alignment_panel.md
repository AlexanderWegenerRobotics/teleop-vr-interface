# Alignment panel (experiment branch `alignment-panel`)

One 2D panel per arm, drawn next to the gripper through the ghost's scene-capture rig,
never on the insertion axis.

- Dot = source (TCP for grasp, held part for place) relative to the target, seen from above.
  `frame: "world"` (default): panel up = +X, panel left = +Y of the robot/world frame.
  `frame: "view"`: panel up = camera view direction projected on the table, left = left in the view.
- Hollow white circle = commanded pose (ghost), filled dot = measured pose (video). The thin line between them is the latency gap; steer the hollow one onto the target.
- Line through the dot = source yaw, white line through the centre = target yaw.
- Circle at the centre = xy tolerance. With `auto_scale` the circle keeps its size and the
  panel scale follows the engine's tolerance cone (coarser when high above the target).
- The panel sits left or right of the gripper, on the side away from the target, so it never covers it.
- Ring around the dot = height above the target, shrinks to the dot at contact.
- Arc around the dot = insertion depth (place).
- Right edge gauge = lean along the view direction (pitch), bottom gauge = lean left/right
  (roll). Marker sits on the side the part leans to; green band = tolerance. Full strength
  when the engine sets `tilt_active`, dimmed otherwise, hidden in grasp.
- Gold = not yet, green = ok, orange = too fast (ring) or wedged (tilt, depth).

## Material: /Game/Materials/M_AlignmentPanel

The C++ loads exactly this path. Keep it separate from M_BoundaryPatch.

1. Content Browser, /Game/Materials: duplicate `M_GraspGlyph` and rename the copy to
   `M_AlignmentPanel`. The duplicate keeps the settings the billboard path already works with.
   Open it and delete every node except the output node.
2. Details panel: Blend Mode Translucent, Shading Model Unlit, Two Sided on,
   Translucency > Disable Depth Test on.
3. Add nodes:
   - `TexCoord` (coordinate index 0)
   - six `Vector Parameter` nodes named exactly `XY`, `Yaw`, `ZD`, `Tilt`, `Flags`, `Cmd`
   - one `Custom` node: Output Type `CMOT Float 4`, Inputs in this order and with these
     names: `UV`, `XY`, `Yaw`, `ZD`, `Tilt`, `Flags`, `Cmd`. Code: paste the whole of
     `docs/alignment_panel_custom_node.hlsl`.
4. Wire: TexCoord -> UV. Each Vector Parameter's RGBA output -> the input of the same name
   (if your node shows no RGBA pin, use `AppendVector(RGB, A)` instead).
5. Custom output -> `ComponentMask (R G B)` -> Emissive Color.
   Custom output -> `ComponentMask (A)` -> Opacity.
6. Save.

Preview check (set the parameter defaults, then reset them to 0 afterwards):
XY (0.3, 0.2, 0.25, 1), Yaw (0, 1, 0.3, 0.95), ZD (0.5, 0, 0, 0), Tilt (0.2, -0.3, 0.25, 1),
Flags (0, 1, 0, 0). You should see a gold dot up-right of the centre with a green yaw line,
a gold ring around it and both edge gauges. If the gauges come out on the left/top, the
sprite UVs are mirrored: flip the sign in the first line of the code that builds `p`.

## Wire format (nudge engine -> interface, UDP msgpack on nudge_port)

```
type: "alignment", arm: "left"|"right", active: bool, phase: "grasp"|"place",
pos_err: [dx, dy, dz]   source - target, world frame, m
yaw_err, target_yaw     rad, world frame (yaw_err already wrapped by part symmetry)
lean_err: [lx, ly]      rad, lean of source vs target toward +X / +Y
tol_xy, tol_yaw, tol_tilt   tolerances at the current height (cone evaluated by the engine)
z_range                 height where the ring is at full size, m
depth                   insertion progress 0..1
xy_ok, yaw_ok, tilt_ok, tilt_active, done, too_fast, wedged   bool
anchor: [x, y, z]       optional, world m: measured source position. Panel position when the ghost is not seeded;
                        with a seeded ghost the command marker is pos_err + (ghost - anchor)
cmd_err: [dx, dy, dz]   optional, overrides the command marker (command - target)
t_issue_ns, seq
```

`active: false` (or no message for 0.4 s) fades the panel out.

## Config

`overlay*.json`:
`"alignment_panel": { "enabled": true, "size_cm": 8.0, "side_offset_cm": 12.0, "up_offset_cm": 4.0, "follow_tau_s": 0.12, "auto_scale": true, "frame": "world" }`.
The overlay itself (`"enabled"` at the top of the file) must be on.

## Trying it without the engine

`pip install msgpack`, start the interface, then `python Tests/alignment_panel_sender.py`.
Sliders for the errors, arm and phase; `play approach` lowers z at the set speed. With no
engaged arm the panel sits at the anchor sliders.

Colours in the shader are linear (material emissive is linear and gets sRGB-encoded on display), so they are written as sRGB^2.2.
