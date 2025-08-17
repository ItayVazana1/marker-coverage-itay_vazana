# Marker Coverage Estimator — **Architecture**

This document describes the system architecture: modules, responsibilities, data flow, concurrency, configuration, and extension points.

---

## 1) Overview

The app is a **terminal UI** (TUI) that batch-processes images to detect a **3×3 color marker** and compute its **coverage** (% of image area). The codebase is split into:
- A **core library** (`mce_core`) with the detection pipeline and shared utilities.
- A **TUI executable** (`MCE_by_IV`) that handles user interaction, batch orchestration, and outputs (CSV + debug overlays).

---

## 2) High-level Components

```mermaid
graph TD
  subgraph Executable (MCE_by_IV)
    MAIN[main.cpp]
    APP[app.cpp<br/>Application runtime & menu loop]
    UI[ui.cpp<br/>Input/Settings/Help/About]
    PROG[progress.cpp<br/>Batch runner & CSV/debug outputs]
    LOG[log.cpp<br/>ANSI colors & logging helpers]
  end

  subgraph Library (mce_core)
    DETECT[detect_and_compute.cpp<br/>mce::detect_and_compute]
  end

  MAIN --> APP
  APP --> UI
  APP --> PROG
  PROG --> DETECT
  UI --> PROG
  LOG --> APP
  LOG --> UI
  LOG --> PROG
```

**Responsibilities**

| Module | Responsibility |
|---|---|
| `main.cpp` | Entry point; enables ANSI on Windows; instantiates and runs `app::Application`. |
| `app.cpp` | Main loop, state storage (input path, flags), dispatch to UI and batch runner. |
| `ui.cpp` | Console UI: read input path (file/folder), settings toggles, help/about, path validation. |
| `progress.cpp` | Recursively collect images, manage output roots, create timestamped result/debug folders, write CSV header/rows, per-image telemetry. |
| `detect_and_compute.cpp` | Full CV pipeline: HSV masking → component → angle sweep → warp → validators → coverage; optional debug overlay writers. |
| `log.cpp` | Colored logs, info/debug/warn helpers, minor utilities for console formatting. |

---

## 3) Runtime Data Flow

```mermaid
sequenceDiagram
  participant User
  participant UI as ui.cpp
  participant APP as app.cpp
  participant PROG as progress.cpp
  participant DET as detect_and_compute.cpp
  participant CSV as results.csv/debug/

  User->>UI: Enter input path & settings
  UI->>APP: Update ApplicationState
  User->>APP: Run
  APP->>PROG: process_and_report(images, state)
  PROG->>PROG: Resolve input (file/folder) & build image list
  loop For each image
    PROG->>DET: detect_and_compute(bgr, out, debug, saveDebug, debugBase)
    DET-->>PROG: {found, percent, angle, occ, hue_score, line_ok, elapsed_ms, S/V, debug paths}
    PROG->>CSV: Append CSV row; write debug overlays if enabled
    PROG-->>APP: Print one-line telemetry
  end
  APP-->>User: Summary & output locations
```

**State**  
`ApplicationState` holds user input and flags: `inputPath`, `isDirectory`, `debug`, `saveDebug`, resolved `outputRoot`, and platform-specific host mapping state (for Docker/Windows to container paths).

**Per-image output (struct-like)**  
- `found` (bool), `percent` (double), `angle_deg` (double)  
- `occupancy`, `hue_score` (double heuristics), `line_ok` (bool)  
- `elapsed_ms` (timing), `Smin`, `Vmin`, `Vmax` (effective HSV thresholds)  
- `debug_quad/warp/mask/crop/clip` (filenames when `saveDebug=true`)

---

## 4) Core Detection Architecture (mce::detect_and_compute)

```mermaid
flowchart LR
A[BGR input] --> B[HSV adaptive mask<br/>percentile clamps + morphology]
B --> C[Largest component<br/>area/compactness gates]
C --> D[minAreaRect<br/>base angle, base box]
D --> E[Angle sweep (coarse→fine)<br/>OpenMP candidates + tightened ROI]
E --> F[Perspective warp → square]
F --> G[Validation cascade (5 paths)<br/>hue richness + grid/line checks]
G --> H{Valid?}
H -- yes --> I[Compute coverage % + choose best]
H -- no --> J[Fallback: warp from base rect → re-validate]
I --> K[Emit telemetry + debug overlays]
J --> K
```

