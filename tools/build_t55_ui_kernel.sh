#!/usr/bin/env bash
# Rebuild only the UI kernel, without the product build tree or Docker volumes.
set -euo pipefail
root=$(realpath "$(dirname "$0")/..")
base=$(realpath "${1:?pinned SPIBone baseline zImage}")
source=$(realpath "${2:?linux-6.6.30.tar.xz}")
driver=$(realpath "${3:?reviewed SPIBone driver source directory}")
out=$(realpath -m "${4:?new kernel output directory}")
export ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- LC_ALL=C TZ=UTC
export KBUILD_BUILD_TIMESTAMP='Thu Jan 1 00:00:00 UTC 2026'
export KBUILD_BUILD_USER=openwing KBUILD_BUILD_HOST=builder KBUILD_BUILD_VERSION=1

printf '%s  %s\n' "$(awk '$2 == "linux-6.6.30.tar.xz" {print $1}' "$root/configs/t55-build.SHA256SUMS")" "$source" | sha256sum --strict -c
(cd "$driver" && sha256sum --strict -c <<'SUMS'
4b8918d13f5eff21c901fed8eb1d9702e58daf6340457c49d5f1311fc77e35d6  spibone.c
26a01099acbef38b1448291e8ab0a4dec498c0963036c718f6f61a81e9882dcd  spibone.h
ce3e41a488b0cf88b53303adc6f1d0bbd1facb4481f796f1bcba4aa462da6fc5  spibone_uapi.h
SUMS
)
mkdir "$out"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
tar -xf "$source" -C "$tmp"
cd "$tmp/linux-6.6.30"
scripts/extract-ikconfig "$base" > .config
mkdir drivers/misc/spibone
cp "$root"/configs/spibone/{Kconfig,Kbuild} "$driver"/spibone{.c,.h,_uapi.h} drivers/misc/spibone/
printf '\nsource "drivers/misc/spibone/Kconfig"\n' >> drivers/misc/Kconfig
printf '\nobj-$(CONFIG_SPIBONE) += spibone/\n' >> drivers/misc/Makefile
scripts/config --enable INPUT_EVDEV --enable INPUT_MISC --enable INPUT_UINPUT --enable HID_MULTITOUCH --enable DRM_ETNAVIV
make olddefconfig
for opt in IKCONFIG SPIBONE SERIAL_IMX DEVTMPFS INPUT_EVDEV INPUT_UINPUT HID_MULTITOUCH DRM_ETNAVIV; do
    grep -Fx "CONFIG_$opt=y" .config
done
make -j"${JOBS:-$(nproc)}" zImage
grep -w spibone_probe System.map
grep -w uinput_misc_init System.map
cp arch/arm/boot/zImage .config System.map "$out/"
scripts/extract-ikconfig "$out/zImage" | cmp "$out/.config" -
(cd "$out" && sha256sum zImage .config System.map > SHA256SUMS)
