# LUXR for Ableton Move: the aarch64 cross compiler (schwung/scripts/build.sh uses it when the host has none)
FROM ubuntu:22.04
RUN apt-get update && apt-get install -y --no-install-recommends gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu \
    && rm -rf /var/lib/apt/lists/*
ENV CROSS_PREFIX=aarch64-linux-gnu-
WORKDIR /build
