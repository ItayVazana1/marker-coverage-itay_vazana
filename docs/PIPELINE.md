# Marker Coverage Estimator — **Pipeline**

This document describes the full **runtime pipeline** of the project, from entrypoint and TUI flow to the detection algorithm internals, batch processing, outputs, and tunables.

---

## 0) High‑level run flow

```mermaid
flowchart LR
A[main.cpp] --> B[app::Application::run → main_loop]
B --> C[ui: input, settings, help, about]
C -->|Run| D[Collect images (file/folder)]
D --> E[progress::process_and_report]
E --> F[mce::detect_and_compute (per image)]
F --> G[Console telemetry & CSV row]
G --> H[Optional debug overlays (mask/quad/warp/crop/clip)]
```
- Entry enables ANSI colors and launches the app loop. fileciteturn5file0  
- The **Application** menu delegates to **ui** (input/settings/help/about) and to the batch runner. fileciteturn6file3  
- Batch runner manages paths, CSV/dirs, and calls the **unified detector** per image. fileciteturn6file14  
- Detector runs the full CV pipeline and returns coverage + telemetry. fileciteturn6file11

---

## 1) UI flow (TUI)

- **Input**: path is read, optionally mapped from Windows `C:\...` to a container mount (`/host/c/...`) via `MCE_HOST_ROOT`, and validated. fileciteturn6file16  
- **Settings**: toggle `Debug logs` and `Save debug overlays`. fileciteturn6file15  
- **Collect images**: single file or recursive folder scan for `.png/.jpg/.jpeg`. fileciteturn6file15

```text
Menu: 1) Input  2) Settings  3) Help  4) About  5) Run  0) Exit
```

---

## 2) Batch runner & outputs

**progress::process_and_report(images, state)** does the per‑image loop:

1. Create outputs under `mce_output/` (override with `MCE_OUTPUT_ROOT`), timestamp subfolder, and open a CSV. fileciteturn6file14  
2. CSV header includes telemetry and paths to all debug artifacts (mask/quad/warp/crop/clip). fileciteturn6file2  
3. For each image: read, build a `debugBase`, call `mce::detect_and_compute`. fileciteturn6file10  
4. Print a **one‑line telemetry**: `path  XX%  (angle=..°, occ=.., hue=.., line=ok/no) [ms]`. fileciteturn6file10  
5. Append a CSV row with coverage, angle, occupancy, hue_score, line_ok, elapsed_ms, S/V thresholds, and debug file paths. fileciteturn6file10

---

## 3) Detection pipeline (per image)

**mce::detect_and_compute(bgr, out, debug, saveDebug, debugBase)** returns coverage and optional artifact paths. The stages are:

### 3.1 Adaptive color mask (HSV)
- Convert to HSV; compute **percentile‑based** clamps for `Smin` (≈85th−10), `Vmin` (≈60th), `Vmax` (≈99th). fileciteturn6file1  
- OR-combine several **hue bands** (red wrap, yellow, green, cyan, blue, magenta). Morphological **close+open** with size ∝ image. fileciteturn6file1  
- Save `*_debug_mask.png` if enabled; also export S/V thresholds to `out`. fileciteturn6file11

### 3.2 Largest connected component
- Keep the best component by area/compactness; drop tiny/noisy blobs. Compute `compFrac` and reject if out of `[min_comp_frac, max_comp_frac]`. fileciteturn6file11

### 3.3 Base orientation
- Contour → `cv::minAreaRect` → **base angle** and base area fraction (logged for diagnostics). fileciteturn6file11

### 3.4 Coarse→fine angle sweep (OpenMP) + ROI tighten
- Around the base angle, evaluate candidates on a discrete grid (coarse then fine). Occupancy and aspect ratio constraints are enforced on a **tightened rectangle** inside a rotated ROI. fileciteturn6file1  
- Crop a padded ROI around the candidate, compute a perspective **warp** to a square (`warpSize≈360`). fileciteturn6file11

### 3.5 5‑path grid validation cascade
Given the warped patch, compute a **hue richness** score once; then run validators until one passes (early‑exit). fileciteturn6file1  
Validators include:
1. Line‑peaks with CLAHE + projections with **two‑peaks prominence** near 1/3 and 2/3.  
2. Color‑gradient + Sobel (hue on unit circle + V).  
3. Max‑gap two‑cuts (select two cuts maximizing edge profiles, check 1/3 and 2/3).  
4. KMeans (K=6) on `[Hcos,Hsin,S,V]` subsample; check label transitions near thirds.  
5. Template correlation against an ideal 3×3 grid edge map. fileciteturn6file1

Each validator contributes a boolean `line_ok`. Together with **hue_score**, a weighted score selects the best candidate. fileciteturn6file11

