"""Generate a self-contained cube drop scene; optionally benchmark the viewer."""
import argparse
import json
import math
from pathlib import Path
import re
import statistics
import struct
import subprocess
import time


def node(name, translation, **properties):
    return dict(type="NODE", name=name, translation=translation,
                rotation=[0, 0, 0, 1], scale=[1, 1, 1], children=[], **properties)


def write_cube(path):
    # Six outward-facing faces, using the viewer's 48-byte vertex layout.
    vertices = []
    for axis in range(3):
        for sign in [-1, 1]:
            u, v = (axis + 1) % 3, (axis + 2) % 3
            corners = []
            for x, y in [(-1, -1), (1, -1), (1, 1), (-1, 1)]:
                position = [0., 0., 0.]
                position[axis], position[u], position[v] = sign * .5, x * .5, y * .5
                corners.append(position)
            normal, tangent = [0., 0., 0.], [0., 0., 0.]
            normal[axis], tangent[u] = sign, 1
            indices = [0, 1, 2, 0, 2, 3] if sign > 0 else [0, 2, 1, 0, 3, 2]
            for i in indices:
                vertices.extend(corners[i] + normal + tangent + [1., 0., 0.])
    path.write_bytes(struct.pack("<" + "f" * len(vertices), *vertices))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cubes", type=int, default=1000)
    parser.add_argument("--layers", type=int, default=1)
    parser.add_argument("--size", type=int, nargs=2, default=[1280, 720], metavar=("W", "H"))
    parser.add_argument("--frames", type=int, default=600)
    parser.add_argument("--run", action="store_true")
    parser.add_argument("--indexed", action="store_true")
    parser.add_argument("--debug", action="store_true")
    args = parser.parse_args()
    if not 1 <= args.layers <= args.cubes or args.frames < 600 or min(args.size) < 1:
        parser.error("require cubes >= layers >= 1, frames >= 600, and positive image dimensions")
    out = Path("scenes")
    out.mkdir(exist_ok=True)
    stem = f"avbd-{args.cubes}" + (f"-layers{args.layers}" if args.layers != 1 else "")
    write_cube(out / f"{stem}.b72")
    side = math.ceil(math.sqrt(math.ceil(args.cubes / args.layers)))
    width = side * 1.5 + 4
    roots = ["camera node", "light node", "ground node"] + [f"cube {i}" for i in range(args.cubes)]
    scene = ["s72-v2", dict(type="SCENE", name=stem, roots=roots)]
    scene.append(dict(type="CAMERA", name="camera", perspective=dict(
        aspect=args.size[0] / args.size[1], vfov=math.pi / 3, near=.1, far=2000)))
    camera_distance = max(width, args.layers * 2 + 6)
    camera = node("camera node", [0, camera_distance * .85, camera_distance * .9], camera="camera")
    camera["rotation"] = [-math.sin(math.pi / 8), 0, 0, math.cos(math.pi / 8)]
    scene.append(camera)
    scene.append(dict(type="LIGHT", name="sun", tint=[1, 1, 1], sun=dict(angle=.01, strength=1)))
    light = node("light node", [0, 10, 0], light="sun")
    light["rotation"] = [-math.sin(math.pi / 4), 0, 0, math.cos(math.pi / 4)]
    scene.append(light)
    scene.append(dict(type="MATERIAL", name="white", lambertian=dict(albedo=[.7, .8, 1])))
    attributes = {}
    for name, offset, fmt in [("POSITION", 0, "R32G32B32_SFLOAT"),
                              ("NORMAL", 12, "R32G32B32_SFLOAT"),
                              ("TANGENT", 24, "R32G32B32A32_SFLOAT"),
                              ("TEXCOORD", 40, "R32G32_SFLOAT")]:
        attributes[name] = dict(src=f"{stem}.b72", offset=offset, stride=48, format=fmt)
    scene.append(dict(type="MESH", name="cube mesh", topology="TRIANGLE_LIST",
                      count=36, material="white", attributes=attributes))
    for name, size, static in [("cube", [1, 1, 1], False), ("ground", [width, 1, width], True)]:
        scene.append(dict(type="COLLIDER", name=name + " collider", box=dict(extents=size),
                          offset=dict(translation=[0, 0, 0], rotation=[0, 0, 0, 1])))
        scene.append(dict(type="RIGIDBODY", name=name + " body", mass=1,
                          collider=name + " collider", is_static=static))
    ground = node("ground node", [0, -.5, 0], mesh="cube mesh", rigidbody="ground body")
    ground["scale"] = [width, 1, width]
    scene.append(ground)
    for i in range(args.cubes):
        column, layer = divmod(i, args.layers)
        position = [(column % side - (side - 1) / 2) * 1.5, 3 + layer,
                    (column // side - (side - 1) / 2) * 1.5]
        scene.append(node(f"cube {i}", position, mesh="cube mesh", rigidbody="cube body"))
    path = out / f"{stem}.s72"
    path.write_text(json.dumps(scene), encoding="utf-8")
    print(path, flush=True)
    if not args.run:
        return 0

    events = "AVAILABLE 0.016666667\n" * (args.frames - 1)
    events += f"AVAILABLE 0.016666667 scenes/{stem}-final.png\n"
    command = ["bin/viewer.exe", "--scene", str(path), "--camera", "camera", "--physics", "gpu",
               "--headless", "--debug" if args.debug else "--no-debug", "--drawing-size", *map(str, args.size)]
    if args.indexed:
        command.append("--indexed")
    log_path = out / f"{stem}.log"
    start = time.perf_counter()
    with log_path.open("w") as log:
        result = subprocess.run(command, input=events, text=True, stdout=log, stderr=subprocess.STDOUT)
    elapsed = time.perf_counter() - start
    print(f"cubes={args.cubes} layers={args.layers} frames={args.frames} size={args.size} "
          f"total_seconds={elapsed:.3f} average_fps_including_startup={args.frames / elapsed:.2f} "
          f"exit={result.returncode}")
    logs = log_path.read_text(errors="replace")
    times = [float(x) for x in re.findall(r"REPORT frame-time ([\d.]+)ms", logs)]
    if result.returncode == 0 and len(times) != args.frames:
        raise RuntimeError(f"Expected {args.frames} rendered frames; got {len(times)}. See {log_path}")
    times = times[60:-1]  # Exclude startup and final image encoding from frame percentiles.
    if times:
        print(f"median_frame_ms={statistics.median(times):.3f} "
              f"p95_frame_ms={sorted(times)[int(len(times) * .95)]:.3f}")
    if "Validation Error" in logs:
        raise RuntimeError(f"Vulkan validation failed; see {log_path}")
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
