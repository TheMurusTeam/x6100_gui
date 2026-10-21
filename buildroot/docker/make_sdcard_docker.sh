#!/bin/bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 Hany El Imam - 1KO125 firmware for the Xiegu X6100
#
# Builds the 1KO125 SD card inside a Docker container: on a Mac with
# OrbStack or Docker Desktop, or on any PC with Docker.
#
#   x6100_gui/buildroot/docker/make_sdcard_docker.sh [OUTPUT_FOLDER]
#
# The container runs ../make_sdcard.sh on this x6100_gui folder (shared
# with the container, so it can be edited on the computer). Buildroot and
# its build outputs (about 20 GB) stay in Docker volumes, inside the Linux
# disk of Docker: fast, and with case-sensitive file names. The image is
# written to OUTPUT_FOLDER (default: the folder that contains x6100_gui).
#
# The build runs in the background: Ctrl+C closes only the log view.
#   docker logs -f x6100-1ko125-build     shows the log again
#   docker stop x6100-1ko125-build        stops the build
#   docker volume rm x6100-1ko125-buildroot-XXXXXXX
#                                         deletes Buildroot and its outputs
#
# Each Buildroot commit has its own volume (XXXXXXX = the first 7 digits of
# BR_COMMIT in make_sdcard.sh), so branches on different Buildroots do not
# spoil each other's builds. The ccache volume is shared.
set -euo pipefail

NAME=x6100-1ko125-build
GUI=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${1:-$(dirname "$GUI")}
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
BR_COMMIT=$(sed -n 's/^BR_COMMIT=//p' "$GUI/buildroot/make_sdcard.sh")
BR_VOL=x6100-1ko125-buildroot-${BR_COMMIT:0:7}

if [ -n "$(docker ps -q -f "name=^${NAME}$")" ]; then
    echo "A build is already running. Its log: docker logs -f $NAME"
    exit 1
fi
docker rm "$NAME" >/dev/null 2>&1 || true

docker build -q -t "$NAME" "$GUI/buildroot/docker" >/dev/null

docker run -d --name "$NAME" \
    -v "$GUI":/work/x6100_gui \
    -v "$BR_VOL":/work/AetherX6100Buildroot \
    -v x6100-1ko125-ccache:/home/builder/.buildroot-ccache \
    -v "$OUT":/work/out \
    "$NAME" /work/x6100_gui/buildroot/make_sdcard.sh /work/out >/dev/null

echo "Buildroot volume: $BR_VOL"
echo "Build started; the image will be written to $OUT"
echo "Ctrl+C closes this view, the build goes on (docker logs -f $NAME shows it again)"
docker logs -f "$NAME"
