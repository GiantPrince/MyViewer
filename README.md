# Vulkan Real-Time Renderer

A Vulkan-based real-time renderer supporting physically based materials, image-based lighting, dynamic lights and shadows, and rigid-body physics.

The renderer is built around the **S72 scene format** and extends it with a lightweight physics representation for rigid bodies, colliders, and constraints.

## Features

* **Vulkan real-time rendering**
* S72 scene loading and hierarchical scene graphs
* Frustum culling using **AABB + SAT**
* Animated scene nodes
* Lambertian, mirror, and **PBR materials**
* Normal mapping
* HDR environment lighting
* GGX environment pre-filtering and BRDF LUT generation with **Vulkan compute shaders**
* Linear and Reinhard tone mapping with adjustable exposure
* Sun, sphere, and spot lights
* **Real-time spot-light shadows with hardware PCF**
* Headless rendering and GPU profiling
* **Rigid-body physics with Augmented Vertex Block Descent (AVBD)**
* Box/sphere collision detection
* Joint and frictional constraints

---

## Usage

The renderer loads an `.s72` scene and its referenced `.b72` and texture files:

```bash
./viewer --scene path/to/scene.s72
```

Useful options:

```text
--scene <scene.s72>          Load a scene
--camera <name>              Start with a named scene camera
--physical-device <name>    Select a Vulkan physical device
--drawing-size <w h>        Set the initial drawing size
--culling None|frustum      Enable/disable frustum culling
--headless                  Render without a window
--profile                   Profile rendering performance
--indexed                   Enable indexed mesh conversion
--tone-map linear|reinhard  Select tone mapping
--E <exposure>              Set exposure
```

### Camera Controls

* `TAB` — switch between scene and free cameras
* `d` — debug camera
* Mouse wheel — zoom
* Left drag — orbit
* Shift + left drag — pan

---

## Rendering

### Materials

The renderer supports Lambertian diffuse shading and physically based rendering.

The PBR implementation follows the approach presented by Epic Games, using **GGX environment pre-filtering** and a **BRDF lookup table**.

The accompanying cube utility can generate the required precomputed data:

```bash
cube input.png --lambertian output.png
cube input.png --ggx output.png
cube --lut output.lut
```

The GGX pre-filtering and BRDF LUT generation are implemented with Vulkan compute shaders.

### Image-Based Lighting

HDR RGBE environment maps are loaded as cubemaps and used for image-based lighting.

For PBR materials, the renderer uses pre-filtered GGX environment maps together with the BRDF LUT. Lambertian materials use a separately precomputed diffuse environment map.

### Tone Mapping

Supported operators:

```bash
--tone-map linear
--tone-map reinhard
```

Exposure can be adjusted with:

```bash
--E <exposure>
```

The Reinhard operator is:

```text
f(x) = x / (1 + x)
```

---

## Lighting & Shadows

The renderer supports three light types:

* **Sun lights**
* **Sphere lights**
* **Spot lights**

Light information is stored in a GPU storage buffer and evaluated by the material shaders.

Spot-light shadows use a two-pass approach:

1. Render shadow maps.
2. Render the scene using the shadow maps.

Shadow sampling uses `sampler2DShadow` with hardware PCF.

Performance testing showed approximately **linear scaling with the number of lights**, with the renderer handling around **500 lights at a reasonable frame rate** in the tested scene.

---

## Physics

The final extension integrates **Augmented Vertex Block Descent (AVBD)** into the renderer.

The physics system supports:

* Rigid bodies
* Static bodies
* Box and sphere colliders
* Box-box collision detection
* Sphere-sphere collision detection
* Sphere-box collision detection
* Joint constraints
* Frictional constraints

The simulation runs inside the normal frame update and directly updates the transforms used by the renderer.

```text
S72 Scene
   ↓
Rigid Bodies / Colliders
   ↓
Collision Detection
   ↓
AVBD Solver
   ↓
Updated Transforms
   ↓
Vulkan Rendering
```

The current implementation uses a naive **O(n²)** broad-phase collision test, which becomes the main bottleneck as the number of rigid bodies increases.

---

## S72 Extension

The project extends the standard S72 format with three small physics-related concepts:

```text
NODE
 └── rigidbody → RIGIDBODY

RIGIDBODY
 ├── mass
 ├── initial_velocity
 └── is_static

COLLIDER
 ├── box / sphere
 └── offset

JOINTCONSTRAINT
 ├── rigidbodyA / rigidbodyB
 ├── attachment offsets
 ├── stiffness
 └── fracture threshold
```

This keeps physics data inside the scene description instead of requiring a separate physics configuration.

The extension is intentionally small and remains compatible with the existing scene-graph structure.

---

## What's Cool?

### GPU precomputation

The renderer uses Vulkan compute shaders not only for rendering, but also for **PBR environment pre-filtering and BRDF LUT generation**.

### Full SAT frustum culling

Instead of using a simple bounding-sphere approximation, the renderer performs a full **26-axis Separating Axis Test** between mesh bounding boxes and the camera frustum.

### Rendering + Physics in One Scene

The S72 extension allows rendering and physics to share the same scene representation. A scene can describe geometry, materials, lights, animation, rigid bodies, colliders, and constraints in one file.

---

## References

This project builds on the following course infrastructure, libraries, and research:

* **nakluV — CMU Vulkan tutorial / starter code**
  [nakluV](https://github.com/15-472/nakluV?utm_source=chatgpt.com)

* **Scene'72 — S72 scene format**
  [S72 Specification & Examples](https://github.com/15-472/s72?utm_source=chatgpt.com)

* **S72 Loader — S72 → C++ data structures**
  [S72 Loader](https://github.com/15-472/s72-loader?utm_source=chatgpt.com)

* **sejp — JSON parser used by the S72 Loader**
  The official S72 Loader uses `sejp` to parse S72 JSON data.

* **Epic Games — Physically Based Shading**
  The PBR implementation follows Epic's approach to GGX-based physically based shading.

* **Augmented Vertex Block Descent** — Chris Giles, Elie Diaz, Cem Yuksel
  [AVBD Project Page](https://graphics.cs.utah.edu/research/projects/avbd/?utm_source=chatgpt.com)
  [AVBD 3D Reference Implementation](https://github.com/savant117/avbd-demo3d?utm_source=chatgpt.com)

* **Blender** — used for creating custom scene assets and exporting them to S72.

* **Chevrolet Camaro 1969 model** — used as the basis for the lighting demonstration scene, sourced from Free3D.

---

## Limitations

* Displacement mapping is not implemented.
* Some optional lighting extensions were not completed.
* The GPU implementation of AVBD is currently unstable for larger scenes.
* Physics broad-phase collision detection is currently O(n²).
