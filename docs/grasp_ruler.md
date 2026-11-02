# Grasp rulers

World-locked rulers on the table, drawn through the ghost's scene-capture rig:

- one ruler along world x
- one ruler along world y
- a height tape near their corner

The rulers are always there, faint. While the nudge engine's `grasp_ruler` sends a target for an arm, each ruler shows the following:

- **Hatched grey:** the part's footprint on that axis.
- **Gold band:** where on the part a grasp is valid (no finger collision at the pick pose or at the goal pose).
- **Gold / green bar:** the valid grasp nearest to the TCP, ± tolerance. It slides with the gripper and stops at the edge of a blocked region. It turns green when the TCP is inside it on that axis.
- **Cyan marker and triangle:** the TCP.
- **Magnified window:** opens when the TCP is within `near_m` of the target in xy. It is 3× by default, with 2 mm ticks.

On the height tape, the bar is the grasp height ± tolerance and the marker is the TCP height. A green check appears when every axis and the yaw are within tolerance.

The engine decides everything; this side only maps world metres onto the rulers.

## Material: /Game/Materials/M_GraspRuler

The C++ loads exactly this path. One material is used for the rulers and the tape.

1. Content Browser, /Game/Materials: duplicate `M_AlignmentPanel` (or `M_GraspGlyph`) and rename it `M_GraspRuler`. Delete every node except the output node.
2. Details: Blend Mode Translucent, Shading Model Unlit, Two Sided on.
3. Add these nodes:
   - `TexCoord` (index 0)
   - six `Vector Parameter` nodes named exactly `Geo`, `BandA`, `BandB`, `Target`, `Loupe`, `Style`
   - one `Custom` node: Output Type `CMOT Float 4`. Inputs in this order: `UV`, `Geo`, `BandA`, `BandB`, `Target`, `Loupe`, `Style`. Code: the whole of `docs/grasp_ruler_custom_node.hlsl`.
4. Wire TexCoord to UV, and each Vector Parameter's RGBA to the input of the same name. If a node shows no RGBA pin, use `AppendVector(RGB, A)`.
5. Wire the Custom output through `ComponentMask (R G B)` to Emissive Color, and through `ComponentMask (A)` to Opacity.
6. Save.

Preview check: set these parameter defaults, then reset them to 0 afterwards.

- Geo (0.4, 0.31, 0, 0)
- Style (0.6, 1, 0.016, 1)
- BandA (0.10, 0.25, 0, -1)
- BandB (0, -1, 0.05, 0.30)
- Target (0.18, 0.008, 0.21, 1)

You should see a white ruler band with cm ticks, grey hatching from 5 to 30 cm, a gold band from 10 to 25 cm, a gold bar at 18 cm and a cyan marker with a triangle at 21 cm.

## Checking the orientation in VR

The plane mesh's UV layout decides which way the shader reads the quad. `uv_bits` in the config fixes it:

| what you see | fix |
|---|---|
| ticks run across the ruler instead of along it | `uv_bits` += 1 |
| the numbers run the wrong way: a marker at a larger x shows up nearer the ruler's `from` end | `uv_bits` += 2 |
| the triangle and magnifier sit on the workspace side instead of the outer side | `uv_bits` += 4 |

## Config (`overlay*.json`)

```
"grasp_ruler": { "enabled": true, "table_z": 0.70, "band_width_m": 0.016, "loupe_gain": 3.0, "loupe_half_range_m": 0.02,
                 "base_alpha": 0.45, "uv_bits": 0,
                 "x_ruler": { "at_y": -0.36, "from": 0.44, "to": 1.00, "outer": -1 },
                 "y_ruler": { "at_x": 0.42, "from": -0.32, "to": 0.35, "outer": -1 },
                 "tape": { "x": 0.60, "y": -0.335, "height_m": 0.15, "width_m": 0.010 } }
```

- Positions are world metres.
- `outer` is the side (−1 or +1 along the other axis) where the marker triangle and the magnifier go. Point it away from the workspace.
- Keep the rulers outside the area parts can spawn in. Otherwise the overlay is drawn over parts, because the overlay has no occlusion.
- The overlay (`"enabled"` at the top of the file) must be on.

## Wire format

A cue command with `channel: visual` and `primitive: grasp_ruler`. Its `payload`:

```
active: bool            false clears the arm's markers at once (otherwise they fade 0.4 s after the last message)
arm, target, prob
valid: bool             false: the part has no valid grasp; only its footprint is shown
tcp: [x, y, z]          pad centre of the gripper, world m
aim: [x, y, z]          nearest valid grasp (pad centre), world m
tol_xy, tol_z           m
bands_x, bands_y        [[a, b], ...] valid grasp positions projected on each axis, world m (at most 3 per axis)
extent_x, extent_y, extent_z   [a, b] the part's world bounding box
ok: [x, y, z]           TCP within tolerance per axis
yaw_err_deg, yaw_ok, near, ready
```

## Known limits

- **No occlusion:** the robot arm passing over a ruler does not hide it.
- **Approximate gripper model:** the box model of the Franka hand used for the collision checks (`teleop-nudge/configs/tasks/*.yaml`, `gripper`) is approximate. Palm half size is 3 × 10.5 × 3 cm and the fingers are 2.2 cm wide.
