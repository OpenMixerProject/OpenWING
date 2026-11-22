#!/usr/bin/env bash
set -euo pipefail
umask 022
export LC_ALL=C TZ=UTC ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf-
export SOURCE_DATE_EPOCH=1767225600 KBUILD_BUILD_USER=openwing KBUILD_BUILD_HOST=builder KBUILD_BUILD_VERSION=1
export KBUILD_BUILD_TIMESTAMP="$(date -u -d "@$SOURCE_DATE_EPOCH")"
diag=/src/tools/wing-t55-diag
cd /work
(cd /archives && sha256sum --strict -c "$diag/sources.sha256")
while read -r _ archive; do tar -xf "/archives/$archive"; done < "$diag/sources.sha256"
jobs=$(nproc)

make -C linux-6.6.30 -j"$jobs" O=/work/kernel mrproper
cp /src/configs/config_linux kernel/.config
cp "$diag/wing-t55-diag.dts" linux-6.6.30/arch/arm/boot/dts/nxp/imx/
linux-6.6.30/scripts/config --file kernel/.config \
    --disable SPI_SPIDEV --disable DEVMEM --disable DEVKMEM --disable MODULES
make -C linux-6.6.30 -j"$jobs" O=/work/kernel olddefconfig
make -C linux-6.6.30 -j"$jobs" O=/work/kernel zImage nxp/imx/wing-t55-diag.dtb &
kernel_pid=$!

(
    cd u-boot-2024.01
    patch -p1 < /src/patches/uboot/behringer-wing-imx6s.patch
    cp /src/configs/uboot/mx6d_linux_defconfig configs/
    make mx6d_linux_defconfig
    scripts/config --disable CMD_SF --disable CMD_MMC --disable CMD_USB_MASS_STORAGE
    make olddefconfig
    make -j"$jobs" u-boot-dtb.imx
)

(
    cd busybox-1.38.0
    make allnoconfig
    awk -F '[ =]' 'NR==FNR { enable[$1]=$0; next }
        !($1 in enable) && !($1 == "#" && $2 in enable) { print }
        END { for (name in enable) print enable[name] }' "$diag/busybox.config" .config > .config.diag
    mv .config.diag .config
    make oldconfig </dev/null
    while read -r line; do grep -Fxq "$line" .config; done < "$diag/busybox.config"
    make -j"$jobs"
)

wait "$kernel_pid"
install -d rootfs/{bin,dev,proc,sys,run,etc}
install -m 0755 busybox-1.38.0/busybox rootfs/bin/busybox
for applet in sh cat mount readlink sleep timeout tr uname; do
    ln -s busybox "rootfs/bin/$applet"
done
install -m 0755 "$diag/init" rootfs/init
install -m 0755 "$diag/wing-t55-diag" rootfs/bin/wing-t55-diag
# Bind the diagnostic build ID to the actual kernel, DT, U-Boot and rootfs inputs.
kernel=kernel/arch/arm/boot/zImage
dtb=kernel/arch/arm/boot/dts/nxp/imx/wing-t55-diag.dtb
uboot=u-boot-2024.01/u-boot-dtb.imx
sha256sum "$kernel" "$dtb" "$uboot" rootfs/bin/busybox rootfs/init rootfs/bin/wing-t55-diag \
    | sha256sum | cut -d ' ' -f 1 > rootfs/etc/wing-diag-build
find rootfs -type f -exec touch -d "@$SOURCE_DATE_EPOCH" {} +
# gen_init_cpio adds the initial /dev/console without needing container mknod privileges.
(
    cd rootfs
    find . -mindepth 1 -type d | sort | sed 's/^\.\(.*\)/dir \1 0755 0 0/'
    find . -type f | sort | while read -r file; do
        mode=$(stat -c %a "$file")
        printf 'file %s %s 0%s 0 0\n' "${file#.}" "$file" "$mode"
    done
    find . -type l | sort | while read -r file; do
        printf 'slink %s %s 0777 0 0\n' "${file#.}" "$(readlink "$file")"
    done
    echo 'nod /dev/console 0600 0 0 c 5 1'
    echo 'nod /dev/null 0666 0 0 c 1 3'
) > initramfs.list
(cd rootfs && ../kernel/usr/gen_init_cpio -t "$SOURCE_DATE_EPOCH" ../initramfs.list) | gzip -n9 > initramfs.cpio.gz

name=wing-compact-t55-diag.wingfw
stage=$(mktemp -d /out/.t55-diag.XXXXXX)
trap 'rm -rf -- "$stage"' EXIT
PYTHONDONTWRITEBYTECODE=1 python3 /src/tools/build_linux_wingfw.py \
    --uboot-imx "$uboot" --kernel "$kernel" --dtb "$dtb" --initramfs initramfs.cpio.gz \
    --fit-output "$stage/wing-t55-diag.itb" --version linux-t55-diag \
    --cdate "$(date -u -d "@$SOURCE_DATE_EPOCH" +%Y-%m-%d_%H:%M:%S)" -o "$stage/$name"
cp initramfs.cpio.gz "$stage/wing-t55-diag-initramfs.cpio.gz"
cp rootfs/etc/wing-diag-build "$stage/build-id"
cd "$stage"
sha256sum "$name" > "$name.sha256"
PYTHONDONTWRITEBYTECODE=1 python3 "$diag/verify.py" "$name" > verification.log
cat verification.log
mv -- * /out/