**Key internal concepts**
- **Adaptive HSV**: Compute `Smin`, `Vmin`, `Vmax` from image percentiles; combine multi-band hue masks; morphology kernel scales with image size.
- **Largest component**: Reject tiny/noisy blobs by relative area; measure `compFrac` window.
- **Orientation**: `cv::minAreaRect` gives orientation prior for the sweep.
- **Angle sweep**: Explore candidate rotations around the base angle (coarse then fine). Tighten rectangular ROI; apply occupancy & aspect constraints. The sweep may run **in parallel (OpenMP)**.
- **Warp**: Perspective transform to a normalized square (e.g., ~360×360) for grid checks.
- **Validation cascade**: Richness-of-hue check + multiple line/grid validators (edge projections at thirds, peak prominence/separation, Sobel/gradient options, k-means on `[Hcos,Hsin,S,V]` subsamples, or lightweight template correlation). Early-exit on first strong pass.
- **Coverage**: `100 × area(candidate_rect) / image_area` (clamped/rounded for display).

**Artifacts** (when enabled):  
`*_debug_mask.png`, `*_debug_quad.png`, `*_debug_warp.png`, `*_debug_crop.png`, `*_debug_clip.png`

---

## 5) Build & Targets

- **Targets**
  - `mce_core` (static lib): detector + helpers.
  - `MCE_by_IV` (executable): app loop, UI, batch runner, logging.
- **Dependencies**: C++17, CMake, OpenCV (`core`, `imgproc`, `imgcodecs`). Ninja/Make/VS supported generators.
- **Optional**: Docker/Compose environment that mounts the host drive and runs the TUI inside the container.

---

## 6) Configuration & Environment

- **Runtime flags** (via TUI): `Debug logs`, `Save debug overlays`.
- **Environment variables**:
  - `MCE_OUTPUT_ROOT` — override default `./mce_output/` root for results & debug artifacts.
  - `MCE_HOST_ROOT` — base for host path mapping (e.g., `/host`); allows pasting `C:\...` in containerized runs (auto-mapped to `/host/c/...`).

- **Tunables** (in detector `Params`):
  - HSV clamps (`Smin/Vmin/Vmax` floors/ceilings), morphology kernel divisors
  - Component gates: min/max component fraction
  - Angle sweep: coarse/fine step & ranges
  - Tightened-ROI constraints: min occupancy, max aspect
  - Warp and validators: warp size, peaks/thirds tolerances, hue thresholds
  - Safety bounds: max quad area fraction

---

## 7) Concurrency & Performance

- **Parallel angle sweep** using **OpenMP** when available; thread-local candidate scoring with best‑of merge.
- **Early-stop** once a sufficiently strong candidate is found (high occupancy + hue + line_ok).
- **I/O efficiency**: debug overlay writing is optional; disabling it increases throughput for large batches.
- **Resolution guidance**: moderate input sizes (~720p) balance speed and robustness.

---

## 8) Logging & Error Handling

- **log.cpp** provides colored INFO/DBG/WARN helpers with clear prefixes and concise, actionable messages.
- Input validation and path existence checks happen in the UI/batch layer; empty or unreadable images are logged and skipped with a CSV entry (found=0).
- Detector returns a structured result; the batch runner never crashes the app on per-image failures.

---

## 9) I/O & Paths

- **Inputs**: single image or folder (recursive scan of `.png/.jpg/.jpeg`, case-insensitive).
- **Outputs**:
  - `results/<timestamp>.csv`
  - `debug/<timestamp>/...` (only when `Save debug overlays` is enabled)
- **Path mapping for Docker on Windows**: paste `C:\...` in the TUI; it is mapped to `/host/c/...` inside the container when `MCE_HOST_ROOT=/host` is set.

---

## 10) Extension Points

- **Add a new validator**: implement a function that scores a warped patch; add it to the validator cascade and combine in the selection logic.
- **Support a new input format**: extend the file collection filter & OpenCV `imread` flags as needed.
- **Alternative UI**: the `progress` and `detect_and_compute` APIs are decoupled; you can replace `ui.cpp` with a GUI/CLI wrapper.
- **New outputs**: extend `progress.cpp` to write additional telemetry (JSON, metrics endpoints) without touching the detector.

---

## 11) Security & Privacy

- All processing is **local**; no network calls or external services.
- Docker runs mount only the paths you explicitly share; outputs go to a known root (`MCE_OUTPUT_ROOT`).

---

## 12) Folder Layout (key)

```
/src
  main.cpp                 # entrypoint
  app.cpp                  # Application loop
  ui.cpp                   # TUI and input/settings/help/about
  progress.cpp             # batch runner, CSV/debug, timing
  detect_and_compute.cpp   # core detector (mce::detect_and_compute)
  log.cpp                  # logging helpers
CMakeLists.txt             # targets and dependencies
compose.yml, Dockerfile    # optional container build/run
```

---

## 13) Design Rationale (short)

- **Separation of concerns**: UI and batch orchestration are isolated from the CV algorithm to keep the detector testable and reusable.
- **Deterministic pipeline** with adaptive thresholds to handle varied lighting while bounding search space via component gating and a guided angle sweep.
- **Performance-aware**: early exits, parallelism, and optional artifacts to control I/O.
- **Portability**: minimal dependencies (OpenCV) with a clean CMake setup; Docker/Compose for reproducibility.