### 3.6 Early‑stop and fallback
- **Early‑stop** if a strong candidate is found (`occ > 0.78`, `hue > 0.85`, `line_ok`). fileciteturn6file11  
- If no angle passes, **fallback**: warp directly from the base `minAreaRect`, re‑validate; if OK, compute coverage. fileciteturn6file11

### 3.7 Coverage, telemetry, and artifacts
- Coverage = `100 × area(candidate_rect) / image_area`, clamped to 0–100 and rounded for display. fileciteturn6file11  
- Write artifacts when enabled:  
  `*_debug_quad.png` (quad + “Coverage: XX%” overlay), `*_debug_warp.png` (square warp),  
  `*_debug_crop.png` (natural‑size perspective‑corrected patch), `*_debug_clip.png` (clipped polygon on original). fileciteturn6file11

---

## 4) Tunable parameters (selected)

| Group | Key params |
|---|---|
| HSV clamps | `Smin_floor/ceil`, `Vmin_floor/ceil`, `Vmax_floor/ceil` |
| Morphology | `close_div`, `open_div` (kernel scales with min(H,W)) |
| Component gating | `min_comp_frac`, `max_comp_frac` |
| Angle scan | `coarse_step/range`, `fine_step/range` |
| Tight box | `min_occupancy`, `max_aspect` |
| Warped checks | `warpSize`, `min_hue_score`, `min_line_peak`, `min_peak_sep`, `thirds_tol` |
| Area bound | `max_quad_area_frac` |
All parameters are defined in a single `Params` struct at the top of the detector. fileciteturn6file1

---

## 5) Environment variables

- **`MCE_OUTPUT_ROOT`**: change where `results/` and `debug/` trees are created. fileciteturn6file14  
- **`MCE_HOST_ROOT`**: when set (e.g., `/host` in Docker Compose), Windows paths pasted into the TUI (like `C:\Users\...`) are auto‑mapped to the container (`/host/c/Users/...`). fileciteturn6file16

---

## 6) Entrypoint and control loop

- **main.cpp** enables virtual terminal on Windows and runs the **Application**. fileciteturn5file0  
- **Application::main_loop**: shows the menu, dispatches UI handlers, and on **Run** collects images then calls the batch runner. fileciteturn6file3

---

## 7) Outputs summary

- **Console**: per‑image line with coverage and telemetry; timing per image + summary at the end. fileciteturn6file10  
- **CSV** (`mce_output/results/<YYYYMMDD-HHMMSS>.csv`): header + one row per image with `found, percent, angle_deg, occupancy, hue_score, line_ok, elapsed_ms, Smin, Vmin, Vmax` and debug paths. fileciteturn6file2  
- **Debug images**: organized under `mce_output/debug/<timestamp>/`. fileciteturn6file2

---

## 8) Problem framing (from brief)

Your solution targets a **3×3 colored patch marker**; must support PNG/JPEG and be robust to rotation/tilt/blur/moderate color shift; documentation and optional Docker are expected. fileciteturn6file6

---

## 9) Reference pseudocode

```cpp
// main.cpp
int main() {
  mce::ansi::enable_virtual_terminal_on_windows();
  app::Application app;
  return app.run();
}

// app::Application
loop {
  ui::main_menu(state);
  switch (ui::read_menu_choice()) {
    case INPUT: ui::input(state); break;
    case SETTINGS: ui::settings(state); break;
    case RUN: {
      auto imgs = ui::collect_images(state.inputPath, state.isDirectory);
      progress::process_and_report(imgs, state);
      break;
    }
    case EXIT: return 0;
  }
}
```

```cpp
// progress::process_and_report
csv.open(resultsDir/timestamp.csv); print header;
for image in images:
  img = imread(image);
  if empty: log & write empty row; continue;
  debugBase = debugDir/(index_name);
  ok = mce::detect_and_compute(img, out, state.debug, state.saveDebug, debugBase);
  log telemetry; write CSV row (incl. debug paths & S/V thresholds);
print summary stats;
```

```cpp
// mce::detect_and_compute
mask = adaptiveHSV(bgr); save mask;
comp = largest_component(mask); gate by compFrac;
rr = minAreaRect(comp); baseAngle = rr.angle;
best = scan around baseAngle (coarse→fine, OpenMP):
  tighten ROI under rotation; if occ/aspect ok:
    warp to square; hue_score + 5-path grid validators;
    if valid: compute coverage & score; keep best; early-stop if strong;
if none valid: fallback warp from rr and re-validate;
emit out {percent, best_angle_deg, occupancy, hue_score, line_ok, debug_* paths};
```

---

## 10) Notes

- OpenMP is used when available to parallelize the angle sweep. fileciteturn6file1  
- Overlay rendering draws the quad and “Coverage: XX%” text into `*_debug_quad.png`. fileciteturn6file11
