# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Hany El Imam - 1KO125 firmware for the Xiegu X6100
#
# A Linux with what Buildroot 2023.02 needs, to build the 1KO125 SD card on
# a Mac (OrbStack or Docker Desktop) or on any PC with Docker.
# Used by make_sdcard_docker.sh; see ../README-1KO125.md.
#
# Ubuntu 22.04, as the GitHub workflow: its CMake (3.22) is accepted by the
# old Buildroot packages.
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive \
    LANG=C.UTF-8 \
    LC_ALL=C.UTF-8 \
    GIT_TERMINAL_PROMPT=0

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential gcc g++ make cmake \
        git ca-certificates wget rsync \
        bc cpio file patch perl unzip bzip2 xz-utils gzip \
        python3 python3-distutils python3-setuptools \
        libncurses-dev libssl-dev \
    && rm -rf /var/lib/apt/lists/* \
    && git config --system --add safe.directory '*'

# Buildroot must not run as root. The folders that become Docker volumes are
# made here, so the volumes belong to this user.
RUN useradd -m -u 1000 builder \
    && mkdir -p /work/AetherX6100Buildroot /work/out /home/builder/.buildroot-ccache \
    && chown -R builder:builder /work /home/builder/.buildroot-ccache

USER builder
WORKDIR /work
