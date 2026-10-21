#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Hany El Imam - 1KO125 firmware for the Xiegu X6100
#
# Builds the 1KO125 SD card image on a Linux PC, the same way as the GitHub
# workflow (.github/workflows/main.yml):
#
#   AetherX6100Buildroot at the pinned commit
#   + the 1KO125 package patches (buildroot/patches/)
#   + this x6100_gui folder, built by Buildroot as the x6100_gui package
#   + the R1CBU U-Boot (u-boot/u-boot-sunxi-with-spl.bin)
#
# Usage, from anywhere:
#
#   x6100_gui/buildroot/make_sdcard.sh [OUTPUT_FOLDER]
#
# AetherX6100Buildroot is cloned next to x6100_gui the first time (the same
# layout as conf.sh). The first build takes hours; the next ones rebuild
# only the GUI and the image.
set -euo pipefail

# Buildroot 2023.02 uses the CMake of the PC, and some of its packages ask
# for a CMake older than 3.5, which CMake 4 refuses. Use a CMake 3 when there
# is one in /opt, and let CMake 4 accept those packages anyway.
for d in /opt/cmake-3*/bin; do
    [ -x "$d/cmake" ] && PATH="$d:$PATH" && break
done
export CMAKE_POLICY_VERSION_MINIMUM=3.5

BR_REPO=https://github.com/gdyuldin/AetherX6100Buildroot
BR_COMMIT=07a8d250f420544949887544432dbfc7565b77f0

GUI=$(cd "$(dirname "$0")/.." && pwd)
BR=$(dirname "$GUI")/AetherX6100Buildroot
OUT=${1:-$(dirname "$GUI")}

# 1. Buildroot at the pinned commit
if [ ! -d "$BR/.git" ]; then
    # $BR may exist and be empty (a Docker volume)
    git clone "$BR_REPO" "$BR"
    git -C "$BR" checkout -q "$BR_COMMIT"
    git -C "$BR" submodule update --init
fi
if [ "$(git -C "$BR" rev-parse HEAD)" != "$BR_COMMIT" ]; then
    echo "WARNING: $BR is not at commit $BR_COMMIT"
    echo "         the image may differ from the released ones"
fi

# 2. The 1KO125 package patches
cp -rv "$GUI/buildroot/patches/." "$BR/br2_external/package/"

# 3. Configure once, and build the GUI from this folder
if [ ! -f "$BR/build/.config" ]; then
    (cd "$BR" && bash ./br_config.sh)
fi
echo "X6100_GUI_OVERRIDE_SRCDIR = $GUI" > "$BR/build/local.mk"

cd "$BR/build"
if [ ! -f images/sdcard.img ]; then
    # first build: the same order as the GitHub workflow
    make lame
    make mpg123
    make libsndfile-dirclean
    make
else
    make x6100_gui-rebuild x6100_webserver-dirclean all
fi

# 4. The R1CBU U-Boot, written at 8 KiB
VERSION=$(git -C "$GUI" describe --tags --always 2>/dev/null || echo local)
IMG="$OUT/sdcard-1KO125-$VERSION-$(date +%Y%m%d-%H%M).img"
cp images/sdcard.img "$IMG"
dd if="$GUI/u-boot/u-boot-sunxi-with-spl.bin" of="$IMG" bs=1024 seek=8 conv=notrunc
grep -q "U-Boot SPL" "$IMG" || { echo "ERROR: U-Boot not found in $IMG"; exit 1; }

echo
echo "SD card image: $IMG"
