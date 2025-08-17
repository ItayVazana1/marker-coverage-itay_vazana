# syntax=docker/dockerfile:1

############################
# Builder
############################
FROM ubuntu:22.04 AS build
ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build pkg-config \
    libopencv-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
COPY . .

# Configure & build (Linux finds OpenCV via /usr/lib/cmake/opencv4)
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
RUN cmake --build build

############################
# Runtime (keep it simple/stable)
############################
FROM ubuntu:22.04
ARG DEBIAN_FRONTEND=noninteractive

# For runtime, we could try to copy needed libs, but versioned names vary.
# Easiest & robust: install libopencv-dev here too (adds ~200MB but avoids mismatch).
RUN apt-get update && apt-get install -y --no-install-recommends libopencv-dev \
    && rm -rf /var/lib/apt/lists/*

# Put binaries in PATH
COPY --from=build /work/build/MCE_by_IV /usr/local/bin/MCE_by_IV
COPY --from=build /work/build/marker_coverage /usr/local/bin/marker_coverage

WORKDIR /app
# Default to the TUI. You can override the command to run the CLI.
CMD ["MCE_by_IV"]
