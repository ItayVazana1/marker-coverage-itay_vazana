# Marker Coverage Estimator (TUI)

Fast, cross-platform C++17/OpenCV TUI that finds a **3×3 color marker** in images and reports its **coverage %**. Built for batch runs, with clean CSV output and visual debug overlays.

**Why you’ll like it:**

- 🚀 **Snappy & robust**: adaptive HSV + smart angle sweep with sensible fallbacks.
- 🧠 **Clear telemetry**: coverage, angle, occupancy, hue score, line check.
- 🧪 **Easy debugging**: optional overlays—mask, quad, warp, crop, clip.
- 🐳 **Container-friendly**: Docker/Compose with host path mapping.

**Build & run (local):** CMake + OpenCV (`core`, `imgproc`, `imgcodecs`), then run the `MCE_by_IV` binary.  
**Build & run (Docker):** `docker compose up --build` and paste Windows paths directly—auto-mapped inside the container.

**Outputs:**

- 📄 **CSV** with per-image results in `mce_output/results/<timestamp>.csv`
- 🖼️ **Debug images** (optional) in `mce_output/debug/<timestamp>/`

**Docs (start here!)** → See [docs/](./docs/)

- 📘 **User Guide** – install, run, tips
- 🔧 **Pipeline** – end-to-end processing flow
- 🏗️ **Architecture** – modules, data flow, extension points

Have fun! ✨
