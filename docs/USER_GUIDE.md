# Marker Coverage Estimator — User Guide

This guide explains how to **install**, **run**, and **interpret results** for the Marker Coverage Estimator (TUI). The app scans images for a 3×3 color marker, then reports the marker’s **coverage** as a percentage of the image area. It can also process folders in **batch** and generate **CSV** results plus optional **debug overlays**.

---

## 1) What you get

- **Terminal UI (TUI)**: a simple screen to point at a file or folder, toggle options, and run.
- **Outputs**:
  - `results/<YYYYMMDD-HHMMSS>.csv` — per-image results (coverage and telemetry).
  - `debug/<YYYYMMDD-HHMMSS>/` — optional overlays (quad/warp/mask/crop/clip) to help you debug detection.
- **Supported inputs**: `.png`, `.jpg`, `.jpeg` (case-insensitive).

> **Privacy**: everything runs locally on your machine or Docker container. No files are uploaded anywhere.

---

## 2) System requirements

- **OS**: Windows 10/11, macOS 12+, or modern Linux.
- **C++ toolchain**: MSVC (Windows), Clang/AppleClang (macOS), or GCC/Clang (Linux).
- **CMake**: ≥ 3.15.
- **OpenCV**: built with `core`, `imgproc`, `imgcodecs`.
- **(Optional) Docker** and Docker Compose for a fully contained build/run.

---

## 3) Download the source code

Choose one:

- **Clone the repo** (recommended):
  ```bash
  git clone <repo-url>
  cd <repo-directory>
  ```
- **Download ZIP** from your Git hosting and extract it, then `cd` into the project directory.

> Throughout this guide we assume you are inside the project directory.

---

## 4) Local installation (no Docker)

### 4.1 Windows (MSVC / Visual Studio)

1. Install **CMake** and **OpenCV** (prebuilt or package manager).  
   If CMake cannot find OpenCV automatically, pass the directory explicitly:
   ```powershell
   cmake -S . -B build -G "Visual Studio 17 2022" -DOpenCV_DIR="C:\path\to\opencv\build"
   ```

2. Configure & build (Release):
   ```powershell
   cmake -S . -B build -G "Visual Studio 17 2022"
   cmake --build build --config Release
   ```

3. The executable will be in `build\Release\` (Visual Studio) or in `build\` (single-config generators).

> **Tip (MSYS2/UCRT64)**: If you use MSYS2 with the UCRT64 environment, ensure `OpenCV` is installed there and let CMake pick it up. If needed, pass `-DOpenCV_DIR=...` similarly.

---

### 4.2 macOS (AppleClang + Homebrew)

1. Install tooling:
   ```bash
   brew install cmake opencv ninja
   ```

2. Configure & build (Release):
   ```bash
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   ```

3. The binary will be at `build/MCE_by_IV` (or inside `build/` for your generator).

> If CMake cannot locate OpenCV, add `-DOpenCV_DIR="$(brew --prefix)/opt/opencv"` when configuring.

---

### 4.3 Linux (GCC/Clang)

1. Install tooling & OpenCV (Debian/Ubuntu example):
   ```bash
   sudo apt-get update
   sudo apt-get install -y cmake g++ libopencv-dev ninja-build
   ```

2. Configure & build (Release):
   ```bash
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   ```

3. The binary will be at `build/MCE_by_IV` (or your generator’s output path).

---

## 5) Run locally (no Docker)

1. Open a terminal in the project directory.
2. Launch the TUI binary:
   - **Windows (VS)**: `.\build\Release\MCE_by_IV.exe`
   - **Windows (Ninja/Make)**: `.\build\MCE_by_IV.exe`
   - **macOS/Linux**: `./build/MCE_by_IV`

3. In the TUI:
   - **Input**: paste a file path or a folder path containing images.
     - Windows example: `C:\Users\You\Pictures\marker.jpg`
     - macOS/Linux example: `/Users/you/Pictures/marker.jpg`
   - **Options**:
     - **Debug logs** — prints more details to the console.
     - **Save debug overlays** — writes visualization images (quad/warp/mask/crop/clip) for each processed input.
   - Press **Run**.

4. Check outputs (default root: `./mce_output/`):
   - CSV → `mce_output/results/<YYYYMMDD-HHMMSS>.csv`
   - Debug images → `mce_output/debug/<YYYYMMDD-HHMMSS>/`

> **Change output location**: set the environment variable `MCE_OUTPUT_ROOT` to an absolute path before launching the app.

---

## 6) Using Docker / Docker Compose

### 6.1 Docker Compose (recommended)

1. Ensure **Docker Desktop** (or Docker Engine) and **Docker Compose** are installed.
2. From the project directory, run:
   ```bash
   docker compose up --build
   ```
3. The container maps your **Windows C:\\** drive to **/host/c** in the container.  
   Examples you can paste inside the TUI (running in the container):
   - `C:\Users\You\Pictures\marker.jpg`  → the app will auto-map it to `/host/c/Users/You/Pictures/marker.jpg`.
   - `C:\Users\You\Pictures` (folder) → mapped to `/host/c/Users/You/Pictures`.

4. Outputs are written to a mounted path so they persist on the host:
   - CSV: `mce_output/results/<timestamp>.csv`
   - Debug: `mce_output/debug/<timestamp>/`

> To change the output root for Docker runs, set an environment variable:
> ```bash
> # one-off
> docker compose run -e MCE_OUTPUT_ROOT=/host/c/Users/You/Desktop/out mce
> ```
> Or put `MCE_OUTPUT_ROOT=/host/c/Users/You/Desktop/out` in a `.env` file next to `compose.yml`.

---

### 6.2 Plain Docker (without Compose)

```bash
# Build
docker build -t marker-coverage .

