# syntax=docker/dockerfile:1

FROM ubuntu:22.04 AS build
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build pkg-config libopencv-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
COPY . .

# 🔧 ensure a fresh out-of-source build directory
RUN rm -rf build CMakeFiles CMakeCache.txt

RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
RUN cmake --build build

FROM ubuntu:22.04
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends libopencv-dev \
    && rm -rf /var/lib/apt/lists/*
ENV TERM=xterm-256color

COPY --from=build /work/build/MCE_by_IV /usr/local/bin/MCE_by_IV
WORKDIR /app
CMD ["MCE_by_IV"]
