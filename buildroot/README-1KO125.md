# Building the 1KO125 SD card

The 1KO125 SD card image is made of:

| Part | Where |
|---|---|
| Linux, root file system, libraries | [AetherX6100Buildroot](https://github.com/gdyuldin/AetherX6100Buildroot) at commit `07a8d250f420544949887544432dbfc7565b77f0` |
| 1KO125 patches to Buildroot packages | `patches/` (the SSTV page of the web server) |
| The GUI | this repository, built by Buildroot as the `x6100_gui` package |
| U-Boot | `../u-boot/u-boot-sunxi-with-spl.bin` (R1CBU), written at 8 KiB |

The radio keeps its original Xiegu BASE firmware: the SD card does not
change it.

## On GitHub

The workflow `.github/workflows/main.yml` builds the image:

- **a tag** (e.g. `1.0.2-beta6`) builds it and attaches
  `sdcard-1KO125-<tag>.img.zip` to a new release (a pre-release when the tag
  contains `beta`);
- **Actions → Build image → Run workflow** builds it from a branch and keeps
  the zip for 14 days in the run page, under *Artifacts*.

## On a Linux PC

    git clone -b 1ko125 https://github.com/TheMurusTeam/x6100_gui
    cd x6100_gui && git submodule update --init && cd ..
    x6100_gui/buildroot/make_sdcard.sh

`make_sdcard.sh` clones AetherX6100Buildroot next to `x6100_gui` at the
pinned commit, adds the patches, builds everything and writes
`sdcard-1KO125-<version>-<date>.img` next to `x6100_gui` (or in the folder
given as argument). It needs the
[Buildroot host packages](https://buildroot.org/downloads/manual/manual.html#requirement).
The first build takes hours; the next ones rebuild only the GUI and the
image.

## On a Mac (or any PC with Docker)

With [OrbStack](https://orbstack.dev) or Docker Desktop:

    git clone -b 1ko125 https://github.com/TheMurusTeam/x6100_gui
    cd x6100_gui && git submodule update --init && cd ..
    x6100_gui/buildroot/docker/make_sdcard_docker.sh

It builds a small Ubuntu 22.04 image (`docker/Dockerfile`) and runs
`make_sdcard.sh` in it. The code stays in your `x6100_gui` folder, where
you edit it; Buildroot and its outputs (about 20 GB) stay in two Docker
volumes, `x6100-1ko125-buildroot` and `x6100-1ko125-ccache`. The image is
written next to `x6100_gui` (or in the folder given as argument).

`build.sh` (R1CBU) is still here: it builds only the GUI binary against an
existing Buildroot, for quick tests.