# Run an interactive session with your host folder mounted (Linux/macOS example)
docker run --rm -it \
  -v "$PWD":/work \
  -v "/Users/you":/host/home \
  -e MCE_OUTPUT_ROOT=/work/mce_output \
  marker-coverage
```
Then run the TUI **inside** the container. On Windows, map your **C:\\** to `/host/c` and paste Windows paths in the TUI; the app will map them.

---

## 7) Interpreting results

### 7.1 CSV columns (typical)
- **index** — running number for the batch.
- **input_path** — absolute or mapped file path processed.
- **found** — `1` if a marker was detected, `0` otherwise.
- **percent** — area coverage of the marker relative to the whole image, 0–100% (float).
- **angle_deg** — best orientation estimate for the marker in degrees.
- **occupancy** — fraction of relevant pixels inside the candidate quad (heuristic).
- **hue_score** — relative strength/consistency of expected hues (heuristic).
- **line_ok** — `1` if expected grid lines/peaks validated, else `0`.
- **elapsed_ms** — processing time for this image.
- **debug_* columns** — file names (if enabled) for overlays: `debug_quad`, `debug_warp`, `debug_mask`, `debug_crop`, `debug_clip`.

> Note: exact column order may evolve; your CSV header lists the definitive order for the build you ran.

### 7.2 Debug overlays
- **Quad** — detected quadrilateral and orientation.
- **Warp** — perspective-corrected ROI used for 3×3 checks.
- **Mask** — thresholded/filtered mask used during detection.
- **Crop** — the candidate region extracted for analysis.
- **Clip** — any intermediate visualization used by validators.

---

## 8) Tips for best results

- Prefer images with the marker reasonably centered and not extremely tiny (<1% of the image).
- Avoid heavy glare, severe motion blur, or extreme low-light.
- If the marker is very large or very small, try a second shot or crop closer to the marker to help the detector.
- When performance matters, disable **Save debug overlays** and process moderate-resolution images (e.g., ~720p).

---

## 9) Troubleshooting

- **Build cannot find OpenCV**  
  Pass the install location to CMake:  
  - Windows: `-DOpenCV_DIR="C:\path\to\opencv\build"`  
  - macOS: `-DOpenCV_DIR="$(brew --prefix)/opt/opencv"`  
  - Linux: ensure `libopencv-dev` (or equivalent) is installed.

- **“Invalid path” or nothing happens after Run**  
  Double‑check the path you pasted. On Docker/Compose, remember that Windows paths are mapped to `/host/c/...` inside the container (the app auto-maps `C:\...` for you).

- **“No marker found” for images you expect to pass**  
  Enable **Save debug overlays** and inspect the quad/warp/mask. Improve lighting, reduce tilt, or try a closer crop.

- **Docker can’t see your C: drive**  
  In Docker Desktop → Settings → Resources → File Sharing, ensure the `C:` drive is shared.

- **CSV is empty**  
  Ensure the input folder actually contains `.png/.jpg/.jpeg` files (case-insensitive).

---

## 10) Uninstall / Clean build

- **Local build**: delete the `build/` directory and `mce_output/` if you want to clear outputs.
- **Docker**: `docker compose down` (stops containers). You can also remove the built image with `docker rmi marker-coverage` (if you used the plain Docker build).

---

## 11) Appendix — Environment variables

- **`MCE_OUTPUT_ROOT`** — set a custom absolute path for the results/debug root folder. Works both locally and in Docker.  
  Example:  
  ```bash
  # macOS/Linux
  export MCE_OUTPUT_ROOT="$HOME/Desktop/mce_out"
  ./build/MCE_by_IV
  ```
  ```powershell
  # Windows PowerShell
  $env:MCE_OUTPUT_ROOT="C:\Users\You\Desktop\mce_out"
  .\build\Release\MCE_by_IV.exe
  ```

---

## 12) About

**Marker Coverage Estimator (TUI)** — C++17, CMake, OpenCV; optional Docker/Compose workflow for reproducible builds.  
**Author**: *Itay Vazana* — Computer Science student & junior Network Engineer.

> For best colors in the TUI, set `TERM=xterm-256color` in your terminal environment.
