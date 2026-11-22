#!/usr/bin/env bash
# Package the UI against the reviewed 0x2bcf0718 SPIBone package. Linux build host.
set -euo pipefail
root=$(realpath "$(dirname "$0")/..")
base=$(realpath "${1:?baseline output directory}")
kernel_output=$(realpath "${2:?output of build_t55_ui_kernel.sh}")
kernel=$kernel_output/zImage
ui=$(realpath "${3:?ARMv7 openwing-ui (linuxkms/femtovg)}")
omc=$(realpath "${4:?directory containing static ARMv7 omc, SHA256SUMS and provenance.txt}")
runtime=$(realpath "${5:?ARMv7 EGL/GBM/Etnaviv runtime root}")
out=${6:?new output directory (must not exist)}
export LC_ALL=C TZ=UTC SOURCE_DATE_EPOCH=1767225600

base_fw=$base/wing-compact-t55-spibone.wingfw
echo "925a5f7fd70c66ade501cadd175faea2ff0ad57920bcfc5758e893edce7044f4  $base_fw" | sha256sum --strict -c
mkdir "$out" # don't overwrite an existing build
out=$(realpath "$out")
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

(cd "$kernel_output" && sha256sum --strict -c SHA256SUMS)
for opt in SPIBONE SERIAL_IMX DEVTMPFS INPUT_EVDEV INPUT_UINPUT HID_MULTITOUCH DRM_ETNAVIV; do
    grep -Fxq "CONFIG_$opt=y" "$kernel_output/.config"
done
arm-linux-gnueabihf-gcc -mcpu=cortex-a9 -Os -static -Wall -Wextra -Werror \
    "$root/software/wing-system-tools/wing-touch-uinput.c" -o "$tmp/wing-touch-uinput"
for exe in "$ui" "$omc/omc" "$tmp/wing-touch-uinput"; do
    elf=$(arm-linux-gnueabihf-readelf -h -l -d "$exe")
    grep -q 'ELF32' <<<"$elf"
    grep -E -q 'Machine:[[:blank:]]+ARM$' <<<"$elf"
    grep -q 'hard-float ABI' <<<"$elf"
    if [[ $exe != "$ui" ]] && grep -Eq 'INTERP|NEEDED' <<<"$elf"; then
        echo "Unexpected dynamic dependency: $exe" >&2; exit 1
    fi
done
test -e "$runtime/lib/ld-linux-armhf.so.3"
test -e "$runtime/usr/lib/arm-linux-gnueabihf/gbm/dri_gbm.so"

unpack_fit() {
    python3 "$root/tools/wingfw_unpack.py" "$1" -o "$2"
    dd if="$2/app_0x10004000.bin" of="$2/image.itb" bs=4096 skip=197 status=none
    for i in 0 1 2; do dumpimage -T flat_dt -p "$i" -o "$2/part$i" "$2/image.itb"; done
}
unpack_fit "$base_fw" "$tmp/base" > "$out/base-unpack.log"
PYTHONPATH="$root/lib/private/tools" python3 - "$base/u-boot-linux.imx" "$tmp/base/app_0x10004000.bin" <<'PY'
import sys
from pathlib import Path
from build_uboot_wingfw import EMBEDDED_UBOOT_OFFSET, parse_imx_image

payload = parse_imx_image(Path(sys.argv[1]).read_bytes())["payload"]
with Path(sys.argv[2]).open("rb") as app:
    app.seek(EMBEDDED_UBOOT_OFFSET)
    if app.read(len(payload)) != payload:
        raise SystemExit("Baseline U-Boot RAM image differs from input .imx")
PY
cmp "$base/wing-usb-console-zImage" "$tmp/base/part0"
cmp "$base/imx6dl-wing-usb-console.dtb" "$tmp/base/part1"
cmp "$base/wing-usb-console-initramfs.cpio.gz" "$tmp/base/part2"

# Keep the reviewed DTB byte-identical: UART4 is PNLC, not a console or a
# hardware-flow-control port. wing-panel-init sets up the existing pad mux.
dtb=$tmp/base/part1
uart=$(fdtget -t s "$dtb" /aliases serial3)
[[ $uart == */serial@21f0000 ]]
[[ $(fdtget -t s "$dtb" "$uart" status) == okay ]]
[[ $(fdtget -t s "$dtb" /chosen stdout-path) != serial3* ]]
[[ $(fdtget -t s "$dtb" /chosen bootargs) != *console=ttymxc3* ]]
if fdtget -p "$dtb" "$uart" | grep -Fx 'uart-has-rtscts'; then
    echo 'Unexpected hardware flow control on PNLC UART' >&2
    exit 1
fi

python3 "$root/tools/stage_t55_ui_initramfs.py" "$tmp/base/part2" \
    "$root/initramfs_root/init" "$ui" "$omc" "$runtime" "$tmp/wing-touch-uinput" "$out/wing-usb-console-initramfs.cpio.gz"
install -m 0644 "$base/u-boot-linux.imx" "$out/u-boot-linux.imx"
install -m 0644 "$tmp/base/part1" "$out/imx6dl-wing-usb-console.dtb"
install -m 0644 "$kernel" "$out/wing-usb-console-zImage"
fw=$out/wing-compact-t55-spibone-ui-touch.wingfw
python3 "$root/tools/build_linux_wingfw.py" --uboot-imx "$out/u-boot-linux.imx" \
    --kernel "$out/wing-usb-console-zImage" --dtb "$out/imx6dl-wing-usb-console.dtb" \
    --initramfs "$out/wing-usb-console-initramfs.cpio.gz" --fit-output "$out/wing-usb-console.itb" \
    --version linux-t55-spibone-ui-touch --cdate 2026-01-01_00:00:00 -o "$fw"
python3 "$root/tools/wingfw_verify.py" "$fw" > "$out/wingfw-verify.log"
grep -q 'Overall.*PASS' "$out/wingfw-verify.log"
unpack_fit "$fw" "$tmp/new" > "$out/wingfw-unpack.log"
cmp -n $((197 * 4096)) "$tmp/base/app_0x10004000.bin" "$tmp/new/app_0x10004000.bin"
cmp -n "$(stat -c %s "$out/wing-usb-console.itb")" "$out/wing-usb-console.itb" "$tmp/new/image.itb"
cmp "$kernel" "$tmp/new/part0"
cmp "$tmp/base/part1" "$tmp/new/part1"
cmp "$out/wing-usb-console-initramfs.cpio.gz" "$tmp/new/part2"
(cd "$out" && sha256sum ./*.wingfw ./*.itb ./*.cpio.gz ./*.dtb ./*zImage ./u-boot-linux.imx > SHA256SUMS)
