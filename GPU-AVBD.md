# GPU AVBD: running and validation

Build from the repository root with **`node Maekfile.js`**. Select the GPU backend
with `--physics gpu`; `--physics cpu` remains the default reference implementation.

```powershell
node Maekfile.js
python avbd-scene.py --cubes 1000 --layers 5 --run
.\bin\viewer.exe --scene scenes/avbd-1000-layers5.s72 --camera camera --physics gpu --no-debug --drawing-size 1280 720
```

`avbd-scene.py` needs only the Python standard library. It generates its own mesh,
materials, ground, and cube scene. `--run` renders 600 frames, saves the final image
and a log in `scenes/`, and reports timing. Use `--cubes 10000` or `--cubes 100000`
to scale up. `--layers` controls stacked cubes per column; the default is a single
layer dropped from height 3. `--size W H` defaults to 1280x720. `--indexed --debug`
exercises indexed instancing with Vulkan validation enabled. Collider outlines are
available in the viewer with `--show-colliders`.

## Measurements on Intel Iris Xe

These are headless Vulkan measurements on the GPU available in this workspace,
using one directional light and a shared cube mesh. Counts exclude the static
ground. Frame timing includes physics, readback, scene traversal, and rendering.
No CPU reference solver runs alongside these rendering measurements.

| Cubes | Layout | Resolution | Average FPS including startup | Median frame | 95th percentile |
|---:|---|---|---:|---:|---:|
| 1,000 | Five-layer stacks | 1280x720 | 83.69 | 5.636 ms | 6.540 ms |
| 10,000 | Single layer | 1280x720 | 53.91 | 10.188 ms | 17.950 ms |
| 100,000 | Single layer | 640x360 | 7.77 | 104.956 ms | 193.631 ms |

Each run rendered 600 frames. Frame percentiles omit the first 60 frames and final
image encoding; average FPS includes startup and encoding. Sleeping is enabled
in the viewer. The 1,000-cube stacked scene stays awake because sleeping is limited
to isolated bodies supported by static geometry.

**100,000 actively simulated cubes are not real-time on this device.** With sleeping
disabled, physics alone measured 102.978 ms per step for 100,000 cubes and 10.258 ms
for 10,000 cubes. Scene traversal and transform upload are additional costs at
100,000 objects. Use the 1,000-cube stacked case for a comfortably real-time example;
10,000 isolated cubes are usable but do not maintain a locked 60 FPS throughout.
These measurements do not establish performance on other GPUs or arbitrary piles.

## Correctness checks

The renderer timing script does not replace numeric physics validation. The
standalone benchmark runs the actual GPU solver without rendering, and can run the
unchanged CPU reference on the same initial bodies. It requires at least 600 steps:
**10 simulated seconds at 60 Hz**, including falling, impact, and settling.

```powershell
.\bin\avbd-benchmark.exe --cubes 1 --frames 600 --compare --debug
.\bin\avbd-benchmark.exe --cubes 1000 --layers 5 --frames 600 --compare --no-sleep
.\bin\avbd-benchmark.exe --cubes 125 --tilt 0.2 --frames 600 --compare --no-sleep
.\bin\avbd-benchmark.exe --wake --frames 600 --compare
.\bin\avbd-benchmark.exe --cubes 10000 --frames 600 --no-sleep
.\bin\avbd-benchmark.exe --cubes 100000 --frames 600 --no-sleep
```

All these checks passed. The benchmark rejects nonfinite state, missing retained
contacts, excessive ground overlap, and persistent late motion. CPU comparison
also checks final positional agreement; a single cube has a tighter trajectory
check. The wake case requires observing both sleep and wake, and allows the
expected continued sliding of frictionless bodies. CPU comparisons are limited
to 1,000 cubes because the reference broad phase is quadratic.

Observed results:

- 100,000 awake cubes: minimum cube bottom -0.009997, late maximum speed 0.000009,
  and 222,800,000 retained-contact observations over 600 steps.
- 1,000 cubes in five-layer stacks: minimum bottom -0.010856, late speed 0.003262,
  final maximum CPU position difference 0.000713. The maximum difference during
  impact was 0.067688 because graph coloring changes the body solve order.
- 125 tilted cubes: maximum CPU position difference 0.000015. Both backends reached
  about -0.099 during the initial tilted impact and settled near -0.011. In comparison
  mode the transient penetration limit accounts for the measured CPU reference;
  the late overlap and motion checks still apply.
- The frictionless wake test observed a sleeping cube wake on dynamic contact,
  with final CPU position difference 0.005700.
- Single-cube physics and the indexed renderer completed validation-layer runs
  without validation errors.

Small contact overlap is part of the CPU algorithm (its collision margin is 0.01).
These results establish matching contact behavior and convergence for the tested
scenes, not bitwise identical trajectories or continuous collision detection.
Very fast motion can miss contacts in this discrete algorithm, including on the
CPU reference.

## Implementation and limits

- GPU bodies and contact history persist on the device. Stable pair slots retain
  feature IDs, sticking anchors, multipliers, and penalties across steps, applying
  the CPU warm-start decay and clamping rules.
- A spatial hash replaces all-pairs discovery; static geometry is checked separately
  so a large ground box does not determine the dynamic grid cell size.
- Both endpoints receive contact adjacency. Graph coloring produces independent
  body groups for in-place Gauss-Seidel updates; indirect dispatch skips empty
  groups. The default remains ten primal/dual iterations per 1/60-second step.
- Axis-aligned box faces use exact rectangle intersection. This avoids cancellation
  from clipping a huge ground polygon down to a small cube. Rotated boxes use the
  general SAT/clipping path. Separate feature IDs prevent reusing mismatched anchors
  when changing collision paths.
- Conservative sleeping requires negligible velocity, contact gaps and force
  residuals, multiple support contacts, and no dynamic neighbors. Dynamic candidate
  overlap wakes both endpoints before narrow phase. Contact history remains stored
  while asleep. Stacks are not put to sleep.
- The renderer batches consecutive compatible meshes/materials into instanced draws,
  caches immutable mesh setup, and reuses traversal storage. Host readback completes
  before CPU transforms are published to the renderer.
- The GPU backend currently supports contacts, not joints. Joint scenes must use
  `--physics cpu`. GPU state is authoritative after construction; recreate the GPU
  solver when topology or simulation parameters change. The viewer reset does this.
- Default capacity is eight owned pair slots per body, 64 candidates per body,
  and 32 colors/rounds. Capacity failures throw explicitly instead of publishing
  corrupt state. Pair slots alone reserve roughly 576 MB for 100,000 cubes;
  device storage-buffer limits are checked before allocation. Many static bodies,
  highly unequal body sizes, or dense contact graphs can cost more or exceed these
  capacities.
