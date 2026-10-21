# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Hany El Imam - 1KO125 firmware for the Xiegu X6100
#
# A Linux with what Buildroot 2023.02 needs, to build the 1KO125 SD card on
# a Mac (OrbStack or Docker Desktop) or on any PC with Docker.
# Used by make_sdcard_docker.sh; see ../README-1KO125.md.
#
# Ubuntu 22.04, as the GitHub workflow. CMake must be 3.23 or newer
# (aether-x6100-control) but older than 4 (old Buildroot packages such as
# libical): Ubuntu has 3.22, so a CMake 3.31 from Kitware goes in /opt,
# as on the GitHub runner. sqlite3 makes params.default.db (sql/).
FROM ubuntu:22.04

ARG CMAKE_VERSION=3.31.8

ENV DEBIAN_FRONTEND=noninteractive \
    LANG=C.UTF-8 \
    LC_ALL=C.UTF-8 \
    GIT_TERMINAL_PROMPT=0

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential gcc g++ make \
        git ca-certificates wget rsync \
        bc cpio file patch perl unzip bzip2 xz-utils gzip \
        python3 python3-distutils python3-setuptools \
        libncurses-dev libssl-dev \
        sqlite3 \
    && rm -rf /var/lib/apt/lists/* \
    && git config --system --add safe.directory '*'

RUN wget -q -O /tmp/cmake.tar.gz \
        https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-$(uname -m).tar.gz \
    && mkdir -p /opt/cmake-${CMAKE_VERSION} \
    && tar -xzf /tmp/cmake.tar.gz -C /opt/cmake-${CMAKE_VERSION} --strip-components=1 \
    && rm /tmp/cmake.tar.gz
ENV PATH=/opt/cmake-${CMAKE_VERSION}/bin:$PATH

# Buildroot must not run as root. The folders that become Docker volumes are
# made here, so the volumes belong to this user.
RUN useradd -m -u 1000 builder \
    && mkdir -p /work/AetherX6100Buildroot /work/out /home/builder/.buildroot-ccache \
    && chown -R builder:builder /work /home/builder/.buildroot-ccache

USER builder
WORKDIR /work
